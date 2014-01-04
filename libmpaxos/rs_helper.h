#ifndef RS_HELPER_H_
#define RS_HELPER_H_

#include "fec.h"
#include "internal_types.h"
#include "utils/logger.h"

static coded_value_t **rs_encode(uint8_t *data, size_t sz_data, int k, int n) {
    LOG_TRACE("start to encode value");
    size_t sz_share = (sz_data + k - 1) / k;

    uint8_t **shares = malloc(sizeof(uint8_t *) * n);
    for (int i = 0; i < n; i++) {
        shares[i] = malloc(sz_share);
        SAFE_ASSERT(shares[i] != NULL);
        memset(shares[i], 0, sz_share);
    }
    for (int i = 0; i < k - 1; i++) {
        memcpy(shares[i], data + sz_share * i, sz_share); 
    }
    memcpy(shares[k-1], data + sz_share * (k-1), sz_share);
    
    fec_t *fec = NULL;
    fec = fec_new(k, n);
    unsigned int *nums = malloc(sizeof(unsigned int) * (n-k));
    for (int i = 0; i < (n-k); i++) {
        nums[i] = i + k;
    }
    fec_encode(fec, shares, shares+k, nums, n-k, sz_share);

    coded_value_t **cvs = malloc(sizeof(coded_value_t **) * n);
    for (int i = 0; i < n; i++) {
        coded_value_t *cv = malloc(sizeof(coded_value_t));
        mpaxos__coded_value_t__init(cv);
        cv->sz = sz_data; 
        cv->k = k;
        cv->n = n;
        cv->id = i;
        cv->value.data = shares[i];
        cv->value.len = sz_share;
        SAFE_ASSERT(cv != NULL);
        cvs[i] = cv;
    }
    LOG_TRACE("finish encoding value");
    return cvs;
}
#endif // RS_HELPER_H_
