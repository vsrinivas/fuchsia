// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/device.h>
#include <ddk/protocol/platform-device.h>

typedef struct {
    zx_device_t* zxdev;
    platform_device_protocol_t pdev;
    // more stuff will be added here
} gauss_pdm_input_t;
