/******************************************************************************
 *
 * Copyright(c) 2017 Intel Deutschland GmbH
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

#ifndef __fw_api_ax_softap_testmode_h__
#define __fw_api_ax_softap_testmode_h__

#include "mac.h"

/**
 * struct trig_frame_common_softap_testmode
 *
 * all the configurable common trigger frame fields that can be set
 * for this mode.
 *
 * @cmn_lsig_len: L-SIG Length
 * @cmn_cascade_indication: Cascade Indication
 * @cmn_carrier_sense_req: CS Required
 * @cmn_gi_ltf: GI And LTF
 * @cmn_mu_mimo_ltf: MU-MIMO LTF Mode
 * @cmn_he_ltf_num: Number Of HE-LTF Symbols
 * @cmn_ldpc_ext_sym: LDPC Extra Symbol Segment
 * @cmn_packet_extension: Packet Extension
 * @cmn_spatial_reuse: Spatial Reuse
 * @cmn_doppler: Doppler
 * @cmn_res_he_sig_a: HE-SIG-A Reserved
 * @reserved: reserved for DW alignment
 */
struct trig_frame_common_softap_testmode {
    __le16 cmn_lsig_len;
    u8 cmn_cascade_indication;
    u8 cmn_carrier_sense_req;
    u8 cmn_gi_ltf;
    u8 cmn_mu_mimo_ltf;
    u8 cmn_he_ltf_num;
    u8 cmn_ldpc_ext_sym;
    u8 cmn_packet_extension;
    __le16 cmn_spatial_reuse;
    u8 cmn_doppler;
    __le16 cmn_res_he_sig_a;
    __le16 reserved;
} __packed; /* TRIG_FRAME_COMMON_SOFTAP_TESTMODE_API_S_VER_1 */

/**
 * struct trig_frame_user_softap_testmode
 *
 * The struct contains all the common configurable per user trigger frame
 * fields that can be set for this mode.
 *
 * @usr_assoc_id: AID12
 * @usr_rsrc_unit_alloc: RU Allocation
 * @usr_coding_type: Coding Type
 * @usr_mcs: MCS
 * @usr_dcm: DCM
 * @usr_ss_allocation: SS Allocation
 * @usr_target_rssi: Target RSSI
 */
struct trig_frame_user_softap_testmode {
    __le16 usr_assoc_id;
    u8 usr_rsrc_unit_alloc;
    u8 usr_coding_type;
    u8 usr_mcs;
    u8 usr_dcm;
    u8 usr_ss_allocation;
    u8 usr_target_rssi;
} __packed; /* TRIG_FRAME_USER_SOFTAP_TESTMODE_API_S_VER_1 */

/**
 * struct trig_frame_user_basic_softap_testmode
 *
 * he struct contains all the basic configurable per user trigger frame
 * fields that can be set for this mode.
 *
 * @usr_space_factor: MPDU MU Spacing Factor
 * @tid_agg_limit: TID Aggregation Limit
 * @preferred_ac_enabled: AC Preference Level
 * @preferred_ac: Preferred AC
 */
struct trig_frame_user_basic_softap_testmode {
    u8 usr_space_factor;
    u8 tid_agg_limit;
    u8 preferred_ac_enabled;
    u8 preferred_ac;
} __packed; /* TRIG_FRAME_USER_BASIC_SOFTAP_TESTMODE_API_S_VER_1 */

/**
 * struct trig_frame_softap_testmode
 *
 * The struct contains all the common configurable trigger frame params that
 * can be set for this mode.
 *
 * @pad_byte_count: the number of bytes to add in padding for the trigger frame
 * @per_user_count: the number of per user sections in the configured trig frame
 * @reserved: reserved for DW alignment
 * @addr1: addr1 of the mh of the configured trig frame
 * @reserved_for_addr1: addr data type in FW is aligned to 8 bytes
 * @addr2: addr2 of the mh of the configured trig frame
 * @reserved_for_addr2: addr data type in FW is aligned to 8 bytes
 */
struct trig_frame_softap_testmode {
    __le16 pad_byte_count;
    u8 per_user_count;
    u8 reserved;
    u8 addr1[6];
    __le16 reserved_for_addr1;
    u8 addr2[6];
    __le16 reserved_for_addr2;
} __packed; /* TRIG_FRAME_SOFTAP_TESTMODE_API_S_VER_1 */

