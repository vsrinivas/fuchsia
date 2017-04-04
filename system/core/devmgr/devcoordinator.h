// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <magenta/types.h>

#if DEVMGR
#include <fs/vfs.h>
#include "memfs-private.h"

#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>

typedef struct device_ctx {
    mx_handle_t hdevice;
    uint32_t flags;
    uint32_t protocol_id;
    uint32_t prop_count;
    VnodeDir* vnode;
    char name[MX_DEVICE_NAME_MAX];
    mx_device_prop_t props[];
} device_ctx_t;

typedef struct {
    mx_driver_t drv;
    struct list_node node;
    const char* libname;
    uint32_t flags;
} driver_ctx_t;

mx_status_t do_publish(device_ctx_t* parent, device_ctx_t* ctx);
void do_unpublish(device_ctx_t* dev);

void coordinator_init(mx_handle_t root_job);
void coordinator(void);

void coordinator_new_driver(driver_ctx_t* ctx);

void enumerate_drivers(void);
#endif

#if !DEVHOST_V2
typedef struct dev_coordinator_msg {
    uint32_t op;
    int32_t arg;
    uint32_t protocol_id;
    char name[MX_DEVICE_NAME_MAX];
} dev_coordinator_msg_t;

#define DC_OP_STATUS 0
#define DC_OP_ADD 1
#define DC_OP_REMOVE 2
#define DC_OP_SHUTDOWN 3
#endif
