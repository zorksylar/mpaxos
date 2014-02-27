#include <mpaxos.h>

char data[20] = "Hello MPaxos!\n";
int do_exit = 0;

void cb(mpaxos_req_t *req) {
    printf("%s", req->data); 
    do_exit = 1;
}

int main () {
    mpaxos_init();
    mpaxos_config_load("./apps/mpaxos.cfg");
    mpaxos_config_set("nodename", "node1");
    mpaxos_set_cb_god(cb);
    mpaxos_start();
    mpaxos_req_t *req;
    memset(&req, 0, sizeof(mpaxos_req_t));
    req->data = data;
    req->sz_data = 20;
     
    mpaxos_commit_req(req);
    while (!do_exit)  {
        sleep(1);
    }
    mpaxos_destroy(); 
} 

