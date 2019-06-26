/******************************************************************************
 *
 * Copyright(c) 2015 - 2017 Intel Deutschland GmbH
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
#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_FW_API_TOF_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_FW_API_TOF_H_

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/fuchsia_porting.h"

/* ToF sub-group command IDs */
enum iwl_mvm_tof_sub_grp_ids {
  TOF_RANGE_REQ_CMD = 0x1,
  TOF_CONFIG_CMD = 0x2,
  TOF_RANGE_ABORT_CMD = 0x3,
  TOF_RANGE_REQ_EXT_CMD = 0x4,
  TOF_RESPONDER_CONFIG_CMD = 0x5,
  TOF_NW_INITIATED_RES_SEND_CMD = 0x6,
  TOF_NEIGHBOR_REPORT_REQ_CMD = 0x7,
  TOF_NEIGHBOR_REPORT_RSP_NOTIF = 0xFC,
  TOF_NW_INITIATED_REQ_RCVD_NOTIF = 0xFD,
  TOF_RANGE_RESPONSE_NOTIF = 0xFE,
  TOF_MCSI_DEBUG_NOTIF = 0xFB,
};

/**
 * struct iwl_tof_config_cmd - ToF configuration
 * @sub_grp_cmd_id: will be removed
 * @tof_disabled: 0 enabled, 1 - disabled
 * @one_sided_disabled: 0 enabled, 1 - disabled
 * @is_debug_mode: 1 debug mode, 0 - otherwise
 * @is_buf_required: 1 channel estimation buffer required, 0 - otherwise
 */
struct iwl_tof_config_cmd {
  __le32 sub_grp_cmd_id;
  uint8_t tof_disabled;
  uint8_t one_sided_disabled;
  uint8_t is_debug_mode;
  uint8_t is_buf_required;
} __packed;

/**
 * struct iwl_tof_responder_config_cmd - ToF AP mode (for debug)
 * @sub_grp_cmd_id: will be removed
 * @burst_period: future use: (currently hard coded in the LMAC)
 *        The interval between two sequential bursts.
 * @min_delta_ftm: future use: (currently hard coded in the LMAC)
 *         The minimum delay between two sequential FTM Responses
 *         in the same burst.
 * @burst_duration: future use: (currently hard coded in the LMAC)
 *         The total time for all FTMs handshake in the same burst.
 *         Affect the time events duration in the LMAC.
 * @num_of_burst_exp: future use: (currently hard coded in the LMAC)
 *         The number of bursts for the current ToF request. Affect
 *         the number of events allocations in the current iteration.
 * @get_ch_est: for xVT only, NA for driver
 * @abort_responder: when set to '1' - Responder will terminate its activity
 *           (all other fields in the command are ignored)
 * @recv_sta_req_params: 1 - Responder will ignore the other Responder's
 *           params and use the recomended Initiator params.
 *           0 - otherwise
 * @channel_num: current AP Channel
 * @bandwidth: current AP Bandwidth: 0  20MHz, 1  40MHz, 2  80MHz
 * @rate: current AP rate
 * @ctrl_ch_position: coding of the control channel position relative to
 *  the center frequency:
 *
 *  40 MHz
 *      0 below center, 1 above center
 *
 *  80 MHz
 *      bits [0..1]
 *       * 0  the near 20MHz to the center,
 *       * 1  the far  20MHz to the center
 *      bit[2]
 *       as above 40MHz
 * @ftm_per_burst: FTMs per Burst
 * @ftm_resp_ts_avail: '0' - we don't measure over the Initial FTM Response,
 *        '1' - we measure over the Initial FTM Response
 * @asap_mode: ASAP / Non ASAP mode for the current WLS station
 * @sta_id: index of the AP STA when in AP mode
 * @tsf_timer_offset_msecs: The dictated time offset (mSec) from the AP's TSF
 * @toa_offset: Artificial addition [0.1nsec] for the ToA - to be used for debug
 *      purposes, simulating station movement by adding various values
 *      to this field
 * @bssid: Current AP BSSID
 */
struct iwl_tof_responder_config_cmd {
  __le32 sub_grp_cmd_id;
  __le16 burst_period;
  uint8_t min_delta_ftm;
  uint8_t burst_duration;
  uint8_t num_of_burst_exp;
  uint8_t get_ch_est;
  uint8_t abort_responder;
  uint8_t recv_sta_req_params;
  uint8_t channel_num;
  uint8_t bandwidth;
  uint8_t rate;
  uint8_t ctrl_ch_position;
  uint8_t ftm_per_burst;
  uint8_t ftm_resp_ts_avail;
  uint8_t asap_mode;
  uint8_t sta_id;
  __le16 tsf_timer_offset_msecs;
  __le16 toa_offset;
  uint8_t bssid[ETH_ALEN];
} __packed;

