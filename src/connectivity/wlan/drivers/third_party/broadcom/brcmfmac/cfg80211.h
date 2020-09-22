/*
 * Copyright (c) 2010 Broadcom Corporation
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_CFG80211_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_CFG80211_H_

#include <lib/sync/completion.h>
#include <threads.h>
#include <zircon/listnode.h>

#include <atomic>

#include <ddk/protocol/wlanphyimpl.h>

/* for brcmu_d11inf */
#include "brcmu_d11.h"
#include "core.h"
#include "fwil_types.h"
#include "timer.h"
#include "workqueue.h"

// clang-format off

#define WL_NUM_SCAN_MAX        10
#define WL_TLV_INFO_MAX        1024
#define WL_BSS_INFO_MAX        2048
#define WL_ASSOC_INFO_MAX      512 /* assoc related fil max buf */
#define WL_EXTRA_BUF_MAX       2048
#define WL_ROAM_TRIGGER_LEVEL  -75
#define WL_ROAM_DELTA          20

/* Keep BRCMF_ESCAN_BUF_SIZE below 64K (65536). Allocing over 64K can be
 * problematic on some systems and should be avoided.
 */
#define BRCMF_ESCAN_BUF_SIZE   65000
#define BRCMF_ESCAN_TIMER_INTERVAL_MS 10000 /* E-Scan timeout */

#define BRCMF_DISCONNECT_TIMER_DUR_MS     ZX_MSEC(50) /* disconnect timer dur */
#define BRCMF_SIGNAL_REPORT_TIMER_DUR_MS  ZX_MSEC(1000) /* Signal report dur */
#define BRCMF_AP_START_TIMER_DUR_MS       ZX_MSEC(1000) /* AP start timer dur */
#define BRCMF_CONNECT_TIMER_DUR_MS        ZX_MSEC(1500) /*connect timer dur*/

#define WL_ESCAN_ACTION_START      1
#define WL_ESCAN_ACTION_CONTINUE   2
#define WL_ESCAN_ACTION_ABORT      3

#define WL_AUTH_SHARED_KEY 1 /* d11 shared authentication */
#define IE_MAX_LEN 512

/* IE TLV processing */
#define TLV_LEN_OFF   1  /* length offset */
#define TLV_HDR_LEN   2  /* header length */
#define TLV_BODY_OFF  2  /* body offset */
#define TLV_OUI_LEN   3  /* oui id length */
#define TLV_OUI_TYPE_LEN 1

#define MSFT_OUI "\x00\x50\xF2"
#define WPA_OUI_TYPE 1
#define WSC_OUI_TYPE 4
#define RSN_OUI "\x00\x0F\xAC"
#define WME_OUI_TYPE 2

#define VS_IE_FIXED_HDR_LEN 6
#define WPA_IE_VERSION_LEN 2
#define WPA_IE_MIN_OUI_LEN 4
#define WPA_IE_SUITE_COUNT_LEN 2

// IEEE Std. 802.11-2016, 9.4.2.1, Table 9-77
#define WLAN_IE_TYPE_SSID 0
#define WLAN_IE_TYPE_SUPP_RATES 1
#define WLAN_IE_TYPE_RSNE 48
#define WLAN_IE_TYPE_EXT_SUPP_RATES 50
#define WLAN_IE_TYPE_VENDOR_SPECIFIC 221

/* IEEE Std. 802.11-2016, 9.4.2.25.2, Table 9-131 */
#define WPA_CIPHER_NONE 0   /* None */
#define WPA_CIPHER_WEP_40 1 /* WEP (40-bit) */
#define WPA_CIPHER_TKIP 2   /* TKIP: default for WPA */
/*      RESERVED             3  */
#define WPA_CIPHER_CCMP_128 4 /* AES (CCM) */
#define WPA_CIPHER_WEP_104 5  /* WEP (104-bit) */
#define WPA_CIPHER_CMAC_128 6 /* BIP-CMAC-128 */

