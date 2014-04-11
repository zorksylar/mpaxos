#ifndef __KVDB_H__
#define __KVDB_H__

#include "mpaxos/mpaxos-types.h"

#define KVDB_RET_UNINITIALIZED  -1
#define KVDB_OPEN_FAILED        -100

// inherent from leveldb
#define KVDB_RET_OK             0
#define KVDB_NOT_FOUND          -200
#define KVDB_CORRUPTION         -300
#define KVDB_NOT_SUPPORTED      -400
#define KVDB_INVALID_ARGUMENT   -500
#define KVDB_IO_ERROR           -600

#ifdef __cplusplus
extern "C" {
#endif

int kvdb_init(char * dbhome, char * mpaxos_config_path);
int kvdb_put(groupid_t table, uint8_t *, size_t, uint8_t *, size_t);
int kvdb_get(groupid_t table, uint8_t * key, size_t klen, uint8_t ** value, size_t *vlen);
int kvdb_del(groupid_t table, uint8_t * key, size_t klen);
int kvdb_batch_put(groupid_t * tables, uint8_t ** keys, size_t * klens, uint8_t ** vals, size_t * vlens, uint32_t pairs);
int kvdb_destroy();

#ifdef __cplusplus
}
#endif

#endif

