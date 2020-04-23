// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_SECUREMEM_DRIVERS_AML_SECUREMEM_LOG_H_
#define SRC_DEVICES_SECUREMEM_DRIVERS_AML_SECUREMEM_LOG_H_

// for kDeviceName
#include <ddk/debug.h>

#include "device.h"

// severity can be ERROR, WARN, INFO, TRACE, SPEW.  See ddk/debug.h.
//
// Using ## __VA_ARGS__ instead of __VA_OPT__(,) __VA_ARGS__ for now, since
// __VA_OPT__ doesn't seem to be available yet.
#define LOG(severity, fmt, ...)                                                                 \
  zxlogf(severity, "[%s:%s:%d] " fmt "", amlogic_secure_mem::kDeviceName, __func__, __LINE__, \
         ##__VA_ARGS__)

#endif  // SRC_DEVICES_SECUREMEM_DRIVERS_AML_SECUREMEM_LOG_H_
