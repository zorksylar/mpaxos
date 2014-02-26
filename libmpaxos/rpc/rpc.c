#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <apr_thread_proc.h>
#include <apr_network_io.h>
#include <apr_poll.h>
#include <errno.h>
#include <inttypes.h>
#include "rpc.h"
#include "utils/logger.h"
#include "utils/safe_assert.h"
#include "utils/mpr_thread_pool.h"
#include "utils/mpr_hash.h"

#define MAX_ON_READ_THREADS 1
#define POLLSET_NUM 100
#define SZ_POLLSETS 4

static apr_pool_t *mp_rpc_ = NULL; 
static server_t *server_ = NULL;
static apr_pollset_t *pollsets_[SZ_POLLSETS];
static apr_thread_t *ths_poll_[SZ_POLLSETS];

static int send_buf_size = 0;
static socklen_t optlen;
static volatile int exit_ = 0;
//static apr_thread_pool_t *tp_on_read_;
static mpr_thread_pool_t *tp_read_;

// for statistics
static apr_time_t time_start_ = 0;
static apr_time_t time_last_ = 0;
static apr_time_t time_curr_ = 0;
static uint64_t sz_data_ = 0;
static uint64_t sz_last_ = 0;
static uint32_t n_data_recv_ = 0;
static uint32_t sz_data_sent_ = 0;
static uint32_t sz_data_tosend_ = 0;
static uint32_t n_data_sent_ = 0;

static uint32_t init_ = 0;

void rpc_init() {
    apr_initialize();
    apr_pool_create(&mp_rpc_, NULL);
    
    for (int i = 0; i < SZ_POLLSETS; i++) {
        apr_pollset_create(&pollsets_[i], POLLSET_NUM, mp_rpc_, APR_POLLSET_THREADSAFE);
        apr_thread_create(&ths_poll_[i], NULL, start_poll, (void*)pollsets_[i], mp_rpc_);
    }
    init_ = 1;
}

void rpc_destroy() {
    while (init_ == 0) {
        // not started.
    }
    exit_ = 1;
    LOG_DEBUG("recv server ends.");
/*
*/
    apr_status_t status = APR_SUCCESS;
//    apr_thread_join(&status, th_poll_);
    for (int i = 0; i < SZ_POLLSETS; i++) {
        apr_thread_join(&status, ths_poll_[i]);
        apr_pollset_destroy(pollsets_[i]);
    }

    apr_pool_destroy(mp_rpc_);
    atexit(apr_terminate);
}

apr_pollset_t* next_pollset() {
    static volatile apr_uint32_t mark = 0;
    uint32_t i = apr_atomic_inc32(&mark);
    i %= SZ_POLLSETS;
    LOG_TRACE("next pollset %i", i);
    return pollsets_[i];
}

void client_create(client_t** c) {
    *c = (client_t *)malloc(sizeof(client_t));
    client_t *client = *c;
    apr_pool_create(&client->com.mp, NULL);
    context_t *ctx = context_gen(&(*c)->com);
    client->ctx = ctx;
    memset(client->com.ip, 0, 100);
    mpr_hash_create_ex(&(*c)->com.ht, 0);
}

void client_destroy(client_t* c) {
    // FIXME close the socket if not.
    context_destroy(c->ctx);
    free(c);
}

void server_create(server_t** s) {
    *s = (server_t*)malloc(sizeof(server_t));
    server_t *server = *s;

    apr_status_t status = APR_SUCCESS;
    apr_pool_create(&server->com.mp, NULL);
    mpr_hash_create_ex(&server->com.ht, 0);
    // FIXME a fixed size is wrong
    server->sz_ctxs = 0; 
    server->ctxs = apr_pcalloc(server->com.mp, sizeof(context_t **) * 100);
    
    context_t *ctx = context_gen(&(*s)->com);
    (*s)->ctx = ctx;
}

void server_destroy(server_t *s) {
    mpr_hash_destroy(s->com.ht);
    //for (int i = 0; i < s->sz_ctxs; i++) {
    //    context_t *ctx = s->ctxs[i];
    //    context_destroy(ctx);
    //}
    context_destroy(s->ctx);
    apr_pool_destroy(s->com.mp);
    free(s);
}

