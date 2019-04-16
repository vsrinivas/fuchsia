/*
 * Copyright (c) 2012 Broadcom Corporation
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

#include "p2p.h"

#include <threads.h>

#include "brcmu_utils.h"
#include "brcmu_wifi.h"
#include "cfg80211.h"
#include "core.h"
#include "debug.h"
#include "defs.h"
#include "fwil.h"
#include "fwil_types.h"
#include "linuxisms.h"
#include "workqueue.h"

/* parameters used for p2p escan */
#define P2PAPI_SCAN_NPROBES 1
#define P2PAPI_SCAN_DWELL_TIME_MS 80
#define P2PAPI_SCAN_SOCIAL_DWELL_TIME_MS 40
#define P2PAPI_SCAN_HOME_TIME_MS 60
#define P2PAPI_SCAN_NPROBS_TIME_MS 30
#define P2PAPI_SCAN_AF_SEARCH_DWELL_TIME_MS 100
#define WL_SCAN_CONNECT_DWELL_TIME_MS 200
#define WL_SCAN_JOIN_PROBE_INTERVAL_MS 20

#define BRCMF_P2P_WILDCARD_SSID "DIRECT-"
#define BRCMF_P2P_WILDCARD_SSID_LEN (sizeof(BRCMF_P2P_WILDCARD_SSID) - 1)

#define SOCIAL_CHAN_1 1
#define SOCIAL_CHAN_2 6
#define SOCIAL_CHAN_3 11
#define IS_P2P_SOCIAL_CHANNEL(channel) \
    ((channel == SOCIAL_CHAN_1) || (channel == SOCIAL_CHAN_2) || (channel == SOCIAL_CHAN_3))
#define BRCMF_P2P_TEMP_CHAN SOCIAL_CHAN_3
#define SOCIAL_CHAN_CNT 3
#define AF_PEER_SEARCH_CNT 2

#define BRCMF_SCB_TIMEOUT_VALUE 20

#define P2P_VER 9 /* P2P version: 9=WiFi P2P v1.0 */
#define P2P_PUB_AF_CATEGORY 0x04
#define P2P_PUB_AF_ACTION 0x09
#define P2P_AF_CATEGORY 0x7f
#define P2P_OUI "\x50\x6F\x9A" /* P2P OUI */
#define P2P_OUI_LEN 3          /* P2P OUI length */

/* Action Frame Constants */
#define DOT11_ACTION_HDR_LEN 2 /* action frame category + action */
#define DOT11_ACTION_CAT_OFF 0 /* category offset */
#define DOT11_ACTION_ACT_OFF 1 /* action offset */

#define P2P_AF_DWELL_TIME 200
#define P2P_AF_MIN_DWELL_TIME 100
#define P2P_AF_MED_DWELL_TIME 400
#define P2P_AF_LONG_DWELL_TIME 1000
#define P2P_AF_TX_MAX_RETRY 1
#define P2P_AF_MAX_WAIT_TIME_MSEC (2000)
#define P2P_INVALID_CHANNEL -1
#define P2P_CHANNEL_SYNC_RETRY 5
#define P2P_AF_FRM_SCAN_MAX_WAIT_MSEC (1500)
#define P2P_DEFAULT_SLEEP_TIME_VSDB 200

/* WiFi P2P Public Action Frame OUI Subtypes */
#define P2P_PAF_GON_REQ 0           /* Group Owner Negotiation Req */
#define P2P_PAF_GON_RSP 1           /* Group Owner Negotiation Rsp */
#define P2P_PAF_GON_CONF 2          /* Group Owner Negotiation Confirm */
#define P2P_PAF_INVITE_REQ 3        /* P2P Invitation Request */
#define P2P_PAF_INVITE_RSP 4        /* P2P Invitation Response */
#define P2P_PAF_DEVDIS_REQ 5        /* Device Discoverability Request */
#define P2P_PAF_DEVDIS_RSP 6        /* Device Discoverability Response */
#define P2P_PAF_PROVDIS_REQ 7       /* Provision Discovery Request */
#define P2P_PAF_PROVDIS_RSP 8       /* Provision Discovery Response */
#define P2P_PAF_SUBTYPE_INVALID 255 /* Invalid Subtype */

