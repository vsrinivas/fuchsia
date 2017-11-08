// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS;

typedef struct {
    // i2c bus config
    uint8_t bus_master;
    uint8_t ten_bit;
    uint16_t address;
    uint32_t bus_speed;
    // optional protocol id for this device
    uint32_t protocol_id;
} auxdata_i2c_device_t;

__END_CDECLS;
