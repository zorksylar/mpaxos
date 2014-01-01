#include <stdlib.h>
#include <stdio.h>
#include <memory.h>
#include <time.h>
#include <unistd.h>
#include <apr_time.h>
#include <sys/stat.h>

#include "kvdb/kvdb.h"

#define DB_HOME "dbhome"

#define LOG(x, ...) printf("[%s:%d] [ II ] "x"\n", __FILE__, __LINE__, ##__VA_ARGS__)

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <mpaxos config file>\n", argv[0]);
        return 0;
    }
    mkdir(DB_HOME, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
    int id = 0;
    int rs = kvdb_init(DB_HOME, argv[1]);
    if (rs != KVDB_RET_OK) {
        LOG("kvdb_init failed");
    }
    uint8_t * key = (uint8_t *)"xxxxasdfasdf";
    size_t klen = 6;
    uint8_t * val = (uint8_t *)"hahahahahah";
    size_t vlen = 10;
    rs = kvdb_put(1, key, klen, val, vlen);
    LOG("put result = %d", rs);
    if (rs) {
        kvdb_destroy();
        return 0;
    }

    rs = kvdb_put(2, key, klen-1, val, vlen);
    LOG("put to table 2 result = %d", rs);
    if (rs) {
        kvdb_destroy();
        return 0;
    }

    uint8_t * val2;
    size_t vlen2;
    rs = kvdb_get(1, key, klen, &val2, &vlen2);
    LOG("get result = %d", rs);
    if (!rs) {
        printf("get keyval pair is: as -> ");
        int same = 1;
        for (size_t i = 0; i < vlen2; i++) {
            putchar(val2[i]);
            if (val2[i] != val[i]) {
                same = 0;
            }
        }
        printf("  ... [ %s ]\n", same ? "PASS" : "FAIL");
    } else {
        kvdb_destroy();
        return 0;
    }

    uint8_t * non_existing_key = (uint8_t *)"non-existing key";
    size_t non_existing_key_len = strlen("non-existing key");

    rs = kvdb_get(1, non_existing_key, non_existing_key_len, &val2, &vlen2);
    if (rs == KVDB_GET_KEY_NOT_EXIST) {
        printf("test non existing key [ PASS ]\n");
    } else {
        printf("test non existing key [ FAIL ], err code = %d\n", rs);
    }

    rs = kvdb_del(1, key, klen);
    if (rs) {
        printf("test del key  ... [ FAIL ]\n");
    } else {
        printf("test del key  ... [ PASS ]\n");
    }

    rs = kvdb_del(1, non_existing_key, non_existing_key_len);
    if (rs == KVDB_DEL_KEY_NOT_EXIST) {
        printf("test del non-existing key  ... [ PASS ]\n");
    } else {
        printf("test del non-existing key  ... [ FAIL ]\n");
    }

    int pairs = 8, p = 0;
    uint8_t ** keys = (uint8_t **)malloc(sizeof(uint8_t *) * pairs);
    uint8_t ** vals = (uint8_t **)malloc(sizeof(uint8_t *) * pairs);
    size_t * klens = (size_t *)malloc(sizeof(size_t) * pairs);
    size_t * vlens = (size_t *)malloc(sizeof(size_t) * pairs);
    groupid_t * tables = (groupid_t *)malloc(sizeof(groupid_t) * pairs);
#define KEYS "  batch key"
#define VALS "  batch val"
    for (p = 0; p < pairs; p++) {
        klens[p] = strlen(KEYS);
        vlens[p] = strlen(VALS);
        keys[p] = (uint8_t *)malloc(klens[p]);
        memcpy(keys[p], KEYS, klens[p]);
        keys[p][0] = '0' + p;

        vals[p] = (uint8_t *)malloc(vlens[p]);
        memcpy(vals[p], VALS, vlens[p]);
        vals[p][0] = '0' + p;
        tables[p] = p + 1;
    }

    rs = kvdb_batch_put(tables, keys, klens, vals, vlens, pairs);

    if (rs) {
        printf("failed to do batch put  ... [ FAIL ]\n");
    }
    uint8_t batch_key[100];
    int kl = strlen(KEYS);
    int vl = strlen(VALS);
    uint8_t * batch_val;
    size_t batch_val_len;
    uint8_t batch_ans[100];
    for (p = 0; p < pairs; p++) {
        memcpy(batch_key, KEYS, kl);
        memcpy(batch_ans, VALS, vl);
        batch_key[0] = '0' + p;
        batch_ans[0] = '0' + p;
        kvdb_get(p+1, batch_key, kl, &batch_val, &batch_val_len);
        if (batch_val_len != vl || memcmp(batch_ans, batch_val, vl)) {
            printf("test value by batch_put: %s, expected = %s, got = %s .... [ FAIL ]\n", batch_key, batch_ans, batch_val);
        } else {
            printf("test value by batch_put: %s .... [ PASS ]\n", batch_key);
        }
    }

    kvdb_destroy();
    return 0;
}

/* vim: set et ai ts=4 sw=4: */

