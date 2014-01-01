#include "include_all.h"
#include <leveldb/c.h>

static leveldb_t *db_;

void db_init() {
    char *err = NULL;
    leveldb_options_t *options;
    options = leveldb_options_create();
    leveldb_options_set_create_if_missing(options, 1);
    db_ = leveldb_open(options, "testdb", &err);
    SAFE_ASSERT(err == NULL);
}

void db_destroy() {
    char *err = NULL;
    leveldb_close(db_);
    // TODO currently we just destroy the db at close. no recovery.
    leveldb_options_t *options;
    options = leveldb_options_create();
    leveldb_destroy_db(options, "testdb", &err);
    SAFE_ASSERT(err == NULL);
}

void db_get_raw(uint8_t *key, size_t sz_key, uint8_t **value, size_t *sz_value) {
    char *err = NULL;
    leveldb_readoptions_t *roptions;
    roptions = leveldb_readoptions_create();
    *value = (uint8_t*)leveldb_get(db_, roptions, (char*)key, sz_key, sz_value, &err);
    SAFE_ASSERT(err == NULL);
}

void db_delete_raw(uint8_t *key, size_t sz_key) {
    char *err = NULL;
    leveldb_writeoptions_t *woptions;
    woptions = leveldb_writeoptions_create();
    leveldb_delete(db_, woptions, (char*)key, sz_key, &err);
    SAFE_ASSERT(err == NULL);
}

void db_put_raw(uint8_t* key, size_t sz_key, uint8_t *value, size_t sz_value, int sync) {
    char *err = NULL;
    leveldb_writeoptions_t *woptions;
    woptions = leveldb_writeoptions_create();
    leveldb_put(db_, woptions, (char*)key, sz_key, (char*)value, sz_value, &err);
    SAFE_ASSERT(err == NULL);
}
