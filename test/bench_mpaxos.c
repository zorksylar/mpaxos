/*
 * test_mpaxos.cc
 *
 *  Created on: Jan 29, 2013
 *      Author: ms
 */

#include <stdlib.h>
#include <stdio.h>
#include <memory.h>
#include <time.h>
#include <unistd.h>
#include <json/json.h>
#include <apr_time.h>
#include <pthread.h>
#include <assert.h>
#include <inttypes.h>
#include <apr_atomic.h>
#include <apr_thread_proc.h>
#include <apr_thread_pool.h>

#include "mpaxos/mpaxos.h"
#include "mpaxos/mpaxos-config.h"

#define MAX_THREADS 100

//This macro should work for gcc version before 4.4
#define __STDC_FORMAT_MACROS
// do not include from ../libmpaxos/utils/logger.h
// it sucks, we should not include from internal library
// if you really want to do that, put the logger into the include/mpaxos directory or this directory as some header lib
// besides, that logger sucks too.
#define LOG_INFO(x, ...) printf("[%s:%d] [ II ] "x"\n", __FILE__, __LINE__, ##__VA_ARGS__)

static uint32_t group_begin_ = 1;
int async = 1;
static int t_sleep_ = 2;

// following is read from command line.
static char* ag_config_ = "./config/config.1.1";
static int ag_n_group_ = 10; 
static int ag_n_batch_ = 1;
static int ag_n_send_ = 100;
static int ag_sz_data_ = 100;
static int ag_sz_data_c_ = 0;
static int ag_quit_ = 1;

static apr_pool_t *mp_test_;
static apr_thread_pool_t *tp_test_;
static apr_thread_cond_t *cd_exit_; 
static apr_thread_mutex_t *mx_exit_;

static uint8_t* TEST_DATA = NULL;
static uint8_t* TEST_DATA_C = NULL;


static apr_uint32_t ready_to_exit = 0;

static apr_time_t time_begin_;
static apr_time_t time_end_;

static apr_uint32_t n_group_running;
static apr_uint32_t n_batch_ = 1;

static apr_uint32_t n_req_ = 0;
static apr_uint32_t n_cb_ = 0;

void test_destroy();

void exit_on_finish() {
    if (ag_quit_) {
        apr_sleep(2000000);
        LOG_INFO("Goodbye! I'm about to destroy myself.");
        fflush(stdout);
        test_destroy();
        mpaxos_destroy();
        LOG_INFO("Lalala");
        free(TEST_DATA);
        exit(0);
    } else {
        LOG_INFO("All my task is done. Go on to serve others.");

        // I want to bring all the evil with me before I go to hell.
        // cannot quit because have to serve for others
        while (true) {
            fflush(stdout);
            apr_sleep(1000000);
        }
    }
}

void stat_result() {
    double period = (time_end_ - time_begin_) / 1000000.0;
    int period_ms = (time_end_ - time_begin_) / 1000;
    uint64_t msg_count = ag_n_send_ * ag_n_group_;
    uint64_t data_count = msg_count * ag_sz_data_;
    double prop_rate = (msg_count + 0.0) / period;
    LOG_INFO("%"PRIu64" proposals commited in %dms, rate:%.2f props/s",
            msg_count, period_ms, prop_rate);
    LOG_INFO("%.2f MB data sent, speed:%.2fMB/s",
            (data_count + 0.0) / (1024 * 1024),
            (data_count + 0.0) / (1024 * 1024) /period);
}

//void cb(groupid_t* gids, size_t sz_gids, slotid_t* sids, 
//        uint8_t *data, size_t sz_data, uint8_t *data_c, size_t sz_data_c, void* para) {

