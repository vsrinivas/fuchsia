// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/device.h>
#include <magenta/types.h>

void devmgr_init(void);
void devmgr_handle_messages(void);

void devmgr_io_init(void);
void devmgr_vfs_init(void);
void devmgr_vfs_exit(void);
void devmgr_launch(const char* name, int argc, const char** argv, int stdiofd,
                   mx_handle_t handle, uint32_t type);
void devmgr_launch_devhost(const char* name, int argc, char** argv,
                           mx_handle_t hdevice, mx_handle_t hrpc);

typedef struct devhost_msg devhost_msg_t;
struct devhost_msg {
    uint32_t op;
    int32_t arg;
    uint32_t protocol_id;
    char name[MX_DEVICE_NAME_MAX];
};

#define DH_OP_STATUS 0
#define DH_OP_ADD 1
#define DH_OP_REMOVE 2
#define DH_OP_SHUTDOWN 3