#define RSN_AKM_NONE 0        /* None (IBSS) */
#define RSN_AKM_UNSPECIFIED 1 /* Over 802.1x */
#define RSN_AKM_PSK 2         /* Pre-shared Key */
#define RSN_AKM_SHA256_1X 5   /* SHA256, 802.1X */
#define RSN_AKM_SHA256_PSK 6  /* SHA256, Pre-shared Key */
#define RSN_CAP_LEN 2         /* Length of RSN capabilities */
#define RSN_CAP_PTK_REPLAY_CNTR_MASK (BIT(2) | BIT(3))
#define RSN_CAP_MFPR_MASK BIT(6)
#define RSN_CAP_MFPC_MASK BIT(7)
#define RSN_PMKID_COUNT_LEN 2

#define VNDR_IE_CMD_LEN 4 /* length of the set command string :"add", "del" (+ NUL) */
#define VNDR_IE_COUNT_OFFSET 4
#define VNDR_IE_PKTFLAG_OFFSET 8
#define VNDR_IE_VSIE_OFFSET 12
#define VNDR_IE_HDR_SIZE 12
#define VNDR_IE_PARSE_LIMIT 5

#define DOT11_MGMT_HDR_LEN 24      /* d11 management header len */
#define DOT11_BCN_PRB_FIXED_LEN 12 /* beacon/probe fixed length */
/* 802.11 Mgmt Packet flags */
#define BRCMF_VNDR_IE_BEACON_FLAG     0x1
#define BRCMF_VNDR_IE_PRBRSP_FLAG     0x2
#define BRCMF_VNDR_IE_ASSOCRSP_FLAG   0x4
#define BRCMF_VNDR_IE_AUTHRSP_FLAG    0x8
#define BRCMF_VNDR_IE_PRBREQ_FLAG    0x10
#define BRCMF_VNDR_IE_ASSOCREQ_FLAG  0x20
/* vendor IE in IW advertisement protocol ID field */
#define BRCMF_VNDR_IE_IWAPID_FLAG    0x40
/* allow custom IE id */
#define BRCMF_VNDR_IE_CUSTOM_FLAG   0x100

/* P2P Action Frames flags (spec ordered) */
#define BRCMF_VNDR_IE_GONREQ_FLAG 0x001000
#define BRCMF_VNDR_IE_GONRSP_FLAG 0x002000
#define BRCMF_VNDR_IE_GONCFM_FLAG 0x004000
#define BRCMF_VNDR_IE_INVREQ_FLAG 0x008000
#define BRCMF_VNDR_IE_INVRSP_FLAG 0x010000
#define BRCMF_VNDR_IE_DISREQ_FLAG 0x020000
#define BRCMF_VNDR_IE_DISRSP_FLAG 0x040000
#define BRCMF_VNDR_IE_PRDREQ_FLAG 0x080000
#define BRCMF_VNDR_IE_PRDRSP_FLAG 0x100000

#define BRCMF_VNDR_IE_P2PAF_SHIFT 12

#define BRCMF_MAX_DEFAULT_KEYS 6

/* beacon loss timeout defaults */
#define BRCMF_DEFAULT_BCN_TIMEOUT_ROAM_ON  2
#define BRCMF_DEFAULT_BCN_TIMEOUT_ROAM_OFF 4

// clang-format on

#define BRCMF_VIF_EVENT_TIMEOUT_MSEC (1500)

#define BRCMF_ACTIVE_SCAN_NUM_PROBES 3
/**
 * enum brcmf_scan_status - scan engine status
 *
 * @BRCMF_SCAN_STATUS_BUSY: scanning in progress on dongle.
 * @BRCMF_SCAN_STATUS_ABORT: scan being aborted on dongle.
 * @BRCMF_SCAN_STATUS_SUPPRESS: scanning is suppressed in driver.
 */
enum brcmf_scan_status {
  BRCMF_SCAN_STATUS_BUSY,
  BRCMF_SCAN_STATUS_ABORT,
  BRCMF_SCAN_STATUS_SUPPRESS,
};

/* dongle configuration */
struct brcmf_cfg80211_conf {
  uint32_t frag_threshold;
  uint32_t rts_threshold;
  uint32_t retry_short;
  uint32_t retry_long;
};

/* security information with currently associated ap */
struct brcmf_cfg80211_security {
  uint32_t wpa_versions;
  uint32_t auth_type;
  uint32_t cipher_pairwise;
  uint32_t cipher_group;
};

