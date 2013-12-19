#include <apr_thread_cond.h>
#include <apr_atomic.h>
#include "rpc/rpc.h"
#include "mpaxos.pb-c.h"

#define N_RPC (1000000)

static apr_pool_t *mp_rpc_ = NULL;
static apr_thread_cond_t *cd_rpc_ = NULL;
static apr_thread_mutex_t *mx_rpc_ = NULL;
static volatile apr_uint32_t ct_rpc_ = 0;
static char* addr;
static int port;

funid_t ADD = 1;
funid_t PROTO = 2;
static apr_time_t tm_begin_ = 0;
static apr_time_t tm_end_ = 0;

static uint32_t n_client_;

typedef struct {
    uint32_t a;
    uint32_t b;
} struct_add;

rpc_state* add(rpc_state *in) {
    LOG_DEBUG("server received rpc request.\n");
    struct_add *sa = (struct_add *)in->buf;
    uint32_t c = sa->a + sa->b;
    rpc_state *out = (rpc_state *)malloc(sizeof(rpc_state));    
    out->buf = (uint8_t*)malloc(sizeof(uint32_t));
    out->sz = sizeof(uint32_t);
    memcpy(out->buf, &c, sizeof(uint32_t));
    return out;
}

rpc_state* add_cb(rpc_state *in) {
    // Do nothing
    LOG_DEBUG("client callback exceuted.\n");
    uint32_t j = apr_atomic_inc32(&ct_rpc_);

    if (j + 1 == N_RPC * n_client_) {
        printf("hahaha\n");
        tm_end_ = apr_time_now();
        apr_thread_mutex_lock(mx_rpc_);
        apr_thread_cond_signal(cd_rpc_);
        apr_thread_mutex_unlock(mx_rpc_);
    }
    return NULL;
}

//static rpc_state* on_accept() {
//
//}

void bench_proto() {
//    for (int i = 0; i < N_RPC; i++) {
//        // keep sending accept    
//        msg_accept_t msg_accp = MPAXOS__MSG_ACCEPT__INIT;
//        msg_header_t header = MPAXOS__MSG_HEADER__INIT;
//        msg_accp.h = &header;
//        msg_accp.h->t = MPAXOS__MSG_HEADER__MSGTYPE_T__ACCEPT;
//        msg_accp.h->tid = 0;
//        msg_accp.h->nid = 0;
//        proposal_t prop = MPAXOS__PROPOSAL__INIT;
//        msg_accp.prop = &prop;
//
//        size_t sz_msg = mpaxos__msg_accept__get_packed_size (&msg_accp);
//        char *buf = (char *)malloc(sz_msg);
//        mpaxos__msg_accept__pack(&msg_accp, (uint8_t *)buf);
//
//        free(buf);
//        client_call(client, PROTO, (uint8_t *)buf, sz_msg);
//    }
}

void bench_add() {
}

void* APR_THREAD_FUNC client_thread(apr_thread_t *th, void *v) {
    client_t *client = NULL;
    client_create(&client);
    strcpy(client->com.ip, addr);
    client->com.port = port;
    client_regfun(client, ADD, add_cb);
    tm_begin_ = apr_time_now();
    client_connect(client);
//    printf("client connected.\n");
    for (int i = 0; i < N_RPC; i++) {
        struct_add sa;
        sa.a = 1;
        sa.b = 2;
        client_call(client, ADD, (uint8_t *)&sa, sizeof(struct_add));
    }
    apr_thread_exit(th, APR_SUCCESS);
    return NULL;
}

void sig_handler(int signo) {
    char *s = strsignal(signo);
    printf("received signal. type: %s\n", s);
    if (signo == SIGINT) {
        exit(0);
    } else if (signo == SIGABRT) {
        exit(0);
    } else {
        //do nothing.
    }
}

int main(int argc, char **argv) {
    for (int i=1; i<NSIG; i++) {
        //if (signal(SIGINT, sig_handler) == SIG_ERR) printf("\ncan't catch SIGINT\n");
        if (signal(i, sig_handler) == SIG_ERR) printf("\ncan't catch SIGINT\n");
    }
    signal(SIGPIPE, SIG_IGN);

    apr_initialize();
    apr_pool_create(&mp_rpc_, NULL);
    apr_thread_cond_create(&cd_rpc_, mp_rpc_);
    apr_thread_mutex_create(&mx_rpc_, APR_THREAD_MUTEX_UNNESTED, mp_rpc_);
    rpc_init();
    
    char *s_or_c = argv[1];
    addr = argv[2];
    port = atoi(argv[3]);
    if (strcmp(s_or_c, "server") == 0) {
        server_t *server = NULL;
        server_create(&server);    
        strcpy(server->com.ip, addr);
        server->com.port = port;
        server_regfun(server, ADD, add); 
        server_start(server);
        printf("server started.\n");
        while (1) {
            sleep(1);
        }
    } else if (strcmp(s_or_c, "client") == 0) {
        n_client_ = atoi(argv[4]);
        
        apr_thread_mutex_lock(mx_rpc_);
        for (int i = 0; i < n_client_; i++) {
            apr_thread_t *th;
            apr_thread_create(&th, NULL, client_thread, NULL, mp_rpc_);
        }
        printf("rpc triggered for %d adds on %d threads.\n", N_RPC * n_client_, n_client_);
        
        apr_thread_cond_wait(cd_rpc_, mx_rpc_);
        apr_thread_mutex_unlock(mx_rpc_);

        int period = (tm_end_ - tm_begin_) / 1000;
        printf("finish %d rpc in %d ms.\n", N_RPC * n_client_, period);
        float rate = (float) N_RPC * n_client_ / 1000 / period;
        printf("rpc rate %f million per sec.\n", rate);
    }
    printf("lalala\n");
    rpc_destroy();
    atexit(apr_terminate);
}

