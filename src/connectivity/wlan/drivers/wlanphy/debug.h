// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_WLANPHY_DEBUG_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_WLANPHY_DEBUG_H_

#include <wlan/drivers/log.h>

#define FILTER_CATEGORY(name, value) constexpr uint32_t name = (1 << (value))
FILTER_CATEGORY(kFiltFnTrace, 0);
FILTER_CATEGORY(kFiltDeviceDebug, 1);
// Add additional categories here.
#undef FILTER_CATEGORY

constexpr uint32_t kFiltSetting = 0;

#define ldebug_device(msg...) ldebug(kFiltDeviceDebug, "wlanphy-device", msg)
#define ltrace_fn(msg...) ltrace(kFiltFnTrace, "wlanphy-fn", msg)

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_WLANPHY_DEBUG_H_