enum brcmf_profile_fwsup {
  BRCMF_PROFILE_FWSUP_NONE,
  BRCMF_PROFILE_FWSUP_PSK,
  BRCMF_PROFILE_FWSUP_1X
};

/**
 * struct brcmf_cfg80211_profile - profile information.
 *
 * @bssid: bssid of joined/joining ibss.
 * @sec: security information.
 * @key: key information.
 * @use_fwsup: use firmware supplicant.
 * @beacon_period: in AP mode, beacon period in TUs.
 */
struct brcmf_cfg80211_profile {
  uint8_t bssid[ETH_ALEN];
  struct brcmf_cfg80211_security sec;
  struct brcmf_wsec_key key[BRCMF_MAX_DEFAULT_KEYS];
  enum brcmf_profile_fwsup use_fwsup;

  // AP-specific
  uint32_t beacon_period;
};

/**
 * enum brcmf_vif_status - bit indices for vif status.
 *
 * @BRCMF_VIF_STATUS_READY: ready for operation.
 * @BRCMF_VIF_STATUS_CONNECTING: connect/join in progress.
 * @BRCMF_VIF_STATUS_CONNECTED: connected/joined successfully.
 * @BRCMF_VIF_STATUS_DISCONNECTING: disconnect/disable in progress.
 * @BRCMF_VIF_STATUS_AP_START_PENDING: AP start pending.
 * @BRCMF_VIF_STATUS_AP_CREATED: AP operation started.
 * @BRCMF_VIF_STATUS_EAP_SUCCUSS: EAPOL handshake successful.
 * @BRCMF_VIF_STATUS_ASSOC_SUCCESS: successful SET_SSID received.
 */
enum brcmf_vif_status {
  BRCMF_VIF_STATUS_READY,
  BRCMF_VIF_STATUS_CONNECTING,
  BRCMF_VIF_STATUS_CONNECTED,
  BRCMF_VIF_STATUS_DISCONNECTING,
  BRCMF_VIF_STATUS_AP_START_PENDING,
  BRCMF_VIF_STATUS_AP_CREATED,
  BRCMF_VIF_STATUS_EAP_SUCCESS,
  BRCMF_VIF_STATUS_ASSOC_SUCCESS,
};

/**
 * struct vif_saved_ie - holds saved IEs for a virtual interface.
 *
 * @probe_req_ie: IE info for probe request.
 * @probe_res_ie: IE info for probe response.
 * @beacon_ie: IE info for beacon frame.
 * @probe_req_ie_len: IE info length for probe request.
 * @probe_res_ie_len: IE info length for probe response.
 * @beacon_ie_len: IE info length for beacon frame.
 */
struct vif_saved_ie {
  uint8_t probe_req_ie[IE_MAX_LEN];
  uint8_t probe_res_ie[IE_MAX_LEN];
  uint8_t beacon_ie[IE_MAX_LEN];
  uint8_t assoc_req_ie[IE_MAX_LEN];
  uint32_t probe_req_ie_len;
  uint32_t probe_res_ie_len;
  uint32_t beacon_ie_len;
  uint32_t assoc_req_ie_len;
};

/**
 * struct brcmf_cfg80211_vif - virtual interface specific information.
 *
 * @ifp: lower layer interface pointer
 * @wdev: wireless device.
 * @profile: profile information.
 * @sme_state: SME state using enum brcmf_vif_status bits.
 * @list: linked list.
 * @mgmt_rx_reg: registered rx mgmt frame types.
 * @mbss: Multiple BSS type, set if not first AP (not relevant for P2P).
 */
struct brcmf_cfg80211_vif {
  struct brcmf_if* ifp;
  struct wireless_dev wdev;
  struct brcmf_cfg80211_profile profile;
  std::atomic<unsigned long> sme_state;
  struct vif_saved_ie saved_ie;
  struct list_node list;
  uint16_t mgmt_rx_reg;
  bool mbss;
  int is_11d;
};

/* association inform */
struct brcmf_cfg80211_connect_info {
  uint8_t* req_ie;
  int32_t req_ie_len;
  uint8_t* resp_ie;
  int32_t resp_ie_len;
};

/* assoc ie length */
struct brcmf_cfg80211_assoc_ielen_le {
  uint32_t req_len;
  uint32_t resp_len;
};

