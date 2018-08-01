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

#ifndef _CORE_H_
#define _CORE_H_

#include <stdatomic.h>
#include <stdint.h>
#define _ALL_SOURCE  // Define to get thrd_create_with_name from threads.h
#include <threads.h>

#include <ddk/device.h>
#include <lib/sync/completion.h>
#include <wlan/protocol/mac.h>

#include "htc.h"
#include "htt.h"
#include "hw.h"
#include "ieee80211.h"
#include "macros.h"
#include "msg_buf.h"
#include "swap.h"
#include "targaddrs.h"
#include "thermal.h"
#include "wmi.h"
#include "wow.h"

#define MS(_v, _f) (((_v) & _f##_MASK) >> _f##_LSB)
#define SM(_v, _f) (((_v) << _f##_LSB) & _f##_MASK)
#define WO(_f)      ((_f##_OFFSET) >> 2)

#define ATH10K_SCAN_ID 0
#define WMI_READY_TIMEOUT (5 * HZ)
#define ATH10K_FLUSH_TIMEOUT_HZ (5 * HZ)
#define ATH10K_CONNECTION_LOSS_HZ (3 * HZ)
#define ATH10K_NUM_CHANS 40
#define ATH10K_FW_VER_LEN 32

/* Antenna noise floor */
#define ATH10K_DEFAULT_NOISE_FLOOR -95

#define ATH10K_MAX_NUM_MGMT_PENDING 128

/* number of failed packets (20 packets with 16 sw reties each) */
#define ATH10K_KICKOUT_THRESHOLD (20 * 16)

/*
 * Use insanely high numbers to make sure that the firmware implementation
 * won't start, we have the same functionality already in hostapd. Unit
 * is seconds.
 */
#define ATH10K_KEEPALIVE_MIN_IDLE 3747
#define ATH10K_KEEPALIVE_MAX_IDLE 3895
#define ATH10K_KEEPALIVE_MAX_UNRESPONSIVE 3900

/* NAPI poll budget */
#define ATH10K_NAPI_BUDGET      64
#define ATH10K_NAPI_QUOTA_LIMIT 60

/* SMBIOS type containing Board Data File Name Extension */
#define ATH10K_SMBIOS_BDF_EXT_TYPE 0xF8

/* SMBIOS type structure length (excluding strings-set) */
#define ATH10K_SMBIOS_BDF_EXT_LENGTH 0x9

/* Offset pointing to Board Data File Name Extension */
#define ATH10K_SMBIOS_BDF_EXT_OFFSET 0x8

/* Board Data File Name Extension string length.
 * String format: BDF_<Customer ID>_<Extension>\0
 */
#define ATH10K_SMBIOS_BDF_EXT_STR_LENGTH 0x20

/* The magic used by QCA spec */
#define ATH10K_SMBIOS_BDF_EXT_MAGIC "BDF_"

struct ath10k;

enum ath10k_bus {
    ATH10K_BUS_PCI,
    ATH10K_BUS_AHB,
    ATH10K_BUS_SDIO,
};

static inline const char* ath10k_bus_str(enum ath10k_bus bus) {
    switch (bus) {
    case ATH10K_BUS_PCI:
        return "pci";
    case ATH10K_BUS_AHB:
        return "ahb";
    case ATH10K_BUS_SDIO:
        return "sdio";
    }

    return "unknown";
}

static inline uint32_t host_interest_item_address(uint32_t item_offset) {
    return QCA988X_HOST_INTEREST_ADDRESS + item_offset;
}

struct ath10k_bmi {
    bool done_sent;
};

struct ath10k_mem_chunk {
    io_buffer_t handle;
    void* vaddr;
    zx_paddr_t paddr;
    uint32_t len;
    uint32_t req_id;
};

struct ath10k_wmi {
    enum ath10k_htc_ep_id eid;
    sync_completion_t service_ready;
    sync_completion_t unified_ready;
    sync_completion_t barrier;
    zx_handle_t tx_credits_event;
    BITARR(svc_map, WMI_SERVICE_MAX);
    struct wmi_cmd_map* cmd;
    struct wmi_vdev_param_map* vdev_param;
    struct wmi_pdev_param_map* pdev_param;
    const struct wmi_ops* ops;
    const struct wmi_peer_flags_map* peer_flags;

