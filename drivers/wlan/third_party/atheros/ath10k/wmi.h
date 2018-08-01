/*
 * Copyright (c) 2005-2011 Atheros Communications Inc.
 * Copyright (c) 2011-2013 Qualcomm Atheros, Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _WMI_H_
#define _WMI_H_

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <zircon/status.h>

#include "hw.h"

/*
 * This file specifies the WMI interface for the Unified Software
 * Architecture.
 *
 * It includes definitions of all the commands and events. Commands are
 * messages from the host to the target. Events and Replies are messages
 * from the target to the host.
 *
 * Ownership of correctness in regards to WMI commands belongs to the host
 * driver and the target is not required to validate parameters for value,
 * proper range, or any other checking.
 *
 * Guidelines for extending this interface are below.
 *
 * 1. Add new WMI commands ONLY within the specified range - 0x9000 - 0x9fff
 *
 * 2. Use ONLY uint32_t type for defining member variables within WMI
 *    command/event structures. Do not use uint8_t, uint16_t, bool or
 *    enum types within these structures.
 *
 * 3. DO NOT define bit fields within structures. Implement bit fields
 *    using masks if necessary. Do not use the programming language's bit
 *    field definition.
 *
 * 4. Define macros for encode/decode of uint8_t, uint16_t fields within
 *    the uint32_t variables. Use these macros for set/get of these fields.
 *    Try to use this to optimize the structure without bloating it with
 *    uint32_t variables for every lower sized field.
 *
 * 5. Do not use PACK/UNPACK attributes for the structures as each member
 *    variable is already 4-byte aligned by virtue of being a uint32_t
 *    type.
 *
 * 6. Comment each parameter part of the WMI command/event structure by
 *    using the 2 stars at the beginning of C comment instead of one star to
 *    enable HTML document generation using Doxygen.
 *
 */

/* Control Path */
struct wmi_cmd_hdr {
    uint32_t cmd_id;
} __PACKED;

#define WMI_CMD_HDR_CMD_ID_MASK   0x00FFFFFF
#define WMI_CMD_HDR_CMD_ID_LSB    0
#define WMI_CMD_HDR_PLT_PRIV_MASK 0xFF000000
#define WMI_CMD_HDR_PLT_PRIV_LSB  24

#define HTC_PROTOCOL_VERSION    0x0002
#define WMI_PROTOCOL_VERSION    0x0002

enum wmi_service {
    WMI_SERVICE_BEACON_OFFLOAD = 0,
    WMI_SERVICE_SCAN_OFFLOAD,
    WMI_SERVICE_ROAM_OFFLOAD,
    WMI_SERVICE_BCN_MISS_OFFLOAD,
    WMI_SERVICE_STA_PWRSAVE,
    WMI_SERVICE_STA_ADVANCED_PWRSAVE,
    WMI_SERVICE_AP_UAPSD,
    WMI_SERVICE_AP_DFS,
    WMI_SERVICE_11AC,
    WMI_SERVICE_BLOCKACK,
    WMI_SERVICE_PHYERR,
    WMI_SERVICE_BCN_FILTER,
    WMI_SERVICE_RTT,
    WMI_SERVICE_RATECTRL,
    WMI_SERVICE_WOW,
    WMI_SERVICE_RATECTRL_CACHE,
    WMI_SERVICE_IRAM_TIDS,
    WMI_SERVICE_ARPNS_OFFLOAD,
    WMI_SERVICE_NLO,
    WMI_SERVICE_GTK_OFFLOAD,
    WMI_SERVICE_SCAN_SCH,
    WMI_SERVICE_CSA_OFFLOAD,
    WMI_SERVICE_CHATTER,
    WMI_SERVICE_COEX_FREQAVOID,
    WMI_SERVICE_PACKET_POWER_SAVE,
    WMI_SERVICE_FORCE_FW_HANG,
    WMI_SERVICE_GPIO,
    WMI_SERVICE_STA_DTIM_PS_MODULATED_DTIM,
    WMI_SERVICE_STA_UAPSD_BASIC_AUTO_TRIG,
    WMI_SERVICE_STA_UAPSD_VAR_AUTO_TRIG,
    WMI_SERVICE_STA_KEEP_ALIVE,
    WMI_SERVICE_TX_ENCAP,
    WMI_SERVICE_BURST,
    WMI_SERVICE_SMART_ANTENNA_SW_SUPPORT,
    WMI_SERVICE_SMART_ANTENNA_HW_SUPPORT,
    WMI_SERVICE_ROAM_SCAN_OFFLOAD,
    WMI_SERVICE_AP_PS_DETECT_OUT_OF_SYNC,
    WMI_SERVICE_EARLY_RX,
    WMI_SERVICE_STA_SMPS,
    WMI_SERVICE_FWTEST,
    WMI_SERVICE_STA_WMMAC,
    WMI_SERVICE_TDLS,
    WMI_SERVICE_MCC_BCN_INTERVAL_CHANGE,
    WMI_SERVICE_ADAPTIVE_OCS,
    WMI_SERVICE_BA_SSN_SUPPORT,
    WMI_SERVICE_FILTER_IPSEC_NATKEEPALIVE,
    WMI_SERVICE_WLAN_HB,
    WMI_SERVICE_LTE_ANT_SHARE_SUPPORT,
    WMI_SERVICE_BATCH_SCAN,
    WMI_SERVICE_QPOWER,
    WMI_SERVICE_PLMREQ,
    WMI_SERVICE_THERMAL_MGMT,
    WMI_SERVICE_RMC,
    WMI_SERVICE_MHF_OFFLOAD,
    WMI_SERVICE_COEX_SAR,
    WMI_SERVICE_BCN_TXRATE_OVERRIDE,
    WMI_SERVICE_NAN,
    WMI_SERVICE_L1SS_STAT,
    WMI_SERVICE_ESTIMATE_LINKSPEED,
    WMI_SERVICE_OBSS_SCAN,
    WMI_SERVICE_TDLS_OFFCHAN,
    WMI_SERVICE_TDLS_UAPSD_BUFFER_STA,
    WMI_SERVICE_TDLS_UAPSD_SLEEP_STA,
    WMI_SERVICE_IBSS_PWRSAVE,
    WMI_SERVICE_LPASS,
    WMI_SERVICE_EXTSCAN,
    WMI_SERVICE_D0WOW,
    WMI_SERVICE_HSOFFLOAD,
    WMI_SERVICE_ROAM_HO_OFFLOAD,
    WMI_SERVICE_RX_FULL_REORDER,
    WMI_SERVICE_DHCP_OFFLOAD,
    WMI_SERVICE_STA_RX_IPA_OFFLOAD_SUPPORT,
    WMI_SERVICE_MDNS_OFFLOAD,
    WMI_SERVICE_SAP_AUTH_OFFLOAD,
    WMI_SERVICE_ATF,
    WMI_SERVICE_COEX_GPIO,
    WMI_SERVICE_ENHANCED_PROXY_STA,
    WMI_SERVICE_TT,
    WMI_SERVICE_PEER_CACHING,
    WMI_SERVICE_AUX_SPECTRAL_INTF,
    WMI_SERVICE_AUX_CHAN_LOAD_INTF,
    WMI_SERVICE_BSS_CHANNEL_INFO_64,
    WMI_SERVICE_EXT_RES_CFG_SUPPORT,
    WMI_SERVICE_MESH_11S,
    WMI_SERVICE_MESH_NON_11S,
    WMI_SERVICE_PEER_STATS,
    WMI_SERVICE_RESTRT_CHNL_SUPPORT,
    WMI_SERVICE_PERIODIC_CHAN_STAT_SUPPORT,
    WMI_SERVICE_TX_MODE_PUSH_ONLY,
    WMI_SERVICE_TX_MODE_PUSH_PULL,
    WMI_SERVICE_TX_MODE_DYNAMIC,

    /* keep last */
    WMI_SERVICE_MAX,
};

enum wmi_10x_service {
    WMI_10X_SERVICE_BEACON_OFFLOAD = 0,
    WMI_10X_SERVICE_SCAN_OFFLOAD,
    WMI_10X_SERVICE_ROAM_OFFLOAD,
    WMI_10X_SERVICE_BCN_MISS_OFFLOAD,
    WMI_10X_SERVICE_STA_PWRSAVE,
    WMI_10X_SERVICE_STA_ADVANCED_PWRSAVE,
    WMI_10X_SERVICE_AP_UAPSD,
    WMI_10X_SERVICE_AP_DFS,
    WMI_10X_SERVICE_11AC,
    WMI_10X_SERVICE_BLOCKACK,
    WMI_10X_SERVICE_PHYERR,
    WMI_10X_SERVICE_BCN_FILTER,
    WMI_10X_SERVICE_RTT,
    WMI_10X_SERVICE_RATECTRL,
    WMI_10X_SERVICE_WOW,
    WMI_10X_SERVICE_RATECTRL_CACHE,
    WMI_10X_SERVICE_IRAM_TIDS,
    WMI_10X_SERVICE_BURST,

    /* introduced in 10.2 */
    WMI_10X_SERVICE_SMART_ANTENNA_SW_SUPPORT,
    WMI_10X_SERVICE_FORCE_FW_HANG,
    WMI_10X_SERVICE_SMART_ANTENNA_HW_SUPPORT,
    WMI_10X_SERVICE_ATF,
    WMI_10X_SERVICE_COEX_GPIO,
    WMI_10X_SERVICE_AUX_SPECTRAL_INTF,
    WMI_10X_SERVICE_AUX_CHAN_LOAD_INTF,
    WMI_10X_SERVICE_BSS_CHANNEL_INFO_64,
    WMI_10X_SERVICE_MESH,
    WMI_10X_SERVICE_EXT_RES_CFG_SUPPORT,
    WMI_10X_SERVICE_PEER_STATS,
};

enum wmi_main_service {
    WMI_MAIN_SERVICE_BEACON_OFFLOAD = 0,
    WMI_MAIN_SERVICE_SCAN_OFFLOAD,
    WMI_MAIN_SERVICE_ROAM_OFFLOAD,
    WMI_MAIN_SERVICE_BCN_MISS_OFFLOAD,
    WMI_MAIN_SERVICE_STA_PWRSAVE,
    WMI_MAIN_SERVICE_STA_ADVANCED_PWRSAVE,
    WMI_MAIN_SERVICE_AP_UAPSD,
    WMI_MAIN_SERVICE_AP_DFS,
    WMI_MAIN_SERVICE_11AC,
    WMI_MAIN_SERVICE_BLOCKACK,
    WMI_MAIN_SERVICE_PHYERR,
    WMI_MAIN_SERVICE_BCN_FILTER,
    WMI_MAIN_SERVICE_RTT,
    WMI_MAIN_SERVICE_RATECTRL,
    WMI_MAIN_SERVICE_WOW,
    WMI_MAIN_SERVICE_RATECTRL_CACHE,
    WMI_MAIN_SERVICE_IRAM_TIDS,
    WMI_MAIN_SERVICE_ARPNS_OFFLOAD,
    WMI_MAIN_SERVICE_NLO,
    WMI_MAIN_SERVICE_GTK_OFFLOAD,
    WMI_MAIN_SERVICE_SCAN_SCH,
    WMI_MAIN_SERVICE_CSA_OFFLOAD,
    WMI_MAIN_SERVICE_CHATTER,
    WMI_MAIN_SERVICE_COEX_FREQAVOID,
    WMI_MAIN_SERVICE_PACKET_POWER_SAVE,
    WMI_MAIN_SERVICE_FORCE_FW_HANG,
    WMI_MAIN_SERVICE_GPIO,
    WMI_MAIN_SERVICE_STA_DTIM_PS_MODULATED_DTIM,
    WMI_MAIN_SERVICE_STA_UAPSD_BASIC_AUTO_TRIG,
    WMI_MAIN_SERVICE_STA_UAPSD_VAR_AUTO_TRIG,
    WMI_MAIN_SERVICE_STA_KEEP_ALIVE,
    WMI_MAIN_SERVICE_TX_ENCAP,
};

enum wmi_10_4_service {
    WMI_10_4_SERVICE_BEACON_OFFLOAD = 0,
    WMI_10_4_SERVICE_SCAN_OFFLOAD,
    WMI_10_4_SERVICE_ROAM_OFFLOAD,
    WMI_10_4_SERVICE_BCN_MISS_OFFLOAD,
    WMI_10_4_SERVICE_STA_PWRSAVE,
    WMI_10_4_SERVICE_STA_ADVANCED_PWRSAVE,
    WMI_10_4_SERVICE_AP_UAPSD,
    WMI_10_4_SERVICE_AP_DFS,
    WMI_10_4_SERVICE_11AC,
    WMI_10_4_SERVICE_BLOCKACK,
    WMI_10_4_SERVICE_PHYERR,
    WMI_10_4_SERVICE_BCN_FILTER,
    WMI_10_4_SERVICE_RTT,
    WMI_10_4_SERVICE_RATECTRL,
    WMI_10_4_SERVICE_WOW,
    WMI_10_4_SERVICE_RATECTRL_CACHE,
    WMI_10_4_SERVICE_IRAM_TIDS,
    WMI_10_4_SERVICE_BURST,
    WMI_10_4_SERVICE_SMART_ANTENNA_SW_SUPPORT,
    WMI_10_4_SERVICE_GTK_OFFLOAD,
    WMI_10_4_SERVICE_SCAN_SCH,
    WMI_10_4_SERVICE_CSA_OFFLOAD,
    WMI_10_4_SERVICE_CHATTER,
    WMI_10_4_SERVICE_COEX_FREQAVOID,
    WMI_10_4_SERVICE_PACKET_POWER_SAVE,
    WMI_10_4_SERVICE_FORCE_FW_HANG,
    WMI_10_4_SERVICE_SMART_ANTENNA_HW_SUPPORT,
    WMI_10_4_SERVICE_GPIO,
    WMI_10_4_SERVICE_STA_UAPSD_BASIC_AUTO_TRIG,
    WMI_10_4_SERVICE_STA_UAPSD_VAR_AUTO_TRIG,
    WMI_10_4_SERVICE_STA_KEEP_ALIVE,
    WMI_10_4_SERVICE_TX_ENCAP,
    WMI_10_4_SERVICE_AP_PS_DETECT_OUT_OF_SYNC,
    WMI_10_4_SERVICE_EARLY_RX,
    WMI_10_4_SERVICE_ENHANCED_PROXY_STA,
    WMI_10_4_SERVICE_TT,
    WMI_10_4_SERVICE_ATF,
    WMI_10_4_SERVICE_PEER_CACHING,
    WMI_10_4_SERVICE_COEX_GPIO,
    WMI_10_4_SERVICE_AUX_SPECTRAL_INTF,
    WMI_10_4_SERVICE_AUX_CHAN_LOAD_INTF,
    WMI_10_4_SERVICE_BSS_CHANNEL_INFO_64,
    WMI_10_4_SERVICE_EXT_RES_CFG_SUPPORT,
    WMI_10_4_SERVICE_MESH_NON_11S,
    WMI_10_4_SERVICE_RESTRT_CHNL_SUPPORT,
    WMI_10_4_SERVICE_PEER_STATS,
    WMI_10_4_SERVICE_MESH_11S,
    WMI_10_4_SERVICE_PERIODIC_CHAN_STAT_SUPPORT,
    WMI_10_4_SERVICE_TX_MODE_PUSH_ONLY,
    WMI_10_4_SERVICE_TX_MODE_PUSH_PULL,
    WMI_10_4_SERVICE_TX_MODE_DYNAMIC,
};

static inline char* wmi_service_name(int service_id) {
#define SVCSTR(x) case x: return #x

    switch (service_id) {
        SVCSTR(WMI_SERVICE_BEACON_OFFLOAD);
        SVCSTR(WMI_SERVICE_SCAN_OFFLOAD);
        SVCSTR(WMI_SERVICE_ROAM_OFFLOAD);
        SVCSTR(WMI_SERVICE_BCN_MISS_OFFLOAD);
        SVCSTR(WMI_SERVICE_STA_PWRSAVE);
        SVCSTR(WMI_SERVICE_STA_ADVANCED_PWRSAVE);
        SVCSTR(WMI_SERVICE_AP_UAPSD);
        SVCSTR(WMI_SERVICE_AP_DFS);
        SVCSTR(WMI_SERVICE_11AC);
        SVCSTR(WMI_SERVICE_BLOCKACK);
        SVCSTR(WMI_SERVICE_PHYERR);
        SVCSTR(WMI_SERVICE_BCN_FILTER);
        SVCSTR(WMI_SERVICE_RTT);
        SVCSTR(WMI_SERVICE_RATECTRL);
        SVCSTR(WMI_SERVICE_WOW);
        SVCSTR(WMI_SERVICE_RATECTRL_CACHE);
        SVCSTR(WMI_SERVICE_IRAM_TIDS);
        SVCSTR(WMI_SERVICE_ARPNS_OFFLOAD);
        SVCSTR(WMI_SERVICE_NLO);
        SVCSTR(WMI_SERVICE_GTK_OFFLOAD);
        SVCSTR(WMI_SERVICE_SCAN_SCH);
        SVCSTR(WMI_SERVICE_CSA_OFFLOAD);
        SVCSTR(WMI_SERVICE_CHATTER);
        SVCSTR(WMI_SERVICE_COEX_FREQAVOID);
        SVCSTR(WMI_SERVICE_PACKET_POWER_SAVE);
        SVCSTR(WMI_SERVICE_FORCE_FW_HANG);
        SVCSTR(WMI_SERVICE_GPIO);
        SVCSTR(WMI_SERVICE_STA_DTIM_PS_MODULATED_DTIM);
        SVCSTR(WMI_SERVICE_STA_UAPSD_BASIC_AUTO_TRIG);
        SVCSTR(WMI_SERVICE_STA_UAPSD_VAR_AUTO_TRIG);
        SVCSTR(WMI_SERVICE_STA_KEEP_ALIVE);
        SVCSTR(WMI_SERVICE_TX_ENCAP);
        SVCSTR(WMI_SERVICE_BURST);
        SVCSTR(WMI_SERVICE_SMART_ANTENNA_SW_SUPPORT);
        SVCSTR(WMI_SERVICE_SMART_ANTENNA_HW_SUPPORT);
        SVCSTR(WMI_SERVICE_ROAM_SCAN_OFFLOAD);
        SVCSTR(WMI_SERVICE_AP_PS_DETECT_OUT_OF_SYNC);
        SVCSTR(WMI_SERVICE_EARLY_RX);
        SVCSTR(WMI_SERVICE_STA_SMPS);
        SVCSTR(WMI_SERVICE_FWTEST);
        SVCSTR(WMI_SERVICE_STA_WMMAC);
        SVCSTR(WMI_SERVICE_TDLS);
        SVCSTR(WMI_SERVICE_MCC_BCN_INTERVAL_CHANGE);
        SVCSTR(WMI_SERVICE_ADAPTIVE_OCS);
        SVCSTR(WMI_SERVICE_BA_SSN_SUPPORT);
        SVCSTR(WMI_SERVICE_FILTER_IPSEC_NATKEEPALIVE);
        SVCSTR(WMI_SERVICE_WLAN_HB);
        SVCSTR(WMI_SERVICE_LTE_ANT_SHARE_SUPPORT);
        SVCSTR(WMI_SERVICE_BATCH_SCAN);
        SVCSTR(WMI_SERVICE_QPOWER);
        SVCSTR(WMI_SERVICE_PLMREQ);
        SVCSTR(WMI_SERVICE_THERMAL_MGMT);
        SVCSTR(WMI_SERVICE_RMC);
        SVCSTR(WMI_SERVICE_MHF_OFFLOAD);
        SVCSTR(WMI_SERVICE_COEX_SAR);
        SVCSTR(WMI_SERVICE_BCN_TXRATE_OVERRIDE);
        SVCSTR(WMI_SERVICE_NAN);
        SVCSTR(WMI_SERVICE_L1SS_STAT);
        SVCSTR(WMI_SERVICE_ESTIMATE_LINKSPEED);
        SVCSTR(WMI_SERVICE_OBSS_SCAN);
        SVCSTR(WMI_SERVICE_TDLS_OFFCHAN);
        SVCSTR(WMI_SERVICE_TDLS_UAPSD_BUFFER_STA);
        SVCSTR(WMI_SERVICE_TDLS_UAPSD_SLEEP_STA);
        SVCSTR(WMI_SERVICE_IBSS_PWRSAVE);
        SVCSTR(WMI_SERVICE_LPASS);
        SVCSTR(WMI_SERVICE_EXTSCAN);
        SVCSTR(WMI_SERVICE_D0WOW);
        SVCSTR(WMI_SERVICE_HSOFFLOAD);
        SVCSTR(WMI_SERVICE_ROAM_HO_OFFLOAD);
        SVCSTR(WMI_SERVICE_RX_FULL_REORDER);
        SVCSTR(WMI_SERVICE_DHCP_OFFLOAD);
        SVCSTR(WMI_SERVICE_STA_RX_IPA_OFFLOAD_SUPPORT);
        SVCSTR(WMI_SERVICE_MDNS_OFFLOAD);
        SVCSTR(WMI_SERVICE_SAP_AUTH_OFFLOAD);
        SVCSTR(WMI_SERVICE_ATF);
        SVCSTR(WMI_SERVICE_COEX_GPIO);
        SVCSTR(WMI_SERVICE_ENHANCED_PROXY_STA);
        SVCSTR(WMI_SERVICE_TT);
        SVCSTR(WMI_SERVICE_PEER_CACHING);
        SVCSTR(WMI_SERVICE_AUX_SPECTRAL_INTF);
        SVCSTR(WMI_SERVICE_AUX_CHAN_LOAD_INTF);
        SVCSTR(WMI_SERVICE_BSS_CHANNEL_INFO_64);
        SVCSTR(WMI_SERVICE_EXT_RES_CFG_SUPPORT);
        SVCSTR(WMI_SERVICE_MESH_11S);
        SVCSTR(WMI_SERVICE_MESH_NON_11S);
        SVCSTR(WMI_SERVICE_PEER_STATS);
        SVCSTR(WMI_SERVICE_RESTRT_CHNL_SUPPORT);
        SVCSTR(WMI_SERVICE_PERIODIC_CHAN_STAT_SUPPORT);
        SVCSTR(WMI_SERVICE_TX_MODE_PUSH_ONLY);
        SVCSTR(WMI_SERVICE_TX_MODE_PUSH_PULL);
        SVCSTR(WMI_SERVICE_TX_MODE_DYNAMIC);
    default:
        return NULL;
    }

#undef SVCSTR
}

#define WMI_SERVICE_IS_ENABLED(wmi_svc_bmap, svc_id, len) \
    ((svc_id) < (len) && \
     (wmi_svc_bmap)[(svc_id) / (sizeof(uint32_t))] & \
     (1 << ((svc_id) % (sizeof(uint32_t)))))

#define SVCMAP(x, y, len) \
    do { \
        if (WMI_SERVICE_IS_ENABLED((in), (x), (len))) \
            BITARR_SET(out, y); \
    } while (0)

static inline void wmi_10x_svc_map(const uint32_t* in, unsigned long* out,
                                   size_t len) {
    SVCMAP(WMI_10X_SERVICE_BEACON_OFFLOAD,
           WMI_SERVICE_BEACON_OFFLOAD, len);
    SVCMAP(WMI_10X_SERVICE_SCAN_OFFLOAD,
           WMI_SERVICE_SCAN_OFFLOAD, len);
    SVCMAP(WMI_10X_SERVICE_ROAM_OFFLOAD,
           WMI_SERVICE_ROAM_OFFLOAD, len);
    SVCMAP(WMI_10X_SERVICE_BCN_MISS_OFFLOAD,
           WMI_SERVICE_BCN_MISS_OFFLOAD, len);
    SVCMAP(WMI_10X_SERVICE_STA_PWRSAVE,
           WMI_SERVICE_STA_PWRSAVE, len);
    SVCMAP(WMI_10X_SERVICE_STA_ADVANCED_PWRSAVE,
           WMI_SERVICE_STA_ADVANCED_PWRSAVE, len);
    SVCMAP(WMI_10X_SERVICE_AP_UAPSD,
           WMI_SERVICE_AP_UAPSD, len);
    SVCMAP(WMI_10X_SERVICE_AP_DFS,
           WMI_SERVICE_AP_DFS, len);
    SVCMAP(WMI_10X_SERVICE_11AC,
           WMI_SERVICE_11AC, len);
    SVCMAP(WMI_10X_SERVICE_BLOCKACK,
           WMI_SERVICE_BLOCKACK, len);
    SVCMAP(WMI_10X_SERVICE_PHYERR,
           WMI_SERVICE_PHYERR, len);
    SVCMAP(WMI_10X_SERVICE_BCN_FILTER,
           WMI_SERVICE_BCN_FILTER, len);
    SVCMAP(WMI_10X_SERVICE_RTT,
           WMI_SERVICE_RTT, len);
    SVCMAP(WMI_10X_SERVICE_RATECTRL,
           WMI_SERVICE_RATECTRL, len);
    SVCMAP(WMI_10X_SERVICE_WOW,
           WMI_SERVICE_WOW, len);
    SVCMAP(WMI_10X_SERVICE_RATECTRL_CACHE,
           WMI_SERVICE_RATECTRL_CACHE, len);
    SVCMAP(WMI_10X_SERVICE_IRAM_TIDS,
           WMI_SERVICE_IRAM_TIDS, len);
    SVCMAP(WMI_10X_SERVICE_BURST,
           WMI_SERVICE_BURST, len);
    SVCMAP(WMI_10X_SERVICE_SMART_ANTENNA_SW_SUPPORT,
           WMI_SERVICE_SMART_ANTENNA_SW_SUPPORT, len);
    SVCMAP(WMI_10X_SERVICE_FORCE_FW_HANG,
           WMI_SERVICE_FORCE_FW_HANG, len);
    SVCMAP(WMI_10X_SERVICE_SMART_ANTENNA_HW_SUPPORT,
           WMI_SERVICE_SMART_ANTENNA_HW_SUPPORT, len);
    SVCMAP(WMI_10X_SERVICE_ATF,
           WMI_SERVICE_ATF, len);
    SVCMAP(WMI_10X_SERVICE_COEX_GPIO,
           WMI_SERVICE_COEX_GPIO, len);
    SVCMAP(WMI_10X_SERVICE_AUX_SPECTRAL_INTF,
           WMI_SERVICE_AUX_SPECTRAL_INTF, len);
    SVCMAP(WMI_10X_SERVICE_AUX_CHAN_LOAD_INTF,
           WMI_SERVICE_AUX_CHAN_LOAD_INTF, len);
    SVCMAP(WMI_10X_SERVICE_BSS_CHANNEL_INFO_64,
           WMI_SERVICE_BSS_CHANNEL_INFO_64, len);
    SVCMAP(WMI_10X_SERVICE_MESH,
           WMI_SERVICE_MESH_11S, len);
    SVCMAP(WMI_10X_SERVICE_EXT_RES_CFG_SUPPORT,
           WMI_SERVICE_EXT_RES_CFG_SUPPORT, len);
    SVCMAP(WMI_10X_SERVICE_PEER_STATS,
           WMI_SERVICE_PEER_STATS, len);
}

static inline void wmi_main_svc_map(const uint32_t* in, unsigned long* out,
                                    size_t len) {
    SVCMAP(WMI_MAIN_SERVICE_BEACON_OFFLOAD,
           WMI_SERVICE_BEACON_OFFLOAD, len);
    SVCMAP(WMI_MAIN_SERVICE_SCAN_OFFLOAD,
           WMI_SERVICE_SCAN_OFFLOAD, len);
    SVCMAP(WMI_MAIN_SERVICE_ROAM_OFFLOAD,
           WMI_SERVICE_ROAM_OFFLOAD, len);
    SVCMAP(WMI_MAIN_SERVICE_BCN_MISS_OFFLOAD,
           WMI_SERVICE_BCN_MISS_OFFLOAD, len);
    SVCMAP(WMI_MAIN_SERVICE_STA_PWRSAVE,
           WMI_SERVICE_STA_PWRSAVE, len);
    SVCMAP(WMI_MAIN_SERVICE_STA_ADVANCED_PWRSAVE,
           WMI_SERVICE_STA_ADVANCED_PWRSAVE, len);
    SVCMAP(WMI_MAIN_SERVICE_AP_UAPSD,
           WMI_SERVICE_AP_UAPSD, len);
    SVCMAP(WMI_MAIN_SERVICE_AP_DFS,
           WMI_SERVICE_AP_DFS, len);
    SVCMAP(WMI_MAIN_SERVICE_11AC,
           WMI_SERVICE_11AC, len);
    SVCMAP(WMI_MAIN_SERVICE_BLOCKACK,
           WMI_SERVICE_BLOCKACK, len);
    SVCMAP(WMI_MAIN_SERVICE_PHYERR,
           WMI_SERVICE_PHYERR, len);
    SVCMAP(WMI_MAIN_SERVICE_BCN_FILTER,
           WMI_SERVICE_BCN_FILTER, len);
    SVCMAP(WMI_MAIN_SERVICE_RTT,
           WMI_SERVICE_RTT, len);
    SVCMAP(WMI_MAIN_SERVICE_RATECTRL,
           WMI_SERVICE_RATECTRL, len);
    SVCMAP(WMI_MAIN_SERVICE_WOW,
           WMI_SERVICE_WOW, len);
    SVCMAP(WMI_MAIN_SERVICE_RATECTRL_CACHE,
           WMI_SERVICE_RATECTRL_CACHE, len);
    SVCMAP(WMI_MAIN_SERVICE_IRAM_TIDS,
           WMI_SERVICE_IRAM_TIDS, len);
    SVCMAP(WMI_MAIN_SERVICE_ARPNS_OFFLOAD,
           WMI_SERVICE_ARPNS_OFFLOAD, len);
    SVCMAP(WMI_MAIN_SERVICE_NLO,
           WMI_SERVICE_NLO, len);
    SVCMAP(WMI_MAIN_SERVICE_GTK_OFFLOAD,
           WMI_SERVICE_GTK_OFFLOAD, len);
    SVCMAP(WMI_MAIN_SERVICE_SCAN_SCH,
           WMI_SERVICE_SCAN_SCH, len);
    SVCMAP(WMI_MAIN_SERVICE_CSA_OFFLOAD,
           WMI_SERVICE_CSA_OFFLOAD, len);
    SVCMAP(WMI_MAIN_SERVICE_CHATTER,
           WMI_SERVICE_CHATTER, len);
    SVCMAP(WMI_MAIN_SERVICE_COEX_FREQAVOID,
           WMI_SERVICE_COEX_FREQAVOID, len);
    SVCMAP(WMI_MAIN_SERVICE_PACKET_POWER_SAVE,
           WMI_SERVICE_PACKET_POWER_SAVE, len);
    SVCMAP(WMI_MAIN_SERVICE_FORCE_FW_HANG,
           WMI_SERVICE_FORCE_FW_HANG, len);
    SVCMAP(WMI_MAIN_SERVICE_GPIO,
           WMI_SERVICE_GPIO, len);
    SVCMAP(WMI_MAIN_SERVICE_STA_DTIM_PS_MODULATED_DTIM,
           WMI_SERVICE_STA_DTIM_PS_MODULATED_DTIM, len);
    SVCMAP(WMI_MAIN_SERVICE_STA_UAPSD_BASIC_AUTO_TRIG,
           WMI_SERVICE_STA_UAPSD_BASIC_AUTO_TRIG, len);
    SVCMAP(WMI_MAIN_SERVICE_STA_UAPSD_VAR_AUTO_TRIG,
           WMI_SERVICE_STA_UAPSD_VAR_AUTO_TRIG, len);
    SVCMAP(WMI_MAIN_SERVICE_STA_KEEP_ALIVE,
           WMI_SERVICE_STA_KEEP_ALIVE, len);
    SVCMAP(WMI_MAIN_SERVICE_TX_ENCAP,
           WMI_SERVICE_TX_ENCAP, len);
}

#if 0 // NEEDS PORTING
static inline void wmi_10_4_svc_map(const uint32_t* in, unsigned long* out,
                                    size_t len) {
    SVCMAP(WMI_10_4_SERVICE_BEACON_OFFLOAD,
           WMI_SERVICE_BEACON_OFFLOAD, len);
    SVCMAP(WMI_10_4_SERVICE_SCAN_OFFLOAD,
           WMI_SERVICE_SCAN_OFFLOAD, len);
    SVCMAP(WMI_10_4_SERVICE_ROAM_OFFLOAD,
           WMI_SERVICE_ROAM_OFFLOAD, len);
    SVCMAP(WMI_10_4_SERVICE_BCN_MISS_OFFLOAD,
           WMI_SERVICE_BCN_MISS_OFFLOAD, len);
    SVCMAP(WMI_10_4_SERVICE_STA_PWRSAVE,
           WMI_SERVICE_STA_PWRSAVE, len);
    SVCMAP(WMI_10_4_SERVICE_STA_ADVANCED_PWRSAVE,
           WMI_SERVICE_STA_ADVANCED_PWRSAVE, len);
    SVCMAP(WMI_10_4_SERVICE_AP_UAPSD,
           WMI_SERVICE_AP_UAPSD, len);
    SVCMAP(WMI_10_4_SERVICE_AP_DFS,
           WMI_SERVICE_AP_DFS, len);
    SVCMAP(WMI_10_4_SERVICE_11AC,
           WMI_SERVICE_11AC, len);
    SVCMAP(WMI_10_4_SERVICE_BLOCKACK,
           WMI_SERVICE_BLOCKACK, len);
    SVCMAP(WMI_10_4_SERVICE_PHYERR,
           WMI_SERVICE_PHYERR, len);
    SVCMAP(WMI_10_4_SERVICE_BCN_FILTER,
           WMI_SERVICE_BCN_FILTER, len);
    SVCMAP(WMI_10_4_SERVICE_RTT,
           WMI_SERVICE_RTT, len);
    SVCMAP(WMI_10_4_SERVICE_RATECTRL,
           WMI_SERVICE_RATECTRL, len);
    SVCMAP(WMI_10_4_SERVICE_WOW,
           WMI_SERVICE_WOW, len);
    SVCMAP(WMI_10_4_SERVICE_RATECTRL_CACHE,
           WMI_SERVICE_RATECTRL_CACHE, len);
    SVCMAP(WMI_10_4_SERVICE_IRAM_TIDS,
           WMI_SERVICE_IRAM_TIDS, len);
    SVCMAP(WMI_10_4_SERVICE_BURST,
           WMI_SERVICE_BURST, len);
    SVCMAP(WMI_10_4_SERVICE_SMART_ANTENNA_SW_SUPPORT,
           WMI_SERVICE_SMART_ANTENNA_SW_SUPPORT, len);
    SVCMAP(WMI_10_4_SERVICE_GTK_OFFLOAD,
           WMI_SERVICE_GTK_OFFLOAD, len);
    SVCMAP(WMI_10_4_SERVICE_SCAN_SCH,
           WMI_SERVICE_SCAN_SCH, len);
    SVCMAP(WMI_10_4_SERVICE_CSA_OFFLOAD,
           WMI_SERVICE_CSA_OFFLOAD, len);
    SVCMAP(WMI_10_4_SERVICE_CHATTER,
           WMI_SERVICE_CHATTER, len);
    SVCMAP(WMI_10_4_SERVICE_COEX_FREQAVOID,
           WMI_SERVICE_COEX_FREQAVOID, len);
    SVCMAP(WMI_10_4_SERVICE_PACKET_POWER_SAVE,
           WMI_SERVICE_PACKET_POWER_SAVE, len);
    SVCMAP(WMI_10_4_SERVICE_FORCE_FW_HANG,
           WMI_SERVICE_FORCE_FW_HANG, len);
    SVCMAP(WMI_10_4_SERVICE_SMART_ANTENNA_HW_SUPPORT,
           WMI_SERVICE_SMART_ANTENNA_HW_SUPPORT, len);
    SVCMAP(WMI_10_4_SERVICE_GPIO,
           WMI_SERVICE_GPIO, len);
    SVCMAP(WMI_10_4_SERVICE_STA_UAPSD_BASIC_AUTO_TRIG,
           WMI_SERVICE_STA_UAPSD_BASIC_AUTO_TRIG, len);
    SVCMAP(WMI_10_4_SERVICE_STA_UAPSD_VAR_AUTO_TRIG,
           WMI_SERVICE_STA_UAPSD_VAR_AUTO_TRIG, len);
    SVCMAP(WMI_10_4_SERVICE_STA_KEEP_ALIVE,
           WMI_SERVICE_STA_KEEP_ALIVE, len);
    SVCMAP(WMI_10_4_SERVICE_TX_ENCAP,
           WMI_SERVICE_TX_ENCAP, len);
    SVCMAP(WMI_10_4_SERVICE_AP_PS_DETECT_OUT_OF_SYNC,
           WMI_SERVICE_AP_PS_DETECT_OUT_OF_SYNC, len);
    SVCMAP(WMI_10_4_SERVICE_EARLY_RX,
           WMI_SERVICE_EARLY_RX, len);
    SVCMAP(WMI_10_4_SERVICE_ENHANCED_PROXY_STA,
           WMI_SERVICE_ENHANCED_PROXY_STA, len);
    SVCMAP(WMI_10_4_SERVICE_TT,
           WMI_SERVICE_TT, len);
    SVCMAP(WMI_10_4_SERVICE_ATF,
           WMI_SERVICE_ATF, len);
    SVCMAP(WMI_10_4_SERVICE_PEER_CACHING,
           WMI_SERVICE_PEER_CACHING, len);
    SVCMAP(WMI_10_4_SERVICE_COEX_GPIO,
           WMI_SERVICE_COEX_GPIO, len);
    SVCMAP(WMI_10_4_SERVICE_AUX_SPECTRAL_INTF,
           WMI_SERVICE_AUX_SPECTRAL_INTF, len);
    SVCMAP(WMI_10_4_SERVICE_AUX_CHAN_LOAD_INTF,
           WMI_SERVICE_AUX_CHAN_LOAD_INTF, len);
    SVCMAP(WMI_10_4_SERVICE_BSS_CHANNEL_INFO_64,
           WMI_SERVICE_BSS_CHANNEL_INFO_64, len);
    SVCMAP(WMI_10_4_SERVICE_EXT_RES_CFG_SUPPORT,
           WMI_SERVICE_EXT_RES_CFG_SUPPORT, len);
    SVCMAP(WMI_10_4_SERVICE_MESH_NON_11S,
           WMI_SERVICE_MESH_NON_11S, len);
    SVCMAP(WMI_10_4_SERVICE_RESTRT_CHNL_SUPPORT,
           WMI_SERVICE_RESTRT_CHNL_SUPPORT, len);
    SVCMAP(WMI_10_4_SERVICE_PEER_STATS,
           WMI_SERVICE_PEER_STATS, len);
    SVCMAP(WMI_10_4_SERVICE_MESH_11S,
           WMI_SERVICE_MESH_11S, len);
    SVCMAP(WMI_10_4_SERVICE_PERIODIC_CHAN_STAT_SUPPORT,
           WMI_SERVICE_PERIODIC_CHAN_STAT_SUPPORT, len);
    SVCMAP(WMI_10_4_SERVICE_TX_MODE_PUSH_ONLY,
           WMI_SERVICE_TX_MODE_PUSH_ONLY, len);
    SVCMAP(WMI_10_4_SERVICE_TX_MODE_PUSH_PULL,
           WMI_SERVICE_TX_MODE_PUSH_PULL, len);
    SVCMAP(WMI_10_4_SERVICE_TX_MODE_DYNAMIC,
           WMI_SERVICE_TX_MODE_DYNAMIC, len);
}