/* dongle escan state */
enum wl_escan_state { WL_ESCAN_STATE_IDLE, WL_ESCAN_STATE_SCANNING };

struct escan_info {
  uint32_t escan_state;
  uint8_t* escan_buf;
  struct brcmf_if* ifp;
  zx_status_t (*run)(struct brcmf_cfg80211_info* cfg, struct brcmf_if* ifp,
                     const wlanif_scan_req_t* request);
};

/**
 * struct brcmf_cfg80211_vif_event - virtual interface event information.
 *
 * @vif_event_wait: Completion awaiting interface event from firmware.
 * @vif_event_lock: protects other members in this structure.
 * @action: either add, change, or delete.
 * @vif: virtual interface object related to the event.
 */
struct brcmf_cfg80211_vif_event {
  sync_completion_t vif_event_wait;
  mtx_t vif_event_lock;
  uint8_t action;
  struct brcmf_cfg80211_vif* vif;
};

/**
 * struct brcmf_cfg80211_wowl - wowl related information.
 *
 * @active: set on suspend, cleared on resume.
 * @pre_pmmode: firmware PM mode at entering suspend.
 * @nd: net dectect data.
 * @nd_info: helper struct to pass to cfg80211.
 * @nd_data_wait: Completion to sync net detect data.
 * @nd_enabled: net detect enabled.
 */
struct brcmf_cfg80211_wowl {
  bool active;
  uint32_t pre_pmmode;
  struct cfg80211_wowlan_nd_match* nd;
  struct cfg80211_wowlan_nd_info* nd_info;
  sync_completion_t nd_data_wait;
  bool nd_enabled;
};

enum brcmf_disconnect_mode { BRCMF_DISCONNECT_DEAUTH, BRCMF_DISCONNECT_DISASSOC };

/**
 * struct brcmf_cfg80211_info - dongle private data of cfg80211 interface
 *
 * @conf: dongle configuration.
 * @btcoex: Bluetooth coexistence information.
 * @scan_request: cfg80211 scan request object.
 * @usr_sync: mainly for dongle up/down synchronization.
 * @bss_list: bss_list holding scanned ap information.
 * @bss_info: bss information for cfg80211 layer.
 * @conn_info: association info.
 * @pmk_list: wpa2 pmk list.
 * @scan_status: scan activity on the dongle.
 * @pub: common driver information.
 * @channel: current channel.
 * @int_escan_map: bucket map for which internal e-scan is done.
 * @ibss_starter: indicates this sta is ibss starter.
 * @pwr_save: indicate whether dongle to support power save mode.
 * @dongle_up: indicate whether dongle up or not.
 * @roam_on: on/off switch for dongle self-roaming.
 * @scan_tried: indicates if first scan attempted.
 * @dcmd_buf: dcmd buffer.
 * @extra_buf: mainly to grab assoc information.
 * @debugfsdir: debugfs folder for this device.
 * @escan_info: escan information.
 * @escan_timer: Timer for catch scan timeout.
 * @escan_timeout_work: scan timeout worker.
 * @disconnect_mode: indicates type of disconnect requested (BRCMF_DISCONNECT_*)
 * @disconnect_timer: timer for disconnection completion.
 * @disconnect_timeout_work: associated work structure for disassociation timer.
 * @connect_timer: timer for firmware response of connect.
 * @connect_timeout_work: associated work structure for association timer.
 * @vif_list: linked list of vif instances.
 * @vif_cnt: number of vif instances.
 * @vif_event: vif event signalling.
 * @vif_event_pending_action: If vif_event is set, this is what it's waiting for.
 * @wowl: wowl related information.
 * @pno: information of pno module.
 * @ap_started: Boolean indicating if SoftAP has been started.
 * @signal_report_timer: Timer to periodically update signal report to SME.
 * @signal_report_work: Work structure for signal report timer.
 * @ap_start_timer: Timer used to wait for ap start confirmation.
 * @ap_start_timeout_work: Work structure for ap start timer
 */