void server_regfun(server_t *s, funid_t fid, void* fun) {
    LOG_TRACE("server regisger function, %x", fun);
    mpr_hash_set(s->com.ht, &fid, sizeof(funid_t), &fun, sizeof(void*)); 
}

void client_regfun(client_t *c, funid_t fid, void *fun) {
    LOG_TRACE("client regisger function, %x", fun);
    mpr_hash_set(c->com.ht, &fid, sizeof(funid_t), &fun, sizeof(void*)); 
}


void server_bind_listen(server_t *r) {
    apr_sockaddr_info_get(&r->com.sa, NULL, APR_INET, r->com.port, 0, r->com.mp);
/*
    apr_socket_create(&r->s, r->sa->family, SOCK_DGRAM, APR_PROTO_UDP, r->pl_recv);
*/
    apr_socket_create(&r->com.s, r->com.sa->family, SOCK_STREAM, APR_PROTO_TCP, r->com.mp);
    apr_socket_opt_set(r->com.s, APR_SO_NONBLOCK, 1);
    apr_socket_timeout_set(r->com.s, -1);
    /* this is useful for a server(socket listening) process */
    apr_socket_opt_set(r->com.s, APR_SO_REUSEADDR, 1);
    apr_socket_opt_set(r->com.s, APR_TCP_NODELAY, 1);
    
    apr_status_t status = APR_SUCCESS;
    status = apr_socket_bind(r->com.s, r->com.sa);
    if (status != APR_SUCCESS) {
        LOG_ERROR("cannot bind.");
        printf("%s", apr_strerror(status, malloc(100), 100));
        SAFE_ASSERT(status == APR_SUCCESS);
    }
    status = apr_socket_listen(r->com.s, 10000); // This is important!
    if (status != APR_SUCCESS) {
        LOG_ERROR("cannot listen.");
        printf("%s", apr_strerror(status, malloc(100), 100));
        SAFE_ASSERT(status == APR_SUCCESS);
    }

}

context_t *context_gen(rpc_common_t *com) {
    context_t *ctx = malloc(sizeof(context_t));
    ctx->buf_recv.buf = malloc(BUF_SIZE__);
    ctx->buf_recv.sz = BUF_SIZE__;
    ctx->buf_recv.offset_begin = 0;
    ctx->buf_recv.offset_end = 0;
    
    ctx->buf_send.sz = BUF_SIZE__;
    ctx->buf_send.buf = calloc(BUF_SIZE__, 1);
    ctx->buf_send.offset_begin = 0;
    ctx->buf_send.offset_end = 0;
    ctx->com = com;
    ctx->ps = next_pollset();
    ctx->n_rpc = 0;
    ctx->sz_recv = 0;
    ctx->sz_send = 0;
    //ctx->on_recv = on_recv;
   
    apr_pool_create(&ctx->mp, NULL);
    apr_thread_mutex_create(&ctx->mx, APR_THREAD_MUTEX_UNNESTED, ctx->mp);
    return ctx;
}

void context_destroy(context_t *ctx) {
    apr_pool_destroy(ctx->mp);
    SAFE_ASSERT(ctx->buf_recv.buf != NULL);
    SAFE_ASSERT(ctx->buf_send.buf != NULL);
    free(ctx->buf_recv.buf);
    free(ctx->buf_send.buf);
    free(ctx);
}