/* WiFi P2P Action Frame OUI Subtypes */
#define P2P_AF_NOTICE_OF_ABSENCE 0 /* Notice of Absence */
#define P2P_AF_PRESENCE_REQ 1      /* P2P Presence Request */
#define P2P_AF_PRESENCE_RSP 2      /* P2P Presence Response */
#define P2P_AF_GO_DISC_REQ 3       /* GO Discoverability Request */

/* P2P Service Discovery related */
#define P2PSD_ACTION_CATEGORY 0x04     /* Public action frame */
#define P2PSD_ACTION_ID_GAS_IREQ 0x0a  /* GAS Initial Request AF */
#define P2PSD_ACTION_ID_GAS_IRESP 0x0b /* GAS Initial Response AF */
#define P2PSD_ACTION_ID_GAS_CREQ 0x0c  /* GAS Comback Request AF */
#define P2PSD_ACTION_ID_GAS_CRESP 0x0d /* GAS Comback Response AF */

#define BRCMF_P2P_DISABLE_TIMEOUT_MSEC (500)
/**
 * struct brcmf_p2p_disc_st_le - set discovery state in firmware.
 *
 * @state: requested discovery state (see enum brcmf_p2p_disc_state).
 * @chspec: channel parameter for %WL_P2P_DISC_ST_LISTEN state.
 * @dwell: dwell time in ms for %WL_P2P_DISC_ST_LISTEN state.
 */
struct brcmf_p2p_disc_st_le {
    uint8_t state;
    uint16_t chspec;
    uint16_t dwell;
};

/**
 * enum brcmf_p2p_disc_state - P2P discovery state values
 *
 * @WL_P2P_DISC_ST_SCAN: P2P discovery with wildcard SSID and P2P IE.
 * @WL_P2P_DISC_ST_LISTEN: P2P discovery off-channel for specified time.
 * @WL_P2P_DISC_ST_SEARCH: P2P discovery with P2P wildcard SSID and P2P IE.
 */
enum brcmf_p2p_disc_state { WL_P2P_DISC_ST_SCAN, WL_P2P_DISC_ST_LISTEN, WL_P2P_DISC_ST_SEARCH };

/**
 * struct brcmf_p2p_scan_le - P2P specific scan request.
 *
 * @type: type of scan method requested (values: 'E' or 'S').
 * @reserved: reserved (ignored).
 * @eparams: parameters used for type 'E'.
 * @sparams: parameters used for type 'S'.
 */
struct brcmf_p2p_scan_le {
    uint8_t type;
    uint8_t reserved[3];
    union {
        struct brcmf_escan_params_le eparams;
        struct brcmf_scan_params_le sparams;
    };
};

/**
 * struct brcmf_p2p_pub_act_frame - WiFi P2P Public Action Frame
 *
 * @category: P2P_PUB_AF_CATEGORY
 * @action: P2P_PUB_AF_ACTION
 * @oui[3]: P2P_OUI
 * @oui_type: OUI type - P2P_VER
 * @subtype: OUI subtype - P2P_TYPE_*
 * @dialog_token: nonzero, identifies req/rsp transaction
 * @elts[1]: Variable length information elements.
 */
struct brcmf_p2p_pub_act_frame {
    uint8_t category;
    uint8_t action;
    uint8_t oui[3];
    uint8_t oui_type;
    uint8_t subtype;
    uint8_t dialog_token;
    uint8_t elts[1];
};

/**
 * struct brcmf_p2p_action_frame - WiFi P2P Action Frame
 *
 * @category: P2P_AF_CATEGORY
 * @OUI[3]: OUI - P2P_OUI
 * @type: OUI Type - P2P_VER
 * @subtype: OUI Subtype - P2P_AF_*
 * @dialog_token: nonzero, identifies req/resp transaction
 * @elts[1]: Variable length information elements.
 */
struct brcmf_p2p_action_frame {
    uint8_t category;
    uint8_t oui[3];
    uint8_t type;
    uint8_t subtype;
    uint8_t dialog_token;
    uint8_t elts[1];
};

/**
 * struct brcmf_p2psd_gas_pub_act_frame - Wi-Fi GAS Public Action Frame
 *
 * @category: 0x04 Public Action Frame
 * @action: 0x6c Advertisement Protocol
 * @dialog_token: nonzero, identifies req/rsp transaction
 * @query_data[1]: Query Data. SD gas ireq SD gas iresp
 */
struct brcmf_p2psd_gas_pub_act_frame {
    uint8_t category;
    uint8_t action;
    uint8_t dialog_token;
    uint8_t query_data[1];
};

