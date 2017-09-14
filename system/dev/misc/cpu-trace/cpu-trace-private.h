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

    struct ipt_device* ipt;
} cpu_trace_device_t;

void ipt_init_once(void);

zx_status_t ipt_ioctl(cpu_trace_device_t* dev, uint32_t op,
                      const void* cmd, size_t cmdlen,
                      void* reply, size_t replymax,
                      size_t* out_actual);

void ipt_release(cpu_trace_device_t* dev);
