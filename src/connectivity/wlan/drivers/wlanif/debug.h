// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_WLANIF_DEBUG_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_WLANIF_DEBUG_H_

#include <wlan/drivers/log.h>

// Compile out debug and trace logs for --release builds (i.e. NDEBUG is defined).
#ifdef NDEBUG
#define WLAN_DRIVER_LOG_LEVEL wlan::drivers::Log::kINFO
#else
#define WLAN_DRIVER_LOG_LEVEL wlan::drivers::Log::kTRACE
#endif

#define FILTER_CATEGORY(name, value) constexpr uint32_t name = (1 << (value))
FILTER_CATEGORY(kFiltFnTrace, 0);
// Add additional categories here.
#undef FILTER_CATEGORY

constexpr uint32_t kFiltSetting = 0;

#define ltrace_fn(msg...) ltrace(kFiltFnTrace, "wlanif-fn", msg)

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_WLANIF_DEBUG_H_
