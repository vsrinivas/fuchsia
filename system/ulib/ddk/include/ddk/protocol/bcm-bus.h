// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/driver.h>
#include <magenta/compiler.h>

__BEGIN_CDECLS;

typedef struct {
    mx_status_t (*get_macid)(mx_device_t* device, uint8_t* out_mac);
    mx_status_t (*get_clock_rate)(mx_device_t* device, uint32_t id, uint32_t* out_clock);
    mx_status_t (*set_framebuffer)(mx_device_t* device, mx_paddr_t addr);
} bcm_bus_protocol_t;

__END_CDECLS;