/**
 * struct brcmf_config_af_params - Action Frame Parameters for tx.
 *
 * @mpc_onoff: To make sure to send successfully action frame, we have to
 *             turn off mpc  0: off, 1: on,  (-1): do nothing
 * @search_channel: 1: search peer's channel to send af
 * extra_listen: keep the dwell time to get af response frame.
 */
struct brcmf_config_af_params {
    int32_t mpc_onoff;
    bool search_channel;
    bool extra_listen;
};

/**
 * brcmf_p2p_set_discover_state - set discover state in firmware.
 *
 * @ifp: low-level interface object.
 * @state: discover state to set.
 * @chanspec: channel parameters (for state @WL_P2P_DISC_ST_LISTEN only).
 * @listen_ms: duration to listen (for state @WL_P2P_DISC_ST_LISTEN only).
 */
static zx_status_t brcmf_p2p_set_discover_state(struct brcmf_if* ifp, uint8_t state,
                                                uint16_t chanspec, uint16_t listen_ms) {
    struct brcmf_p2p_disc_st_le discover_state;
    zx_status_t ret = ZX_OK;
    brcmf_dbg(TRACE, "enter\n");

    discover_state.state = state;
    discover_state.chspec = chanspec;
    discover_state.dwell = listen_ms;
    ret = brcmf_fil_bsscfg_data_set(ifp, "p2p_state", &discover_state, sizeof(discover_state));
    return ret;
}

/**
 * brcmf_p2p_enable_discovery() - initialize and configure discovery.
 *
 * @p2p: P2P specific data.
 *
 * Initializes the discovery device and configure the virtual interface.
 */
static zx_status_t brcmf_p2p_enable_discovery(struct brcmf_p2p_info* p2p) {
    struct brcmf_cfg80211_vif* vif;
    zx_status_t ret = ZX_OK;

    brcmf_dbg(TRACE, "enter\n");
    vif = p2p->bss_idx[P2PAPI_BSSCFG_DEVICE].vif;
    if (!vif) {
        brcmf_err("P2P config device not available\n");
        ret = ZX_ERR_UNAVAILABLE;
        goto exit;
    }

    if (brcmf_test_bit_in_array(BRCMF_P2P_STATUS_ENABLED, &p2p->status)) {
        brcmf_dbg(INFO, "P2P config device already configured\n");
        goto exit;
    }

    /* Re-initialize P2P Discovery in the firmware */
    vif = p2p->bss_idx[P2PAPI_BSSCFG_PRIMARY].vif;
    ret = brcmf_fil_iovar_int_set(vif->ifp, "p2p_disc", 1);
    if (ret != ZX_OK) {
        brcmf_err("set p2p_disc error\n");
        goto exit;
    }
    vif = p2p->bss_idx[P2PAPI_BSSCFG_DEVICE].vif;
    ret = brcmf_p2p_set_discover_state(vif->ifp, WL_P2P_DISC_ST_SCAN, 0, 0);
    if (ret != ZX_OK) {
        brcmf_err("unable to set WL_P2P_DISC_ST_SCAN\n");
        goto exit;
    }

    /*
     * Set wsec to any non-zero value in the discovery bsscfg
     * to ensure our P2P probe responses have the privacy bit
     * set in the 802.11 WPA IE. Some peer devices may not
     * initiate WPS with us if this bit is not set.
     */
    ret = brcmf_fil_bsscfg_int_set(vif->ifp, "wsec", AES_ENABLED);
    if (ret != ZX_OK) {
        brcmf_err("wsec error %d\n", ret);
        goto exit;
    }

    brcmf_set_bit_in_array(BRCMF_P2P_STATUS_ENABLED, &p2p->status);
exit:
    return ret;
}

/**
 * brcmf_p2p_discover_listen() - set firmware to discover listen state.
 *
 * @p2p: p2p device.
 * @channel: channel nr for discover listen.
 * @duration: time in ms to stay on channel.
 *
 */