#undef SVCMAP
#endif // NEEDS PORTING

/* 2 word representation of MAC addr */
struct wmi_mac_addr {
    union {
        uint8_t addr[6];
        struct {
            uint32_t word0;
            uint32_t word1;
        } __PACKED;
    } __PACKED;
} __PACKED;

struct wmi_cmd_map {
    uint32_t init_cmdid;
    uint32_t start_scan_cmdid;
    uint32_t stop_scan_cmdid;
    uint32_t scan_chan_list_cmdid;
    uint32_t scan_sch_prio_tbl_cmdid;
    uint32_t pdev_set_regdomain_cmdid;
    uint32_t pdev_set_channel_cmdid;
    uint32_t pdev_set_param_cmdid;
    uint32_t pdev_pktlog_enable_cmdid;
    uint32_t pdev_pktlog_disable_cmdid;
    uint32_t pdev_set_wmm_params_cmdid;
    uint32_t pdev_set_ht_cap_ie_cmdid;
    uint32_t pdev_set_vht_cap_ie_cmdid;
    uint32_t pdev_set_dscp_tid_map_cmdid;
    uint32_t pdev_set_quiet_mode_cmdid;
    uint32_t pdev_green_ap_ps_enable_cmdid;
    uint32_t pdev_get_tpc_config_cmdid;
    uint32_t pdev_set_base_macaddr_cmdid;
    uint32_t vdev_create_cmdid;
    uint32_t vdev_delete_cmdid;
    uint32_t vdev_start_request_cmdid;
    uint32_t vdev_restart_request_cmdid;
    uint32_t vdev_up_cmdid;
    uint32_t vdev_stop_cmdid;
    uint32_t vdev_down_cmdid;
    uint32_t vdev_set_param_cmdid;
    uint32_t vdev_install_key_cmdid;
    uint32_t peer_create_cmdid;
    uint32_t peer_delete_cmdid;
    uint32_t peer_flush_tids_cmdid;
    uint32_t peer_set_param_cmdid;
    uint32_t peer_assoc_cmdid;
    uint32_t peer_add_wds_entry_cmdid;
    uint32_t peer_remove_wds_entry_cmdid;
    uint32_t peer_mcast_group_cmdid;
    uint32_t bcn_tx_cmdid;
    uint32_t pdev_send_bcn_cmdid;
    uint32_t bcn_tmpl_cmdid;
    uint32_t bcn_filter_rx_cmdid;
    uint32_t prb_req_filter_rx_cmdid;
    uint32_t mgmt_tx_cmdid;
    uint32_t prb_tmpl_cmdid;
    uint32_t addba_clear_resp_cmdid;
    uint32_t addba_send_cmdid;
    uint32_t addba_status_cmdid;
    uint32_t delba_send_cmdid;
    uint32_t addba_set_resp_cmdid;
    uint32_t send_singleamsdu_cmdid;
    uint32_t sta_powersave_mode_cmdid;
    uint32_t sta_powersave_param_cmdid;
    uint32_t sta_mimo_ps_mode_cmdid;
    uint32_t pdev_dfs_enable_cmdid;
    uint32_t pdev_dfs_disable_cmdid;
    uint32_t roam_scan_mode;
    uint32_t roam_scan_rssi_threshold;
    uint32_t roam_scan_period;
    uint32_t roam_scan_rssi_change_threshold;
    uint32_t roam_ap_profile;
    uint32_t ofl_scan_add_ap_profile;
    uint32_t ofl_scan_remove_ap_profile;
    uint32_t ofl_scan_period;
    uint32_t p2p_dev_set_device_info;
    uint32_t p2p_dev_set_discoverability;
    uint32_t p2p_go_set_beacon_ie;
    uint32_t p2p_go_set_probe_resp_ie;
    uint32_t p2p_set_vendor_ie_data_cmdid;
    uint32_t ap_ps_peer_param_cmdid;
    uint32_t ap_ps_peer_uapsd_coex_cmdid;
    uint32_t peer_rate_retry_sched_cmdid;
    uint32_t wlan_profile_trigger_cmdid;
    uint32_t wlan_profile_set_hist_intvl_cmdid;
    uint32_t wlan_profile_get_profile_data_cmdid;
    uint32_t wlan_profile_enable_profile_id_cmdid;
    uint32_t wlan_profile_list_profile_id_cmdid;
    uint32_t pdev_suspend_cmdid;
    uint32_t pdev_resume_cmdid;
    uint32_t add_bcn_filter_cmdid;
    uint32_t rmv_bcn_filter_cmdid;
    uint32_t wow_add_wake_pattern_cmdid;
    uint32_t wow_del_wake_pattern_cmdid;
    uint32_t wow_enable_disable_wake_event_cmdid;
    uint32_t wow_enable_cmdid;
    uint32_t wow_hostwakeup_from_sleep_cmdid;
    uint32_t rtt_measreq_cmdid;
    uint32_t rtt_tsf_cmdid;
    uint32_t vdev_spectral_scan_configure_cmdid;
    uint32_t vdev_spectral_scan_enable_cmdid;
    uint32_t request_stats_cmdid;
    uint32_t set_arp_ns_offload_cmdid;
    uint32_t network_list_offload_config_cmdid;
    uint32_t gtk_offload_cmdid;
    uint32_t csa_offload_enable_cmdid;
    uint32_t csa_offload_chanswitch_cmdid;
    uint32_t chatter_set_mode_cmdid;
    uint32_t peer_tid_addba_cmdid;
    uint32_t peer_tid_delba_cmdid;
    uint32_t sta_dtim_ps_method_cmdid;
    uint32_t sta_uapsd_auto_trig_cmdid;
    uint32_t sta_keepalive_cmd;
    uint32_t echo_cmdid;
    uint32_t pdev_utf_cmdid;
    uint32_t dbglog_cfg_cmdid;
    uint32_t pdev_qvit_cmdid;
    uint32_t pdev_ftm_intg_cmdid;
    uint32_t vdev_set_keepalive_cmdid;
    uint32_t vdev_get_keepalive_cmdid;
    uint32_t force_fw_hang_cmdid;
    uint32_t gpio_config_cmdid;
    uint32_t gpio_output_cmdid;
    uint32_t pdev_get_temperature_cmdid;
    uint32_t vdev_set_wmm_params_cmdid;
    uint32_t tdls_set_state_cmdid;
    uint32_t tdls_peer_update_cmdid;
    uint32_t adaptive_qcs_cmdid;
    uint32_t scan_update_request_cmdid;
    uint32_t vdev_standby_response_cmdid;
    uint32_t vdev_resume_response_cmdid;
    uint32_t wlan_peer_caching_add_peer_cmdid;
    uint32_t wlan_peer_caching_evict_peer_cmdid;
    uint32_t wlan_peer_caching_restore_peer_cmdid;
    uint32_t wlan_peer_caching_print_all_peers_info_cmdid;
    uint32_t peer_update_wds_entry_cmdid;
    uint32_t peer_add_proxy_sta_entry_cmdid;
    uint32_t rtt_keepalive_cmdid;
    uint32_t oem_req_cmdid;
    uint32_t nan_cmdid;
    uint32_t vdev_ratemask_cmdid;
    uint32_t qboost_cfg_cmdid;
    uint32_t pdev_smart_ant_enable_cmdid;
    uint32_t pdev_smart_ant_set_rx_antenna_cmdid;
    uint32_t peer_smart_ant_set_tx_antenna_cmdid;
    uint32_t peer_smart_ant_set_train_info_cmdid;
    uint32_t peer_smart_ant_set_node_config_ops_cmdid;
    uint32_t pdev_set_antenna_switch_table_cmdid;
    uint32_t pdev_set_ctl_table_cmdid;
    uint32_t pdev_set_mimogain_table_cmdid;
    uint32_t pdev_ratepwr_table_cmdid;
    uint32_t pdev_ratepwr_chainmsk_table_cmdid;
    uint32_t pdev_fips_cmdid;
    uint32_t tt_set_conf_cmdid;
    uint32_t fwtest_cmdid;
    uint32_t vdev_atf_request_cmdid;
    uint32_t peer_atf_request_cmdid;
    uint32_t pdev_get_ani_cck_config_cmdid;
    uint32_t pdev_get_ani_ofdm_config_cmdid;
    uint32_t pdev_reserve_ast_entry_cmdid;
    uint32_t pdev_get_nfcal_power_cmdid;
    uint32_t pdev_get_tpc_cmdid;
    uint32_t pdev_get_ast_info_cmdid;
    uint32_t vdev_set_dscp_tid_map_cmdid;
    uint32_t pdev_get_info_cmdid;
    uint32_t vdev_get_info_cmdid;
    uint32_t vdev_filter_neighbor_rx_packets_cmdid;
    uint32_t mu_cal_start_cmdid;
    uint32_t set_cca_params_cmdid;
    uint32_t pdev_bss_chan_info_request_cmdid;
    uint32_t pdev_enable_adaptive_cca_cmdid;
    uint32_t ext_resource_cfg_cmdid;
};

/*
 * wmi command groups.
 */
enum wmi_cmd_group {
    /* 0 to 2 are reserved */
    WMI_GRP_START = 0x3,
    WMI_GRP_SCAN = WMI_GRP_START,
    WMI_GRP_PDEV,
    WMI_GRP_VDEV,
    WMI_GRP_PEER,
    WMI_GRP_MGMT,
    WMI_GRP_BA_NEG,
    WMI_GRP_STA_PS,
    WMI_GRP_DFS,
    WMI_GRP_ROAM,
    WMI_GRP_OFL_SCAN,
    WMI_GRP_P2P,
    WMI_GRP_AP_PS,
    WMI_GRP_RATE_CTRL,
    WMI_GRP_PROFILE,
    WMI_GRP_SUSPEND,
    WMI_GRP_BCN_FILTER,
    WMI_GRP_WOW,
    WMI_GRP_RTT,
    WMI_GRP_SPECTRAL,
    WMI_GRP_STATS,
    WMI_GRP_ARP_NS_OFL,
    WMI_GRP_NLO_OFL,
    WMI_GRP_GTK_OFL,
    WMI_GRP_CSA_OFL,
    WMI_GRP_CHATTER,
    WMI_GRP_TID_ADDBA,
    WMI_GRP_MISC,
    WMI_GRP_GPIO,
};

#define WMI_CMD_GRP(grp_id) (((grp_id) << 12) | 0x1)
#define WMI_EVT_GRP_START_ID(grp_id) (((grp_id) << 12) | 0x1)

#define WMI_CMD_UNSUPPORTED 0

/* Command IDs and command events for MAIN FW. */
enum wmi_cmd_id {
    WMI_INIT_CMDID = 0x1,

    /* Scan specific commands */
    WMI_START_SCAN_CMDID = WMI_CMD_GRP(WMI_GRP_SCAN),
    WMI_STOP_SCAN_CMDID,
    WMI_SCAN_CHAN_LIST_CMDID,
    WMI_SCAN_SCH_PRIO_TBL_CMDID,

    /* PDEV (physical device) specific commands */
    WMI_PDEV_SET_REGDOMAIN_CMDID = WMI_CMD_GRP(WMI_GRP_PDEV),
    WMI_PDEV_SET_CHANNEL_CMDID,
    WMI_PDEV_SET_PARAM_CMDID,
    WMI_PDEV_PKTLOG_ENABLE_CMDID,
    WMI_PDEV_PKTLOG_DISABLE_CMDID,
    WMI_PDEV_SET_WMM_PARAMS_CMDID,
    WMI_PDEV_SET_HT_CAP_IE_CMDID,
    WMI_PDEV_SET_VHT_CAP_IE_CMDID,
    WMI_PDEV_SET_DSCP_TID_MAP_CMDID,
    WMI_PDEV_SET_QUIET_MODE_CMDID,
    WMI_PDEV_GREEN_AP_PS_ENABLE_CMDID,
    WMI_PDEV_GET_TPC_CONFIG_CMDID,
    WMI_PDEV_SET_BASE_MACADDR_CMDID,

    /* VDEV (virtual device) specific commands */
    WMI_VDEV_CREATE_CMDID = WMI_CMD_GRP(WMI_GRP_VDEV),
    WMI_VDEV_DELETE_CMDID,
    WMI_VDEV_START_REQUEST_CMDID,
    WMI_VDEV_RESTART_REQUEST_CMDID,
    WMI_VDEV_UP_CMDID,
    WMI_VDEV_STOP_CMDID,
    WMI_VDEV_DOWN_CMDID,
    WMI_VDEV_SET_PARAM_CMDID,
    WMI_VDEV_INSTALL_KEY_CMDID,

    /* peer specific commands */
    WMI_PEER_CREATE_CMDID = WMI_CMD_GRP(WMI_GRP_PEER),
    WMI_PEER_DELETE_CMDID,
    WMI_PEER_FLUSH_TIDS_CMDID,
    WMI_PEER_SET_PARAM_CMDID,
    WMI_PEER_ASSOC_CMDID,
    WMI_PEER_ADD_WDS_ENTRY_CMDID,
    WMI_PEER_REMOVE_WDS_ENTRY_CMDID,
    WMI_PEER_MCAST_GROUP_CMDID,

    /* beacon/management specific commands */
    WMI_BCN_TX_CMDID = WMI_CMD_GRP(WMI_GRP_MGMT),
    WMI_PDEV_SEND_BCN_CMDID,
    WMI_BCN_TMPL_CMDID,
    WMI_BCN_FILTER_RX_CMDID,
    WMI_PRB_REQ_FILTER_RX_CMDID,
    WMI_MGMT_TX_CMDID,
    WMI_PRB_TMPL_CMDID,

    /* commands to directly control BA negotiation directly from host. */
    WMI_ADDBA_CLEAR_RESP_CMDID = WMI_CMD_GRP(WMI_GRP_BA_NEG),
    WMI_ADDBA_SEND_CMDID,
    WMI_ADDBA_STATUS_CMDID,
    WMI_DELBA_SEND_CMDID,
    WMI_ADDBA_SET_RESP_CMDID,
    WMI_SEND_SINGLEAMSDU_CMDID,

    /* Station power save specific config */
    WMI_STA_POWERSAVE_MODE_CMDID = WMI_CMD_GRP(WMI_GRP_STA_PS),
    WMI_STA_POWERSAVE_PARAM_CMDID,
    WMI_STA_MIMO_PS_MODE_CMDID,

    /** DFS-specific commands */
    WMI_PDEV_DFS_ENABLE_CMDID = WMI_CMD_GRP(WMI_GRP_DFS),
    WMI_PDEV_DFS_DISABLE_CMDID,

    /* Roaming specific  commands */
    WMI_ROAM_SCAN_MODE = WMI_CMD_GRP(WMI_GRP_ROAM),
    WMI_ROAM_SCAN_RSSI_THRESHOLD,
    WMI_ROAM_SCAN_PERIOD,
    WMI_ROAM_SCAN_RSSI_CHANGE_THRESHOLD,
    WMI_ROAM_AP_PROFILE,

    /* offload scan specific commands */
    WMI_OFL_SCAN_ADD_AP_PROFILE = WMI_CMD_GRP(WMI_GRP_OFL_SCAN),
    WMI_OFL_SCAN_REMOVE_AP_PROFILE,
    WMI_OFL_SCAN_PERIOD,

    /* P2P specific commands */
    WMI_P2P_DEV_SET_DEVICE_INFO = WMI_CMD_GRP(WMI_GRP_P2P),
    WMI_P2P_DEV_SET_DISCOVERABILITY,
    WMI_P2P_GO_SET_BEACON_IE,
    WMI_P2P_GO_SET_PROBE_RESP_IE,
    WMI_P2P_SET_VENDOR_IE_DATA_CMDID,

    /* AP power save specific config */
    WMI_AP_PS_PEER_PARAM_CMDID = WMI_CMD_GRP(WMI_GRP_AP_PS),
    WMI_AP_PS_PEER_UAPSD_COEX_CMDID,

    /* Rate-control specific commands */
    WMI_PEER_RATE_RETRY_SCHED_CMDID =
        WMI_CMD_GRP(WMI_GRP_RATE_CTRL),

    /* WLAN Profiling commands. */
    WMI_WLAN_PROFILE_TRIGGER_CMDID = WMI_CMD_GRP(WMI_GRP_PROFILE),
    WMI_WLAN_PROFILE_SET_HIST_INTVL_CMDID,
    WMI_WLAN_PROFILE_GET_PROFILE_DATA_CMDID,
    WMI_WLAN_PROFILE_ENABLE_PROFILE_ID_CMDID,
    WMI_WLAN_PROFILE_LIST_PROFILE_ID_CMDID,

    /* Suspend resume command Ids */
    WMI_PDEV_SUSPEND_CMDID = WMI_CMD_GRP(WMI_GRP_SUSPEND),
    WMI_PDEV_RESUME_CMDID,

    /* Beacon filter commands */
    WMI_ADD_BCN_FILTER_CMDID = WMI_CMD_GRP(WMI_GRP_BCN_FILTER),
    WMI_RMV_BCN_FILTER_CMDID,

    /* WOW Specific WMI commands*/
    WMI_WOW_ADD_WAKE_PATTERN_CMDID = WMI_CMD_GRP(WMI_GRP_WOW),
    WMI_WOW_DEL_WAKE_PATTERN_CMDID,
    WMI_WOW_ENABLE_DISABLE_WAKE_EVENT_CMDID,
    WMI_WOW_ENABLE_CMDID,
    WMI_WOW_HOSTWAKEUP_FROM_SLEEP_CMDID,

    /* RTT measurement related cmd */
    WMI_RTT_MEASREQ_CMDID = WMI_CMD_GRP(WMI_GRP_RTT),
    WMI_RTT_TSF_CMDID,

    /* spectral scan commands */
    WMI_VDEV_SPECTRAL_SCAN_CONFIGURE_CMDID = WMI_CMD_GRP(WMI_GRP_SPECTRAL),
    WMI_VDEV_SPECTRAL_SCAN_ENABLE_CMDID,

    /* F/W stats */
    WMI_REQUEST_STATS_CMDID = WMI_CMD_GRP(WMI_GRP_STATS),

    /* ARP OFFLOAD REQUEST*/
    WMI_SET_ARP_NS_OFFLOAD_CMDID = WMI_CMD_GRP(WMI_GRP_ARP_NS_OFL),

    /* NS offload confid*/
    WMI_NETWORK_LIST_OFFLOAD_CONFIG_CMDID = WMI_CMD_GRP(WMI_GRP_NLO_OFL),

    /* GTK offload Specific WMI commands*/
    WMI_GTK_OFFLOAD_CMDID = WMI_CMD_GRP(WMI_GRP_GTK_OFL),

    /* CSA offload Specific WMI commands*/
    WMI_CSA_OFFLOAD_ENABLE_CMDID = WMI_CMD_GRP(WMI_GRP_CSA_OFL),
    WMI_CSA_OFFLOAD_CHANSWITCH_CMDID,

    /* Chatter commands*/
    WMI_CHATTER_SET_MODE_CMDID = WMI_CMD_GRP(WMI_GRP_CHATTER),

    /* addba specific commands */
    WMI_PEER_TID_ADDBA_CMDID = WMI_CMD_GRP(WMI_GRP_TID_ADDBA),
    WMI_PEER_TID_DELBA_CMDID,

    /* set station mimo powersave method */
    WMI_STA_DTIM_PS_METHOD_CMDID,
    /* Configure the Station UAPSD AC Auto Trigger Parameters */
    WMI_STA_UAPSD_AUTO_TRIG_CMDID,

    /* STA Keep alive parameter configuration,
     * Requires WMI_SERVICE_STA_KEEP_ALIVE
     */
    WMI_STA_KEEPALIVE_CMD,

    /* misc command group */
    WMI_ECHO_CMDID = WMI_CMD_GRP(WMI_GRP_MISC),
    WMI_PDEV_UTF_CMDID,
    WMI_DBGLOG_CFG_CMDID,
    WMI_PDEV_QVIT_CMDID,
    WMI_PDEV_FTM_INTG_CMDID,
    WMI_VDEV_SET_KEEPALIVE_CMDID,
    WMI_VDEV_GET_KEEPALIVE_CMDID,
    WMI_FORCE_FW_HANG_CMDID,

    /* GPIO Configuration */
    WMI_GPIO_CONFIG_CMDID = WMI_CMD_GRP(WMI_GRP_GPIO),
    WMI_GPIO_OUTPUT_CMDID,
};

enum wmi_event_id {
    WMI_SERVICE_READY_EVENTID = 0x1,
    WMI_READY_EVENTID,

    /* Scan specific events */
    WMI_SCAN_EVENTID = WMI_EVT_GRP_START_ID(WMI_GRP_SCAN),

    /* PDEV specific events */
    WMI_PDEV_TPC_CONFIG_EVENTID = WMI_EVT_GRP_START_ID(WMI_GRP_PDEV),
    WMI_CHAN_INFO_EVENTID,
    WMI_PHYERR_EVENTID,

    /* VDEV specific events */
    WMI_VDEV_START_RESP_EVENTID = WMI_EVT_GRP_START_ID(WMI_GRP_VDEV),
    WMI_VDEV_STOPPED_EVENTID,
    WMI_VDEV_INSTALL_KEY_COMPLETE_EVENTID,

    /* peer specific events */
    WMI_PEER_STA_KICKOUT_EVENTID = WMI_EVT_GRP_START_ID(WMI_GRP_PEER),

    /* beacon/mgmt specific events */
    WMI_MGMT_RX_EVENTID = WMI_EVT_GRP_START_ID(WMI_GRP_MGMT),
    WMI_HOST_SWBA_EVENTID,
    WMI_TBTTOFFSET_UPDATE_EVENTID,

    /* ADDBA Related WMI Events*/
    WMI_TX_DELBA_COMPLETE_EVENTID = WMI_EVT_GRP_START_ID(WMI_GRP_BA_NEG),
    WMI_TX_ADDBA_COMPLETE_EVENTID,

    /* Roam event to trigger roaming on host */
    WMI_ROAM_EVENTID = WMI_EVT_GRP_START_ID(WMI_GRP_ROAM),
    WMI_PROFILE_MATCH,

    /* WoW */
    WMI_WOW_WAKEUP_HOST_EVENTID = WMI_EVT_GRP_START_ID(WMI_GRP_WOW),

    /* RTT */
    WMI_RTT_MEASUREMENT_REPORT_EVENTID = WMI_EVT_GRP_START_ID(WMI_GRP_RTT),
    WMI_TSF_MEASUREMENT_REPORT_EVENTID,
    WMI_RTT_ERROR_REPORT_EVENTID,

    /* GTK offload */
    WMI_GTK_OFFLOAD_STATUS_EVENTID = WMI_EVT_GRP_START_ID(WMI_GRP_GTK_OFL),
    WMI_GTK_REKEY_FAIL_EVENTID,

    /* CSA IE received event */
    WMI_CSA_HANDLING_EVENTID = WMI_EVT_GRP_START_ID(WMI_GRP_CSA_OFL),

    /* Misc events */
    WMI_ECHO_EVENTID = WMI_EVT_GRP_START_ID(WMI_GRP_MISC),
    WMI_PDEV_UTF_EVENTID,
    WMI_DEBUG_MESG_EVENTID,
    WMI_UPDATE_STATS_EVENTID,
    WMI_DEBUG_PRINT_EVENTID,
    WMI_DCS_INTERFERENCE_EVENTID,
    WMI_PDEV_QVIT_EVENTID,
    WMI_WLAN_PROFILE_DATA_EVENTID,
    WMI_PDEV_FTM_INTG_EVENTID,
    WMI_WLAN_FREQ_AVOID_EVENTID,
    WMI_VDEV_GET_KEEPALIVE_EVENTID,

    /* GPIO Event */
    WMI_GPIO_INPUT_EVENTID = WMI_EVT_GRP_START_ID(WMI_GRP_GPIO),
};

/* Command IDs and command events for 10.X firmware */
enum wmi_10x_cmd_id {
    WMI_10X_START_CMDID = 0x9000,
    WMI_10X_END_CMDID = 0x9FFF,

    /* initialize the wlan sub system */
    WMI_10X_INIT_CMDID,

    /* Scan specific commands */

    WMI_10X_START_SCAN_CMDID = WMI_10X_START_CMDID,
    WMI_10X_STOP_SCAN_CMDID,
    WMI_10X_SCAN_CHAN_LIST_CMDID,
    WMI_10X_ECHO_CMDID,

    /* PDEV(physical device) specific commands */
    WMI_10X_PDEV_SET_REGDOMAIN_CMDID,
    WMI_10X_PDEV_SET_CHANNEL_CMDID,
    WMI_10X_PDEV_SET_PARAM_CMDID,
    WMI_10X_PDEV_PKTLOG_ENABLE_CMDID,
    WMI_10X_PDEV_PKTLOG_DISABLE_CMDID,
    WMI_10X_PDEV_SET_WMM_PARAMS_CMDID,
    WMI_10X_PDEV_SET_HT_CAP_IE_CMDID,
    WMI_10X_PDEV_SET_VHT_CAP_IE_CMDID,
    WMI_10X_PDEV_SET_BASE_MACADDR_CMDID,
    WMI_10X_PDEV_SET_DSCP_TID_MAP_CMDID,
    WMI_10X_PDEV_SET_QUIET_MODE_CMDID,
    WMI_10X_PDEV_GREEN_AP_PS_ENABLE_CMDID,
    WMI_10X_PDEV_GET_TPC_CONFIG_CMDID,

    /* VDEV(virtual device) specific commands */
    WMI_10X_VDEV_CREATE_CMDID,
    WMI_10X_VDEV_DELETE_CMDID,
    WMI_10X_VDEV_START_REQUEST_CMDID,
    WMI_10X_VDEV_RESTART_REQUEST_CMDID,
    WMI_10X_VDEV_UP_CMDID,
    WMI_10X_VDEV_STOP_CMDID,
    WMI_10X_VDEV_DOWN_CMDID,
    WMI_10X_VDEV_STANDBY_RESPONSE_CMDID,
    WMI_10X_VDEV_RESUME_RESPONSE_CMDID,
    WMI_10X_VDEV_SET_PARAM_CMDID,
    WMI_10X_VDEV_INSTALL_KEY_CMDID,

    /* peer specific commands */
    WMI_10X_PEER_CREATE_CMDID,
    WMI_10X_PEER_DELETE_CMDID,
    WMI_10X_PEER_FLUSH_TIDS_CMDID,
    WMI_10X_PEER_SET_PARAM_CMDID,
    WMI_10X_PEER_ASSOC_CMDID,
    WMI_10X_PEER_ADD_WDS_ENTRY_CMDID,
    WMI_10X_PEER_REMOVE_WDS_ENTRY_CMDID,
    WMI_10X_PEER_MCAST_GROUP_CMDID,

    /* beacon/management specific commands */

    WMI_10X_BCN_TX_CMDID,
    WMI_10X_BCN_PRB_TMPL_CMDID,
    WMI_10X_BCN_FILTER_RX_CMDID,
    WMI_10X_PRB_REQ_FILTER_RX_CMDID,
    WMI_10X_MGMT_TX_CMDID,

    /* commands to directly control ba negotiation directly from host. */
    WMI_10X_ADDBA_CLEAR_RESP_CMDID,
    WMI_10X_ADDBA_SEND_CMDID,
    WMI_10X_ADDBA_STATUS_CMDID,
    WMI_10X_DELBA_SEND_CMDID,
    WMI_10X_ADDBA_SET_RESP_CMDID,
    WMI_10X_SEND_SINGLEAMSDU_CMDID,

    /* Station power save specific config */
    WMI_10X_STA_POWERSAVE_MODE_CMDID,
    WMI_10X_STA_POWERSAVE_PARAM_CMDID,
    WMI_10X_STA_MIMO_PS_MODE_CMDID,

    /* set debug log config */
    WMI_10X_DBGLOG_CFG_CMDID,

    /* DFS-specific commands */
    WMI_10X_PDEV_DFS_ENABLE_CMDID,
    WMI_10X_PDEV_DFS_DISABLE_CMDID,

    /* QVIT specific command id */
    WMI_10X_PDEV_QVIT_CMDID,

    /* Offload Scan and Roaming related  commands */
    WMI_10X_ROAM_SCAN_MODE,
    WMI_10X_ROAM_SCAN_RSSI_THRESHOLD,
    WMI_10X_ROAM_SCAN_PERIOD,
    WMI_10X_ROAM_SCAN_RSSI_CHANGE_THRESHOLD,
    WMI_10X_ROAM_AP_PROFILE,
    WMI_10X_OFL_SCAN_ADD_AP_PROFILE,
    WMI_10X_OFL_SCAN_REMOVE_AP_PROFILE,
    WMI_10X_OFL_SCAN_PERIOD,

    /* P2P specific commands */
    WMI_10X_P2P_DEV_SET_DEVICE_INFO,
    WMI_10X_P2P_DEV_SET_DISCOVERABILITY,
    WMI_10X_P2P_GO_SET_BEACON_IE,
    WMI_10X_P2P_GO_SET_PROBE_RESP_IE,

    /* AP power save specific config */
    WMI_10X_AP_PS_PEER_PARAM_CMDID,
    WMI_10X_AP_PS_PEER_UAPSD_COEX_CMDID,

    /* Rate-control specific commands */
    WMI_10X_PEER_RATE_RETRY_SCHED_CMDID,

    /* WLAN Profiling commands. */
    WMI_10X_WLAN_PROFILE_TRIGGER_CMDID,
    WMI_10X_WLAN_PROFILE_SET_HIST_INTVL_CMDID,
    WMI_10X_WLAN_PROFILE_GET_PROFILE_DATA_CMDID,
    WMI_10X_WLAN_PROFILE_ENABLE_PROFILE_ID_CMDID,
    WMI_10X_WLAN_PROFILE_LIST_PROFILE_ID_CMDID,

    /* Suspend resume command Ids */
    WMI_10X_PDEV_SUSPEND_CMDID,
    WMI_10X_PDEV_RESUME_CMDID,

    /* Beacon filter commands */
    WMI_10X_ADD_BCN_FILTER_CMDID,
    WMI_10X_RMV_BCN_FILTER_CMDID,

    /* WOW Specific WMI commands*/
    WMI_10X_WOW_ADD_WAKE_PATTERN_CMDID,
    WMI_10X_WOW_DEL_WAKE_PATTERN_CMDID,
    WMI_10X_WOW_ENABLE_DISABLE_WAKE_EVENT_CMDID,
    WMI_10X_WOW_ENABLE_CMDID,
    WMI_10X_WOW_HOSTWAKEUP_FROM_SLEEP_CMDID,

    /* RTT measurement related cmd */
    WMI_10X_RTT_MEASREQ_CMDID,
    WMI_10X_RTT_TSF_CMDID,

    /* transmit beacon by value */
    WMI_10X_PDEV_SEND_BCN_CMDID,

    /* F/W stats */
    WMI_10X_VDEV_SPECTRAL_SCAN_CONFIGURE_CMDID,
    WMI_10X_VDEV_SPECTRAL_SCAN_ENABLE_CMDID,
    WMI_10X_REQUEST_STATS_CMDID,

    /* GPIO Configuration */
    WMI_10X_GPIO_CONFIG_CMDID,
    WMI_10X_GPIO_OUTPUT_CMDID,

    WMI_10X_PDEV_UTF_CMDID = WMI_10X_END_CMDID - 1,
};

enum wmi_10x_event_id {
    WMI_10X_SERVICE_READY_EVENTID = 0x8000,
    WMI_10X_READY_EVENTID,
    WMI_10X_START_EVENTID = 0x9000,
    WMI_10X_END_EVENTID = 0x9FFF,

    /* Scan specific events */
    WMI_10X_SCAN_EVENTID = WMI_10X_START_EVENTID,
    WMI_10X_ECHO_EVENTID,
    WMI_10X_DEBUG_MESG_EVENTID,
    WMI_10X_UPDATE_STATS_EVENTID,

    /* Instantaneous RSSI event */
    WMI_10X_INST_RSSI_STATS_EVENTID,

    /* VDEV specific events */
    WMI_10X_VDEV_START_RESP_EVENTID,
    WMI_10X_VDEV_STANDBY_REQ_EVENTID,
    WMI_10X_VDEV_RESUME_REQ_EVENTID,
    WMI_10X_VDEV_STOPPED_EVENTID,

    /* peer  specific events */
    WMI_10X_PEER_STA_KICKOUT_EVENTID,

    /* beacon/mgmt specific events */
    WMI_10X_HOST_SWBA_EVENTID,
    WMI_10X_TBTTOFFSET_UPDATE_EVENTID,
    WMI_10X_MGMT_RX_EVENTID,

    /* Channel stats event */
    WMI_10X_CHAN_INFO_EVENTID,

    /* PHY Error specific WMI event */
    WMI_10X_PHYERR_EVENTID,

    /* Roam event to trigger roaming on host */
    WMI_10X_ROAM_EVENTID,

    /* matching AP found from list of profiles */
    WMI_10X_PROFILE_MATCH,

    /* debug print message used for tracing FW code while debugging */
    WMI_10X_DEBUG_PRINT_EVENTID,
    /* VI spoecific event */
    WMI_10X_PDEV_QVIT_EVENTID,
    /* FW code profile data in response to profile request */
    WMI_10X_WLAN_PROFILE_DATA_EVENTID,

    /*RTT related event ID*/
    WMI_10X_RTT_MEASUREMENT_REPORT_EVENTID,
    WMI_10X_TSF_MEASUREMENT_REPORT_EVENTID,
    WMI_10X_RTT_ERROR_REPORT_EVENTID,

    WMI_10X_WOW_WAKEUP_HOST_EVENTID,
    WMI_10X_DCS_INTERFERENCE_EVENTID,

    /* TPC config for the current operating channel */
    WMI_10X_PDEV_TPC_CONFIG_EVENTID,

    WMI_10X_GPIO_INPUT_EVENTID,
    WMI_10X_PDEV_UTF_EVENTID = WMI_10X_END_EVENTID - 1,
};

