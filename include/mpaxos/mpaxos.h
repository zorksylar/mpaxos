
/*
 * mpaxos.h
 *
 *  Created on: Dec 30, 2012
 *      Author: ms
 */

#ifndef __MPAXOS_H__
#define __MPAXOS_H__

#include <stdio.h>
#include <stdlib.h>
#include <apr_hash.h>

#include "mpaxos-types.h"

#ifdef __cplusplus
extern "C" {
#endif

extern apr_pool_t *mp_global_;

void mpaxos_init();

void mpaxos_start();

void mpaxos_stop();

void mpaxos_destroy();

int mpaxos_config_load(const char * config_file_path);

#define mpaxos_load_config mpaxos_config_load 

int mpaxos_config_set(const char *key, const char *value);

void mpaxos_set_cb(groupid_t, mpaxos_cb_t); 

void mpaxos_set_cb_god(mpaxos_cb_t);

int mpaxos_commit_req(mpaxos_req_t *);

int mpaxos_commit_raw(groupid_t* gids, size_t sz_gids, uint8_t *data,
        size_t sz_data, uint8_t *data_c, size_t sz_data_c, void* cb_para);

void add_group(groupid_t gid);

void set_listen_port(int port);

//int commit_sync(groupid_t* gids, size_t sz_gids, uint8_t *val,
//        size_t sz_val);
//
//int commit_async(groupid_t* gids, size_t sz_gids, uint8_t *val,
//        size_t sz_val, void *cb_para);

int get_insnum(groupid_t gid, slotid_t** sid);

/**
  please free val after use.
  */
int get_insval(groupid_t gid, slotid_t sid, uint8_t** val,
        size_t* val_sz);

//int put_instval(groupid_t, slotid_t, uint8_t *, size_t);

bool has_value(groupid_t gid, slotid_t sid);

int get_last_cb_sid(groupid_t gid);

//void set_cb_req(groupid_t gid, slotid_t sid, mpaxos_req_t* req);

//mpaxos_req_t* get_cb_req(groupid_t gid, slotid_t sid);

int add_last_cb_sid(groupid_t gid);

int commit_async_run();

#ifdef __cplusplus
}
#endif

#endif /* MPAXOS_H_ */
