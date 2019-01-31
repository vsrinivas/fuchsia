// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/debug.h>

// Verbose logging macros useful when debugging driver behavior.  Enable by adding
// "driver.zxcrypt.log=+spew" to the kernel command line arguments when booting.
#define LOG_ENTRY() LOG_ENTRY_ARGS("")
#define LOG_ENTRY_ARGS(fmt, ...)                                                                   \
    zxlogf(SPEW, "%s:%d - %s(" fmt ")\n", __FILE__, __LINE__, __PRETTY_FUNCTION__, ##__VA_ARGS__)