void add_write_buf_to_ctx(context_t *ctx, funid_t type, 
    const uint8_t *buf, size_t sz_buf) {
    apr_thread_mutex_lock(ctx->mx);
    
//    apr_atomic_add32(&sz_data_tosend_, sizeof(funid_t) + sz_buf + sizeof(size_t));
//    apr_atomic_inc32(&n_data_sent_);
    
    // realloc the write buf if not enough.
    if (sz_buf + sizeof(size_t) + sizeof(funid_t)
        > ctx->buf_send.sz - ctx->buf_send.offset_end) {
        LOG_TRACE("remalloc sending buffer.");
        uint8_t *newbuf = malloc(BUF_SIZE__);
        memcpy(newbuf, ctx->buf_send.buf + ctx->buf_send.offset_begin, 
                ctx->buf_send.offset_end - ctx->buf_send.offset_begin);
        free(ctx->buf_send.buf);
        ctx->buf_send.buf = newbuf;
        ctx->buf_send.offset_end -= ctx->buf_send.offset_begin;
        ctx->buf_send.offset_begin = 0;
        // there is possibility that even remalloc, still not enough
        if (sz_buf + sizeof(size_t) + sizeof(funid_t)
            > ctx->buf_send.sz - ctx->buf_send.offset_end) {
            LOG_ERROR("no enough write buffer after remalloc");
            SAFE_ASSERT(0);
        }
    } else {
        SAFE_ASSERT(1);
    }
    // copy memory
    LOG_TRACE("add message to sending buffer, message size: %d", sz_buf);
     
    LOG_TRACE("size in buf:%llx, original size:%llx", 
        *(ctx->buf_send.buf + ctx->buf_send.offset_end), sz_buf + sizeof(funid_t));
    
    *(size_t*)(ctx->buf_send.buf + ctx->buf_send.offset_end) = sz_buf + sizeof(funid_t);
    ctx->buf_send.offset_end += sizeof(size_t);
    memcpy(ctx->buf_send.buf + ctx->buf_send.offset_end, &type, sizeof(funid_t));
    ctx->buf_send.offset_end += sizeof(funid_t);
    memcpy(ctx->buf_send.buf + ctx->buf_send.offset_end, buf, sz_buf);
    ctx->buf_send.offset_end += sz_buf;
    
    // change poll type
    if (ctx->pfd.reqevents == APR_POLLIN) {
        apr_pollset_remove(ctx->ps, &ctx->pfd);
        ctx->pfd.reqevents = APR_POLLIN | APR_POLLOUT;
        apr_pollset_add(ctx->ps, &ctx->pfd);
    }
    
    apr_thread_mutex_unlock(ctx->mx);
}

void reply_to(read_state_t *state) {
    add_write_buf_to_ctx(state->ctx, state->reply_msg_type,
        state->buf_write, state->sz_buf_write);
    free(state->buf_write);
}

void stat_on_write(size_t sz) {
    LOG_TRACE("sent data size: %d", sz);
    sz_data_sent_ += sz;
//    apr_atomic_sub32(&sz_data_tosend_, sz);
}

void poll_on_write(context_t *ctx, const apr_pollfd_t *pfd) {
    LOG_TRACE("write message on socket %x", pfd->desc.s);
    apr_status_t status = APR_SUCCESS;
    apr_thread_mutex_lock(ctx->mx);
    uint8_t *buf = ctx->buf_send.buf + ctx->buf_send.offset_begin;
    size_t n = ctx->buf_send.offset_end - ctx->buf_send.offset_begin;
    if (n > 0) {
        int tmp = n;
        status = apr_socket_send(pfd->desc.s, (char *)buf, &n);
        stat_on_write(n);
        ctx->buf_send.offset_begin += n;
        ctx->sz_send += n; 
        if (status == APR_SUCCESS || status == APR_EAGAIN) {
        
        } else if (status == APR_ECONNRESET) {
            LOG_ERROR("connection reset on write, is this a mac os?");
            apr_pollset_remove(ctx->ps, &ctx->pfd);
            return;
        } else if (status == APR_EAGAIN) {
            LOG_ERROR("on write, socket busy, resource temporarily unavailable.");
            // do nothing.
        } else if (status == APR_EPIPE) {
            LOG_ERROR("on write, broken pipe, epipe error, is this a mac os?");
            LOG_ERROR("rpc called %"PRIu64", data received: %"PRIu64" bytes, sent: %"PRIu64" bytes", ctx->n_rpc, ctx->sz_recv, ctx->sz_send);
            apr_pollset_remove(ctx->ps, &ctx->pfd);
            return;
        } else {
            LOG_ERROR("error code: %d, error message: %s",(int)status, apr_strerror(status, malloc(100), 100));
            LOG_ERROR("try to write %d bytes in write buffer.", tmp);
            SAFE_ASSERT(status == APR_SUCCESS);
        }
    } else if (n == 0){
        LOG_WARN("nothing to write? how so?");
    } else {
        SAFE_ASSERT(0);
    }
    
    n = ctx->buf_send.offset_end - ctx->buf_send.offset_begin;
    if (n == 0) {
        // buf empty, remove out poll.
        apr_pollset_remove(ctx->ps, &ctx->pfd);
        ctx->pfd.reqevents = APR_POLLIN;
        apr_pollset_add(ctx->ps, &ctx->pfd);
    } else {
        apr_pollset_remove(ctx->ps, &ctx->pfd);
        ctx->pfd.reqevents = APR_POLLIN | APR_POLLOUT;
        apr_pollset_add(ctx->ps, &ctx->pfd);
    }
    apr_thread_mutex_unlock(ctx->mx);
}


