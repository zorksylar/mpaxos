
#ifndef ASYNC_H_
#define ASYNC_H_

void mpaxos_async_init();

void mpaxos_async_enlist(groupid_t *gids, size_t sz_gids, uint8_t *data, size_t sz_data, void* cb_para);

void mpaxos_async_daemon();

void mpaxos_async_job();

void mpaxos_async_destroy();

#endif