static zx_status_t brcmf_p2p_discover_listen(struct brcmf_p2p_info* p2p, uint16_t channel,
                                             uint32_t duration) {
    struct brcmf_cfg80211_vif* vif;
    struct brcmu_chan ch;
    zx_status_t err = ZX_OK;

    vif = p2p->bss_idx[P2PAPI_BSSCFG_DEVICE].vif;
    if (!vif) {
        brcmf_err("Discovery is not set, so we have nothing to do\n");
        err = ZX_ERR_UNAVAILABLE;
        goto exit;
    }

    if (brcmf_test_bit_in_array(BRCMF_P2P_STATUS_DISCOVER_LISTEN, &p2p->status)) {
        brcmf_err("Previous LISTEN is not completed yet\n");
        /* WAR: prevent cookie mismatch in wpa_supplicant return OK */
        goto exit;
    }

    ch.chnum = channel;
    ch.bw = BRCMU_CHAN_BW_20;
    p2p->cfg->d11inf.encchspec(&ch);
    err = brcmf_p2p_set_discover_state(vif->ifp, WL_P2P_DISC_ST_LISTEN, ch.chspec,
                                       (uint16_t)duration);
    if (err == ZX_OK) {
        brcmf_set_bit_in_array(BRCMF_P2P_STATUS_DISCOVER_LISTEN, &p2p->status);
        p2p->remain_on_channel_cookie++;
    }
exit:
    return err;
}

/**
 * brcmf_p2p_remain_on_channel() - put device on channel and stay there.
 *
 * @wiphy: wiphy device.
 * @channel: channel to stay on.
 * @duration: time in ms to remain on channel.
 *
 */
zx_status_t brcmf_p2p_remain_on_channel(struct wiphy* wiphy, struct wireless_dev* wdev,
                                        struct ieee80211_channel* channel, unsigned int duration,
                                        uint64_t* cookie) {
    struct brcmf_cfg80211_info* cfg = wiphy_to_cfg(wiphy);
    struct brcmf_p2p_info* p2p = &cfg->p2p;
    zx_status_t err;
    uint16_t channel_nr;

    channel_nr = ieee80211_frequency_to_channel(channel->center_freq);
    brcmf_dbg(TRACE, "Enter, channel: %d, duration ms (%d)\n", channel_nr, duration);

    err = brcmf_p2p_enable_discovery(p2p);
    if (err != ZX_OK) {
        goto exit;
    }
    err = brcmf_p2p_discover_listen(p2p, channel_nr, duration);
    if (err != ZX_OK) {
        goto exit;
    }

    memcpy(&p2p->remain_on_channel, channel, sizeof(*channel));
    *cookie = p2p->remain_on_channel_cookie;
    cfg80211_ready_on_channel(wdev, *cookie, channel, duration);

exit:
    return err;
}

/**
 * brcmf_p2p_notify_listen_complete() - p2p listen has completed.
 *
 * @ifp: interface control.
 * @e: event message. Not used, to make it usable for fweh event dispatcher.
 * @data: payload of message. Not used.
 *
 */
zx_status_t brcmf_p2p_notify_listen_complete(struct brcmf_if* ifp, const struct brcmf_event_msg* e,
                                             void* data) {
    struct brcmf_cfg80211_info* cfg = ifp->drvr->config;
    struct brcmf_p2p_info* p2p = &cfg->p2p;

    brcmf_dbg(TRACE, "Enter\n");
    if (brcmf_test_and_clear_bit_in_array(BRCMF_P2P_STATUS_DISCOVER_LISTEN, &p2p->status)) {
        if (brcmf_test_and_clear_bit_in_array(BRCMF_P2P_STATUS_WAITING_NEXT_AF_LISTEN,
                &p2p->status)) {
            brcmf_clear_bit_in_array(BRCMF_P2P_STATUS_WAITING_NEXT_ACT_FRAME, &p2p->status);
            brcmf_dbg(INFO, "Listen DONE, wake up wait_next_af\n");
            sync_completion_signal(&p2p->wait_next_af);
        }

        cfg80211_remain_on_channel_expired(&ifp->vif->wdev, p2p->remain_on_channel_cookie,
                                           &p2p->remain_on_channel);
    }
    return ZX_OK;
}

/**
 * brcmf_p2p_scan_finding_common_channel() - was escan used for finding channel
 *
 * @cfg: common configuration struct.
 * @bi: bss info struct, result from scan.
 *
 */
bool brcmf_p2p_scan_finding_common_channel(struct brcmf_cfg80211_info* cfg,
                                           struct brcmf_bss_info_le* bi)