struct brcmf_cfg80211_info {
  struct brcmf_cfg80211_conf* conf;
  struct brcmf_btcoex_info* btcoex;
  const wlanif_scan_req_t* scan_request;
  mtx_t usr_sync;
  struct wl_cfg80211_bss_info* bss_info;
  struct brcmf_cfg80211_connect_info conn_info;
  struct brcmf_pmk_list_le pmk_list;
  std::atomic<unsigned long> scan_status;
  struct brcmf_pub* pub;
  uint32_t channel;
  uint32_t int_escan_map;
  bool ibss_starter;
  bool pwr_save;
  bool dongle_up;
  bool scan_tried;
  uint8_t* dcmd_buf;
  uint8_t* extra_buf;
  zx_handle_t debugfsdir;
  struct escan_info escan_info;
  Timer* escan_timer;
  WorkItem escan_timeout_work;
  uint8_t disconnect_mode;
  Timer* disconnect_timer;
  WorkItem disconnect_timeout_work;
  Timer* connect_timer;
  WorkItem connect_timeout_work;
  struct list_node vif_list;
  struct brcmf_cfg80211_vif_event vif_event;
  uint8_t vif_event_pending_action;
  sync_completion_t vif_disabled;
  struct brcmu_d11inf d11inf;
  struct brcmf_assoclist_le assoclist;
  struct brcmf_cfg80211_wowl wowl;
  struct brcmf_pno_info* pno;
  bool ap_started;
  Timer* signal_report_timer;
  WorkItem signal_report_work;
  Timer* ap_start_timer;
  WorkItem ap_start_timeout_work;
};

/**
 * struct brcmf_tlv - tag_ID/length/value_buffer tuple.
 *
 * @id: tag identifier.
 * @len: number of bytes in value buffer.
 * @data: value buffer.
 */
struct brcmf_tlv {
  uint8_t id;
  uint8_t len;
  uint8_t data[1];
};

static inline struct net_device* cfg_to_ndev(struct brcmf_cfg80211_info* cfg) {
  struct brcmf_cfg80211_vif* vif;
  vif = list_peek_head_type(&cfg->vif_list, struct brcmf_cfg80211_vif, list);
  return vif->wdev.netdev;
}

static inline struct brcmf_if* ndev_to_if(struct net_device* ndev) {
  return static_cast<brcmf_if*>(ndev->priv);
}

static inline struct brcmf_if* cfg_to_if(struct brcmf_cfg80211_info* cfg) {
  return static_cast<brcmf_if*>(cfg_to_ndev(cfg)->priv);
}

static inline struct brcmf_cfg80211_vif* ndev_to_vif(struct net_device* ndev) {
  return ndev_to_if(ndev)->vif;
}

static inline struct wireless_dev* ndev_to_wdev(struct net_device* ndev) {
  return &ndev_to_vif(ndev)->wdev;
}

static inline struct brcmf_cfg80211_profile* ndev_to_prof(struct net_device* ndev) {
  return &ndev_to_vif(ndev)->profile;
}

static inline struct brcmf_cfg80211_connect_info* cfg_to_conn(struct brcmf_cfg80211_info* cfg) {
  return &cfg->conn_info;
}

zx_status_t brcmf_cfg80211_attach(struct brcmf_pub* drvr);
void brcmf_cfg80211_detach(struct brcmf_cfg80211_info* cfg);
zx_status_t brcmf_cfg80211_up(struct net_device* ndev);
zx_status_t brcmf_cfg80211_down(struct net_device* ndev);
uint16_t brcmf_cfg80211_get_iftype(struct brcmf_if* ifp);

zx_status_t brcmf_cfg80211_add_iface(struct brcmf_pub* drvr, const char* name,
                                     struct vif_params* params,
                                     const wlanphy_impl_create_iface_req_t* req,
                                     struct wireless_dev** wdev_out);
zx_status_t brcmf_cfg80211_del_iface(struct brcmf_cfg80211_info* cfg, struct wireless_dev* wdev);

zx_status_t brcmf_alloc_vif(struct brcmf_cfg80211_info* cfg, uint16_t type,
                            struct brcmf_cfg80211_vif** vif_out);
void brcmf_free_vif(struct brcmf_cfg80211_vif* vif);

zx_status_t brcmf_vif_set_mgmt_ie(struct brcmf_cfg80211_vif* vif, int32_t pktflag,
                                  const uint8_t* vndr_ie_buf, uint32_t vndr_ie_len);
