// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/device.h>

typedef struct proxy_iostate proxy_iostate_t;

struct zx_device {
    uintptr_t magic;

    zx_protocol_device_t* ops;

    // reserved for driver use; will not be touched by devmgr
    void* ctx;

    uint32_t flags;
    uint32_t refcount;

    zx_handle_t event;
    zx_handle_t local_event;
    zx_handle_t rpc;

    // most devices implement a single
    // protocol beyond the base device protocol
    uint32_t protocol_id;
    void* protocol_ops;

    // driver that has published this device
    zx_driver_t* driver;

    // parent in the device tree
    zx_device_t* parent;

    // for the parent's device_list
    struct list_node node;

    // list of this device's children in the device tree
    struct list_node children;

    // list node for the defer_device_list
    struct list_node defer;

    // iostate
    void* ios;
    proxy_iostate_t* proxy_ios;

    char name[ZX_DEVICE_NAME_MAX + 1];
};

// zx_device_t objects must be created or initialized by the driver manager's
// device_create() function.  Drivers MAY NOT touch any
// fields in the zx_device_t, except for the protocol_id and protocol_ops
// fields which it may fill out after init and before device_add() is called,
// and the ctx field which may be used to store driver-specific data.

#define DEV_FLAG_DEAD           0x00000001  // being deleted
#define DEV_FLAG_VERY_DEAD      0x00000002  // safe for ref0 and release()
#define DEV_FLAG_UNBINDABLE     0x00000004  // nobody may bind to this device
#define DEV_FLAG_BUSY           0x00000010  // device being created
#define DEV_FLAG_INSTANCE       0x00000020  // this device was created-on-open
#define DEV_FLAG_MULTI_BIND     0x00000080  // this device accepts many children
#define DEV_FLAG_ADDED          0x00000100  // device_add() has been called for this device
#define DEV_FLAG_INVISIBLE      0x00000200  // device not visible via devfs
#define DEV_FLAG_UNBOUND        0x00000400  // informed that it should self-delete asap
#define DEV_FLAG_WANTS_REBIND   0x00000800  // when last child goes, rebind this device

#define DEV_MAGIC 'MDEV'

zx_status_t device_bind(zx_device_t* dev, const char* drv_libname);
zx_status_t device_open_at(zx_device_t* dev, zx_device_t** out, const char* path, uint32_t flags);
zx_status_t device_close(zx_device_t* dev, uint32_t flags);

static inline zx_status_t dev_op_open(zx_device_t* dev, zx_device_t** dev_out, uint32_t flags) {
    return dev->ops->open(dev->ctx, dev_out, flags);
}

static inline zx_status_t dev_op_open_at(zx_device_t* dev, zx_device_t** dev_out,
                                           const char* path, uint32_t flags) {
    return dev->ops->open_at(dev->ctx, dev_out, path, flags);
}

static inline zx_status_t dev_op_close(zx_device_t* dev, uint32_t flags) {
    return dev->ops->close(dev->ctx, flags);
}

static inline void dev_op_unbind(zx_device_t* dev) {
    dev->ops->unbind(dev->ctx);
}

static inline void dev_op_release(zx_device_t* dev) {
    dev->ops->release(dev->ctx);
}

static inline zx_status_t dev_op_suspend(zx_device_t* dev, uint32_t flags) {
    return dev->ops->suspend(dev->ctx, flags);
}

static inline zx_status_t dev_op_resume(zx_device_t* dev, uint32_t flags) {
    return dev->ops->resume(dev->ctx, flags);
}

static inline zx_status_t dev_op_read(zx_device_t* dev, void* buf, size_t count, zx_off_t off,
                                         size_t* actual) {
    return dev->ops->read(dev->ctx, buf, count, off, actual);
}

static inline zx_status_t dev_op_write(zx_device_t* dev, const void* buf, size_t count,
                                          zx_off_t off, size_t* actual) {
    return dev->ops->write(dev->ctx, buf, count, off, actual);
}

static inline zx_off_t dev_op_get_size(zx_device_t* dev) {
    return dev->ops->get_size(dev->ctx);
}

static inline zx_status_t dev_op_ioctl(zx_device_t* dev, uint32_t op,
                                      const void* in_buf, size_t in_len,
                                      void* out_buf, size_t out_len, size_t* out_actual) {
    return dev->ops->ioctl(dev->ctx, op, in_buf, in_len, out_buf, out_len, out_actual);
}
