// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <threads.h>

#include <fbl/mutex.h>
#include <zircon/thread_annotations.h>
#include <zircon/types.h>

typedef struct zx_packet_guest_io zx_packet_guest_io_t;
typedef struct zx_vcpu_io zx_vcpu_io_t;

/* Stores the IO port state. */
class IoPort {
public:
    zx_status_t Init(zx_handle_t guest);

    zx_status_t Read(uint16_t port, zx_vcpu_io_t* vcpu_io) const;
    zx_status_t Write(const zx_packet_guest_io_t* io);

private:
    mutable fbl::Mutex mutex_;
#if __x86_64__
    // Index of the RTC register to use.
    uint8_t rtc_index_ TA_GUARDED(mutex_) = 0;
    // Command being issued to the i8042 controller.
    uint8_t i8042_command_ TA_GUARDED(mutex_) = 0;
    // State of power management enable register.
    uint16_t pm1_enable_ TA_GUARDED(mutex_) = 0;
#endif // __x86_64__
};