zx_status_t brcmf_vif_clear_mgmt_ies(struct brcmf_cfg80211_vif* vif);
bool brcmf_get_vif_state_any(struct brcmf_cfg80211_info* cfg, unsigned long state);
void brcmf_cfg80211_arm_vif_event(struct brcmf_cfg80211_info* cfg, struct brcmf_cfg80211_vif* vif,
                                  uint8_t pending_action);
void brcmf_cfg80211_disarm_vif_event(struct brcmf_cfg80211_info* cfg);
bool brcmf_cfg80211_vif_event_armed(struct brcmf_cfg80211_info* cfg);
zx_status_t brcmf_cfg80211_wait_vif_event(struct brcmf_cfg80211_info* cfg, zx_duration_t timeout);
zx_status_t brcmf_notify_escan_complete(struct brcmf_cfg80211_info* cfg, struct brcmf_if* ifp,
                                        bool aborted, bool fw_abort);
void brcmf_enable_mpc(struct brcmf_if* ndev, int mpc);
void brcmf_abort_scanning(struct brcmf_cfg80211_info* cfg);
void brcmf_free_net_device_vif(struct net_device* ndev);
zx_status_t brcmf_set_iface_macaddr(bool is_ap, net_device* ndev, const uint8_t mac_addr[ETH_ALEN]);

void brcmf_cfg80211_rx(struct brcmf_if* ifp, const void* data, size_t size);

uint8_t brcmf_cfg80211_classify8021d(const uint8_t* data, size_t size);

// TODO: Move to core.h
zx_status_t brcmf_netdev_open(struct net_device* ndev);

// Protocol ops implementations.

zx_status_t brcmf_if_start(net_device* ndev, const wlanif_impl_ifc_protocol_t* ifc,
                           zx_handle_t* out_sme_channel);
void brcmf_if_stop(net_device* ndev);
void brcmf_if_query(net_device* ndev, wlanif_query_info_t* info);
void brcmf_if_start_scan(net_device* ndev, const wlanif_scan_req_t* req);
void brcmf_if_join_req(net_device* ndev, const wlanif_join_req_t* req);
void brcmf_if_auth_req(net_device* ndev, const wlanif_auth_req_t* req);
void brcmf_if_auth_resp(net_device* ndev, const wlanif_auth_resp_t* ind);
void brcmf_if_deauth_req(net_device* ndev, const wlanif_deauth_req_t* req);
void brcmf_if_assoc_req(net_device* ndev, const wlanif_assoc_req_t* req);
void brcmf_if_assoc_resp(net_device* ndev, const wlanif_assoc_resp_t* ind);
void brcmf_if_disassoc_req(net_device* ndev, const wlanif_disassoc_req_t* req);
void brcmf_if_reset_req(net_device* ndev, const wlanif_reset_req_t* req);
void brcmf_if_start_req(net_device* ndev, const wlanif_start_req_t* req);
void brcmf_if_stop_req(net_device* ndev, const wlanif_stop_req_t* req);
void brcmf_if_set_keys_req(net_device* ndev, const wlanif_set_keys_req_t* req);
void brcmf_if_del_keys_req(net_device* ndev, const wlanif_del_keys_req_t* req);
void brcmf_if_eapol_req(net_device* ndev, const wlanif_eapol_req_t* req);
void brcmf_if_stats_query_req(net_device* ndev);
void brcmf_if_start_capture_frames(net_device* ndev, const wlanif_start_capture_frames_req_t* req,
                                   wlanif_start_capture_frames_resp_t* resp);
void brcmf_if_stop_capture_frames(net_device* ndev);
zx_status_t brcmf_if_set_multicast_promisc(net_device* ndev, bool enable);
void brcmf_if_data_queue_tx(net_device* ndev, uint32_t options, ethernet_netbuf_t* netbuf,
                            ethernet_impl_queue_tx_callback completion_cb, void* cookie);
void brcmf_extract_ies(const uint8_t* ie, size_t ie_len, wlanif_bss_description_t* bss);

// If the WMM parameter IE (used for QoS) is available from the association response, set its
// body into the Association Confirm message.
void set_assoc_conf_wmm_param(const brcmf_cfg80211_info* cfg, wlanif_assoc_confirm_t* confirm);
void brcmf_cfg80211_handle_eapol_frame(struct brcmf_if* ifp, const void* data, size_t size);
#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_CFG80211_H_
