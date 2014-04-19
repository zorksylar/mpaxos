#include <sys/types.h>
#include <cstdio>
#include <cstring>
#include <assert.h>
#include <map>

#include "leveldb/db.h"
#include "leveldb/write_batch.h"
#include "kvdb/kvdb.h"
#include "kvdb_log.h"
#include "mpaxos/mpaxos.h"
#include "mpaxos/mpaxos-config.h"
#include "operation.h"
#include "lock.h"


typedef int32_t kvdb_ret_t;

static int initialized = 0;
static leveldb::DB* db;
static std::map<operation_id_t, Operation> operations;
static std::map<groupid_t, leveldb::DB *> dbs;


// TODO : support multiple dbs
static leveldb::Status open_db(groupid_t gid) {
    if (dbs[gid] != NULL) {
      return leveldb::Status::OK();
    }

    assert(!!initialized);
    dbs[gid] = db;
    return leveldb::Status::OK();
}

static kvdb_ret_t kvdb_ret(const leveldb::Status &s) {
    if (s.ok()) {
        return KVDB_RET_OK;
    } else if (s.IsNotFound()) {
        return KVDB_RET_NOT_FOUND;
    } else if (s.IsCorruption()) {
        return KVDB_RET_CORRUPTION;
    } else if (s.IsIOError()) {
        return KVDB_RET_IO_ERROR;
    }
    
    return KVDB_RET_OK;
}


void mpaxos_callback(mpaxos_req_t * req) {
    OperationParam * commit_param = (OperationParam *) (req->cb_para);

    std::string idstr = "";

    for (int i = 0; i < req->sz_gids; i ++) {
        idstr += std::to_string(req->gids[i]);
    }

    DD("mpaxos callback table.cound = %zu, ids = [%s], msg len = %zu, op_id = %lu", req->sz_gids, idstr.c_str(), req->sz_data, (unsigned long) commit_param->id);

    assert(req->sz_gids > 0);
    // TODO
    assert(req->sz_gids == 1);

    groupid_t table = req->gids[0]; 

    std::map<operation_id_t, Operation>::iterator it;
    it = operations.find(commit_param->id);
    assert(it != operations.end());

    Buf buf(req->data, req->sz_data);
    Operation op = unwrap(buf);
    
    assert(op.code < OPERATION_TYPE_COUNT && op.code >= OP_NOP);

    if (it != operations.end()) {
        it->second.lock.lock();
        it->second.code = op.code;
        it->second.args = op.args;
    }

    if (op.pairs == 1) {
        assert((uintptr_t)op.tables == table);
    } else {
        assert(op.pairs == req->sz_gids);
        for (int i = 0; i < req->sz_gids; i++) {
            assert(op.tables[i] == req->gids[i]);
        }
    }

    leveldb::Status status;
    for (int i = 0; i < req->sz_gids; i++) {
        status = open_db(req->gids[i]);
        if (false == status.ok()) {
            if (it != operations.end()) {
                it->second.result.errcode = kvdb_ret(status);
            }
            EE("error open db for %d, op = %d, msg = %s", table, op.code, status.ToString().c_str());
            break;
        }
    }

    if (true == status.ok()) {
        if (op.code == OP_PUT || (op.code == OP_BATCH_PUT && op.pairs == 1)) {
            leveldb::Slice dbkey((char *)op.args[0].buf, op.args[0].len);
            leveldb::Slice dbval((char *)op.args[1].buf, op.args[1].len);

            // TODO leveldb::WriteOptions() async or sync
            status = db->Put(leveldb::WriteOptions(), dbkey, dbval);
            if (it != operations.end()) {
                it->second.result.errcode = kvdb_ret(status);
                it->second.result.progress = status.ok() ? 1 : -1;
            }
            if (false == status.ok()) {
                EE("error put value for %d", table);
            }

        } else if (op.code == OP_GET) {
            leveldb::Slice dbkey((char *)op.args[0].buf, op.args[0].len);
            std::string dbval;
            
            // TODO : leveldb::ReadOptions() 
            status = db->Get(leveldb::ReadOptions(), dbkey, &dbval);
            if (false == status.ok()) {
                EE("error get value for %d\n", table);
            }
            if (it != operations.end()) {
                it->second.result.errcode = kvdb_ret(status);
                it->second.result.progress = status.ok() ? 1 : -1;
                if (true == status.ok()) {
                    // TODO
                    char * sbuf = (char *) malloc(dbval.size());
                    assert(sbuf != NULL) ;
                    memcpy(sbuf, dbval.data(), dbval.size());

                    it->second.result.buf = (uint8_t *)sbuf;
                    it->second.result.len = dbval.size();
                }
            }
       
        } else if (op.code == OP_DEL) {
            leveldb::Slice dbkey((char *)op.args[0].buf, op.args[0].len);
            // TODO
            status = db->Delete(leveldb::WriteOptions(), dbkey);
            if (false == status.ok()) {
                EE("error del value for %d\n", table);
            }
            if (it != operations.end()) {
                it->second.result.errcode = kvdb_ret(status);
                it->second.result.progress = status.ok() ? 1 : -1;
            }

        } else if (op.code == OP_BATCH_PUT) {
            assert(op.pairs > 1);
            leveldb::WriteBatch batch;
            for (int p = 0; p < op.pairs; p++) {
                leveldb::Slice dbkey((char *)op.args[2 * p].buf, op.args[2 * p].len);
                leveldb::Slice dbval((char *)op.args[2 * p + 1].buf, op.args[2 * p + 1].len);
                batch.Put(dbkey, dbval);
            }

            // TODO : WriteOptions()
            status = db->Write(leveldb::WriteOptions(), &batch);
            if (false == status.ok()) {
                EE("error batch put for tx %d, msg : %s", commit_param->id, status.ToString().c_str());
            }
            if (it != operations.end()) {
                it->second.result.errcode = kvdb_ret(status);
                it->second.result.progress = status.ok() ? 1 : -1;
            }
        }

    }

    if (it != operations.end()) {
        it->second.lock.signal();
        it->second.lock.unlock();
    }

}



