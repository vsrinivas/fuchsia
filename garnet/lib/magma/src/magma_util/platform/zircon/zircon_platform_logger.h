// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MAGMA_SRC_MAGMA_UTIL_PLATFORM_ZIRCON_ZIRCON_PLATFORM_LOGGER_H_
#define GARNET_LIB_MAGMA_SRC_MAGMA_UTIL_PLATFORM_ZIRCON_ZIRCON_PLATFORM_LOGGER_H_

#include <zircon/compiler.h>

#include "platform_logger.h"

namespace magma {

class ZirconPlatformLogger {
 public:
  __WEAK static bool InitWithFdio();
};

}  // namespace magma

#endif  // GARNET_LIB_MAGMA_SRC_MAGMA_UTIL_PLATFORM_ZIRCON_ZIRCON_PLATFORM_LOGGER_H_
