// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/protocol/platform-device.h>
#include <magenta/listnode.h>

typedef struct {
    list_node_t gpios;
    platform_device_protocol_t pdev;
} hi3660_bus_t;
