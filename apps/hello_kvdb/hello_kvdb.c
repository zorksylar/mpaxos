#include <kvdb.h>
#include <mpaxos.h>
#include <stdio.h>
#include <stdlib.h>

const static char dbhome[] = "./kvdb_home"

int main() {

    printf("%s\n", dbhome);
    dbhome[1] = 'a';
    printf("%s\n", dbhome);


    return 0;
}



