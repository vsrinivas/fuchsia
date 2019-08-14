/******************************************************************************
 *
 * Copyright(c) 2005 - 2014 Intel Corporation. All rights reserved.
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

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_IWL_PHY_DB_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_IWL_PHY_DB_H_

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/iwl-op-mode.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/iwl-trans.h"

// This should be static, but is exported for unittest.
enum iwl_phy_db_section_type {
  IWL_PHY_DB_CFG = 1,
  IWL_PHY_DB_CALIB_NCH,
  IWL_PHY_DB_UNUSED,
  IWL_PHY_DB_CALIB_CHG_PAPD,
  IWL_PHY_DB_CALIB_CHG_TXP,
  IWL_PHY_DB_MAX,
};

struct iwl_phy_db* iwl_phy_db_init(struct iwl_trans* trans);

void iwl_phy_db_free(struct iwl_phy_db* phy_db);

int iwl_phy_db_set_section(struct iwl_phy_db* phy_db, struct iwl_rx_packet* pkt);

#if IS_ENABLED(CPTCFG_IWLXVT)
int iwl_phy_db_get_section_data(struct iwl_phy_db* phy_db, uint32_t type, uint8_t** data,
                                uint16_t* size, uint16_t ch_id);
#endif

// This should be static, but is exported for unittest.
zx_status_t iwl_phy_db_send_all_channel_groups(struct iwl_phy_db* phy_db,
                                               enum iwl_phy_db_section_type type,
                                               int max_ch_groups);

zx_status_t iwl_send_phy_db_data(struct iwl_phy_db* phy_db);

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_IWL_PHY_DB_H_