int kvdb_init(char *dbhome, char * mpaxos_config_path){
  if (initialized) {
    WW("kvdb already initialized");
    return 0;
  }

  leveldb::Options options;
  options.create_if_missing = true;

  leveldb::Status status = leveldb::DB::Open(options, dbhome, &db);
  II("leveldb::DB open/create: %s", status.ToString().c_str());
  
  if (false == status.ok()) {
    EE("leveldb::DB open/create failed, msg: %s", status.ToString().c_str());
    return KVDB_RET_OPEN_FAILED ;
  }

  mpaxos_init();

  mpaxos_load_config(mpaxos_config_path);

  // register mpaxos global callback
  mpaxos_set_cb_god(mpaxos_callback);

  mpaxos_start();

  initialized = 1;

  return KVDB_RET_OK;
}


// TODO : multiple db support
int kvdb_destroy() {
    if (!initialized) {
        return KVDB_RET_UNINITIALIZED;
    }

    delete db;
//    std::map<groupid_t, leveldb::DB *>::iterator dbit;
//    for (dbit = dbs.begin(); dbit != dbs.end(); dbit ++) {
//        delete (*dbit).second;
//    }

    mpaxos_stop();
    mpaxos_destroy();
    return KVDB_RET_OK;
}



int kvdb_put(uint8_t * key, size_t klen, uint8_t * value, size_t vlen) {
    if (!initialized) {
        return KVDB_RET_UNINITIALIZED;
    }
    
    // Always map key to talbe 1 TODO
    groupid_t table = 1;
    
    operation_id_t op_id = genOperationId();
    
    operations[op_id].lock.lock();
    
    Buf keybuf(key, klen);
    Buf valbuf(value, vlen);
    Buf commit = wrap(OP_PUT, table, keybuf, valbuf);

    OperationParam * param = new OperationParam();
    param->id = op_id;

    II("id: %lu, PUT mpaxos_commit_req table = %d, value len = %zu", op_id, table, commit.len);
    
    // TODO
    mpaxos_req_t* req = (mpaxos_req_t*) malloc(sizeof(mpaxos_req_t));
    req->gids = &table;
    req->sz_gids = 1;
    req->data = commit.buf;
    req->sz_data = commit.len;
    req->cb_para = (void *)param;

    int ret = mpaxos_commit_req(req);
    assert(!ret);

    while (operations[op_id].result.progress >= 0 && operations[op_id].result.progress < 1) {
        operations[op_id].lock.wait();
    }
    operations[op_id].lock.unlock();

    return operations[op_id].result.errcode;
}


