// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "device-internal.h"
#include <ddk/device.h>
#include <ddk/driver.h>

#include <magenta/types.h>

#include <mxio/dispatcher.h>
#include <mxio/remoteio.h>

#include <threads.h>

void cprintf(const char* fmt, ...);

// Nothing outside of devmgr/main.c and devmgr/devmgr.c
// should be calling devmgr_*() APIs, as this could
// violate the internal locking design.

// Safe external APIs are in device.h and device_internal.h

mx_status_t devmgr_driver_add(mx_driver_t* driver);
mx_status_t devmgr_driver_remove(mx_driver_t* driver);
mx_status_t devmgr_driver_unbind(mx_driver_t* driver, mx_device_t* dev);

mx_status_t devmgr_device_add(mx_device_t* dev, mx_device_t* parent);
mx_status_t devmgr_device_remove(mx_device_t* dev);
mx_status_t devmgr_device_bind(mx_device_t* dev, const char* drv_name);
mx_status_t devmgr_device_rebind(mx_device_t* dev);
mx_status_t devmgr_device_create(mx_device_t** dev, mx_driver_t* driver,
                                 const char* name, mx_protocol_device_t* ops);
void devmgr_device_init(mx_device_t* dev, mx_driver_t* driver,
                        const char* name, mx_protocol_device_t* ops);
void devmgr_device_set_bindable(mx_device_t* dev, bool bindable);

mx_device_t* devmgr_device_root(void);

mx_status_t devmgr_device_open(mx_device_t* dev, mx_device_t** out, uint32_t flags);
mx_status_t devmgr_device_close(mx_device_t* dev);

mx_status_t devmgr_control(const char* cmd);

bool devmgr_is_bindable(mx_driver_t* drv, mx_device_t* dev);

// Internals
void devmgr_init(bool hostproc);
void devmgr_init_builtin_drivers(void);
void devmgr_dump(void);
void devmgr_handle_messages(void);

void devmgr_io_init(void);
void devmgr_vfs_init(void);
void devmgr_launch(const char* name, int argc, const char** argv, int stdiofd);
void devmgr_launch_devhost(const char* name, mx_handle_t h, const char* arg0, const char* arg1);

mx_status_t devmgr_get_handles(mx_device_t* dev, mx_handle_t* handles, uint32_t* ids);

int devmgr_get_pcidev_index(mx_device_t* dev, uint16_t* vid, uint16_t* did);
mx_status_t devmgr_create_pcidev(mx_device_t** out, uint32_t index);

void dev_ref_release(mx_device_t* dev);
static inline void dev_ref_acquire(mx_device_t* dev) {
    dev->refcount++;
}

typedef struct devhost_msg devhost_msg_t;
struct devhost_msg {
    uint32_t op;
    int32_t arg;
    uintptr_t device_id;
    uint32_t protocol_id;
    char namedata[MX_DEVICE_NAME_MAX + 1];
};

#define DH_OP_STATUS 0
#define DH_OP_ADD 1
#define DH_OP_REMOVE 2

mx_status_t devmgr_host_process(mx_device_t* dev, mx_driver_t* drv);
mx_status_t devmgr_handler(mx_handle_t h, void* cb, void* cookie);

// routines devhost uses to talk to devmgr
mx_status_t devhost_add(mx_device_t* dev, mx_device_t* parent);
mx_status_t devhost_remove(mx_device_t* dev);

extern bool devmgr_is_remote;
extern mx_handle_t devhost_handle;
extern mtx_t __devmgr_api_lock;

extern mxio_dispatcher_t* devmgr_rio_dispatcher;
extern mxio_dispatcher_t* devmgr_devhost_dispatcher;

mx_status_t devmgr_rio_handler(mxrio_msg_t* msg, mx_handle_t rh, void* cookie);

extern bool __dm_locked;

extern mx_handle_t root_resource_handle;

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