/**
 * not thread-safe
 * @param sz
 */
void stat_on_read(size_t sz) {
    time_curr_ = apr_time_now();
    time_last_ = (time_last_ == 0) ? time_curr_ : time_last_;
/*
    if (time_start_ == 0) {
        time_start_ = (time_start_ == 0) ? time_curr_: time_start_;
        recv_last_time = recv_start_time;
    }
*/
    sz_data_ += sz;
    sz_last_ += sz;
    apr_time_t period = time_curr_ - time_last_;
    if (period > 1000000) {
        //uint32_t n_push;
        //uint32_t n_pop;
        //mpaxos_async_push_pop_count(&n_push, &n_pop);
        float speed = (double)sz_last_ / (1024 * 1024) / (period / 1000000.0);
        //printf("%d messages %"PRIu64" bytes received. Speed: %.2f MB/s. "
        //    "Total sent count: %d,  bytes:%d, left to send: %d",// n_push:%d, n_pop:%d\n", 
        //    apr_atomic_read32(&n_data_recv_), 
        //    sz_data_, 
        //    speed, 
        //    apr_atomic_read32(&n_data_sent_), 
        //    apr_atomic_read32(&sz_data_sent_),
        //    apr_atomic_read32(&sz_data_tosend_)); 
        //    //n_push, n_pop);
        time_last_ = time_curr_;
        sz_last_ = 0;
    }
}

void poll_on_read(context_t * ctx, const apr_pollfd_t *pfd) {
    LOG_TRACE("HERE I AM, ON_READ");
    apr_status_t status = APR_SUCCESS;

    uint8_t *buf = ctx->buf_recv.buf + ctx->buf_recv.offset_end;
    size_t n = ctx->buf_recv.sz - ctx->buf_recv.offset_end;
    
    status = apr_socket_recv(pfd->desc.s, (char *)buf, &n);
//    LOG_DEBUG("finish reading socket.");
    if (status == APR_SUCCESS) {
        stat_on_read(n);
        ctx->buf_recv.offset_end += n;
        if (n == 0) {
            LOG_WARN("received an empty message.");
        } else {
            // LOG_DEBUG("raw data received.");
            // extract message.
            while (ctx->buf_recv.offset_end - ctx->buf_recv.offset_begin > sizeof(size_t)) {
                size_t sz_msg = *((size_t *)(ctx->buf_recv.buf + ctx->buf_recv.offset_begin));
                LOG_TRACE("next recv message size: %d", sz_msg);
                if (ctx->buf_recv.offset_end - ctx->buf_recv.offset_begin >= sz_msg + sizeof(size_t)) {
                    buf = ctx->buf_recv.buf + ctx->buf_recv.offset_begin + sizeof(size_t);
                    ctx->buf_recv.offset_begin += sz_msg + sizeof(size_t);

                    funid_t fid = *(funid_t*)(buf);
                    rpc_state *state = malloc(sizeof(rpc_state));
                    state->sz = sz_msg - sizeof(funid_t);
                    state->buf = malloc(sz_msg);
                    state->ctx = ctx;
                    memcpy(state->buf, buf + sizeof(funid_t), state->sz);
//                    state->ctx = ctx;
/*
                    apr_thread_pool_push(tp_on_read_, (*(ctx->on_recv)), (void*)state, 0, NULL);
//                    mpr_thread_pool_push(tp_read_, (void*)state);
*/
//                    apr_atomic_inc32(&n_data_recv_);
                    //(*(ctx->on_recv))(NULL, state);
                    // FIXME call
                    rpc_state* (**fun)(void*) = NULL;
                    size_t sz;
                    mpr_hash_get(ctx->com->ht, &fid, sizeof(funid_t), (void**)&fun, &sz);
                    SAFE_ASSERT(fun != NULL);
                    LOG_TRACE("going to call function %x", *fun);
                    ctx->n_rpc++;
                    ctx->sz_recv += n;
                    rpc_state *ret_s = (**fun)(state);
                    free(state->buf);
                    free(state);

                    if (ret_s != NULL) {
                        add_write_buf_to_ctx(ctx, fid, ret_s->buf, ret_s->sz);
                        free(ret_s->buf);
                        free(ret_s);
                    }
        
                } else {
                    break;
                }
            }

            if (ctx->buf_recv.offset_end + BUF_SIZE__ / 10 > ctx->buf_recv.sz) {
                // remalloc the buffer
                // TODO [fix] this remalloc is nothing if buf is full.
                LOG_TRACE("remalloc recv buf");
                uint8_t *buf = calloc(BUF_SIZE__, 1);
                memcpy(buf, ctx->buf_recv.buf + ctx->buf_recv.offset_begin, 
                        ctx->buf_recv.offset_end - ctx->buf_recv.offset_begin);
                free(ctx->buf_recv.buf);
                ctx->buf_recv.buf = buf;
                ctx->buf_recv.offset_end -= ctx->buf_recv.offset_begin;
                ctx->buf_recv.offset_begin = 0;
            }
        }
    } else if (status == APR_EOF) {
        LOG_INFO("on read, received eof, close socket");
        apr_pollset_remove(ctx->ps, &ctx->pfd);
        // context_destroy(ctx);
    } else if (status == APR_ECONNRESET) {
        LOG_ERROR("on read. connection reset.");
        // TODO [improve] you may retry connect
        apr_pollset_remove(ctx->ps, &ctx->pfd);
    } else if (status == APR_EAGAIN) {
        LOG_ERROR("socket busy, resource temporarily unavailable.");
        // do nothing.
    } else {
        LOG_ERROR("unkown error on poll reading. %s\n", apr_strerror(status, malloc(100), 100));
        SAFE_ASSERT(0);
    }
}