int kvdb_get(uint8_t * key, size_t klen, uint8_t **value, size_t *vlen) {
    if (!initialized) {
        return KVDB_RET_UNINITIALIZED;
    }

    // Always taoble 1 TODO 
    groupid_t table = 1;

    operation_id_t op_id = genOperationId();
    
    operations[op_id].lock.lock();

    Buf keybuf(key, klen);
    Buf commit = wrap(OP_GET, table, keybuf);

    OperationParam * param = new OperationParam();
    param->id = op_id;

    II("id: %lu, GET mpaxos_commit_req: table = %d, value len = %zu", op_id, table, commit.len);
    
    // TODO
    mpaxos_req_t * req = (mpaxos_req_t *) malloc(sizeof(mpaxos_req_t));
    req->gids = &table;
    req->sz_gids = 1;
    req->data = commit.buf;
    req->sz_data = commit.len;
    req->cb_para = (void *)param;

    int ret = mpaxos_commit_req(req);
    assert(!ret);

    while (operations[op_id].result.progress >= 0 && operations[op_id].result.progress < 1) {
        operations[op_id].lock.wait();
    }
    operations[op_id].lock.unlock();

    if (operations[op_id].result.errcode == 0) {
        *vlen = operations[op_id].result.len;
        *value = new uint8_t[*vlen];
        memcpy(*value, operations[op_id].result.buf, *vlen);
    }

    return operations[op_id].result.errcode;
}


int kvdb_del(uint8_t * key, size_t klen) {
    if (!initialized) {
        return KVDB_RET_UNINITIALIZED;
    }

    // TODO
    groupid_t table = 1;
    
    operation_id_t op_id = genOperationId();
    operations[op_id].lock.lock();
    
    Buf keybuf(key, klen);
    Buf commit = wrap(OP_DEL, table, keybuf);

    OperationParam * param = new OperationParam();
    param->id = op_id;

    II("id: %lu, DEL mpaxos_commit_req: table = %d, value len = %zu", op_id, table, commit.len);

     // TODO
    mpaxos_req_t * req = (mpaxos_req_t *) malloc(sizeof(mpaxos_req_t));
    req->gids = &table;
    req->sz_gids = 1;
    req->data = commit.buf;
    req->sz_data = commit.len;
    req->cb_para = (void *)param;

    int ret = mpaxos_commit_req(req);
    assert(!ret);
   
    while (operations[op_id].result.progress >= 0 && operations[op_id].result.progress < 1) {
        operations[op_id].lock.wait();
    }
    operations[op_id].lock.unlock();

    return operations[op_id].result.errcode;
}


int kvdb_batch_put(uint8_t **keys, size_t * klens, uint8_t ** vals, size_t * vlens, uint32_t pairs) {
    if (!initialized) {
        return KVDB_RET_UNINITIALIZED;
    }

    // TODO, keys should in the same talbe ? 
    groupid_t table = 1;
    operation_id_t op_id = genOperationId();
    operations[op_id].lock.lock();
    
    Buf * args = new Buf[pairs * 2];
    for (int p = 0; p < pairs; p++) {
        args[2 * p].buf = keys[p];
        args[2 * p].len = klens[p];
        args[2 * p + 1].buf = vals[p];
        args[2 * p + 1].len = vlens[p];
    }

    Operation op(OP_BATCH_PUT, &table, args, pairs);
    Buf commit = wrap(op);

    OperationParam * param = new OperationParam();
    param->id = op_id;
    
    II("id: %lu, BATCH_PUT mpaxos_commit_req: table = %d, value len = %zu", op_id, table, commit.len);

     // TODO
    mpaxos_req_t * req = (mpaxos_req_t *) malloc(sizeof(mpaxos_req_t));
    req->gids = &table;
    req->sz_gids = pairs;
    req->data = commit.buf;
    req->sz_data = commit.len;
    req->cb_para = (void *)param;

    int ret = mpaxos_commit_req(req);
    assert(!ret);
 
    while (operations[op_id].result.progress >= 0 && operations[op_id].result.progress < 1) {
        DD("progress = %d", operations[op_id].result.progress);
        operations[op_id].lock.wait();
    }
    DD("finish batch put");
    operations[op_id].lock.unlock();
    return operations[op_id].result.errcode;
}


int kvdb_transaction_start(){
    // TODO
    return 0;
}



