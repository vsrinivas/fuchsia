// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <threads.h>

#include <zircon/types.h>

__BEGIN_CDECLS

typedef struct zx_packet_guest_io zx_packet_guest_io_t;
typedef struct zx_vcpu_io zx_vcpu_io_t;

/* Stores the IO port state. */
typedef struct io_port {
    mtx_t mutex;
    // Index of the RTC register to use.
    uint8_t rtc_index;
    // Command being issued to the i8042 controller.
    uint8_t i8042_command;
    // State of power management enable register.
    uint16_t pm1_enable;
} io_port_t;

void io_port_init(io_port_t* io_port);
zx_status_t io_port_read(const io_port_t* io_port, uint16_t port, zx_vcpu_io_t* vcpu_io);
zx_status_t io_port_write(io_port_t* io_port, const zx_packet_guest_io_t* io);

__END_CDECLS