void poll_on_accept(server_t *r) {
    apr_status_t status = APR_SUCCESS;
    apr_socket_t *ns = NULL;
    status = apr_socket_accept(&ns, r->com.s, r->com.mp);
//    apr_socket_t *sock_listen = r->com.s;
//    LOG_INFO("accept on fd %x", sock_listen->socketdes);
    if (status != APR_SUCCESS) {
        LOG_ERROR("recvr accept error.");
        LOG_ERROR("%s", apr_strerror(status, calloc(100, 1), 100));
        SAFE_ASSERT(status == APR_SUCCESS);
    }
    apr_socket_opt_set(ns, APR_SO_NONBLOCK, 1);
    apr_socket_opt_set(ns, APR_TCP_NODELAY, 1);
//    apr_socket_opt_set(ns, APR_SO_REUSEADDR, 1);
    context_t *ctx = context_gen(&r->com);
    ctx->s = ns;
    apr_pollfd_t pfd = {ctx->mp, APR_POLL_SOCKET, APR_POLLIN, 0, {NULL}, NULL};
    ctx->pfd = pfd;
    ctx->pfd.desc.s = ns;
    ctx->pfd.client_data = ctx;

    // FIXME can context be managed by itself?
    //r->ctxs[r->sz_ctxs++] = ctx;
    apr_pollset_add(ctx->ps, &ctx->pfd);
    static int j = 0;
    LOG_TRACE("accepted %d connection.", ++j);
}