void cb(mpaxos_req_t *req) {
    apr_atomic_inc32(&n_cb_);
    uint32_t n_left = (uint32_t)(uintptr_t)req->cb_para;
    if (n_left-- > 0) {
        apr_atomic_inc32(&n_req_);
        req->cb_para = (void*)(uintptr_t)n_left;
//        mpaxos_commit_raw(gids, sz_gids, TEST_DATA, ag_sz_data_, TEST_DATA_C, ag_sz_data_c_, (void*)(uintptr_t)n_left);
        mpaxos_commit_req(req);
        
    } else {
        apr_atomic_dec32(&n_group_running);
        if (apr_atomic_read32(&n_group_running) == 0) {
            time_end_ = apr_time_now();
            stat_result();
            apr_atomic_set32(&ready_to_exit, 1);

            apr_thread_mutex_lock(mx_exit_);
            apr_thread_cond_signal(cd_exit_);
            apr_thread_mutex_unlock(mx_exit_);
        }
    }
}

void test_async_start() {
    apr_thread_mutex_lock(mx_exit_);
    apr_atomic_set32(&n_group_running, ag_n_group_);
    time_begin_ = apr_time_now();
    for (int i = 0; i < ag_n_group_; i++) {
        groupid_t *gids = (groupid_t*) malloc(ag_n_batch_ * sizeof(groupid_t));
        groupid_t gid_start = (i * ag_n_batch_) + group_begin_;
        for (int j = 0; j < n_batch_; j++) {
            gids[j] = gid_start + j;
        }
        apr_atomic_inc32(&n_req_);
        LOG_INFO("trying to commit a raw request, first group id: %x", gids[0]);
        mpaxos_commit_raw(gids, n_batch_, TEST_DATA, ag_sz_data_, TEST_DATA_C, ag_sz_data_c_, (void*)(uintptr_t)(ag_n_send_-1));
   //     printf("n_tosend: %d\n", n_tosend);
    }
    
    apr_thread_cond_wait(cd_exit_, mx_exit_);
    apr_thread_mutex_unlock(mx_exit_);
    
    printf("n_req: %d, n_cb: %d\n", apr_atomic_read32(&n_req_), apr_atomic_read32(&n_cb_));
    exit_on_finish();
    //while (1) {
    //    while (!apr_atomic_read32(&ready_to_exit)) {
    //        apr_sleep(2000000);
    //        printf("n_req: %d, n_cb: %d\n", apr_atomic_read32(&n_req_), apr_atomic_read32(&n_cb_));
    //    }
    //    exit_on_finish();
    //}
}

// Do not use this
void* APR_THREAD_FUNC test_group(apr_thread_t *p_t, void *v) {
    groupid_t gid = (groupid_t)(uintptr_t)v;
    apr_time_t start_time;
    apr_time_t curr_time;

    uint64_t msg_count = 0;
    uint64_t data_count = 0;
    int val_size = 64;
    uint8_t *val = (uint8_t *) calloc(val_size, sizeof(char));

    start_time = apr_time_now();

    //Keep sending

    int n = ag_n_send_;
    do {
        if (async) {
//            commit_async(&gid, 1, val, val_size, NULL);
        } else {
//            commit_sync(&gid, 1, val, val_size);
        }
        msg_count++;
        data_count += val_size;
    } while (--n > 0);
    curr_time = apr_time_now();
    double period = (curr_time - start_time) / 1000000.0;
    double prop_rate = (msg_count + 0.0) / period;
    LOG_INFO("gid:%d, %"PRIu64" proposals commited in %.2fs, rate:%.2f props/s",
            gid, msg_count, period, prop_rate);
    LOG_INFO("%.2f MB data sent, speed:%.2fMB/s",
            (data_count + 0.0) / (1024 * 1024),
            (data_count + 0.0) / (1024 * 1024) /period);
    free(val);
    return NULL;
}

void test_init() {
    apr_initialize();
    apr_pool_create(&mp_test_, NULL);
    apr_thread_mutex_create(&mx_exit_, APR_THREAD_MUTEX_UNNESTED, mp_test_);
    apr_thread_cond_create(&cd_exit_, mp_test_);

    if (!async) {
        apr_thread_pool_create(&tp_test_, MAX_THREADS, MAX_THREADS, mp_test_);
    }
}