    uint32_t num_mem_chunks;
    uint32_t rx_decap_mode;
    struct ath10k_mem_chunk mem_chunks[WMI_MAX_MEM_REQS];
};

#if 0 // NEEDS PORTING
struct ath10k_fw_stats_peer {
    struct list_head list;

    uint8_t peer_macaddr[ETH_ALEN];
    uint32_t peer_rssi;
    uint32_t peer_tx_rate;
    uint32_t peer_rx_rate; /* 10x only */
    uint32_t rx_duration;
};

struct ath10k_fw_extd_stats_peer {
    struct list_head list;

    uint8_t peer_macaddr[ETH_ALEN];
    uint32_t rx_duration;
};

struct ath10k_fw_stats_vdev {
    struct list_head list;

    uint32_t vdev_id;
    uint32_t beacon_snr;
    uint32_t data_snr;
    uint32_t num_tx_frames[4];
    uint32_t num_rx_frames;
    uint32_t num_tx_frames_retries[4];
    uint32_t num_tx_frames_failures[4];
    uint32_t num_rts_fail;
    uint32_t num_rts_success;
    uint32_t num_rx_err;
    uint32_t num_rx_discard;
    uint32_t num_tx_not_acked;
    uint32_t tx_rate_history[10];
    uint32_t beacon_rssi_history[10];
};

struct ath10k_fw_stats_pdev {
    struct list_head list;

    /* PDEV stats */
    int32_t ch_noise_floor;
    uint32_t tx_frame_count; /* Cycles spent transmitting frames */
    uint32_t rx_frame_count; /* Cycles spent receiving frames */
    uint32_t rx_clear_count; /* Total channel busy time, evidently */
    uint32_t cycle_count; /* Total on-channel time */
    uint32_t phy_err_count;
    uint32_t chan_tx_power;
    uint32_t ack_rx_bad;
    uint32_t rts_bad;
    uint32_t rts_good;
    uint32_t fcs_bad;
    uint32_t no_beacons;
    uint32_t mib_int_count;

    /* PDEV TX stats */
    int32_t comp_queued;
    int32_t comp_delivered;
    int32_t msdu_enqued;
    int32_t mpdu_enqued;
    int32_t wmm_drop;
    int32_t local_enqued;
    int32_t local_freed;
    int32_t hw_queued;
    int32_t hw_reaped;
    int32_t underrun;
    uint32_t hw_paused;
    int32_t tx_abort;
    int32_t mpdus_requed;
    uint32_t tx_ko;
    uint32_t data_rc;
    uint32_t self_triggers;
    uint32_t sw_retry_failure;
    uint32_t illgl_rate_phy_err;
    uint32_t pdev_cont_xretry;
    uint32_t pdev_tx_timeout;
    uint32_t pdev_resets;
    uint32_t phy_underrun;
    uint32_t txop_ovf;
    uint32_t seq_posted;
    uint32_t seq_failed_queueing;
    uint32_t seq_completed;
    uint32_t seq_restarted;
    uint32_t mu_seq_posted;
    uint32_t mpdus_sw_flush;
    uint32_t mpdus_hw_filter;
    uint32_t mpdus_truncated;
    uint32_t mpdus_ack_failed;
    uint32_t mpdus_expired;

    /* PDEV RX stats */
    int32_t mid_ppdu_route_change;
    int32_t status_rcvd;
    int32_t r0_frags;
    int32_t r1_frags;
    int32_t r2_frags;
    int32_t r3_frags;
    int32_t htt_msdus;
    int32_t htt_mpdus;
    int32_t loc_msdus;
    int32_t loc_mpdus;
    int32_t oversize_amsdu;
    int32_t phy_errs;
    int32_t phy_err_drop;
    int32_t mpdu_errs;
    int32_t rx_ovfl_errs;
};

struct ath10k_fw_stats {
    bool extended;
    struct list_head pdevs;
    struct list_head vdevs;
    struct list_head peers;
    struct list_head peers_extd;
};

#define ATH10K_TPC_TABLE_TYPE_FLAG  1
#define ATH10K_TPC_PREAM_TABLE_END  0xFFFF

