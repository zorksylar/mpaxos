#include <kvdb.h>
#include <mpaxos.h>
#include <stdio.h>
#include <stdlib.h>

const static char dbhome[] = "./kvdb_home";

int main() {

    int ret = KVDB_RET_OK;
    ret = kvdb_init(dbhome, "./mpaxos.cfg");
    assert(ret);

    



    

    return 0;
}



