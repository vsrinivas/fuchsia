// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_SYSMEM_DRIVERS_SYSMEM_MACROS_H_
#define SRC_DEVICES_SYSMEM_DRIVERS_SYSMEM_MACROS_H_

#include <lib/ddk/debug.h>

// severity can be ERROR, WARN, INFO, DEBUG, TRACE.  See ddk/debug.h.
#define LOG(severity, fmt, ...) zxlogf(severity, fmt, ##__VA_ARGS__)

#define DRIVER_ERROR(fmt, ...) LOG(ERROR, fmt, ##__VA_ARGS__)

#define DRIVER_INFO(fmt, ...) LOG(INFO, fmt, ##__VA_ARGS__)

#define DRIVER_DEBUG(fmt, ...) LOG(DEBUG, fmt, ##__VA_ARGS__)

#endif  // SRC_DEVICES_SYSMEM_DRIVERS_SYSMEM_MACROS_H_
