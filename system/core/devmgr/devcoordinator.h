// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <zircon/types.h>
#include <zircon/listnode.h>

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
#define PENDING_SUSPEND 2

struct dc_devhost {
    port_handler_t ph;
    zx_handle_t hrpc;
    zx_handle_t proc;
    zx_koid_t koid;
    int32_t refcount;
    uint32_t flags;
    devhost_t* parent;

    // list of all devices on this devhost
    list_node_t devices;

    // listnode for this devhost in the all devhosts list
    list_node_t anode;

    // listnode for this devhost in the order-to-suspend list
    list_node_t snode;

    // listnode for this devhost in its parent devhost's list-of-children
    list_node_t node;

    // list of all child devhosts of this devhost
    list_node_t children;
};

#define DEV_HOST_DYING 1
#define DEV_HOST_SUSPEND 2

struct dc_device {
    zx_handle_t hrpc;
    uint32_t flags;

    port_handler_t ph;

    devhost_t* host;
    const char* name;
    const char* libname;
    const char* args;
    work_t work;
    int32_t refcount;
    uint32_t protocol_id;
    uint32_t prop_count;
    devnode_t* self;
    devnode_t* link;
    device_t* parent;
    device_t* proxy;

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

    // listnode for this device's metadata (list of dc_metadata_t)
    list_node_t metadata;

    zx_device_prop_t props[];
};

// This device is never destroyed
#define DEV_CTX_IMMORTAL      0x01

// This device requires that children are created in a
// new devhost attached to a proxy device
#define DEV_CTX_MUST_ISOLATE  0x02

// This device may be bound multiple times
#define DEV_CTX_MULTI_BIND    0x04

// This device is bound and not eligible for binding
// again until unbound.  Not allowed on MULTI_BIND ctx.
#define DEV_CTX_BOUND         0x08

// Device has been remove()'d
#define DEV_CTX_DEAD          0x10

// Device has been removed but its rpc channel is not
// torn down yet.  The rpc transport will call remove
// when it notices at which point the device will leave
// the zombie state and drop the reference associated
// with the rpc channel, allowing complete destruction.
#define DEV_CTX_ZOMBIE        0x20

// Device is a proxy -- its "parent" is the device it's
// a proxy to.
#define DEV_CTX_PROXY         0x40

// Device is not visible in devfs or bindable.
// Devices may be created in this state, but may not
// return to this state once made visible.
#define DEV_CTX_INVISIBLE     0x80

struct dc_driver {
    const char* name;
    const zx_bind_inst_t* binding;
    uint32_t binding_size;
    uint32_t flags;
    zx_handle_t dso_vmo;
    struct list_node node;
    const char* libname;
};

#define DRIVER_NAME_LEN_MAX 64

zx_status_t devfs_publish(device_t* parent, device_t* dev);
void devfs_unpublish(device_t* dev);
void devfs_advertise(device_t* dev);
void devfs_advertise_modified(device_t* dev);

device_t* coordinator_init(zx_handle_t root_job);
void coordinator(void);

void load_driver(const char* path,
                 void (*func)(driver_t* drv, const char* version));
void find_loadable_drivers(const char* path,
                           void (*func)(driver_t* drv, const char* version));

bool dc_is_bindable(driver_t* drv, uint32_t protocol_id,
                    zx_device_prop_t* props, size_t prop_count,
                    bool autobind);

#define DC_MAX_DATA 4096

// The first two fields of devcoordinator messages align
// with those of remoteio messages so we avoid needing a
// dedicated channel for forwarding OPEN operations.
// Our opcodes set the high bit to avoid overlap.
typedef struct {
    zx_txid_t txid;     // FIDL message header
    uint32_t reserved0;
    uint32_t flags;
    uint32_t op;

    union {
        zx_status_t status;
        uint32_t protocol_id;
        uint32_t value;
    };
    uint32_t datalen;
    uint32_t namelen;
    uint32_t argslen;

    uint8_t data[DC_MAX_DATA];
} dc_msg_t;

typedef struct {
    zx_txid_t txid;
    zx_status_t status;
} dc_status_t;

// This bit differentiates DC OPs from RIO OPs
#define DC_OP_ID_BIT                0x10000000

// Coord->Host Ops
#define DC_OP_CREATE_DEVICE_STUB    0x10000001
#define DC_OP_CREATE_DEVICE         0x10000002
#define DC_OP_BIND_DRIVER           0x10000003
#define DC_OP_CONNECT_PROXY         0x10000004
#define DC_OP_SUSPEND               0x10000005

// Host->Coord Ops
#define DC_OP_STATUS                0x10000010
#define DC_OP_ADD_DEVICE            0x10000011
#define DC_OP_ADD_DEVICE_INVISIBLE  0x10000012
#define DC_OP_REMOVE_DEVICE         0x10000013  // also Coord->Host
#define DC_OP_MAKE_VISIBLE          0x10000014
#define DC_OP_BIND_DEVICE           0x10000015
#define DC_OP_GET_TOPO_PATH         0x10000016
#define DC_OP_LOAD_FIRMWARE         0x10000017
#define DC_OP_GET_METADATA          0x10000018
#define DC_OP_ADD_METADATA          0x10000019
#define DC_OP_PUBLISH_METADATA      0x1000001A

// Host->Coord Ops for DmCtl
#define DC_OP_DM_COMMAND            0x10000020
#define DC_OP_DM_OPEN_VIRTCON       0x10000021
#define DC_OP_DM_WATCH              0x10000022
#define DC_OP_DM_MEXEC              0x10000023
#define DC_PATH_MAX 1024

zx_status_t dc_msg_pack(dc_msg_t* msg, uint32_t* len_out,
                        const void* data, size_t datalen,
                        const char* name, const char* args);
zx_status_t dc_msg_unpack(dc_msg_t* msg, size_t len, const void** data,
                          const char** name, const char** args);
zx_status_t dc_msg_rpc(zx_handle_t h, dc_msg_t* msg, size_t msglen,
                       zx_handle_t* handles, size_t hcount,
                       dc_status_t* rsp, size_t rsp_len, size_t* resp_actual,
                       zx_handle_t* outhandle);

extern bool dc_asan_drivers;
extern bool dc_launched_first_devhost;
