/*
 * mpaxos_types.h
 *
 *  Created on: Jan 29, 2013
 *      Author: ms
 */


#ifndef __MPAXOS_TYPES_H__
#define __MPAXOS_TYPES_H__

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <apr_hash.h>
#include <apr_thread_proc.h>
#include <apr_thread_cond.h>

typedef uint32_t groupid_t;
typedef uint32_t nodeid_t;

typedef uint64_t slotid_t;
typedef uint64_t ballotid_t;
typedef uint64_t txnid_t;

typedef struct {
    txnid_t id;
    groupid_t* gids;
    size_t sz_gids;
    slotid_t *sids;
    uint8_t *data;
    size_t sz_data;
    uint8_t *data_c;
    size_t sz_data_c;
    void* cb_para;
    uint32_t n_retry;   // how many times it has been retried.
    apr_time_t tm_start; 
    apr_time_t tm_end;
    int sync;   // asynchronous callback or wait until finish.
} mpaxos_req_t;

typedef struct {
    uint8_t *data;
    uint32_t len;
} value_t;

//typedef void(*mpaxos_cb_t)(groupid_t*, size_t, slotid_t*, uint8_t *, size_t, uint8_t *, size_t, void *para);
typedef void(*mpaxos_cb_t)(mpaxos_req_t *);

#endif /* MPAXOS_TYPES_H_ */
