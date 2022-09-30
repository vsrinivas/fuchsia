// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Platform-dependant regulatory data structures / APIs.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_PLATFORM_REGULATORY_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_PLATFORM_REGULATORY_H_

#include <stdint.h>

#include "banjo/wlanphyimpl.h"

// The preserved channel number used in the |mcc_info| data structure.
#define MAX_MCC_INFO_CH 64

// This data structure is used to save the MCC info returned from the firmware in MCC_UPDATE_CMD.
struct mcc_info {
  wlanphy_country_t country;

  size_t num_ch;                       // total number of the channel.
  uint8_t channels[MAX_MCC_INFO_CH];   // channel number. ex: 1-14, 36-152.
  uint16_t ch_flags[MAX_MCC_INFO_CH];  // enum iwl_nvm_channel_flags
};

// Returns true if we can transmit packet out on this channel.
//
// |ch_num| starts from 1. Possible values include: 1-14, 36, 40, ...
//
bool reg_channel_is_txable(struct mcc_info* mcc_info, uint8_t ch_num);

// Given a list of channels (in |num_ch| and |ch_list|), this function will copy only allowed
// channels to the |out_list|.
//
// If the num_ch is 0, which means wildcard, it will copy all allowed channels to the |out_list|.
//
// This function returns how many channels in the |out_list|. The |out_list| must have enough space
// to store the returning values. To be safe, the caller can allocate MAX_MCC_INFO_CH elements.
//
size_t reg_filter_channels(bool active_scan, struct mcc_info* mcc_info, size_t num_ch,
                           const uint8_t* ch_list, uint8_t* out_list);

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_PLATFORM_REGULATORY_H_
