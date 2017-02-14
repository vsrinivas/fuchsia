// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/device.h>

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