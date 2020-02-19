// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BUS_DRIVERS_PLATFORM_CPU_TRACE_H_
#define SRC_DEVICES_BUS_DRIVERS_PLATFORM_CPU_TRACE_H_

#include <zircon/compiler.h>

#include <ddk/device.h>

// This value is passed to bti_create as a marker; it does not have a particular
// meaning to anything in the system, it just needs to be unique.
// The value chosen here is the same used by acpi/include/cpu-trace.h.
// "CPUTRACE"
#define CPU_TRACE_BTI_ID 0x4350555452414345UL

// Publish a pbus device under sysroot, with access to the given BTI handle.
// Unconditionally takes ownership of the BTI handle.
zx_status_t publish_cpu_trace(zx_handle_t bti, zx_device_t* sys_root);

#endif  // SRC_DEVICES_BUS_DRIVERS_PLATFORM_CPU_TRACE_H_