struct ath10k_tpc_table {
    uint32_t pream_idx[WMI_TPC_RATE_MAX];
    uint8_t rate_code[WMI_TPC_RATE_MAX];
    char tpc_value[WMI_TPC_RATE_MAX][WMI_TPC_TX_N_CHAIN * WMI_TPC_BUF_SIZE];
};

struct ath10k_tpc_stats {
    uint32_t reg_domain;
    uint32_t chan_freq;
    uint32_t phy_mode;
    uint32_t twice_antenna_reduction;
    uint32_t twice_max_rd_power;
    int32_t twice_antenna_gain;
    uint32_t power_limit;
    uint32_t num_tx_chain;
    uint32_t ctl;
    uint32_t rate_max;
    uint8_t flag[WMI_TPC_FLAG];
    struct ath10k_tpc_table tpc_table[WMI_TPC_FLAG];
};

struct ath10k_dfs_stats {
    uint32_t phy_errors;
    uint32_t pulses_total;
    uint32_t pulses_detected;
    uint32_t pulses_discarded;
    uint32_t radar_detected;
};

#define ATH10K_MAX_NUM_PEER_IDS (1 << 11) /* htt rx_desc limit */

struct ath10k_peer {
    struct list_head list;
    struct ieee80211_vif* vif;
    struct ieee80211_sta* sta;

    bool removed;
    int vdev_id;
    uint8_t addr[ETH_ALEN];
    BITARR(peer_ids, ATH10K_MAX_NUM_PEER_IDS);

    /* protected by ar->data_lock */
    struct ieee80211_key_conf* keys[WMI_MAX_KEY_INDEX + 1];
};

struct ath10k_sta {
    struct ath10k_vif* arvif;

    /* the following are protected by ar->data_lock */
    uint32_t changed; /* IEEE80211_RC_* */
    uint32_t bw;
    uint32_t nss;
    uint32_t smps;
    uint16_t peer_id;
    struct rate_info txrate;

    struct work_struct update_wk;

#ifdef CONFIG_MAC80211_DEBUGFS
    /* protected by conf_mutex */
    bool aggr_mode;
    uint64_t rx_duration;
#endif
};
#endif // NEEDS PORTING

#define ATH10K_VDEV_SETUP_TIMEOUT (ZX_SEC(5))

enum ath10k_beacon_state {
    ATH10K_BEACON_SCHEDULED = 0,
    ATH10K_BEACON_SENDING,
    ATH10K_BEACON_SENT,
};

struct ath10k_vif_iter {
    uint32_t vdev_id;
    struct ath10k_vif* arvif;
};

/* Copy Engine register dump, protected by ce-lock */
struct ath10k_ce_crash_data {
    uint32_t base_addr;
    uint32_t src_wr_idx;
    uint32_t src_r_idx;
    uint32_t dst_wr_idx;
    uint32_t dst_r_idx;
};

struct ath10k_ce_crash_hdr {
    uint32_t ce_count;
    uint32_t reserved[3]; /* for future use */
    struct ath10k_ce_crash_data entries[];
};

/* used for crash-dump storage, protected by data-lock */
struct ath10k_fw_crash_data {
    bool crashed_since_read;

    uint8_t uuid[16];
    struct timespec timestamp;
    uint32_t registers[REG_DUMP_COUNT_QCA988X];
    struct ath10k_ce_crash_data ce_crash_data[CE_COUNT_MAX];
};

#if 0 // NEEDS PORTING
struct ath10k_debug {
    struct dentry* debugfs_phy;

    struct ath10k_fw_stats fw_stats;
    sync_completion_t fw_stats_complete;
    bool fw_stats_done;

    unsigned long htt_stats_mask;
    struct delayed_work htt_stats_dwork;
    struct ath10k_dfs_stats dfs_stats;
    struct ath_dfs_pool_stats dfs_pool_stats;

    /* used for tpc-dump storage, protected by data-lock */
    struct ath10k_tpc_stats* tpc_stats;

    sync_completion_t tpc_complete;

    /* protected by conf_mutex */
    uint64_t fw_dbglog_mask;
    uint32_t fw_dbglog_level;
    uint32_t pktlog_filter;
    uint32_t reg_addr;
    uint32_t nf_cal_period;
    void* cal_data;

