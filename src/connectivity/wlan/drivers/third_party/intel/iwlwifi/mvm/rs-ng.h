/******************************************************************************
 *
 * Copyright(c) 2012 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2013 - 2015 Intel Mobile Communications GmbH
 * Copyright(c) 2016 - 2017 Intel Deutschland GmbH
 * Copyright(c) 2018        Intel Corporation
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
 *
 *****************************************************************************/
#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_MVM_RS_NG_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_MVM_RS_NG_H_

#include <ddk/hw/wlan/wlaninfo.h>

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/iwl-trans.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/mvm/fw-api.h"

#define U08 uint8_t
#define U16 uint16_t
#define U32 uint32_t
#define U64 uint64_t
#define INLINE inline

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/mvm/API_rates.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/mvm/_rateScaleMng.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/mvm/apiGroupDatapath.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/mvm/apiVersion.h"

#define RS_NAME "iwlwifi-rs"

#define LINK_QUAL_AGG_FRAME_LIMIT_DEF (63)
#define LINK_QUAL_AGG_FRAME_LIMIT_GEN2_DEF (64)

#define RS_DRV_DATA_LQ_COLOR_POS (8)
#define RS_DRV_DATA_PACK(lq_color, reduced_txp) \
  ((void*)(((lq_color) << RS_DRV_DATA_LQ_COLOR_POS) | ((uintptr_t)reduced_txp)))

struct iwl_mvm;
struct iwl_mvm_sta;

struct iwl_lq_sta_rs_fw_pers {
  struct iwl_mvm* drv;
  uint32_t sta_id;
  uint8_t chains;
  int8_t chain_signal[IEEE80211_MAX_CHAINS];
  int8_t last_rssi;
#ifdef CPTCFG_MAC80211_DEBUGFS
  uint32_t dbg_fixed_rate;
  uint32_t dbg_agg_frame_count_lim;
#endif
};

struct iwl_lq_sta_rs_fw {
  uint32_t last_rate_n_flags;

  struct iwl_lq_sta_rs_fw_pers pers;
};

struct iwl_lq_sta {
  struct iwl_lq_cmd lq;

  RS_MNG_STA_INFO_S pers;
};

int iwl_mvm_rate_control_register(void);
void iwl_mvm_rate_control_unregister(void);
int iwl_mvm_tx_protection(struct iwl_mvm* mvm, struct iwl_mvm_sta* mvmsta, bool enable);
void iwl_mvm_rs_rate_init(struct iwl_mvm* mvm, struct ieee80211_sta* sta, wlan_info_band_t band,
                          bool update);
void iwl_mvm_rs_tx_status(struct iwl_mvm* mvm, struct ieee80211_sta* sta, int tid,
                          struct ieee80211_tx_info* info, bool is_ndp);
#ifdef CPTCFG_IWLWIFI_DEBUGFS
void iwl_mvm_reset_frame_stats(struct iwl_mvm* mvm);
#endif

void iwl_mvm_tlc_update_notif(struct iwl_mvm* mvm, struct iwl_rx_cmd_buffer* rxb);
void rs_fw_rate_init(struct iwl_mvm* mvm, struct ieee80211_sta* sta, wlan_info_band_t band,
                     bool update);
void iwl_mvm_rs_add_sta(struct iwl_mvm* mvm, struct iwl_mvm_sta* mvmsta);
int rs_fw_tx_protection(struct iwl_mvm* mvm, struct iwl_mvm_sta* mvmsta, bool enable);

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_MVM_RS_NG_H_
