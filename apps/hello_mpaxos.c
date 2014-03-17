#include <mpaxos.h>
#include <unistd.h>

unsigned char data_[20] = "Hello MPaxos!\n";
int exit_ = 0;

void cb(mpaxos_req_t *req) {
    printf("%s", req->data);
    exit_ = 1;
}

int main () {
    mpaxos_init();
    mpaxos_config_load("mpaxos.cfg");
    mpaxos_config_set("nodename", "node1");
    mpaxos_set_cb_god(cb);
    mpaxos_start();
    mpaxos_req_t req;
    memset(&req, 0, sizeof(mpaxos_req_t));

    req.data = data_;
    req.sz_data = 20;
    mpaxos_commit_req(&req);
    while (!exit_)  {
        sleep(1);
    }
    mpaxos_destroy();
}