enum wmi_10_2_cmd_id {
    WMI_10_2_START_CMDID = 0x9000,
    WMI_10_2_END_CMDID = 0x9FFF,
    WMI_10_2_INIT_CMDID,
    WMI_10_2_START_SCAN_CMDID = WMI_10_2_START_CMDID,
    WMI_10_2_STOP_SCAN_CMDID,
    WMI_10_2_SCAN_CHAN_LIST_CMDID,
    WMI_10_2_ECHO_CMDID,
    WMI_10_2_PDEV_SET_REGDOMAIN_CMDID,
    WMI_10_2_PDEV_SET_CHANNEL_CMDID,
    WMI_10_2_PDEV_SET_PARAM_CMDID,
    WMI_10_2_PDEV_PKTLOG_ENABLE_CMDID,
    WMI_10_2_PDEV_PKTLOG_DISABLE_CMDID,
    WMI_10_2_PDEV_SET_WMM_PARAMS_CMDID,
    WMI_10_2_PDEV_SET_HT_CAP_IE_CMDID,
    WMI_10_2_PDEV_SET_VHT_CAP_IE_CMDID,
    WMI_10_2_PDEV_SET_BASE_MACADDR_CMDID,
    WMI_10_2_PDEV_SET_QUIET_MODE_CMDID,
    WMI_10_2_PDEV_GREEN_AP_PS_ENABLE_CMDID,
    WMI_10_2_PDEV_GET_TPC_CONFIG_CMDID,
    WMI_10_2_VDEV_CREATE_CMDID,
    WMI_10_2_VDEV_DELETE_CMDID,
    WMI_10_2_VDEV_START_REQUEST_CMDID,
    WMI_10_2_VDEV_RESTART_REQUEST_CMDID,
    WMI_10_2_VDEV_UP_CMDID,
    WMI_10_2_VDEV_STOP_CMDID,
    WMI_10_2_VDEV_DOWN_CMDID,
    WMI_10_2_VDEV_STANDBY_RESPONSE_CMDID,
    WMI_10_2_VDEV_RESUME_RESPONSE_CMDID,
    WMI_10_2_VDEV_SET_PARAM_CMDID,
    WMI_10_2_VDEV_INSTALL_KEY_CMDID,
    WMI_10_2_VDEV_SET_DSCP_TID_MAP_CMDID,
    WMI_10_2_PEER_CREATE_CMDID,
    WMI_10_2_PEER_DELETE_CMDID,
    WMI_10_2_PEER_FLUSH_TIDS_CMDID,
    WMI_10_2_PEER_SET_PARAM_CMDID,
    WMI_10_2_PEER_ASSOC_CMDID,
    WMI_10_2_PEER_ADD_WDS_ENTRY_CMDID,
    WMI_10_2_PEER_UPDATE_WDS_ENTRY_CMDID,
    WMI_10_2_PEER_REMOVE_WDS_ENTRY_CMDID,
    WMI_10_2_PEER_MCAST_GROUP_CMDID,
    WMI_10_2_BCN_TX_CMDID,
    WMI_10_2_BCN_PRB_TMPL_CMDID,
    WMI_10_2_BCN_FILTER_RX_CMDID,
    WMI_10_2_PRB_REQ_FILTER_RX_CMDID,
    WMI_10_2_MGMT_TX_CMDID,
    WMI_10_2_ADDBA_CLEAR_RESP_CMDID,
    WMI_10_2_ADDBA_SEND_CMDID,
    WMI_10_2_ADDBA_STATUS_CMDID,
    WMI_10_2_DELBA_SEND_CMDID,
    WMI_10_2_ADDBA_SET_RESP_CMDID,
    WMI_10_2_SEND_SINGLEAMSDU_CMDID,
    WMI_10_2_STA_POWERSAVE_MODE_CMDID,
    WMI_10_2_STA_POWERSAVE_PARAM_CMDID,
    WMI_10_2_STA_MIMO_PS_MODE_CMDID,
    WMI_10_2_DBGLOG_CFG_CMDID,
    WMI_10_2_PDEV_DFS_ENABLE_CMDID,
    WMI_10_2_PDEV_DFS_DISABLE_CMDID,
    WMI_10_2_PDEV_QVIT_CMDID,
    WMI_10_2_ROAM_SCAN_MODE,
    WMI_10_2_ROAM_SCAN_RSSI_THRESHOLD,
    WMI_10_2_ROAM_SCAN_PERIOD,
    WMI_10_2_ROAM_SCAN_RSSI_CHANGE_THRESHOLD,
    WMI_10_2_ROAM_AP_PROFILE,
    WMI_10_2_OFL_SCAN_ADD_AP_PROFILE,
    WMI_10_2_OFL_SCAN_REMOVE_AP_PROFILE,
    WMI_10_2_OFL_SCAN_PERIOD,
    WMI_10_2_P2P_DEV_SET_DEVICE_INFO,
    WMI_10_2_P2P_DEV_SET_DISCOVERABILITY,
    WMI_10_2_P2P_GO_SET_BEACON_IE,
    WMI_10_2_P2P_GO_SET_PROBE_RESP_IE,
    WMI_10_2_AP_PS_PEER_PARAM_CMDID,
    WMI_10_2_AP_PS_PEER_UAPSD_COEX_CMDID,
    WMI_10_2_PEER_RATE_RETRY_SCHED_CMDID,
    WMI_10_2_WLAN_PROFILE_TRIGGER_CMDID,
    WMI_10_2_WLAN_PROFILE_SET_HIST_INTVL_CMDID,
    WMI_10_2_WLAN_PROFILE_GET_PROFILE_DATA_CMDID,
    WMI_10_2_WLAN_PROFILE_ENABLE_PROFILE_ID_CMDID,
    WMI_10_2_WLAN_PROFILE_LIST_PROFILE_ID_CMDID,
    WMI_10_2_PDEV_SUSPEND_CMDID,
    WMI_10_2_PDEV_RESUME_CMDID,
    WMI_10_2_ADD_BCN_FILTER_CMDID,
    WMI_10_2_RMV_BCN_FILTER_CMDID,
    WMI_10_2_WOW_ADD_WAKE_PATTERN_CMDID,
    WMI_10_2_WOW_DEL_WAKE_PATTERN_CMDID,
    WMI_10_2_WOW_ENABLE_DISABLE_WAKE_EVENT_CMDID,
    WMI_10_2_WOW_ENABLE_CMDID,
    WMI_10_2_WOW_HOSTWAKEUP_FROM_SLEEP_CMDID,
    WMI_10_2_RTT_MEASREQ_CMDID,
    WMI_10_2_RTT_TSF_CMDID,
    WMI_10_2_RTT_KEEPALIVE_CMDID,
    WMI_10_2_PDEV_SEND_BCN_CMDID,
    WMI_10_2_VDEV_SPECTRAL_SCAN_CONFIGURE_CMDID,
    WMI_10_2_VDEV_SPECTRAL_SCAN_ENABLE_CMDID,
    WMI_10_2_REQUEST_STATS_CMDID,
    WMI_10_2_GPIO_CONFIG_CMDID,
    WMI_10_2_GPIO_OUTPUT_CMDID,
    WMI_10_2_VDEV_RATEMASK_CMDID,
    WMI_10_2_PDEV_SMART_ANT_ENABLE_CMDID,
    WMI_10_2_PDEV_SMART_ANT_SET_RX_ANTENNA_CMDID,
    WMI_10_2_PEER_SMART_ANT_SET_TX_ANTENNA_CMDID,
    WMI_10_2_PEER_SMART_ANT_SET_TRAIN_INFO_CMDID,
    WMI_10_2_PEER_SMART_ANT_SET_NODE_CONFIG_OPS_CMDID,
    WMI_10_2_FORCE_FW_HANG_CMDID,
    WMI_10_2_PDEV_SET_ANTENNA_SWITCH_TABLE_CMDID,
    WMI_10_2_PDEV_SET_CTL_TABLE_CMDID,
    WMI_10_2_PDEV_SET_MIMOGAIN_TABLE_CMDID,
    WMI_10_2_PDEV_RATEPWR_TABLE_CMDID,
    WMI_10_2_PDEV_RATEPWR_CHAINMSK_TABLE_CMDID,
    WMI_10_2_PDEV_GET_INFO,
    WMI_10_2_VDEV_GET_INFO,
    WMI_10_2_VDEV_ATF_REQUEST_CMDID,
    WMI_10_2_PEER_ATF_REQUEST_CMDID,
    WMI_10_2_PDEV_GET_TEMPERATURE_CMDID,
    WMI_10_2_MU_CAL_START_CMDID,
    WMI_10_2_SET_LTEU_CONFIG_CMDID,
    WMI_10_2_SET_CCA_PARAMS,
    WMI_10_2_PDEV_BSS_CHAN_INFO_REQUEST_CMDID,
    WMI_10_2_PDEV_UTF_CMDID = WMI_10_2_END_CMDID - 1,
};

enum wmi_10_2_event_id {
    WMI_10_2_SERVICE_READY_EVENTID = 0x8000,
    WMI_10_2_READY_EVENTID,
    WMI_10_2_DEBUG_MESG_EVENTID,
    WMI_10_2_START_EVENTID = 0x9000,
    WMI_10_2_END_EVENTID = 0x9FFF,
    WMI_10_2_SCAN_EVENTID = WMI_10_2_START_EVENTID,
    WMI_10_2_ECHO_EVENTID,
    WMI_10_2_UPDATE_STATS_EVENTID,
    WMI_10_2_INST_RSSI_STATS_EVENTID,
    WMI_10_2_VDEV_START_RESP_EVENTID,
    WMI_10_2_VDEV_STANDBY_REQ_EVENTID,
    WMI_10_2_VDEV_RESUME_REQ_EVENTID,
    WMI_10_2_VDEV_STOPPED_EVENTID,
    WMI_10_2_PEER_STA_KICKOUT_EVENTID,
    WMI_10_2_HOST_SWBA_EVENTID,
    WMI_10_2_TBTTOFFSET_UPDATE_EVENTID,
    WMI_10_2_MGMT_RX_EVENTID,
    WMI_10_2_CHAN_INFO_EVENTID,
    WMI_10_2_PHYERR_EVENTID,
    WMI_10_2_ROAM_EVENTID,
    WMI_10_2_PROFILE_MATCH,
    WMI_10_2_DEBUG_PRINT_EVENTID,
    WMI_10_2_PDEV_QVIT_EVENTID,
    WMI_10_2_WLAN_PROFILE_DATA_EVENTID,
    WMI_10_2_RTT_MEASUREMENT_REPORT_EVENTID,
    WMI_10_2_TSF_MEASUREMENT_REPORT_EVENTID,
    WMI_10_2_RTT_ERROR_REPORT_EVENTID,
    WMI_10_2_RTT_KEEPALIVE_EVENTID,
    WMI_10_2_WOW_WAKEUP_HOST_EVENTID,
    WMI_10_2_DCS_INTERFERENCE_EVENTID,
    WMI_10_2_PDEV_TPC_CONFIG_EVENTID,
    WMI_10_2_GPIO_INPUT_EVENTID,
    WMI_10_2_PEER_RATECODE_LIST_EVENTID,
    WMI_10_2_GENERIC_BUFFER_EVENTID,
    WMI_10_2_MCAST_BUF_RELEASE_EVENTID,
    WMI_10_2_MCAST_LIST_AGEOUT_EVENTID,
    WMI_10_2_WDS_PEER_EVENTID,
    WMI_10_2_PEER_STA_PS_STATECHG_EVENTID,
    WMI_10_2_PDEV_TEMPERATURE_EVENTID,
    WMI_10_2_MU_REPORT_EVENTID,
    WMI_10_2_PDEV_BSS_CHAN_INFO_EVENTID,
    WMI_10_2_PDEV_UTF_EVENTID = WMI_10_2_END_EVENTID - 1,
};

enum wmi_10_4_cmd_id {
    WMI_10_4_START_CMDID = 0x9000,
    WMI_10_4_END_CMDID = 0x9FFF,
    WMI_10_4_INIT_CMDID,
    WMI_10_4_START_SCAN_CMDID = WMI_10_4_START_CMDID,
    WMI_10_4_STOP_SCAN_CMDID,
    WMI_10_4_SCAN_CHAN_LIST_CMDID,
    WMI_10_4_SCAN_SCH_PRIO_TBL_CMDID,
    WMI_10_4_SCAN_UPDATE_REQUEST_CMDID,
    WMI_10_4_ECHO_CMDID,
    WMI_10_4_PDEV_SET_REGDOMAIN_CMDID,
    WMI_10_4_PDEV_SET_CHANNEL_CMDID,
    WMI_10_4_PDEV_SET_PARAM_CMDID,
    WMI_10_4_PDEV_PKTLOG_ENABLE_CMDID,
    WMI_10_4_PDEV_PKTLOG_DISABLE_CMDID,
    WMI_10_4_PDEV_SET_WMM_PARAMS_CMDID,
    WMI_10_4_PDEV_SET_HT_CAP_IE_CMDID,
    WMI_10_4_PDEV_SET_VHT_CAP_IE_CMDID,
    WMI_10_4_PDEV_SET_BASE_MACADDR_CMDID,
    WMI_10_4_PDEV_SET_DSCP_TID_MAP_CMDID,
    WMI_10_4_PDEV_SET_QUIET_MODE_CMDID,
    WMI_10_4_PDEV_GREEN_AP_PS_ENABLE_CMDID,
    WMI_10_4_PDEV_GET_TPC_CONFIG_CMDID,
    WMI_10_4_VDEV_CREATE_CMDID,
    WMI_10_4_VDEV_DELETE_CMDID,
    WMI_10_4_VDEV_START_REQUEST_CMDID,
    WMI_10_4_VDEV_RESTART_REQUEST_CMDID,
    WMI_10_4_VDEV_UP_CMDID,
    WMI_10_4_VDEV_STOP_CMDID,
    WMI_10_4_VDEV_DOWN_CMDID,
    WMI_10_4_VDEV_STANDBY_RESPONSE_CMDID,
    WMI_10_4_VDEV_RESUME_RESPONSE_CMDID,
    WMI_10_4_VDEV_SET_PARAM_CMDID,
    WMI_10_4_VDEV_INSTALL_KEY_CMDID,
    WMI_10_4_WLAN_PEER_CACHING_ADD_PEER_CMDID,
    WMI_10_4_WLAN_PEER_CACHING_EVICT_PEER_CMDID,
    WMI_10_4_WLAN_PEER_CACHING_RESTORE_PEER_CMDID,
    WMI_10_4_WLAN_PEER_CACHING_PRINT_ALL_PEERS_INFO_CMDID,
    WMI_10_4_PEER_CREATE_CMDID,
    WMI_10_4_PEER_DELETE_CMDID,
    WMI_10_4_PEER_FLUSH_TIDS_CMDID,
    WMI_10_4_PEER_SET_PARAM_CMDID,
    WMI_10_4_PEER_ASSOC_CMDID,
    WMI_10_4_PEER_ADD_WDS_ENTRY_CMDID,
    WMI_10_4_PEER_UPDATE_WDS_ENTRY_CMDID,
    WMI_10_4_PEER_REMOVE_WDS_ENTRY_CMDID,
    WMI_10_4_PEER_ADD_PROXY_STA_ENTRY_CMDID,
    WMI_10_4_PEER_MCAST_GROUP_CMDID,
    WMI_10_4_BCN_TX_CMDID,
    WMI_10_4_PDEV_SEND_BCN_CMDID,
    WMI_10_4_BCN_PRB_TMPL_CMDID,
    WMI_10_4_BCN_FILTER_RX_CMDID,
    WMI_10_4_PRB_REQ_FILTER_RX_CMDID,
    WMI_10_4_MGMT_TX_CMDID,
    WMI_10_4_PRB_TMPL_CMDID,
    WMI_10_4_ADDBA_CLEAR_RESP_CMDID,
    WMI_10_4_ADDBA_SEND_CMDID,
    WMI_10_4_ADDBA_STATUS_CMDID,
    WMI_10_4_DELBA_SEND_CMDID,
    WMI_10_4_ADDBA_SET_RESP_CMDID,
    WMI_10_4_SEND_SINGLEAMSDU_CMDID,
    WMI_10_4_STA_POWERSAVE_MODE_CMDID,
    WMI_10_4_STA_POWERSAVE_PARAM_CMDID,
    WMI_10_4_STA_MIMO_PS_MODE_CMDID,
    WMI_10_4_DBGLOG_CFG_CMDID,
    WMI_10_4_PDEV_DFS_ENABLE_CMDID,
    WMI_10_4_PDEV_DFS_DISABLE_CMDID,
    WMI_10_4_PDEV_QVIT_CMDID,
    WMI_10_4_ROAM_SCAN_MODE,
    WMI_10_4_ROAM_SCAN_RSSI_THRESHOLD,
    WMI_10_4_ROAM_SCAN_PERIOD,
    WMI_10_4_ROAM_SCAN_RSSI_CHANGE_THRESHOLD,
    WMI_10_4_ROAM_AP_PROFILE,
    WMI_10_4_OFL_SCAN_ADD_AP_PROFILE,
    WMI_10_4_OFL_SCAN_REMOVE_AP_PROFILE,
    WMI_10_4_OFL_SCAN_PERIOD,
    WMI_10_4_P2P_DEV_SET_DEVICE_INFO,
    WMI_10_4_P2P_DEV_SET_DISCOVERABILITY,
    WMI_10_4_P2P_GO_SET_BEACON_IE,
    WMI_10_4_P2P_GO_SET_PROBE_RESP_IE,
    WMI_10_4_P2P_SET_VENDOR_IE_DATA_CMDID,
    WMI_10_4_AP_PS_PEER_PARAM_CMDID,
    WMI_10_4_AP_PS_PEER_UAPSD_COEX_CMDID,
    WMI_10_4_PEER_RATE_RETRY_SCHED_CMDID,
    WMI_10_4_WLAN_PROFILE_TRIGGER_CMDID,
    WMI_10_4_WLAN_PROFILE_SET_HIST_INTVL_CMDID,
    WMI_10_4_WLAN_PROFILE_GET_PROFILE_DATA_CMDID,
    WMI_10_4_WLAN_PROFILE_ENABLE_PROFILE_ID_CMDID,
    WMI_10_4_WLAN_PROFILE_LIST_PROFILE_ID_CMDID,
    WMI_10_4_PDEV_SUSPEND_CMDID,
    WMI_10_4_PDEV_RESUME_CMDID,
    WMI_10_4_ADD_BCN_FILTER_CMDID,
    WMI_10_4_RMV_BCN_FILTER_CMDID,
    WMI_10_4_WOW_ADD_WAKE_PATTERN_CMDID,
    WMI_10_4_WOW_DEL_WAKE_PATTERN_CMDID,
    WMI_10_4_WOW_ENABLE_DISABLE_WAKE_EVENT_CMDID,
    WMI_10_4_WOW_ENABLE_CMDID,
    WMI_10_4_WOW_HOSTWAKEUP_FROM_SLEEP_CMDID,
    WMI_10_4_RTT_MEASREQ_CMDID,
    WMI_10_4_RTT_TSF_CMDID,
    WMI_10_4_RTT_KEEPALIVE_CMDID,
    WMI_10_4_OEM_REQ_CMDID,
    WMI_10_4_NAN_CMDID,
    WMI_10_4_VDEV_SPECTRAL_SCAN_CONFIGURE_CMDID,
    WMI_10_4_VDEV_SPECTRAL_SCAN_ENABLE_CMDID,
    WMI_10_4_REQUEST_STATS_CMDID,
    WMI_10_4_GPIO_CONFIG_CMDID,
    WMI_10_4_GPIO_OUTPUT_CMDID,
    WMI_10_4_VDEV_RATEMASK_CMDID,
    WMI_10_4_CSA_OFFLOAD_ENABLE_CMDID,
    WMI_10_4_GTK_OFFLOAD_CMDID,
    WMI_10_4_QBOOST_CFG_CMDID,
    WMI_10_4_CSA_OFFLOAD_CHANSWITCH_CMDID,
    WMI_10_4_PDEV_SMART_ANT_ENABLE_CMDID,
    WMI_10_4_PDEV_SMART_ANT_SET_RX_ANTENNA_CMDID,
    WMI_10_4_PEER_SMART_ANT_SET_TX_ANTENNA_CMDID,
    WMI_10_4_PEER_SMART_ANT_SET_TRAIN_INFO_CMDID,
    WMI_10_4_PEER_SMART_ANT_SET_NODE_CONFIG_OPS_CMDID,
    WMI_10_4_VDEV_SET_KEEPALIVE_CMDID,
    WMI_10_4_VDEV_GET_KEEPALIVE_CMDID,
    WMI_10_4_FORCE_FW_HANG_CMDID,
    WMI_10_4_PDEV_SET_ANTENNA_SWITCH_TABLE_CMDID,
    WMI_10_4_PDEV_SET_CTL_TABLE_CMDID,
    WMI_10_4_PDEV_SET_MIMOGAIN_TABLE_CMDID,
    WMI_10_4_PDEV_RATEPWR_TABLE_CMDID,
    WMI_10_4_PDEV_RATEPWR_CHAINMSK_TABLE_CMDID,
    WMI_10_4_PDEV_FIPS_CMDID,
    WMI_10_4_TT_SET_CONF_CMDID,
    WMI_10_4_FWTEST_CMDID,
    WMI_10_4_VDEV_ATF_REQUEST_CMDID,
    WMI_10_4_PEER_ATF_REQUEST_CMDID,
    WMI_10_4_PDEV_GET_ANI_CCK_CONFIG_CMDID,
    WMI_10_4_PDEV_GET_ANI_OFDM_CONFIG_CMDID,
    WMI_10_4_PDEV_RESERVE_AST_ENTRY_CMDID,
    WMI_10_4_PDEV_GET_NFCAL_POWER_CMDID,
    WMI_10_4_PDEV_GET_TPC_CMDID,
    WMI_10_4_PDEV_GET_AST_INFO_CMDID,
    WMI_10_4_VDEV_SET_DSCP_TID_MAP_CMDID,
    WMI_10_4_PDEV_GET_TEMPERATURE_CMDID,
    WMI_10_4_PDEV_GET_INFO_CMDID,
    WMI_10_4_VDEV_GET_INFO_CMDID,
    WMI_10_4_VDEV_FILTER_NEIGHBOR_RX_PACKETS_CMDID,
    WMI_10_4_MU_CAL_START_CMDID,
    WMI_10_4_SET_CCA_PARAMS_CMDID,
    WMI_10_4_PDEV_BSS_CHAN_INFO_REQUEST_CMDID,
    WMI_10_4_EXT_RESOURCE_CFG_CMDID,
    WMI_10_4_VDEV_SET_IE_CMDID,
    WMI_10_4_SET_LTEU_CONFIG_CMDID,
    WMI_10_4_PDEV_UTF_CMDID = WMI_10_4_END_CMDID - 1,
};

enum wmi_10_4_event_id {
    WMI_10_4_SERVICE_READY_EVENTID = 0x8000,
    WMI_10_4_READY_EVENTID,
    WMI_10_4_DEBUG_MESG_EVENTID,
    WMI_10_4_START_EVENTID = 0x9000,
    WMI_10_4_END_EVENTID = 0x9FFF,
    WMI_10_4_SCAN_EVENTID = WMI_10_4_START_EVENTID,
    WMI_10_4_ECHO_EVENTID,
    WMI_10_4_UPDATE_STATS_EVENTID,
    WMI_10_4_INST_RSSI_STATS_EVENTID,
    WMI_10_4_VDEV_START_RESP_EVENTID,
    WMI_10_4_VDEV_STANDBY_REQ_EVENTID,
    WMI_10_4_VDEV_RESUME_REQ_EVENTID,
    WMI_10_4_VDEV_STOPPED_EVENTID,
    WMI_10_4_PEER_STA_KICKOUT_EVENTID,
    WMI_10_4_HOST_SWBA_EVENTID,
    WMI_10_4_TBTTOFFSET_UPDATE_EVENTID,
    WMI_10_4_MGMT_RX_EVENTID,
    WMI_10_4_CHAN_INFO_EVENTID,
    WMI_10_4_PHYERR_EVENTID,
    WMI_10_4_ROAM_EVENTID,
    WMI_10_4_PROFILE_MATCH,
    WMI_10_4_DEBUG_PRINT_EVENTID,
    WMI_10_4_PDEV_QVIT_EVENTID,
    WMI_10_4_WLAN_PROFILE_DATA_EVENTID,
    WMI_10_4_RTT_MEASUREMENT_REPORT_EVENTID,
    WMI_10_4_TSF_MEASUREMENT_REPORT_EVENTID,
    WMI_10_4_RTT_ERROR_REPORT_EVENTID,
    WMI_10_4_RTT_KEEPALIVE_EVENTID,
    WMI_10_4_OEM_CAPABILITY_EVENTID,
    WMI_10_4_OEM_MEASUREMENT_REPORT_EVENTID,
    WMI_10_4_OEM_ERROR_REPORT_EVENTID,
    WMI_10_4_NAN_EVENTID,
    WMI_10_4_WOW_WAKEUP_HOST_EVENTID,
    WMI_10_4_GTK_OFFLOAD_STATUS_EVENTID,
    WMI_10_4_GTK_REKEY_FAIL_EVENTID,
    WMI_10_4_DCS_INTERFERENCE_EVENTID,
    WMI_10_4_PDEV_TPC_CONFIG_EVENTID,
    WMI_10_4_CSA_HANDLING_EVENTID,
    WMI_10_4_GPIO_INPUT_EVENTID,
    WMI_10_4_PEER_RATECODE_LIST_EVENTID,
    WMI_10_4_GENERIC_BUFFER_EVENTID,
    WMI_10_4_MCAST_BUF_RELEASE_EVENTID,
    WMI_10_4_MCAST_LIST_AGEOUT_EVENTID,
    WMI_10_4_VDEV_GET_KEEPALIVE_EVENTID,
    WMI_10_4_WDS_PEER_EVENTID,
    WMI_10_4_PEER_STA_PS_STATECHG_EVENTID,
    WMI_10_4_PDEV_FIPS_EVENTID,
    WMI_10_4_TT_STATS_EVENTID,
    WMI_10_4_PDEV_CHANNEL_HOPPING_EVENTID,
    WMI_10_4_PDEV_ANI_CCK_LEVEL_EVENTID,
    WMI_10_4_PDEV_ANI_OFDM_LEVEL_EVENTID,
    WMI_10_4_PDEV_RESERVE_AST_ENTRY_EVENTID,
    WMI_10_4_PDEV_NFCAL_POWER_EVENTID,
    WMI_10_4_PDEV_TPC_EVENTID,
    WMI_10_4_PDEV_GET_AST_INFO_EVENTID,
    WMI_10_4_PDEV_TEMPERATURE_EVENTID,
    WMI_10_4_PDEV_NFCAL_POWER_ALL_CHANNELS_EVENTID,
    WMI_10_4_PDEV_BSS_CHAN_INFO_EVENTID,
    WMI_10_4_MU_REPORT_EVENTID,
    WMI_10_4_PDEV_UTF_EVENTID = WMI_10_4_END_EVENTID - 1,
};

enum wmi_phy_mode {
    MODE_11A        = 0,   /* 11a Mode */
    MODE_11G        = 1,   /* 11b/g Mode */
    MODE_11B        = 2,   /* 11b Mode */
    MODE_11GONLY    = 3,   /* 11g only Mode */
    MODE_11NA_HT20   = 4,  /* 11a HT20 mode */
    MODE_11NG_HT20   = 5,  /* 11g HT20 mode */
    MODE_11NA_HT40   = 6,  /* 11a HT40 mode */
    MODE_11NG_HT40   = 7,  /* 11g HT40 mode */
    MODE_11AC_VHT20 = 8,
    MODE_11AC_VHT40 = 9,
    MODE_11AC_VHT80 = 10,
    /*    MODE_11AC_VHT160 = 11, */
    MODE_11AC_VHT20_2G = 11,
    MODE_11AC_VHT40_2G = 12,
    MODE_11AC_VHT80_2G = 13,
    MODE_11AC_VHT80_80 = 14,
    MODE_11AC_VHT160 = 15,
    MODE_UNKNOWN    = 16,
    MODE_MAX        = 16
};

static inline const char* ath10k_wmi_phymode_str(enum wmi_phy_mode mode) {
    switch (mode) {
    case MODE_11A:
        return "11a";
    case MODE_11G:
        return "11g";
    case MODE_11B:
        return "11b";
    case MODE_11GONLY:
        return "11gonly";
    case MODE_11NA_HT20:
        return "11na-ht20";
    case MODE_11NG_HT20:
        return "11ng-ht20";
    case MODE_11NA_HT40:
        return "11na-ht40";
    case MODE_11NG_HT40:
        return "11ng-ht40";
    case MODE_11AC_VHT20:
        return "11ac-vht20";
    case MODE_11AC_VHT40:
        return "11ac-vht40";
    case MODE_11AC_VHT80:
        return "11ac-vht80";
    case MODE_11AC_VHT160:
        return "11ac-vht160";
    case MODE_11AC_VHT80_80:
        return "11ac-vht80+80";
    case MODE_11AC_VHT20_2G:
        return "11ac-vht20-2g";
    case MODE_11AC_VHT40_2G:
        return "11ac-vht40-2g";
    case MODE_11AC_VHT80_2G:
        return "11ac-vht80-2g";
    case MODE_UNKNOWN:
        /* skip */
        break;

        /* no default handler to allow compiler to check that the
         * enum is fully handled
         */
    };

    return "<unknown>";
}

#define WMI_CHAN_LIST_TAG   0x1
#define WMI_SSID_LIST_TAG   0x2
#define WMI_BSSID_LIST_TAG  0x3
#define WMI_IE_TAG      0x4

struct wmi_channel {
    uint32_t mhz;
    uint32_t band_center_freq1;
    uint32_t band_center_freq2; /* valid for 11ac, 80plus80 */
    union {
        uint32_t flags; /* WMI_CHAN_FLAG_ */
        struct {
            uint8_t mode; /* only 6 LSBs */
        } __PACKED;
    } __PACKED;
    union {
        uint32_t reginfo0;
        struct {
            /* note: power unit is 0.5 dBm */
            uint8_t min_power;
            uint8_t max_power;
            uint8_t reg_power;
            uint8_t reg_classid;
        } __PACKED;
    } __PACKED;
    union {
        uint32_t reginfo1;
        struct {
            uint8_t antenna_max;
            uint8_t max_tx_power;
        } __PACKED;
    } __PACKED;
} __PACKED;

struct wmi_channel_arg {
    uint32_t freq;
    uint32_t band_center_freq1;
    uint32_t band_center_freq2;
    bool passive;
    bool allow_ibss;
    bool allow_ht;
    bool allow_vht;
    bool ht40plus;
    bool chan_radar;
    /* note: power unit is 0.5 dBm */
    uint32_t min_power;
    uint32_t max_power;
    uint32_t max_reg_power;
    uint32_t max_antenna_gain;
    uint32_t reg_class_id;
    enum wmi_phy_mode mode;
};

enum wmi_channel_change_cause {
    WMI_CHANNEL_CHANGE_CAUSE_NONE = 0,
    WMI_CHANNEL_CHANGE_CAUSE_CSA,
};

#define WMI_CHAN_FLAG_HT40_PLUS      (1 << 6)
#define WMI_CHAN_FLAG_PASSIVE        (1 << 7)
#define WMI_CHAN_FLAG_ADHOC_ALLOWED  (1 << 8)
#define WMI_CHAN_FLAG_AP_DISABLED    (1 << 9)
#define WMI_CHAN_FLAG_DFS            (1 << 10)
#define WMI_CHAN_FLAG_ALLOW_HT       (1 << 11)
#define WMI_CHAN_FLAG_ALLOW_VHT      (1 << 12)

/* Indicate reason for channel switch */
#define WMI_CHANNEL_CHANGE_CAUSE_CSA (1 << 13)

#define WMI_MAX_SPATIAL_STREAM        3 /* default max ss */

/* HT Capabilities*/
#define WMI_HT_CAP_ENABLED                0x0001   /* HT Enabled/ disabled */
#define WMI_HT_CAP_HT20_SGI               0x0002   /* Short Guard Interval with HT20 */
#define WMI_HT_CAP_DYNAMIC_SMPS           0x0004   /* Dynamic MIMO powersave */
#define WMI_HT_CAP_TX_STBC                0x0008   /* B3 TX STBC */
#define WMI_HT_CAP_TX_STBC_MASK_SHIFT     3
#define WMI_HT_CAP_RX_STBC                0x0030   /* B4-B5 RX STBC */
#define WMI_HT_CAP_RX_STBC_MASK_SHIFT     4
#define WMI_HT_CAP_LDPC                   0x0040   /* LDPC supported */
#define WMI_HT_CAP_L_SIG_TXOP_PROT        0x0080   /* L-SIG TXOP Protection */
#define WMI_HT_CAP_MPDU_DENSITY           0x0700   /* MPDU Density */
#define WMI_HT_CAP_MPDU_DENSITY_MASK_SHIFT 8
#define WMI_HT_CAP_HT40_SGI               0x0800

#define WMI_HT_CAP_DEFAULT_ALL (WMI_HT_CAP_ENABLED       | \
                                WMI_HT_CAP_HT20_SGI      | \
                                WMI_HT_CAP_HT40_SGI      | \
                                WMI_HT_CAP_TX_STBC       | \
                                WMI_HT_CAP_RX_STBC       | \
                                WMI_HT_CAP_LDPC)

/*
 * WMI_VHT_CAP_* these maps to ieee 802.11ac vht capability information
 * field. The fields not defined here are not supported, or reserved.
 * Do not change these masks and if you have to add new one follow the
 * bitmask as specified by 802.11ac draft.
 */

#define WMI_VHT_CAP_MAX_MPDU_LEN_MASK            0x00000003
#define WMI_VHT_CAP_RX_LDPC                      0x00000010
#define WMI_VHT_CAP_SGI_80MHZ                    0x00000020
#define WMI_VHT_CAP_SGI_160MHZ                   0x00000040
#define WMI_VHT_CAP_TX_STBC                      0x00000080
#define WMI_VHT_CAP_RX_STBC_MASK                 0x00000300
#define WMI_VHT_CAP_RX_STBC_MASK_SHIFT           8
#define WMI_VHT_CAP_SU_BFER                      0x00000800
#define WMI_VHT_CAP_SU_BFEE                      0x00001000
#define WMI_VHT_CAP_MAX_CS_ANT_MASK              0x0000E000
#define WMI_VHT_CAP_MAX_CS_ANT_MASK_SHIFT        13
#define WMI_VHT_CAP_MAX_SND_DIM_MASK             0x00070000
#define WMI_VHT_CAP_MAX_SND_DIM_MASK_SHIFT       16
#define WMI_VHT_CAP_MU_BFER                      0x00080000
#define WMI_VHT_CAP_MU_BFEE                      0x00100000
#define WMI_VHT_CAP_MAX_AMPDU_LEN_EXP            0x03800000
#define WMI_VHT_CAP_MAX_AMPDU_LEN_EXP_SHIFT      23
#define WMI_VHT_CAP_RX_FIXED_ANT                 0x10000000
#define WMI_VHT_CAP_TX_FIXED_ANT                 0x20000000

/* The following also refer for max HT AMSDU */
#define WMI_VHT_CAP_MAX_MPDU_LEN_3839            0x00000000
#define WMI_VHT_CAP_MAX_MPDU_LEN_7935            0x00000001
#define WMI_VHT_CAP_MAX_MPDU_LEN_11454           0x00000002

#define WMI_VHT_CAP_DEFAULT_ALL (WMI_VHT_CAP_MAX_MPDU_LEN_11454  | \
                                 WMI_VHT_CAP_RX_LDPC             | \
                                 WMI_VHT_CAP_SGI_80MHZ           | \
                                 WMI_VHT_CAP_TX_STBC             | \
                                 WMI_VHT_CAP_RX_STBC_MASK        | \
                                 WMI_VHT_CAP_MAX_AMPDU_LEN_EXP   | \
                                 WMI_VHT_CAP_RX_FIXED_ANT        | \
                                 WMI_VHT_CAP_TX_FIXED_ANT)

/*
 * Interested readers refer to Rx/Tx MCS Map definition as defined in
 * 802.11ac
 */
#define WMI_VHT_MAX_MCS_4_SS_MASK(r, ss)      ((3 & (r)) << (((ss) - 1) << 1))
#define WMI_VHT_MAX_SUPP_RATE_MASK           0x1fff0000
#define WMI_VHT_MAX_SUPP_RATE_MASK_SHIFT     16

enum {
    REGDMN_MODE_11A              = 0x00001, /* 11a channels */
    REGDMN_MODE_TURBO            = 0x00002, /* 11a turbo-only channels */
    REGDMN_MODE_11B              = 0x00004, /* 11b channels */
    REGDMN_MODE_PUREG            = 0x00008, /* 11g channels (OFDM only) */
    REGDMN_MODE_11G              = 0x00008, /* XXX historical */
    REGDMN_MODE_108G             = 0x00020, /* 11a+Turbo channels */
    REGDMN_MODE_108A             = 0x00040, /* 11g+Turbo channels */
    REGDMN_MODE_XR               = 0x00100, /* XR channels */
    REGDMN_MODE_11A_HALF_RATE    = 0x00200, /* 11A half rate channels */
    REGDMN_MODE_11A_QUARTER_RATE = 0x00400, /* 11A quarter rate channels */
    REGDMN_MODE_11NG_HT20        = 0x00800, /* 11N-G HT20 channels */
    REGDMN_MODE_11NA_HT20        = 0x01000, /* 11N-A HT20 channels */
    REGDMN_MODE_11NG_HT40PLUS    = 0x02000, /* 11N-G HT40 + channels */
    REGDMN_MODE_11NG_HT40MINUS   = 0x04000, /* 11N-G HT40 - channels */
    REGDMN_MODE_11NA_HT40PLUS    = 0x08000, /* 11N-A HT40 + channels */
    REGDMN_MODE_11NA_HT40MINUS   = 0x10000, /* 11N-A HT40 - channels */
    REGDMN_MODE_11AC_VHT20       = 0x20000, /* 5Ghz, VHT20 */
    REGDMN_MODE_11AC_VHT40PLUS   = 0x40000, /* 5Ghz, VHT40 + channels */
    REGDMN_MODE_11AC_VHT40MINUS  = 0x80000, /* 5Ghz  VHT40 - channels */
    REGDMN_MODE_11AC_VHT80       = 0x100000, /* 5Ghz, VHT80 channels */
    REGDMN_MODE_11AC_VHT160      = 0x200000,     /* 5Ghz, VHT160 channels */
    REGDMN_MODE_11AC_VHT80_80    = 0x400000,     /* 5Ghz, VHT80+80 channels */
    REGDMN_MODE_ALL              = 0xffffffff
};

#define REGDMN_CAP1_CHAN_HALF_RATE        0x00000001
#define REGDMN_CAP1_CHAN_QUARTER_RATE     0x00000002
#define REGDMN_CAP1_CHAN_HAL49GHZ         0x00000004

/* regulatory capabilities */
#define REGDMN_EEPROM_EEREGCAP_EN_FCC_MIDBAND   0x0040
#define REGDMN_EEPROM_EEREGCAP_EN_KK_U1_EVEN    0x0080
#define REGDMN_EEPROM_EEREGCAP_EN_KK_U2         0x0100
#define REGDMN_EEPROM_EEREGCAP_EN_KK_MIDBAND    0x0200
#define REGDMN_EEPROM_EEREGCAP_EN_KK_U1_ODD     0x0400
#define REGDMN_EEPROM_EEREGCAP_EN_KK_NEW_11A    0x0800

struct hal_reg_capabilities {
    /* regdomain value specified in EEPROM */
    uint32_t eeprom_rd;
    /*regdomain */
    uint32_t eeprom_rd_ext;
    /* CAP1 capabilities bit map. */
    uint32_t regcap1;
    /* REGDMN EEPROM CAP. */
    uint32_t regcap2;
    /* REGDMN MODE */
    uint32_t wireless_modes;
    uint32_t low_2ghz_chan;
    uint32_t high_2ghz_chan;
    uint32_t low_5ghz_chan;
    uint32_t high_5ghz_chan;
} __PACKED;

enum wlan_mode_capability {
    WHAL_WLAN_11A_CAPABILITY   = 0x1,
    WHAL_WLAN_11G_CAPABILITY   = 0x2,
    WHAL_WLAN_11AG_CAPABILITY  = 0x3,
};

/* structure used by FW for requesting host memory */
struct wlan_host_mem_req {
    /* ID of the request */
    uint32_t req_id;
    /* size of the  of each unit */
    uint32_t unit_size;
    /* flags to  indicate that
     * the number units is dependent
     * on number of resources(num vdevs num peers .. etc)
     */
    uint32_t num_unit_info;
    /*
     * actual number of units to allocate . if flags in the num_unit_info
     * indicate that number of units is tied to number of a particular
     * resource to allocate then  num_units filed is set to 0 and host
     * will derive the number units from number of the resources it is
     * requesting.
     */
    uint32_t num_units;
} __PACKED;

/*
 * The following struct holds optional payload for
 * wmi_service_ready_event,e.g., 11ac pass some of the
 * device capability to the host.
 */
struct wmi_service_ready_event {
    uint32_t sw_version;
    uint32_t sw_version_1;
    uint32_t abi_version;
    /* WMI_PHY_CAPABILITY */
    uint32_t phy_capability;
    /* Maximum number of frag table entries that SW will populate less 1 */
    uint32_t max_frag_entry;
    uint32_t wmi_service_bitmap[16];
    uint32_t num_rf_chains;
    /*
     * The following field is only valid for service type
     * WMI_SERVICE_11AC
     */
    uint32_t ht_cap_info; /* WMI HT Capability */
    uint32_t vht_cap_info; /* VHT capability info field of 802.11ac */
    uint32_t vht_supp_mcs; /* VHT Supported MCS Set field Rx/Tx same */
    uint32_t hw_min_tx_power;
    uint32_t hw_max_tx_power;
    struct hal_reg_capabilities hal_reg_capabilities;
    uint32_t sys_cap_info;
    uint32_t min_pkt_size_enable; /* Enterprise mode short pkt enable */
    /*
     * Max beacon and Probe Response IE offload size
     * (includes optional P2P IEs)
     */
    uint32_t max_bcn_ie_size;
    /*
     * request to host to allocate a chuck of memory and pss it down to FW
     * via WM_INIT. FW uses this as FW extesnsion memory for saving its
     * data structures. Only valid for low latency interfaces like PCIE
     * where FW can access this memory directly (or) by DMA.
     */
    uint32_t num_mem_reqs;
    struct wlan_host_mem_req mem_reqs[0];
} __PACKED;

/* This is the definition from 10.X firmware branch */
struct wmi_10x_service_ready_event {
    uint32_t sw_version;
    uint32_t abi_version;

