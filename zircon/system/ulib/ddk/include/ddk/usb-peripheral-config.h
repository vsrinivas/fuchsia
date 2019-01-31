// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fuchsia/hardware/usb/peripheral/c/fidl.h>
#include <stdint.h>

struct UsbConfig {
    uint16_t vid;
    uint16_t pid;
    char manufacturer[240];
    char product[240];
    char serial[240];
    fuchsia_hardware_usb_peripheral_FunctionDescriptor functions[];
};