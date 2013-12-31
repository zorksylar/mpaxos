.PHONY: all clean

ifndef MAKEFILE_MAIN
    $(error Config variable not defined, Please Make in project root dir)
endif

THIS_MAKEFILE_PATH:=$(word $(words $(MAKEFILE_LIST)),$(MAKEFILE_LIST))
THIS_DIR:=$(shell cd $(dir $(THIS_MAKEFILE_PATH));pwd)
THIS_MAKEFILE:=$(notdir $(THIS_MAKEFILE_PATH))

LIB_MPAXOS_LIBS= json apr-1 apr-util-1 libprotobuf-c
LIB_MPAXOS_CC_FLAGS=$(addprefix `pkg-config --cflags , $(addsuffix `,$(LIB_MPAXOS_LIBS)))
LIB_MPAXOS_LD_FLAGS=$(addprefix `pkg-config --libs , $(addsuffix `,$(LIB_MPAXOS_LIBS)))

LIB_MPAXOS_SRC=$(wildcard $(DIR_BASE)/libmpaxos/*.c wildcard $(DIR_BASE)/libmpaxos/rpc/*.c)
LIB_MPAXOS_OBJ=$(addprefix $(DIR_OBJ)/, $(addsuffix .o,$(basename $(subst $(THIS_DIR)/,,$(LIB_MPAXOS_SRC)))))

# vim: ai:ts=4:sw=4:et!

