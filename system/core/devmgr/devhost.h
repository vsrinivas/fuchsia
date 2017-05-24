// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "device-internal.h"
#include "devcoordinator.h"

#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>

#include <mxio/dispatcher.h>
#include <mxio/remoteio.h>

#include <magenta/types.h>

#include <threads.h>
#include <stdint.h>


// Handle IDs for USER0 handles
#define ID_HDEVICE 0
#define ID_HRPC 1
#define ID_HACPI 2
#define ID_HLAUNCHER 3
#define ID_HJOBROOT 4

// Nothing outside of devmgr/{devmgr,devhost,rpc-device}.c
// should be calling devhost_*() APIs, as this could
// violate the internal locking design.

// Safe external APIs are in device.h and device_internal.h

typedef struct mx_driver_rec {
    const char* name;
    mx_driver_ops_t* ops;
    void* ctx;
    const char* libname;
    list_node_t node;
    mx_status_t status;
} mx_driver_rec_t;

extern mx_protocol_device_t device_default_ops;

mx_status_t devhost_device_unbind(mx_device_t* dev);

mx_status_t devhost_device_add(mx_device_t* dev, mx_device_t* parent,
                               const mx_device_prop_t* props, uint32_t prop_count,
                               const char* businfo, mx_handle_t resource);
mx_status_t devhost_device_install(mx_device_t* dev);
mx_status_t devhost_device_add_root(mx_device_t* dev);
mx_status_t devhost_device_remove(mx_device_t* dev);
mx_status_t devhost_device_bind(mx_device_t* dev, const char* drv_libname);
mx_status_t devhost_device_rebind(mx_device_t* dev);
mx_status_t devhost_device_create(mx_device_t* parent, const char* name, void* ctx,
                                  mx_protocol_device_t* ops, mx_device_t** out);
void devhost_device_set_protocol(mx_device_t* dev, uint32_t proto_id, void* proto_ops);
void devhost_device_set_bindable(mx_device_t* dev, bool bindable);
mx_status_t devhost_device_open_at(mx_device_t* dev, mx_device_t** out,
                                 const char* path, uint32_t flags);
mx_status_t devhost_device_close(mx_device_t* dev, uint32_t flags);
void devhost_device_destroy(mx_device_t* dev);

mx_status_t devhost_load_firmware(mx_device_t* dev, const char* path,
                                  mx_handle_t* fw, size_t* size);

// shared between devhost.c and rpc-device.c
typedef struct devhost_iostate {
    mx_device_t* dev;
    size_t io_off;
    uint32_t flags;
    bool dead;
    port_handler_t ph;
} devhost_iostate_t;

devhost_iostate_t* create_devhost_iostate(mx_device_t* dev);
mx_status_t devhost_rio_handler(mxrio_msg_t* msg, void* cookie);

mx_status_t devhost_start_iostate(devhost_iostate_t* ios, mx_handle_t h);

// routines devhost uses to talk to dev coordinator
mx_status_t devhost_add(mx_device_t* dev, mx_device_t* child,
                        const char* businfo, mx_handle_t resource,
                        const mx_device_prop_t* props, uint32_t prop_count);
mx_status_t devhost_remove(mx_device_t* dev);
mx_status_t devhost_add_internal(mx_device_t* parent,
                                 const char* name, uint32_t protocol_id,
                                 mx_handle_t* _hdevice, mx_handle_t* _hrpc);
mx_status_t devhost_connect(mx_device_t* dev, mx_handle_t hdevice, mx_handle_t hrpc);

extern mxio_dispatcher_t* devhost_rio_dispatcher;


// pci plumbing
int devhost_get_pcidev_index(mx_device_t* dev, uint16_t* vid, uint16_t* did);
mx_status_t devhost_create_pcidev(mx_device_t** out, uint32_t index);


// device refcounts
void dev_ref_release(mx_device_t* dev);
static inline void dev_ref_acquire(mx_device_t* dev) {
    dev->refcount++;
}

mx_handle_t get_root_resource(void);
mx_handle_t get_sysinfo_job_root(void);
mx_handle_t get_app_launcher(void);

mx_device_t* device_create_setup(mx_device_t* parent);

// locking and lock debugging
extern mtx_t __devhost_api_lock;
extern bool __dm_locked;

#if 0
static inline void __DM_DIE(const char* fn, int ln) {
    cprintf("OOPS: %s: %d\n", fn, ln);
    *((int*) 0x3333) = 1;
}
static inline void __DM_LOCK(const char* fn, int ln) {
    //cprintf(devhost_is_remote ? "X" : "+");
    if (__dm_locked) __DM_DIE(fn, ln);
    mtx_lock(&__devhost_api_lock);
    cprintf("LOCK: %s: %d\n", fn, ln);
    __dm_locked = true;
}

static inline void __DM_UNLOCK(const char* fn, int ln) {
    cprintf("UNLK: %s: %d\n", fn, ln);
    //cprintf(devhost_is_remote ? "x" : "-");
    if (!__dm_locked) __DM_DIE(fn, ln);
    __dm_locked = false;
    mtx_unlock(&__devhost_api_lock);
}

#define DM_LOCK() __DM_LOCK(__FILE__, __LINE__)
#define DM_UNLOCK() __DM_UNLOCK(__FILE__, __LINE__)
#else
static inline void DM_LOCK(void) {
    mtx_lock(&__devhost_api_lock);
}

static inline void DM_UNLOCK(void) {
    mtx_unlock(&__devhost_api_lock);
}
#endif
