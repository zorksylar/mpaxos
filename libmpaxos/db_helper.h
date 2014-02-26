
#ifndef DB_HELPER_H_
#define DB_HELPER_H_


void db_init();

void db_put_raw(uint8_t* key, size_t sz_key, uint8_t *value, size_t sz_value, int sync);

void db_get_raw(uint8_t *key, size_t sz_key, uint8_t **value, size_t *sz_value);

void db_destroy(); 

#endif // DB_HELPER_H_
