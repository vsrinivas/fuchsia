// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/device.h>

struct mx_device {
    uintptr_t magic;

    mx_protocol_device_t* ops;

    // reserved for driver use; will not be touched by devmgr
    void* ctx;

    uint32_t flags;
    uint32_t refcount;

    mx_handle_t event;
    mx_handle_t local_event;
    mx_handle_t rpc;
    mx_handle_t resource;

    // most devices implement a single
    // protocol beyond the base device protocol
    uint32_t protocol_id;
    void* protocol_ops;

    // driver that has published this device
    mx_driver_t* driver;

    // parent in the device tree
    mx_device_t* parent;

    // driver that is bound to this device, NULL if unbound
    mx_driver_t* owner;

    void* owner_cookie;

    // for the parent's device_list
    struct list_node node;

    // list of this device's children in the device tree
    struct list_node children;

    // list of this device's instances
    struct list_node instances;

    // iostate
    void* ios;

    char name[MX_DEVICE_NAME_MAX + 1];
};

// mx_device_t objects must be created or initialized by the driver manager's
// device_create() function.  Drivers MAY NOT touch any
// fields in the mx_device_t, except for the protocol_id and protocol_ops
// fields which it may fill out after init and before device_add() is called,
// and the ctx field which may be used to store driver-specific data.

#define DEV_FLAG_DEAD           0x00000001  // being deleted
#define DEV_FLAG_VERY_DEAD      0x00000002  // safe for ref0 and release()
#define DEV_FLAG_UNBINDABLE     0x00000004  // nobody may bind to this device
#define DEV_FLAG_BUSY           0x00000010  // device being created
#define DEV_FLAG_INSTANCE       0x00000020  // this device was created-on-open
#define DEV_FLAG_MULTI_BIND     0x00000080  // this device accepts many children
#define DEV_FLAG_ADDED          0x00000100  // device_add() has been called for this device

#define DEV_MAGIC 'MDEV'

mx_status_t device_bind(mx_device_t* dev, const char* drv_libname);
mx_status_t device_open_at(mx_device_t* dev, mx_device_t** out, const char* path, uint32_t flags);
mx_status_t device_close(mx_device_t* dev, uint32_t flags);

static inline mx_status_t dev_op_open(mx_device_t* dev, mx_device_t** dev_out, uint32_t flags) {
    return dev->ops->open(dev->ctx, dev_out, flags);
}

static inline mx_status_t dev_op_open_at(mx_device_t* dev, mx_device_t** dev_out,
                                           const char* path, uint32_t flags) {
    return dev->ops->open_at(dev->ctx, dev_out, path, flags);
}

static inline mx_status_t dev_op_close(mx_device_t* dev, uint32_t flags) {
    return dev->ops->close(dev->ctx, flags);
}

static inline void dev_op_unbind(mx_device_t* dev) {
    dev->ops->unbind(dev->ctx);
}

static inline void dev_op_release(mx_device_t* dev) {
    dev->ops->release(dev->ctx);
}

static inline mx_status_t dev_op_suspend(mx_device_t* dev, uint32_t flags) {
    return dev->ops->suspend(dev->ctx, flags);
}

static inline mx_status_t dev_op_resume(mx_device_t* dev, uint32_t flags) {
    return dev->ops->resume(dev->ctx, flags);
}

static inline mx_status_t dev_op_read(mx_device_t* dev, void* buf, size_t count, mx_off_t off,
                                         size_t* actual) {
    return dev->ops->read(dev->ctx, buf, count, off, actual);
}

static inline mx_status_t dev_op_write(mx_device_t* dev, const void* buf, size_t count,
                                          mx_off_t off, size_t* actual) {
    return dev->ops->write(dev->ctx, buf, count, off, actual);
}

static inline mx_off_t dev_op_get_size(mx_device_t* dev) {
    return dev->ops->get_size(dev->ctx);
}

static inline mx_status_t dev_op_ioctl(mx_device_t* dev, uint32_t op,
                                      const void* in_buf, size_t in_len,
                                      void* out_buf, size_t out_len, size_t* out_actual) {
    return dev->ops->ioctl(dev->ctx, op, in_buf, in_len, out_buf, out_len, out_actual);
}
