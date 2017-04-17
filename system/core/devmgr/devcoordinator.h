// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <magenta/types.h>
#include <magenta/listnode.h>

typedef struct port_handler port_handler_t;

struct port_handler {
    mx_handle_t handle;
    mx_signals_t waitfor;
    mx_status_t (*func)(port_handler_t* ph, mx_signals_t signals);
};

typedef struct {
    mx_handle_t handle;
} port_t;

mx_status_t port_init(port_t* port);
mx_status_t port_watch(port_t* port, port_handler_t* ph);
mx_status_t port_dispatch(port_t* port);

#if DEVMGR
#include <fs/vfs.h>
#include "memfs-private.h"

#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>

typedef struct devhost_ctx {
    port_handler_t ph;
    mx_handle_t hrpc;
    mx_handle_t proc;
} devhost_ctx_t;

typedef struct device_ctx {
    mx_handle_t hdevice;
#if DEVHOST_V2
    mx_handle_t hrsrc;
    port_handler_t ph;
    devhost_ctx_t* host;
    list_node_t node;
    const char* args;
#endif
    uint32_t flags;
    uint32_t protocol_id;
    uint32_t prop_count;
    VnodeDir* vnode;
    char name[MX_DEVICE_NAME_MAX + 1];
    mx_device_prop_t props[];
} device_ctx_t;

// This device is never destroyed
#define DEV_CTX_IMMORTAL   0x01

// This device is a bus device
// (a devhost will be created to contain its children)
#define DEV_CTX_BUSDEV     0x02

// This device may be bound multiple times
#define DEV_CTX_MULTI_BIND 0x04

// This device is bound and not eligible for binding
// again until unbound.  Not allowed on MULTI_BIND ctx.
#define DEV_CTX_BOUND      0x08

#define DEV_CTX_DEAD       0x10
typedef struct {
    mx_driver_t drv;
    struct list_node node;
    const char* libname;
    uint32_t flags;
} driver_ctx_t;

#define DRIVER_NAME_LEN_MAX 64

mx_status_t do_publish(device_ctx_t* parent, device_ctx_t* ctx);
void do_unpublish(device_ctx_t* dev);

void coordinator_init(VnodeDir* vnroot, mx_handle_t root_job);
void coordinator(void);

void coordinator_new_driver(driver_ctx_t* ctx);

void enumerate_drivers(void);

bool dc_is_bindable(mx_driver_t* drv, uint32_t protocol_id,
                    mx_device_prop_t* props, size_t prop_count,
                    mx_device_t* dev, bool autobind);
#endif

#if DEVHOST_V2

#define DC_MAX_DATA 4096

// The first two fields of devcoordinator messages align
// with those of remoteio messages so we avoid needing a
// dedicated channel for forwarding OPEN operations.
// Our opcodes set the high bit to avoid overlap.
typedef struct {
    mx_txid_t txid;
    uint32_t op;

    uint32_t protocol_id;
    uint32_t datalen;
    uint32_t namelen;
    uint32_t argslen;

    uint8_t data[DC_MAX_DATA];
} dc_msg_t;

typedef struct {
    mx_txid_t txid;
    mx_status_t status;
} dc_status_t;

// Coord->Host Ops
#define DC_OP_CREATE_DEVICE  0x80000001
#define DC_OP_BIND_DRIVER    0x80000002

// Host->Coord Ops
#define DC_OP_ADD_DEVICE     0x80000011
#define DC_OP_REMOVE_DEVICE  0x80000012

mx_status_t dc_msg_pack(dc_msg_t* msg, uint32_t* len_out,
                        const void* data, size_t datalen,
                        const char* name, const char* args);
mx_status_t dc_msg_unpack(dc_msg_t* msg, size_t len, const void** data,
                          const char** name, const char** args);

#else
typedef struct dev_coordinator_msg {
    uint32_t op;
    int32_t arg;
    uint32_t protocol_id;
    char name[MX_DEVICE_NAME_MAX];
} dev_coordinator_msg_t;

#define DC_OP_STATUS 0
#define DC_OP_ADD 1
#define DC_OP_REMOVE 2
#define DC_OP_SHUTDOWN 3
#endif
