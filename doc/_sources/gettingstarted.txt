Getting Started
===============

Compile
-------

Get MPaxos source codes from github:

.. code-block:: bash

   git clone https://github.com/msmummy/mpaxos.git
   cd mpaxos

MPaxos requires a few other libraries, including:
 * pkg-config
 * apache portable runtime
 * berkerly db
 * libprotobuf-c
 * json-c
 * check

These libraries can be installed in various ways. But you have to make sure they can be accessed by pkg-config.

For example, in Ubuntu versions >= 12.04 (previous version might work, but untested), you need to:

.. code-block:: bash

   sudo apt-get install libdb5.1-dev -y
   sudo apt-get install libprotobuf* protobuf-c-compiler -y
   sudo apt-get install libapr1-dev libaprutil1-dev  -y
   sudo apt-get install libjson0-dev -y
   sudo apt-get install check -y

And because in Ubuntu, pkg-config cannot access protobuf-c by default, you need to do this mannualy:

.. code-block:: bash

   sudo cp ./config/libprotobuf-c.pc /usr/lib/pkgconfig/

MPaxos use WAF for the compilation.

.. code-block:: bash

   ./waf configure
   ./waf
 
On successful build, you should read:

.. code-block:: bash

   'build' finished successfully (0.015s)

Install the headers, libraries and pkg-config files.

.. code-block:: bash

   sudo ./waf install

An Example
----------

Next we write a simple example running MPaxos framework on a single machine.

First write a configuration file. Save it as ``mpaxos.cfg``:

.. code-block:: json

    {
        "nodes":[
        {   
            "name": "node1",
            "addr": "localhost",
            "port": 8881
        }   
        ]   
    }

Then the following ``hello_mpaxos.c``:

.. code-block:: c

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

Compile.

.. code-block:: bash
   
   gcc hello_mpaxos.c `pkg-config --cflags --libs mpaxos` -o hello_mpaxos.out

Run.

.. code-block:: bash
   
   ./hello_mpaxos.out

You should get:

.. code-block:: bash
   
   Hello MPaxos!

Wow!

