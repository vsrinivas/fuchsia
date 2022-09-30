/******************************************************************************
 *
 * Copyright(c) 2005 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2015 Intel Mobile Communications GmbH
 * Copyright (C) 2018 Intel Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  * Neither the name Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *****************************************************************************/
#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_IWL_EEPROM_PARSE_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_IWL_EEPROM_PARSE_H_

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/iwl-trans.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/platform/banjo/common.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/platform/compiler.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/platform/kernel.h"

struct iwl_nvm_data {
  int n_hw_addrs;
  uint8_t hw_addr[ETH_ALEN];

  uint8_t calib_version;
  __le16 calib_voltage;

  __le16 raw_temperature;
  __le16 kelvin_temperature;
  __le16 kelvin_voltage;
  __le16 xtal_calib[2];

  bool sku_cap_band_24ghz_enable;
  bool sku_cap_band_52ghz_enable;
  bool sku_cap_11n_enable;
  bool sku_cap_11ac_enable;
  bool sku_cap_11ax_enable;
  bool sku_cap_amt_enable;
  bool sku_cap_ipan_enable;
  bool sku_cap_mimo_disabled;

  uint16_t radio_cfg_type;
  uint8_t radio_cfg_step;
  uint8_t radio_cfg_dash;
  uint8_t radio_cfg_pnum;
  uint8_t valid_tx_ant, valid_rx_ant;

  uint32_t nvm_version;
  int8_t max_tx_pwr_half_dbm;

  bool lar_enabled;
  bool vht160_supported;
  struct ieee80211_supported_band bands[fuchsia_wlan_common_MAX_BANDS];
  struct ieee80211_channel channels[];
};

/**
 * iwl_parse_eeprom_data - parse EEPROM data and return values
 *
 * @dev: device pointer we're parsing for, for debug only
 * @cfg: device configuration for parsing and overrides
 * @eeprom: the EEPROM data
 * @eeprom_size: length of the EEPROM data
 *
 * This function parses all EEPROM values we need and then
 * returns a (newly allocated) struct containing all the
 * relevant values for driver use. The struct must be freed
 * later with iwl_free_nvm_data().
 */
struct iwl_nvm_data* iwl_parse_eeprom_data(struct device* dev, const struct iwl_cfg* cfg,
                                           const uint8_t* eeprom, size_t eeprom_size);

// Setup the 'sband' structure (channel list and numbers) from the NVM 'data'.
size_t iwl_init_sband_channels(struct iwl_nvm_data* data, struct ieee80211_supported_band* sband,
                               size_t n_channels, wlan_band_t band);

void iwl_init_ht_hw_capab(const struct iwl_cfg* cfg, struct iwl_nvm_data* data,
                          struct ieee80211_sta_ht_cap* ht_info, wlan_band_t band, uint8_t tx_chains,
                          uint8_t rx_chains);

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_IWL_EEPROM_PARSE_H_
