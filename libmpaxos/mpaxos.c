/*
 * mpaxos.c
 *
 *  Created on: Dec 30, 2012
 *      Author: ms
 */

#include "include_all.h"

#define QUEUE_SIZE 1000

apr_pool_t *mp_global_;
apr_hash_t *lastslot_ht_;   //groupid_t -> slotid_t #hash table to store the last slot id that has been called back.

// perhaps we need a large hashtable, probably not
// groupid_t -> some structure
// some structure:
//      a hash table to store value sid->value   ###need to be permanent         
//      last slotid_t that has been called back. ###need to be permanent
//      callback function.      ###cannot be be permanent 
//      a hash table to store callback parameter sid->cb_para    ###permanent?  now not.
//      a mutex for callback. 

int port_;

void mpaxos_init() {
    apr_initialize();
    apr_pool_create(&mp_global_, NULL);
    lastslot_ht_ = apr_hash_make(mp_global_);

    // initialize db
    db_init();

    // initialize view
    view_init();

    // initialize comm
    comm_init();

    // initialize controller
    controller_init();

    // initialize proposer
    proposer_init();

    // initialize acceptor
    acceptor_init();
    
    // initialize slot manager
    slot_mgr_init();
    
    // initialize the recorder
    recorder_init();
    
    // initialize asynchrounous commit and callback module
    mpaxos_async_init(); 

    
    //start_server(port);

}

void mpaxos_start() {
    // start listening
    start_server(port_);
}

void mpaxos_stop() {
    // TODO [improve] when to stop?
}

void mpaxos_destroy() {
    comm_destroy();
    
    acceptor_destroy();
    proposer_destroy();
    controller_destroy();

    // stop asynchrouns callback.
    mpaxos_async_destroy();

    db_destroy();

    apr_pool_destroy(mp_global_);
    apr_terminate();

}

void set_listen_port(int port) {
    port_ = port;
}

int commit_sync(groupid_t* gids, size_t gid_len, uint8_t *val,
        size_t val_len) {
	// TODO
    return 0;
}

void lock_group_commit(groupid_t* gids, size_t sz_gids) {
    char buf[100];
    for (int i = 0; i < sz_gids; i++) {
        groupid_t gid = gids[i];
        sprintf(buf, "COMMIT%x", gid);
        m_lock(buf);
    }
}

void unlock_group_commit(groupid_t* gids, size_t sz_gids) {
    char buf[100];
    for (int i = 0; i < sz_gids; i++) {
        groupid_t gid = gids[i];
        sprintf(buf, "COMMIT%x", gid);
        m_unlock(buf);
    }
}

/**
 * commit a request that is to be processed asynchronously. add the request to the aync job queue. 
 */

int mpaxos_commit_raw(groupid_t *gids, size_t sz_gids, uint8_t *data, 
    size_t sz_data, uint8_t *data_c, size_t sz_data_c, void* cb_para) {
    mpaxos_req_t *r = (mpaxos_req_t *)malloc(sizeof(mpaxos_req_t));
    r->gids = malloc(sz_gids * sizeof(groupid_t));
    r->sz_gids = sz_gids;
    r->sz_data = sz_data;
    r->sz_data_c = sz_data_c;
    r->cb_para = cb_para;
    r->n_retry = 0;
    r->id = gen_txn_id();
    if (sz_data > 0) {
        r->data = malloc(sz_data);
        memcpy(r->data, data, sz_data);
    } else {
        r->data = NULL;
    }
    
    if (sz_data_c > 0) {
        r->data_c = malloc(sz_data_c);
        memcpy(r->data_c, data_c, sz_data_c);
    } else {
        r->data_c = NULL;
    }
    memcpy(r->gids, gids, sz_gids * sizeof(groupid_t));
    mpaxos_async_enlist(r);    
    return 0;
}

int mpaxos_commit_req(mpaxos_req_t *req) {
    mpaxos_req_t *r = (mpaxos_req_t *) malloc(sizeof(mpaxos_req_t));
    r->sz_gids = req->sz_gids;
    r->sz_data = req->sz_data;
    r->sz_data_c = req->sz_data_c;
    r->cb_para = req->cb_para;
    r->sync = 0; // TODO currently only async mode.
    r->n_retry = 0;
    r->id = gen_txn_id();

    if (r->sz_gids > 0) {
        r->gids = malloc(r->sz_gids * sizeof(groupid_t)); 
        memcpy(r->gids, req->gids, r->sz_gids * sizeof(groupid_t));
    } else if (r->sz_gids == 0) {
        r->gids = malloc(1 * sizeof(groupid_t));
        r->gids[0] = 1; 
    } else {
        SAFE_ASSERT(0);
    }

    if (r->sz_data > 0) {
        r->data = (uint8_t *) malloc(r->sz_data);
        memcpy(r->data, req->data, r->sz_data);
    } else {
        r->data = NULL;
    }

    if (r->sz_data_c > 0) {
        r->data_c = (uint8_t *) malloc(r->sz_data_c);
        memcpy(r->data_c, req->data_c, r->sz_data_c);
    } else {
        r->data_c = NULL;
    }

    mpaxos_async_enlist(r);    
    return 0;
}

pthread_mutex_t add_last_cb_sid_mutex = PTHREAD_MUTEX_INITIALIZER;
int add_last_cb_sid(groupid_t gid) {
    pthread_mutex_lock(&add_last_cb_sid_mutex);
    slotid_t* sid_ptr = apr_hash_get(lastslot_ht_, &gid, sizeof(gid));
    if (sid_ptr == NULL) {
        sid_ptr = apr_palloc(mp_global_, sizeof(slotid_t));
        *sid_ptr = 0;
        groupid_t *gid_ptr = apr_palloc(mp_global_, sizeof(groupid_t));
        *gid_ptr = gid;
        apr_hash_set(lastslot_ht_, gid_ptr, sizeof(gid), sid_ptr);
    }
    *sid_ptr += 1;
    
    slotid_t sid = *sid_ptr; 
    pthread_mutex_unlock(&add_last_cb_sid_mutex);
    return sid;
}


pthread_mutex_t get_last_cb_sid_mutex = PTHREAD_MUTEX_INITIALIZER;
int get_last_cb_sid(groupid_t gid) {
    // TODO [FIX] need to lock.
    pthread_mutex_lock(&get_last_cb_sid_mutex);
    slotid_t* sid_ptr = apr_hash_get(lastslot_ht_, &gid, sizeof(gid));
    if (sid_ptr == NULL) {
        sid_ptr = apr_palloc(mp_global_, sizeof(slotid_t));
        *sid_ptr = 0;
        groupid_t *gid_ptr = apr_palloc(mp_global_, sizeof(groupid_t));
        *gid_ptr = gid;
        apr_hash_set(lastslot_ht_, gid_ptr, sizeof(gid), sid_ptr);
    }
    
    slotid_t sid = *sid_ptr; 
    pthread_mutex_unlock(&get_last_cb_sid_mutex);
    return sid;
}

int get_insnum(groupid_t gid, slotid_t** in) {
    slotid_t* sid_ptr = apr_hash_get(lastslot_ht_, &gid, sizeof(gid));
    if (sid_ptr == NULL) {
        sid_ptr = apr_palloc(mp_global_, sizeof(slotid_t));
        *sid_ptr = 0;
        groupid_t *gid_ptr = apr_palloc(mp_global_, sizeof(groupid_t));
        *gid_ptr = gid;
        apr_hash_set(lastslot_ht_, gid_ptr, sizeof(gid), sid_ptr);
    }
    *in = sid_ptr;
    return 0;
}