    struct ath10k_fw_crash_data* fw_crash_data;
};
#endif // NEEDS PORTING

enum ath10k_state {
    ATH10K_STATE_OFF = 0,
    ATH10K_STATE_ON,

    /* When doing firmware recovery the device is first powered down.
     * mac80211 is supposed to call in to start() hook later on. It is
     * however possible that driver unloading and firmware crash overlap.
     * mac80211 can wait on conf_mutex in stop() while the device is
     * stopped in ath10k_core_restart() work holding conf_mutex. The state
     * RESTARTED means that the device is up and mac80211 has started hw
     * reconfiguration. Once mac80211 is done with the reconfiguration we
     * set the state to STATE_ON in reconfig_complete().
     */
    ATH10K_STATE_RESTARTING,
    ATH10K_STATE_RESTARTED,

    /* The device has crashed while restarting hw. This state is like ON
     * but commands are blocked in HTC and -ECOMM response is given. This
     * prevents completion timeouts and makes the driver more responsive to
     * userspace commands. This is also prevents recursive recovery.
     */
    ATH10K_STATE_WEDGED,

    /* factory tests */
    ATH10K_STATE_UTF,
};

enum ath10k_firmware_mode {
    /* the default mode, standard 802.11 functionality */
    ATH10K_FIRMWARE_MODE_NORMAL,

    /* factory tests etc */
    ATH10K_FIRMWARE_MODE_UTF,
};

enum ath10k_fw_features {
    /* wmi_mgmt_rx_hdr contains extra RSSI information */
    ATH10K_FW_FEATURE_EXT_WMI_MGMT_RX = 0,

    /* Firmware from 10X branch. Deprecated, don't use in new code. */
    ATH10K_FW_FEATURE_WMI_10X = 1,

    /* firmware support tx frame management over WMI, otherwise it's HTT */
    ATH10K_FW_FEATURE_HAS_WMI_MGMT_TX = 2,

    /* Firmware does not support P2P */
    ATH10K_FW_FEATURE_NO_P2P = 3,

    /* Firmware 10.2 feature bit. The ATH10K_FW_FEATURE_WMI_10X feature
     * bit is required to be set as well. Deprecated, don't use in new
     * code.
     */
    ATH10K_FW_FEATURE_WMI_10_2 = 4,

    /* Some firmware revisions lack proper multi-interface client powersave
     * implementation. Enabling PS could result in connection drops,
     * traffic stalls, etc.
     */
    ATH10K_FW_FEATURE_MULTI_VIF_PS_SUPPORT = 5,

    /* Some firmware revisions have an incomplete WoWLAN implementation
     * despite WMI service bit being advertised. This feature flag is used
     * to distinguish whether WoWLAN is really supported or not.
     */
    ATH10K_FW_FEATURE_WOWLAN_SUPPORT = 6,

    /* Don't trust error code from otp.bin */
    ATH10K_FW_FEATURE_IGNORE_OTP_RESULT = 7,

    /* Some firmware revisions pad 4th hw address to 4 byte boundary making
     * it 8 bytes long in Native Wifi Rx decap.
     */
    ATH10K_FW_FEATURE_NO_NWIFI_DECAP_4ADDR_PADDING = 8,

    /* Firmware supports bypassing PLL setting on init. */
    ATH10K_FW_FEATURE_SUPPORTS_SKIP_CLOCK_INIT = 9,

    /* Raw mode support. If supported, FW supports receiving and trasmitting
     * frames in raw mode.
     */
    ATH10K_FW_FEATURE_RAW_MODE_SUPPORT = 10,

    /* Firmware Supports Adaptive CCA*/
    ATH10K_FW_FEATURE_SUPPORTS_ADAPTIVE_CCA = 11,

    /* Firmware supports management frame protection */
    ATH10K_FW_FEATURE_MFP_SUPPORT = 12,

    /* Firmware supports pull-push model where host shares it's software
     * queue state with firmware and firmware generates fetch requests
     * telling host which queues to dequeue tx from.
     *
     * Primary function of this is improved MU-MIMO performance with
     * multiple clients.
     */
    ATH10K_FW_FEATURE_PEER_FLOW_CONTROL = 13,

