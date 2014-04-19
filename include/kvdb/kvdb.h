#ifndef __KVDB_H__
#define __KVDB_H__

#include "mpaxos-types.h"

#define KVDB_RET_UNINITIALIZED      -1
#define KVDB_RET_OPEN_FAILED        -100

// inherent from leveldb
#define KVDB_RET_OK                 0
#define KVDB_RET_NOT_FOUND          -200
#define KVDB_RET_CORRUPTION         -300
#define KVDB_RET_NOT_SUPPORTED      -400
#define KVDB_RET_INVALID_ARGUMENT   -500
#define KVDB_RET_IO_ERROR           -600

#ifdef __cplusplus
extern "C" {
#endif

int kvdb_init(char * dbhome, char * mpaxos_config_path);
int kvdb_put(uint8_t *, size_t, uint8_t *, size_t);
int kvdb_get(uint8_t * key, size_t klen, uint8_t ** value, size_t *vlen);
int kvdb_del(uint8_t * key, size_t klen);
int kvdb_batch_put(uint8_t ** keys, size_t * klens, uint8_t ** vals, size_t * vlens, uint32_t pairs);
int kvdb_destroy();

#ifdef __cplusplus
}
#endif

#endif

