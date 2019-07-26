// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_DEV_SYSMEM_SYSMEM_MACROS_H_
#define ZIRCON_SYSTEM_DEV_SYSMEM_SYSMEM_MACROS_H_

#include <ddk/debug.h>

#define DRIVER_ERROR(fmt, ...) zxlogf(ERROR, "[%s %d] " fmt, __func__, __LINE__, ##__VA_ARGS__)

#define DRIVER_INFO(fmt, ...) zxlogf(INFO, "[%s %d] " fmt, __func__, __LINE__, ##__VA_ARGS__)

#endif  // ZIRCON_SYSTEM_DEV_SYSMEM_SYSMEM_MACROS_H_