    /* Firmware supports BT-Coex without reloading firmware via pdev param.
     * To support Bluetooth coexistence pdev param, WMI_COEX_GPIO_SUPPORT of
     * extended resource config should be enabled always. This firmware IE
     * is used to configure WMI_COEX_GPIO_SUPPORT.
     */
    ATH10K_FW_FEATURE_BTCOEX_PARAM = 14,

    /* Unused flag and proven to be not working, enable this if you want
     * to experiment sending NULL func data frames in HTT TX
     */
    ATH10K_FW_FEATURE_SKIP_NULL_FUNC_WAR = 15,

    /* Firmware allow other BSS mesh broadcast/multicast frames without
     * creating monitor interface. Appropriate rxfilters are programmed for
     * mesh vdev by firmware itself. This feature flags will be used for
     * not creating monitor vdev while configuring mesh node.
     */
    ATH10K_FW_FEATURE_ALLOWS_MESH_BCAST = 16,

    /* keep last */
    ATH10K_FW_FEATURE_COUNT,
};

enum ath10k_dev_flags {
    /* Indicates that ath10k device is during CAC phase of DFS */
    ATH10K_CAC_RUNNING,
    ATH10K_FLAG_CORE_REGISTERED,

    /* Device has crashed and needs to restart. This indicates any pending
     * waiters should immediately cancel instead of waiting for a time out.
     */
    ATH10K_FLAG_CRASH_FLUSH,

    /* Use Raw mode instead of native WiFi Tx/Rx encap mode.
     * Raw mode supports both hardware and software crypto. Native WiFi only
     * supports hardware crypto.
     */
    ATH10K_FLAG_RAW_MODE,

    /* Disable HW crypto engine */
    ATH10K_FLAG_HW_CRYPTO_DISABLED,

    /* Bluetooth coexistance enabled */
    ATH10K_FLAG_BTCOEX,

    /* Per Station statistics service */
    ATH10K_FLAG_PEER_STATS,

    /* keep last */
    ATH10K_FLAG_MAX,
};

enum ath10k_cal_mode {
    ATH10K_CAL_MODE_FILE,
    ATH10K_CAL_MODE_OTP,
    ATH10K_CAL_MODE_DT,
    ATH10K_PRE_CAL_MODE_FILE,
    ATH10K_PRE_CAL_MODE_DT,
    ATH10K_CAL_MODE_EEPROM,
};

enum ath10k_crypt_mode {
    /* Only use hardware crypto engine */
    ATH10K_CRYPT_MODE_HW,
    /* Only use software crypto engine */
    ATH10K_CRYPT_MODE_SW,
};

static inline const char* ath10k_cal_mode_str(enum ath10k_cal_mode mode) {
    switch (mode) {
    case ATH10K_CAL_MODE_FILE:
        return "file";
    case ATH10K_CAL_MODE_OTP:
        return "otp";
    case ATH10K_CAL_MODE_DT:
        return "dt";
    case ATH10K_PRE_CAL_MODE_FILE:
        return "pre-cal-file";
    case ATH10K_PRE_CAL_MODE_DT:
        return "pre-cal-dt";
    case ATH10K_CAL_MODE_EEPROM:
        return "eeprom";
    }

    return "unknown";
}

struct ath10k_firmware {
    zx_handle_t vmo;
    uint8_t* data;
    size_t size;
};

enum ath10k_scan_state {
    ATH10K_SCAN_IDLE,
    ATH10K_SCAN_STARTING,
    ATH10K_SCAN_RUNNING,
    ATH10K_SCAN_ABORTING,
};

static inline const char* ath10k_scan_state_str(enum ath10k_scan_state state) {
    switch (state) {
    case ATH10K_SCAN_IDLE:
        return "idle";
    case ATH10K_SCAN_STARTING:
        return "starting";
    case ATH10K_SCAN_RUNNING:
        return "running";
    case ATH10K_SCAN_ABORTING:
        return "aborting";
    }

    return "unknown";
}

enum ath10k_tx_pause_reason {
    ATH10K_TX_PAUSE_Q_FULL,
    ATH10K_TX_PAUSE_MAX,
};

