// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_SYSMEM_DRIVERS_SYSMEM_MACROS_H_
#define SRC_DEVICES_SYSMEM_DRIVERS_SYSMEM_MACROS_H_

#include <ddk/debug.h>

// severity can be ERROR, WARN, INFO, TRACE, SPEW.  See ddk/debug.h.
//
// Using ## __VA_ARGS__ instead of __VA_OPT__(,) __VA_ARGS__ for now, since
// __VA_OPT__ doesn't seem to be available yet.
#define LOG(severity, fmt, ...) \
  zxlogf(severity, "[%s:%s:%d] " fmt "", "sysmem", __func__, __LINE__, ##__VA_ARGS__)

#define DRIVER_ERROR(fmt, ...) LOG(ERROR, fmt, ##__VA_ARGS__)

#define DRIVER_INFO(fmt, ...) LOG(INFO, fmt, ##__VA_ARGS__)

#define DRIVER_TRACE(fmt, ...) LOG(TRACE, fmt, ##__VA_ARGS__)

#endif  // SRC_DEVICES_SYSMEM_DRIVERS_SYSMEM_MACROS_H_
