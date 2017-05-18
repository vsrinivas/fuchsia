// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/device.h>

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

static inline mx_status_t device_op_open(mx_device_t* dev, mx_device_t** dev_out, uint32_t flags) {
    return dev->ops->open(dev->ctx, dev_out, flags);
}

static inline mx_status_t device_op_open_at(mx_device_t* dev, mx_device_t** dev_out,
                                           const char* path, uint32_t flags) {
    return dev->ops->open_at(dev->ctx, dev_out, path, flags);
}

static inline mx_status_t device_op_close(mx_device_t* dev, uint32_t flags) {
    return dev->ops->close(dev->ctx, flags);
}

static inline void device_op_unbind(mx_device_t* dev) {
    dev->ops->unbind(dev->ctx);
}

static inline void device_op_release(mx_device_t* dev) {
    dev->ops->release(dev->ctx);
}

static inline mx_status_t device_op_suspend(mx_device_t* dev, uint32_t flags) {
    return dev->ops->suspend(dev->ctx, flags);
}

static inline mx_status_t device_op_resume(mx_device_t* dev, uint32_t flags) {
    return dev->ops->resume(dev->ctx, flags);
}
