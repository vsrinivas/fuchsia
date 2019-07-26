// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/device.h>
#include <zircon/types.h>

#ifdef __x86_64__

// Intel Processor Trace

zx_status_t insntrace_bind(void* ctx, zx_device_t* parent);

#endif  // __x86_64__

// Performance Monitor
// This driver accesses the PMU of the chip as well as various other
// h/w and s/w provided counters.

zx_status_t perfmon_bind(void* ctx, zx_device_t* parent);
