// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
// Wrapper functions for C code to call C++ code.
//
// Some Banjo and MLME functions/variables/consts are C++ only. Use this file to call them.
//
// Note thar this file is include by both .c and .cc files.
//

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_CPP_WRAPPER_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_CPP_WRAPPER_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

// Returns the center frequency of the channel.
//
// Args:
//   chan_num: starts from 1. It is the channel number.
//
// Returns:
//   the frequency in MHz.
//
uint16_t get_center_freq(uint8_t ch_num);

#ifdef __cplusplus
}  // extern "C"
#endif  // __cplusplus

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_CPP_WRAPPER_H_
