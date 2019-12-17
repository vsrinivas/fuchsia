/******************************************************************************
 *
 * Copyright(c) 2013 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2013 - 2014 Intel Mobile Communications GmbH
 * Copyright(c) 2017        Intel Deutschland GmbH
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
 *****************************************************************************/

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_FW_API_COEX_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_FW_API_COEX_H_

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/fuchsia_porting.h"

#define BITS(nb) (BIT(nb) - 1)

enum iwl_bt_coex_lut_type {
  BT_COEX_TIGHT_LUT = 0,
  BT_COEX_LOOSE_LUT,
  BT_COEX_TX_DIS_LUT,

  BT_COEX_MAX_LUT,
  BT_COEX_INVALID_LUT = 0xff,
}; /* BT_COEX_DECISION_LUT_INDEX_API_E_VER_1 */

#define BT_REDUCED_TX_POWER_BIT BIT(7)

enum iwl_bt_coex_mode {
  BT_COEX_DISABLE = 0x0,
  BT_COEX_NW = 0x1,
  BT_COEX_BT = 0x2,
  BT_COEX_WIFI = 0x3,
}; /* BT_COEX_MODES_E */

enum iwl_bt_coex_enabled_modules {
  BT_COEX_MPLUT_ENABLED = BIT(0),
  BT_COEX_MPLUT_BOOST_ENABLED = BIT(1),
  BT_COEX_SYNC2SCO_ENABLED = BIT(2),
  BT_COEX_CORUN_ENABLED = BIT(3),
  BT_COEX_HIGH_BAND_RET = BIT(4),
}; /* BT_COEX_MODULES_ENABLE_E_VER_1 */

/**
 * struct iwl_bt_coex_cmd - bt coex configuration command
 * @mode: &enum iwl_bt_coex_mode
 * @enabled_modules: &enum iwl_bt_coex_enabled_modules
 *
 * The structure is used for the BT_COEX command.
 */
struct iwl_bt_coex_cmd {
  __le32 mode;
  __le32 enabled_modules;
} __packed; /* BT_COEX_CMD_API_S_VER_6 */

/**
 * struct iwl_bt_coex_reduced_txp_update_cmd
 * @reduced_txp: bit BT_REDUCED_TX_POWER_BIT to enable / disable, rest of the
 *  bits are the sta_id (value)
 */
struct iwl_bt_coex_reduced_txp_update_cmd {
  __le32 reduced_txp;
} __packed; /* BT_COEX_UPDATE_REDUCED_TX_POWER_API_S_VER_1 */

/**
 * struct iwl_bt_coex_ci_cmd - bt coex channel inhibition command
 * @bt_primary_ci: primary channel inhibition bitmap
 * @primary_ch_phy_id: primary channel PHY ID
 * @bt_secondary_ci: secondary channel inhibition bitmap
 * @secondary_ch_phy_id: secondary channel PHY ID
 *
 * Used for BT_COEX_CI command
 */
struct iwl_bt_coex_ci_cmd {
  __le64 bt_primary_ci;
  __le32 primary_ch_phy_id;

  __le64 bt_secondary_ci;
  __le32 secondary_ch_phy_id;
} __packed; /* BT_CI_MSG_API_S_VER_2 */

#define BT_MBOX(n_dw, _msg, _pos, _nbits)                                      \
  BT_MBOX##n_dw##_##_msg##_POS = (_pos), BT_MBOX##n_dw##_##_msg = BITS(_nbits) \
                                                                  << BT_MBOX##n_dw##_##_msg##_POS

enum iwl_bt_mxbox_dw0 {
  BT_MBOX(0, LE_SLAVE_LAT, 0, 3),
  BT_MBOX(0, LE_PROF1, 3, 1),
  BT_MBOX(0, LE_PROF2, 4, 1),
  BT_MBOX(0, LE_PROF_OTHER, 5, 1),
  BT_MBOX(0, CHL_SEQ_N, 8, 4),
  BT_MBOX(0, INBAND_S, 13, 1),
  BT_MBOX(0, LE_MIN_RSSI, 16, 4),
  BT_MBOX(0, LE_SCAN, 20, 1),
  BT_MBOX(0, LE_ADV, 21, 1),
  BT_MBOX(0, LE_MAX_TX_POWER, 24, 4),
  BT_MBOX(0, OPEN_CON_1, 28, 2),
};

enum iwl_bt_mxbox_dw1 {
  BT_MBOX(1, BR_MAX_TX_POWER, 0, 4),
  BT_MBOX(1, IP_SR, 4, 1),
  BT_MBOX(1, LE_MSTR, 5, 1),
  BT_MBOX(1, AGGR_TRFC_LD, 8, 6),
  BT_MBOX(1, MSG_TYPE, 16, 3),
  BT_MBOX(1, SSN, 19, 2),
};

