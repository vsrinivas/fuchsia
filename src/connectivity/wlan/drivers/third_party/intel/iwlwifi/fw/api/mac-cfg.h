/******************************************************************************
 *
 * Copyright(c) 2012 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2013 - 2015 Intel Mobile Communications GmbH
 * Copyright(c) 2016 - 2017 Intel Deutschland GmbH
 * Copyright(c) 2018 Intel Corporation
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

#ifndef __iwl_fw_api_mac_cfg_h__
#define __iwl_fw_api_mac_cfg_h__

/**
 * enum iwl_mac_conf_subcmd_ids - mac configuration command IDs
 */
enum iwl_mac_conf_subcmd_ids {
    /**
     * @LOW_LATENCY_CMD: &struct iwl_mac_low_latency_cmd
     */
    LOW_LATENCY_CMD = 0x3,
    /**
     * @PROBE_RESPONSE_DATA_NOTIF: &struct iwl_probe_resp_data_notif
     */
    PROBE_RESPONSE_DATA_NOTIF = 0xFC,

    /**
     * @CHANNEL_SWITCH_NOA_NOTIF: &struct iwl_channel_switch_noa_notif
     */
    CHANNEL_SWITCH_NOA_NOTIF = 0xFF,
};

#define IWL_P2P_NOA_DESC_COUNT (2)

/**
 * struct iwl_p2p_noa_attr - NOA attr contained in probe resp FW notification
 *
 * @id: attribute id
 * @len_low: length low half
 * @len_high: length high half
 * @idx: instance of NoA timing
 * @ctwin: GO's ct window and pwer save capability
 * @desc: NoA descriptor
 * @reserved: reserved for alignment purposes
 */
struct iwl_p2p_noa_attr {
    uint8_t id;
    uint8_t len_low;
    uint8_t len_high;
    uint8_t idx;
    uint8_t ctwin;
    struct ieee80211_p2p_noa_desc desc[IWL_P2P_NOA_DESC_COUNT];
    uint8_t reserved;
} __packed;

#define IWL_PROBE_RESP_DATA_NO_CSA (0xff)

/**
 * struct iwl_probe_resp_data_notif - notification with NOA and CSA counter
 *
 * @mac_id: the mac which should send the probe response
 * @noa_active: notifies if the noa attribute should be handled
 * @noa_attr: P2P NOA attribute
 * @csa_counter: current csa counter
 * @reserved: reserved for alignment purposes
 */
struct iwl_probe_resp_data_notif {
    __le32 mac_id;
    __le32 noa_active;
    struct iwl_p2p_noa_attr noa_attr;
    uint8_t csa_counter;
    uint8_t reserved[3];
} __packed; /* PROBE_RESPONSE_DATA_NTFY_API_S_VER_1 */

/**
 * struct iwl_channel_switch_noa_notif - Channel switch NOA notification
 *
 * @id_and_color: ID and color of the MAC
 */
struct iwl_channel_switch_noa_notif {
    __le32 id_and_color;
} __packed; /* CHANNEL_SWITCH_START_NTFY_API_S_VER_1 */

/**
 * struct iwl_mac_low_latency_cmd - set/clear mac to 'low-latency mode'
 *
 * @mac_id: MAC ID to whom to apply the low-latency configurations
 * @low_latency_rx: 1/0 to set/clear Rx low latency direction
 * @low_latency_tx: 1/0 to set/clear Tx low latency direction
 * @reserved: reserved for alignment purposes
 */
struct iwl_mac_low_latency_cmd {
    __le32 mac_id;
    uint8_t low_latency_rx;
    uint8_t low_latency_tx;
    __le16 reserved;
} __packed; /* MAC_LOW_LATENCY_API_S_VER_1 */

#endif /* __iwl_fw_api_mac_cfg_h__ */