void* APR_THREAD_FUNC start_poll(apr_thread_t *t, void *arg) {
    apr_pollset_t *ps = arg;
    apr_status_t status = APR_SUCCESS;
    while (!exit_) {
        int num = 0;
        const apr_pollfd_t *ret_pfd;
        status = apr_pollset_poll(ps, 100 * 1000, &num, &ret_pfd);
        if (status == APR_SUCCESS) {
            if (num <=0 ) {
                LOG_ERROR("poll error. poll num le 0.");
                SAFE_ASSERT(num > 0);
            }
            for(int i = 0; i < num; i++) {
                if (ret_pfd[i].rtnevents & APR_POLLIN) {
                    if (server_ != NULL && ret_pfd[i].desc.s == server_->com.s) {
                        // new connection arrives.
                        poll_on_accept(server_);
                    } else {
                        poll_on_read(ret_pfd[i].client_data, &ret_pfd[i]);
                    }
                } 
                if (ret_pfd[i].rtnevents & APR_POLLOUT) {
                    poll_on_write(ret_pfd[i].client_data, &ret_pfd[i]);
                }
                if (!(ret_pfd[i].rtnevents & (APR_POLLOUT | APR_POLLIN))) {
                    // have no idea.
                    LOG_ERROR("see some poll event neither in or out. event:%d",
                        ret_pfd[i].rtnevents);
                    SAFE_ASSERT(0);
                }
            }
        } else if (status == APR_EINTR) {
            // the signal we get when process exit, wakeup, or add in and write.
            LOG_WARN("the receiver epoll exits?");
            continue;
        } else if (status == APR_TIMEUP) {
            // debug.
            //int c = mpr_thread_pool_task_count(tp_read_);
/*
            LOG_INFO("epoll timeout. thread pool task size: %d", c);
*/
            stat_on_read(0);
            continue;
        } else {
            LOG_ERROR("poll error. %s", apr_strerror(status, calloc(1, 100), 100));
            SAFE_ASSERT(0);
        }
    }
    SAFE_ASSERT(apr_thread_exit(t, APR_SUCCESS) == APR_SUCCESS);
    return NULL;
}

void server_start(server_t* s) {
    server_bind_listen(s);
    server_ = s;
    apr_pollfd_t pfd = {s->com.mp, APR_POLL_SOCKET, APR_POLLIN, 0, {NULL}, NULL};
    pfd.desc.s = s->com.s;
    apr_pollset_add(s->ctx->ps, &pfd);
}

void client_connect(client_t *c) {
    LOG_DEBUG("connecting to server %s %d", c->com.ip, c->com.port);
    
    apr_status_t status = APR_SUCCESS;
    status = apr_sockaddr_info_get(&c->com.sa, c->com.ip, APR_INET, c->com.port, 0, c->com.mp);
//    status = apr_sockaddr_info_get(&c->com.sa, c->com.ip, APR_UNSPEC, c->com.port, APR_IPV4_ADDR_OK, c->com.mp);
    SAFE_ASSERT(status == APR_SUCCESS);
    status = apr_socket_create(&c->com.s, c->com.sa->family, SOCK_STREAM, APR_PROTO_TCP, c->com.mp);
    SAFE_ASSERT(status == APR_SUCCESS);
    status = apr_socket_opt_set(c->com.s, APR_TCP_NODELAY, 1);
    SAFE_ASSERT(status == APR_SUCCESS);

    while (1) {
        LOG_TRACE("TCP CLIENT TRYING TO CONNECT.");
        status = apr_socket_connect(c->com.s, c->com.sa);
        if (status == APR_SUCCESS /*|| status == APR_EINPROGRESS */) {
            break;
        } else {
            LOG_ERROR("client connect error:%s", apr_strerror(status, malloc(100), 100));
//            LOG_ERROR("client addr: %s:%d", c->com.ip, c->com.port);
            continue;
        }
    }
    LOG_TRACE("connected socket on remote addr %s, port %d", c->com.ip, c->com.port);
    status = apr_socket_opt_set(c->com.s, APR_SO_NONBLOCK, 1);
    SAFE_ASSERT(status == APR_SUCCESS);
    
    // add to epoll
    context_t *ctx = c->ctx;
    while (ctx->ps == NULL) {
        // not inited yet, just wait.
    }
    ctx->s = c->com.s;
    apr_pollfd_t pfd = {ctx->mp, APR_POLL_SOCKET, APR_POLLIN, 0, {NULL}, NULL};
    ctx->pfd = pfd;
    ctx->pfd.desc.s = ctx->s;
    ctx->pfd.client_data = ctx;
   // status = apr_pollset_add(pollset_, &ctx->pfd);
    status = apr_pollset_add(ctx->ps, &ctx->pfd);
    SAFE_ASSERT(status == APR_SUCCESS);
}

void client_call(client_t *c, funid_t fid, const uint8_t *buf, size_t sz_buf) {
    add_write_buf_to_ctx(c->ctx, fid, buf, sz_buf);
}