/**
 * struct iwl_tof_range_request_ext_cmd - extended range req for WLS
 * @sub_grp_cmd_id: will be removed
 * @tsf_timer_offset_msec: the recommended time offset (mSec) from the AP's TSF
 * @reserved: reserved
 * @min_delta_ftm: Minimal time between two consecutive measurements,
 *         in units of 100us. 0 means no preference by station
 * @ftm_format_and_bw20M: FTM Channel Spacing/Format for 20MHz: recommended
 *          value be sent to the AP
 * @ftm_format_and_bw40M: FTM Channel Spacing/Format for 40MHz: recommended
 *          value to be sent to the AP
 * @ftm_format_and_bw80M: FTM Channel Spacing/Format for 80MHz: recommended
 *          value to be sent to the AP
 */
struct iwl_tof_range_req_ext_cmd {
  __le32 sub_grp_cmd_id;
  __le16 tsf_timer_offset_msec;
  __le16 reserved;
  uint8_t min_delta_ftm;
  uint8_t ftm_format_and_bw20M;
  uint8_t ftm_format_and_bw40M;
  uint8_t ftm_format_and_bw80M;
} __packed;

#define IWL_MVM_TOF_MAX_APS 21

/**
 * struct iwl_tof_range_req_ap_entry - AP configuration parameters
 * @channel_num: Current AP Channel
 * @bandwidth: Current AP Bandwidth: 0  20MHz, 1  40MHz, 2  80MHz
 * @tsf_delta_direction: TSF relatively to the subject AP
 * @ctrl_ch_position: Coding of the control channel position relative to the
 *  center frequency.
 * @bssid: AP's bss id
 * @measure_type: Measurement type: 0 - two sided, 1 - One sided
 * @num_of_bursts: Recommended value to be sent to the AP.  2s Exponent of the
 *         number of measurement iterations (min 2^0 = 1, max 2^14)
 * @burst_period: Recommended value to be sent to the AP. Measurement
 *        periodicity In units of 100ms. ignored if num_of_bursts = 0
 * @samples_per_burst: 2-sided: the number of FTMs pairs in single Burst (1-31)
 *             1-sided: how many rts/cts pairs should be used per burst.
 * @retries_per_sample: Max number of retries that the LMAC should send
 *          in case of no replies by the AP.
 * @tsf_delta: TSF Delta in units of microseconds.
 *         The difference between the AP TSF and the device local clock.
 * @location_req: Location Request Bit[0] LCI should be sent in the FTMR
 *                Bit[1] Civic should be sent in the FTMR
 * @asap_mode: 0 - non asap mode, 1 - asap mode (not relevant for one sided)
 * @enable_dyn_ack: Enable Dynamic ACK BW.
 *      0  Initiator interact with regular AP
 *      1  Initiator interact with Responder machine: need to send the
 *      Initiator Acks with HT 40MHz / 80MHz, since the Responder should
 *      use it for its ch est measurement (this flag will be set when we
 *      configure the opposite machine to be Responder).
 * @rssi: Last received value
 *    leagal values: -128-0 (0x7f). above 0x0 indicating an invalid value.
 */
struct iwl_tof_range_req_ap_entry {
  uint8_t channel_num;
  uint8_t bandwidth;
  uint8_t tsf_delta_direction;
  uint8_t ctrl_ch_position;
  uint8_t bssid[ETH_ALEN];
  uint8_t measure_type;
  uint8_t num_of_bursts;
  __le16 burst_period;
  uint8_t samples_per_burst;
  uint8_t retries_per_sample;
  __le32 tsf_delta;
  uint8_t location_req;
  uint8_t asap_mode;
  uint8_t enable_dyn_ack;
  int8_t rssi;
} __packed;

/**
 * enum iwl_tof_response_mode
 * @IWL_MVM_TOF_RESPOSE_ASAP: report each AP measurement separately as soon as
 *                possible (not supported for this release)
 * @IWL_MVM_TOF_RESPOSE_TIMEOUT: report all AP measurements as a batch upon
 *               timeout expiration
 * @IWL_MVM_TOF_RESPOSE_COMPLETE: report all AP measurements as a batch at the
 *                earlier of: measurements completion / timeout
 *                expiration.
 */
enum iwl_tof_response_mode {
  IWL_MVM_TOF_RESPOSE_ASAP = 1,
  IWL_MVM_TOF_RESPOSE_TIMEOUT,
  IWL_MVM_TOF_RESPOSE_COMPLETE,
};

/**
 * struct iwl_tof_range_req_cmd - start measurement cmd
 * @sub_grp_cmd_id: will be removed
 * @request_id: A Token incremented per request. The same Token will be
 *      sent back in the range response
 * @initiator: 0- NW initiated,  1 - Client Initiated
 * @one_sided_los_disable: '0'- run ML-Algo for both ToF/OneSided,
 *             '1' - run ML-Algo for ToF only
 * @req_timeout: Requested timeout of the response in units of 100ms.
 *       This is equivalent to the session time configured to the
 *       LMAC in Initiator Request
 * @report_policy: Supported partially for this release: For current release -
 *         the range report will be uploaded as a batch when ready or
 *         when the session is done (successfully / partially).
 *         one of iwl_tof_response_mode.
 * @los_det_disable: tbd
 * @num_of_ap: Number of APs to measure (error if > IWL_MVM_TOF_MAX_APS)
 * @macaddr_random: '0' Use default source MAC address (i.e. p2_p),
 *              '1' Use MAC Address randomization according to the below
 * @macaddr_template: template for the MAC address
 * @macaddr_mask: Bits set to 0 shall be copied from the MAC address template.
 *        Bits set to 1 shall be randomized by the UMAC
 * @ap: per-AP request data
 */
