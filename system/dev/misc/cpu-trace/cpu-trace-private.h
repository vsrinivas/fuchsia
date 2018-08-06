// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stddef.h>
#include <threads.h>

#include <zircon/types.h>

typedef struct cpu_trace_device {
    mtx_t lock;

    // Only one open of this device is supported at a time. KISS for now.
    bool opened;

#ifdef __x86_64__
    struct insntrace_device* insntrace;
    struct cpuperf_device* cpuperf;
#endif

    zx_handle_t bti;
} cpu_trace_device_t;

#ifdef __x86_64__

// Intel Processor Trace

void insntrace_init_once(void);

zx_status_t insntrace_ioctl(cpu_trace_device_t* dev, uint32_t op,
                            const void* cmd, size_t cmdlen,
                            void* reply, size_t replymax,
                            size_t* out_actual);


void insntrace_release(cpu_trace_device_t* dev);

// Intel Performance Monitor

void cpuperf_init_once(void);

zx_status_t cpuperf_ioctl(cpu_trace_device_t* dev, uint32_t op,
                          const void* cmd, size_t cmdlen,
                          void* reply, size_t replymax,
                          size_t* out_actual);

void cpuperf_release(cpu_trace_device_t* dev);

#endif // __x86_64__