{
    struct brcmf_p2p_info* p2p = &cfg->p2p;
    struct afx_hdl* afx_hdl = &p2p->afx_hdl;
    struct brcmu_chan ch;
    uint8_t* ie;
    zx_status_t err;
    uint8_t p2p_dev_addr[ETH_ALEN];

    if (!brcmf_test_bit_in_array(BRCMF_P2P_STATUS_FINDING_COMMON_CHANNEL, &p2p->status)) {
        return false;
    }

    if (bi == NULL) {
        brcmf_dbg(TRACE, "ACTION FRAME SCAN Done\n");
        if (afx_hdl->peer_chan == P2P_INVALID_CHANNEL) {
            sync_completion_signal(&afx_hdl->act_frm_scan);
        }
        return true;
    }

    ie = ((uint8_t*)bi) + bi->ie_offset;
    memset(p2p_dev_addr, 0, sizeof(p2p_dev_addr));
    err = cfg80211_get_p2p_attr(ie, bi->ie_length, IEEE80211_P2P_ATTR_DEVICE_INFO,
                                p2p_dev_addr, sizeof(p2p_dev_addr));
    if (err != ZX_OK)
        err = cfg80211_get_p2p_attr(ie, bi->ie_length, IEEE80211_P2P_ATTR_DEVICE_ID,
                                    p2p_dev_addr, sizeof(p2p_dev_addr));
    if ((err == ZX_OK) && (ether_addr_equal(p2p_dev_addr, afx_hdl->tx_dst_addr))) {
        if (!bi->ctl_ch) {
            ch.chspec = bi->chanspec;
            cfg->d11inf.decchspec(&ch);
            bi->ctl_ch = ch.control_ch_num;
        }
        afx_hdl->peer_chan = bi->ctl_ch;
        brcmf_dbg(TRACE, "ACTION FRAME SCAN : Peer %pM found, channel : %d\n", afx_hdl->tx_dst_addr,
                  afx_hdl->peer_chan);
        sync_completion_signal(&afx_hdl->act_frm_scan);
    }
    return true;
}

/**
 * brcmf_p2p_get_current_chanspec() - Get current operation channel.
 *
 * @p2p: P2P specific data.
 * @chanspec: chanspec to be returned.
 */
static void brcmf_p2p_get_current_chanspec(struct brcmf_p2p_info* p2p, uint16_t* chanspec) {
    struct brcmf_if* ifp;
    uint8_t mac_addr[ETH_ALEN];
    struct brcmu_chan ch;
    struct brcmf_bss_info_le* bi;
    uint8_t* buf;

    ifp = p2p->bss_idx[P2PAPI_BSSCFG_PRIMARY].vif->ifp;

    if (brcmf_fil_cmd_data_get(ifp, BRCMF_C_GET_BSSID, mac_addr, ETH_ALEN) == ZX_OK) {
        buf = calloc(1, WL_BSS_INFO_MAX);
        if (buf != NULL) {
            *(uint32_t*)buf = WL_BSS_INFO_MAX;
            if (brcmf_fil_cmd_data_get(ifp, BRCMF_C_GET_BSS_INFO, buf, WL_BSS_INFO_MAX) == ZX_OK) {
                bi = (struct brcmf_bss_info_le*)(buf + 4);
                *chanspec = bi->chanspec;
                free(buf);
                return;
            }
            free(buf);
        }
    }
    /* Use default channel for P2P */
    ch.chnum = BRCMF_P2P_TEMP_CHAN;
    ch.bw = BRCMU_CHAN_BW_20;
    p2p->cfg->d11inf.encchspec(&ch);
    *chanspec = ch.chspec;
}

/**
 * Change a P2P Role.
 * Parameters:
 * @mac: MAC address of the BSS to change a role
 * Returns 0 if success.
 */