enum iwl_bt_mxbox_dw2 {
  BT_MBOX(2, SNIFF_ACT, 0, 3),
  BT_MBOX(2, PAG, 3, 1),
  BT_MBOX(2, INQUIRY, 4, 1),
  BT_MBOX(2, CONN, 5, 1),
  BT_MBOX(2, SNIFF_INTERVAL, 8, 5),
  BT_MBOX(2, DISC, 13, 1),
  BT_MBOX(2, SCO_TX_ACT, 16, 2),
  BT_MBOX(2, SCO_RX_ACT, 18, 2),
  BT_MBOX(2, ESCO_RE_TX, 20, 2),
  BT_MBOX(2, SCO_DURATION, 24, 6),
};

enum iwl_bt_mxbox_dw3 {
  BT_MBOX(3, SCO_STATE, 0, 1),
  BT_MBOX(3, SNIFF_STATE, 1, 1),
  BT_MBOX(3, A2DP_STATE, 2, 1),
  BT_MBOX(3, ACL_STATE, 3, 1),
  BT_MBOX(3, MSTR_STATE, 4, 1),
  BT_MBOX(3, OBX_STATE, 5, 1),
  BT_MBOX(3, A2DP_SRC, 6, 1),
  BT_MBOX(3, OPEN_CON_2, 8, 2),
  BT_MBOX(3, TRAFFIC_LOAD, 10, 2),
  BT_MBOX(3, CHL_SEQN_LSB, 12, 1),
  BT_MBOX(3, INBAND_P, 13, 1),
  BT_MBOX(3, MSG_TYPE_2, 16, 3),
  BT_MBOX(3, SSN_2, 19, 2),
  BT_MBOX(3, UPDATE_REQUEST, 21, 1),
};