    /* WMI_PHY_CAPABILITY */
    uint32_t phy_capability;

    /* Maximum number of frag table entries that SW will populate less 1 */
    uint32_t max_frag_entry;
    uint32_t wmi_service_bitmap[16];
    uint32_t num_rf_chains;

    /*
     * The following field is only valid for service type
     * WMI_SERVICE_11AC
     */
    uint32_t ht_cap_info; /* WMI HT Capability */
    uint32_t vht_cap_info; /* VHT capability info field of 802.11ac */
    uint32_t vht_supp_mcs; /* VHT Supported MCS Set field Rx/Tx same */
    uint32_t hw_min_tx_power;
    uint32_t hw_max_tx_power;

    struct hal_reg_capabilities hal_reg_capabilities;

    uint32_t sys_cap_info;
    uint32_t min_pkt_size_enable; /* Enterprise mode short pkt enable */

    /*
     * request to host to allocate a chuck of memory and pss it down to FW
     * via WM_INIT. FW uses this as FW extesnsion memory for saving its
     * data structures. Only valid for low latency interfaces like PCIE
     * where FW can access this memory directly (or) by DMA.
     */
    uint32_t num_mem_reqs;

    struct wlan_host_mem_req mem_reqs[0];
} __PACKED;

#define WMI_SERVICE_READY_TIMEOUT (ZX_SEC(5))
#define WMI_UNIFIED_READY_TIMEOUT (ZX_SEC(5))

struct wmi_ready_event {
    uint32_t sw_version;
    uint32_t abi_version;
    struct wmi_mac_addr mac_addr;
    uint32_t status;
} __PACKED;

struct wmi_resource_config {
    /* number of virtual devices (VAPs) to support */
    uint32_t num_vdevs;

    /* number of peer nodes to support */
    uint32_t num_peers;

    /*
     * In offload mode target supports features like WOW, chatter and
     * other protocol offloads. In order to support them some
     * functionalities like reorder buffering, PN checking need to be
     * done in target. This determines maximum number of peers supported
     * by target in offload mode
     */
    uint32_t num_offload_peers;

    /* For target-based RX reordering */
    uint32_t num_offload_reorder_bufs;

    /* number of keys per peer */
    uint32_t num_peer_keys;

    /* total number of TX/RX data TIDs */
    uint32_t num_tids;

    /*
     * max skid for resolving hash collisions
     *
     *   The address search table is sparse, so that if two MAC addresses
     *   result in the same hash value, the second of these conflicting
     *   entries can slide to the next index in the address search table,
     *   and use it, if it is unoccupied.  This ast_skid_limit parameter
     *   specifies the upper bound on how many subsequent indices to search
     *   over to find an unoccupied space.
     */
    uint32_t ast_skid_limit;

    /*
     * the nominal chain mask for transmit
     *
     *   The chain mask may be modified dynamically, e.g. to operate AP
     *   tx with a reduced number of chains if no clients are associated.
     *   This configuration parameter specifies the nominal chain-mask that
     *   should be used when not operating with a reduced set of tx chains.
     */
    uint32_t tx_chain_mask;

    /*
     * the nominal chain mask for receive
     *
     *   The chain mask may be modified dynamically, e.g. for a client
     *   to use a reduced number of chains for receive if the traffic to
     *   the client is low enough that it doesn't require downlink MIMO
     *   or antenna diversity.
     *   This configuration parameter specifies the nominal chain-mask that
     *   should be used when not operating with a reduced set of rx chains.
     */
    uint32_t rx_chain_mask;

    /*
     * what rx reorder timeout (ms) to use for the AC
     *
     *   Each WMM access class (voice, video, best-effort, background) will
     *   have its own timeout value to dictate how long to wait for missing
     *   rx MPDUs to arrive before flushing subsequent MPDUs that have
     *   already been received.
     *   This parameter specifies the timeout in milliseconds for each
     *   class.
     */
    uint32_t rx_timeout_pri_vi;
    uint32_t rx_timeout_pri_vo;
    uint32_t rx_timeout_pri_be;
    uint32_t rx_timeout_pri_bk;

    /*
     * what mode the rx should decap packets to
     *
     *   MAC can decap to RAW (no decap), native wifi or Ethernet types
     *   THis setting also determines the default TX behavior, however TX
     *   behavior can be modified on a per VAP basis during VAP init
     */
    uint32_t rx_decap_mode;

    /* what is the maximum number of scan requests that can be queued */
    uint32_t scan_max_pending_reqs;

    /* maximum VDEV that could use BMISS offload */
    uint32_t bmiss_offload_max_vdev;

    /* maximum VDEV that could use offload roaming */
    uint32_t roam_offload_max_vdev;

    /* maximum AP profiles that would push to offload roaming */
    uint32_t roam_offload_max_ap_profiles;

    /*
     * how many groups to use for mcast->ucast conversion
     *
     *   The target's WAL maintains a table to hold information regarding
     *   which peers belong to a given multicast group, so that if
     *   multicast->unicast conversion is enabled, the target can convert
     *   multicast tx frames to a series of unicast tx frames, to each
     *   peer within the multicast group.
         This num_mcast_groups configuration parameter tells the target how
     *   many multicast groups to provide storage for within its multicast
     *   group membership table.
     */
    uint32_t num_mcast_groups;

    /*
     * size to alloc for the mcast membership table
     *
     *   This num_mcast_table_elems configuration parameter tells the
     *   target how many peer elements it needs to provide storage for in
     *   its multicast group membership table.
     *   These multicast group membership table elements are shared by the
     *   multicast groups stored within the table.
     */
    uint32_t num_mcast_table_elems;

    /*
     * whether/how to do multicast->unicast conversion
     *
     *   This configuration parameter specifies whether the target should
     *   perform multicast --> unicast conversion on transmit, and if so,
     *   what to do if it finds no entries in its multicast group
     *   membership table for the multicast IP address in the tx frame.
     *   Configuration value:
     *   0 -> Do not perform multicast to unicast conversion.
     *   1 -> Convert multicast frames to unicast, if the IP multicast
     *        address from the tx frame is found in the multicast group
     *        membership table.  If the IP multicast address is not found,
     *        drop the frame.
     *   2 -> Convert multicast frames to unicast, if the IP multicast
     *        address from the tx frame is found in the multicast group
     *        membership table.  If the IP multicast address is not found,
     *        transmit the frame as multicast.
     */
    uint32_t mcast2ucast_mode;

    /*
     * how much memory to allocate for a tx PPDU dbg log
     *
     *   This parameter controls how much memory the target will allocate
     *   to store a log of tx PPDU meta-information (how large the PPDU
     *   was, when it was sent, whether it was successful, etc.)
     */
    uint32_t tx_dbg_log_size;

    /* how many AST entries to be allocated for WDS */
    uint32_t num_wds_entries;

    /*
     * MAC DMA burst size, e.g., For target PCI limit can be
     * 0 -default, 1 256B
     */
    uint32_t dma_burst_size;

    /*
     * Fixed delimiters to be inserted after every MPDU to
     * account for interface latency to avoid underrun.
     */
    uint32_t mac_aggr_delim;

    /*
     *   determine whether target is responsible for detecting duplicate
     *   non-aggregate MPDU and timing out stale fragments.
     *
     *   A-MPDU reordering is always performed on the target.
     *
     *   0: target responsible for frag timeout and dup checking
     *   1: host responsible for frag timeout and dup checking
     */
    uint32_t rx_skip_defrag_timeout_dup_detection_check;

    /*
     * Configuration for VoW :
     * No of Video Nodes to be supported
     * and Max no of descriptors for each Video link (node).
     */
    uint32_t vow_config;

    /* maximum VDEV that could use GTK offload */
    uint32_t gtk_offload_max_vdev;

    /* Number of msdu descriptors target should use */
    uint32_t num_msdu_desc;

    /*
     * Max. number of Tx fragments per MSDU
     *  This parameter controls the max number of Tx fragments per MSDU.
     *  This is sent by the target as part of the WMI_SERVICE_READY event
     *  and is overridden by the OS shim as required.
     */
    uint32_t max_frag_entries;
} __PACKED;

struct wmi_resource_config_10x {
    /* number of virtual devices (VAPs) to support */
    uint32_t num_vdevs;

    /* number of peer nodes to support */
    uint32_t num_peers;

    /* number of keys per peer */
    uint32_t num_peer_keys;

    /* total number of TX/RX data TIDs */
    uint32_t num_tids;

    /*
     * max skid for resolving hash collisions
     *
     *   The address search table is sparse, so that if two MAC addresses
     *   result in the same hash value, the second of these conflicting
     *   entries can slide to the next index in the address search table,
     *   and use it, if it is unoccupied.  This ast_skid_limit parameter
     *   specifies the upper bound on how many subsequent indices to search
     *   over to find an unoccupied space.
     */
    uint32_t ast_skid_limit;

    /*
     * the nominal chain mask for transmit
     *
     *   The chain mask may be modified dynamically, e.g. to operate AP
     *   tx with a reduced number of chains if no clients are associated.
     *   This configuration parameter specifies the nominal chain-mask that
     *   should be used when not operating with a reduced set of tx chains.
     */
    uint32_t tx_chain_mask;

    /*
     * the nominal chain mask for receive
     *
     *   The chain mask may be modified dynamically, e.g. for a client
     *   to use a reduced number of chains for receive if the traffic to
     *   the client is low enough that it doesn't require downlink MIMO
     *   or antenna diversity.
     *   This configuration parameter specifies the nominal chain-mask that
     *   should be used when not operating with a reduced set of rx chains.
     */
    uint32_t rx_chain_mask;

    /*
     * what rx reorder timeout (ms) to use for the AC
     *
     *   Each WMM access class (voice, video, best-effort, background) will
     *   have its own timeout value to dictate how long to wait for missing
     *   rx MPDUs to arrive before flushing subsequent MPDUs that have
     *   already been received.
     *   This parameter specifies the timeout in milliseconds for each
     *   class.
     */
    uint32_t rx_timeout_pri_vi;
    uint32_t rx_timeout_pri_vo;
    uint32_t rx_timeout_pri_be;
    uint32_t rx_timeout_pri_bk;

    /*
     * what mode the rx should decap packets to
     *
     *   MAC can decap to RAW (no decap), native wifi or Ethernet types
     *   THis setting also determines the default TX behavior, however TX
     *   behavior can be modified on a per VAP basis during VAP init
     */
    uint32_t rx_decap_mode;

    /* what is the maximum number of scan requests that can be queued */
    uint32_t scan_max_pending_reqs;

    /* maximum VDEV that could use BMISS offload */
    uint32_t bmiss_offload_max_vdev;

    /* maximum VDEV that could use offload roaming */
    uint32_t roam_offload_max_vdev;

    /* maximum AP profiles that would push to offload roaming */
    uint32_t roam_offload_max_ap_profiles;

    /*
     * how many groups to use for mcast->ucast conversion
     *
     *   The target's WAL maintains a table to hold information regarding
     *   which peers belong to a given multicast group, so that if
     *   multicast->unicast conversion is enabled, the target can convert
     *   multicast tx frames to a series of unicast tx frames, to each
     *   peer within the multicast group.
         This num_mcast_groups configuration parameter tells the target how
     *   many multicast groups to provide storage for within its multicast
     *   group membership table.
     */
    uint32_t num_mcast_groups;

    /*
     * size to alloc for the mcast membership table
     *
     *   This num_mcast_table_elems configuration parameter tells the
     *   target how many peer elements it needs to provide storage for in
     *   its multicast group membership table.
     *   These multicast group membership table elements are shared by the
     *   multicast groups stored within the table.
     */
    uint32_t num_mcast_table_elems;

    /*
     * whether/how to do multicast->unicast conversion
     *
     *   This configuration parameter specifies whether the target should
     *   perform multicast --> unicast conversion on transmit, and if so,
     *   what to do if it finds no entries in its multicast group
     *   membership table for the multicast IP address in the tx frame.
     *   Configuration value:
     *   0 -> Do not perform multicast to unicast conversion.
     *   1 -> Convert multicast frames to unicast, if the IP multicast
     *        address from the tx frame is found in the multicast group
     *        membership table.  If the IP multicast address is not found,
     *        drop the frame.
     *   2 -> Convert multicast frames to unicast, if the IP multicast
     *        address from the tx frame is found in the multicast group
     *        membership table.  If the IP multicast address is not found,
     *        transmit the frame as multicast.
     */
    uint32_t mcast2ucast_mode;

    /*
     * how much memory to allocate for a tx PPDU dbg log
     *
     *   This parameter controls how much memory the target will allocate
     *   to store a log of tx PPDU meta-information (how large the PPDU
     *   was, when it was sent, whether it was successful, etc.)
     */
    uint32_t tx_dbg_log_size;

    /* how many AST entries to be allocated for WDS */
    uint32_t num_wds_entries;

    /*
     * MAC DMA burst size, e.g., For target PCI limit can be
     * 0 -default, 1 256B
     */
    uint32_t dma_burst_size;

    /*
     * Fixed delimiters to be inserted after every MPDU to
     * account for interface latency to avoid underrun.
     */
    uint32_t mac_aggr_delim;

    /*
     *   determine whether target is responsible for detecting duplicate
     *   non-aggregate MPDU and timing out stale fragments.
     *
     *   A-MPDU reordering is always performed on the target.
     *
     *   0: target responsible for frag timeout and dup checking
     *   1: host responsible for frag timeout and dup checking
     */
    uint32_t rx_skip_defrag_timeout_dup_detection_check;

    /*
     * Configuration for VoW :
     * No of Video Nodes to be supported
     * and Max no of descriptors for each Video link (node).
     */
    uint32_t vow_config;

    /* Number of msdu descriptors target should use */
    uint32_t num_msdu_desc;

    /*
     * Max. number of Tx fragments per MSDU
     *  This parameter controls the max number of Tx fragments per MSDU.
     *  This is sent by the target as part of the WMI_SERVICE_READY event
     *  and is overridden by the OS shim as required.
     */
    uint32_t max_frag_entries;
} __PACKED;

enum wmi_10_2_feature_mask {
    WMI_10_2_RX_BATCH_MODE = (1 << 0),
    WMI_10_2_ATF_CONFIG    = (1 << 1),
    WMI_10_2_COEX_GPIO     = (1 << 3),
    WMI_10_2_BSS_CHAN_INFO = (1 << 6),
    WMI_10_2_PEER_STATS    = (1 << 7),
};

struct wmi_resource_config_10_2 {
    struct wmi_resource_config_10x common;
    uint32_t max_peer_ext_stats;
    uint32_t smart_ant_cap; /* 0-disable, 1-enable */
    uint32_t bk_min_free;
    uint32_t be_min_free;
    uint32_t vi_min_free;
    uint32_t vo_min_free;
    uint32_t feature_mask;
} __PACKED;

#define NUM_UNITS_IS_NUM_VDEVS         (1 << 0)
#define NUM_UNITS_IS_NUM_PEERS         (1 << 1)
#define NUM_UNITS_IS_NUM_ACTIVE_PEERS  (1 << 2)

struct wmi_resource_config_10_4 {
    /* Number of virtual devices (VAPs) to support */
    uint32_t num_vdevs;

    /* Number of peer nodes to support */
    uint32_t num_peers;

    /* Number of active peer nodes to support */
    uint32_t num_active_peers;

    /* In offload mode, target supports features like WOW, chatter and other
     * protocol offloads. In order to support them some functionalities like
     * reorder buffering, PN checking need to be done in target.
     * This determines maximum number of peers supported by target in
     * offload mode.
     */
    uint32_t num_offload_peers;

    /* Number of reorder buffers available for doing target based reorder
     * Rx reorder buffering
     */
    uint32_t num_offload_reorder_buffs;

    /* Number of keys per peer */
    uint32_t num_peer_keys;

    /* Total number of TX/RX data TIDs */
    uint32_t num_tids;

    /* Max skid for resolving hash collisions.
     * The address search table is sparse, so that if two MAC addresses
     * result in the same hash value, the second of these conflicting
     * entries can slide to the next index in the address search table,
     * and use it, if it is unoccupied.  This ast_skid_limit parameter
     * specifies the upper bound on how many subsequent indices to search
     * over to find an unoccupied space.
     */
    uint32_t ast_skid_limit;

    /* The nominal chain mask for transmit.
     * The chain mask may be modified dynamically, e.g. to operate AP tx
     * with a reduced number of chains if no clients are associated.
     * This configuration parameter specifies the nominal chain-mask that
     * should be used when not operating with a reduced set of tx chains.
     */
    uint32_t tx_chain_mask;

    /* The nominal chain mask for receive.
     * The chain mask may be modified dynamically, e.g. for a client to use
     * a reduced number of chains for receive if the traffic to the client
     * is low enough that it doesn't require downlink MIMO or antenna
     * diversity. This configuration parameter specifies the nominal
     * chain-mask that should be used when not operating with a reduced
     * set of rx chains.
     */
    uint32_t rx_chain_mask;

    /* What rx reorder timeout (ms) to use for the AC.
     * Each WMM access class (voice, video, best-effort, background) will
     * have its own timeout value to dictate how long to wait for missing
     * rx MPDUs to arrive before flushing subsequent MPDUs that have already
     * been received. This parameter specifies the timeout in milliseconds
     * for each class.
     */
    uint32_t rx_timeout_pri[4];

    /* What mode the rx should decap packets to.
     * MAC can decap to RAW (no decap), native wifi or Ethernet types.
     * This setting also determines the default TX behavior, however TX
     * behavior can be modified on a per VAP basis during VAP init
     */
    uint32_t rx_decap_mode;

    uint32_t scan_max_pending_req;

    uint32_t bmiss_offload_max_vdev;

    uint32_t roam_offload_max_vdev;

    uint32_t roam_offload_max_ap_profiles;

    /* How many groups to use for mcast->ucast conversion.
     * The target's WAL maintains a table to hold information regarding
     * which peers belong to a given multicast group, so that if
     * multicast->unicast conversion is enabled, the target can convert
     * multicast tx frames to a series of unicast tx frames, to each peer
     * within the multicast group. This num_mcast_groups configuration
     * parameter tells the target how many multicast groups to provide
     * storage for within its multicast group membership table.
     */
    uint32_t num_mcast_groups;

    /* Size to alloc for the mcast membership table.
     * This num_mcast_table_elems configuration parameter tells the target
     * how many peer elements it needs to provide storage for in its
     * multicast group membership table. These multicast group membership
     * table elements are shared by the multicast groups stored within
     * the table.
     */
    uint32_t num_mcast_table_elems;

    /* Whether/how to do multicast->unicast conversion.
     * This configuration parameter specifies whether the target should
     * perform multicast --> unicast conversion on transmit, and if so,
     * what to do if it finds no entries in its multicast group membership
     * table for the multicast IP address in the tx frame.
     * Configuration value:
     * 0 -> Do not perform multicast to unicast conversion.
     * 1 -> Convert multicast frames to unicast, if the IP multicast address
     *      from the tx frame is found in the multicast group membership
     *      table.  If the IP multicast address is not found, drop the frame
     * 2 -> Convert multicast frames to unicast, if the IP multicast address
     *      from the tx frame is found in the multicast group membership
     *      table.  If the IP multicast address is not found, transmit the
     *      frame as multicast.
     */
    uint32_t mcast2ucast_mode;

    /* How much memory to allocate for a tx PPDU dbg log.
     * This parameter controls how much memory the target will allocate to
     * store a log of tx PPDU meta-information (how large the PPDU was,
     * when it was sent, whether it was successful, etc.)
     */
    uint32_t tx_dbg_log_size;

    /* How many AST entries to be allocated for WDS */
    uint32_t num_wds_entries;

    /* MAC DMA burst size. 0 -default, 1 -256B */
    uint32_t dma_burst_size;

    /* Fixed delimiters to be inserted after every MPDU to account for
     * interface latency to avoid underrun.
     */
    uint32_t mac_aggr_delim;

    /* Determine whether target is responsible for detecting duplicate
     * non-aggregate MPDU and timing out stale fragments. A-MPDU reordering
     * is always performed on the target.
     *
     * 0: target responsible for frag timeout and dup checking
     * 1: host responsible for frag timeout and dup checking
     */
    uint32_t rx_skip_defrag_timeout_dup_detection_check;

    /* Configuration for VoW : No of Video nodes to be supported and max
     * no of descriptors for each video link (node).
     */
    uint32_t vow_config;

    /* Maximum vdev that could use gtk offload */
    uint32_t gtk_offload_max_vdev;

    /* Number of msdu descriptors target should use */
    uint32_t num_msdu_desc;

    /* Max number of tx fragments per MSDU.
     * This parameter controls the max number of tx fragments per MSDU.
     * This will passed by target as part of the WMI_SERVICE_READY event
     * and is overridden by the OS shim as required.
     */
    uint32_t max_frag_entries;

    /* Max number of extended peer stats.
     * This parameter controls the max number of peers for which extended
     * statistics are supported by target
     */
    uint32_t max_peer_ext_stats;

    /* Smart antenna capabilities information.
     * 1 - Smart antenna is enabled
     * 0 - Smart antenna is disabled
     * In future this can contain smart antenna specific capabilities.
     */
    uint32_t smart_ant_cap;

    /* User can configure the buffers allocated for each AC (BE, BK, VI, VO)
     * during init.
     */
    uint32_t bk_minfree;
    uint32_t be_minfree;
    uint32_t vi_minfree;
    uint32_t vo_minfree;

    /* Rx batch mode capability.
     * 1 - Rx batch mode enabled
     * 0 - Rx batch mode disabled
     */
    uint32_t rx_batchmode;

    /* Thermal throttling capability.
     * 1 - Capable of thermal throttling
     * 0 - Not capable of thermal throttling
     */
    uint32_t tt_support;

    /* ATF configuration.
     * 1  - Enable ATF
     * 0  - Disable ATF
     */
    uint32_t atf_config;

    /* Configure padding to manage IP header un-alignment
     * 1  - Enable padding
     * 0  - Disable padding
     */
    uint32_t iphdr_pad_config;

    /* qwrap configuration (bits 15-0)
     * 1  - This is qwrap configuration
     * 0  - This is not qwrap
     *
     * Bits 31-16 is alloc_frag_desc_for_data_pkt (1 enables, 0 disables)
     * In order to get ack-RSSI reporting and to specify the tx-rate for
     * individual frames, this option must be enabled.  This uses an extra
     * 4 bytes per tx-msdu descriptor, so don't enable it unless you need it.
     */
    uint32_t qwrap_config;
} __PACKED;

/**
 * enum wmi_10_4_feature_mask - WMI 10.4 feature enable/disable flags
 * @WMI_10_4_LTEU_SUPPORT: LTEU config
 * @WMI_10_4_COEX_GPIO_SUPPORT: COEX GPIO config
 * @WMI_10_4_AUX_RADIO_SPECTRAL_INTF: AUX Radio Enhancement for spectral scan
 * @WMI_10_4_AUX_RADIO_CHAN_LOAD_INTF: AUX Radio Enhancement for chan load scan
 * @WMI_10_4_BSS_CHANNEL_INFO_64: BSS channel info stats
 * @WMI_10_4_PEER_STATS: Per station stats
 */
enum wmi_10_4_feature_mask {
    WMI_10_4_LTEU_SUPPORT               = (1 << 0),
    WMI_10_4_COEX_GPIO_SUPPORT          = (1 << 1),
    WMI_10_4_AUX_RADIO_SPECTRAL_INTF    = (1 << 2),
    WMI_10_4_AUX_RADIO_CHAN_LOAD_INTF   = (1 << 3),
    WMI_10_4_BSS_CHANNEL_INFO_64        = (1 << 4),
    WMI_10_4_PEER_STATS                 = (1 << 5),
};

struct wmi_ext_resource_config_10_4_cmd {
    /* contains enum wmi_host_platform_type */
    uint32_t host_platform_config;
    /* see enum wmi_10_4_feature_mask */
    uint32_t fw_feature_bitmap;
};

/* strucutre describing host memory chunk. */
struct host_memory_chunk {
    /* id of the request that is passed up in service ready */
    uint32_t req_id;
    /* the physical address the memory chunk */
    uint32_t ptr;
    /* size of the chunk */
    uint32_t size;
} __PACKED;

struct wmi_host_mem_chunks {
    uint32_t count;
    /* some fw revisions require at least 1 chunk regardless of count */
    struct host_memory_chunk items[1];
} __PACKED;

struct wmi_init_cmd {
    struct wmi_resource_config resource_config;
    struct wmi_host_mem_chunks mem_chunks;
} __PACKED;

/* _10x structure is from 10.X FW API */
struct wmi_init_cmd_10x {
    struct wmi_resource_config_10x resource_config;
    struct wmi_host_mem_chunks mem_chunks;
} __PACKED;

struct wmi_init_cmd_10_2 {
    struct wmi_resource_config_10_2 resource_config;
    struct wmi_host_mem_chunks mem_chunks;
} __PACKED;

struct wmi_init_cmd_10_4 {
    struct wmi_resource_config_10_4 resource_config;
    struct wmi_host_mem_chunks mem_chunks;
} __PACKED;

struct wmi_chan_list_entry {
    uint16_t freq;
    uint8_t phy_mode; /* valid for 10.2 only */
    uint8_t reserved;
} __PACKED;

/* TLV for channel list */
struct wmi_chan_list {
    uint32_t tag; /* WMI_CHAN_LIST_TAG */
    uint32_t num_chan;
    struct wmi_chan_list_entry channel_list[0];
} __PACKED;

struct wmi_bssid_list {
    uint32_t tag; /* WMI_BSSID_LIST_TAG */
    uint32_t num_bssid;
    struct wmi_mac_addr bssid_list[0];
} __PACKED;

struct wmi_ie_data {
    uint32_t tag; /* WMI_IE_TAG */
    uint32_t ie_len;
    uint8_t ie_data[0];
} __PACKED;

struct wmi_ssid {
    uint32_t ssid_len;
    uint8_t ssid[32];
} __PACKED;

struct wmi_ssid_list {
    uint32_t tag; /* WMI_SSID_LIST_TAG */
    uint32_t num_ssids;
    struct wmi_ssid ssids[0];
} __PACKED;

/* prefix used by scan requestor ids on the host */
#define WMI_HOST_SCAN_REQUESTOR_ID_PREFIX 0xA000

/* prefix used by scan request ids generated on the host */
/* host cycles through the lower 12 bits to generate ids */
#define WMI_HOST_SCAN_REQ_ID_PREFIX 0xA000

#define WLAN_SCAN_PARAMS_MAX_SSID    16
#define WLAN_SCAN_PARAMS_MAX_BSSID   4
#define WLAN_SCAN_PARAMS_MAX_IE_LEN  256

/* Values lower than this may be refused by some firmware revisions with a scan
 * completion with a timedout reason.
 */
#define WMI_SCAN_CHAN_MIN_TIME_MSEC 40

/* Scan priority numbers must be sequential, starting with 0 */
enum wmi_scan_priority {
    WMI_SCAN_PRIORITY_VERY_LOW = 0,
    WMI_SCAN_PRIORITY_LOW,
    WMI_SCAN_PRIORITY_MEDIUM,
    WMI_SCAN_PRIORITY_HIGH,
    WMI_SCAN_PRIORITY_VERY_HIGH,
    WMI_SCAN_PRIORITY_COUNT   /* number of priorities supported */
};

struct wmi_start_scan_common {
    /* Scan ID */
    uint32_t scan_id;
    /* Scan requestor ID */
    uint32_t scan_req_id;
    /* VDEV id(interface) that is requesting scan */
    uint32_t vdev_id;
    /* Scan Priority, input to scan scheduler */
    uint32_t scan_priority;
    /* Scan events subscription */
    uint32_t notify_scan_events;
    /* dwell time in msec on active channels */
    uint32_t dwell_time_active;
    /* dwell time in msec on passive channels */
    uint32_t dwell_time_passive;
    /*
     * min time in msec on the BSS channel,only valid if atleast one
     * VDEV is active
     */
    uint32_t min_rest_time;
    /*
     * max rest time in msec on the BSS channel,only valid if at least
     * one VDEV is active
     */
    /*
     * the scanner will rest on the bss channel at least min_rest_time
     * after min_rest_time the scanner will start checking for tx/rx
     * activity on all VDEVs. if there is no activity the scanner will
     * switch to off channel. if there is activity the scanner will let
     * the radio on the bss channel until max_rest_time expires.at
     * max_rest_time scanner will switch to off channel irrespective of
     * activity. activity is determined by the idle_time parameter.
     */
    uint32_t max_rest_time;
    /*
     * time before sending next set of probe requests.
     * The scanner keeps repeating probe requests transmission with
     * period specified by repeat_probe_time.
     * The number of probe requests specified depends on the ssid_list
     * and bssid_list
     */
    uint32_t repeat_probe_time;
    /* time in msec between 2 consequetive probe requests with in a set. */
    uint32_t probe_spacing_time;
    /*
     * data inactivity time in msec on bss channel that will be used by
     * scanner for measuring the inactivity.
     */
    uint32_t idle_time;
    /* maximum time in msec allowed for scan  */
    uint32_t max_scan_time;
    /*
     * delay in msec before sending first probe request after switching
     * to a channel
     */
    uint32_t probe_delay;
    /* Scan control flags */
    uint32_t scan_ctrl_flags;
} __PACKED;

struct wmi_start_scan_tlvs {
    /* TLV parameters. These includes channel list, ssid list, bssid list,
     * extra ies.
     */
    uint8_t tlvs[0];
} __PACKED;

struct wmi_start_scan_cmd {
    struct wmi_start_scan_common common;
    uint32_t burst_duration_ms;
    struct wmi_start_scan_tlvs tlvs;
} __PACKED;

/* This is the definition from 10.X firmware branch */
struct wmi_10x_start_scan_cmd {
    struct wmi_start_scan_common common;
    struct wmi_start_scan_tlvs tlvs;
} __PACKED;

struct wmi_ssid_arg {
    int len;
    const uint8_t* ssid;
};

struct wmi_bssid_arg {
    const uint8_t* bssid;
};

struct wmi_start_scan_arg {
    uint32_t scan_id;
    uint32_t scan_req_id;
    uint32_t vdev_id;
    uint32_t scan_priority;
    uint32_t notify_scan_events;
    uint32_t dwell_time_active;
    uint32_t dwell_time_passive;
    uint32_t min_rest_time;
    uint32_t max_rest_time;
    uint32_t repeat_probe_time;
    uint32_t probe_spacing_time;
    uint32_t idle_time;
    uint32_t max_scan_time;
    uint32_t probe_delay;
    uint32_t scan_ctrl_flags;
    uint32_t burst_duration_ms;

    uint32_t ie_len;
    uint32_t n_channels;
    uint32_t n_ssids;
    uint32_t n_bssids;

    uint8_t ie[WLAN_SCAN_PARAMS_MAX_IE_LEN];
    uint16_t channels[64];
    struct wmi_ssid_arg ssids[WLAN_SCAN_PARAMS_MAX_SSID];
    struct wmi_bssid_arg bssids[WLAN_SCAN_PARAMS_MAX_BSSID];
};

/* scan control flags */

/* passively scan all channels including active channels */
#define WMI_SCAN_FLAG_PASSIVE        0x1
/* add wild card ssid probe request even though ssid_list is specified. */
#define WMI_SCAN_ADD_BCAST_PROBE_REQ 0x2
/* add cck rates to rates/xrate ie for the generated probe request */
#define WMI_SCAN_ADD_CCK_RATES 0x4
/* add ofdm rates to rates/xrate ie for the generated probe request */
#define WMI_SCAN_ADD_OFDM_RATES 0x8
/* To enable indication of Chan load and Noise floor to host */
#define WMI_SCAN_CHAN_STAT_EVENT 0x10
/* Filter Probe request frames  */
#define WMI_SCAN_FILTER_PROBE_REQ 0x20
/* When set, DFS channels will not be scanned */
#define WMI_SCAN_BYPASS_DFS_CHN 0x40
/* Different FW scan engine may choose to bail out on errors.
 * Allow the driver to have influence over that.
 */
#define WMI_SCAN_CONTINUE_ON_ERROR 0x80

/* WMI_SCAN_CLASS_MASK must be the same value as IEEE80211_SCAN_CLASS_MASK */
#define WMI_SCAN_CLASS_MASK 0xFF000000

enum wmi_stop_scan_type {
    WMI_SCAN_STOP_ONE   = 0x00000000, /* stop by scan_id */
    WMI_SCAN_STOP_VDEV_ALL  = 0x01000000, /* stop by vdev_id */
    WMI_SCAN_STOP_ALL   = 0x04000000, /* stop all scans */
};

struct wmi_stop_scan_cmd {
    uint32_t scan_req_id;
    uint32_t scan_id;
    uint32_t req_type;
    uint32_t vdev_id;
} __PACKED;

struct wmi_stop_scan_arg {
    uint32_t req_id;
    enum wmi_stop_scan_type req_type;
    union {
        uint32_t scan_id;
        uint32_t vdev_id;
    } u;
};

struct wmi_scan_chan_list_cmd {
    uint32_t num_scan_chans;
    struct wmi_channel chan_info[0];
} __PACKED;

struct wmi_scan_chan_list_arg {
    uint32_t n_channels;
    struct wmi_channel_arg* channels;
};

enum wmi_bss_filter {
    WMI_BSS_FILTER_NONE = 0,        /* no beacons forwarded */
    WMI_BSS_FILTER_ALL,             /* all beacons forwarded */
    WMI_BSS_FILTER_PROFILE,         /* only beacons matching profile */
    WMI_BSS_FILTER_ALL_BUT_PROFILE, /* all but beacons matching profile */
    WMI_BSS_FILTER_CURRENT_BSS,     /* only beacons matching current BSS */
    WMI_BSS_FILTER_ALL_BUT_BSS,     /* all but beacons matching BSS */
    WMI_BSS_FILTER_PROBED_SSID,     /* beacons matching probed ssid */
    WMI_BSS_FILTER_LAST_BSS,        /* marker only */
};

enum wmi_scan_event_type {
    WMI_SCAN_EVENT_STARTED              = (1 << 0),
    WMI_SCAN_EVENT_COMPLETED            = (1 << 1),
    WMI_SCAN_EVENT_BSS_CHANNEL          = (1 << 2),
    WMI_SCAN_EVENT_FOREIGN_CHANNEL      = (1 << 3),
    WMI_SCAN_EVENT_DEQUEUED             = (1 << 4),
    /* possibly by high-prio scan */
    WMI_SCAN_EVENT_PREEMPTED            = (1 << 5),
    WMI_SCAN_EVENT_START_FAILED         = (1 << 6),
    WMI_SCAN_EVENT_RESTARTED            = (1 << 7),
    WMI_SCAN_EVENT_FOREIGN_CHANNEL_EXIT = (1 << 8),
    WMI_SCAN_EVENT_MAX                  = (1 << 15),
};

enum wmi_scan_completion_reason {
    WMI_SCAN_REASON_COMPLETED,
    WMI_SCAN_REASON_CANCELLED,
    WMI_SCAN_REASON_PREEMPTED,
    WMI_SCAN_REASON_TIMEDOUT,
    WMI_SCAN_REASON_INTERNAL_FAILURE,
    WMI_SCAN_REASON_MAX,
};

struct wmi_scan_event {
    uint32_t event_type; /* %WMI_SCAN_EVENT_ */
    uint32_t reason; /* %WMI_SCAN_REASON_ */
    uint32_t channel_freq; /* only valid for WMI_SCAN_EVENT_FOREIGN_CHANNEL */
    uint32_t scan_req_id;
    uint32_t scan_id;
    uint32_t vdev_id;
} __PACKED;

/*
 * This defines how much headroom is kept in the
 * receive frame between the descriptor and the
 * payload, in order for the WMI PHY error and
 * management handler to insert header contents.
 *
 * This is in bytes.
 */
#define WMI_MGMT_RX_HDR_HEADROOM    52

/*
 * This event will be used for sending scan results
 * as well as rx mgmt frames to the host. The rx buffer
 * will be sent as part of this WMI event. It would be a
 * good idea to pass all the fields in the RX status
 * descriptor up to the host.
 */
struct wmi_mgmt_rx_hdr_v1 {
    uint32_t channel;
    uint32_t snr;
    uint32_t rate;
    uint32_t phy_mode;
    uint32_t buf_len;
    uint32_t status; /* %WMI_RX_STATUS_ */
} __PACKED;

struct wmi_mgmt_rx_hdr_v2 {
    struct wmi_mgmt_rx_hdr_v1 v1;
    uint32_t rssi_ctl[4];
} __PACKED;

struct wmi_mgmt_rx_event_v1 {
    struct wmi_mgmt_rx_hdr_v1 hdr;
    uint8_t buf[0];
} __PACKED;

struct wmi_mgmt_rx_event_v2 {
    struct wmi_mgmt_rx_hdr_v2 hdr;
    uint8_t buf[0];
} __PACKED;

struct wmi_10_4_mgmt_rx_hdr {
    uint32_t channel;
    uint32_t snr;
    uint8_t rssi_ctl[4];
    uint32_t rate;
    uint32_t phy_mode;
    uint32_t buf_len;
    uint32_t status;
} __PACKED;

struct wmi_10_4_mgmt_rx_event {
    struct wmi_10_4_mgmt_rx_hdr hdr;
    uint8_t buf[0];
} __PACKED;

struct wmi_mgmt_rx_ext_info {
    uint64_t rx_mac_timestamp;
} __PACKED __ALIGNED(4);

#define WMI_RX_STATUS_OK                        0x00
#define WMI_RX_STATUS_ERR_CRC                   0x01
#define WMI_RX_STATUS_ERR_DECRYPT               0x08
#define WMI_RX_STATUS_ERR_MIC                   0x10
#define WMI_RX_STATUS_ERR_KEY_CACHE_MISS        0x20
/* Extension data at the end of mgmt frame */
#define WMI_RX_STATUS_EXT_INFO                  0x40

#define PHY_ERROR_GEN_SPECTRAL_SCAN             0x26
#define PHY_ERROR_GEN_FALSE_RADAR_EXT           0x24
#define PHY_ERROR_GEN_RADAR                     0x05

#define PHY_ERROR_10_4_RADAR_MASK               0x4
#define PHY_ERROR_10_4_SPECTRAL_SCAN_MASK       0x4000000

enum phy_err_type {
    PHY_ERROR_UNKNOWN,
    PHY_ERROR_SPECTRAL_SCAN,
    PHY_ERROR_FALSE_RADAR_EXT,
    PHY_ERROR_RADAR
};

struct wmi_phyerr {
    uint32_t tsf_timestamp;
    uint16_t freq1;
    uint16_t freq2;
    uint8_t rssi_combined;
    uint8_t chan_width_mhz;
    uint8_t phy_err_code;
    uint8_t rsvd0;
    uint32_t rssi_chains[4];
    uint16_t nf_chains[4];
    uint32_t buf_len;
    uint8_t buf[0];
} __PACKED;

