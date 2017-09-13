// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/device.h>
#include <ddk/binding.h>
#include <zircon/types.h>

typedef struct kpci_device {
    zx_device_t* zxdev;
    zx_handle_t handle;
    uint32_t index;
    zx_pcie_device_info_t info;
    zx_device_prop_t props[8];
} kpci_device_t;
