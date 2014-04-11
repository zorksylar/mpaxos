#include <sys/types.h>
#include <cstdio>
#include <cstring>
#include <assert.h>
#include <map>

#include "leveldb/db.h"
#include "kvdb/kvdb.h"
#include "kvdb_log.h"
#include "mpaxos/mpaxos.h"
#include "mpaxos/mpaxos-config.h"
#include "operation.h"
#include "lock.h"


static int initialized = 0;
static leveldb::DB* db;
static std::map<operation_id_t, Operation> operations;
static std::map<groupid_t, leveldb::DB *> dbs;



static int open_db(groupid_t gid) {
    if (dbs[gid] != NULL) {
      return 0;
    }
    char tmp[25];
    memset(tmp, 0, sizeof(tmp));

}

void mpaxos_callback(groupid_t* ids, size_t sz_ids, slotid_t* slots, uint8_t *msg, size_t mlen, void * param) {
  OperationParam * commit_param = (OperationParam *) param;
  
  std::string idstr = "";
  std::string slotstr = "";
  
  for (int i = 0;i < sz_ids; i++) {
    idstr += std::to_string(ids[i]);
    slotstr += " " + std::to_string(slots[i]);
  }

  DD("mpaxos callback table.count = %zu, ids = [%s], slots = [%s], msg len = %zu, op_id = %lu", sz_ids, idstr.c_str(), slotstr.c_str(), mlen, (unsigned long)commit_param->id);

  assert(sz_ids > 0);

  groupid_t id = ids[0];        // for single table operation, id is enough

  std::map<uint64_t, Operation>::iterator it;
  it = operations.find(commit_param->id);
  assert (it != operations.end());

  Buf buf(msg, mlen);
  Operation op = unwrap(buf);  // get operation from msg
  
  if (it != operations.end()) {
    it->second.lock.lock();
    it->second.code = op.code;
    it->second.args = op.args;
  }

  if (op.parais == 1) {
    assert((uintptr_t)op.tables == id);
  } else {
    assert(op.pairs == sz_ids);
    for (int i = 0; i < sz_ids; i++) {
      assert(op.tables[i] == ids[i]);
    }
    
  }

  int rs;
  for (int i = 0; i < sz_ids; i++) {
    rs = 
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
    return KVDB_OPEN_FAILED ;
  }

  mpaxos_init();

  mpaxos_load_config(mpaxos_config_path);

  // register mpaxos global callback
  mpaxos_set_cb_god(mpaxos_callback);

  mpaxos_start();

  initialized = 1;

  return KVDB_RET_OK;
}


int kvdb_destroy() {
    if (!initialized) {
        return KVDB_RET_UNINITIALIZED;
    }
}



int kvdb_put(uint8_t * key, size_t klen, uint8_t * value, size_t vlen) {
    if (!initialized) {
        return KVDB_RET_UNITIALIZED;
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
    mpaxos_req_t req = {};
    req.gids = &table;
    req.sz_gids = 1;
    req.data = commit.buf;
    req.sz_data = commit.len;
    req.cb_para = (void *)param;

    int ret = mpaxos_commit_req(req);
    assert(!ret);

    while (operation[op_id].result.progress >= 0 && operations[op_id].result.progress < 1) {
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

    OpeartionParam * param = new OperationParam();
    param->id = op_id;

    II("id: %lu, GET mpaxos_commit_req: table = %d, value len = %zu", op_id, table, commit.len);
    
    // TODO
    mpaxos_req_t req = {};
    req.gids = &table;
    req.sz_gids = 1;
    req.data = commit.buf;
    req.sz_data = commit.len;
    req.cb_para = (void *)param;

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

    return operation[op_id].result.errcode;
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
    mpaxos_req_t req = {};
    req.gids = &table;
    req.sz_gids = 1;
    req.data = commit.buf;
    req.sz_data = commit.len;
    req.cb_para = (void *)param;

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
    groupid_t tables = 1;
    operation_id_t op_id = genOperationId();
    operations[op_id].lock.lock();
    
    Buf * args = new Buf[pairs * 2];
    for (int p = 0; p < pairs; p++) {
        args[2 * p].buf = keys[p];
        args[2 * p].len = klens[p];
        args[2 * p + 1].buf = vals[p];
        args[2 * p + 1].len = vlens[p];
    }

    Operation op(OP_BATCH_PUT, &tables, args, pairs);
    Buf commit = wrap(op);

    OperationParam * param = new OperationParam();
    param->id = op_id;
    
    II("id: %lu, BATCH_PUT mpaxos_commit_req: table = %d, value len = %zu", op_id, table, commit.len);

     // TODO
    mpaxos_req_t req = {};
    req.gids = &table;
    req.sz_gids = pairs;
    req.data = commit.buf;
    req.sz_data = commit.len;
    req.cb_para = (void *)param;

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