struct wmi_phyerr_event {
    uint32_t num_phyerrs;
    uint32_t tsf_l32;
    uint32_t tsf_u32;
    struct wmi_phyerr phyerrs[0];
} __PACKED;

struct wmi_10_4_phyerr_event {
    uint32_t tsf_l32;
    uint32_t tsf_u32;
    uint16_t freq1;
    uint16_t freq2;
    uint8_t rssi_combined;
    uint8_t chan_width_mhz;
    uint8_t phy_err_code;
    uint8_t rsvd0;
    uint32_t rssi_chains[4];
    uint16_t nf_chains[4];
    uint32_t phy_err_mask[2];
    uint32_t tsf_timestamp;
    uint32_t buf_len;
    uint8_t buf[0];
} __PACKED;

#define PHYERR_TLV_SIG              0xBB
#define PHYERR_TLV_TAG_SEARCH_FFT_REPORT    0xFB
#define PHYERR_TLV_TAG_RADAR_PULSE_SUMMARY  0xF8
#define PHYERR_TLV_TAG_SPECTRAL_SUMMARY_REPORT  0xF9

struct phyerr_radar_report {
    uint32_t reg0; /* RADAR_REPORT_REG0_* */
    uint32_t reg1; /* RADAR_REPORT_REG1_* */
} __PACKED;

#define RADAR_REPORT_REG0_PULSE_IS_CHIRP_MASK       0x80000000
#define RADAR_REPORT_REG0_PULSE_IS_CHIRP_LSB        31

#define RADAR_REPORT_REG0_PULSE_IS_MAX_WIDTH_MASK   0x40000000
#define RADAR_REPORT_REG0_PULSE_IS_MAX_WIDTH_LSB    30

#define RADAR_REPORT_REG0_AGC_TOTAL_GAIN_MASK       0x3FF00000
#define RADAR_REPORT_REG0_AGC_TOTAL_GAIN_LSB        20

#define RADAR_REPORT_REG0_PULSE_DELTA_DIFF_MASK     0x000F0000
#define RADAR_REPORT_REG0_PULSE_DELTA_DIFF_LSB      16

#define RADAR_REPORT_REG0_PULSE_DELTA_PEAK_MASK     0x0000FC00
#define RADAR_REPORT_REG0_PULSE_DELTA_PEAK_LSB      10

#define RADAR_REPORT_REG0_PULSE_SIDX_MASK       0x000003FF
#define RADAR_REPORT_REG0_PULSE_SIDX_LSB        0

#define RADAR_REPORT_REG1_PULSE_SRCH_FFT_VALID_MASK 0x80000000
#define RADAR_REPORT_REG1_PULSE_SRCH_FFT_VALID_LSB  31

#define RADAR_REPORT_REG1_PULSE_AGC_MB_GAIN_MASK    0x7F000000
#define RADAR_REPORT_REG1_PULSE_AGC_MB_GAIN_LSB     24

#define RADAR_REPORT_REG1_PULSE_SUBCHAN_MASK_MASK   0x00FF0000
#define RADAR_REPORT_REG1_PULSE_SUBCHAN_MASK_LSB    16

#define RADAR_REPORT_REG1_PULSE_TSF_OFFSET_MASK     0x0000FF00
#define RADAR_REPORT_REG1_PULSE_TSF_OFFSET_LSB      8

#define RADAR_REPORT_REG1_PULSE_DUR_MASK        0x000000FF
#define RADAR_REPORT_REG1_PULSE_DUR_LSB         0

struct phyerr_fft_report {
    uint32_t reg0; /* SEARCH_FFT_REPORT_REG0_ * */
    uint32_t reg1; /* SEARCH_FFT_REPORT_REG1_ * */
} __PACKED;

#define SEARCH_FFT_REPORT_REG0_TOTAL_GAIN_DB_MASK   0xFF800000
#define SEARCH_FFT_REPORT_REG0_TOTAL_GAIN_DB_LSB    23

#define SEARCH_FFT_REPORT_REG0_BASE_PWR_DB_MASK     0x007FC000
#define SEARCH_FFT_REPORT_REG0_BASE_PWR_DB_LSB      14

#define SEARCH_FFT_REPORT_REG0_FFT_CHN_IDX_MASK     0x00003000
#define SEARCH_FFT_REPORT_REG0_FFT_CHN_IDX_LSB      12

#define SEARCH_FFT_REPORT_REG0_PEAK_SIDX_MASK       0x00000FFF
#define SEARCH_FFT_REPORT_REG0_PEAK_SIDX_LSB        0

#define SEARCH_FFT_REPORT_REG1_RELPWR_DB_MASK       0xFC000000
#define SEARCH_FFT_REPORT_REG1_RELPWR_DB_LSB        26

#define SEARCH_FFT_REPORT_REG1_AVGPWR_DB_MASK       0x03FC0000
#define SEARCH_FFT_REPORT_REG1_AVGPWR_DB_LSB        18

#define SEARCH_FFT_REPORT_REG1_PEAK_MAG_MASK        0x0003FF00
#define SEARCH_FFT_REPORT_REG1_PEAK_MAG_LSB     8

#define SEARCH_FFT_REPORT_REG1_NUM_STR_BINS_IB_MASK 0x000000FF
#define SEARCH_FFT_REPORT_REG1_NUM_STR_BINS_IB_LSB  0

struct phyerr_tlv {
    uint16_t len;
    uint8_t tag;
    uint8_t sig;
} __PACKED;

#define DFS_RSSI_POSSIBLY_FALSE         50
#define DFS_PEAK_MAG_THOLD_POSSIBLY_FALSE   40

struct wmi_mgmt_tx_hdr {
    uint32_t vdev_id;
    struct wmi_mac_addr peer_macaddr;
    uint32_t tx_rate;
    uint32_t tx_power;
    uint32_t buf_len;
} __PACKED;

struct wmi_mgmt_tx_cmd {
    struct wmi_mgmt_tx_hdr hdr;
    uint8_t buf[0];
} __PACKED;

struct wmi_echo_event {
    uint32_t value;
} __PACKED;

struct wmi_echo_cmd {
    uint32_t value;
} __PACKED;

struct wmi_pdev_set_regdomain_cmd {
    uint32_t reg_domain;
    uint32_t reg_domain_2G;
    uint32_t reg_domain_5G;
    uint32_t conformance_test_limit_2G;
    uint32_t conformance_test_limit_5G;
} __PACKED;

enum wmi_dfs_region {
    /* Uninitialized dfs domain */
    WMI_UNINIT_DFS_DOMAIN = 0,

    /* FCC3 dfs domain */
    WMI_FCC_DFS_DOMAIN = 1,

    /* ETSI dfs domain */
    WMI_ETSI_DFS_DOMAIN = 2,

    /*Japan dfs domain */
    WMI_MKK4_DFS_DOMAIN = 3,
};

struct wmi_pdev_set_regdomain_cmd_10x {
    uint32_t reg_domain;
    uint32_t reg_domain_2G;
    uint32_t reg_domain_5G;
    uint32_t conformance_test_limit_2G;
    uint32_t conformance_test_limit_5G;

    /* dfs domain from wmi_dfs_region */
    uint32_t dfs_domain;
} __PACKED;

/* Command to set/unset chip in quiet mode */
struct wmi_pdev_set_quiet_cmd {
    /* period in TUs */
    uint32_t period;

    /* duration in TUs */
    uint32_t duration;

    /* offset in TUs */
    uint32_t next_start;

    /* enable/disable */
    uint32_t enabled;
} __PACKED;

/*
 * 802.11g protection mode.
 */
enum ath10k_protmode {
    ATH10K_PROT_NONE     = 0,    /* no protection */
    ATH10K_PROT_CTSONLY  = 1,    /* CTS to self */
    ATH10K_PROT_RTSCTS   = 2,    /* RTS-CTS */
};

enum wmi_rtscts_profile {
    WMI_RTSCTS_FOR_NO_RATESERIES = 0,
    WMI_RTSCTS_FOR_SECOND_RATESERIES,
    WMI_RTSCTS_ACROSS_SW_RETRIES
};

#define WMI_RTSCTS_ENABLED      1
#define WMI_RTSCTS_SET_MASK     0x0f
#define WMI_RTSCTS_SET_LSB      0

#define WMI_RTSCTS_PROFILE_MASK     0xf0
#define WMI_RTSCTS_PROFILE_LSB      4

enum wmi_beacon_gen_mode {
    WMI_BEACON_STAGGERED_MODE = 0,
    WMI_BEACON_BURST_MODE = 1
};

enum wmi_csa_event_ies_present_flag {
    WMI_CSA_IE_PRESENT = 0x00000001,
    WMI_XCSA_IE_PRESENT = 0x00000002,
    WMI_WBW_IE_PRESENT = 0x00000004,
    WMI_CSWARP_IE_PRESENT = 0x00000008,
};

/* wmi CSA receive event from beacon frame */
struct wmi_csa_event {
    uint32_t i_fc_dur;
    /* Bit 0-15: FC */
    /* Bit 16-31: DUR */
    struct wmi_mac_addr i_addr1;
    struct wmi_mac_addr i_addr2;
    uint32_t csa_ie[2];
    uint32_t xcsa_ie[2];
    uint32_t wb_ie[2];
    uint32_t cswarp_ie;
    uint32_t ies_present_flag; /* wmi_csa_event_ies_present_flag */
} __PACKED;

/* the definition of different PDEV parameters */
#define PDEV_DEFAULT_STATS_UPDATE_PERIOD    500
#define VDEV_DEFAULT_STATS_UPDATE_PERIOD    500
#define PEER_DEFAULT_STATS_UPDATE_PERIOD    500

struct wmi_pdev_param_map {
    uint32_t tx_chain_mask;
    uint32_t rx_chain_mask;
    uint32_t txpower_limit2g;
    uint32_t txpower_limit5g;
    uint32_t txpower_scale;
    uint32_t beacon_gen_mode;
    uint32_t beacon_tx_mode;
    uint32_t resmgr_offchan_mode;
    uint32_t protection_mode;
    uint32_t dynamic_bw;
    uint32_t non_agg_sw_retry_th;
    uint32_t agg_sw_retry_th;
    uint32_t sta_kickout_th;
    uint32_t ac_aggrsize_scaling;
    uint32_t ltr_enable;
    uint32_t ltr_ac_latency_be;
    uint32_t ltr_ac_latency_bk;
    uint32_t ltr_ac_latency_vi;
    uint32_t ltr_ac_latency_vo;
    uint32_t ltr_ac_latency_timeout;
    uint32_t ltr_sleep_override;
    uint32_t ltr_rx_override;
    uint32_t ltr_tx_activity_timeout;
    uint32_t l1ss_enable;
    uint32_t dsleep_enable;
    uint32_t pcielp_txbuf_flush;
    uint32_t pcielp_txbuf_watermark;
    uint32_t pcielp_txbuf_tmo_en;
    uint32_t pcielp_txbuf_tmo_value;
    uint32_t pdev_stats_update_period;
    uint32_t vdev_stats_update_period;
    uint32_t peer_stats_update_period;
    uint32_t bcnflt_stats_update_period;
    uint32_t pmf_qos;
    uint32_t arp_ac_override;
    uint32_t dcs;
    uint32_t ani_enable;
    uint32_t ani_poll_period;
    uint32_t ani_listen_period;
    uint32_t ani_ofdm_level;
    uint32_t ani_cck_level;
    uint32_t dyntxchain;
    uint32_t proxy_sta;
    uint32_t idle_ps_config;
    uint32_t power_gating_sleep;
    uint32_t fast_channel_reset;
    uint32_t burst_dur;
    uint32_t burst_enable;
    uint32_t cal_period;
    uint32_t aggr_burst;
    uint32_t rx_decap_mode;
    uint32_t smart_antenna_default_antenna;
    uint32_t igmpmld_override;
    uint32_t igmpmld_tid;
    uint32_t antenna_gain;
    uint32_t rx_filter;
    uint32_t set_mcast_to_ucast_tid;
    uint32_t proxy_sta_mode;
    uint32_t set_mcast2ucast_mode;
    uint32_t set_mcast2ucast_buffer;
    uint32_t remove_mcast2ucast_buffer;
    uint32_t peer_sta_ps_statechg_enable;
    uint32_t igmpmld_ac_override;
    uint32_t block_interbss;
    uint32_t set_disable_reset_cmdid;
    uint32_t set_msdu_ttl_cmdid;
    uint32_t set_ppdu_duration_cmdid;
    uint32_t txbf_sound_period_cmdid;
    uint32_t set_promisc_mode_cmdid;
    uint32_t set_burst_mode_cmdid;
    uint32_t en_stats;
    uint32_t mu_group_policy;
    uint32_t noise_detection;
    uint32_t noise_threshold;
    uint32_t dpd_enable;
    uint32_t set_mcast_bcast_echo;
    uint32_t atf_strict_sch;
    uint32_t atf_sched_duration;
    uint32_t ant_plzn;
    uint32_t mgmt_retry_limit;
    uint32_t sensitivity_level;
    uint32_t signed_txpower_2g;
    uint32_t signed_txpower_5g;
    uint32_t enable_per_tid_amsdu;
    uint32_t enable_per_tid_ampdu;
    uint32_t cca_threshold;
    uint32_t rts_fixed_rate;
    uint32_t pdev_reset;
    uint32_t wapi_mbssid_offset;
    uint32_t arp_srcaddr;
    uint32_t arp_dstaddr;
    uint32_t enable_btcoex;
};

#define WMI_PDEV_PARAM_UNSUPPORTED 0

enum wmi_pdev_param {
    /* TX chain mask */
    WMI_PDEV_PARAM_TX_CHAIN_MASK = 0x1,
    /* RX chain mask */
    WMI_PDEV_PARAM_RX_CHAIN_MASK,
    /* TX power limit for 2G Radio */
    WMI_PDEV_PARAM_TXPOWER_LIMIT2G,
    /* TX power limit for 5G Radio */
    WMI_PDEV_PARAM_TXPOWER_LIMIT5G,
    /* TX power scale */
    WMI_PDEV_PARAM_TXPOWER_SCALE,
    /* Beacon generation mode . 0: host, 1: target   */
    WMI_PDEV_PARAM_BEACON_GEN_MODE,
    /* Beacon generation mode . 0: staggered 1: bursted   */
    WMI_PDEV_PARAM_BEACON_TX_MODE,
    /*
     * Resource manager off chan mode .
     * 0: turn off off chan mode. 1: turn on offchan mode
     */
    WMI_PDEV_PARAM_RESMGR_OFFCHAN_MODE,
    /*
     * Protection mode:
     * 0: no protection 1:use CTS-to-self 2: use RTS/CTS
     */
    WMI_PDEV_PARAM_PROTECTION_MODE,
    /*
     * Dynamic bandwidth - 0: disable, 1: enable
     *
     * When enabled HW rate control tries different bandwidths when
     * retransmitting frames.
     */
    WMI_PDEV_PARAM_DYNAMIC_BW,
    /* Non aggregrate/ 11g sw retry threshold.0-disable */
    WMI_PDEV_PARAM_NON_AGG_SW_RETRY_TH,
    /* aggregrate sw retry threshold. 0-disable*/
    WMI_PDEV_PARAM_AGG_SW_RETRY_TH,
    /* Station kickout threshold (non of consecutive failures).0-disable */
    WMI_PDEV_PARAM_STA_KICKOUT_TH,
    /* Aggerate size scaling configuration per AC */
    WMI_PDEV_PARAM_AC_AGGRSIZE_SCALING,
    /* LTR enable */
    WMI_PDEV_PARAM_LTR_ENABLE,
    /* LTR latency for BE, in us */
    WMI_PDEV_PARAM_LTR_AC_LATENCY_BE,
    /* LTR latency for BK, in us */
    WMI_PDEV_PARAM_LTR_AC_LATENCY_BK,
    /* LTR latency for VI, in us */
    WMI_PDEV_PARAM_LTR_AC_LATENCY_VI,
    /* LTR latency for VO, in us  */
    WMI_PDEV_PARAM_LTR_AC_LATENCY_VO,
    /* LTR AC latency timeout, in ms */
    WMI_PDEV_PARAM_LTR_AC_LATENCY_TIMEOUT,
    /* LTR platform latency override, in us */
    WMI_PDEV_PARAM_LTR_SLEEP_OVERRIDE,
    /* LTR-RX override, in us */
    WMI_PDEV_PARAM_LTR_RX_OVERRIDE,
    /* Tx activity timeout for LTR, in us */
    WMI_PDEV_PARAM_LTR_TX_ACTIVITY_TIMEOUT,
    /* L1SS state machine enable */
    WMI_PDEV_PARAM_L1SS_ENABLE,
    /* Deep sleep state machine enable */
    WMI_PDEV_PARAM_DSLEEP_ENABLE,
    /* RX buffering flush enable */
    WMI_PDEV_PARAM_PCIELP_TXBUF_FLUSH,
    /* RX buffering matermark */
    WMI_PDEV_PARAM_PCIELP_TXBUF_WATERMARK,
    /* RX buffering timeout enable */
    WMI_PDEV_PARAM_PCIELP_TXBUF_TMO_EN,
    /* RX buffering timeout value */
    WMI_PDEV_PARAM_PCIELP_TXBUF_TMO_VALUE,
    /* pdev level stats update period in ms */
    WMI_PDEV_PARAM_PDEV_STATS_UPDATE_PERIOD,
    /* vdev level stats update period in ms */
    WMI_PDEV_PARAM_VDEV_STATS_UPDATE_PERIOD,
    /* peer level stats update period in ms */
    WMI_PDEV_PARAM_PEER_STATS_UPDATE_PERIOD,
    /* beacon filter status update period */
    WMI_PDEV_PARAM_BCNFLT_STATS_UPDATE_PERIOD,
    /* QOS Mgmt frame protection MFP/PMF 0: disable, 1: enable */
    WMI_PDEV_PARAM_PMF_QOS,
    /* Access category on which ARP frames are sent */
    WMI_PDEV_PARAM_ARP_AC_OVERRIDE,
    /* DCS configuration */
    WMI_PDEV_PARAM_DCS,
    /* Enable/Disable ANI on target */
    WMI_PDEV_PARAM_ANI_ENABLE,
    /* configure the ANI polling period */
    WMI_PDEV_PARAM_ANI_POLL_PERIOD,
    /* configure the ANI listening period */
    WMI_PDEV_PARAM_ANI_LISTEN_PERIOD,
    /* configure OFDM immunity level */
    WMI_PDEV_PARAM_ANI_OFDM_LEVEL,
    /* configure CCK immunity level */
    WMI_PDEV_PARAM_ANI_CCK_LEVEL,
    /* Enable/Disable CDD for 1x1 STAs in rate control module */
    WMI_PDEV_PARAM_DYNTXCHAIN,
    /* Enable/Disable proxy STA */
    WMI_PDEV_PARAM_PROXY_STA,
    /* Enable/Disable low power state when all VDEVs are inactive/idle. */
    WMI_PDEV_PARAM_IDLE_PS_CONFIG,
    /* Enable/Disable power gating sleep */
    WMI_PDEV_PARAM_POWER_GATING_SLEEP,
};

enum wmi_10x_pdev_param {
    /* TX chian mask */
    WMI_10X_PDEV_PARAM_TX_CHAIN_MASK = 0x1,
    /* RX chian mask */
    WMI_10X_PDEV_PARAM_RX_CHAIN_MASK,
    /* TX power limit for 2G Radio */
    WMI_10X_PDEV_PARAM_TXPOWER_LIMIT2G,
    /* TX power limit for 5G Radio */
    WMI_10X_PDEV_PARAM_TXPOWER_LIMIT5G,
    /* TX power scale */
    WMI_10X_PDEV_PARAM_TXPOWER_SCALE,
    /* Beacon generation mode . 0: host, 1: target   */
    WMI_10X_PDEV_PARAM_BEACON_GEN_MODE,
    /* Beacon generation mode . 0: staggered 1: bursted   */
    WMI_10X_PDEV_PARAM_BEACON_TX_MODE,
    /*
     * Resource manager off chan mode .
     * 0: turn off off chan mode. 1: turn on offchan mode
     */
    WMI_10X_PDEV_PARAM_RESMGR_OFFCHAN_MODE,
    /*
     * Protection mode:
     * 0: no protection 1:use CTS-to-self 2: use RTS/CTS
     */
    WMI_10X_PDEV_PARAM_PROTECTION_MODE,
    /* Dynamic bandwidth 0: disable 1: enable */
    WMI_10X_PDEV_PARAM_DYNAMIC_BW,
    /* Non aggregrate/ 11g sw retry threshold.0-disable */
    WMI_10X_PDEV_PARAM_NON_AGG_SW_RETRY_TH,
    /* aggregrate sw retry threshold. 0-disable*/
    WMI_10X_PDEV_PARAM_AGG_SW_RETRY_TH,
    /* Station kickout threshold (non of consecutive failures).0-disable */
    WMI_10X_PDEV_PARAM_STA_KICKOUT_TH,
    /* Aggerate size scaling configuration per AC */
    WMI_10X_PDEV_PARAM_AC_AGGRSIZE_SCALING,
    /* LTR enable */
    WMI_10X_PDEV_PARAM_LTR_ENABLE,
    /* LTR latency for BE, in us */
    WMI_10X_PDEV_PARAM_LTR_AC_LATENCY_BE,
    /* LTR latency for BK, in us */
    WMI_10X_PDEV_PARAM_LTR_AC_LATENCY_BK,
    /* LTR latency for VI, in us */
    WMI_10X_PDEV_PARAM_LTR_AC_LATENCY_VI,
    /* LTR latency for VO, in us  */
    WMI_10X_PDEV_PARAM_LTR_AC_LATENCY_VO,
    /* LTR AC latency timeout, in ms */
    WMI_10X_PDEV_PARAM_LTR_AC_LATENCY_TIMEOUT,
    /* LTR platform latency override, in us */
    WMI_10X_PDEV_PARAM_LTR_SLEEP_OVERRIDE,
    /* LTR-RX override, in us */
    WMI_10X_PDEV_PARAM_LTR_RX_OVERRIDE,
    /* Tx activity timeout for LTR, in us */
    WMI_10X_PDEV_PARAM_LTR_TX_ACTIVITY_TIMEOUT,
    /* L1SS state machine enable */
    WMI_10X_PDEV_PARAM_L1SS_ENABLE,
    /* Deep sleep state machine enable */
    WMI_10X_PDEV_PARAM_DSLEEP_ENABLE,
    /* pdev level stats update period in ms */
    WMI_10X_PDEV_PARAM_PDEV_STATS_UPDATE_PERIOD,
    /* vdev level stats update period in ms */
    WMI_10X_PDEV_PARAM_VDEV_STATS_UPDATE_PERIOD,
    /* peer level stats update period in ms */
    WMI_10X_PDEV_PARAM_PEER_STATS_UPDATE_PERIOD,
    /* beacon filter status update period */
    WMI_10X_PDEV_PARAM_BCNFLT_STATS_UPDATE_PERIOD,
    /* QOS Mgmt frame protection MFP/PMF 0: disable, 1: enable */
    WMI_10X_PDEV_PARAM_PMF_QOS,
    /* Access category on which ARP and DHCP frames are sent */
    WMI_10X_PDEV_PARAM_ARPDHCP_AC_OVERRIDE,
    /* DCS configuration */
    WMI_10X_PDEV_PARAM_DCS,
    /* Enable/Disable ANI on target */
    WMI_10X_PDEV_PARAM_ANI_ENABLE,
    /* configure the ANI polling period */
    WMI_10X_PDEV_PARAM_ANI_POLL_PERIOD,
    /* configure the ANI listening period */
    WMI_10X_PDEV_PARAM_ANI_LISTEN_PERIOD,
    /* configure OFDM immunity level */
    WMI_10X_PDEV_PARAM_ANI_OFDM_LEVEL,
    /* configure CCK immunity level */
    WMI_10X_PDEV_PARAM_ANI_CCK_LEVEL,
    /* Enable/Disable CDD for 1x1 STAs in rate control module */
    WMI_10X_PDEV_PARAM_DYNTXCHAIN,
    /* Enable/Disable Fast channel reset*/
    WMI_10X_PDEV_PARAM_FAST_CHANNEL_RESET,
    /* Set Bursting DUR */
    WMI_10X_PDEV_PARAM_BURST_DUR,
    /* Set Bursting Enable*/
    WMI_10X_PDEV_PARAM_BURST_ENABLE,

    /* following are available as of firmware 10.2 */
    WMI_10X_PDEV_PARAM_SMART_ANTENNA_DEFAULT_ANTENNA,
    WMI_10X_PDEV_PARAM_IGMPMLD_OVERRIDE,
    WMI_10X_PDEV_PARAM_IGMPMLD_TID,
    WMI_10X_PDEV_PARAM_ANTENNA_GAIN,
    WMI_10X_PDEV_PARAM_RX_DECAP_MODE,
    WMI_10X_PDEV_PARAM_RX_FILTER,
    WMI_10X_PDEV_PARAM_SET_MCAST_TO_UCAST_TID,
    WMI_10X_PDEV_PARAM_PROXY_STA_MODE,
    WMI_10X_PDEV_PARAM_SET_MCAST2UCAST_MODE,
    WMI_10X_PDEV_PARAM_SET_MCAST2UCAST_BUFFER,
    WMI_10X_PDEV_PARAM_REMOVE_MCAST2UCAST_BUFFER,
    WMI_10X_PDEV_PARAM_PEER_STA_PS_STATECHG_ENABLE,
    WMI_10X_PDEV_PARAM_RTS_FIXED_RATE,
    WMI_10X_PDEV_PARAM_CAL_PERIOD
};

enum wmi_10_4_pdev_param {
    WMI_10_4_PDEV_PARAM_TX_CHAIN_MASK = 0x1,
    WMI_10_4_PDEV_PARAM_RX_CHAIN_MASK,
    WMI_10_4_PDEV_PARAM_TXPOWER_LIMIT2G,
    WMI_10_4_PDEV_PARAM_TXPOWER_LIMIT5G,
    WMI_10_4_PDEV_PARAM_TXPOWER_SCALE,
    WMI_10_4_PDEV_PARAM_BEACON_GEN_MODE,
    WMI_10_4_PDEV_PARAM_BEACON_TX_MODE,
    WMI_10_4_PDEV_PARAM_RESMGR_OFFCHAN_MODE,
    WMI_10_4_PDEV_PARAM_PROTECTION_MODE,
    WMI_10_4_PDEV_PARAM_DYNAMIC_BW,
    WMI_10_4_PDEV_PARAM_NON_AGG_SW_RETRY_TH,
    WMI_10_4_PDEV_PARAM_AGG_SW_RETRY_TH,
    WMI_10_4_PDEV_PARAM_STA_KICKOUT_TH,
    WMI_10_4_PDEV_PARAM_AC_AGGRSIZE_SCALING,
    WMI_10_4_PDEV_PARAM_LTR_ENABLE,
    WMI_10_4_PDEV_PARAM_LTR_AC_LATENCY_BE,
    WMI_10_4_PDEV_PARAM_LTR_AC_LATENCY_BK,
    WMI_10_4_PDEV_PARAM_LTR_AC_LATENCY_VI,
    WMI_10_4_PDEV_PARAM_LTR_AC_LATENCY_VO,
    WMI_10_4_PDEV_PARAM_LTR_AC_LATENCY_TIMEOUT,
    WMI_10_4_PDEV_PARAM_LTR_SLEEP_OVERRIDE,
    WMI_10_4_PDEV_PARAM_LTR_RX_OVERRIDE,
    WMI_10_4_PDEV_PARAM_LTR_TX_ACTIVITY_TIMEOUT,
    WMI_10_4_PDEV_PARAM_L1SS_ENABLE,
    WMI_10_4_PDEV_PARAM_DSLEEP_ENABLE,
    WMI_10_4_PDEV_PARAM_PCIELP_TXBUF_FLUSH,
    WMI_10_4_PDEV_PARAM_PCIELP_TXBUF_WATERMARK,
    WMI_10_4_PDEV_PARAM_PCIELP_TXBUF_TMO_EN,
    WMI_10_4_PDEV_PARAM_PCIELP_TXBUF_TMO_VALUE,
    WMI_10_4_PDEV_PARAM_PDEV_STATS_UPDATE_PERIOD,
    WMI_10_4_PDEV_PARAM_VDEV_STATS_UPDATE_PERIOD,
    WMI_10_4_PDEV_PARAM_PEER_STATS_UPDATE_PERIOD,
    WMI_10_4_PDEV_PARAM_BCNFLT_STATS_UPDATE_PERIOD,
    WMI_10_4_PDEV_PARAM_PMF_QOS,
    WMI_10_4_PDEV_PARAM_ARP_AC_OVERRIDE,
    WMI_10_4_PDEV_PARAM_DCS,
    WMI_10_4_PDEV_PARAM_ANI_ENABLE,
    WMI_10_4_PDEV_PARAM_ANI_POLL_PERIOD,
    WMI_10_4_PDEV_PARAM_ANI_LISTEN_PERIOD,
    WMI_10_4_PDEV_PARAM_ANI_OFDM_LEVEL,
    WMI_10_4_PDEV_PARAM_ANI_CCK_LEVEL,
    WMI_10_4_PDEV_PARAM_DYNTXCHAIN,
    WMI_10_4_PDEV_PARAM_PROXY_STA,
    WMI_10_4_PDEV_PARAM_IDLE_PS_CONFIG,
    WMI_10_4_PDEV_PARAM_POWER_GATING_SLEEP,
    WMI_10_4_PDEV_PARAM_AGGR_BURST,
    WMI_10_4_PDEV_PARAM_RX_DECAP_MODE,
    WMI_10_4_PDEV_PARAM_FAST_CHANNEL_RESET,
    WMI_10_4_PDEV_PARAM_BURST_DUR,
    WMI_10_4_PDEV_PARAM_BURST_ENABLE,
    WMI_10_4_PDEV_PARAM_SMART_ANTENNA_DEFAULT_ANTENNA,
    WMI_10_4_PDEV_PARAM_IGMPMLD_OVERRIDE,
    WMI_10_4_PDEV_PARAM_IGMPMLD_TID,
    WMI_10_4_PDEV_PARAM_ANTENNA_GAIN,
    WMI_10_4_PDEV_PARAM_RX_FILTER,
    WMI_10_4_PDEV_SET_MCAST_TO_UCAST_TID,
    WMI_10_4_PDEV_PARAM_PROXY_STA_MODE,
    WMI_10_4_PDEV_PARAM_SET_MCAST2UCAST_MODE,
    WMI_10_4_PDEV_PARAM_SET_MCAST2UCAST_BUFFER,
    WMI_10_4_PDEV_PARAM_REMOVE_MCAST2UCAST_BUFFER,
    WMI_10_4_PDEV_PEER_STA_PS_STATECHG_ENABLE,
    WMI_10_4_PDEV_PARAM_IGMPMLD_AC_OVERRIDE,
    WMI_10_4_PDEV_PARAM_BLOCK_INTERBSS,
    WMI_10_4_PDEV_PARAM_SET_DISABLE_RESET_CMDID,
    WMI_10_4_PDEV_PARAM_SET_MSDU_TTL_CMDID,
    WMI_10_4_PDEV_PARAM_SET_PPDU_DURATION_CMDID,
    WMI_10_4_PDEV_PARAM_TXBF_SOUND_PERIOD_CMDID,
    WMI_10_4_PDEV_PARAM_SET_PROMISC_MODE_CMDID,
    WMI_10_4_PDEV_PARAM_SET_BURST_MODE_CMDID,
    WMI_10_4_PDEV_PARAM_EN_STATS,
    WMI_10_4_PDEV_PARAM_MU_GROUP_POLICY,
    WMI_10_4_PDEV_PARAM_NOISE_DETECTION,
    WMI_10_4_PDEV_PARAM_NOISE_THRESHOLD,
    WMI_10_4_PDEV_PARAM_DPD_ENABLE,
    WMI_10_4_PDEV_PARAM_SET_MCAST_BCAST_ECHO,
    WMI_10_4_PDEV_PARAM_ATF_STRICT_SCH,
    WMI_10_4_PDEV_PARAM_ATF_SCHED_DURATION,
    WMI_10_4_PDEV_PARAM_ANT_PLZN,
    WMI_10_4_PDEV_PARAM_MGMT_RETRY_LIMIT,
    WMI_10_4_PDEV_PARAM_SENSITIVITY_LEVEL,
    WMI_10_4_PDEV_PARAM_SIGNED_TXPOWER_2G,
    WMI_10_4_PDEV_PARAM_SIGNED_TXPOWER_5G,
    WMI_10_4_PDEV_PARAM_ENABLE_PER_TID_AMSDU,
    WMI_10_4_PDEV_PARAM_ENABLE_PER_TID_AMPDU,
    WMI_10_4_PDEV_PARAM_CCA_THRESHOLD,
    WMI_10_4_PDEV_PARAM_RTS_FIXED_RATE,
    WMI_10_4_PDEV_PARAM_CAL_PERIOD,
    WMI_10_4_PDEV_PARAM_PDEV_RESET,
    WMI_10_4_PDEV_PARAM_WAPI_MBSSID_OFFSET,
    WMI_10_4_PDEV_PARAM_ARP_SRCADDR,
    WMI_10_4_PDEV_PARAM_ARP_DSTADDR,
    WMI_10_4_PDEV_PARAM_TXPOWER_DECR_DB,
    WMI_10_4_PDEV_PARAM_RX_BATCHMODE,
    WMI_10_4_PDEV_PARAM_PACKET_AGGR_DELAY,
    WMI_10_4_PDEV_PARAM_ATF_OBSS_NOISE_SCH,
    WMI_10_4_PDEV_PARAM_ATF_OBSS_NOISE_SCALING_FACTOR,
    WMI_10_4_PDEV_PARAM_CUST_TXPOWER_SCALE,
    WMI_10_4_PDEV_PARAM_ATF_DYNAMIC_ENABLE,
    WMI_10_4_PDEV_PARAM_ATF_SSID_GROUP_POLICY,
    WMI_10_4_PDEV_PARAM_ENABLE_BTCOEX,
};

struct wmi_pdev_set_param_cmd {
    uint32_t param_id;
    uint32_t param_value;
} __PACKED;

/* valid period is 1 ~ 60000ms, unit in millisecond */
#define WMI_PDEV_PARAM_CAL_PERIOD_MAX 60000

struct wmi_pdev_get_tpc_config_cmd {
    /* parameter   */
    uint32_t param;
} __PACKED;

#define WMI_TPC_CONFIG_PARAM        1
#define WMI_TPC_RATE_MAX        160
#define WMI_TPC_TX_N_CHAIN      4
#define WMI_TPC_PREAM_TABLE_MAX     10
#define WMI_TPC_FLAG            3
#define WMI_TPC_BUF_SIZE        10

enum wmi_tpc_table_type {
    WMI_TPC_TABLE_TYPE_CDD = 0,
    WMI_TPC_TABLE_TYPE_STBC = 1,
    WMI_TPC_TABLE_TYPE_TXBF = 2,
};

enum wmi_tpc_config_event_flag {
    WMI_TPC_CONFIG_EVENT_FLAG_TABLE_CDD = 0x1,
    WMI_TPC_CONFIG_EVENT_FLAG_TABLE_STBC    = 0x2,
    WMI_TPC_CONFIG_EVENT_FLAG_TABLE_TXBF    = 0x4,
};

struct wmi_pdev_tpc_config_event {
    uint32_t reg_domain;
    uint32_t chan_freq;
    uint32_t phy_mode;
    uint32_t twice_antenna_reduction;
    uint32_t twice_max_rd_power;
    int32_t twice_antenna_gain;
    uint32_t power_limit;
    uint32_t rate_max;
    uint32_t num_tx_chain;
    uint32_t ctl;
    uint32_t flags;
    int8_t max_reg_allow_pow[WMI_TPC_TX_N_CHAIN];
    int8_t max_reg_allow_pow_agcdd[WMI_TPC_TX_N_CHAIN][WMI_TPC_TX_N_CHAIN];
    int8_t max_reg_allow_pow_agstbc[WMI_TPC_TX_N_CHAIN][WMI_TPC_TX_N_CHAIN];
    int8_t max_reg_allow_pow_agtxbf[WMI_TPC_TX_N_CHAIN][WMI_TPC_TX_N_CHAIN];
    uint8_t rates_array[WMI_TPC_RATE_MAX];
} __PACKED;

/* Transmit power scale factor. */
enum wmi_tp_scale {
    WMI_TP_SCALE_MAX    = 0,    /* no scaling (default) */
    WMI_TP_SCALE_50     = 1,    /* 50% of max (-3 dBm) */
    WMI_TP_SCALE_25     = 2,    /* 25% of max (-6 dBm) */
    WMI_TP_SCALE_12     = 3,    /* 12% of max (-9 dBm) */
    WMI_TP_SCALE_MIN    = 4,    /* min, but still on   */
    WMI_TP_SCALE_SIZE   = 5,    /* max num of enum     */
};

struct wmi_pdev_chanlist_update_event {
    /* number of channels */
    uint32_t num_chan;
    /* array of channels */
    struct wmi_channel channel_list[1];
} __PACKED;

#define WMI_MAX_DEBUG_MESG (sizeof(uint32_t) * 32)

struct wmi_debug_mesg_event {
    /* message buffer, NULL terminated */
    char bufp[WMI_MAX_DEBUG_MESG];
} __PACKED;

enum {
    /* P2P device */
    VDEV_SUBTYPE_P2PDEV = 0,
    /* P2P client */
    VDEV_SUBTYPE_P2PCLI,
    /* P2P GO */
    VDEV_SUBTYPE_P2PGO,
    /* BT3.0 HS */
    VDEV_SUBTYPE_BT,
};

struct wmi_pdev_set_channel_cmd {
    /* idnore power , only use flags , mode and freq */
    struct wmi_channel chan;
} __PACKED;

struct wmi_pdev_pktlog_enable_cmd {
    uint32_t ev_bitmap;
} __PACKED;

/* Customize the DSCP (bit) to TID (0-7) mapping for QOS */
#define WMI_DSCP_MAP_MAX    (64)
struct wmi_pdev_set_dscp_tid_map_cmd {
    /* map indicating DSCP to TID conversion */
    uint32_t dscp_to_tid_map[WMI_DSCP_MAP_MAX];
} __PACKED;

enum mcast_bcast_rate_id {
    WMI_SET_MCAST_RATE,
    WMI_SET_BCAST_RATE
};

struct mcast_bcast_rate {
    enum mcast_bcast_rate_id rate_id;
    uint32_t rate;
} __PACKED;

struct wmi_wmm_params {
    uint32_t cwmin;
    uint32_t cwmax;
    uint32_t aifs;
    uint32_t txop;
    uint32_t acm;
    uint32_t no_ack;
} __PACKED;

