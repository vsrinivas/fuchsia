/*
 * Copyright (c) 2005-2011 Atheros Communications Inc.
 * Copyright (c) 2011-2014 Qualcomm Atheros, Inc.
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

#ifndef _WMI_OPS_H_
#define _WMI_OPS_H_

struct ath10k;

struct wmi_ops {
    void (*rx)(struct ath10k* ar,
               struct ath10k_msg_buf* buf);
    void (*map_svc)(const uint32_t* in,
                    BITARR_TYPE* out,
                    size_t len);

    int (*pull_scan)(struct ath10k* ar,
                     struct ath10k_msg_buf* buf,
                     struct wmi_scan_ev_arg* arg);
    int (*pull_mgmt_rx)(struct ath10k* ar,
                        struct ath10k_msg_buf* buf,
                        struct wmi_mgmt_rx_ev_arg* arg);
    int (*pull_ch_info)(struct ath10k* ar,
                        struct ath10k_msg_buf* buf,
                        struct wmi_ch_info_ev_arg* arg);
    int (*pull_vdev_start)(struct ath10k* ar,
                           struct ath10k_msg_buf* buf,
                           struct wmi_vdev_start_ev_arg* arg);
    int (*pull_peer_kick)(struct ath10k* ar,
                          struct ath10k_msg_buf* buf,
                          struct wmi_peer_kick_ev_arg* arg);
    int (*pull_swba)(struct ath10k* ar,
                     struct ath10k_msg_buf* buf,
                     struct wmi_swba_ev_arg* arg);
    int (*pull_phyerr_hdr)(struct ath10k* ar,
                           struct ath10k_msg_buf* buf,
                           struct wmi_phyerr_hdr_arg* arg);
    int (*pull_phyerr)(struct ath10k* ar,
                       const void* phyerr_buf,
                       int left_len,
                       struct wmi_phyerr_ev_arg* arg);
    zx_status_t (*pull_svc_rdy)(struct ath10k* ar,
                                struct ath10k_msg_buf* buf,
                                struct wmi_svc_rdy_ev_arg* arg);
    zx_status_t (*pull_rdy)(struct ath10k* ar,
                            struct ath10k_msg_buf* buf,
                            struct wmi_rdy_ev_arg* arg);
    int (*pull_fw_stats)(struct ath10k* ar,
                         struct ath10k_msg_buf* buf,
                         struct ath10k_fw_stats* stats);
    int (*pull_roam_ev)(struct ath10k* ar,
                        struct ath10k_msg_buf* buf,
                        struct wmi_roam_ev_arg* arg);
    int (*pull_wow_event)(struct ath10k* ar,
                          struct ath10k_msg_buf* buf,
                          struct wmi_wow_ev_arg* arg);
    zx_status_t (*pull_echo_ev)(struct ath10k* ar,
                                struct ath10k_msg_buf* msg_buf,
                                struct wmi_echo_ev_arg* arg);
    enum wmi_txbf_conf (*get_txbf_conf_scheme)(struct ath10k* ar);

    zx_status_t (*gen_pdev_suspend)(struct ath10k* ar,
                                    struct ath10k_msg_buf** msg_buf_ptr,
                                    uint32_t suspend_opt);
    zx_status_t (*gen_pdev_resume)(struct ath10k* ar,
                                   struct ath10k_msg_buf** msg_buf_ptr);
    zx_status_t (*gen_pdev_set_rd)(struct ath10k* ar,
                                   struct ath10k_msg_buf** msg_buf_ptr,
                                   uint16_t rd,
                                   uint16_t rd2g,
                                   uint16_t rd5g,
                                   uint16_t ctl2g,
                                   uint16_t ctl5g,
                                   enum wmi_dfs_region dfs_reg);
    zx_status_t (*gen_pdev_set_param)(struct ath10k* ar,
                                      struct ath10k_msg_buf** msg_buf_ptr,
                                      uint32_t id,
                                      uint32_t value);
    zx_status_t (*gen_init)(struct ath10k* ar,
                            struct ath10k_msg_buf** msg_buf_ptr);
    zx_status_t (*gen_start_scan)(struct ath10k* ar,
                                  struct ath10k_msg_buf** msg_buf_ptr,
                                  const struct wmi_start_scan_arg* arg);
    zx_status_t (*gen_stop_scan)(struct ath10k* ar,
                                 struct ath10k_msg_buf** msg_buf_ptr,
                                 const struct wmi_stop_scan_arg* arg);
    zx_status_t (*gen_vdev_create)(struct ath10k* ar,
                                   struct ath10k_msg_buf** msg_buf_ptr,
                                   uint32_t vdev_id,
                                   enum wmi_vdev_type type,
                                   enum wmi_vdev_subtype subtype,
                                   const uint8_t macaddr[ETH_ALEN]);
    zx_status_t (*gen_vdev_delete)(struct ath10k* ar,
                                   struct ath10k_msg_buf** msg_buf_ptr,
                                   uint32_t vdev_id);
    zx_status_t (*gen_vdev_start)(struct ath10k* ar,
                                  struct ath10k_msg_buf** msg_buf_ptr,
                                  const struct wmi_vdev_start_request_arg* arg,
                                  bool restart);
    zx_status_t (*gen_vdev_stop)(struct ath10k* ar,
                                 struct ath10k_msg_buf** msg_buf_ptr,
                                 uint32_t vdev_id);
    zx_status_t (*gen_vdev_up)(struct ath10k* ar,
                               struct ath10k_msg_buf** msg_buf_ptr,
                               uint32_t vdev_id,
                               uint32_t aid,
                               const uint8_t* bssid);
    zx_status_t (*gen_vdev_down)(struct ath10k* ar,
                                 struct ath10k_msg_buf** msg_buf_ptr,
                                 uint32_t vdev_id);
    zx_status_t (*gen_vdev_set_param)(struct ath10k* ar,
                                      struct ath10k_msg_buf** msg_buf_ptr,
                                      uint32_t vdev_id,
                                      uint32_t param_id,
                                      uint32_t param_value);
    zx_status_t (*gen_vdev_install_key)(struct ath10k* ar,
                                        struct ath10k_msg_buf** msg_buf_ptr,
                                        const struct wmi_vdev_install_key_arg* arg);
    zx_status_t (*gen_vdev_spectral_conf)(struct ath10k* ar,
                                          struct ath10k_msg_buf** msg_buf_ptr,
                                          const struct wmi_vdev_spectral_conf_arg* arg);
    zx_status_t (*gen_vdev_spectral_enable)(struct ath10k* ar,
                                            struct ath10k_msg_buf** msg_buf_ptr,
                                            uint32_t vdev_id,
                                            uint32_t trigger,
                                            uint32_t enable);
    zx_status_t (*gen_vdev_wmm_conf)(struct ath10k* ar,
                                     struct ath10k_msg_buf** msg_buf_ptr,
                                     uint32_t vdev_id,
                                     const struct wmi_wmm_params_all_arg* arg);
    zx_status_t (*gen_peer_create)(struct ath10k* ar,
                                   struct ath10k_msg_buf** msg_buf_ptr,
                                   uint32_t vdev_id,
                                   const uint8_t peer_addr[ETH_ALEN],
                                   enum wmi_peer_type peer_type);
    zx_status_t (*gen_peer_delete)(struct ath10k* ar,
                                   struct ath10k_msg_buf** msg_buf_ptr,
                                   uint32_t vdev_id,
                                   const uint8_t peer_addr[ETH_ALEN]);
    zx_status_t (*gen_peer_flush)(struct ath10k* ar,
                                  struct ath10k_msg_buf** msg_buf_ptr,
                                  uint32_t vdev_id,
                                  const uint8_t peer_addr[ETH_ALEN],
                                  uint32_t tid_bitmap);
    zx_status_t (*gen_peer_set_param)(struct ath10k* ar,
                                      struct ath10k_msg_buf** msg_buf_ptr,
                                      uint32_t vdev_id,
                                      const uint8_t* peer_addr,
                                      enum wmi_peer_param param_id,
                                      uint32_t param_value);
    zx_status_t (*gen_peer_assoc)(struct ath10k* ar,
                                  struct ath10k_msg_buf** msg_buf_ptr,
                                  const struct wmi_peer_assoc_complete_arg* arg);
    zx_status_t (*gen_set_psmode)(struct ath10k* ar,
                                  struct ath10k_msg_buf** msg_buf_ptr,
                                  uint32_t vdev_id,
                                  enum wmi_sta_ps_mode psmode);
    zx_status_t (*gen_set_sta_ps)(struct ath10k* ar,
                                  struct ath10k_msg_buf** msg_buf_ptr,
                                  uint32_t vdev_id,
                                  enum wmi_sta_powersave_param param_id,
                                  uint32_t value);
    zx_status_t (*gen_set_ap_ps)(struct ath10k* ar,
                                 struct ath10k_msg_buf** msg_buf_ptr,
                                 uint32_t vdev_id,
                                 const uint8_t* mac,
                                 enum wmi_ap_ps_peer_param param_id,
                                 uint32_t value);
    zx_status_t (*gen_scan_chan_list)(struct ath10k* ar,
                                      struct ath10k_msg_buf** msg_buf_ptr,
                                      const struct wmi_scan_chan_list_arg* arg);
    zx_status_t (*gen_beacon_dma)(struct ath10k* ar,
                                  struct ath10k_msg_buf** msg_buf_ptr,
                                  uint32_t vdev_id,
                                  const void* bcn,
                                  size_t bcn_len,
                                  uint32_t bcn_paddr,
                                  bool dtim_zero,
                                  bool deliver_cab);
    zx_status_t (*gen_pdev_set_wmm)(struct ath10k* ar,
                                    struct ath10k_msg_buf** msg_buf_ptr,
                                    const struct wmi_wmm_params_all_arg* arg);
    zx_status_t (*gen_request_stats)(struct ath10k* ar,
                                     struct ath10k_msg_buf** msg_buf_ptr,
                                     uint32_t stats_mask);
    zx_status_t (*gen_force_fw_hang)(struct ath10k* ar,
                                     struct ath10k_msg_buf** msg_buf_ptr,
                                     enum wmi_force_fw_hang_type type,
                                     uint32_t delay_ms);
    zx_status_t (*gen_mgmt_tx)(struct ath10k* ar,
                               struct ath10k_msg_buf** msg_buf_ptr,
                               struct ath10k_msg_buf* msdu);
    zx_status_t (*gen_dbglog_cfg)(struct ath10k* ar,
                                  struct ath10k_msg_buf** msg_buf_ptr,
                                  uint64_t module_enable,
                                  uint32_t log_level);
    zx_status_t (*gen_pktlog_enable)(struct ath10k* ar,
                                     struct ath10k_msg_buf** msg_buf_ptr,
                                     uint32_t filter);
    zx_status_t (*gen_pktlog_disable)(struct ath10k* ar,
                                      struct ath10k_msg_buf** msg_buf_ptr);
    zx_status_t (*gen_pdev_set_quiet_mode)(struct ath10k* ar,
                                           struct ath10k_msg_buf** msg_buf_ptr,
                                           uint32_t period,
                                           uint32_t duration,
                                           uint32_t next_offset,
                                           uint32_t enabled);
    zx_status_t (*gen_pdev_get_temperature)(struct ath10k* ar,
                                            struct ath10k_msg_buf** msg_buf_ptr);
    zx_status_t (*gen_addba_clear_resp)(struct ath10k* ar,
                                        struct ath10k_msg_buf** msg_buf_ptr,
                                        uint32_t vdev_id,
                                        const uint8_t* mac);
    zx_status_t (*gen_addba_send)(struct ath10k* ar,
                                  struct ath10k_msg_buf** msg_buf_ptr,
                                  uint32_t vdev_id,
                                  const uint8_t* mac,
                                  uint32_t tid,
                                  uint32_t buf_size);
    zx_status_t (*gen_addba_set_resp)(struct ath10k* ar,
                                      struct ath10k_msg_buf** msg_buf_ptr,
                                      uint32_t vdev_id,
                                      const uint8_t* mac,
                                      uint32_t tid,
                                      uint32_t status);
    zx_status_t (*gen_delba_send)(struct ath10k* ar,
                                  struct ath10k_msg_buf** msg_buf_ptr,
                                  uint32_t vdev_id,
                                  const uint8_t* mac,
                                  uint32_t tid,
                                  uint32_t initiator,
                                  uint32_t reason);
    zx_status_t (*gen_bcn_tmpl)(struct ath10k* ar,
                                struct ath10k_msg_buf** msg_buf_ptr,
                                uint32_t vdev_id,
                                uint32_t tim_ie_offset,
                                struct ath10k_msg_buf* bcn,
                                uint32_t prb_caps,
                                uint32_t prb_erp,
                                void* prb_ies,
                                size_t prb_ies_len);
    zx_status_t (*gen_prb_tmpl)(struct ath10k* ar,
                                struct ath10k_msg_buf** msg_buf_ptr,
                                uint32_t vdev_id,
                                struct ath10k_msg_buf* bcn);
    zx_status_t (*gen_p2p_go_bcn_ie)(struct ath10k* ar,
                                     struct ath10k_msg_buf** msg_buf_ptr,
                                     uint32_t vdev_id,
                                     const uint8_t* p2p_ie);
    zx_status_t (*gen_vdev_sta_uapsd)(struct ath10k* ar,
                                      struct ath10k_msg_buf** msg_buf_ptr,
                                      uint32_t vdev_id,
                                      const uint8_t peer_addr[ETH_ALEN],
                                      const struct wmi_sta_uapsd_auto_trig_arg* args,
                                      uint32_t num_ac);
    zx_status_t (*gen_sta_keepalive)(struct ath10k* ar,
                                     struct ath10k_msg_buf** msg_buf_ptr,
                                     const struct wmi_sta_keepalive_arg* arg);
    zx_status_t (*gen_wow_enable)(struct ath10k* ar,
                                  struct ath10k_msg_buf** msg_buf_ptr);
    zx_status_t (*gen_wow_add_wakeup_event)(struct ath10k* ar,
                                            struct ath10k_msg_buf** msg_buf_ptr,
                                            uint32_t vdev_id,
                                            enum wmi_wow_wakeup_event event,
                                            uint32_t enable);
    zx_status_t (*gen_wow_host_wakeup_ind)(struct ath10k* ar,
                                           struct ath10k_msg_buf** msg_buf_ptr);
    zx_status_t (*gen_wow_add_pattern)(struct ath10k* ar,
                                       struct ath10k_msg_buf** msg_buf_ptr,
                                       uint32_t vdev_id,
                                       uint32_t pattern_id,
                                       const uint8_t* pattern,
                                       const uint8_t* mask,
                                       int pattern_len,
                                       int pattern_offset);
    zx_status_t (*gen_wow_del_pattern)(struct ath10k* ar,
                                       struct ath10k_msg_buf** msg_buf_ptr,
                                       uint32_t vdev_id,
                                       uint32_t pattern_id);
    zx_status_t (*gen_update_fw_tdls_state)(struct ath10k* ar,
                                            struct ath10k_msg_buf** msg_buf_ptr,
                                            uint32_t vdev_id,
                                            enum wmi_tdls_state state);
    zx_status_t (*gen_tdls_peer_update)(struct ath10k* ar,
                                        struct ath10k_msg_buf** msg_buf_ptr,
                                        const struct wmi_tdls_peer_update_cmd_arg* arg,
                                        const struct wmi_tdls_peer_capab_arg* cap,
                                        const struct wmi_channel_arg* chan);
    zx_status_t (*gen_adaptive_qcs)(struct ath10k* ar,
                                    struct ath10k_msg_buf** msg_buf_ptr,
                                    bool enable);
    zx_status_t (*gen_pdev_get_tpc_config)(struct ath10k* ar,
                                           struct ath10k_msg_buf** msg_buf_ptr,
                                           uint32_t param);
    void (*fw_stats_fill)(struct ath10k* ar,
                          struct ath10k_fw_stats* fw_stats,
                          char* buf);
    zx_status_t (*gen_pdev_enable_adaptive_cca)(struct ath10k* ar,
                                                struct ath10k_msg_buf** msg_buf_ptr,
                                                uint8_t enable,
                                                uint32_t detect_level,
                                                uint32_t detect_margin);
    zx_status_t (*ext_resource_config)(struct ath10k* ar,
                                       struct ath10k_msg_buf** msg_buf_ptr,
                                       enum wmi_host_platform_type type,
                                       uint32_t fw_feature_bitmap);
    int (*get_vdev_subtype)(struct ath10k* ar,
                            enum wmi_vdev_subtype subtype);
    zx_status_t (*gen_pdev_bss_chan_info_req)(struct ath10k* ar,
                                              struct ath10k_msg_buf** msg_buf_ptr,
                                              enum wmi_bss_survey_req_type type);
    zx_status_t (*gen_echo)(struct ath10k* ar,
                            struct ath10k_msg_buf** msg_buf_ptr,
                            uint32_t value);
};

zx_status_t ath10k_wmi_cmd_send(struct ath10k* ar, struct ath10k_msg_buf* buf, uint32_t cmd_id);

static inline zx_status_t
ath10k_wmi_rx(struct ath10k* ar, struct ath10k_msg_buf* buf) {
    if (COND_WARN_ONCE(!ar->wmi.ops->rx)) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    ar->wmi.ops->rx(ar, buf);
    return ZX_OK;
}

static inline zx_status_t
ath10k_wmi_map_svc(struct ath10k* ar, const uint32_t* in, BITARR_TYPE* out, size_t len) {
    if (!ar->wmi.ops->map_svc) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    ar->wmi.ops->map_svc(in, out, len);
    return ZX_OK;
}

static inline zx_status_t
ath10k_wmi_pull_scan(struct ath10k* ar, struct ath10k_msg_buf* buf, struct wmi_scan_ev_arg* arg) {
    if (!ar->wmi.ops->pull_scan) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    return ar->wmi.ops->pull_scan(ar, buf, arg);
}

static inline zx_status_t
ath10k_wmi_pull_mgmt_rx(struct ath10k* ar, struct ath10k_msg_buf* buf,
                        struct wmi_mgmt_rx_ev_arg* arg) {
    if (!ar->wmi.ops->pull_mgmt_rx) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    return ar->wmi.ops->pull_mgmt_rx(ar, buf, arg);
}

static inline zx_status_t
ath10k_wmi_pull_ch_info(struct ath10k* ar, struct ath10k_msg_buf* buf,
                        struct wmi_ch_info_ev_arg* arg) {
    if (!ar->wmi.ops->pull_ch_info) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    return ar->wmi.ops->pull_ch_info(ar, buf, arg);
}

static inline zx_status_t
ath10k_wmi_pull_vdev_start(struct ath10k* ar, struct ath10k_msg_buf* buf,
                           struct wmi_vdev_start_ev_arg* arg) {
    if (!ar->wmi.ops->pull_vdev_start) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    return ar->wmi.ops->pull_vdev_start(ar, buf, arg);
}

static inline zx_status_t
ath10k_wmi_pull_peer_kick(struct ath10k* ar, struct ath10k_msg_buf* buf,
                          struct wmi_peer_kick_ev_arg* arg) {
    if (!ar->wmi.ops->pull_peer_kick) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    return ar->wmi.ops->pull_peer_kick(ar, buf, arg);
}

static inline zx_status_t
ath10k_wmi_pull_swba(struct ath10k* ar, struct ath10k_msg_buf* buf,
                     struct wmi_swba_ev_arg* arg) {
    if (!ar->wmi.ops->pull_swba) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    return ar->wmi.ops->pull_swba(ar, buf, arg);
}

static inline zx_status_t
ath10k_wmi_pull_phyerr_hdr(struct ath10k* ar, struct ath10k_msg_buf* buf,
                           struct wmi_phyerr_hdr_arg* arg) {
    if (!ar->wmi.ops->pull_phyerr_hdr) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    return ar->wmi.ops->pull_phyerr_hdr(ar, buf, arg);
}

static inline zx_status_t
ath10k_wmi_pull_phyerr(struct ath10k* ar, const void* phyerr_buf,
                       int left_len, struct wmi_phyerr_ev_arg* arg) {
    if (!ar->wmi.ops->pull_phyerr) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    return ar->wmi.ops->pull_phyerr(ar, phyerr_buf, left_len, arg);
}

static inline zx_status_t
ath10k_wmi_pull_svc_rdy(struct ath10k* ar, struct ath10k_msg_buf* msg_buf,
                        struct wmi_svc_rdy_ev_arg* arg) {
    if (!ar->wmi.ops->pull_svc_rdy) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    return ar->wmi.ops->pull_svc_rdy(ar, msg_buf, arg);
}

static inline zx_status_t
ath10k_wmi_pull_rdy(struct ath10k* ar, struct ath10k_msg_buf* msg_buf,
                    struct wmi_rdy_ev_arg* arg) {
    if (!ar->wmi.ops->pull_rdy) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    return ar->wmi.ops->pull_rdy(ar, msg_buf, arg);
}

static inline zx_status_t
ath10k_wmi_pull_fw_stats(struct ath10k* ar, struct ath10k_msg_buf* buf,
                         struct ath10k_fw_stats* stats) {
    if (!ar->wmi.ops->pull_fw_stats) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    return ar->wmi.ops->pull_fw_stats(ar, buf, stats);
}

static inline zx_status_t
ath10k_wmi_pull_roam_ev(struct ath10k* ar, struct ath10k_msg_buf* buf,
                        struct wmi_roam_ev_arg* arg) {
    if (!ar->wmi.ops->pull_roam_ev) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    return ar->wmi.ops->pull_roam_ev(ar, buf, arg);
}

static inline zx_status_t
ath10k_wmi_pull_wow_event(struct ath10k* ar, struct ath10k_msg_buf* buf,
                          struct wmi_wow_ev_arg* arg) {
    if (!ar->wmi.ops->pull_wow_event) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    return ar->wmi.ops->pull_wow_event(ar, buf, arg);
}

static inline zx_status_t
ath10k_wmi_pull_echo_ev(struct ath10k* ar, struct ath10k_msg_buf* msg_buf,
                        struct wmi_echo_ev_arg* arg) {
    if (!ar->wmi.ops->pull_echo_ev) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    return ar->wmi.ops->pull_echo_ev(ar, msg_buf, arg);
}

static inline enum wmi_txbf_conf
ath10k_wmi_get_txbf_conf_scheme(struct ath10k* ar) {
    if (!ar->wmi.ops->get_txbf_conf_scheme) {
        return WMI_TXBF_CONF_UNSUPPORTED;
    }

    return ar->wmi.ops->get_txbf_conf_scheme(ar);
}

static inline zx_status_t
ath10k_wmi_mgmt_tx(struct ath10k* ar, struct ath10k_msg_buf* msdu) {

    struct ath10k_msg_buf* buf;
    zx_status_t ret;

    if (!ar->wmi.ops->gen_mgmt_tx) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    ret = ar->wmi.ops->gen_mgmt_tx(ar, &buf, msdu);
    if (ret != ZX_OK) {
        return ret;
    }

    ret = ath10k_wmi_cmd_send(ar, buf, ar->wmi.cmd->mgmt_tx_cmdid);
    if (ret != ZX_OK) {
        return ret;
    }

    return ZX_OK;
}

static inline zx_status_t
ath10k_wmi_pdev_set_regdomain(struct ath10k* ar, uint16_t rd, uint16_t rd2g, uint16_t rd5g,
                              uint16_t ctl2g, uint16_t ctl5g, enum wmi_dfs_region dfs_reg) {
    zx_status_t status;
    struct ath10k_msg_buf* buf;

    if (!ar->wmi.ops->gen_pdev_set_rd) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    status = ar->wmi.ops->gen_pdev_set_rd(ar, &buf, rd, rd2g, rd5g, ctl2g, ctl5g, dfs_reg);
    if (status != ZX_OK) {
        return status;
    }

    return ath10k_wmi_cmd_send(ar, buf, ar->wmi.cmd->pdev_set_regdomain_cmdid);
}

static inline zx_status_t
ath10k_wmi_pdev_suspend_target(struct ath10k* ar, uint32_t suspend_opt) {
    struct ath10k_msg_buf* buf;

    if (!ar->wmi.ops->gen_pdev_suspend) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    zx_status_t status = ar->wmi.ops->gen_pdev_suspend(ar, &buf, suspend_opt);
    if (status != ZX_OK) {
        return status;
    }

    return ath10k_wmi_cmd_send(ar, buf, ar->wmi.cmd->pdev_suspend_cmdid);
}

static inline zx_status_t
ath10k_wmi_pdev_resume_target(struct ath10k* ar) {
    struct ath10k_msg_buf* buf;
    zx_status_t status;

    if (!ar->wmi.ops->gen_pdev_resume) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    status = ar->wmi.ops->gen_pdev_resume(ar, &buf);
    if (status != ZX_OK) {
        return status;
    }

    return ath10k_wmi_cmd_send(ar, buf, ar->wmi.cmd->pdev_resume_cmdid);
}

static inline zx_status_t
ath10k_wmi_pdev_set_param(struct ath10k* ar, uint32_t id, uint32_t value) {
    struct ath10k_msg_buf* buf;
    zx_status_t status;

    if (!ar->wmi.ops->gen_pdev_set_param) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    status = ar->wmi.ops->gen_pdev_set_param(ar, &buf, id, value);
    if (status != ZX_OK) {
        return status;
    }

    return ath10k_wmi_cmd_send(ar, buf, ar->wmi.cmd->pdev_set_param_cmdid);
}

static inline zx_status_t
ath10k_wmi_cmd_init(struct ath10k* ar) {
    struct ath10k_msg_buf* buf;
    zx_status_t status;

    if (!ar->wmi.ops->gen_init) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    status = ar->wmi.ops->gen_init(ar, &buf);
    if (status != ZX_OK) {
        return status;
    }

    return ath10k_wmi_cmd_send(ar, buf, ar->wmi.cmd->init_cmdid);
}

static inline zx_status_t
ath10k_wmi_start_scan(struct ath10k* ar,
                      const struct wmi_start_scan_arg* arg) {
    struct ath10k_msg_buf* buf;

    if (!ar->wmi.ops->gen_start_scan) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    zx_status_t status = ar->wmi.ops->gen_start_scan(ar, &buf, arg);
    if (status != ZX_OK) {
        return status;
    }

    return ath10k_wmi_cmd_send(ar, buf, ar->wmi.cmd->start_scan_cmdid);
}

static inline zx_status_t
ath10k_wmi_stop_scan(struct ath10k* ar, const struct wmi_stop_scan_arg* arg) {
    struct ath10k_msg_buf* buf;

    if (!ar->wmi.ops->gen_stop_scan) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    zx_status_t status = ar->wmi.ops->gen_stop_scan(ar, &buf, arg);
    if (status != ZX_OK) {
        return status;
    }

    return ath10k_wmi_cmd_send(ar, buf, ar->wmi.cmd->stop_scan_cmdid);
}

static inline zx_status_t
ath10k_wmi_vdev_create(struct ath10k* ar, uint32_t vdev_id,
                       enum wmi_vdev_type type,
                       enum wmi_vdev_subtype subtype,
                       const uint8_t macaddr[ETH_ALEN]) {
    struct ath10k_msg_buf* buf;

    if (!ar->wmi.ops->gen_vdev_create) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    zx_status_t status = ar->wmi.ops->gen_vdev_create(ar, &buf, vdev_id, type, subtype, macaddr);
    if (status != ZX_OK) {
        return status;
    }

    return ath10k_wmi_cmd_send(ar, buf, ar->wmi.cmd->vdev_create_cmdid);
}

static inline zx_status_t
ath10k_wmi_vdev_delete(struct ath10k* ar, uint32_t vdev_id) {
    struct ath10k_msg_buf* buf;

    if (!ar->wmi.ops->gen_vdev_delete) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    zx_status_t status = ar->wmi.ops->gen_vdev_delete(ar, &buf, vdev_id);
    if (status != ZX_OK) {
        return status;
    }

    return ath10k_wmi_cmd_send(ar, buf, ar->wmi.cmd->vdev_delete_cmdid);
}

static inline zx_status_t
ath10k_wmi_vdev_start(struct ath10k* ar,
                      const struct wmi_vdev_start_request_arg* arg) {
    struct ath10k_msg_buf* buf;

    if (!ar->wmi.ops->gen_vdev_start) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    zx_status_t status = ar->wmi.ops->gen_vdev_start(ar, &buf, arg, false);
    if (status != ZX_OK) {
        return status;
    }

    return ath10k_wmi_cmd_send(ar, buf,
                               ar->wmi.cmd->vdev_start_request_cmdid);
}

static inline zx_status_t
ath10k_wmi_vdev_restart(struct ath10k* ar,
                        const struct wmi_vdev_start_request_arg* arg) {
    struct ath10k_msg_buf* buf;

    if (!ar->wmi.ops->gen_vdev_start) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    zx_status_t status = ar->wmi.ops->gen_vdev_start(ar, &buf, arg, true);
    if (status != ZX_OK) {
        return status;
    }

    return ath10k_wmi_cmd_send(ar, buf,
                               ar->wmi.cmd->vdev_restart_request_cmdid);
}

static inline zx_status_t
ath10k_wmi_vdev_stop(struct ath10k* ar, uint32_t vdev_id) {
    struct ath10k_msg_buf* buf;

    if (!ar->wmi.ops->gen_vdev_stop) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    zx_status_t status = ar->wmi.ops->gen_vdev_stop(ar, &buf, vdev_id);
    if (status != ZX_OK) {
        return status;
    }

    return ath10k_wmi_cmd_send(ar, buf, ar->wmi.cmd->vdev_stop_cmdid);
}

static inline zx_status_t
ath10k_wmi_vdev_up(struct ath10k* ar, uint32_t vdev_id, uint32_t aid, const uint8_t* bssid) {
    struct ath10k_msg_buf* buf;

    if (!ar->wmi.ops->gen_vdev_up) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    zx_status_t status = ar->wmi.ops->gen_vdev_up(ar, &buf, vdev_id, aid, bssid);
    if (status != ZX_OK) {
        return status;
    }

    return ath10k_wmi_cmd_send(ar, buf, ar->wmi.cmd->vdev_up_cmdid);
}

static inline zx_status_t
ath10k_wmi_vdev_down(struct ath10k* ar, uint32_t vdev_id) {
    struct ath10k_msg_buf* buf;

    if (!ar->wmi.ops->gen_vdev_down) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    zx_status_t status = ar->wmi.ops->gen_vdev_down(ar, &buf, vdev_id);
    if (status != ZX_OK) {
        return status;
    }

    return ath10k_wmi_cmd_send(ar, buf, ar->wmi.cmd->vdev_down_cmdid);
}

static inline zx_status_t
ath10k_wmi_vdev_set_param(struct ath10k* ar, uint32_t vdev_id, uint32_t param_id,
                          uint32_t param_value) {
    struct ath10k_msg_buf* buf;

    if (!ar->wmi.ops->gen_vdev_set_param) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    zx_status_t status = ar->wmi.ops->gen_vdev_set_param(ar, &buf, vdev_id, param_id,
                                                         param_value);
    if (status != ZX_OK) {
        return status;
    }

    return ath10k_wmi_cmd_send(ar, buf, ar->wmi.cmd->vdev_set_param_cmdid);
}

static inline zx_status_t
ath10k_wmi_vdev_install_key(struct ath10k* ar,
                            const struct wmi_vdev_install_key_arg* arg) {
    struct ath10k_msg_buf* buf;

    if (!ar->wmi.ops->gen_vdev_install_key) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    zx_status_t status = ar->wmi.ops->gen_vdev_install_key(ar, &buf, arg);
    if (status != ZX_OK) {
        return status;
    }

    return ath10k_wmi_cmd_send(ar, buf,
                               ar->wmi.cmd->vdev_install_key_cmdid);
}

static inline zx_status_t
ath10k_wmi_vdev_spectral_conf(struct ath10k* ar,
                              const struct wmi_vdev_spectral_conf_arg* arg) {
    struct ath10k_msg_buf* buf;
    uint32_t cmd_id;

    if (!ar->wmi.ops->gen_vdev_spectral_conf) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    zx_status_t status = ar->wmi.ops->gen_vdev_spectral_conf(ar, &buf, arg);
    if (status != ZX_OK) {
        return status;
    }

    cmd_id = ar->wmi.cmd->vdev_spectral_scan_configure_cmdid;
    return ath10k_wmi_cmd_send(ar, buf, cmd_id);
}

static inline zx_status_t
ath10k_wmi_vdev_spectral_enable(struct ath10k* ar, uint32_t vdev_id, uint32_t trigger,
                                uint32_t enable) {
    struct ath10k_msg_buf* buf;
    uint32_t cmd_id;

    if (!ar->wmi.ops->gen_vdev_spectral_enable) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    zx_status_t status = ar->wmi.ops->gen_vdev_spectral_enable(ar, &buf, vdev_id, trigger,
                                                               enable);
    if (status != ZX_OK) {
        return status;
    }

    cmd_id = ar->wmi.cmd->vdev_spectral_scan_enable_cmdid;
    return ath10k_wmi_cmd_send(ar, buf, cmd_id);
}

static inline zx_status_t
ath10k_wmi_vdev_sta_uapsd(struct ath10k* ar, uint32_t vdev_id,
                          const uint8_t peer_addr[ETH_ALEN],
                          const struct wmi_sta_uapsd_auto_trig_arg* args,
                          uint32_t num_ac) {
    struct ath10k_msg_buf* buf;
    uint32_t cmd_id;

    if (!ar->wmi.ops->gen_vdev_sta_uapsd) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    zx_status_t status = ar->wmi.ops->gen_vdev_sta_uapsd(ar, &buf, vdev_id, peer_addr, args,
                                                         num_ac);
    if (status != ZX_OK) {
        return status;
    }

    cmd_id = ar->wmi.cmd->sta_uapsd_auto_trig_cmdid;
    return ath10k_wmi_cmd_send(ar, buf, cmd_id);
}

static inline zx_status_t
ath10k_wmi_vdev_wmm_conf(struct ath10k* ar, uint32_t vdev_id,
                         const struct wmi_wmm_params_all_arg* arg) {
    struct ath10k_msg_buf* buf;
    uint32_t cmd_id;

    zx_status_t status = ar->wmi.ops->gen_vdev_wmm_conf(ar, &buf, vdev_id, arg);
    if (status != ZX_OK) {
        return status;
    }

    cmd_id = ar->wmi.cmd->vdev_set_wmm_params_cmdid;
    return ath10k_wmi_cmd_send(ar, buf, cmd_id);
}

static inline zx_status_t
ath10k_wmi_peer_create(struct ath10k* ar, uint32_t vdev_id,
                       const uint8_t peer_addr[ETH_ALEN],
                       enum wmi_peer_type peer_type) {
    struct ath10k_msg_buf* buf;

    if (!ar->wmi.ops->gen_peer_create) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    zx_status_t status = ar->wmi.ops->gen_peer_create(ar, &buf, vdev_id, peer_addr, peer_type);
    if (status != ZX_OK) {
        return status;
    }

    return ath10k_wmi_cmd_send(ar, buf, ar->wmi.cmd->peer_create_cmdid);
}

static inline zx_status_t
ath10k_wmi_peer_delete(struct ath10k* ar, uint32_t vdev_id,
                       const uint8_t peer_addr[ETH_ALEN]) {
    struct ath10k_msg_buf* buf;

    if (!ar->wmi.ops->gen_peer_delete) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    zx_status_t status = ar->wmi.ops->gen_peer_delete(ar, &buf, vdev_id, peer_addr);
    if (status != ZX_OK) {
        return status;
    }

    return ath10k_wmi_cmd_send(ar, buf, ar->wmi.cmd->peer_delete_cmdid);
}

static inline zx_status_t
ath10k_wmi_peer_flush(struct ath10k* ar, uint32_t vdev_id,
                      const uint8_t peer_addr[ETH_ALEN], uint32_t tid_bitmap) {
    struct ath10k_msg_buf* buf;

    if (!ar->wmi.ops->gen_peer_flush) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    zx_status_t status = ar->wmi.ops->gen_peer_flush(ar, &buf, vdev_id, peer_addr, tid_bitmap);
    if (status != ZX_OK) {
        return status;
    }

    return ath10k_wmi_cmd_send(ar, buf, ar->wmi.cmd->peer_flush_tids_cmdid);
}

static inline zx_status_t
ath10k_wmi_peer_set_param(struct ath10k* ar, uint32_t vdev_id, const uint8_t* peer_addr,
                          enum wmi_peer_param param_id, uint32_t param_value) {
    struct ath10k_msg_buf* buf;

    if (!ar->wmi.ops->gen_peer_set_param) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    zx_status_t status = ar->wmi.ops->gen_peer_set_param(ar, &buf, vdev_id, peer_addr, param_id,
                                                         param_value);
    if (status != ZX_OK) {
        return status;
    }

    return ath10k_wmi_cmd_send(ar, buf, ar->wmi.cmd->peer_set_param_cmdid);
}

static inline zx_status_t
ath10k_wmi_set_psmode(struct ath10k* ar, uint32_t vdev_id,
                      enum wmi_sta_ps_mode psmode) {
    struct ath10k_msg_buf* buf;

    if (!ar->wmi.ops->gen_set_psmode) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    zx_status_t status = ar->wmi.ops->gen_set_psmode(ar, &buf, vdev_id, psmode);
    if (status != ZX_OK) {
        return status;
    }

    return ath10k_wmi_cmd_send(ar, buf,
                               ar->wmi.cmd->sta_powersave_mode_cmdid);
}

static inline zx_status_t
ath10k_wmi_set_sta_ps_param(struct ath10k* ar, uint32_t vdev_id,
                            enum wmi_sta_powersave_param param_id, uint32_t value) {
    struct ath10k_msg_buf* buf;

    if (!ar->wmi.ops->gen_set_sta_ps) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    zx_status_t status = ar->wmi.ops->gen_set_sta_ps(ar, &buf, vdev_id, param_id, value);
    if (status != ZX_OK) {
        return status;
    }

    return ath10k_wmi_cmd_send(ar, buf, ar->wmi.cmd->sta_powersave_param_cmdid);
}

static inline zx_status_t
ath10k_wmi_set_ap_ps_param(struct ath10k* ar, uint32_t vdev_id, const uint8_t* mac,
                           enum wmi_ap_ps_peer_param param_id, uint32_t value) {
    struct ath10k_msg_buf* buf;

    if (!ar->wmi.ops->gen_set_ap_ps) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    zx_status_t status = ar->wmi.ops->gen_set_ap_ps(ar, &buf, vdev_id, mac, param_id, value);
    if (status != ZX_OK) {
        return status;
    }

    return ath10k_wmi_cmd_send(ar, buf, ar->wmi.cmd->ap_ps_peer_param_cmdid);
}

static inline zx_status_t
ath10k_wmi_scan_chan_list(struct ath10k* ar,
                          const struct wmi_scan_chan_list_arg* arg) {
    struct ath10k_msg_buf* buf;

    if (!ar->wmi.ops->gen_scan_chan_list) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    zx_status_t status = ar->wmi.ops->gen_scan_chan_list(ar, &buf, arg);
    if (status != ZX_OK) {
        return status;
    }

    return ath10k_wmi_cmd_send(ar, buf, ar->wmi.cmd->scan_chan_list_cmdid);
}

static inline zx_status_t
ath10k_wmi_peer_assoc(struct ath10k* ar,
                      const struct wmi_peer_assoc_complete_arg* arg) {
    struct ath10k_msg_buf* buf;

    if (!ar->wmi.ops->gen_peer_assoc) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    zx_status_t status = ar->wmi.ops->gen_peer_assoc(ar, &buf, arg);
    if (status != ZX_OK) {
        return status;
    }

    return ath10k_wmi_cmd_send(ar, buf, ar->wmi.cmd->peer_assoc_cmdid);
}

static inline zx_status_t
ath10k_wmi_beacon_send_ref_nowait(struct ath10k* ar, uint32_t vdev_id,
                                  const void* bcn, size_t bcn_len,
                                  uint32_t bcn_paddr, bool dtim_zero,
                                  bool deliver_cab) {
    struct ath10k_msg_buf* buf;

    if (!ar->wmi.ops->gen_beacon_dma) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    zx_status_t status = ar->wmi.ops->gen_beacon_dma(ar, &buf, vdev_id, bcn, bcn_len, bcn_paddr,
                                                     dtim_zero, deliver_cab);
    if (status != ZX_OK) {
        return status;
    }

    status = ath10k_wmi_cmd_send_nowait(ar, buf, ar->wmi.cmd->pdev_send_bcn_cmdid);
    if (status != ZX_OK) {
        ath10k_msg_buf_free(buf);
        return status;
    }

    return ZX_OK;
}

static inline zx_status_t
ath10k_wmi_pdev_set_wmm_params(struct ath10k* ar,
                               const struct wmi_wmm_params_all_arg* arg) {
    struct ath10k_msg_buf* buf;

    if (!ar->wmi.ops->gen_pdev_set_wmm) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    zx_status_t status = ar->wmi.ops->gen_pdev_set_wmm(ar, &buf, arg);
    if (status != ZX_OK) {
        return status;
    }

    return ath10k_wmi_cmd_send(ar, buf,
                               ar->wmi.cmd->pdev_set_wmm_params_cmdid);
}

static inline zx_status_t
ath10k_wmi_request_stats(struct ath10k* ar, uint32_t stats_mask) {
    struct ath10k_msg_buf* buf;

    if (!ar->wmi.ops->gen_request_stats) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    zx_status_t status = ar->wmi.ops->gen_request_stats(ar, &buf, stats_mask);
    if (status != ZX_OK) {
        return status;
    }

    return ath10k_wmi_cmd_send(ar, buf, ar->wmi.cmd->request_stats_cmdid);
}

static inline zx_status_t
ath10k_wmi_force_fw_hang(struct ath10k* ar,
                         enum wmi_force_fw_hang_type type, uint32_t delay_ms) {
    struct ath10k_msg_buf* buf;

    if (!ar->wmi.ops->gen_force_fw_hang) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    zx_status_t status = ar->wmi.ops->gen_force_fw_hang(ar, &buf, type, delay_ms);
    if (status != ZX_OK) {
        return status;
    }

    return ath10k_wmi_cmd_send(ar, buf, ar->wmi.cmd->force_fw_hang_cmdid);
}

static inline zx_status_t
ath10k_wmi_dbglog_cfg(struct ath10k* ar, uint64_t module_enable, uint32_t log_level) {
    struct ath10k_msg_buf* buf;

    if (!ar->wmi.ops->gen_dbglog_cfg) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    zx_status_t status = ar->wmi.ops->gen_dbglog_cfg(ar, &buf, module_enable, log_level);
    if (status != ZX_OK) {
        return status;
    }

    return ath10k_wmi_cmd_send(ar, buf, ar->wmi.cmd->dbglog_cfg_cmdid);
}

static inline zx_status_t
ath10k_wmi_pdev_pktlog_enable(struct ath10k* ar, uint32_t filter) {
    struct ath10k_msg_buf* buf;

    if (!ar->wmi.ops->gen_pktlog_enable) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    zx_status_t status = ar->wmi.ops->gen_pktlog_enable(ar, &buf, filter);
    if (status != ZX_OK) {
        return status;
    }

    return ath10k_wmi_cmd_send(ar, buf, ar->wmi.cmd->pdev_pktlog_enable_cmdid);
}

static inline zx_status_t
ath10k_wmi_pdev_pktlog_disable(struct ath10k* ar) {
    struct ath10k_msg_buf* buf;

    if (!ar->wmi.ops->gen_pktlog_disable) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    zx_status_t status = ar->wmi.ops->gen_pktlog_disable(ar, &buf);
    if (status != ZX_OK) {
        return status;
    }

    return ath10k_wmi_cmd_send(ar, buf, ar->wmi.cmd->pdev_pktlog_disable_cmdid);
}

static inline zx_status_t
ath10k_wmi_pdev_set_quiet_mode(struct ath10k* ar, uint32_t period, uint32_t duration,
                               uint32_t next_offset, uint32_t enabled) {
    struct ath10k_msg_buf* buf;

    if (!ar->wmi.ops->gen_pdev_set_quiet_mode) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    zx_status_t status = ar->wmi.ops->gen_pdev_set_quiet_mode(ar, &buf, period, duration,
                                                              next_offset, enabled);
    if (status != ZX_OK) {
        return status;
    }

    return ath10k_wmi_cmd_send(ar, buf, ar->wmi.cmd->pdev_set_quiet_mode_cmdid);
}

static inline zx_status_t
ath10k_wmi_pdev_get_temperature(struct ath10k* ar) {
    struct ath10k_msg_buf* buf;

    if (!ar->wmi.ops->gen_pdev_get_temperature) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    zx_status_t status = ar->wmi.ops->gen_pdev_get_temperature(ar, &buf);
    if (status != ZX_OK) {
        return status;
    }

    return ath10k_wmi_cmd_send(ar, buf, ar->wmi.cmd->pdev_get_temperature_cmdid);
}

static inline zx_status_t
ath10k_wmi_addba_clear_resp(struct ath10k* ar, uint32_t vdev_id, const uint8_t* mac) {
    struct ath10k_msg_buf* buf;

    if (!ar->wmi.ops->gen_addba_clear_resp) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    zx_status_t status = ar->wmi.ops->gen_addba_clear_resp(ar, &buf, vdev_id, mac);
    if (status != ZX_OK) {
        return status;
    }

    return ath10k_wmi_cmd_send(ar, buf, ar->wmi.cmd->addba_clear_resp_cmdid);
}

static inline zx_status_t
ath10k_wmi_addba_send(struct ath10k* ar, uint32_t vdev_id, const uint8_t* mac,
                      uint32_t tid, uint32_t buf_size) {
    struct ath10k_msg_buf* buf;

    if (!ar->wmi.ops->gen_addba_send) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    zx_status_t status = ar->wmi.ops->gen_addba_send(ar, &buf, vdev_id, mac, tid, buf_size);
    if (status != ZX_OK) {
        return status;
    }

    return ath10k_wmi_cmd_send(ar, buf, ar->wmi.cmd->addba_send_cmdid);
}

static inline zx_status_t
ath10k_wmi_addba_set_resp(struct ath10k* ar, uint32_t vdev_id, const uint8_t* mac,
                          uint32_t tid, uint32_t status) {
    struct ath10k_msg_buf* buf;

    if (!ar->wmi.ops->gen_addba_set_resp) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    zx_status_t result = ar->wmi.ops->gen_addba_set_resp(ar, &buf, vdev_id, mac, tid, status);
    if (result != ZX_OK) {
        return result;
    }

    return ath10k_wmi_cmd_send(ar, buf, ar->wmi.cmd->addba_set_resp_cmdid);
}

static inline zx_status_t
ath10k_wmi_delba_send(struct ath10k* ar, uint32_t vdev_id, const uint8_t* mac,
                      uint32_t tid, uint32_t initiator, uint32_t reason) {
    struct ath10k_msg_buf* buf;

    if (!ar->wmi.ops->gen_delba_send) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    zx_status_t status = ar->wmi.ops->gen_delba_send(ar, &buf, vdev_id, mac, tid, initiator,
                                                     reason);
    if (status != ZX_OK) {
        return status;
    }

    return ath10k_wmi_cmd_send(ar, buf, ar->wmi.cmd->delba_send_cmdid);
}

static inline zx_status_t
ath10k_wmi_bcn_tmpl(struct ath10k* ar, uint32_t vdev_id, uint32_t tim_ie_offset,
                    struct ath10k_msg_buf* bcn, uint32_t prb_caps, uint32_t prb_erp,
                    void* prb_ies, size_t prb_ies_len) {
    struct ath10k_msg_buf* buf;

    if (!ar->wmi.ops->gen_bcn_tmpl) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    zx_status_t status = ar->wmi.ops->gen_bcn_tmpl(ar, &buf, vdev_id, tim_ie_offset, bcn,
                                                   prb_caps, prb_erp, prb_ies, prb_ies_len);
    if (status != ZX_OK) {
        return status;
    }

    return ath10k_wmi_cmd_send(ar, buf, ar->wmi.cmd->bcn_tmpl_cmdid);
}

static inline zx_status_t
ath10k_wmi_prb_tmpl(struct ath10k* ar, uint32_t vdev_id, struct ath10k_msg_buf* prb) {
    struct ath10k_msg_buf* buf;

    if (!ar->wmi.ops->gen_prb_tmpl) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    zx_status_t status = ar->wmi.ops->gen_prb_tmpl(ar, &buf, vdev_id, prb);
    if (status != ZX_OK) {
        return status;
    }

    return ath10k_wmi_cmd_send(ar, buf, ar->wmi.cmd->prb_tmpl_cmdid);
}

static inline zx_status_t
ath10k_wmi_p2p_go_bcn_ie(struct ath10k* ar, uint32_t vdev_id, const uint8_t* p2p_ie) {
    struct ath10k_msg_buf* buf;

    if (!ar->wmi.ops->gen_p2p_go_bcn_ie) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    zx_status_t status = ar->wmi.ops->gen_p2p_go_bcn_ie(ar, &buf, vdev_id, p2p_ie);
    if (status != ZX_OK) {
        return status;
    }

    return ath10k_wmi_cmd_send(ar, buf, ar->wmi.cmd->p2p_go_set_beacon_ie);
}

static inline zx_status_t
ath10k_wmi_sta_keepalive(struct ath10k* ar,
                         const struct wmi_sta_keepalive_arg* arg) {
    struct ath10k_msg_buf* buf;
    uint32_t cmd_id;

    if (!ar->wmi.ops->gen_sta_keepalive) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    zx_status_t status = ar->wmi.ops->gen_sta_keepalive(ar, &buf, arg);
    if (status != ZX_OK) {
        return status;
    }

    cmd_id = ar->wmi.cmd->sta_keepalive_cmd;
    return ath10k_wmi_cmd_send(ar, buf, cmd_id);
}

static inline zx_status_t
ath10k_wmi_wow_enable(struct ath10k* ar) {
    struct ath10k_msg_buf* buf;
    uint32_t cmd_id;

    if (!ar->wmi.ops->gen_wow_enable) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    zx_status_t status = ar->wmi.ops->gen_wow_enable(ar, &buf);
    if (status != ZX_OK) {
        return status;
    }

    cmd_id = ar->wmi.cmd->wow_enable_cmdid;
    return ath10k_wmi_cmd_send(ar, buf, cmd_id);
}

static inline zx_status_t
ath10k_wmi_wow_add_wakeup_event(struct ath10k* ar, uint32_t vdev_id,
                                enum wmi_wow_wakeup_event event,
                                uint32_t enable) {
    struct ath10k_msg_buf* buf;
    uint32_t cmd_id;

    if (!ar->wmi.ops->gen_wow_add_wakeup_event) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    zx_status_t status = ar->wmi.ops->gen_wow_add_wakeup_event(ar, &buf, vdev_id, event, enable);
    if (status != ZX_OK) {
        return status;
    }

    cmd_id = ar->wmi.cmd->wow_enable_disable_wake_event_cmdid;
    return ath10k_wmi_cmd_send(ar, buf, cmd_id);
}

static inline zx_status_t
ath10k_wmi_wow_host_wakeup_ind(struct ath10k* ar) {
    struct ath10k_msg_buf* buf;
    uint32_t cmd_id;

    if (!ar->wmi.ops->gen_wow_host_wakeup_ind) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    zx_status_t status = ar->wmi.ops->gen_wow_host_wakeup_ind(ar, &buf);
    if (status != ZX_OK) {
        return status;
    }

    cmd_id = ar->wmi.cmd->wow_hostwakeup_from_sleep_cmdid;
    return ath10k_wmi_cmd_send(ar, buf, cmd_id);
}

static inline zx_status_t
ath10k_wmi_wow_add_pattern(struct ath10k* ar, uint32_t vdev_id, uint32_t pattern_id,
                           const uint8_t* pattern, const uint8_t* mask,
                           int pattern_len, int pattern_offset) {
    struct ath10k_msg_buf* buf;
    uint32_t cmd_id;

    if (!ar->wmi.ops->gen_wow_add_pattern) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    zx_status_t status = ar->wmi.ops->gen_wow_add_pattern(ar, &buf, vdev_id, pattern_id,
                                                          pattern, mask, pattern_len,
                                                          pattern_offset);
    if (status != ZX_OK) {
        return status;
    }

    cmd_id = ar->wmi.cmd->wow_add_wake_pattern_cmdid;
    return ath10k_wmi_cmd_send(ar, buf, cmd_id);
}

static inline zx_status_t
ath10k_wmi_wow_del_pattern(struct ath10k* ar, uint32_t vdev_id, uint32_t pattern_id) {
    struct ath10k_msg_buf* buf;
    uint32_t cmd_id;

    if (!ar->wmi.ops->gen_wow_del_pattern) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    zx_status_t status = ar->wmi.ops->gen_wow_del_pattern(ar, &buf, vdev_id, pattern_id);
    if (status != ZX_OK) {
        return status;
    }

    cmd_id = ar->wmi.cmd->wow_del_wake_pattern_cmdid;
    return ath10k_wmi_cmd_send(ar, buf, cmd_id);
}

static inline zx_status_t
ath10k_wmi_update_fw_tdls_state(struct ath10k* ar, uint32_t vdev_id,
                                enum wmi_tdls_state state) {
    struct ath10k_msg_buf* buf;

    if (!ar->wmi.ops->gen_update_fw_tdls_state) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    zx_status_t status = ar->wmi.ops->gen_update_fw_tdls_state(ar, &buf, vdev_id, state);
    if (status != ZX_OK) {
        return status;
    }

    return ath10k_wmi_cmd_send(ar, buf, ar->wmi.cmd->tdls_set_state_cmdid);
}

static inline zx_status_t
ath10k_wmi_tdls_peer_update(struct ath10k* ar,
                            const struct wmi_tdls_peer_update_cmd_arg* arg,
                            const struct wmi_tdls_peer_capab_arg* cap,
                            const struct wmi_channel_arg* chan) {
    struct ath10k_msg_buf* buf;

    if (!ar->wmi.ops->gen_tdls_peer_update) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    zx_status_t status = ar->wmi.ops->gen_tdls_peer_update(ar, &buf, arg, cap, chan);
    if (status != ZX_OK) {
        return status;
    }

    return ath10k_wmi_cmd_send(ar, buf, ar->wmi.cmd->tdls_peer_update_cmdid);
}

static inline zx_status_t
ath10k_wmi_adaptive_qcs(struct ath10k* ar, bool enable) {
    struct ath10k_msg_buf* buf;

    if (!ar->wmi.ops->gen_adaptive_qcs) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    zx_status_t status = ar->wmi.ops->gen_adaptive_qcs(ar, &buf, enable);
    if (status != ZX_OK) {
        return status;
    }

    return ath10k_wmi_cmd_send(ar, buf, ar->wmi.cmd->adaptive_qcs_cmdid);
}

static inline zx_status_t
ath10k_wmi_pdev_get_tpc_config(struct ath10k* ar, uint32_t param) {
    struct ath10k_msg_buf* buf;

    if (!ar->wmi.ops->gen_pdev_get_tpc_config) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    zx_status_t status = ar->wmi.ops->gen_pdev_get_tpc_config(ar, &buf, param);

    if (status != ZX_OK) {
        return status;
    }

    return ath10k_wmi_cmd_send(ar, buf, ar->wmi.cmd->pdev_get_tpc_config_cmdid);
}

static inline zx_status_t
ath10k_wmi_fw_stats_fill(struct ath10k* ar, struct ath10k_fw_stats* fw_stats,
                         char* buf) {
    if (!ar->wmi.ops->fw_stats_fill) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    ar->wmi.ops->fw_stats_fill(ar, fw_stats, buf);
    return ZX_OK;
}

static inline zx_status_t
ath10k_wmi_pdev_enable_adaptive_cca(struct ath10k* ar, uint8_t enable,
                                    uint32_t detect_level, uint32_t detect_margin) {
    struct ath10k_msg_buf* buf;

    if (!ar->wmi.ops->gen_pdev_enable_adaptive_cca) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    zx_status_t status = ar->wmi.ops->gen_pdev_enable_adaptive_cca(ar, &buf, enable,
                                                                   detect_level, detect_margin);

    if (status != ZX_OK) {
        return status;
    }

    return ath10k_wmi_cmd_send(ar, buf, ar->wmi.cmd->pdev_enable_adaptive_cca_cmdid);
}

static inline zx_status_t
ath10k_wmi_ext_resource_config(struct ath10k* ar,
                               enum wmi_host_platform_type type,
                               uint32_t fw_feature_bitmap) {
    struct ath10k_msg_buf* buf;

    if (!ar->wmi.ops->ext_resource_config) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    zx_status_t status = ar->wmi.ops->ext_resource_config(ar, &buf, type, fw_feature_bitmap);
    if (status != ZX_OK) {
        return status;
    }

    return ath10k_wmi_cmd_send(ar, buf, ar->wmi.cmd->ext_resource_cfg_cmdid);
}

static inline zx_status_t
ath10k_wmi_get_vdev_subtype(struct ath10k* ar, enum wmi_vdev_subtype subtype) {
    if (!ar->wmi.ops->get_vdev_subtype) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    return ar->wmi.ops->get_vdev_subtype(ar, subtype);
}

static inline zx_status_t
ath10k_wmi_pdev_bss_chan_info_request(struct ath10k* ar,
                                      enum wmi_bss_survey_req_type type) {
    struct ath10k_wmi* wmi = &ar->wmi;
    struct ath10k_msg_buf* buf;

    if (!wmi->ops->gen_pdev_bss_chan_info_req) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    zx_status_t status = wmi->ops->gen_pdev_bss_chan_info_req(ar, &buf, type);
    if (status != ZX_OK) {
        return status;
    }

    return ath10k_wmi_cmd_send(ar, buf, wmi->cmd->pdev_bss_chan_info_request_cmdid);
}

static inline zx_status_t
ath10k_wmi_echo(struct ath10k* ar, uint32_t value) {
    struct ath10k_wmi* wmi = &ar->wmi;
    struct ath10k_msg_buf* buf;
    zx_status_t status;

    if (!wmi->ops->gen_echo) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    status = wmi->ops->gen_echo(ar, &buf, value);
    if (status != ZX_OK) {
        return status;
    }

    return ath10k_wmi_cmd_send(ar, buf, wmi->cmd->echo_cmdid);
}

#endif
