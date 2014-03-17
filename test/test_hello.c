
#include "mpaxos/mpaxos.h"

static unsigned char data_[20] = "Hello MPaxos!\n";
static int exit_ = 0;

static void cb(mpaxos_req_t *req) {
    printf("%s", req->data);
    exit_ = 1;
}

START_TEST(hello) {
    mpaxos_init();
    mpaxos_config_load("./config/config.1.1");
    mpaxos_config_set("nodename", "node1");
    mpaxos_set_cb_god(cb);
    mpaxos_start();
    mpaxos_req_t req;
    memset(&req, 0, sizeof(mpaxos_req_t));

    req.data = data_;
    req.sz_data = 20;

    //req.sz_gids = 1;
    //req.gids = malloc(1 * sizeof(groupid_t));
    //req.gids[0] = 1;

    mpaxos_commit_req(&req);
    while (!exit_)  {
        sleep(1);
    }
    mpaxos_destroy();

} END_TEST