struct wmi_pdev_set_wmm_params {
    struct wmi_wmm_params ac_be;
    struct wmi_wmm_params ac_bk;
    struct wmi_wmm_params ac_vi;
    struct wmi_wmm_params ac_vo;
} __PACKED;

struct wmi_wmm_params_arg {
    uint32_t cwmin;
    uint32_t cwmax;
    uint32_t aifs;
    uint32_t txop;
    uint32_t acm;
    uint32_t no_ack;
};

struct wmi_wmm_params_all_arg {
    struct wmi_wmm_params_arg ac_be;
    struct wmi_wmm_params_arg ac_bk;
    struct wmi_wmm_params_arg ac_vi;
    struct wmi_wmm_params_arg ac_vo;
};

struct wmi_pdev_stats_tx {
    /* Num HTT cookies queued to dispatch list */
    uint32_t comp_queued;

    /* Num HTT cookies dispatched */
    uint32_t comp_delivered;

    /* Num MSDU queued to WAL */
    uint32_t msdu_enqued;

    /* Num MPDU queue to WAL */
    uint32_t mpdu_enqued;

    /* Num MSDUs dropped by WMM limit */
    uint32_t wmm_drop;

    /* Num Local frames queued */
    uint32_t local_enqued;

    /* Num Local frames done */
    uint32_t local_freed;

    /* Num queued to HW */
    uint32_t hw_queued;

    /* Num PPDU reaped from HW */
    uint32_t hw_reaped;

    /* Num underruns */
    uint32_t underrun;

    /* Num PPDUs cleaned up in TX abort */
    uint32_t tx_abort;

    /* Num MPDUs requed by SW */
    uint32_t mpdus_requed;

    /* excessive retries */
    uint32_t tx_ko;

    /* data hw rate code */
    uint32_t data_rc;

    /* Scheduler self triggers */
    uint32_t self_triggers;

    /* frames dropped due to excessive sw retries */
    uint32_t sw_retry_failure;

    /* illegal rate phy errors  */
    uint32_t illgl_rate_phy_err;

    /* wal pdev continuous xretry */
    uint32_t pdev_cont_xretry;

    /* wal pdev continous xretry */
    uint32_t pdev_tx_timeout;

    /* wal pdev resets  */
    uint32_t pdev_resets;

    /* frames dropped due to non-availability of stateless TIDs */
    uint32_t stateless_tid_alloc_failure;

    uint32_t phy_underrun;

    /* MPDU is more than txop limit */
    uint32_t txop_ovf;
} __PACKED;

struct wmi_10_4_pdev_stats_tx {
    /* Num HTT cookies queued to dispatch list */
    uint32_t comp_queued;

    /* Num HTT cookies dispatched */
    uint32_t comp_delivered;

    /* Num MSDU queued to WAL */
    uint32_t msdu_enqued;

    /* Num MPDU queue to WAL */
    uint32_t mpdu_enqued;

    /* Num MSDUs dropped by WMM limit */
    uint32_t wmm_drop;

    /* Num Local frames queued */
    uint32_t local_enqued;

    /* Num Local frames done */
    uint32_t local_freed;

    /* Num queued to HW */
    uint32_t hw_queued;

    /* Num PPDU reaped from HW */
    uint32_t hw_reaped;

    /* Num underruns */
    uint32_t underrun;

    /* HW Paused. */
    uint32_t  hw_paused;

    /* Num PPDUs cleaned up in TX abort */
    uint32_t tx_abort;

    /* Num MPDUs requed by SW */
    uint32_t mpdus_requed;

    /* excessive retries */
    uint32_t tx_ko;

    /* data hw rate code */
    uint32_t data_rc;

    /* Scheduler self triggers */
    uint32_t self_triggers;

    /* frames dropped due to excessive sw retries */
    uint32_t sw_retry_failure;

    /* illegal rate phy errors  */
    uint32_t illgl_rate_phy_err;

    /* wal pdev continuous xretry */
    uint32_t pdev_cont_xretry;

    /* wal pdev tx timeouts */
    uint32_t pdev_tx_timeout;

    /* wal pdev resets  */
    uint32_t pdev_resets;

    /* frames dropped due to non-availability of stateless TIDs */
    uint32_t stateless_tid_alloc_failure;

    uint32_t phy_underrun;

    /* MPDU is more than txop limit */
    uint32_t txop_ovf;

    /* Number of Sequences posted */
    uint32_t seq_posted;

    /* Number of Sequences failed queueing */
    uint32_t seq_failed_queueing;

    /* Number of Sequences completed */
    uint32_t seq_completed;

    /* Number of Sequences restarted */
    uint32_t seq_restarted;

    /* Number of MU Sequences posted */
    uint32_t mu_seq_posted;

    /* Num MPDUs flushed by SW, HWPAUSED,SW TXABORT(Reset,channel change) */
    uint32_t mpdus_sw_flush;

    /* Num MPDUs filtered by HW, all filter condition (TTL expired) */
    uint32_t mpdus_hw_filter;

    /* Num MPDUs truncated by PDG
     * (TXOP, TBTT, PPDU_duration based on rate, dyn_bw)
     */
    uint32_t mpdus_truncated;

    /* Num MPDUs that was tried but didn't receive ACK or BA */
    uint32_t mpdus_ack_failed;

    /* Num MPDUs that was dropped due to expiry. */
    uint32_t mpdus_expired;
} __PACKED;

struct wmi_pdev_stats_rx {
    /* Cnts any change in ring routing mid-ppdu */
    uint32_t mid_ppdu_route_change;

    /* Total number of statuses processed */
    uint32_t status_rcvd;

    /* Extra frags on rings 0-3 */
    uint32_t r0_frags;
    uint32_t r1_frags;
    uint32_t r2_frags;
    uint32_t r3_frags;

    /* MSDUs / MPDUs delivered to HTT */
    uint32_t htt_msdus;
    uint32_t htt_mpdus;

    /* MSDUs / MPDUs delivered to local stack */
    uint32_t loc_msdus;
    uint32_t loc_mpdus;

    /* AMSDUs that have more MSDUs than the status ring size */
    uint32_t oversize_amsdu;

    /* Number of PHY errors */
    uint32_t phy_errs;

    /* Number of PHY errors drops */
    uint32_t phy_err_drop;

    /* Number of mpdu errors - FCS, MIC, ENC etc. */
    uint32_t mpdu_errs;
} __PACKED;

struct wmi_pdev_stats_peer {
    /* REMOVE THIS ONCE REAL PEER STAT COUNTERS ARE ADDED */
    uint32_t dummy;
} __PACKED;

enum wmi_stats_id {
    WMI_STAT_PEER      = (1 << 0),
    WMI_STAT_AP        = (1 << 1),
    WMI_STAT_PDEV      = (1 << 2),
    WMI_STAT_VDEV      = (1 << 3),
    WMI_STAT_BCNFLT    = (1 << 4),
    WMI_STAT_VDEV_RATE = (1 << 5),
};

enum wmi_10_4_stats_id {
    WMI_10_4_STAT_PEER      = (1 << 0),
    WMI_10_4_STAT_AP        = (1 << 1),
    WMI_10_4_STAT_INST      = (1 << 2),
    WMI_10_4_STAT_PEER_EXTD = (1 << 3),
};

struct wlan_inst_rssi_args {
    uint16_t cfg_retry_count;
    uint16_t retry_count;
};

struct wmi_request_stats_cmd {
    uint32_t stats_id;

    uint32_t vdev_id;

    /* peer MAC address */
    struct wmi_mac_addr peer_macaddr;

    /* Instantaneous RSSI arguments */
    struct wlan_inst_rssi_args inst_rssi_args;
} __PACKED;

/* Suspend option */
enum {
    /* suspend */
    WMI_PDEV_SUSPEND,

    /* suspend and disable all interrupts */
    WMI_PDEV_SUSPEND_AND_DISABLE_INTR,
};

struct wmi_pdev_suspend_cmd {
    /* suspend option sent to target */
    uint32_t suspend_opt;
} __PACKED;

struct wmi_stats_event {
    uint32_t stats_id; /* WMI_STAT_ */
    /*
     * number of pdev stats event structures
     * (wmi_pdev_stats) 0 or 1
     */
    uint32_t num_pdev_stats;
    /*
     * number of vdev stats event structures
     * (wmi_vdev_stats) 0 or max vdevs
     */
    uint32_t num_vdev_stats;
    /*
     * number of peer stats event structures
     * (wmi_peer_stats) 0 or max peers
     */
    uint32_t num_peer_stats;
    uint32_t num_bcnflt_stats;
    /*
     * followed by
     *   num_pdev_stats * size of(struct wmi_pdev_stats)
     *   num_vdev_stats * size of(struct wmi_vdev_stats)
     *   num_peer_stats * size of(struct wmi_peer_stats)
     *
     *  By having a zero sized array, the pointer to data area
     *  becomes available without increasing the struct size
     */
    uint8_t data[0];
} __PACKED;

struct wmi_10_2_stats_event {
    uint32_t stats_id; /* %WMI_REQUEST_ */
    uint32_t num_pdev_stats;
    uint32_t num_pdev_ext_stats;
    uint32_t num_vdev_stats;
    uint32_t num_peer_stats;
    uint32_t num_bcnflt_stats;
    uint8_t data[0];
} __PACKED;

/*
 * PDEV statistics
 * TODO: add all PDEV stats here
 */
struct wmi_pdev_stats_base {
    uint32_t chan_nf;
    uint32_t tx_frame_count; /* Cycles spent transmitting frames */
    uint32_t rx_frame_count; /* Cycles spent receiving frames */
    uint32_t rx_clear_count; /* Total channel busy time, evidently */
    uint32_t cycle_count; /* Total on-channel time */
    uint32_t phy_err_count;
    uint32_t chan_tx_pwr;
} __PACKED;

struct wmi_pdev_stats {
    struct wmi_pdev_stats_base base;
    struct wmi_pdev_stats_tx tx;
    struct wmi_pdev_stats_rx rx;
    struct wmi_pdev_stats_peer peer;
} __PACKED;

struct wmi_pdev_stats_extra {
    uint32_t ack_rx_bad;
    uint32_t rts_bad;
    uint32_t rts_good;
    uint32_t fcs_bad;
    uint32_t no_beacons;
    uint32_t mib_int_count;
} __PACKED;

struct wmi_10x_pdev_stats {
    struct wmi_pdev_stats_base base;
    struct wmi_pdev_stats_tx tx;
    struct wmi_pdev_stats_rx rx;
    struct wmi_pdev_stats_peer peer;
    struct wmi_pdev_stats_extra extra;
} __PACKED;

struct wmi_pdev_stats_mem {
    uint32_t dram_free;
    uint32_t iram_free;
} __PACKED;

struct wmi_10_2_pdev_stats {
    struct wmi_pdev_stats_base base;
    struct wmi_pdev_stats_tx tx;
    uint32_t mc_drop;
    struct wmi_pdev_stats_rx rx;
    uint32_t pdev_rx_timeout;
    struct wmi_pdev_stats_mem mem;
    struct wmi_pdev_stats_peer peer;
    struct wmi_pdev_stats_extra extra;
} __PACKED;

struct wmi_10_4_pdev_stats {
    struct wmi_pdev_stats_base base;
    struct wmi_10_4_pdev_stats_tx tx;
    struct wmi_pdev_stats_rx rx;
    uint32_t rx_ovfl_errs;
    struct wmi_pdev_stats_mem mem;
    uint32_t sram_free_size;
    struct wmi_pdev_stats_extra extra;
} __PACKED;

/*
 * VDEV statistics
 * TODO: add all VDEV stats here
 */
struct wmi_vdev_stats {
    uint32_t vdev_id;
} __PACKED;

/*
 * peer statistics.
 * TODO: add more stats
 */
struct wmi_peer_stats {
    struct wmi_mac_addr peer_macaddr;
    uint32_t peer_rssi;
    uint32_t peer_tx_rate;
} __PACKED;

struct wmi_10x_peer_stats {
    struct wmi_peer_stats old;
    uint32_t peer_rx_rate;
} __PACKED;

struct wmi_10_2_peer_stats {
    struct wmi_peer_stats old;
    uint32_t peer_rx_rate;
    uint32_t current_per;
    uint32_t retries;
    uint32_t tx_rate_count;
    uint32_t max_4ms_frame_len;
    uint32_t total_sub_frames;
    uint32_t tx_bytes;
    uint32_t num_pkt_loss_overflow[4];
    uint32_t num_pkt_loss_excess_retry[4];
} __PACKED;

struct wmi_10_2_4_peer_stats {
    struct wmi_10_2_peer_stats common;
    uint32_t peer_rssi_changed;
} __PACKED;

struct wmi_10_2_4_ext_peer_stats {
    struct wmi_10_2_peer_stats common;
    uint32_t peer_rssi_changed;
    uint32_t rx_duration;
} __PACKED;

struct wmi_10_4_peer_stats {
    struct wmi_mac_addr peer_macaddr;
    uint32_t peer_rssi;
    uint32_t peer_rssi_seq_num;
    uint32_t peer_tx_rate;
    uint32_t peer_rx_rate;
    uint32_t current_per;
    uint32_t retries;
    uint32_t tx_rate_count;
    uint32_t max_4ms_frame_len;
    uint32_t total_sub_frames;
    uint32_t tx_bytes;
    uint32_t num_pkt_loss_overflow[4];
    uint32_t num_pkt_loss_excess_retry[4];
    uint32_t peer_rssi_changed;
} __PACKED;

struct wmi_10_4_peer_extd_stats {
    struct wmi_mac_addr peer_macaddr;
    uint32_t inactive_time;
    uint32_t peer_chain_rssi;
    uint32_t rx_duration;
    uint32_t reserved[10];
} __PACKED;

struct wmi_10_4_bss_bcn_stats {
    uint32_t vdev_id;
    uint32_t bss_bcns_dropped;
    uint32_t bss_bcn_delivered;
} __PACKED;

struct wmi_10_4_bss_bcn_filter_stats {
    uint32_t bcns_dropped;
    uint32_t bcns_delivered;
    uint32_t active_filters;
    struct wmi_10_4_bss_bcn_stats bss_stats;
} __PACKED;

struct wmi_10_2_pdev_ext_stats {
    uint32_t rx_rssi_comb;
    uint32_t rx_rssi[4];
    uint32_t rx_mcs[10];
    uint32_t tx_mcs[10];
    uint32_t ack_rssi;
} __PACKED;

struct wmi_vdev_create_cmd {
    uint32_t vdev_id;
    uint32_t vdev_type;
    uint32_t vdev_subtype;
    struct wmi_mac_addr vdev_macaddr;
} __PACKED;

enum wmi_vdev_type {
    WMI_VDEV_TYPE_AP      = 1,
    WMI_VDEV_TYPE_STA     = 2,
    WMI_VDEV_TYPE_IBSS    = 3,
    WMI_VDEV_TYPE_MONITOR = 4,
};

enum wmi_vdev_subtype {
    WMI_VDEV_SUBTYPE_NONE,
    WMI_VDEV_SUBTYPE_P2P_DEVICE,
    WMI_VDEV_SUBTYPE_P2P_CLIENT,
    WMI_VDEV_SUBTYPE_P2P_GO,
    WMI_VDEV_SUBTYPE_PROXY_STA,
    WMI_VDEV_SUBTYPE_MESH_11S,
    WMI_VDEV_SUBTYPE_MESH_NON_11S,
};

enum wmi_vdev_subtype_legacy {
    WMI_VDEV_SUBTYPE_LEGACY_NONE      = 0,
    WMI_VDEV_SUBTYPE_LEGACY_P2P_DEV   = 1,
    WMI_VDEV_SUBTYPE_LEGACY_P2P_CLI   = 2,
    WMI_VDEV_SUBTYPE_LEGACY_P2P_GO    = 3,
    WMI_VDEV_SUBTYPE_LEGACY_PROXY_STA = 4,
};

enum wmi_vdev_subtype_10_2_4 {
    WMI_VDEV_SUBTYPE_10_2_4_NONE      = 0,
    WMI_VDEV_SUBTYPE_10_2_4_P2P_DEV   = 1,
    WMI_VDEV_SUBTYPE_10_2_4_P2P_CLI   = 2,
    WMI_VDEV_SUBTYPE_10_2_4_P2P_GO    = 3,
    WMI_VDEV_SUBTYPE_10_2_4_PROXY_STA = 4,
    WMI_VDEV_SUBTYPE_10_2_4_MESH_11S  = 5,
};

enum wmi_vdev_subtype_10_4 {
    WMI_VDEV_SUBTYPE_10_4_NONE         = 0,
    WMI_VDEV_SUBTYPE_10_4_P2P_DEV      = 1,
    WMI_VDEV_SUBTYPE_10_4_P2P_CLI      = 2,
    WMI_VDEV_SUBTYPE_10_4_P2P_GO       = 3,
    WMI_VDEV_SUBTYPE_10_4_PROXY_STA    = 4,
    WMI_VDEV_SUBTYPE_10_4_MESH_NON_11S = 5,
    WMI_VDEV_SUBTYPE_10_4_MESH_11S     = 6,
};

/* values for vdev_subtype */

/* values for vdev_start_request flags */
/*
 * Indicates that AP VDEV uses hidden ssid. only valid for
 *  AP/GO
 */
#define WMI_VDEV_START_HIDDEN_SSID  (1 << 0)
/*
 * Indicates if robust management frame/management frame
 *  protection is enabled. For GO/AP vdevs, it indicates that
 *  it may support station/client associations with RMF enabled.
 *  For STA/client vdevs, it indicates that sta will
 *  associate with AP with RMF enabled.
 */
#define WMI_VDEV_START_PMF_ENABLED  (1 << 1)

struct wmi_p2p_noa_descriptor {
    uint32_t type_count; /* 255: continuous schedule, 0: reserved */
    uint32_t duration;  /* Absent period duration in micro seconds */
    uint32_t interval;   /* Absent period interval in micro seconds */
    uint32_t start_time; /* 32 bit tsf time when in starts */
} __PACKED;

struct wmi_vdev_start_request_cmd {
    /* WMI channel */
    struct wmi_channel chan;
    /* unique id identifying the VDEV, generated by the caller */
    uint32_t vdev_id;
    /* requestor id identifying the caller module */
    uint32_t requestor_id;
    /* beacon interval from received beacon */
    uint32_t beacon_interval;
    /* DTIM Period from the received beacon */
    uint32_t dtim_period;
    /* Flags */
    uint32_t flags;
    /* ssid field. Only valid for AP/GO/IBSS/BTAmp VDEV type. */
    struct wmi_ssid ssid;
    /* beacon/probe response xmit rate. Applicable for SoftAP. */
    uint32_t bcn_tx_rate;
    /* beacon/probe response xmit power. Applicable for SoftAP. */
    uint32_t bcn_tx_power;
    /* number of p2p NOA descriptor(s) from scan entry */
    uint32_t num_noa_descriptors;
    /*
     * Disable H/W ack. This used by WMI_VDEV_RESTART_REQUEST_CMDID.
     * During CAC, Our HW shouldn't ack ditected frames
     */
    uint32_t disable_hw_ack;
    /* actual p2p NOA descriptor from scan entry */
    struct wmi_p2p_noa_descriptor noa_descriptors[2];
} __PACKED;

struct wmi_vdev_restart_request_cmd {
    struct wmi_vdev_start_request_cmd vdev_start_request_cmd;
} __PACKED;

struct wmi_vdev_start_request_arg {
    uint32_t vdev_id;
    struct wmi_channel_arg channel;
    uint32_t bcn_intval;
    uint32_t dtim_period;
    uint8_t* ssid;
    uint32_t ssid_len;
    uint32_t bcn_tx_rate;
    uint32_t bcn_tx_power;
    bool disable_hw_ack;
    bool hidden_ssid;
    bool pmf_enabled;
};

struct wmi_vdev_delete_cmd {
    /* unique id identifying the VDEV, generated by the caller */
    uint32_t vdev_id;
} __PACKED;

struct wmi_vdev_up_cmd {
    uint32_t vdev_id;
    uint32_t vdev_assoc_id;
    struct wmi_mac_addr vdev_bssid;
} __PACKED;

struct wmi_vdev_stop_cmd {
    uint32_t vdev_id;
} __PACKED;

struct wmi_vdev_down_cmd {
    uint32_t vdev_id;
} __PACKED;

struct wmi_vdev_standby_response_cmd {
    /* unique id identifying the VDEV, generated by the caller */
    uint32_t vdev_id;
} __PACKED;

struct wmi_vdev_resume_response_cmd {
    /* unique id identifying the VDEV, generated by the caller */
    uint32_t vdev_id;
} __PACKED;

struct wmi_vdev_set_param_cmd {
    uint32_t vdev_id;
    uint32_t param_id;
    uint32_t param_value;
} __PACKED;

#define WMI_MAX_KEY_INDEX   3
#define WMI_MAX_KEY_LEN     32

#define WMI_KEY_PAIRWISE 0x00
#define WMI_KEY_GROUP    0x01
#define WMI_KEY_TX_USAGE 0x02 /* default tx key - static wep */

struct wmi_key_seq_counter {
    uint32_t key_seq_counter_l;
    uint32_t key_seq_counter_h;
} __PACKED;

#define WMI_CIPHER_NONE     0x0 /* clear key */
#define WMI_CIPHER_WEP      0x1
#define WMI_CIPHER_TKIP     0x2
#define WMI_CIPHER_AES_OCB  0x3
#define WMI_CIPHER_AES_CCM  0x4
#define WMI_CIPHER_WAPI     0x5
#define WMI_CIPHER_CKIP     0x6
#define WMI_CIPHER_AES_CMAC 0x7

struct wmi_vdev_install_key_cmd {
    uint32_t vdev_id;
    struct wmi_mac_addr peer_macaddr;
    uint32_t key_idx;
    uint32_t key_flags;
    uint32_t key_cipher; /* %WMI_CIPHER_ */
    struct wmi_key_seq_counter key_rsc_counter;
    struct wmi_key_seq_counter key_global_rsc_counter;
    struct wmi_key_seq_counter key_tsc_counter;
    uint8_t wpi_key_rsc_counter[16];
    uint8_t wpi_key_tsc_counter[16];
    uint32_t key_len;
    uint32_t key_txmic_len;
    uint32_t key_rxmic_len;

    /* contains key followed by tx mic followed by rx mic */
    uint8_t key_data[0];
} __PACKED;

struct wmi_vdev_install_key_arg {
    uint32_t vdev_id;
    const uint8_t* macaddr;
    uint32_t key_idx;
    uint32_t key_flags;
    uint32_t key_cipher;
    uint32_t key_len;
    uint32_t key_txmic_len;
    uint32_t key_rxmic_len;
    const void* key_data;
};

/*
 * vdev fixed rate format:
 * - preamble - b7:b6 - see WMI_RATE_PREMABLE_
 * - nss      - b5:b4 - ss number (0 mean 1ss)
 * - rate_mcs - b3:b0 - as below
 *    CCK:  0 - 11Mbps, 1 - 5,5Mbps, 2 - 2Mbps, 3 - 1Mbps,
 *          4 - 11Mbps (s), 5 - 5,5Mbps (s), 6 - 2Mbps (s)
 *    OFDM: 0 - 48Mbps, 1 - 24Mbps, 2 - 12Mbps, 3 - 6Mbps,
 *          4 - 54Mbps, 5 - 36Mbps, 6 - 18Mbps, 7 - 9Mbps
 *    HT/VHT: MCS index
 */

/* Preamble types to be used with VDEV fixed rate configuration */
enum wmi_rate_preamble {
    WMI_RATE_PREAMBLE_OFDM,
    WMI_RATE_PREAMBLE_CCK,
    WMI_RATE_PREAMBLE_HT,
    WMI_RATE_PREAMBLE_VHT,
};

#define ATH10K_HW_NSS(rate)     (1 + (((rate) >> 4) & 0x3))
#define ATH10K_HW_PREAMBLE(rate)    (((rate) >> 6) & 0x3)
#define ATH10K_HW_MCS_RATE(rate)    ((rate) & 0xf)
#define ATH10K_HW_LEGACY_RATE(rate) ((rate) & 0x3f)
#define ATH10K_HW_BW(flags)     (((flags) >> 3) & 0x3)
#define ATH10K_HW_GI(flags)     (((flags) >> 5) & 0x1)
#define ATH10K_HW_RATECODE(rate, nss, preamble) \
    (((preamble) << 6) | ((nss) << 4) | (rate))

#define VHT_MCS_NUM     10
#define VHT_BW_NUM      4
#define VHT_NSS_NUM     4

/* Value to disable fixed rate setting */
#define WMI_FIXED_RATE_NONE    (0xff)

struct wmi_vdev_param_map {
    uint32_t rts_threshold;
    uint32_t fragmentation_threshold;
    uint32_t beacon_interval;
    uint32_t listen_interval;
    uint32_t multicast_rate;
    uint32_t mgmt_tx_rate;
    uint32_t slot_time;
    uint32_t preamble;
    uint32_t swba_time;
    uint32_t wmi_vdev_stats_update_period;
    uint32_t wmi_vdev_pwrsave_ageout_time;
    uint32_t wmi_vdev_host_swba_interval;
    uint32_t dtim_period;
    uint32_t wmi_vdev_oc_scheduler_air_time_limit;
    uint32_t wds;
    uint32_t atim_window;
    uint32_t bmiss_count_max;
    uint32_t bmiss_first_bcnt;
    uint32_t bmiss_final_bcnt;
    uint32_t feature_wmm;
    uint32_t chwidth;
    uint32_t chextoffset;
    uint32_t disable_htprotection;
    uint32_t sta_quickkickout;
    uint32_t mgmt_rate;
    uint32_t protection_mode;
    uint32_t fixed_rate;
    uint32_t sgi;
    uint32_t ldpc;
    uint32_t tx_stbc;
    uint32_t rx_stbc;
    uint32_t intra_bss_fwd;
    uint32_t def_keyid;
    uint32_t nss;
    uint32_t bcast_data_rate;
    uint32_t mcast_data_rate;
    uint32_t mcast_indicate;
    uint32_t dhcp_indicate;
    uint32_t unknown_dest_indicate;
    uint32_t ap_keepalive_min_idle_inactive_time_secs;
    uint32_t ap_keepalive_max_idle_inactive_time_secs;
    uint32_t ap_keepalive_max_unresponsive_time_secs;
    uint32_t ap_enable_nawds;
    uint32_t mcast2ucast_set;
    uint32_t enable_rtscts;
    uint32_t txbf;
    uint32_t packet_powersave;
    uint32_t drop_unencry;
    uint32_t tx_encap_type;
    uint32_t ap_detect_out_of_sync_sleeping_sta_time_secs;
    uint32_t rc_num_retries;
    uint32_t cabq_maxdur;
    uint32_t mfptest_set;
    uint32_t rts_fixed_rate;
    uint32_t vht_sgimask;
    uint32_t vht80_ratemask;
    uint32_t early_rx_adjust_enable;
    uint32_t early_rx_tgt_bmiss_num;
    uint32_t early_rx_bmiss_sample_cycle;
    uint32_t early_rx_slop_step;
    uint32_t early_rx_init_slop;
    uint32_t early_rx_adjust_pause;
    uint32_t proxy_sta;
    uint32_t meru_vc;
    uint32_t rx_decap_type;
    uint32_t bw_nss_ratemask;
    uint32_t inc_tsf;
    uint32_t dec_tsf;
};

#define WMI_VDEV_PARAM_UNSUPPORTED 0

/* the definition of different VDEV parameters */
enum wmi_vdev_param {
    /* RTS Threshold */
    WMI_VDEV_PARAM_RTS_THRESHOLD = 0x1,
    /* Fragmentation threshold */
    WMI_VDEV_PARAM_FRAGMENTATION_THRESHOLD,
    /* beacon interval in TUs */
    WMI_VDEV_PARAM_BEACON_INTERVAL,
    /* Listen interval in TUs */
    WMI_VDEV_PARAM_LISTEN_INTERVAL,
    /* multicast rate in Mbps */
    WMI_VDEV_PARAM_MULTICAST_RATE,
    /* management frame rate in Mbps */
    WMI_VDEV_PARAM_MGMT_TX_RATE,
    /* slot time (long vs short) */
    WMI_VDEV_PARAM_SLOT_TIME,
    /* preamble (long vs short) */
    WMI_VDEV_PARAM_PREAMBLE,
    /* SWBA time (time before tbtt in msec) */
    WMI_VDEV_PARAM_SWBA_TIME,
    /* time period for updating VDEV stats */
    WMI_VDEV_STATS_UPDATE_PERIOD,
    /* age out time in msec for frames queued for station in power save */
    WMI_VDEV_PWRSAVE_AGEOUT_TIME,
    /*
     * Host SWBA interval (time in msec before tbtt for SWBA event
     * generation).
     */
    WMI_VDEV_HOST_SWBA_INTERVAL,
    /* DTIM period (specified in units of num beacon intervals) */
    WMI_VDEV_PARAM_DTIM_PERIOD,
    /*
     * scheduler air time limit for this VDEV. used by off chan
     * scheduler.
     */
    WMI_VDEV_OC_SCHEDULER_AIR_TIME_LIMIT,
    /* enable/dsiable WDS for this VDEV  */
    WMI_VDEV_PARAM_WDS,
    /* ATIM Window */
    WMI_VDEV_PARAM_ATIM_WINDOW,
    /* BMISS max */
    WMI_VDEV_PARAM_BMISS_COUNT_MAX,
    /* BMISS first time */
    WMI_VDEV_PARAM_BMISS_FIRST_BCNT,
    /* BMISS final time */
    WMI_VDEV_PARAM_BMISS_FINAL_BCNT,
    /* WMM enables/disabled */
    WMI_VDEV_PARAM_FEATURE_WMM,
    /* Channel width */
    WMI_VDEV_PARAM_CHWIDTH,
    /* Channel Offset */
    WMI_VDEV_PARAM_CHEXTOFFSET,
    /* Disable HT Protection */
    WMI_VDEV_PARAM_DISABLE_HTPROTECTION,
    /* Quick STA Kickout */
    WMI_VDEV_PARAM_STA_QUICKKICKOUT,
    /* Rate to be used with Management frames */
    WMI_VDEV_PARAM_MGMT_RATE,
    /* Protection Mode */
    WMI_VDEV_PARAM_PROTECTION_MODE,
    /* Fixed rate setting */
    WMI_VDEV_PARAM_FIXED_RATE,
    /* Short GI Enable/Disable */
    WMI_VDEV_PARAM_SGI,
    /* Enable LDPC */
    WMI_VDEV_PARAM_LDPC,
    /* Enable Tx STBC */
    WMI_VDEV_PARAM_TX_STBC,
    /* Enable Rx STBC */
    WMI_VDEV_PARAM_RX_STBC,
    /* Intra BSS forwarding  */
    WMI_VDEV_PARAM_INTRA_BSS_FWD,
    /* Setting Default xmit key for Vdev */
    WMI_VDEV_PARAM_DEF_KEYID,
    /* NSS width */
    WMI_VDEV_PARAM_NSS,
    /* Set the custom rate for the broadcast data frames */
    WMI_VDEV_PARAM_BCAST_DATA_RATE,
    /* Set the custom rate (rate-code) for multicast data frames */
    WMI_VDEV_PARAM_MCAST_DATA_RATE,
    /* Tx multicast packet indicate Enable/Disable */
    WMI_VDEV_PARAM_MCAST_INDICATE,
    /* Tx DHCP packet indicate Enable/Disable */
    WMI_VDEV_PARAM_DHCP_INDICATE,
    /* Enable host inspection of Tx unicast packet to unknown destination */
    WMI_VDEV_PARAM_UNKNOWN_DEST_INDICATE,

    /* The minimum amount of time AP begins to consider STA inactive */
    WMI_VDEV_PARAM_AP_KEEPALIVE_MIN_IDLE_INACTIVE_TIME_SECS,

    /*
     * An associated STA is considered inactive when there is no recent
     * TX/RX activity and no downlink frames are buffered for it. Once a
     * STA exceeds the maximum idle inactive time, the AP will send an
     * 802.11 data-null as a keep alive to verify the STA is still
     * associated. If the STA does ACK the data-null, or if the data-null
     * is buffered and the STA does not retrieve it, the STA will be
     * considered unresponsive
     * (see WMI_VDEV_AP_KEEPALIVE_MAX_UNRESPONSIVE_TIME_SECS).
     */
    WMI_VDEV_PARAM_AP_KEEPALIVE_MAX_IDLE_INACTIVE_TIME_SECS,

    /*
     * An associated STA is considered unresponsive if there is no recent
     * TX/RX activity and downlink frames are buffered for it. Once a STA
     * exceeds the maximum unresponsive time, the AP will send a
     * WMI_STA_KICKOUT event to the host so the STA can be deleted.
     */
    WMI_VDEV_PARAM_AP_KEEPALIVE_MAX_UNRESPONSIVE_TIME_SECS,

    /* Enable NAWDS : MCAST INSPECT Enable, NAWDS Flag set */
    WMI_VDEV_PARAM_AP_ENABLE_NAWDS,
    /* Enable/Disable RTS-CTS */
    WMI_VDEV_PARAM_ENABLE_RTSCTS,
    /* Enable TXBFee/er */
    WMI_VDEV_PARAM_TXBF,

    /* Set packet power save */
    WMI_VDEV_PARAM_PACKET_POWERSAVE,

    /*
     * Drops un-encrypted packets if eceived in an encrypted connection
     * otherwise forwards to host.
     */
    WMI_VDEV_PARAM_DROP_UNENCRY,

    /*
     * Set the encapsulation type for frames.
     */
    WMI_VDEV_PARAM_TX_ENCAP_TYPE,
};

/* the definition of different VDEV parameters */
enum wmi_10x_vdev_param {
    /* RTS Threshold */
    WMI_10X_VDEV_PARAM_RTS_THRESHOLD = 0x1,
    /* Fragmentation threshold */
    WMI_10X_VDEV_PARAM_FRAGMENTATION_THRESHOLD,
    /* beacon interval in TUs */
    WMI_10X_VDEV_PARAM_BEACON_INTERVAL,
    /* Listen interval in TUs */
    WMI_10X_VDEV_PARAM_LISTEN_INTERVAL,
    /* multicast rate in Mbps */
    WMI_10X_VDEV_PARAM_MULTICAST_RATE,
    /* management frame rate in Mbps */
    WMI_10X_VDEV_PARAM_MGMT_TX_RATE,
    /* slot time (long vs short) */
    WMI_10X_VDEV_PARAM_SLOT_TIME,
    /* preamble (long vs short) */
    WMI_10X_VDEV_PARAM_PREAMBLE,
    /* SWBA time (time before tbtt in msec) */
    WMI_10X_VDEV_PARAM_SWBA_TIME,
    /* time period for updating VDEV stats */
    WMI_10X_VDEV_STATS_UPDATE_PERIOD,
    /* age out time in msec for frames queued for station in power save */
    WMI_10X_VDEV_PWRSAVE_AGEOUT_TIME,
    /*
     * Host SWBA interval (time in msec before tbtt for SWBA event
     * generation).
     */
    WMI_10X_VDEV_HOST_SWBA_INTERVAL,
    /* DTIM period (specified in units of num beacon intervals) */
    WMI_10X_VDEV_PARAM_DTIM_PERIOD,
    /*
     * scheduler air time limit for this VDEV. used by off chan
     * scheduler.
     */
    WMI_10X_VDEV_OC_SCHEDULER_AIR_TIME_LIMIT,
    /* enable/dsiable WDS for this VDEV  */
    WMI_10X_VDEV_PARAM_WDS,
    /* ATIM Window */
    WMI_10X_VDEV_PARAM_ATIM_WINDOW,
    /* BMISS max */
    WMI_10X_VDEV_PARAM_BMISS_COUNT_MAX,
    /* WMM enables/disabled */
    WMI_10X_VDEV_PARAM_FEATURE_WMM,
    /* Channel width */
    WMI_10X_VDEV_PARAM_CHWIDTH,
    /* Channel Offset */
    WMI_10X_VDEV_PARAM_CHEXTOFFSET,
    /* Disable HT Protection */
    WMI_10X_VDEV_PARAM_DISABLE_HTPROTECTION,
    /* Quick STA Kickout */
    WMI_10X_VDEV_PARAM_STA_QUICKKICKOUT,
    /* Rate to be used with Management frames */
    WMI_10X_VDEV_PARAM_MGMT_RATE,
    /* Protection Mode */
    WMI_10X_VDEV_PARAM_PROTECTION_MODE,
    /* Fixed rate setting */
    WMI_10X_VDEV_PARAM_FIXED_RATE,
    /* Short GI Enable/Disable */
    WMI_10X_VDEV_PARAM_SGI,
    /* Enable LDPC */
    WMI_10X_VDEV_PARAM_LDPC,
    /* Enable Tx STBC */
    WMI_10X_VDEV_PARAM_TX_STBC,
    /* Enable Rx STBC */
    WMI_10X_VDEV_PARAM_RX_STBC,
    /* Intra BSS forwarding  */
    WMI_10X_VDEV_PARAM_INTRA_BSS_FWD,
    /* Setting Default xmit key for Vdev */
    WMI_10X_VDEV_PARAM_DEF_KEYID,
    /* NSS width */
    WMI_10X_VDEV_PARAM_NSS,
    /* Set the custom rate for the broadcast data frames */
    WMI_10X_VDEV_PARAM_BCAST_DATA_RATE,
    /* Set the custom rate (rate-code) for multicast data frames */
    WMI_10X_VDEV_PARAM_MCAST_DATA_RATE,
    /* Tx multicast packet indicate Enable/Disable */
    WMI_10X_VDEV_PARAM_MCAST_INDICATE,
    /* Tx DHCP packet indicate Enable/Disable */
    WMI_10X_VDEV_PARAM_DHCP_INDICATE,
    /* Enable host inspection of Tx unicast packet to unknown destination */
    WMI_10X_VDEV_PARAM_UNKNOWN_DEST_INDICATE,

    /* The minimum amount of time AP begins to consider STA inactive */
    WMI_10X_VDEV_PARAM_AP_KEEPALIVE_MIN_IDLE_INACTIVE_TIME_SECS,

    /*
     * An associated STA is considered inactive when there is no recent
     * TX/RX activity and no downlink frames are buffered for it. Once a
     * STA exceeds the maximum idle inactive time, the AP will send an
     * 802.11 data-null as a keep alive to verify the STA is still
     * associated. If the STA does ACK the data-null, or if the data-null
     * is buffered and the STA does not retrieve it, the STA will be
     * considered unresponsive
     * (see WMI_10X_VDEV_AP_KEEPALIVE_MAX_UNRESPONSIVE_TIME_SECS).
     */
    WMI_10X_VDEV_PARAM_AP_KEEPALIVE_MAX_IDLE_INACTIVE_TIME_SECS,

