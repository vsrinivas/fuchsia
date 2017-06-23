// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <magenta/types.h>
#include <magenta/listnode.h>

#include <port/port.h>

typedef struct dc_work work_t;
typedef struct dc_pending pending_t;
typedef struct dc_devhost devhost_t;
typedef struct dc_device device_t;
typedef struct dc_driver driver_t;
typedef struct dc_devnode devnode_t;

struct dc_work {
    list_node_t node;
    uint32_t op;
    uint32_t arg;
    void* ptr;
};

struct dc_pending {
    list_node_t node;
    void* ctx;
    uint32_t op;
};

#define PENDING_BIND 1

struct dc_devhost {
    port_handler_t ph;
    mx_handle_t hrpc;
    mx_handle_t proc;
    mx_koid_t koid;
    int32_t refcount;
    uint32_t flags;

    // list of all devices on this devhost
    list_node_t devices;
};

#define DEV_HOST_DYING 1

struct dc_device {
    mx_handle_t hrpc;
    mx_handle_t hrsrc;
    port_handler_t ph;
    devhost_t* host;
    const char* name;
    const char* libname;
    const char* args;
    work_t work;
    uint32_t flags;
    int32_t refcount;
    uint32_t protocol_id;
    uint32_t prop_count;
    devnode_t* self;
    devnode_t* link;
    device_t* parent;
    device_t* shadow;

    // listnode for this device in its parent's
    // list-of-children
    list_node_t node;

    // listnode for this device in its devhost's
    // list-of-devices
    list_node_t dhnode;

    // list of all child devices of this device
    list_node_t children;

    // list of outstanding requests from the devcoord
    // to this device's devhost, awaiting a response
    list_node_t pending;

    // listnode for this device in the all devices list
    list_node_t anode;

    mx_device_prop_t props[];
};

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

// Device has been remove()'d
#define DEV_CTX_DEAD       0x10

// Device has been removed but its rpc channel is not
// torn down yet.  The rpc transport will call remove
// when it notices at which point the device will leave
// the zombie state and drop the reference associated
// with the rpc channel, allowing complete destruction.
#define DEV_CTX_ZOMBIE     0x20

#define DEV_CTX_SHADOW     0x40

struct dc_driver {
    const char* name;
    const mx_bind_inst_t* binding;
    uint32_t binding_size;
    uint32_t flags;
    struct list_node node;
    const char* libname;
};

#define DRIVER_NAME_LEN_MAX 64

mx_status_t devfs_publish(device_t* parent, device_t* dev);
void devfs_unpublish(device_t* dev);

device_t* coordinator_init(mx_handle_t root_job);
void coordinator(void);

void dc_driver_added(driver_t* drv, const char* version);

void load_driver(const char* path);
void find_loadable_drivers(const char* path);

bool dc_is_bindable(driver_t* drv, uint32_t protocol_id,
                    mx_device_prop_t* props, size_t prop_count,
                    bool autobind);

#define DC_MAX_DATA 4096

// The first two fields of devcoordinator messages align
// with those of remoteio messages so we avoid needing a
// dedicated channel for forwarding OPEN operations.
// Our opcodes set the high bit to avoid overlap.
typedef struct {
    mx_txid_t txid;
    uint32_t op;

    union {
        mx_status_t status;
        uint32_t protocol_id;
    };
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
#define DC_OP_CREATE_DEVICE_STUB 0x80000001
#define DC_OP_CREATE_DEVICE      0x80000002
#define DC_OP_BIND_DRIVER        0x80000003

// Host->Coord Ops
#define DC_OP_STATUS             0x80000010
#define DC_OP_ADD_DEVICE         0x80000011
#define DC_OP_REMOVE_DEVICE      0x80000012
#define DC_OP_BIND_DEVICE        0x80000013
#define DC_OP_GET_TOPO_PATH      0x80000014

// Host->Coord Ops for DmCtl
#define DC_OP_DM_COMMAND         0x80000020
#define DC_OP_DM_OPEN_VIRTCON    0x80000021
#define DC_OP_DM_WATCH           0x80000022
#define DC_PATH_MAX 1024

mx_status_t dc_msg_pack(dc_msg_t* msg, uint32_t* len_out,
                        const void* data, size_t datalen,
                        const char* name, const char* args);
mx_status_t dc_msg_unpack(dc_msg_t* msg, size_t len, const void** data,
                          const char** name, const char** args);
mx_status_t dc_msg_rpc(mx_handle_t h, dc_msg_t* msg, size_t msglen,
                       mx_handle_t* handles, size_t hcount,
                       dc_status_t* rsp, size_t rsp_len);

void devmgr_set_mdi(mx_handle_t mdi_handle);