struct ath10k_fw_file {
    struct ath10k_firmware firmware;

    char fw_version[ATH10K_FW_VER_LEN];

    BITARR(fw_features, ATH10K_FW_FEATURE_COUNT);

    enum ath10k_fw_wmi_op_version wmi_op_version;
    enum ath10k_fw_htt_op_version htt_op_version;

    const void* firmware_data;
    size_t firmware_len;

    const void* otp_data;
    size_t otp_len;

    const void* codeswap_data;
    size_t codeswap_len;

    /* The original idea of struct ath10k_fw_file was that it only
     * contains struct firmware and pointers to various parts (actual
     * firmware binary, otp, metadata etc) of the file. This seg_info
     * is actually created separate but as this is used similarly as
     * the other firmware components it's more convenient to have it
     * here.
     */
    struct ath10k_swap_code_seg_info* firmware_swap_code_seg_info;
};

struct ath10k_fw_components {
    struct ath10k_firmware board;
    const void* board_data;
    size_t board_len;

    struct ath10k_fw_file fw_file;
};

#if 0 // NEEDS PORTING
struct ath10k_per_peer_tx_stats {
    uint32_t succ_bytes;
    uint32_t retry_bytes;
    uint32_t failed_bytes;
    uint8_t  ratecode;
    uint8_t  flags;
    uint16_t peer_id;
    uint16_t succ_pkts;
    uint16_t retry_pkts;
    uint16_t failed_pkts;
    uint16_t duration;
    uint32_t reserved1;
    uint32_t reserved2;
};
#endif // NEEDS PORTING

struct ath10k_vif {
    uint32_t vdev_id;
    uint16_t peer_id;
    enum wmi_vdev_type vdev_type;
    enum wmi_vdev_subtype vdev_subtype;
    uint32_t beacon_interval;
    uint32_t dtim_period;
    void* beacon_buf;
    unsigned long tx_paused; /* arbitrary values defined by target */

    struct ath10k* ar;

    bool is_started;
    bool is_up;
    bool spectral_enabled;
    bool ps;
    uint32_t aid;
    uint8_t bssid[ETH_ALEN];

    uint16_t tx_seq_no;

    union {
        struct {
            uint32_t uapsd;
        } sta;
        struct {
            /* 512 stations */
            uint8_t tim_bitmap[64];
            uint8_t tim_len;
            uint32_t ssid_len;
            uint8_t ssid[IEEE80211_SSID_LEN_MAX];
            bool hidden_ssid;
            /* P2P_IE with NoA attribute for P2P_GO case */
            uint32_t noa_len;
            uint8_t* noa_data;
        } ap;
    } u;

    bool use_cts_prot;
    bool nohwcrypt;
    int num_legacy_stations;
    int txpower;
    struct wmi_wmm_params_all_arg wmm_params;
};

struct ath10k {
    zx_device_t* zxdev;
    uint8_t mac_addr[ETH_ALEN];

    enum ath10k_hw_rev hw_rev;
    uint16_t dev_id;
    uint32_t chip_id;
    uint32_t target_version;
    uint8_t fw_version_major;
    uint32_t fw_version_minor;
    uint16_t fw_version_release;
    uint16_t fw_version_build;
    uint32_t fw_stats_req_mask;
    uint32_t phy_capability;
    uint32_t hw_min_tx_power;
    uint32_t hw_max_tx_power;
    uint32_t hw_eeprom_rd;
    uint32_t ht_cap_info;
    uint32_t vht_cap_info;
    uint32_t num_rf_chains;
    uint32_t max_spatial_stream;
    /* protected by conf_mutex */
    uint32_t low_5ghz_chan;
    uint32_t high_5ghz_chan;
    bool ani_enabled;

    bool p2p;

    struct {
        enum ath10k_bus bus;
        const struct ath10k_hif_ops* ops;
    } hif;

    struct {
       wlanmac_ifc_t* ifc;
       void* cookie;
    } wlanmac;

    sync_completion_t target_suspend;

    const struct ath10k_hw_regs* regs;
    const struct ath10k_hw_ce_regs* hw_ce_regs;
    const struct ath10k_hw_values* hw_values;
    struct ath10k_bmi bmi;
    struct ath10k_wmi wmi;
    struct ath10k_htc htc;
    struct ath10k_htt htt;

