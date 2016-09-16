// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "device-internal.h"
#include <ddk/device.h>
#include <ddk/driver.h>

#include <mxio/dispatcher.h>
#include <mxio/remoteio.h>

#include <magenta/types.h>

#include <threads.h>
#include <stdint.h>

// Nothing outside of devmgr/{devmgr,devhost,rpc-device}.c
// should be calling devmgr_*() APIs, as this could
// violate the internal locking design.

// Safe external APIs are in device.h and device_internal.h

mx_status_t devmgr_driver_add(mx_driver_t* driver);
mx_status_t devmgr_driver_remove(mx_driver_t* driver);
mx_status_t devmgr_driver_unbind(mx_driver_t* driver, mx_device_t* dev);

mx_status_t devmgr_device_add(mx_device_t* dev, mx_device_t* parent);
mx_status_t devmgr_device_add_root(mx_device_t* dev);
mx_status_t devmgr_device_remove(mx_device_t* dev);
mx_status_t devmgr_device_bind(mx_device_t* dev, const char* drv_name);
mx_status_t devmgr_device_rebind(mx_device_t* dev);
mx_status_t devmgr_device_create(mx_device_t** dev, mx_driver_t* driver,
                                 const char* name, mx_protocol_device_t* ops);
void devmgr_device_init(mx_device_t* dev, mx_driver_t* driver,
                        const char* name, mx_protocol_device_t* ops);
void devmgr_device_set_bindable(mx_device_t* dev, bool bindable);
mx_status_t devmgr_device_openat(mx_device_t* dev, mx_device_t** out,
                                 const char* path, uint32_t flags);
mx_status_t devmgr_device_close(mx_device_t* dev);
bool devmgr_is_bindable(mx_driver_t* drv, mx_device_t* dev);


// shared between devhost.c and rpc-device.c
typedef struct iostate {
    mx_device_t* dev;
    size_t io_off;
    mtx_t lock;
} iostate_t;

iostate_t* create_iostate(mx_device_t* dev);
mx_status_t devmgr_rio_handler(mxrio_msg_t* msg, mx_handle_t rh, void* cookie);

// routines devhost uses to talk to devmgr
mx_status_t devhost_add(mx_device_t* dev, mx_device_t* child);
mx_status_t devhost_remove(mx_device_t* dev);

// pci plumbing
int devmgr_get_pcidev_index(mx_device_t* dev, uint16_t* vid, uint16_t* did);
mx_status_t devmgr_create_pcidev(mx_device_t** out, uint32_t index);

// dmctl plumbing
mx_status_t devmgr_control(const char* cmd);

// device refcounts
void dev_ref_release(mx_device_t* dev);
static inline void dev_ref_acquire(mx_device_t* dev) {
    dev->refcount++;
}

// locking and lock debugging
extern mtx_t __devmgr_api_lock;
extern bool __dm_locked;

#if 0
static inline void __DM_DIE(const char* fn, int ln) {
    cprintf("OOPS: %s: %d\n", fn, ln);
    *((int*) 0x3333) = 1;
}
static inline void __DM_LOCK(const char* fn, int ln) {
    //cprintf(devmgr_is_remote ? "X" : "+");
    if (__dm_locked) __DM_DIE(fn, ln);
    mtx_lock(&__devmgr_api_lock);
    cprintf("LOCK: %s: %d\n", fn, ln);
    __dm_locked = true;
}

static inline void __DM_UNLOCK(const char* fn, int ln) {
    cprintf("UNLK: %s: %d\n", fn, ln);
    //cprintf(devmgr_is_remote ? "x" : "-");
    if (!__dm_locked) __DM_DIE(fn, ln);
    __dm_locked = false;
    mtx_unlock(&__devmgr_api_lock);
}

#define DM_LOCK() __DM_LOCK(__FILE__, __LINE__)
#define DM_UNLOCK() __DM_UNLOCK(__FILE__, __LINE__)
#else
static inline void DM_LOCK(void) {
    mtx_lock(&__devmgr_api_lock);
}

static inline void DM_UNLOCK(void) {
    mtx_unlock(&__devmgr_api_lock);
}
#endif