void test_destroy() {
    apr_thread_cond_destroy(cd_exit_);
    apr_thread_mutex_destroy(mx_exit_);
    if (!async) {
        apr_pool_destroy(mp_test_);
    }
}


void usage(char *progname) {
    fprintf(stderr, "usage: %s ", progname);
    fprintf(stderr, "[-c config_file] [-s n_send] [-g n_group] [-b n_batch] [-d sz_data_] [-e sz_data_c] [-q quit]\n");
    fprintf(stderr, "where:\n");
    fprintf(stderr, "\t-c identifies the path to configuration file, default: ./config/config.1.1\n");
    fprintf(stderr, "\t-s identifies the number of proposals to issue for each Paxos group, default: 100\n");
    fprintf(stderr, "\t-g identifies the number of Paxos groups, default: 10\n");
    fprintf(stderr, "\t-b identifies the number of Paxos groups to batch together, default: 1\n");
    fprintf(stderr, "\t-d identifies the data size in each proposal, default: 100\n");
    fprintf(stderr, "\t-e identifies the data size to be coded in each proposal, default: 0\n");
    fprintf(stderr, "\t-q identifies whether to quit after finish requests. default: 1\n");
}

int main(int argc, char **argv) {
    char ch;
    while ((ch = getopt(argc, argv, "c:s:g:b:d:e:q:h")) != EOF) {
        switch (ch) {
        case 'h':
            usage(argv[0]);
            return 0;
        case 'c':
            ag_config_ = optarg;
            break;
        case 's':
            ag_n_send_ = atoi(optarg);
            break;
        case 'g':
            ag_n_send_ = atoi(optarg);
            break;;
        case 'b':
            ag_n_batch_ = atoi(optarg);
            break;
        case 'd':
            ag_sz_data_ = atoi(optarg);
            break;
        case 'e':
            ag_sz_data_c_ = atoi(optarg);
            break;
        case 'q':
            ag_quit_ = atoi(optarg);
            break;
        }
    }

    // init mpaxos
    mpaxos_init();
    
    int ret = mpaxos_config_load(ag_config_);
    if (ret != 0) {
        mpaxos_destroy();
        exit(10);
    }
    
    if (ag_sz_data_ > 0) {
        TEST_DATA = calloc(ag_sz_data_, 1);
    }

    if (ag_sz_data_c_ > 0) {
        TEST_DATA_C = malloc(ag_sz_data_c_);
    }
    
    LOG_INFO("test for %d messages.", ag_n_send_);
    LOG_INFO("test for %d threads.", ag_n_group_);
    LOG_INFO("async mode %d", async);

    // start mpaxos service
    mpaxos_set_cb_god(cb);
    mpaxos_start();

    if (ag_n_send_ > 0 && ag_n_group_ > 0) {
        // wait some time to wait for everyone starts
        sleep(t_sleep_);
        
        LOG_INFO("i have something to propose.");
        
        test_init();
        if (async) {
            test_async_start();
        } else {
//            apr_time_t begin_time = apr_time_now();
//
//            for (uint32_t i = 0; i < n_group; i++) {
//                apr_thread_pool_push(tp_test_, test_group, (void*)(uintptr_t)(i+1), 0, NULL);
//            }
//            while (apr_thread_pool_tasks_count(tp_test_) > 0) {
//                sleep(0);
//            }
//
//            apr_thread_pool_destroy(tp_test_);
//            apr_time_t end_time = apr_time_now();
//            double time_period = (end_time - begin_time) / 1000000.0;
//            uint64_t n_msg = n_tosend * n_group;
//            LOG_INFO("in total %"PRIu64" proposals commited in %.2fsec," 
//                    "rate:%.2f props per sec.", n_msg, time_period, n_msg / time_period); 
        }
    }

    exit_on_finish();
}

