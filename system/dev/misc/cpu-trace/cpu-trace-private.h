// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/device.h>
#include <zircon/types.h>

#ifdef __x86_64__

// Intel Processor Trace

zx_status_t insntrace_bind(void* ctx, zx_device_t* parent);

// Intel Performance Monitor

zx_status_t cpuperf_bind(void* ctx, zx_device_t* parent);

#endif // __x86_64__
