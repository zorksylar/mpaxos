#include <apr_thread_proc.h>
#include "utils/mpr_hash.h"

#define N_THREAD 4
#define N_HASH_ACCESS 1000000

int simple_fun__ = 0;
static mpr_hash_t *ht;
static apr_pool_t *mp_mpr_;

void simple_fun() {
    printf("simple function called.\n");
    simple_fun__++;
}

void* APR_THREAD_FUNC mpr_hash_thread_test(apr_thread_t *th, void* data) {
    mpr_hash_t *h;
    if (data == NULL) {
        h = ht;  
    } else {
        h = data;
    }
    for (uint32_t i = 0; i < N_HASH_ACCESS; i++) {
        size_t sz;
        int *v;
        mpr_hash_get(h, &i, sizeof(uint32_t), (void**)&v, &sz); 
    }
    apr_thread_exit(th, APR_SUCCESS);
}

START_TEST(mpr_hash) {
    uint8_t k1 = 5; 
    uint16_t k2 = 6; 
    uint32_t k3 = 7; 
    uint64_t k4 = 8; 
    int t = 10;

    mpr_hash_create_ex(&ht, 0);
    mpr_hash_set(ht, &k1, sizeof(uint8_t), &t, sizeof(int)) ;
    mpr_hash_set(ht, &k2, sizeof(uint16_t), &t, sizeof(int)); 
    mpr_hash_set(ht, &k3, sizeof(uint32_t), &t, sizeof(int)); 
    mpr_hash_set(ht, &k4, sizeof(uint64_t), &t, sizeof(int)); 

    int *res = NULL;
    size_t sz;
    uint8_t kk1 = 5; 
    uint16_t kk2 = 6; 
    uint32_t kk3 = 7; 
    uint64_t kk4 = 8; 
    mpr_hash_get(ht, &kk1, sizeof(uint8_t), (void**)&res, &sz);
    ck_assert(res != NULL && *res == 10);
    mpr_hash_get(ht, &kk2, sizeof(uint16_t), (void**)&res, &sz);
    ck_assert(res != NULL && *res == 10);
    mpr_hash_get(ht, &kk3, sizeof(uint32_t), (void**)&res, &sz);
    ck_assert(res != NULL && *res == 10);
    mpr_hash_get(ht, &kk4, sizeof(uint64_t), (void**)&res, &sz);
    ck_assert(res != NULL && *res == 10);

    uint8_t kfun = 9;
    void *sfun = simple_fun;
    mpr_hash_set(ht, &kfun, sizeof(uint8_t), &sfun, sizeof(void *));
    void (**fun)() = NULL;
    mpr_hash_get(ht, &kfun, sizeof(uint8_t), (void**)&fun, &sz);
    ck_assert(fun != NULL); 
    (**fun)();
    ck_assert(simple_fun__ == 1);
    

    apr_initialize();
    atexit(apr_terminate);
    apr_pool_create(&mp_mpr_, NULL);
    for (uint32_t i = 0; i < N_HASH_ACCESS; i++) {
        mpr_hash_set(ht, &i, sizeof(uint32_t), &i, sizeof(uint32_t));
    }
    
    // test for multiple thread access
    apr_time_t t1, t2;
    apr_status_t status;
    t1 = apr_time_now();
    apr_thread_t *th;
    apr_thread_create(&th, NULL, mpr_hash_thread_test, NULL, mp_mpr_);          
    apr_thread_join(&status, th);
    t2 = apr_time_now();
    float rate = N_HASH_ACCESS / ((float)(t2-t1) / 1000000);
    printf("hash access rate: %f per sec.\n", rate);

    t1 = apr_time_now();
    apr_thread_t *ths[N_THREAD];


    for (int i = 0 ; i < N_THREAD; i++) {
        apr_thread_create(&ths[i], NULL, mpr_hash_thread_test, NULL, mp_mpr_);
    }

    for (int i = 0; i < N_THREAD; i++) {
        apr_thread_join(&status, ths[i]);
    }
    t2 = apr_time_now();
    rate = N_THREAD * N_HASH_ACCESS / ((float)(t2-t1) / 1000000);
    printf("hash access rate: %f per sec for %d threads.\n", rate, N_THREAD);
    

    // different threads access different hash tables.
    mpr_hash_t *hts[N_THREAD];
    for (int i = 0; i < N_THREAD; i++) {
        mpr_hash_create_ex(&hts[i], 0);
        for (uint32_t j = 0; j < N_HASH_ACCESS; j++) {
            mpr_hash_set(hts[i], &j, sizeof(uint32_t), &j, sizeof(uint32_t));
        }
    }

    t1 = apr_time_now();
//    apr_thread_t *ths[N_THREAD];

    for (int i = 0 ; i < N_THREAD; i++) {
        apr_thread_create(&ths[i], NULL, mpr_hash_thread_test, hts[i], mp_mpr_);
    }

    for (int i = 0; i < N_THREAD; i++) {
        apr_thread_join(&status, ths[i]);
    }
    t2 = apr_time_now();
    rate = N_THREAD * N_HASH_ACCESS / ((float)(t2-t1) / 1000000);
    printf("hash access rate: %f per sec for %d threads.\n", rate, N_THREAD);

    for (int i = 0; i < N_THREAD; i++) {
        mpr_hash_destroy(hts[i]);
    }

    mpr_hash_destroy(ht);
    apr_pool_destroy(mp_mpr_);
}
END_TEST