zx_status_t brcmf_p2p_ifchange(struct brcmf_cfg80211_info* cfg,
                               enum brcmf_fil_p2p_if_types if_type) {
    struct brcmf_p2p_info* p2p = &cfg->p2p;
    struct brcmf_cfg80211_vif* vif;
    struct brcmf_fil_p2p_if_le if_request;
    zx_status_t err;
    uint16_t chanspec;

    brcmf_dbg(TRACE, "Enter\n");

    vif = p2p->bss_idx[P2PAPI_BSSCFG_PRIMARY].vif;
    if (!vif) {
        brcmf_err("vif for P2PAPI_BSSCFG_PRIMARY does not exist\n");
        return ZX_ERR_UNAVAILABLE;
    }
    brcmf_notify_escan_complete(cfg, vif->ifp, true, true);
    vif = p2p->bss_idx[P2PAPI_BSSCFG_CONNECTION].vif;
    if (!vif) {
        brcmf_err("vif for P2PAPI_BSSCFG_CONNECTION does not exist\n");
        return ZX_ERR_UNAVAILABLE;
    }
    brcmf_set_mpc(vif->ifp, 0);

    /* In concurrency case, STA may be already associated in a particular */
    /* channel. so retrieve the current channel of primary interface and  */
    /* then start the virtual interface on that.                          */
    brcmf_p2p_get_current_chanspec(p2p, &chanspec);

    if_request.type = (uint16_t)if_type;
    if_request.chspec = chanspec;
    memcpy(if_request.addr, p2p->int_addr, sizeof(if_request.addr));

    // Every code path but this one returns ZX_ERR_UNAVAILABLE if another event was left armed.
    // It may be a bug that the test was omitted here, or maybe this is a special case that
    // needs to be concurrent (but I doubt it, because the code doesn't support concurrency
    // at all). After we get the wlan-generic driver, and see whether we need to support
    // concurrency, either put in a paranoid-test like in all the other code paths, or support
    // it properly.
    if (brcmf_cfg80211_vif_event_armed(cfg)) {
        // TODO(cphoenix): Deal with this better, or prevent it
        brcmf_err(" * * Concurrent vif events should never happen.");
    }

    brcmf_cfg80211_arm_vif_event(cfg, vif, BRCMF_E_IF_CHANGE);
    err = brcmf_fil_iovar_data_set(vif->ifp, "p2p_ifupd", &if_request, sizeof(if_request));
    if (err != ZX_OK) {
        brcmf_err("p2p_ifupd FAILED, err=%d\n", err);
        brcmf_cfg80211_disarm_vif_event(cfg);
        return err;
    }
    err = brcmf_cfg80211_wait_vif_event(cfg, ZX_MSEC(BRCMF_VIF_EVENT_TIMEOUT_MSEC));
    brcmf_cfg80211_disarm_vif_event(cfg);
    if (err != ZX_OK) {
        brcmf_err("No BRCMF_E_IF_CHANGE event received\n");
        return ZX_ERR_IO;
    }

    err = brcmf_fil_cmd_int_set(vif->ifp, BRCMF_C_SET_SCB_TIMEOUT, BRCMF_SCB_TIMEOUT_VALUE);

    return err;
}

zx_status_t brcmf_p2p_start_device(struct wiphy* wiphy, struct wireless_dev* wdev) {
    struct brcmf_cfg80211_info* cfg = wiphy_to_cfg(wiphy);
    struct brcmf_p2p_info* p2p = &cfg->p2p;
    struct brcmf_cfg80211_vif* vif;
    zx_status_t err;

    vif = containerof(wdev, struct brcmf_cfg80211_vif, wdev);
    mtx_lock(&cfg->usr_sync);
    err = brcmf_p2p_enable_discovery(p2p);
    if (err == ZX_OK) {
        brcmf_set_bit_in_array(BRCMF_VIF_STATUS_READY, &vif->sme_state);
    }
    mtx_unlock(&cfg->usr_sync);
    return err;
}

void brcmf_p2p_stop_device(struct wiphy* wiphy, struct wireless_dev* wdev) {
    struct brcmf_cfg80211_info* cfg = wiphy_to_cfg(wiphy);
    struct brcmf_p2p_info* p2p = &cfg->p2p;
    struct brcmf_cfg80211_vif* vif;

    vif = containerof(wdev, struct brcmf_cfg80211_vif, wdev);
    /* This call can be result of the unregister_wdev call. In that case
     * we dont want to do anything anymore. Just return. The config vif
     * will have been cleared at this point.
     */
    if (p2p->bss_idx[P2PAPI_BSSCFG_DEVICE].vif == vif) {
        mtx_lock(&cfg->usr_sync);
        /* Set the discovery state to SCAN */
        (void)brcmf_p2p_set_discover_state(vif->ifp, WL_P2P_DISC_ST_SCAN, 0, 0);
        brcmf_abort_scanning(cfg);
        brcmf_clear_bit_in_array(BRCMF_VIF_STATUS_READY, &vif->sme_state);
        mtx_unlock(&cfg->usr_sync);
    }
}