/**
 * struct trig_frame_ax_softap_dl_basic
 *
 * command to configure the 11ax softap testmode for basic DL. In this mode
 * the AP prepends 0-3 trigger frames to each aggregation so that the client
 * is triggered to send a BA to the aggregation. This mode requires to enable
 * the softap and connect an AX client to it which has been modified to send
 * the triggered responses in HE_SU instead of HE_TRIG so that the softap
 * hardware could receive it.
 *
 * @frame_params: general trigger frame params
 * @common: content of the common trigger frame fields
 * @per_user: content of the per user trigger frame fields - up to 3 per user
 *        sections can be configured in this mode for each trigger frame
 * @per_user_basic: content of the basic type per user trigger frame fields -
 *          up to 3 per user sections can be configured in this mode
 *          for each trigger frame
 */
struct trig_frame_ax_softap_dl_basic {
    struct trig_frame_softap_testmode frame_params;
    struct trig_frame_common_softap_testmode common;
    struct trig_frame_user_softap_testmode per_user[3];
    struct trig_frame_user_basic_softap_testmode per_user_basic[3];
} __packed; /* TRIG_FRAME_SOFTAP_TESTMODE_DL_BASIC_API_S_VER_1 */

/**
 * struct ax_softap_testmode_dl_basic_cmd
 *
 * @enable: enable or disable this test mode
 * @txop_duration_disable: bool to disable the txop duration in the HE PLCP of
 *             the agg carrying the trigger frames for this
 *             test mode
 * @configured_frames_count: number of trigger frames configured for the
 *               test mode - max 3 trigger frames
 * @reserved: reserved for DW alignment
 * @frames: the trigger frames content - up to 3 trigger frames can
 *      be configured
 */
struct ax_softap_testmode_dl_basic_cmd {
    u8 enable;
    u8 txop_duration_disable;
    u8 configured_frames_count;
    u8 reserved;
    struct trig_frame_ax_softap_dl_basic frames[3];
} __packed; /* AX_SOFTAP_TESTMODE_DL_BASIC_API_S_VER_1 */

/**
 * struct trig_frame_bar_tid_ax_softap_testmode_dl_mu_bar
 *
 * The struct contains all the configurable bar per tid trigger frame
 * fields that can be set for this mode.
 *
 * @association_id: AID
 * @ba_ssn_bitmap_size: Fragment Number subfield (bits 1-2)
 * @reserved: reserved for DW alignment
 */
struct trig_frame_bar_tid_ax_softap_testmode_dl_mu_bar {
    __le16 association_id;
    u8 ba_ssn_bitmap_size;
    u8 reserved;
} __packed; /* TRIG_FRAME_BAR_TID_SOFTAP_TESTMODE_DL_MU_BAR_API_S_VER_1 */

/**
 * struct trig_frame_bar_ax_softap_testmode_dl_mu_bar
 *
 * The struct contains all the configurable bar trigger frame fields that
 * can be set for this mode.
 *
 * @block_ack_policy: BA Ack Policy
 * @block_ack_type: BA Type
 * @tid_count: the number of TIDs configured in this mu bar trigger frame
 *         per user section
 * @reserved: reserved for DW alignment
 * @per_tid: MU-BAR trigger frame configuration per TID
 */
struct trig_frame_bar_ax_softap_testmode_dl_mu_bar {
    u8 block_ack_policy;
    u8 block_ack_type;
    u8 tid_count;
    u8 reserved;
    struct trig_frame_bar_tid_ax_softap_testmode_dl_mu_bar per_tid[3];
} __packed; /* TRIG_FRAME_BAR_SOFTAP_TESTMODE_DL_MU_BAR_API_S_VER_1 */

/**
 * struct trig_frame_ax_softap_dl_mu_bar
 *
 * command to configure the 11ax softap testmode for MU Bar DL. In this mode
 * the AP sends a trigger frame after each aggregation so that the client is
 * triggered to send a BA to the aggregation.
 * This mode requires to enable the softap and connect an AX client to it
 * which has been modified to send the triggered responses in HE_SU
 * instead of HE_TRIG so that the softap hardware could receive it.
 *
 * @frame_params: general trigger frame params
 * @common: content of the common trigger frame fields
 * @per_user: content of the per user trigger frame fields - up to 3 per user
 *        sections can be configured in this mode for each trigger frame
 * @bar: content of the mu bar type per user trigger frame fields  - up to 3
 *   per user sections can be configured in this mode for each trigger frame
 */