#define BT_MBOX_MSG(_notif, _num, _field)                                  \
  ((le32_to_cpu((_notif)->mbox_msg[(_num)]) & BT_MBOX##_num##_##_field) >> \
   BT_MBOX##_num##_##_field##_POS)

#define BT_MBOX_PRINT(_num, _field, _end)                         \
  pos += scnprintf(buf + pos, bufsz - pos, "\t%s: %d%s", #_field, \
                   BT_MBOX_MSG(notif, _num, _field), true ? "\n" : ", ");
enum iwl_bt_activity_grading {
  BT_OFF = 0,
  BT_ON_NO_CONNECTION = 1,
  BT_LOW_TRAFFIC = 2,
  BT_HIGH_TRAFFIC = 3,
  BT_VERY_HIGH_TRAFFIC = 4,

  BT_MAX_AG,
}; /* BT_COEX_BT_ACTIVITY_GRADING_API_E_VER_1 */

enum iwl_bt_ci_compliance {
  BT_CI_COMPLIANCE_NONE = 0,
  BT_CI_COMPLIANCE_PRIMARY = 1,
  BT_CI_COMPLIANCE_SECONDARY = 2,
  BT_CI_COMPLIANCE_BOTH = 3,
}; /* BT_COEX_CI_COMPLIENCE_E_VER_1 */

/**
 * struct iwl_bt_coex_profile_notif - notification about BT coex
 * @mbox_msg: message from BT to WiFi
 * @msg_idx: the index of the message
 * @bt_ci_compliance: enum %iwl_bt_ci_compliance
 * @primary_ch_lut: LUT used for primary channel &enum iwl_bt_coex_lut_type
 * @secondary_ch_lut: LUT used for secondary channel &enum iwl_bt_coex_lut_type
 * @bt_activity_grading: the activity of BT &enum iwl_bt_activity_grading
 * @ttc_status: is TTC enabled - one bit per PHY
 * @rrc_status: is RRC enabled - one bit per PHY
 * @reserved: reserved
 */
struct iwl_bt_coex_profile_notif {
  __le32 mbox_msg[4];
  __le32 msg_idx;
  __le32 bt_ci_compliance;

  __le32 primary_ch_lut;
  __le32 secondary_ch_lut;
  __le32 bt_activity_grading;
  uint8_t ttc_status;
  uint8_t rrc_status;
  __le16 reserved;
} __packed; /* BT_COEX_PROFILE_NTFY_API_S_VER_4 */

#ifdef CPTCFG_IWLWIFI_FRQ_MGR
/*
 * struct iwl_config_2g_coex_cmd - 2G Coex configuration command
 * (CONFIG_2G_COEX_CMD = 0x71)
 * @enabled: 2g coex is enabled/disabled
 */
struct iwl_config_2g_coex_cmd {
  __le32 enabled;
} __packed; /* CONFIG_2G_COEX_CMD_API_S_VER_1 */
#endif

#ifdef CPTCFG_IWLWIFI_LTE_COEX

#define WIFI_BAND_24_NUM_CHANNELS 14
#define LTE_COEX_MFUART_CONFIG_LENGTH 12
#define LTE_COEX_FRAME_STRUCTURE_LENGTH 2

/**
 * struct iwl_lte_coex_config_cmd - LTE Coex configuration command
 * @lte_state: lte modem state
 * @lte_band: lte operating band
 * @lte_chan: lte operating channel
 * @lte_frame_structure: ?
 * @tx_safe_freq_min: ?
 * @tx_safe_freq_max: ?
 * @rx_safe_freq_min: ?
 * @rx_safe_freq_max: ?
 * @max_tx_power: wifi static max tx output power per channel
 *
 * Used for LTE_COEX_CONFIG_CMD command
 */
struct iwl_lte_coex_config_cmd {
  __le32 lte_state;
  __le32 lte_band;
  __le32 lte_chan;
  __le32 lte_frame_structure[LTE_COEX_FRAME_STRUCTURE_LENGTH];
  __le32 tx_safe_freq_min;
  __le32 tx_safe_freq_max;
  __le32 rx_safe_freq_min;
  __le32 rx_safe_freq_max;
  uint8_t max_tx_power[WIFI_BAND_24_NUM_CHANNELS];
} __packed; /* LTE_COEX_CONFIG_CMD_API_S_VER_1 */

/**
 * struct iwl_lte_coex_static_params_cmd - LTE Coex static params configuration
 * command
 * @mfu_config: MFUART config and RT signals assert/de-assert timing
 * @tx_power_in_dbm: Wifi safe power table
 *
 * Used for LTE_COEX_STATIC_PARAMS_CMD command
 */
struct iwl_lte_coex_static_params_cmd {
  __le32 mfu_config[LTE_COEX_MFUART_CONFIG_LENGTH];
  int8_t tx_power_in_dbm[32];
} __packed; /* LTE_COEX_STATIC_PARAMS_CMD_API_S_VER_1 */

/**
 * struct iwl_lte_coex_wifi_reported_channel_cmd - LTE Coex reported channels
 * configuration command
 * @channel: channel number (1-14)
 * @bandwidth: bandwidth (0-3)
 *
 * Used for LTE_COEX_WIFI_REPORTED_CHANNEL_CMD command
 */
struct iwl_lte_coex_wifi_reported_channel_cmd {
  __le32 channel;
  __le32 bandwidth;
} __packed; /* LTE_COEX_WIFI_REPORTED_CHANNEL_CMD_API_S_VER_1 */

/**
 * struct iwl_lte_coex_sps_cmd - LTE Coex semi persistent info command
 *
 * @lte_semi_persistent_info:
 *
 * Used for LTE_COEX_SPS_CMD command
 */
struct iwl_lte_coex_sps_cmd {
  __le32 lte_semi_persistent_info;
} __packed; /* LTE_COEX_WIFI_SPS_CMD_API_S_VER_1 */

/**
 * struct iwl_lte_coex_fine_tuning_params_cmd - LTE Coex fine tuning parameters
 * @rx_protection_assert_timing: 802_RX_PRI request advance time
 * @tx_protection_assert_timing: 802_TX_ON request advance time
 * @rx_protection_timeout: Cancel Rx Protection request due to no Rx threshold
 * @min_tx_power: Min-Tx-Power threshold for Data/Management frames
 * @lte_ul_load_uapsd_threshold: 'LTE UL Load' counter thresholds
 *  for recommending Power-Manager to enter to UAPSD
 * @rx_failure_during_ul_uapsd_threshold: 'Rx Failure due to UL' counter
 *  thresholds for recommending Power-Manager to enter to UAPSD
 * @rx_failure_during_ul_sc_threshold: 'Rx Failure due to UL'
 *  counter threshold for recommending Scan-Manager to apply compensation
 * @rx_duration_for_ack_protection_us: Tx Ack size for Tx Protection
 * @beacon_failure_during_ul_counter: Failed Rx Beacon threshold
 * @dtim_failure_during_ul_counter: Failed Rx DTIM threshold
 *
 * Used for LTE_COEX_FINE_TUNING_PARAMS_CMD command
 */
struct iwl_lte_coex_fine_tuning_params_cmd {
  __le32 rx_protection_assert_timing;
  __le32 tx_protection_assert_timing;
  __le32 rx_protection_timeout;
  __le32 min_tx_power;
  __le32 lte_ul_load_uapsd_threshold;
  __le32 rx_failure_during_ul_uapsd_threshold;
  __le32 rx_failure_during_ul_sc_threshold;
  __le32 rx_duration_for_ack_protection_us;
  __le32 beacon_failure_during_ul_counter;
  __le32 dtim_failure_during_ul_counter;
} __packed; /* LTE_COEX_FINE_TUNING_PARAMS_CMD_API_S_VER_1 */

/**
 * struct iwl_lte_coex_statistic_ntfy - LTE Coex statistics notification
 * @statistic_placeholder: placeholder
 */
struct iwl_lte_coex_statistic_ntfy {
  __le32 statistic_placeholder;
} __packed; /* LTE_COEX_STATISTIC_NTFY_API_S_VER_1 */
#endif      /* CPTCFG_IWLWIFI_LTE_COEX */

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_FW_API_COEX_H_
