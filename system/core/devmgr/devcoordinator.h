// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <zircon/compiler.h>
#include <zircon/types.h>
#include <zircon/listnode.h>

#include <fbl/intrusive_double_list.h>
#include <port/port.h>

namespace devmgr {

typedef struct dc_work work_t;
typedef struct dc_pending pending_t;
typedef struct dc_devhost devhost_t;
typedef struct dc_device device_t;
typedef struct dc_driver driver_t;
typedef struct dc_devnode devnode_t;

struct dc_work {
    list_node_t node;
    enum struct Op : uint32_t {
        kIdle = 0,
        kDeviceAdded = 1,
        kDriverAdded = 2,
    } op;
    uint32_t arg;
    void* ptr;
};

struct dc_pending {
    fbl::DoublyLinkedListNodeState<dc_pending*> node;
    struct Node {
        static fbl::DoublyLinkedListNodeState<dc_pending*>& node_state(
            dc_pending& obj) {
            return obj.node;
        }
    };

    void* ctx;
    enum struct Op : uint32_t {
        kBind = 1,
        kSuspend = 2,
    } op;
};

struct dc_devhost {
    dc_devhost();

    port_handler_t ph;
    zx_handle_t hrpc;
    zx_handle_t proc;
    zx_koid_t koid;
    mutable int32_t refcount_;
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

    // The AddRef and Release functions follow the contract for fbl::RefPtr.
    void AddRef() const {
        ++refcount_;
    }

    // Returns true when the last reference has been released.
    bool Release() const {
        const int32_t rc = refcount_;
        --refcount_;
        return rc == 1;
    }
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
    mutable int32_t refcount_;
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
    fbl::DoublyLinkedList<dc_pending*, dc_pending::Node> pending;

    // listnode for this device in the all devices list
    list_node_t anode;

    // listnode for this device's metadata (list of dc_metadata_t)
    list_node_t metadata;

    zx_device_prop_t* Props() {
        dc_device* end = this + 1;
        return reinterpret_cast<zx_device_prop_t*>(end);
    }

    // The AddRef and Release functions follow the contract for fbl::RefPtr.
    void AddRef() const {
        ++refcount_;
    }

    // Returns true when the last reference has been released.
    bool Release() const {
        const int32_t rc = refcount_;
        --refcount_;
        return rc == 1;
    }
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
void coordinator();

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

    enum struct Op : uint32_t {
        // This bit differentiates DC OPs from RIO OPs
        kIdBit = 0x10000000,

        // Coord->Host Ops
        kCreateDeviceStub = 0x10000001,
        kCreateDevice = 0x10000002,
        kBindDriver = 0x10000003,
        kConnectProxy = 0x10000004,
        kSuspend = 0x10000005,

        // Host->Coord Ops
        kStatus = 0x10000010,
        kAddDevice = 0x10000011,
        kAddDeviceInvisible = 0x10000012,
        kRemoveDevice = 0x10000013,  // also Coord->Host
        kMakeVisible = 0x10000014,
        kBindDevice = 0x10000015,
        kGetTopoPath = 0x10000016,
        kLoadFirmware = 0x10000017,
        kGetMetadata = 0x10000018,
        kAddMetadata = 0x10000019,
        kPublishMetadata = 0x1000001a,

        // Host->Coord Ops for DmCtl
        kDmCommand = 0x10000020,
        kDmOpenVirtcon = 0x10000021,
        kDmWatch = 0x10000022,
        kDmMexec = 0x10000023,
    } op;

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

} // namespace devmgr
