// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "device-internal.h"
#include "devcoordinator.h"

#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>

#include <fdio/remoteio.h>

#include <zircon/thread_annotations.h>
#include <zircon/types.h>

#include <threads.h>
#include <stdint.h>


// Handle IDs for USER0 handles
#define ID_HJOBROOT 4

// Nothing outside of devmgr/{devmgr,devhost,rpc-device}.c
// should be calling devhost_*() APIs, as this could
// violate the internal locking design.

// Safe external APIs are in device.h and device_internal.h

typedef struct zx_driver {
    const char* name;
    zx_driver_rec_t* driver_rec;
    const zx_driver_ops_t* ops;
    void* ctx;
    const char* libname;
    list_node_t node;
    zx_status_t status;
} zx_driver_t;

extern zx_protocol_device_t device_default_ops;

// locking and lock debugging
extern mtx_t __devhost_api_lock;
extern bool __dm_locked;

#define REQ_DM_LOCK TA_REQ(&__devhost_api_lock)
#define USE_DM_LOCK TA_GUARDED(&__devhost_api_lock)

zx_status_t devhost_device_add(zx_device_t* dev, zx_device_t* parent,
                               const zx_device_prop_t* props, uint32_t prop_count,
                               const char* proxy_args) REQ_DM_LOCK;
zx_status_t devhost_device_remove(zx_device_t* dev) REQ_DM_LOCK;
zx_status_t devhost_device_bind(zx_device_t* dev, const char* drv_libname) REQ_DM_LOCK;
zx_status_t devhost_device_rebind(zx_device_t* dev) REQ_DM_LOCK;
zx_status_t devhost_device_create(zx_driver_t* drv, zx_device_t* parent,
                                  const char* name, void* ctx,
                                  zx_protocol_device_t* ops, zx_device_t** out) REQ_DM_LOCK;
zx_status_t devhost_device_open_at(zx_device_t* dev, zx_device_t** out,
                                 const char* path, uint32_t flags) REQ_DM_LOCK;
zx_status_t devhost_device_close(zx_device_t* dev, uint32_t flags) REQ_DM_LOCK;
zx_status_t devhost_device_suspend(zx_device_t* dev, uint32_t flags) REQ_DM_LOCK;
void devhost_device_destroy(zx_device_t* dev) REQ_DM_LOCK;

zx_status_t devhost_load_firmware(zx_device_t* dev, const char* path,
                                  zx_handle_t* fw, size_t* size) REQ_DM_LOCK;

zx_status_t devhost_get_topo_path(zx_device_t* dev, char* path,
                                  size_t max, size_t* actual);

// shared between devhost.c and rpc-device.c
typedef struct devhost_iostate {
    zx_device_t* dev;
    size_t io_off;
    uint32_t flags;
    bool dead;
    port_handler_t ph;
} devhost_iostate_t;

devhost_iostate_t* create_devhost_iostate(zx_device_t* dev);
zx_status_t devhost_rio_handler(zxrio_msg_t* msg, void* cookie);

zx_status_t devhost_start_iostate(devhost_iostate_t* ios, zx_handle_t h);

// routines devhost uses to talk to dev coordinator
zx_status_t devhost_add(zx_device_t* dev, zx_device_t* child, const char* proxy_args,
                        const zx_device_prop_t* props, uint32_t prop_count);
zx_status_t devhost_remove(zx_device_t* dev);
void devhost_make_visible(zx_device_t* dev);


// device refcounts
void dev_ref_release(zx_device_t* dev);
static inline void dev_ref_acquire(zx_device_t* dev) {
    dev->refcount++;
}

zx_handle_t get_root_resource(void);

typedef struct {
    zx_device_t* parent;
    zx_device_t* child;
    zx_handle_t rpc;
} creation_context_t;

void devhost_set_creation_context(creation_context_t* ctx);

#if 0
static inline void __DM_DIE(const char* fn, int ln) {
    cprintf("OOPS: %s: %d\n", fn, ln);
    *((int*) 0x3333) = 1;
}
static inline void __DM_LOCK(const char* fn, int ln) __TA_ACQUIRE(&__devhost_api_lock) {
    //cprintf(devhost_is_remote ? "X" : "+");
    if (__dm_locked) __DM_DIE(fn, ln);
    mtx_lock(&__devhost_api_lock);
    cprintf("LOCK: %s: %d\n", fn, ln);
    __dm_locked = true;
}

static inline void __DM_UNLOCK(const char* fn, int ln) __TA_RELEASE(&__devhost_api_lock) {
    cprintf("UNLK: %s: %d\n", fn, ln);
    //cprintf(devhost_is_remote ? "x" : "-");
    if (!__dm_locked) __DM_DIE(fn, ln);
    __dm_locked = false;
    mtx_unlock(&__devhost_api_lock);
}

#define DM_LOCK() __DM_LOCK(__FILE__, __LINE__)
#define DM_UNLOCK() __DM_UNLOCK(__FILE__, __LINE__)
#else
static inline void DM_LOCK(void) __TA_ACQUIRE(&__devhost_api_lock) {
    mtx_lock(&__devhost_api_lock);
}

static inline void DM_UNLOCK(void) __TA_RELEASE(&__devhost_api_lock) {
    mtx_unlock(&__devhost_api_lock);
}
#endif
