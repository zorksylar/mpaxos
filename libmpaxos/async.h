
#ifndef ASYNC_H_
#define ASYNC_H_

#include "internal_types.h"

void mpaxos_async_init();

void mpaxos_async_enlist(mpaxos_req_t *);

//void*  mpaxos_async_daemon(void* data);
void* APR_THREAD_FUNC mpaxos_async_daemon(apr_thread_t *th, void* data);

void mpaxos_async_job();

void mpaxos_async_destroy();

void async_ready_callback(mpaxos_req_t*);

#endif
