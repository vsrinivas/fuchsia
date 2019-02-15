// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains internal definitions common to all arches.
// These definitions are used for communication between the cpu-trace
// device driver and the kernel only.

#ifndef LIB_ZIRCON_INTERNAL_DEVICE_CPU_TRACE_COMMON_PM_H_
#define LIB_ZIRCON_INTERNAL_DEVICE_CPU_TRACE_COMMON_PM_H_

// This is for passing buffer specs to the kernel.
typedef struct {
    zx_handle_t vmo;
} zx_pmu_buffer_t;

#endif  // LIB_ZIRCON_INTERNAL_DEVICE_CPU_TRACE_COMMON_PM_H_