struct iwl_tof_range_req_cmd {
  __le32 sub_grp_cmd_id;
  uint8_t request_id;
  uint8_t initiator;
  uint8_t one_sided_los_disable;
  uint8_t req_timeout;
  uint8_t report_policy;
  uint8_t los_det_disable;
  uint8_t num_of_ap;
  uint8_t macaddr_random;
  uint8_t macaddr_template[ETH_ALEN];
  uint8_t macaddr_mask[ETH_ALEN];
  struct iwl_tof_range_req_ap_entry ap[IWL_MVM_TOF_MAX_APS];
} __packed;

/**
 * struct iwl_tof_gen_resp_cmd - generic ToF response
 * @sub_grp_cmd_id: will be removed
 * @data: response data
 */
struct iwl_tof_gen_resp_cmd {
  __le32 sub_grp_cmd_id;
  uint8_t data[];
} __packed;

/**
 * struct iwl_tof_range_rsp_ap_entry_ntfy - AP parameters (response)
 * @bssid: BSSID of the AP
 * @measure_status: current APs measurement status, one of
 *  &enum iwl_tof_entry_status.
 * @measure_bw: Current AP Bandwidth: 0  20MHz, 1  40MHz, 2  80MHz
 * @rtt: The Round Trip Time that took for the last measurement for
 *   current AP [nSec]
 * @rtt_variance: The Variance of the RTT values measured for current AP
 * @rtt_spread: The Difference between the maximum and the minimum RTT
 *         values measured for current AP in the current session [nsec]
 * @rssi: RSSI as uploaded in the Channel Estimation notification
 * @rssi_spread: The Difference between the maximum and the minimum RSSI values
 *          measured for current AP in the current session
 * @reserved: reserved
 * @range: Measured range [cm]
 * @range_variance: Measured range variance [cm]
 * @timestamp: The GP2 Clock [usec] where Channel Estimation notification was
 *         uploaded by the LMAC
 */
struct iwl_tof_range_rsp_ap_entry_ntfy {
  uint8_t bssid[ETH_ALEN];
  uint8_t measure_status;
  uint8_t measure_bw;
  __le32 rtt;
  __le32 rtt_variance;
  __le32 rtt_spread;
  int8_t rssi;
  uint8_t rssi_spread;
  __le16 reserved;
  __le32 range;
  __le32 range_variance;
  __le32 timestamp;
} __packed;

/**
 * struct iwl_tof_range_rsp_ntfy -
 * @request_id: A Token ID of the corresponding Range request
 * @request_status: status of current measurement session
 * @last_in_batch: reprot policy (when not all responses are uploaded at once)
 * @num_of_aps: Number of APs to measure (error if > IWL_MVM_TOF_MAX_APS)
 * @ap: per-AP data
 */
struct iwl_tof_range_rsp_ntfy {
  uint8_t request_id;
  uint8_t request_status;
  uint8_t last_in_batch;
  uint8_t num_of_aps;
  struct iwl_tof_range_rsp_ap_entry_ntfy ap[IWL_MVM_TOF_MAX_APS];
} __packed;

#define IWL_MVM_TOF_MCSI_BUF_SIZE (245)
/**
 * struct iwl_tof_mcsi_notif - used for debug
 * @token: token ID for the current session
 * @role: '0' - initiator, '1' - responder
 * @reserved: reserved
 * @initiator_bssid: initiator machine
 * @responder_bssid: responder machine
 * @mcsi_buffer: debug data
 */
struct iwl_tof_mcsi_notif {
  uint8_t token;
  uint8_t role;
  __le16 reserved;
  uint8_t initiator_bssid[ETH_ALEN];
  uint8_t responder_bssid[ETH_ALEN];
  uint8_t mcsi_buffer[IWL_MVM_TOF_MCSI_BUF_SIZE * 4];
} __packed;

/**
 * struct iwl_tof_neighbor_report_notif
 * @bssid: BSSID of the AP which sent the report
 * @request_token: same token as the corresponding request
 * @status: status code
 * @report_ie_len: the length of the response frame starting from the Element ID
 * @data: the IEs
 */
struct iwl_tof_neighbor_report {
  uint8_t bssid[ETH_ALEN];
  uint8_t request_token;
  uint8_t status;
  __le16 report_ie_len;
  uint8_t data[];
} __packed;

/**
 * struct iwl_tof_range_abort_cmd
 * @sub_grp_cmd_id: will be removed
 * @request_id: corresponds to a range request
 * @reserved: reserved
 */
struct iwl_tof_range_abort_cmd {
  __le32 sub_grp_cmd_id;
  uint8_t request_id;
  uint8_t reserved[3];
} __packed;

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_FW_API_TOF_H_
