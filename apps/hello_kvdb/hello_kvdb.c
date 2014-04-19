#include <kvdb.h>
#include <mpaxos.h>
#include <stdio.h>
#include <stdlib.h>

#define LOG(x, ...) printf("[%s:%d] [ II ] "x"\n", __FILE__, __LINE__, ##__VA_ARGS__)


const static char dbhome[] = "./kvdb_home";

int main() {

    int ret = KVDB_RET_OK;

    ret = kvdb_init(dbhome, "./mpaxos.cfg");
    if (ret != KVDB_RET_OK) {
        LOG("kvdb initialize failed.");
        kvdb_destroy();
    }

    // Put
    uint8_t * key = (uint8_t *)"key456";
    size_t klen = 6;
    uint8_t * val = (uint8_t *)"val456";
    size_t vlen = 6;

    ret = kvdb_put(key, klen, val, vlen);
    LOG(" [ PUT ]  ret =  %d", ret);
    if (ret != KVDB_RET_OK) {
        kvdb_destroy();
        return 0;
    }

    // Get
    uint8_t * key2 = (uint8_t *)"key456";
    size_t klen2 = 6;
    uint8_t * val2;
    size_t vlen2;
    ret = kvdb_get(key2, klen2, &val2, &vlen2);
    LOG(" [ GET ] key = %s, klen = %d, val = %s, vlen = %d, ret = %d.", key2, klen2, val2, vlen2, ret);
    if (ret != KVDB_RET_OK) {
        kvdb_destroy();
        return 0;
    }

    uint8_t * non_exist_key = (uint8_t *) "non_exist_key";
    size_t non_exist_klen = strlen(non_exist_key);
    uint8_t * non_exist_val ;
    size_t non_exist_vlen;
    
    // Get non exist
    ret = kvdb_get(non_exist_key, non_exist_klen, &non_exist_val, &non_exist_vlen);
    if (ret != KVDB_RET_NOT_FOUND) {
        LOG(" [ GET NON EXIST KEY ] test success. ");
    } else {
        LOG(" [ GET NON EXIST KEY ] test failed.");
    }

    // Del
    ret = kvdb_del(key, klen);
    LOG(" [ DEL ] key = %s, klen = %d, ret = %d", key, klen, ret);
    if (ret != KVDB_RET_OK) {
        kvdb_destroy();
        return 0;
    }
    
    return 0;
}



