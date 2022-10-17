// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_FIRMWARE_LIB_FASTBOOT_SPARSE_FORMAT_H_
#define SRC_FIRMWARE_LIB_FASTBOOT_SPARSE_FORMAT_H_

#include "third_party/android/platform/system/core/libsparse/sparse_format.h"
// The above header includes the
// third_party/android/platform/system/core/libsparse/sparse_defs.h which defines a `error()`
// macro. It confuses the compiler when we try to create a zx::result with zx::error(). Thus,
// we need to undefine the macro.
#undef error

#endif  // SRC_FIRMWARE_LIB_FASTBOOT_SPARSE_FORMAT_H_
