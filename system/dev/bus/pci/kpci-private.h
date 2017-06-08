// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/device.h>
#include <ddk/binding.h>
#include <magenta/types.h>

typedef struct kpci_device {
    mx_device_t* mxdev;
    mx_handle_t handle;
    uint32_t index;
    mx_pcie_device_info_t info;
    mx_device_prop_t props[8];
} kpci_device_t;