    struct ath10k_hw_params hw_params;

    /* contains the firmware images used with ATH10K_FIRMWARE_MODE_NORMAL */
    struct ath10k_fw_components normal_mode_fw;

    /* READ-ONLY images of the running firmware, which can be either
     * normal or UTF. Do not modify, release etc!
     */
    const struct ath10k_fw_components* running_fw;

    struct ath10k_firmware pre_cal_file;
    struct ath10k_firmware cal_file;

    struct {
        uint32_t vendor;
        uint32_t device;
        uint32_t subsystem_vendor;
        uint32_t subsystem_device;

        bool bmi_ids_valid;
        uint8_t bmi_board_id;
        uint8_t bmi_chip_id;

        char bdf_ext[ATH10K_SMBIOS_BDF_EXT_STR_LENGTH];
    } id;

    int fw_api;
    int bd_api;
    enum ath10k_cal_mode cal_mode;

    struct {
        sync_completion_t started;
        sync_completion_t completed;
        sync_completion_t on_channel;
#if 0 // NEEDS PORTING
        struct delayed_work timeout;
#endif // NEEDS PORTING
        enum ath10k_scan_state state;
        bool is_roc;
        int vdev_id;
        int roc_freq;
        bool roc_notify;
    } scan;

#if 0 // NEEDS PORTING
    struct {
        struct ieee80211_supported_band sbands[NUM_NL80211_BANDS];
    } mac;
#endif // NEEDS PORTING

    /* should never be NULL; needed for regular htt rx */
    wlan_channel_t rx_channel;

    /* valid during scan; needed for mgmt rx during scan */
    wlan_channel_t scan_channel;

#if 0 // NEEDS PORTING
    /* current operating channel definition */
    struct cfg80211_chan_def chandef;

    /* currently configured operating channel in firmware */
    struct ieee80211_channel* tgt_oper_chan;
#endif // NEEDS PORTING

    unsigned long long free_vdev_map;
    struct ath10k_vif arvif;
    struct ath10k_vif* monitor_arvif;
    bool monitor;
    int monitor_vdev_id;
    bool monitor_started;
    unsigned int filter_flags;

    BITARR(dev_flags, ATH10K_FLAG_MAX);
    bool dfs_block_radar_events;

    /* protected by conf_mutex */
    bool radar_enabled;
    int num_started_vdevs;

    /* Protected by conf-mutex */
    uint8_t cfg_tx_chainmask;
    uint8_t cfg_rx_chainmask;

    sync_completion_t install_key_done;

    sync_completion_t vdev_setup_done;

#if 0 // NEEDS PORTING
    struct workqueue_struct* workqueue;
    /* Auxiliary workqueue */
    struct workqueue_struct* workqueue_aux;
#endif // NEEDS PORTING

    /* prevents concurrent FW reconfiguration */
    mtx_t conf_mutex;

    /* protects shared structure data */
    mtx_t data_lock;
    /* protects: ar->txqs, artxq->list */
    mtx_t txqs_lock;

    list_node_t txqs;
    list_node_t arvifs;
    list_node_t peers;
#if 0 // NEEDS PORTING
    struct ath10k_peer* peer_map[ATH10K_MAX_NUM_PEER_IDS];
    wait_queue_head_t peer_mapping_wq;
#endif // NEEDS PORTING

    /* protected by conf_mutex */
    int num_peers;
    int num_stations;

    int max_num_peers;
    int max_num_stations;
    int max_num_vdevs;
    int max_num_tdls_vdevs;
    int num_active_peers;
    int num_tids;

    struct ath10k_msg_buf* svc_rdy_buf;

    struct ath10k_msg_buf_state msg_buf_state;

#if 0 // NEEDS PORTING
    struct work_struct offchan_tx_work;
    struct sk_buff_head offchan_tx_queue;
    sync_completion_t offchan_tx_completed;
    struct sk_buff* offchan_tx_skb;

    struct work_struct wmi_mgmt_tx_work;
    struct sk_buff_head wmi_mgmt_tx_queue;
#endif // NEEDS PORTING

