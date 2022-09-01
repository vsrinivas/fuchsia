// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.
//
#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_LIB_LOG_CPP_TEST_ZX_TICKS_OVERRIDE_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_LIB_LOG_CPP_TEST_ZX_TICKS_OVERRIDE_H_

#include <zircon/syscalls.h>

// Defined in zx_ticks_override.cc
// Allows tests to control how much time is passing in tests.
void zx_ticks_set(zx_ticks_t ticks);
void zx_ticks_increment(zx_ticks_t ticks);

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_LIB_LOG_CPP_TEST_ZX_TICKS_OVERRIDE_H_