struct trig_frame_ax_softap_dl_mu_bar {
    struct trig_frame_softap_testmode frame_params;
    struct trig_frame_common_softap_testmode common;
    struct trig_frame_user_softap_testmode per_user[3];
    struct trig_frame_bar_ax_softap_testmode_dl_mu_bar bar[3];
} __packed; /* TRIG_FRAME_SOFTAP_TESTMODE_DL_MU_BAR_API_S_VER_1 */

/**
 * struct ax_softap_testmode_dl_mu_bar_cmd
 *
 * @enable: enable or disable this test mode
 * @reserved1: reserved for DW alignment
 * @reserved2: reserved for DW alignment
 * @rate_n_flags: rate for TX operation of the trigger frame
 * @frame: the trigger frame content
 */
struct ax_softap_testmode_dl_mu_bar_cmd {
    u8 enable;
    __le16 reserved1;
    u8 reserved2;
    __le32 rate_n_flags;
    struct trig_frame_ax_softap_dl_mu_bar frame;
} __packed; /* AX_SOFTAP_TESTMODE_DL_MU_BAR_API_S_VER_1 */

/**
 * struct per_trig_params_ax_softap_ul
 *
 * @assoc_id: assoc id of the configured trig frame per user section
 *        override (only for the first per user section in the frame
 *        if more than 1 exist)
 * @duration: duration of the mh of the configured trig frame
 * @addr1: addr1 of the mh of the configured trig frame override
 * @reserved_for_addr1: reserved for DW alignment
 * @rate_n_flags: rate for TX operation of the configured trigger frame
 */
struct per_trig_params_ax_softap_ul {
    __le16 assoc_id;
    __le16 duration;
    u8 addr1[6];
    __le16 reserved_for_addr1;
    __le32 rate_n_flags;
} __packed; /* PER_TRIG_PARAMS_SOFTAP_TESTMODE_UL_API_S_VER_1 */

/**
 * struct trig_frame_ax_softap_ul
 *
 * command to configure the 11ax softap testmode for UL In this mode the
 * AP sends a trigger frame periodically with a timer so that the client
 * is triggered to send QOS data to the AP.
 * This mode requires to enable the softap and connect an AX client to it
 * which has been modified to send the triggered responses in HE_SU
 * instead of HE_TRIG so that the softap hardware could receive it.
 *
 * @frame_params: general trigger frame params
 * @common: content of the common trigger frame fields
 * @per_user: content of the per user trigger frame fields  - up to 3 per user
 *        sections can be configured in this mode for each trigger frame
 * @per_user_basic: content of the basic type per user trigger frame fields
 *          - up to 3 per user sections can be configured in this
 *          mode for each trigger frame
 */
struct trig_frame_ax_softap_ul {
    struct trig_frame_softap_testmode frame_params;
    struct trig_frame_common_softap_testmode common;
    struct trig_frame_user_softap_testmode per_user[3];
    struct trig_frame_user_basic_softap_testmode per_user_basic[3];
} __packed; /* TRIG_FRAME_SOFTAP_TESTMODE_UL_API_S_VER_1 */

/**
 * struct ax_softap_testmode_ul_cmd
 *
 * @enable: enable or disable this test mode
 * @trig_frame_periodic_msec: the time timer interval in msecs for sending the
 *                trigger frames
 * @reserved: reserved for DW alignment
 * @frame: the trigger frame content
 * @number_of_triggers_in_sequence: the number of triggers to send after period
 * @per_trigger: params to override config for each trigger in a sequence
 */
struct ax_softap_testmode_ul_cmd {
    u8 enable;
    u8 trig_frame_periodic_msec;
    __le16 reserved;
    struct trig_frame_ax_softap_ul frame;
    __le32 number_of_triggers_in_sequence;
    struct per_trig_params_ax_softap_ul per_trigger[4];
} __packed; /* AX_SOFTAP_TESTMODE_UL_API_S_VER_2 */

/**
 * struct ax_softap_client_testmode_cmd
 *
 * @enable: enable or disable this test mode
 * @reserved1: reserved for DW alignment
 * @reserved2: reserved for DW alignment
 */
struct ax_softap_client_testmode_cmd {
    u8 enable;
    u8 reserved1;
    __le16 reserved2;
} __packed; /* AX_SOFTAP_CLIENT_TESTMODE_API_S_VER_1 */

#endif /* __fw_api_ax_softap_testmode_h__ */