    /*
     * An associated STA is considered unresponsive if there is no recent
     * TX/RX activity and downlink frames are buffered for it. Once a STA
     * exceeds the maximum unresponsive time, the AP will send a
     * WMI_10X_STA_KICKOUT event to the host so the STA can be deleted.
     */
    WMI_10X_VDEV_PARAM_AP_KEEPALIVE_MAX_UNRESPONSIVE_TIME_SECS,

    /* Enable NAWDS : MCAST INSPECT Enable, NAWDS Flag set */
    WMI_10X_VDEV_PARAM_AP_ENABLE_NAWDS,

    WMI_10X_VDEV_PARAM_MCAST2UCAST_SET,
    /* Enable/Disable RTS-CTS */
    WMI_10X_VDEV_PARAM_ENABLE_RTSCTS,

    WMI_10X_VDEV_PARAM_AP_DETECT_OUT_OF_SYNC_SLEEPING_STA_TIME_SECS,

    /* following are available as of firmware 10.2 */
    WMI_10X_VDEV_PARAM_TX_ENCAP_TYPE,
    WMI_10X_VDEV_PARAM_CABQ_MAXDUR,
    WMI_10X_VDEV_PARAM_MFPTEST_SET,
    WMI_10X_VDEV_PARAM_RTS_FIXED_RATE,
    WMI_10X_VDEV_PARAM_VHT_SGIMASK,
    WMI_10X_VDEV_PARAM_VHT80_RATEMASK,
    WMI_10X_VDEV_PARAM_TSF_INCREMENT,
};

enum wmi_10_4_vdev_param {
    WMI_10_4_VDEV_PARAM_RTS_THRESHOLD = 0x1,
    WMI_10_4_VDEV_PARAM_FRAGMENTATION_THRESHOLD,
    WMI_10_4_VDEV_PARAM_BEACON_INTERVAL,
    WMI_10_4_VDEV_PARAM_LISTEN_INTERVAL,
    WMI_10_4_VDEV_PARAM_MULTICAST_RATE,
    WMI_10_4_VDEV_PARAM_MGMT_TX_RATE,
    WMI_10_4_VDEV_PARAM_SLOT_TIME,
    WMI_10_4_VDEV_PARAM_PREAMBLE,
    WMI_10_4_VDEV_PARAM_SWBA_TIME,
    WMI_10_4_VDEV_STATS_UPDATE_PERIOD,
    WMI_10_4_VDEV_PWRSAVE_AGEOUT_TIME,
    WMI_10_4_VDEV_HOST_SWBA_INTERVAL,
    WMI_10_4_VDEV_PARAM_DTIM_PERIOD,
    WMI_10_4_VDEV_OC_SCHEDULER_AIR_TIME_LIMIT,
    WMI_10_4_VDEV_PARAM_WDS,
    WMI_10_4_VDEV_PARAM_ATIM_WINDOW,
    WMI_10_4_VDEV_PARAM_BMISS_COUNT_MAX,
    WMI_10_4_VDEV_PARAM_BMISS_FIRST_BCNT,
    WMI_10_4_VDEV_PARAM_BMISS_FINAL_BCNT,
    WMI_10_4_VDEV_PARAM_FEATURE_WMM,
    WMI_10_4_VDEV_PARAM_CHWIDTH,
    WMI_10_4_VDEV_PARAM_CHEXTOFFSET,
    WMI_10_4_VDEV_PARAM_DISABLE_HTPROTECTION,
    WMI_10_4_VDEV_PARAM_STA_QUICKKICKOUT,
    WMI_10_4_VDEV_PARAM_MGMT_RATE,
    WMI_10_4_VDEV_PARAM_PROTECTION_MODE,
    WMI_10_4_VDEV_PARAM_FIXED_RATE,
    WMI_10_4_VDEV_PARAM_SGI,
    WMI_10_4_VDEV_PARAM_LDPC,
    WMI_10_4_VDEV_PARAM_TX_STBC,
    WMI_10_4_VDEV_PARAM_RX_STBC,
    WMI_10_4_VDEV_PARAM_INTRA_BSS_FWD,
    WMI_10_4_VDEV_PARAM_DEF_KEYID,
    WMI_10_4_VDEV_PARAM_NSS,
    WMI_10_4_VDEV_PARAM_BCAST_DATA_RATE,
    WMI_10_4_VDEV_PARAM_MCAST_DATA_RATE,
    WMI_10_4_VDEV_PARAM_MCAST_INDICATE,
    WMI_10_4_VDEV_PARAM_DHCP_INDICATE,
    WMI_10_4_VDEV_PARAM_UNKNOWN_DEST_INDICATE,
    WMI_10_4_VDEV_PARAM_AP_KEEPALIVE_MIN_IDLE_INACTIVE_TIME_SECS,
    WMI_10_4_VDEV_PARAM_AP_KEEPALIVE_MAX_IDLE_INACTIVE_TIME_SECS,
    WMI_10_4_VDEV_PARAM_AP_KEEPALIVE_MAX_UNRESPONSIVE_TIME_SECS,
    WMI_10_4_VDEV_PARAM_AP_ENABLE_NAWDS,
    WMI_10_4_VDEV_PARAM_MCAST2UCAST_SET,
    WMI_10_4_VDEV_PARAM_ENABLE_RTSCTS,
    WMI_10_4_VDEV_PARAM_RC_NUM_RETRIES,
    WMI_10_4_VDEV_PARAM_TXBF,
    WMI_10_4_VDEV_PARAM_PACKET_POWERSAVE,
    WMI_10_4_VDEV_PARAM_DROP_UNENCRY,
    WMI_10_4_VDEV_PARAM_TX_ENCAP_TYPE,
    WMI_10_4_VDEV_PARAM_AP_DETECT_OUT_OF_SYNC_SLEEPING_STA_TIME_SECS,
    WMI_10_4_VDEV_PARAM_CABQ_MAXDUR,
    WMI_10_4_VDEV_PARAM_MFPTEST_SET,
    WMI_10_4_VDEV_PARAM_RTS_FIXED_RATE,
    WMI_10_4_VDEV_PARAM_VHT_SGIMASK,
    WMI_10_4_VDEV_PARAM_VHT80_RATEMASK,
    WMI_10_4_VDEV_PARAM_EARLY_RX_ADJUST_ENABLE,
    WMI_10_4_VDEV_PARAM_EARLY_RX_TGT_BMISS_NUM,
    WMI_10_4_VDEV_PARAM_EARLY_RX_BMISS_SAMPLE_CYCLE,
    WMI_10_4_VDEV_PARAM_EARLY_RX_SLOP_STEP,
    WMI_10_4_VDEV_PARAM_EARLY_RX_INIT_SLOP,
    WMI_10_4_VDEV_PARAM_EARLY_RX_ADJUST_PAUSE,
    WMI_10_4_VDEV_PARAM_PROXY_STA,
    WMI_10_4_VDEV_PARAM_MERU_VC,
    WMI_10_4_VDEV_PARAM_RX_DECAP_TYPE,
    WMI_10_4_VDEV_PARAM_BW_NSS_RATEMASK,
    WMI_10_4_VDEV_PARAM_SENSOR_AP,
    WMI_10_4_VDEV_PARAM_BEACON_RATE,
    WMI_10_4_VDEV_PARAM_DTIM_ENABLE_CTS,
    WMI_10_4_VDEV_PARAM_STA_KICKOUT,
    WMI_10_4_VDEV_PARAM_CAPABILITIES,
    WMI_10_4_VDEV_PARAM_TSF_INCREMENT,
    WMI_10_4_VDEV_PARAM_RX_FILTER,
    WMI_10_4_VDEV_PARAM_MGMT_TX_POWER,
    WMI_10_4_VDEV_PARAM_ATF_SSID_SCHED_POLICY,
    WMI_10_4_VDEV_PARAM_DISABLE_DYN_BW_RTS,
    WMI_10_4_VDEV_PARAM_TSF_DECREMENT,
};

#define WMI_VDEV_PARAM_TXBF_SU_TX_BFEE (1 << 0)
#define WMI_VDEV_PARAM_TXBF_MU_TX_BFEE (1 << 1)
#define WMI_VDEV_PARAM_TXBF_SU_TX_BFER (1 << 2)
#define WMI_VDEV_PARAM_TXBF_MU_TX_BFER (1 << 3)

#define WMI_TXBF_STS_CAP_OFFSET_LSB 4
#define WMI_TXBF_STS_CAP_OFFSET_MASK    0xf0
#define WMI_BF_SOUND_DIM_OFFSET_LSB 8
#define WMI_BF_SOUND_DIM_OFFSET_MASK    0xf00

/* slot time long */
#define WMI_VDEV_SLOT_TIME_LONG     0x1
/* slot time short */
#define WMI_VDEV_SLOT_TIME_SHORT    0x2
/* preablbe long */
#define WMI_VDEV_PREAMBLE_LONG      0x1
/* preablbe short */
#define WMI_VDEV_PREAMBLE_SHORT     0x2

enum wmi_start_event_param {
    WMI_VDEV_RESP_START_EVENT = 0,
    WMI_VDEV_RESP_RESTART_EVENT,
};

struct wmi_vdev_start_response_event {
    uint32_t vdev_id;
    uint32_t req_id;
    uint32_t resp_type; /* %WMI_VDEV_RESP_ */
    uint32_t status;
} __PACKED;

struct wmi_vdev_standby_req_event {
    /* unique id identifying the VDEV, generated by the caller */
    uint32_t vdev_id;
} __PACKED;

struct wmi_vdev_resume_req_event {
    /* unique id identifying the VDEV, generated by the caller */
    uint32_t vdev_id;
} __PACKED;

struct wmi_vdev_stopped_event {
    /* unique id identifying the VDEV, generated by the caller */
    uint32_t vdev_id;
} __PACKED;

/*
 * common structure used for simple events
 * (stopped, resume_req, standby response)
 */
struct wmi_vdev_simple_event {
    /* unique id identifying the VDEV, generated by the caller */
    uint32_t vdev_id;
} __PACKED;

/* VDEV start response status codes */
/* VDEV successfully started */
#define WMI_INIFIED_VDEV_START_RESPONSE_STATUS_SUCCESS  0x0

/* requested VDEV not found */
#define WMI_INIFIED_VDEV_START_RESPONSE_INVALID_VDEVID  0x1

/* unsupported VDEV combination */
#define WMI_INIFIED_VDEV_START_RESPONSE_NOT_SUPPORTED   0x2

/* TODO: please add more comments if you have in-depth information */
struct wmi_vdev_spectral_conf_cmd {
    uint32_t vdev_id;

    /* number of fft samples to send (0 for infinite) */
    uint32_t scan_count;
    uint32_t scan_period;
    uint32_t scan_priority;

    /* number of bins in the FFT: 2^(fft_size - bin_scale) */
    uint32_t scan_fft_size;
    uint32_t scan_gc_ena;
    uint32_t scan_restart_ena;
    uint32_t scan_noise_floor_ref;
    uint32_t scan_init_delay;
    uint32_t scan_nb_tone_thr;
    uint32_t scan_str_bin_thr;
    uint32_t scan_wb_rpt_mode;
    uint32_t scan_rssi_rpt_mode;
    uint32_t scan_rssi_thr;
    uint32_t scan_pwr_format;

    /* rpt_mode: Format of FFT report to software for spectral scan
     * triggered FFTs:
     *  0: No FFT report (only spectral scan summary report)
     *  1: 2-dword summary of metrics for each completed FFT + spectral
     *     scan summary report
     *  2: 2-dword summary of metrics for each completed FFT +
     *     1x- oversampled bins(in-band) per FFT + spectral scan summary
     *     report
     *  3: 2-dword summary of metrics for each completed FFT +
     *     2x- oversampled bins (all) per FFT + spectral scan summary
     */
    uint32_t scan_rpt_mode;
    uint32_t scan_bin_scale;
    uint32_t scan_dbm_adj;
    uint32_t scan_chn_mask;
} __PACKED;

struct wmi_vdev_spectral_conf_arg {
    uint32_t vdev_id;
    uint32_t scan_count;
    uint32_t scan_period;
    uint32_t scan_priority;
    uint32_t scan_fft_size;
    uint32_t scan_gc_ena;
    uint32_t scan_restart_ena;
    uint32_t scan_noise_floor_ref;
    uint32_t scan_init_delay;
    uint32_t scan_nb_tone_thr;
    uint32_t scan_str_bin_thr;
    uint32_t scan_wb_rpt_mode;
    uint32_t scan_rssi_rpt_mode;
    uint32_t scan_rssi_thr;
    uint32_t scan_pwr_format;
    uint32_t scan_rpt_mode;
    uint32_t scan_bin_scale;
    uint32_t scan_dbm_adj;
    uint32_t scan_chn_mask;
};

#define WMI_SPECTRAL_ENABLE_DEFAULT              0
#define WMI_SPECTRAL_COUNT_DEFAULT               0
#define WMI_SPECTRAL_PERIOD_DEFAULT             35
#define WMI_SPECTRAL_PRIORITY_DEFAULT            1
#define WMI_SPECTRAL_FFT_SIZE_DEFAULT            7
#define WMI_SPECTRAL_GC_ENA_DEFAULT              1
#define WMI_SPECTRAL_RESTART_ENA_DEFAULT         0
#define WMI_SPECTRAL_NOISE_FLOOR_REF_DEFAULT   -96
#define WMI_SPECTRAL_INIT_DELAY_DEFAULT         80
#define WMI_SPECTRAL_NB_TONE_THR_DEFAULT        12
#define WMI_SPECTRAL_STR_BIN_THR_DEFAULT         8
#define WMI_SPECTRAL_WB_RPT_MODE_DEFAULT         0
#define WMI_SPECTRAL_RSSI_RPT_MODE_DEFAULT       0
#define WMI_SPECTRAL_RSSI_THR_DEFAULT         0xf0
#define WMI_SPECTRAL_PWR_FORMAT_DEFAULT          0
#define WMI_SPECTRAL_RPT_MODE_DEFAULT            2
#define WMI_SPECTRAL_BIN_SCALE_DEFAULT           1
#define WMI_SPECTRAL_DBM_ADJ_DEFAULT             1
#define WMI_SPECTRAL_CHN_MASK_DEFAULT            1

struct wmi_vdev_spectral_enable_cmd {
    uint32_t vdev_id;
    uint32_t trigger_cmd;
    uint32_t enable_cmd;
} __PACKED;

#define WMI_SPECTRAL_TRIGGER_CMD_TRIGGER  1
#define WMI_SPECTRAL_TRIGGER_CMD_CLEAR    2
#define WMI_SPECTRAL_ENABLE_CMD_ENABLE    1
#define WMI_SPECTRAL_ENABLE_CMD_DISABLE   2

/* Beacon processing related command and event structures */
struct wmi_bcn_tx_hdr {
    uint32_t vdev_id;
    uint32_t tx_rate;
    uint32_t tx_power;
    uint32_t bcn_len;
} __PACKED;

struct wmi_bcn_tx_cmd {
    struct wmi_bcn_tx_hdr hdr;
    uint8_t* bcn[0];
} __PACKED;

struct wmi_bcn_tx_arg {
    uint32_t vdev_id;
    uint32_t tx_rate;
    uint32_t tx_power;
    uint32_t bcn_len;
    const void* bcn;
};

enum wmi_bcn_tx_ref_flags {
    WMI_BCN_TX_REF_FLAG_DTIM_ZERO = 0x1,
    WMI_BCN_TX_REF_FLAG_DELIVER_CAB = 0x2,
};

/* TODO: It is unclear why "no antenna" works while any other seemingly valid
 * chainmask yields no beacons on the air at all.
 */
#define WMI_BCN_TX_REF_DEF_ANTENNA 0

struct wmi_bcn_tx_ref_cmd {
    uint32_t vdev_id;
    uint32_t data_len;
    /* physical address of the frame - dma pointer */
    uint32_t data_ptr;
    /* id for host to track */
    uint32_t msdu_id;
    /* frame ctrl to setup PPDU desc */
    uint32_t frame_control;
    /* to control CABQ traffic: WMI_BCN_TX_REF_FLAG_ */
    uint32_t flags;
    /* introduced in 10.2 */
    uint32_t antenna_mask;
} __PACKED;

/* Beacon filter */
#define WMI_BCN_FILTER_ALL   0 /* Filter all beacons */
#define WMI_BCN_FILTER_NONE  1 /* Pass all beacons */
#define WMI_BCN_FILTER_RSSI  2 /* Pass Beacons RSSI >= RSSI threshold */
#define WMI_BCN_FILTER_BSSID 3 /* Pass Beacons with matching BSSID */
#define WMI_BCN_FILTER_SSID  4 /* Pass Beacons with matching SSID */

struct wmi_bcn_filter_rx_cmd {
    /* Filter ID */
    uint32_t bcn_filter_id;
    /* Filter type - wmi_bcn_filter */
    uint32_t bcn_filter;
    /* Buffer len */
    uint32_t bcn_filter_len;
    /* Filter info (threshold, BSSID, RSSI) */
    uint8_t* bcn_filter_buf;
} __PACKED;

/* Capabilities and IEs to be passed to firmware */
struct wmi_bcn_prb_info {
    /* Capabilities */
    uint32_t caps;
    /* ERP info */
    uint32_t erp;
    /* Advanced capabilities */
    /* HT capabilities */
    /* HT Info */
    /* ibss_dfs */
    /* wpa Info */
    /* rsn Info */
    /* rrm info */
    /* ath_ext */
    /* app IE */
} __PACKED;

struct wmi_bcn_tmpl_cmd {
    /* unique id identifying the VDEV, generated by the caller */
    uint32_t vdev_id;
    /* TIM IE offset from the beginning of the template. */
    uint32_t tim_ie_offset;
    /* beacon probe capabilities and IEs */
    struct wmi_bcn_prb_info bcn_prb_info;
    /* beacon buffer length */
    uint32_t buf_len;
    /* variable length data */
    uint8_t data[1];
} __PACKED;

struct wmi_prb_tmpl_cmd {
    /* unique id identifying the VDEV, generated by the caller */
    uint32_t vdev_id;
    /* beacon probe capabilities and IEs */
    struct wmi_bcn_prb_info bcn_prb_info;
    /* beacon buffer length */
    uint32_t buf_len;
    /* Variable length data */
    uint8_t data[1];
} __PACKED;

enum wmi_sta_ps_mode {
    /* enable power save for the given STA VDEV */
    WMI_STA_PS_MODE_DISABLED = 0,
    /* disable power save  for a given STA VDEV */
    WMI_STA_PS_MODE_ENABLED = 1,
};

struct wmi_sta_powersave_mode_cmd {
    /* unique id identifying the VDEV, generated by the caller */
    uint32_t vdev_id;

    /*
     * Power save mode
     * (see enum wmi_sta_ps_mode)
     */
    uint32_t sta_ps_mode;
} __PACKED;

enum wmi_csa_offload_en {
    WMI_CSA_OFFLOAD_DISABLE = 0,
    WMI_CSA_OFFLOAD_ENABLE = 1,
};

struct wmi_csa_offload_enable_cmd {
    uint32_t vdev_id;
    uint32_t csa_offload_enable;
} __PACKED;

struct wmi_csa_offload_chanswitch_cmd {
    uint32_t vdev_id;
    struct wmi_channel chan;
} __PACKED;

/*
 * This parameter controls the policy for retrieving frames from AP while the
 * STA is in sleep state.
 *
 * Only takes affect if the sta_ps_mode is enabled
 */
enum wmi_sta_ps_param_rx_wake_policy {
    /*
     * Wake up when ever there is an  RX activity on the VDEV. In this mode
     * the Power save SM(state machine) will come out of sleep by either
     * sending null frame (or) a data frame (with PS==0) in response to TIM
     * bit set in the received beacon frame from AP.
     */
    WMI_STA_PS_RX_WAKE_POLICY_WAKE = 0,

    /*
     * Here the power save state machine will not wakeup in response to TIM
     * bit, instead it will send a PSPOLL (or) UASPD trigger based on UAPSD
     * configuration setup by WMISET_PS_SET_UAPSD  WMI command.  When all
     * access categories are delivery-enabled, the station will send a
     * UAPSD trigger frame, otherwise it will send a PS-Poll.
     */
    WMI_STA_PS_RX_WAKE_POLICY_POLL_UAPSD = 1,
};

/*
 * Number of tx frames/beacon  that cause the power save SM to wake up.
 *
 * Value 1 causes the SM to wake up for every TX. Value 0 has a special
 * meaning, It will cause the SM to never wake up. This is useful if you want
 * to keep the system to sleep all the time for some kind of test mode . host
 * can change this parameter any time.  It will affect at the next tx frame.
 */
enum wmi_sta_ps_param_tx_wake_threshold {
    WMI_STA_PS_TX_WAKE_THRESHOLD_NEVER = 0,
    WMI_STA_PS_TX_WAKE_THRESHOLD_ALWAYS = 1,

    /*
     * Values greater than one indicate that many TX attempts per beacon
     * interval before the STA will wake up
     */
};

/*
 * The maximum number of PS-Poll frames the FW will send in response to
 * traffic advertised in TIM before waking up (by sending a null frame with PS
 * = 0). Value 0 has a special meaning: there is no maximum count and the FW
 * will send as many PS-Poll as are necessary to retrieve buffered BU. This
 * parameter is used when the RX wake policy is
 * WMI_STA_PS_RX_WAKE_POLICY_POLL_UAPSD and ignored when the RX wake
 * policy is WMI_STA_PS_RX_WAKE_POLICY_WAKE.
 */
enum wmi_sta_ps_param_pspoll_count {
    WMI_STA_PS_PSPOLL_COUNT_NO_MAX = 0,
    /*
     * Values greater than 0 indicate the maximum numer of PS-Poll frames
     * FW will send before waking up.
     */

    /* When u-APSD is enabled the firmware will be very reluctant to exit
     * STA PS. This could result in very poor Rx performance with STA doing
     * PS-Poll for each and every buffered frame. This value is a bit
     * arbitrary.
     */
    WMI_STA_PS_PSPOLL_COUNT_UAPSD = 3,
};

/*
 * This will include the delivery and trigger enabled state for every AC.
 * This is the negotiated state with AP. The host MLME needs to set this based
 * on AP capability and the state Set in the association request by the
 * station MLME.Lower 8 bits of the value specify the UAPSD configuration.
 */
#define WMI_UAPSD_AC_TYPE_DELI 0
#define WMI_UAPSD_AC_TYPE_TRIG 1

#define WMI_UAPSD_AC_BIT_MASK(ac, type) \
    (type == WMI_UAPSD_AC_TYPE_DELI ? 1 << (ac << 1) : 1 << ((ac << 1) + 1))

enum wmi_sta_ps_param_uapsd {
    WMI_STA_PS_UAPSD_AC0_DELIVERY_EN = (1 << 0),
    WMI_STA_PS_UAPSD_AC0_TRIGGER_EN  = (1 << 1),
    WMI_STA_PS_UAPSD_AC1_DELIVERY_EN = (1 << 2),
    WMI_STA_PS_UAPSD_AC1_TRIGGER_EN  = (1 << 3),
    WMI_STA_PS_UAPSD_AC2_DELIVERY_EN = (1 << 4),
    WMI_STA_PS_UAPSD_AC2_TRIGGER_EN  = (1 << 5),
    WMI_STA_PS_UAPSD_AC3_DELIVERY_EN = (1 << 6),
    WMI_STA_PS_UAPSD_AC3_TRIGGER_EN  = (1 << 7),
};

#define WMI_STA_UAPSD_MAX_INTERVAL_MSEC UINT_MAX

struct wmi_sta_uapsd_auto_trig_param {
    uint32_t wmm_ac;
    uint32_t user_priority;
    uint32_t service_interval;
    uint32_t suspend_interval;
    uint32_t delay_interval;
};

struct wmi_sta_uapsd_auto_trig_cmd_fixed_param {
    uint32_t vdev_id;
    struct wmi_mac_addr peer_macaddr;
    uint32_t num_ac;
};

struct wmi_sta_uapsd_auto_trig_arg {
    uint32_t wmm_ac;
    uint32_t user_priority;
    uint32_t service_interval;
    uint32_t suspend_interval;
    uint32_t delay_interval;
};

enum wmi_sta_powersave_param {
    /*
     * Controls how frames are retrievd from AP while STA is sleeping
     *
     * (see enum wmi_sta_ps_param_rx_wake_policy)
     */
    WMI_STA_PS_PARAM_RX_WAKE_POLICY = 0,

    /*
     * The STA will go active after this many TX
     *
     * (see enum wmi_sta_ps_param_tx_wake_threshold)
     */
    WMI_STA_PS_PARAM_TX_WAKE_THRESHOLD = 1,

    /*
     * Number of PS-Poll to send before STA wakes up
     *
     * (see enum wmi_sta_ps_param_pspoll_count)
     *
     */
    WMI_STA_PS_PARAM_PSPOLL_COUNT = 2,

    /*
     * TX/RX inactivity time in msec before going to sleep.
     *
     * The power save SM will monitor tx/rx activity on the VDEV, if no
     * activity for the specified msec of the parameter the Power save
     * SM will go to sleep.
     */
    WMI_STA_PS_PARAM_INACTIVITY_TIME = 3,

    /*
     * Set uapsd configuration.
     *
     * (see enum wmi_sta_ps_param_uapsd)
     */
    WMI_STA_PS_PARAM_UAPSD = 4,
};

struct wmi_sta_powersave_param_cmd {
    uint32_t vdev_id;
    uint32_t param_id; /* %WMI_STA_PS_PARAM_ */
    uint32_t param_value;
} __PACKED;

/* No MIMO power save */
#define WMI_STA_MIMO_PS_MODE_DISABLE
/* mimo powersave mode static*/
#define WMI_STA_MIMO_PS_MODE_STATIC
/* mimo powersave mode dynamic */
#define WMI_STA_MIMO_PS_MODE_DYNAMIC

struct wmi_sta_mimo_ps_mode_cmd {
    /* unique id identifying the VDEV, generated by the caller */
    uint32_t vdev_id;
    /* mimo powersave mode as defined above */
    uint32_t mimo_pwrsave_mode;
} __PACKED;

/* U-APSD configuration of peer station from (re)assoc request and TSPECs */
enum wmi_ap_ps_param_uapsd {
    WMI_AP_PS_UAPSD_AC0_DELIVERY_EN = (1 << 0),
    WMI_AP_PS_UAPSD_AC0_TRIGGER_EN  = (1 << 1),
    WMI_AP_PS_UAPSD_AC1_DELIVERY_EN = (1 << 2),
    WMI_AP_PS_UAPSD_AC1_TRIGGER_EN  = (1 << 3),
    WMI_AP_PS_UAPSD_AC2_DELIVERY_EN = (1 << 4),
    WMI_AP_PS_UAPSD_AC2_TRIGGER_EN  = (1 << 5),
    WMI_AP_PS_UAPSD_AC3_DELIVERY_EN = (1 << 6),
    WMI_AP_PS_UAPSD_AC3_TRIGGER_EN  = (1 << 7),
};

/* U-APSD maximum service period of peer station */
enum wmi_ap_ps_peer_param_max_sp {
    WMI_AP_PS_PEER_PARAM_MAX_SP_UNLIMITED = 0,
    WMI_AP_PS_PEER_PARAM_MAX_SP_2 = 1,
    WMI_AP_PS_PEER_PARAM_MAX_SP_4 = 2,
    WMI_AP_PS_PEER_PARAM_MAX_SP_6 = 3,
    MAX_WMI_AP_PS_PEER_PARAM_MAX_SP,
};

/*
 * AP power save parameter
 * Set a power save specific parameter for a peer station
 */
enum wmi_ap_ps_peer_param {
    /* Set uapsd configuration for a given peer.
     *
     * Include the delivery and trigger enabled state for every AC.
     * The host  MLME needs to set this based on AP capability and stations
     * request Set in the association request  received from the station.
     *
     * Lower 8 bits of the value specify the UAPSD configuration.
     *
     * (see enum wmi_ap_ps_param_uapsd)
     * The default value is 0.
     */
    WMI_AP_PS_PEER_PARAM_UAPSD = 0,

    /*
     * Set the service period for a UAPSD capable station
     *
     * The service period from wme ie in the (re)assoc request frame.
     *
     * (see enum wmi_ap_ps_peer_param_max_sp)
     */
    WMI_AP_PS_PEER_PARAM_MAX_SP = 1,

    /* Time in seconds for aging out buffered frames for STA in PS */
    WMI_AP_PS_PEER_PARAM_AGEOUT_TIME = 2,
};

struct wmi_ap_ps_peer_cmd {
    /* unique id identifying the VDEV, generated by the caller */
    uint32_t vdev_id;

    /* peer MAC address */
    struct wmi_mac_addr peer_macaddr;

    /* AP powersave param (see enum wmi_ap_ps_peer_param) */
    uint32_t param_id;

    /* AP powersave param value */
    uint32_t param_value;
} __PACKED;

/* 128 clients = 4 words */
#define WMI_TIM_BITMAP_ARRAY_SIZE 4

struct wmi_tim_info {
    uint32_t tim_len;
    uint32_t tim_mcast;
    uint32_t tim_bitmap[WMI_TIM_BITMAP_ARRAY_SIZE];
    uint32_t tim_changed;
    uint32_t tim_num_ps_pending;
} __PACKED;

struct wmi_tim_info_arg {
    uint32_t tim_len;
    uint32_t tim_mcast;
    const uint32_t* tim_bitmap;
    uint32_t tim_changed;
    uint32_t tim_num_ps_pending;
} __PACKED;

/* Maximum number of NOA Descriptors supported */
#define WMI_P2P_MAX_NOA_DESCRIPTORS 4
#define WMI_P2P_OPPPS_ENABLE_BIT    (1 << 0)
#define WMI_P2P_OPPPS_CTWINDOW_OFFSET   1
#define WMI_P2P_NOA_CHANGED_BIT (1 << 0)

struct wmi_p2p_noa_info {
    /* Bit 0 - Flag to indicate an update in NOA schedule
     * Bits 7-1 - Reserved
     */
    uint8_t changed;
    /* NOA index */
    uint8_t index;
    /* Bit 0 - Opp PS state of the AP
     * Bits 1-7 - Ctwindow in TUs
     */
    uint8_t ctwindow_oppps;
    /* Number of NOA descriptors */
    uint8_t num_descriptors;

    struct wmi_p2p_noa_descriptor descriptors[WMI_P2P_MAX_NOA_DESCRIPTORS];
} __PACKED;

struct wmi_bcn_info {
    struct wmi_tim_info tim_info;
    struct wmi_p2p_noa_info p2p_noa_info;
} __PACKED;

struct wmi_host_swba_event {
    uint32_t vdev_map;
    struct wmi_bcn_info bcn_info[0];
} __PACKED;

struct wmi_10_2_4_bcn_info {
    struct wmi_tim_info tim_info;
    /* The 10.2.4 FW doesn't have p2p NOA info */
} __PACKED;

struct wmi_10_2_4_host_swba_event {
    uint32_t vdev_map;
    struct wmi_10_2_4_bcn_info bcn_info[0];
} __PACKED;

/* 16 words = 512 client + 1 word = for guard */
#define WMI_10_4_TIM_BITMAP_ARRAY_SIZE 17

struct wmi_10_4_tim_info {
    uint32_t tim_len;
    uint32_t tim_mcast;
    uint32_t tim_bitmap[WMI_10_4_TIM_BITMAP_ARRAY_SIZE];
    uint32_t tim_changed;
    uint32_t tim_num_ps_pending;
} __PACKED;

#define WMI_10_4_P2P_MAX_NOA_DESCRIPTORS 1

struct wmi_10_4_p2p_noa_info {
    /* Bit 0 - Flag to indicate an update in NOA schedule
     * Bits 7-1 - Reserved
     */
    uint8_t changed;
    /* NOA index */
    uint8_t index;
    /* Bit 0 - Opp PS state of the AP
     * Bits 1-7 - Ctwindow in TUs
     */
    uint8_t ctwindow_oppps;
    /* Number of NOA descriptors */
    uint8_t num_descriptors;

    struct wmi_p2p_noa_descriptor
        noa_descriptors[WMI_10_4_P2P_MAX_NOA_DESCRIPTORS];
} __PACKED;

struct wmi_10_4_bcn_info {
    struct wmi_10_4_tim_info tim_info;
    struct wmi_10_4_p2p_noa_info p2p_noa_info;
} __PACKED;

struct wmi_10_4_host_swba_event {
    uint32_t vdev_map;
    struct wmi_10_4_bcn_info bcn_info[0];
} __PACKED;

#define WMI_MAX_AP_VDEV 16

struct wmi_tbtt_offset_event {
    uint32_t vdev_map;
    uint32_t tbttoffset_list[WMI_MAX_AP_VDEV];
} __PACKED;

struct wmi_peer_create_cmd {
    uint32_t vdev_id;
    struct wmi_mac_addr peer_macaddr;
} __PACKED;

enum wmi_peer_type {
    WMI_PEER_TYPE_DEFAULT = 0,
    WMI_PEER_TYPE_BSS = 1,
    WMI_PEER_TYPE_TDLS = 2,
};

struct wmi_peer_delete_cmd {
    uint32_t vdev_id;
    struct wmi_mac_addr peer_macaddr;
} __PACKED;

struct wmi_peer_flush_tids_cmd {
    uint32_t vdev_id;
    struct wmi_mac_addr peer_macaddr;
    uint32_t peer_tid_bitmap;
} __PACKED;

struct wmi_fixed_rate {
    /*
     * rate mode . 0: disable fixed rate (auto rate)
     *   1: legacy (non 11n) rate  specified as ieee rate 2*Mbps
     *   2: ht20 11n rate  specified as mcs index
     *   3: ht40 11n rate  specified as mcs index
     */
    uint32_t  rate_mode;
    /*
     * 4 rate values for 4 rate series. series 0 is stored in byte 0 (LSB)
     * and series 3 is stored at byte 3 (MSB)
     */
    uint32_t  rate_series;
    /*
     * 4 retry counts for 4 rate series. retry count for rate 0 is stored
     * in byte 0 (LSB) and retry count for rate 3 is stored at byte 3
     * (MSB)
     */
    uint32_t  rate_retries;
} __PACKED;

struct wmi_peer_fixed_rate_cmd {
    /* unique id identifying the VDEV, generated by the caller */
    uint32_t vdev_id;
    /* peer MAC address */
    struct wmi_mac_addr peer_macaddr;
    /* fixed rate */
    struct wmi_fixed_rate peer_fixed_rate;
} __PACKED;

#define WMI_MGMT_TID    17

struct wmi_addba_clear_resp_cmd {
    /* unique id identifying the VDEV, generated by the caller */
    uint32_t vdev_id;
    /* peer MAC address */
    struct wmi_mac_addr peer_macaddr;
} __PACKED;

struct wmi_addba_send_cmd {
    /* unique id identifying the VDEV, generated by the caller */
    uint32_t vdev_id;
    /* peer MAC address */
    struct wmi_mac_addr peer_macaddr;
    /* Tid number */
    uint32_t tid;
    /* Buffer/Window size*/
    uint32_t buffersize;
} __PACKED;

struct wmi_delba_send_cmd {
    /* unique id identifying the VDEV, generated by the caller */
    uint32_t vdev_id;
    /* peer MAC address */
    struct wmi_mac_addr peer_macaddr;
    /* Tid number */
    uint32_t tid;
    /* Is Initiator */
    uint32_t initiator;
    /* Reason code */
    uint32_t reasoncode;
} __PACKED;

struct wmi_addba_setresponse_cmd {
    /* unique id identifying the vdev, generated by the caller */
    uint32_t vdev_id;
    /* peer mac address */
    struct wmi_mac_addr peer_macaddr;
    /* Tid number */
    uint32_t tid;
    /* status code */
    uint32_t statuscode;
} __PACKED;

struct wmi_send_singleamsdu_cmd {
    /* unique id identifying the vdev, generated by the caller */
    uint32_t vdev_id;
    /* peer mac address */
    struct wmi_mac_addr peer_macaddr;
    /* Tid number */
    uint32_t tid;
} __PACKED;

enum wmi_peer_smps_state {
    WMI_PEER_SMPS_PS_NONE = 0x0,
    WMI_PEER_SMPS_STATIC  = 0x1,
    WMI_PEER_SMPS_DYNAMIC = 0x2
};

enum wmi_peer_chwidth {
    WMI_PEER_CHWIDTH_20MHZ = 0,
    WMI_PEER_CHWIDTH_40MHZ = 1,
    WMI_PEER_CHWIDTH_80MHZ = 2,
    WMI_PEER_CHWIDTH_160MHZ = 3,
};

enum wmi_peer_param {
    WMI_PEER_SMPS_STATE = 0x1, /* see %wmi_peer_smps_state */
    WMI_PEER_AMPDU      = 0x2,
    WMI_PEER_AUTHORIZE  = 0x3,
    WMI_PEER_CHAN_WIDTH = 0x4,
    WMI_PEER_NSS        = 0x5,
    WMI_PEER_USE_4ADDR  = 0x6,
    WMI_PEER_DEBUG      = 0xa,
    WMI_PEER_DUMMY_VAR  = 0xff, /* dummy parameter for STA PS workaround */
};

struct wmi_peer_set_param_cmd {
    uint32_t vdev_id;
    struct wmi_mac_addr peer_macaddr;
    uint32_t param_id;
    uint32_t param_value;
} __PACKED;

#define MAX_SUPPORTED_RATES 128

struct wmi_rate_set {
    /* total number of rates */
    uint32_t num_rates;
    /*
     * rates (each 8bit value) packed into a 32 bit word.
     * the rates are filled from least significant byte to most
     * significant byte.
     */
    uint32_t rates[(MAX_SUPPORTED_RATES / 4) + 1];
} __PACKED;

struct wmi_rate_set_arg {
    unsigned int num_rates;
    uint8_t rates[MAX_SUPPORTED_RATES];
};

/*
 * NOTE: It would bea good idea to represent the Tx MCS
 * info in one word and Rx in another word. This is split
 * into multiple words for convenience
 */
struct wmi_vht_rate_set {
    uint32_t rx_max_rate; /* Max Rx data rate */
    uint32_t rx_mcs_set;  /* Negotiated RX VHT rates */
    uint32_t tx_max_rate; /* Max Tx data rate */
    uint32_t tx_mcs_set;  /* Negotiated TX VHT rates */
} __PACKED;

struct wmi_vht_rate_set_arg {
    uint32_t rx_max_rate;
    uint32_t rx_mcs_set;
    uint32_t tx_max_rate;
    uint32_t tx_mcs_set;
};

struct wmi_peer_set_rates_cmd {
    /* peer MAC address */
    struct wmi_mac_addr peer_macaddr;
    /* legacy rate set */
    struct wmi_rate_set peer_legacy_rates;
    /* ht rate set */
    struct wmi_rate_set peer_ht_rates;
} __PACKED;

