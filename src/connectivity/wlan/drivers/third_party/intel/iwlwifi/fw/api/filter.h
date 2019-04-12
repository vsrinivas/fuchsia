/******************************************************************************
 *
 * Copyright(c) 2012 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2013 - 2015 Intel Mobile Communications GmbH
 * Copyright(c) 2016 - 2017 Intel Deutschland GmbH
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

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_FW_API_FILTER_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_FW_API_FILTER_H_

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/fw/api/mac.h"

#define MAX_PORT_ID_NUM 2
#define MAX_MCAST_FILTERING_ADDRESSES 256

/**
 * struct iwl_mcast_filter_cmd - configure multicast filter.
 * @filter_own: Set 1 to filter out multicast packets sent by station itself
 * @port_id:    Multicast MAC addresses array specifier. This is a strange way
 *      to identify network interface adopted in host-device IF.
 *      It is used by FW as index in array of addresses. This array has
 *      MAX_PORT_ID_NUM members.
 * @count:  Number of MAC addresses in the array
 * @pass_all:   Set 1 to pass all multicast packets.
 * @bssid:  current association BSSID.
 * @reserved:   reserved
 * @addr_list:  Place holder for array of MAC addresses.
 *      IMPORTANT: add padding if necessary to ensure DWORD alignment.
 */
struct iwl_mcast_filter_cmd {
    uint8_t filter_own;
    uint8_t port_id;
    uint8_t count;
    uint8_t pass_all;
    uint8_t bssid[6];
    uint8_t reserved[2];
    uint8_t addr_list[0];
} __packed; /* MCAST_FILTERING_CMD_API_S_VER_1 */

#define MAX_BCAST_FILTERS 8
#define MAX_BCAST_FILTER_ATTRS 2

/**
 * enum iwl_mvm_bcast_filter_attr_offset - written by fw for each Rx packet
 * @BCAST_FILTER_OFFSET_PAYLOAD_START: offset is from payload start.
 * @BCAST_FILTER_OFFSET_IP_END: offset is from ip header end (i.e.
 *  start of ip payload).
 */
enum iwl_mvm_bcast_filter_attr_offset {
    BCAST_FILTER_OFFSET_PAYLOAD_START = 0,
    BCAST_FILTER_OFFSET_IP_END = 1,
};

/**
 * struct iwl_fw_bcast_filter_attr - broadcast filter attribute
 * @offset_type:    &enum iwl_mvm_bcast_filter_attr_offset.
 * @offset: starting offset of this pattern.
 * @reserved1:  reserved
 * @val:    value to match - big endian (MSB is the first
 *      byte to match from offset pos).
 * @mask:   mask to match (big endian).
 */
struct iwl_fw_bcast_filter_attr {
    uint8_t offset_type;
    uint8_t offset;
    __le16 reserved1;
    __be32 val;
    __be32 mask;
} __packed; /* BCAST_FILTER_ATT_S_VER_1 */

/**
 * enum iwl_mvm_bcast_filter_frame_type - filter frame type
 * @BCAST_FILTER_FRAME_TYPE_ALL: consider all frames.
 * @BCAST_FILTER_FRAME_TYPE_IPV4: consider only ipv4 frames
 */
enum iwl_mvm_bcast_filter_frame_type {
    BCAST_FILTER_FRAME_TYPE_ALL = 0,
    BCAST_FILTER_FRAME_TYPE_IPV4 = 1,
};

/**
 * struct iwl_fw_bcast_filter - broadcast filter
 * @discard: discard frame (1) or let it pass (0).
 * @frame_type: &enum iwl_mvm_bcast_filter_frame_type.
 * @reserved1: reserved
 * @num_attrs: number of valid attributes in this filter.
 * @attrs: attributes of this filter. a filter is considered matched
 *  only when all its attributes are matched (i.e. AND relationship)
 */
struct iwl_fw_bcast_filter {
    uint8_t discard;
    uint8_t frame_type;
    uint8_t num_attrs;
    uint8_t reserved1;
    struct iwl_fw_bcast_filter_attr attrs[MAX_BCAST_FILTER_ATTRS];
} __packed; /* BCAST_FILTER_S_VER_1 */

/**
 * struct iwl_fw_bcast_mac - per-mac broadcast filtering configuration.
 * @default_discard: default action for this mac (discard (1) / pass (0)).
 * @reserved1: reserved
 * @attached_filters: bitmap of relevant filters for this mac.
 */
struct iwl_fw_bcast_mac {
    uint8_t default_discard;
    uint8_t reserved1;
    __le16 attached_filters;
} __packed; /* BCAST_MAC_CONTEXT_S_VER_1 */

/**
 * struct iwl_bcast_filter_cmd - broadcast filtering configuration
 * @disable: enable (0) / disable (1)
 * @max_bcast_filters: max number of filters (MAX_BCAST_FILTERS)
 * @max_macs: max number of macs (NUM_MAC_INDEX_DRIVER)
 * @reserved1: reserved
 * @filters: broadcast filters
 * @macs: broadcast filtering configuration per-mac
 */
struct iwl_bcast_filter_cmd {
    uint8_t disable;
    uint8_t max_bcast_filters;
    uint8_t max_macs;
    uint8_t reserved1;
    struct iwl_fw_bcast_filter filters[MAX_BCAST_FILTERS];
    struct iwl_fw_bcast_mac macs[NUM_MAC_INDEX_DRIVER];
} __packed; /* BCAST_FILTERING_HCMD_API_S_VER_1 */

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_FW_API_FILTER_H_