    enum ath10k_state state;

    thrd_t isr_thread;
    thrd_t register_work;
    thrd_t restart_work;
    thrd_t assoc_work;
#if DEBUG_MSG_BUF
    thrd_t monitor_thread;
#endif

    mtx_t assoc_lock;
    sync_completion_t assoc_complete;
    struct ath10k_msg_buf* assoc_frame;

#if 0 // NEEDS PORTING
    /* cycle count is reported twice for each visited channel during scan.
     * access protected by data_lock
     */
    uint32_t survey_last_rx_clear_count;
    uint32_t survey_last_cycle_count;
    struct survey_info survey[ATH10K_NUM_CHANS];
#endif // NEEDS PORTING

    /* Channel info events are expected to come in pairs without and with
     * COMPLETE flag set respectively for each channel visit during scan.
     *
     * However there are deviations from this rule. This flag is used to
     * avoid reporting garbage data.
     */
    bool ch_info_can_report_survey;
    sync_completion_t bss_survey_done;

#if 0 // NEEDS PORTING
    struct dfs_pattern_detector* dfs_detector;

    unsigned long tx_paused; /* see ATH10K_TX_PAUSE_ */

#ifdef CONFIG_ATH10K_DEBUGFS
    struct ath10k_debug debug;
    struct {
        /* relay(fs) channel for spectral scan */
        struct rchan* rfs_chan_spec_scan;

        /* spectral_mode and spec_config are protected by conf_mutex */
        enum ath10k_spectral_mode mode;
        struct ath10k_spec_scan config;
    } spectral;
#endif

    struct {
        /* protected by conf_mutex */
        struct ath10k_fw_components utf_mode_fw;

        /* protected by data_lock */
        bool utf_monitor;
    } testmode;
#endif // NEEDS PORTING

    struct {
        /* protected by data_lock */
        uint32_t fw_crash_counter;
        uint32_t fw_warm_reset_counter;
        uint32_t fw_cold_reset_counter;
    } stats;

    struct ath10k_thermal thermal;
    struct ath10k_wow wow;
#if 0 // NEEDS PORTING
    struct ath10k_per_peer_tx_stats peer_tx_stats;

    /* NAPI */
    struct net_device napi_dev;
    struct napi_struct napi;

    struct work_struct set_coverage_class_work;
    /* protected by conf_mutex */
    struct {
        /* writing also protected by data_lock */
        int16_t coverage_class;

        uint32_t reg_phyclk;
        uint32_t reg_slottime_conf;
        uint32_t reg_slottime_orig;
        uint32_t reg_ack_cts_timeout_conf;
        uint32_t reg_ack_cts_timeout_orig;
    } fw_coverage;
#endif // NEEDS PORTING

    /* must be last */
    void* drv_priv;
};

static inline bool ath10k_peer_stats_enabled(struct ath10k* ar) {
    if (BITARR_TEST(ar->dev_flags, ATH10K_FLAG_PEER_STATS)
        && BITARR_TEST(ar->wmi.svc_map, WMI_SERVICE_PEER_STATS)) {
        return true;
    }

    return false;
}

zx_status_t ath10k_core_create(struct ath10k** ar_ptr, size_t priv_size,
                               zx_device_t* dev, enum ath10k_bus bus,
                               enum ath10k_hw_rev hw_rev,
                               const struct ath10k_hif_ops* hif_ops);
void ath10k_core_destroy(struct ath10k* ar);
void ath10k_core_get_fw_features_str(struct ath10k* ar,
                                     char* buf,
                                     size_t max_len);
zx_status_t ath10k_core_fetch_firmware_api_n(struct ath10k* ar, const char* name,
                                             struct ath10k_fw_file* fw_file);

zx_status_t ath10k_core_start(struct ath10k* ar, enum ath10k_firmware_mode mode,
                              const struct ath10k_fw_components* fw_components);
zx_status_t ath10k_wait_for_suspend(struct ath10k* ar, uint32_t suspend_opt);
void ath10k_core_stop(struct ath10k* ar);
zx_status_t ath10k_core_register(struct ath10k* ar, uint32_t chip_id);
void ath10k_core_unregister(struct ath10k* ar);

#endif /* _CORE_H_ */
