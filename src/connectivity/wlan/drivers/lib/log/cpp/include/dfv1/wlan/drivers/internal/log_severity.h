// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_LIB_LOG_CPP_INCLUDE_DFV1_WLAN_DRIVERS_INTERNAL_LOG_SEVERITY_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_LIB_LOG_CPP_INCLUDE_DFV1_WLAN_DRIVERS_INTERNAL_LOG_SEVERITY_H_

#include <lib/ddk/debug.h>

#define LOG_SEVERITY_TYPE fx_log_severity_t
#define LOG_SEVERITY(s) DDK_LOG_##s

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_LIB_LOG_CPP_INCLUDE_DFV1_WLAN_DRIVERS_INTERNAL_LOG_SEVERITY_H_
