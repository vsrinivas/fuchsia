// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_DEV_SECUREMEM_AML_SECUREMEM_LOG_H_
#define ZIRCON_SYSTEM_DEV_SECUREMEM_AML_SECUREMEM_LOG_H_

// for kDeviceName
#include <ddk/debug.h>

#include "device.h"

// severity can be ERROR, WARN, INFO, TRACE, SPEW.  See ddk/debug.h.
//
// Using ## __VA_ARGS__ instead of __VA_OPT__(,) __VA_ARGS__ for now, since
// __VA_OPT__ doesn't seem to be available yet.
#define LOG(severity, fmt, ...)                                                                 \
  zxlogf(severity, "[%s:%s:%d] " fmt "\n", amlogic_secure_mem::kDeviceName, __func__, __LINE__, \
         ##__VA_ARGS__)

#endif  // ZIRCON_SYSTEM_DEV_SECUREMEM_AML_SECUREMEM_LOG_H_