struct wmi_peer_set_q_empty_callback_cmd {
    /* unique id identifying the VDEV, generated by the caller */
    uint32_t vdev_id;
    /* peer MAC address */
    struct wmi_mac_addr peer_macaddr;
    uint32_t callback_enable;
} __PACKED;

struct wmi_peer_flags_map {
    uint32_t auth;
    uint32_t qos;
    uint32_t need_ptk_4_way;
    uint32_t need_gtk_2_way;
    uint32_t apsd;
    uint32_t ht;
    uint32_t bw40;
    uint32_t stbc;
    uint32_t ldbc;
    uint32_t dyn_mimops;
    uint32_t static_mimops;
    uint32_t spatial_mux;
    uint32_t vht;
    uint32_t bw80;
    uint32_t vht_2g;
    uint32_t pmf;
    uint32_t bw160;
};

enum wmi_peer_flags {
    WMI_PEER_AUTH = 0x00000001,
    WMI_PEER_QOS = 0x00000002,
    WMI_PEER_NEED_PTK_4_WAY = 0x00000004,
    WMI_PEER_NEED_GTK_2_WAY = 0x00000010,
    WMI_PEER_APSD = 0x00000800,
    WMI_PEER_HT = 0x00001000,
    WMI_PEER_40MHZ = 0x00002000,
    WMI_PEER_STBC = 0x00008000,
    WMI_PEER_LDPC = 0x00010000,
    WMI_PEER_DYN_MIMOPS = 0x00020000,
    WMI_PEER_STATIC_MIMOPS = 0x00040000,
    WMI_PEER_SPATIAL_MUX = 0x00200000,
    WMI_PEER_VHT = 0x02000000,
    WMI_PEER_80MHZ = 0x04000000,
    WMI_PEER_VHT_2G = 0x08000000,
    WMI_PEER_PMF = 0x10000000,
    WMI_PEER_160MHZ = 0x20000000
};

enum wmi_10x_peer_flags {
    WMI_10X_PEER_AUTH = 0x00000001,
    WMI_10X_PEER_QOS = 0x00000002,
    WMI_10X_PEER_NEED_PTK_4_WAY = 0x00000004,
    WMI_10X_PEER_NEED_GTK_2_WAY = 0x00000010,
    WMI_10X_PEER_APSD = 0x00000800,
    WMI_10X_PEER_HT = 0x00001000,
    WMI_10X_PEER_40MHZ = 0x00002000,
    WMI_10X_PEER_STBC = 0x00008000,
    WMI_10X_PEER_LDPC = 0x00010000,
    WMI_10X_PEER_DYN_MIMOPS = 0x00020000,
    WMI_10X_PEER_STATIC_MIMOPS = 0x00040000,
    WMI_10X_PEER_SPATIAL_MUX = 0x00200000,
    WMI_10X_PEER_VHT = 0x02000000,
    WMI_10X_PEER_80MHZ = 0x04000000,
    WMI_10X_PEER_160MHZ = 0x20000000
};

enum wmi_10_2_peer_flags {
    WMI_10_2_PEER_AUTH = 0x00000001,
    WMI_10_2_PEER_QOS = 0x00000002,
    WMI_10_2_PEER_NEED_PTK_4_WAY = 0x00000004,
    WMI_10_2_PEER_NEED_GTK_2_WAY = 0x00000010,
    WMI_10_2_PEER_APSD = 0x00000800,
    WMI_10_2_PEER_HT = 0x00001000,
    WMI_10_2_PEER_40MHZ = 0x00002000,
    WMI_10_2_PEER_STBC = 0x00008000,
    WMI_10_2_PEER_LDPC = 0x00010000,
    WMI_10_2_PEER_DYN_MIMOPS = 0x00020000,
    WMI_10_2_PEER_STATIC_MIMOPS = 0x00040000,
    WMI_10_2_PEER_SPATIAL_MUX = 0x00200000,
    WMI_10_2_PEER_VHT = 0x02000000,
    WMI_10_2_PEER_80MHZ = 0x04000000,
    WMI_10_2_PEER_VHT_2G = 0x08000000,
    WMI_10_2_PEER_PMF = 0x10000000,
    WMI_10_2_PEER_160MHZ = 0x20000000
};

/*
 * Peer rate capabilities.
 *
 * This is of interest to the ratecontrol
 * module which resides in the firmware. The bit definitions are
 * consistent with that defined in if_athrate.c.
 */
#define WMI_RC_DS_FLAG          0x01
#define WMI_RC_CW40_FLAG        0x02
#define WMI_RC_SGI_FLAG         0x04
#define WMI_RC_HT_FLAG          0x08
#define WMI_RC_RTSCTS_FLAG      0x10
#define WMI_RC_TX_STBC_FLAG     0x20
#define WMI_RC_RX_STBC_FLAG     0xC0
#define WMI_RC_RX_STBC_FLAG_S   6
#define WMI_RC_WEP_TKIP_FLAG    0x100
#define WMI_RC_TS_FLAG          0x200
#define WMI_RC_UAPSD_FLAG       0x400

/* Maximum listen interval supported by hw in units of beacon interval */
#define ATH10K_MAX_HW_LISTEN_INTERVAL 5

struct wmi_common_peer_assoc_complete_cmd {
    struct wmi_mac_addr peer_macaddr;
    uint32_t vdev_id;
    uint32_t peer_new_assoc; /* 1=assoc, 0=reassoc */
    uint32_t peer_associd; /* 16 LSBs */
    uint32_t peer_flags;
    uint32_t peer_caps; /* 16 LSBs */
    uint32_t peer_listen_intval;
    uint32_t peer_ht_caps;
    uint32_t peer_max_mpdu;
    uint32_t peer_mpdu_density; /* 0..16 */
    uint32_t peer_rate_caps;
    struct wmi_rate_set peer_legacy_rates;
    struct wmi_rate_set peer_ht_rates;
    uint32_t peer_nss; /* num of spatial streams */
    uint32_t peer_vht_caps;
    uint32_t peer_phymode;
    struct wmi_vht_rate_set peer_vht_rates;
};

struct wmi_main_peer_assoc_complete_cmd {
    struct wmi_common_peer_assoc_complete_cmd cmd;

    /* HT Operation Element of the peer. Five bytes packed in 2
     *  INT32 array and filled from lsb to msb.
     */
    uint32_t peer_ht_info[2];
} __PACKED;

struct wmi_10_1_peer_assoc_complete_cmd {
    struct wmi_common_peer_assoc_complete_cmd cmd;
} __PACKED;

#define WMI_PEER_ASSOC_INFO0_MAX_MCS_IDX_LSB 0
#define WMI_PEER_ASSOC_INFO0_MAX_MCS_IDX_MASK 0x0f
#define WMI_PEER_ASSOC_INFO0_MAX_NSS_LSB 4
#define WMI_PEER_ASSOC_INFO0_MAX_NSS_MASK 0xf0

struct wmi_10_2_peer_assoc_complete_cmd {
    struct wmi_common_peer_assoc_complete_cmd cmd;
    uint32_t info0; /* WMI_PEER_ASSOC_INFO0_ */
} __PACKED;

#define PEER_BW_RXNSS_OVERRIDE_OFFSET  31

struct wmi_10_4_peer_assoc_complete_cmd {
    struct wmi_10_2_peer_assoc_complete_cmd cmd;
    uint32_t peer_bw_rxnss_override;
} __PACKED;

struct wmi_peer_assoc_complete_arg {
    uint8_t addr[ETH_ALEN];
    uint32_t vdev_id;
    bool peer_reassoc;
    uint16_t peer_aid;
    uint32_t peer_flags; /* see %WMI_PEER_ */
    uint16_t peer_caps;
    uint32_t peer_listen_intval;
    uint32_t peer_ht_caps;
    uint32_t peer_max_mpdu;
    uint32_t peer_mpdu_density; /* 0..16 */
    uint32_t peer_rate_caps; /* see %WMI_RC_ */
    struct wmi_rate_set_arg peer_legacy_rates;
    struct wmi_rate_set_arg peer_ht_rates;
    uint32_t peer_num_spatial_streams;
    uint32_t peer_vht_caps;
    enum wmi_phy_mode peer_phymode;
    struct wmi_vht_rate_set_arg peer_vht_rates;
    uint32_t peer_bw_rxnss_override;
};

struct wmi_peer_add_wds_entry_cmd {
    /* peer MAC address */
    struct wmi_mac_addr peer_macaddr;
    /* wds MAC addr */
    struct wmi_mac_addr wds_macaddr;
} __PACKED;

struct wmi_peer_remove_wds_entry_cmd {
    /* wds MAC addr */
    struct wmi_mac_addr wds_macaddr;
} __PACKED;

struct wmi_peer_q_empty_callback_event {
    /* peer MAC address */
    struct wmi_mac_addr peer_macaddr;
} __PACKED;

/*
 * Channel info WMI event
 */
struct wmi_chan_info_event {
    uint32_t err_code;
    uint32_t freq;
    uint32_t cmd_flags;
    uint32_t noise_floor;
    uint32_t rx_clear_count;
    uint32_t cycle_count;
} __PACKED;

struct wmi_10_4_chan_info_event {
    uint32_t err_code;
    uint32_t freq;
    uint32_t cmd_flags;
    uint32_t noise_floor;
    uint32_t rx_clear_count;
    uint32_t cycle_count;
    uint32_t chan_tx_pwr_range;
    uint32_t chan_tx_pwr_tp;
    uint32_t rx_frame_count;
} __PACKED;

struct wmi_peer_sta_kickout_event {
    struct wmi_mac_addr peer_macaddr;
} __PACKED;

#define WMI_CHAN_INFO_FLAG_COMPLETE (1 << 0)
#define WMI_CHAN_INFO_FLAG_PRE_COMPLETE (1 << 1)

/* Beacon filter wmi command info */
#define BCN_FLT_MAX_SUPPORTED_IES   256
#define BCN_FLT_MAX_ELEMS_IE_LIST   (BCN_FLT_MAX_SUPPORTED_IES / 32)

struct bss_bcn_stats {
    uint32_t vdev_id;
    uint32_t bss_bcnsdropped;
    uint32_t bss_bcnsdelivered;
} __PACKED;

struct bcn_filter_stats {
    uint32_t bcns_dropped;
    uint32_t bcns_delivered;
    uint32_t activefilters;
    struct bss_bcn_stats bss_stats;
} __PACKED;

struct wmi_add_bcn_filter_cmd {
    uint32_t vdev_id;
    uint32_t ie_map[BCN_FLT_MAX_ELEMS_IE_LIST];
} __PACKED;

enum wmi_sta_keepalive_method {
    WMI_STA_KEEPALIVE_METHOD_NULL_FRAME = 1,
    WMI_STA_KEEPALIVE_METHOD_UNSOLICITATED_ARP_RESPONSE = 2,
};

#define WMI_STA_KEEPALIVE_INTERVAL_DISABLE 0

/* Firmware crashes if keepalive interval exceeds this limit */
#define WMI_STA_KEEPALIVE_INTERVAL_MAX_SECONDS 0xffff

/* note: ip4 addresses are in network byte order, i.e. big endian */
struct wmi_sta_keepalive_arp_resp {
    uint8_t src_ip4_addr[4];  // network byte order
    uint8_t dest_ip4_addr[4]; // network byte order
    struct wmi_mac_addr dest_mac_addr;
} __PACKED;

struct wmi_sta_keepalive_cmd {
    uint32_t vdev_id;
    uint32_t enabled;
    uint32_t method; /* WMI_STA_KEEPALIVE_METHOD_ */
    uint32_t interval; /* in seconds */
    struct wmi_sta_keepalive_arp_resp arp_resp;
} __PACKED;

struct wmi_sta_keepalive_arg {
    uint32_t vdev_id;
    uint32_t enabled;
    uint32_t method;
    uint32_t interval;
    uint8_t src_ip4_addr[4];  // network byte order
    uint8_t dest_ip4_addr[4]; // network byte order
    const uint8_t dest_mac_addr[ETH_ALEN];
};

enum wmi_force_fw_hang_type {
    WMI_FORCE_FW_HANG_ASSERT = 1,
    WMI_FORCE_FW_HANG_NO_DETECT,
    WMI_FORCE_FW_HANG_CTRL_EP_FULL,
    WMI_FORCE_FW_HANG_EMPTY_POINT,
    WMI_FORCE_FW_HANG_STACK_OVERFLOW,
    WMI_FORCE_FW_HANG_INFINITE_LOOP,
};

#define WMI_FORCE_FW_HANG_RANDOM_TIME 0xFFFFFFFF

struct wmi_force_fw_hang_cmd {
    uint32_t type;
    uint32_t delay_ms;
} __PACKED;

enum ath10k_dbglog_level {
    ATH10K_DBGLOG_LEVEL_VERBOSE = 0,
    ATH10K_DBGLOG_LEVEL_INFO = 1,
    ATH10K_DBGLOG_LEVEL_WARN = 2,
    ATH10K_DBGLOG_LEVEL_ERR = 3,
};

/* VAP ids to enable dbglog */
#define ATH10K_DBGLOG_CFG_VAP_LOG_LSB       0
#define ATH10K_DBGLOG_CFG_VAP_LOG_MASK      0x0000ffff

/* to enable dbglog in the firmware */
#define ATH10K_DBGLOG_CFG_REPORTING_ENABLE_LSB  16
#define ATH10K_DBGLOG_CFG_REPORTING_ENABLE_MASK 0x00010000

/* timestamp resolution */
#define ATH10K_DBGLOG_CFG_RESOLUTION_LSB    17
#define ATH10K_DBGLOG_CFG_RESOLUTION_MASK   0x000E0000

/* number of queued messages before sending them to the host */
#define ATH10K_DBGLOG_CFG_REPORT_SIZE_LSB   20
#define ATH10K_DBGLOG_CFG_REPORT_SIZE_MASK  0x0ff00000

/*
 * Log levels to enable. This defines the minimum level to enable, this is
 * not a bitmask. See enum ath10k_dbglog_level for the values.
 */
#define ATH10K_DBGLOG_CFG_LOG_LVL_LSB       28
#define ATH10K_DBGLOG_CFG_LOG_LVL_MASK      0x70000000

/*
 * Note: this is a cleaned up version of a struct firmware uses. For
 * example, config_valid was hidden inside an array.
 */
struct wmi_dbglog_cfg_cmd {
    /* bitmask to hold mod id config*/
    uint32_t module_enable;

    /* see ATH10K_DBGLOG_CFG_ */
    uint32_t config_enable;

    /* mask of module id bits to be changed */
    uint32_t module_valid;

    /* mask of config bits to be changed, see ATH10K_DBGLOG_CFG_ */
    uint32_t config_valid;
} __PACKED;

struct wmi_10_4_dbglog_cfg_cmd {
    /* bitmask to hold mod id config*/
    uint64_t module_enable;

    /* see ATH10K_DBGLOG_CFG_ */
    uint32_t config_enable;

    /* mask of module id bits to be changed */
    uint64_t module_valid;

    /* mask of config bits to be changed, see ATH10K_DBGLOG_CFG_ */
    uint32_t config_valid;
} __PACKED;

enum wmi_roam_reason {
    WMI_ROAM_REASON_BETTER_AP = 1,
    WMI_ROAM_REASON_BEACON_MISS = 2,
    WMI_ROAM_REASON_LOW_RSSI = 3,
    WMI_ROAM_REASON_SUITABLE_AP_FOUND = 4,
    WMI_ROAM_REASON_HO_FAILED = 5,

    /* keep last */
    WMI_ROAM_REASON_MAX,
};

struct wmi_roam_ev {
    uint32_t vdev_id;
    uint32_t reason;
} __PACKED;

#define ATH10K_FRAGMT_THRESHOLD_MIN 540
#define ATH10K_FRAGMT_THRESHOLD_MAX 2346

#define WMI_MAX_EVENT 0x1000
/* Maximum number of pending TXed WMI packets */
#define WMI_SKB_HEADROOM sizeof(struct wmi_cmd_hdr)

/* By default disable power save for IBSS */
#define ATH10K_DEFAULT_ATIM 0

#define WMI_MAX_MEM_REQS 16

struct wmi_scan_ev_arg {
    uint32_t event_type; /* %WMI_SCAN_EVENT_ */
    uint32_t reason; /* %WMI_SCAN_REASON_ */
    uint32_t channel_freq; /* only valid for WMI_SCAN_EVENT_FOREIGN_CHANNEL */
    uint32_t scan_req_id;
    uint32_t scan_id;
    uint32_t vdev_id;
};

struct wmi_mgmt_rx_ev_arg {
    uint32_t channel;
    uint32_t snr;
    uint32_t rate;
    uint32_t phy_mode;
    uint32_t buf_len;
    uint32_t status; /* %WMI_RX_STATUS_ */
    struct wmi_mgmt_rx_ext_info ext_info;
};

struct wmi_ch_info_ev_arg {
    uint32_t err_code;
    uint32_t freq;
    uint32_t cmd_flags;
    uint32_t noise_floor;
    uint32_t rx_clear_count;
    uint32_t cycle_count;
    uint32_t chan_tx_pwr_range;
    uint32_t chan_tx_pwr_tp;
    uint32_t rx_frame_count;
};

struct wmi_vdev_start_ev_arg {
    uint32_t vdev_id;
    uint32_t req_id;
    uint32_t resp_type; /* %WMI_VDEV_RESP_ */
    uint32_t status;
};

struct wmi_peer_kick_ev_arg {
    const uint8_t* mac_addr;
};

struct wmi_swba_ev_arg {
    uint32_t vdev_map;
    struct wmi_tim_info_arg tim_info[WMI_MAX_AP_VDEV];
    const struct wmi_p2p_noa_info* noa_info[WMI_MAX_AP_VDEV];
};

struct wmi_phyerr_ev_arg {
    uint32_t tsf_timestamp;
    uint16_t freq1;
    uint16_t freq2;
    uint8_t rssi_combined;
    uint8_t chan_width_mhz;
    uint8_t phy_err_code;
    uint16_t nf_chains[4];
    uint32_t buf_len;
    const uint8_t* buf;
    uint8_t hdr_len;
};

struct wmi_phyerr_hdr_arg {
    uint32_t num_phyerrs;
    uint32_t tsf_l32;
    uint32_t tsf_u32;
    uint32_t buf_len;
    const void* phyerrs;
};

struct wmi_svc_rdy_ev_arg {
    uint32_t min_tx_power;
    uint32_t max_tx_power;
    uint32_t ht_cap;
    uint32_t vht_cap;
    uint32_t sw_ver0;
    uint32_t sw_ver1;
    uint32_t fw_build;
    uint32_t phy_capab;
    uint32_t num_rf_chains;
    uint32_t eeprom_rd;
    uint32_t num_mem_reqs;
    uint32_t low_5ghz_chan;
    uint32_t high_5ghz_chan;
    const uint32_t* service_map;
    size_t service_map_len;
    const struct wlan_host_mem_req* mem_reqs[WMI_MAX_MEM_REQS];
};

struct wmi_rdy_ev_arg {
    uint32_t sw_version;
    uint32_t abi_version;
    uint32_t status;
    const uint8_t* mac_addr;
};

struct wmi_roam_ev_arg {
    uint32_t vdev_id;
    uint32_t reason;
    uint32_t rssi;
};

struct wmi_echo_ev_arg {
    uint32_t value;
};

struct wmi_pdev_temperature_event {
    /* temperature value in Celcius degree */
    uint32_t temperature;
} __PACKED;

struct wmi_pdev_bss_chan_info_event {
    uint32_t freq;
    uint32_t noise_floor;
    uint64_t cycle_busy;
    uint64_t cycle_total;
    uint64_t cycle_tx;
    uint64_t cycle_rx;
    uint64_t cycle_rx_bss;
    uint32_t reserved;
} __PACKED;

/* WOW structures */
enum wmi_wow_wakeup_event {
    WOW_BMISS_EVENT = 0,
    WOW_BETTER_AP_EVENT,
    WOW_DEAUTH_RECVD_EVENT,
    WOW_MAGIC_PKT_RECVD_EVENT,
    WOW_GTK_ERR_EVENT,
    WOW_FOURWAY_HSHAKE_EVENT,
    WOW_EAPOL_RECVD_EVENT,
    WOW_NLO_DETECTED_EVENT,
    WOW_DISASSOC_RECVD_EVENT,
    WOW_PATTERN_MATCH_EVENT,
    WOW_CSA_IE_EVENT,
    WOW_PROBE_REQ_WPS_IE_EVENT,
    WOW_AUTH_REQ_EVENT,
    WOW_ASSOC_REQ_EVENT,
    WOW_HTT_EVENT,
    WOW_RA_MATCH_EVENT,
    WOW_HOST_AUTO_SHUTDOWN_EVENT,
    WOW_IOAC_MAGIC_EVENT,
    WOW_IOAC_SHORT_EVENT,
    WOW_IOAC_EXTEND_EVENT,
    WOW_IOAC_TIMER_EVENT,
    WOW_DFS_PHYERR_RADAR_EVENT,
    WOW_BEACON_EVENT,
    WOW_CLIENT_KICKOUT_EVENT,
    WOW_EVENT_MAX,
};

#define C2S(x) case x: return #x

static inline const char* wow_wakeup_event(enum wmi_wow_wakeup_event ev) {
    switch (ev) {
        C2S(WOW_BMISS_EVENT);
        C2S(WOW_BETTER_AP_EVENT);
        C2S(WOW_DEAUTH_RECVD_EVENT);
        C2S(WOW_MAGIC_PKT_RECVD_EVENT);
        C2S(WOW_GTK_ERR_EVENT);
        C2S(WOW_FOURWAY_HSHAKE_EVENT);
        C2S(WOW_EAPOL_RECVD_EVENT);
        C2S(WOW_NLO_DETECTED_EVENT);
        C2S(WOW_DISASSOC_RECVD_EVENT);
        C2S(WOW_PATTERN_MATCH_EVENT);
        C2S(WOW_CSA_IE_EVENT);
        C2S(WOW_PROBE_REQ_WPS_IE_EVENT);
        C2S(WOW_AUTH_REQ_EVENT);
        C2S(WOW_ASSOC_REQ_EVENT);
        C2S(WOW_HTT_EVENT);
        C2S(WOW_RA_MATCH_EVENT);
        C2S(WOW_HOST_AUTO_SHUTDOWN_EVENT);
        C2S(WOW_IOAC_MAGIC_EVENT);
        C2S(WOW_IOAC_SHORT_EVENT);
        C2S(WOW_IOAC_EXTEND_EVENT);
        C2S(WOW_IOAC_TIMER_EVENT);
        C2S(WOW_DFS_PHYERR_RADAR_EVENT);
        C2S(WOW_BEACON_EVENT);
        C2S(WOW_CLIENT_KICKOUT_EVENT);
        C2S(WOW_EVENT_MAX);
    default:
        return NULL;
    }
}

enum wmi_wow_wake_reason {
    WOW_REASON_UNSPECIFIED = -1,
    WOW_REASON_NLOD = 0,
    WOW_REASON_AP_ASSOC_LOST,
    WOW_REASON_LOW_RSSI,
    WOW_REASON_DEAUTH_RECVD,
    WOW_REASON_DISASSOC_RECVD,
    WOW_REASON_GTK_HS_ERR,
    WOW_REASON_EAP_REQ,
    WOW_REASON_FOURWAY_HS_RECV,
    WOW_REASON_TIMER_INTR_RECV,
    WOW_REASON_PATTERN_MATCH_FOUND,
    WOW_REASON_RECV_MAGIC_PATTERN,
    WOW_REASON_P2P_DISC,
    WOW_REASON_WLAN_HB,
    WOW_REASON_CSA_EVENT,
    WOW_REASON_PROBE_REQ_WPS_IE_RECV,
    WOW_REASON_AUTH_REQ_RECV,
    WOW_REASON_ASSOC_REQ_RECV,
    WOW_REASON_HTT_EVENT,
    WOW_REASON_RA_MATCH,
    WOW_REASON_HOST_AUTO_SHUTDOWN,
    WOW_REASON_IOAC_MAGIC_EVENT,
    WOW_REASON_IOAC_SHORT_EVENT,
    WOW_REASON_IOAC_EXTEND_EVENT,
    WOW_REASON_IOAC_TIMER_EVENT,
    WOW_REASON_ROAM_HO,
    WOW_REASON_DFS_PHYERR_RADADR_EVENT,
    WOW_REASON_BEACON_RECV,
    WOW_REASON_CLIENT_KICKOUT_EVENT,
    WOW_REASON_DEBUG_TEST = 0xFF,
};

static inline const char* wow_reason(enum wmi_wow_wake_reason reason) {
    switch (reason) {
        C2S(WOW_REASON_UNSPECIFIED);
        C2S(WOW_REASON_NLOD);
        C2S(WOW_REASON_AP_ASSOC_LOST);
        C2S(WOW_REASON_LOW_RSSI);
        C2S(WOW_REASON_DEAUTH_RECVD);
        C2S(WOW_REASON_DISASSOC_RECVD);
        C2S(WOW_REASON_GTK_HS_ERR);
        C2S(WOW_REASON_EAP_REQ);
        C2S(WOW_REASON_FOURWAY_HS_RECV);
        C2S(WOW_REASON_TIMER_INTR_RECV);
        C2S(WOW_REASON_PATTERN_MATCH_FOUND);
        C2S(WOW_REASON_RECV_MAGIC_PATTERN);
        C2S(WOW_REASON_P2P_DISC);
        C2S(WOW_REASON_WLAN_HB);
        C2S(WOW_REASON_CSA_EVENT);
        C2S(WOW_REASON_PROBE_REQ_WPS_IE_RECV);
        C2S(WOW_REASON_AUTH_REQ_RECV);
        C2S(WOW_REASON_ASSOC_REQ_RECV);
        C2S(WOW_REASON_HTT_EVENT);
        C2S(WOW_REASON_RA_MATCH);
        C2S(WOW_REASON_HOST_AUTO_SHUTDOWN);
        C2S(WOW_REASON_IOAC_MAGIC_EVENT);
        C2S(WOW_REASON_IOAC_SHORT_EVENT);
        C2S(WOW_REASON_IOAC_EXTEND_EVENT);
        C2S(WOW_REASON_IOAC_TIMER_EVENT);
        C2S(WOW_REASON_ROAM_HO);
        C2S(WOW_REASON_DFS_PHYERR_RADADR_EVENT);
        C2S(WOW_REASON_BEACON_RECV);
        C2S(WOW_REASON_CLIENT_KICKOUT_EVENT);
        C2S(WOW_REASON_DEBUG_TEST);
    default:
        return NULL;
    }
}

#undef C2S

struct wmi_wow_ev_arg {
    uint32_t vdev_id;
    uint32_t flag;
    enum wmi_wow_wake_reason wake_reason;
    uint32_t data_len;
};

#define WOW_MIN_PATTERN_SIZE    1
#define WOW_MAX_PATTERN_SIZE    148
#define WOW_MAX_PKT_OFFSET  128

enum wmi_tdls_state {
    WMI_TDLS_DISABLE,
    WMI_TDLS_ENABLE_PASSIVE,
    WMI_TDLS_ENABLE_ACTIVE,
};

enum wmi_tdls_peer_state {
    WMI_TDLS_PEER_STATE_PEERING,
    WMI_TDLS_PEER_STATE_CONNECTED,
    WMI_TDLS_PEER_STATE_TEARDOWN,
};

struct wmi_tdls_peer_update_cmd_arg {
    uint32_t vdev_id;
    enum wmi_tdls_peer_state peer_state;
    uint8_t addr[ETH_ALEN];
};

#define WMI_TDLS_MAX_SUPP_OPER_CLASSES 32

struct wmi_tdls_peer_capab_arg {
    uint8_t peer_uapsd_queues;
    uint8_t peer_max_sp;
    uint32_t buff_sta_support;
    uint32_t off_chan_support;
    uint32_t peer_curr_operclass;
    uint32_t self_curr_operclass;
    uint32_t peer_chan_len;
    uint32_t peer_operclass_len;
    uint8_t peer_operclass[WMI_TDLS_MAX_SUPP_OPER_CLASSES];
    uint32_t is_peer_responder;
    uint32_t pref_offchan_num;
    uint32_t pref_offchan_bw;
};

enum wmi_txbf_conf {
    WMI_TXBF_CONF_UNSUPPORTED,
    WMI_TXBF_CONF_BEFORE_ASSOC,
    WMI_TXBF_CONF_AFTER_ASSOC,
};

#define WMI_CCA_DETECT_LEVEL_AUTO   0
#define WMI_CCA_DETECT_MARGIN_AUTO  0

struct wmi_pdev_set_adaptive_cca_params {
    uint32_t enable;
    uint32_t cca_detect_level;
    uint32_t cca_detect_margin;
} __PACKED;

enum wmi_host_platform_type {
    WMI_HOST_PLATFORM_HIGH_PERF,
    WMI_HOST_PLATFORM_LOW_PERF,
};

enum wmi_bss_survey_req_type {
    WMI_BSS_SURVEY_REQ_TYPE_READ = 1,
    WMI_BSS_SURVEY_REQ_TYPE_READ_CLEAR,
};

struct wmi_pdev_chan_info_req_cmd {
    uint32_t type;
    uint32_t reserved;
} __PACKED;

#define WMI_PFX(type) ATH10K_MSG_TYPE_WMI_##type

#define WMI_MSG(type, hdr) \
    MSG(WMI_PFX(type), ATH10K_MSG_TYPE_WMI, sizeof(struct hdr))

#define WMI_MSGS \
    MSG(ATH10K_MSG_TYPE_WMI, ATH10K_MSG_TYPE_HTC, sizeof(struct wmi_cmd_hdr)), \
    WMI_MSG(ECHO_CMD, wmi_echo_cmd),                                           \
    WMI_MSG(INIT_CMD_10_2, wmi_init_cmd_10_2),                                 \
    WMI_MSG(PDEV_SET_PARAM, wmi_pdev_set_param_cmd),                           \
    WMI_MSG(PDEV_SET_RD, wmi_pdev_set_regdomain_cmd),                          \
    WMI_MSG(PDEV_SUSPEND, wmi_pdev_suspend_cmd),                               \
    WMI_MSG(VDEV_CREATE, wmi_vdev_create_cmd),                                 \
    WMI_MSG(VDEV_DELETE, wmi_vdev_delete_cmd),                                 \
    WMI_MSG(VDEV_DOWN, wmi_vdev_down_cmd),                                     \
    WMI_MSG(VDEV_INSTALL_KEY, wmi_vdev_install_key_cmd),                       \
    WMI_MSG(VDEV_SET_PARAM, wmi_vdev_set_param_cmd),                           \
    WMI_MSG(VDEV_START, wmi_vdev_start_request_cmd),                           \
    WMI_MSG(VDEV_STOP, wmi_vdev_stop_cmd),                                     \
    WMI_MSG(VDEV_UP, wmi_vdev_up_cmd)

#define WMI_TX_CREDITS_AVAILABLE ZX_USER_SIGNAL_0

struct ath10k;
struct ath10k_vif;
struct ath10k_fw_stats_pdev;
struct ath10k_fw_stats_peer;
struct ath10k_fw_stats;

zx_status_t ath10k_wmi_attach(struct ath10k* ar);
void ath10k_wmi_detach(struct ath10k* ar);
void ath10k_wmi_free_host_mem(struct ath10k* ar);
zx_status_t ath10k_wmi_wait_for_service_ready(struct ath10k* ar);
zx_status_t ath10k_wmi_wait_for_unified_ready(struct ath10k* ar);

int ath10k_wmi_connect(struct ath10k* ar);

zx_status_t ath10k_wmi_cmd_send(struct ath10k* ar, struct ath10k_msg_buf* buf, uint32_t cmd_id);
int ath10k_wmi_cmd_send_nowait(struct ath10k* ar, struct ath10k_msg_buf* skb, uint32_t cmd_id);
void ath10k_wmi_start_scan_init(struct ath10k* ar, struct wmi_start_scan_arg* arg);

#if 0 // NEEDS PORTING
void ath10k_wmi_pull_pdev_stats_base(const struct wmi_pdev_stats_base* src,
                                     struct ath10k_fw_stats_pdev* dst);
void ath10k_wmi_pull_pdev_stats_tx(const struct wmi_pdev_stats_tx* src,
                                   struct ath10k_fw_stats_pdev* dst);
void ath10k_wmi_pull_pdev_stats_rx(const struct wmi_pdev_stats_rx* src,
                                   struct ath10k_fw_stats_pdev* dst);
void ath10k_wmi_pull_pdev_stats_extra(const struct wmi_pdev_stats_extra* src,
                                      struct ath10k_fw_stats_pdev* dst);
void ath10k_wmi_pull_peer_stats(const struct wmi_peer_stats* src,
                                struct ath10k_fw_stats_peer* dst);
#endif // NEEDS PORTING
void ath10k_wmi_put_host_mem_chunks(struct ath10k* ar,
                                    struct wmi_host_mem_chunks* chunks);
void ath10k_wmi_put_start_scan_common(struct wmi_start_scan_common* cmn,
                                      const struct wmi_start_scan_arg* arg);
void ath10k_wmi_set_wmm_param(struct wmi_wmm_params* params,
                              const struct wmi_wmm_params_arg* arg);
void ath10k_wmi_put_wmi_channel(struct wmi_channel* ch,
                                const struct wmi_channel_arg* arg);
int ath10k_wmi_start_scan_verify(const struct wmi_start_scan_arg* arg);

int ath10k_wmi_event_scan(struct ath10k* ar, struct ath10k_msg_buf* buf);
int ath10k_wmi_event_mgmt_rx(struct ath10k* ar, struct ath10k_msg_buf* buf);
#if 0 // NEEDS PORTING
void ath10k_wmi_event_chan_info(struct ath10k* ar, struct sk_buff* skb);
#endif // NEEDS PORTING
void ath10k_wmi_event_echo(struct ath10k* ar, struct ath10k_msg_buf* buf);
#if 0 // NEEDS PORTING
int ath10k_wmi_event_debug_mesg(struct ath10k* ar, struct sk_buff* skb);
void ath10k_wmi_event_update_stats(struct ath10k* ar, struct sk_buff* skb);
#endif // NEEDS PORTING
void ath10k_wmi_event_vdev_start_resp(struct ath10k* ar, struct ath10k_msg_buf* buf);
void ath10k_wmi_event_vdev_stopped(struct ath10k* ar, struct ath10k_msg_buf* buf);
#if 0 // NEEDS PORTING
void ath10k_wmi_event_peer_sta_kickout(struct ath10k* ar, struct sk_buff* skb);
void ath10k_wmi_event_host_swba(struct ath10k* ar, struct sk_buff* skb);
void ath10k_wmi_event_tbttoffset_update(struct ath10k* ar, struct sk_buff* skb);
void ath10k_wmi_event_dfs(struct ath10k* ar,
                          struct wmi_phyerr_ev_arg* phyerr, uint64_t tsf);
void ath10k_wmi_event_spectral_scan(struct ath10k* ar,
                                    struct wmi_phyerr_ev_arg* phyerr,
                                    uint64_t tsf);
void ath10k_wmi_event_phyerr(struct ath10k* ar, struct sk_buff* skb);
void ath10k_wmi_event_roam(struct ath10k* ar, struct sk_buff* skb);
void ath10k_wmi_event_profile_match(struct ath10k* ar, struct sk_buff* skb);
void ath10k_wmi_event_debug_print(struct ath10k* ar, struct sk_buff* skb);
void ath10k_wmi_event_pdev_qvit(struct ath10k* ar, struct sk_buff* skb);
void ath10k_wmi_event_wlan_profile_data(struct ath10k* ar, struct sk_buff* skb);
void ath10k_wmi_event_rtt_measurement_report(struct ath10k* ar,
        struct sk_buff* skb);
void ath10k_wmi_event_tsf_measurement_report(struct ath10k* ar,
        struct sk_buff* skb);
void ath10k_wmi_event_rtt_error_report(struct ath10k* ar, struct sk_buff* skb);
void ath10k_wmi_event_wow_wakeup_host(struct ath10k* ar, struct sk_buff* skb);
void ath10k_wmi_event_dcs_interference(struct ath10k* ar, struct sk_buff* skb);
void ath10k_wmi_event_pdev_tpc_config(struct ath10k* ar, struct sk_buff* skb);
void ath10k_wmi_event_pdev_ftm_intg(struct ath10k* ar, struct sk_buff* skb);
void ath10k_wmi_event_gtk_offload_status(struct ath10k* ar,
        struct sk_buff* skb);
void ath10k_wmi_event_gtk_rekey_fail(struct ath10k* ar, struct sk_buff* skb);
void ath10k_wmi_event_delba_complete(struct ath10k* ar, struct sk_buff* skb);
void ath10k_wmi_event_addba_complete(struct ath10k* ar, struct sk_buff* skb);
#endif // NEEDS PORTING
void ath10k_wmi_event_vdev_install_key_complete(struct ath10k* ar, struct ath10k_msg_buf* msg_buf);
#if 0 // NEEDS PORTING
void ath10k_wmi_event_inst_rssi_stats(struct ath10k* ar, struct sk_buff* skb);
void ath10k_wmi_event_vdev_standby_req(struct ath10k* ar, struct sk_buff* skb);
void ath10k_wmi_event_vdev_resume_req(struct ath10k* ar, struct sk_buff* skb);
#endif // NEEDS PORTING
void ath10k_wmi_event_service_ready(struct ath10k* ar, struct ath10k_msg_buf* msg_buf);
zx_status_t ath10k_wmi_event_ready(struct ath10k* ar, struct ath10k_msg_buf* msg_buf);
#if 0 // NEEDS PORTING
int ath10k_wmi_op_pull_phyerr_ev(struct ath10k* ar, const void* phyerr_buf,
                                 int left_len, struct wmi_phyerr_ev_arg* arg);
void ath10k_wmi_main_op_fw_stats_fill(struct ath10k* ar,
                                      struct ath10k_fw_stats* fw_stats,
                                      char* buf);
void ath10k_wmi_10x_op_fw_stats_fill(struct ath10k* ar,
                                     struct ath10k_fw_stats* fw_stats,
                                     char* buf);
size_t ath10k_wmi_fw_stats_num_peers(struct list_head* head);
size_t ath10k_wmi_fw_stats_num_vdevs(struct list_head* head);
void ath10k_wmi_10_4_op_fw_stats_fill(struct ath10k* ar,
                                      struct ath10k_fw_stats* fw_stats,
                                      char* buf);
#endif // NEEDS PORTING
int ath10k_wmi_op_get_vdev_subtype(struct ath10k* ar,
                                   enum wmi_vdev_subtype subtype);
zx_status_t ath10k_wmi_barrier(struct ath10k* ar);

#endif /* _WMI_H_ */
