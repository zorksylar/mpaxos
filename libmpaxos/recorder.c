/**
 *
 */ 

#include "include_all.h"

extern int flush__;

static apr_pool_t *mp_recorder_;
static apr_hash_t *ht_value_;        //instid_t -> value_t
//static apr_hash_t *ht_newest_;     // gid -> sid
static mpr_hash_t *ht_newest_;      // gid -> sid
static mpr_hash_t *ht_decided_;     // instid_t -> 1
static apr_thread_mutex_t *mx_value_;
static apr_thread_mutex_t *mx_newest_;


typedef struct {
    slotid_t sid;
    int is_me;
} nextsid_t;


void recorder_init() {
    apr_pool_create(&mp_recorder_, NULL);
    ht_value_ = apr_hash_make(mp_recorder_);
    mpr_hash_create_ex(&ht_newest_, MPR_HASH_THREAD_UNSAFE);
    mpr_hash_create_ex(&ht_decided_, MPR_HASH_THREAD_SAFE);

    apr_thread_mutex_create(&mx_value_, APR_THREAD_MUTEX_UNNESTED, mp_recorder_);
    apr_thread_mutex_create(&mx_newest_, APR_THREAD_MUTEX_UNNESTED, mp_recorder_);
}

void recorder_destroy() {
    apr_pool_destroy(mp_recorder_);
    mpr_hash_destroy(ht_newest_);
    mpr_hash_destroy(ht_decided_);
}

int get_instval(uint32_t gid, uint32_t in, char* buf,
        uint32_t bufsize, uint32_t *val_size) {
    //TODO [FIX]
    return 0;
}


/**
 * For every value accepted. must call this function. 
 * Notice that if deciding an unaccepted value, must accept it first.
 */
void record_accepted(roundid_t *rid, proposal_t *prop) {
    int flush;
    if (flush__ == ASYNC) {
        flush = 0; 
    } else if (flush__ == SYNC) {
        flush = 1;
    } else if (flush__ == MEM) {
        // copy it into value ht.
    } else {
        // do not have to record.
        return;
    }
    // [FIXME]  write to disk in a meaningful way?
    //
    instid_t *iid = malloc(sizeof(instid_t));
    memset(iid, 0, sizeof(instid_t));
    iid->gid = rid->gid;
    iid->sid = rid->sid;

    if (flush__ == MEM) {
        proposal_t *p = NULL; 
        apr_thread_mutex_lock(mx_value_);
        p = apr_hash_get(ht_value_, iid, sizeof(instid_t));      
        if (p != NULL) {
            prop_free(p);
        }
        p = prop_copy(prop);
        apr_hash_set(ht_value_, iid, sizeof(instid_t), p);
        apr_thread_mutex_unlock(mx_value_);
    } else {
        uint8_t *value = NULL;
        size_t sz_value = 0;
        prop_pack(prop, &value, &sz_value);
        db_put_raw((uint8_t*)iid, sizeof(instid_t), value, sz_value, 1);
        prop_buf_free(value);
    }

}

/**
 * Just keep a mark is enough, without flush. 
 * You should record the accepted prop before you decide it.
 */
void record_decided(proposal_t *prop) {
    for (int i = 0; i <prop->n_rids; i++) {
        roundid_t *rid = prop->rids[i];
        instid_t iid;
        memset(&iid, 0, sizeof(instid_t));
        iid.gid = rid->gid;
        iid.sid = rid->sid;
        uint8_t v = 1;
        mpr_hash_set(ht_decided_, &iid, sizeof(instid_t), &v, sizeof(uint8_t)); 

        // renew the newest value number
        apr_thread_mutex_lock(mx_newest_);
        nextsid_t *nextsid = NULL;
        size_t sz;
        mpr_hash_get(ht_newest_, &rid->gid, sizeof(groupid_t), &nextsid, &sz); 
        if (nextsid == NULL) {
            nextsid_t ns;
            ns.sid = 0;
            ns.is_me = 0;
            LOG_TRACE("newest id for gid: %d does not exist", rid->gid);
            mpr_hash_set(ht_newest_, &rid->gid, sizeof(groupid_t), &ns, sizeof(nextsid_t));
            mpr_hash_get(ht_newest_, &rid->gid, sizeof(groupid_t), &nextsid, &sz); 
        }

        if (nextsid->sid <= rid->sid) {
            nextsid->sid = rid->sid + 1;
            nextsid->is_me = (prop->nid == get_local_nid()) ? 1 : 0;
            LOG_TRACE("update the newest sid. gid:%d, sid:%"PRIx64, rid->gid, nextsid->sid);
        }
        apr_thread_mutex_unlock(mx_newest_);
    }
}

slotid_t get_newest_sid(groupid_t gid, int *is_me) {
    apr_thread_mutex_lock(mx_newest_);
    nextsid_t *nextsid = NULL;
    size_t sz;
    mpr_hash_get(ht_newest_, &gid, sizeof(groupid_t), &nextsid, &sz);    
    if (nextsid == NULL) {
        LOG_TRACE("get newest id for gid: %d does not exist", gid);
        nextsid_t ns;
        ns.sid = 1;
        ns.is_me = 0;
        mpr_hash_set(ht_newest_, &gid, sizeof(groupid_t), &ns, sizeof(nextsid_t));
        mpr_hash_get(ht_newest_, &gid, sizeof(groupid_t), &nextsid, &sz); 
    }
    slotid_t sid = nextsid->sid;
    *is_me = nextsid->is_me;
    apr_thread_mutex_unlock(mx_newest_);
    LOG_DEBUG("next newest sid %"PRIx64" for gid %d", nextsid->sid, gid);
    return sid;
}
