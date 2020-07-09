// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BLOCK_DRIVERS_BLOCK_VERITY_DEBUG_H_
#define SRC_DEVICES_BLOCK_DRIVERS_BLOCK_VERITY_DEBUG_H_

#include <ddk/debug.h>

// Verbose logging macros useful when debugging driver behavior.  Enable by adding
// "driver.block_verity.log=+spew" to the kernel command line arguments when booting.
#define LOG_ENTRY() LOG_ENTRY_ARGS("")
#define LOG_ENTRY_ARGS(fmt, ...) \
  zxlogf(TRACE, "%s:%d - %s(" fmt ")", __FILE__, __LINE__, __PRETTY_FUNCTION__, ##__VA_ARGS__)

#endif  // SRC_DEVICES_BLOCK_DRIVERS_BLOCK_VERITY_DEBUG_H_
