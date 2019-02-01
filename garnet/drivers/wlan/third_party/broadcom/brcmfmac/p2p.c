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
 * brcmf_p2p_is_pub_action() - true if p2p public type frame.
 *
 * @frame: action frame data.
 * @frame_len: length of action frame data.
 *
 * Determine if action frame is p2p public action type
 */
static bool brcmf_p2p_is_pub_action(void* frame, uint32_t frame_len) {
    struct brcmf_p2p_pub_act_frame* pact_frm;

    if (frame == NULL) {
        return false;
    }

    pact_frm = (struct brcmf_p2p_pub_act_frame*)frame;
    if (frame_len < sizeof(struct brcmf_p2p_pub_act_frame) - 1) {
        return false;
    }

    if (pact_frm->category == P2P_PUB_AF_CATEGORY && pact_frm->action == P2P_PUB_AF_ACTION &&
            pact_frm->oui_type == P2P_VER && memcmp(pact_frm->oui, P2P_OUI, P2P_OUI_LEN) == 0) {
        return true;
    }

    return false;
}

/**
 * brcmf_p2p_is_p2p_action() - true if p2p action type frame.
 *
 * @frame: action frame data.
 * @frame_len: length of action frame data.
 *
 * Determine if action frame is p2p action type
 */
static bool brcmf_p2p_is_p2p_action(void* frame, uint32_t frame_len) {
    struct brcmf_p2p_action_frame* act_frm;

    if (frame == NULL) {
        return false;
    }

    act_frm = (struct brcmf_p2p_action_frame*)frame;
    if (frame_len < sizeof(struct brcmf_p2p_action_frame) - 1) {
        return false;
    }

    if (act_frm->category == P2P_AF_CATEGORY && act_frm->type == P2P_VER &&
            memcmp(act_frm->oui, P2P_OUI, P2P_OUI_LEN) == 0) {
        return true;
    }

    return false;
}

/**
 * brcmf_p2p_is_gas_action() - true if p2p gas action type frame.
 *
 * @frame: action frame data.
 * @frame_len: length of action frame data.
 *
 * Determine if action frame is p2p gas action type
 */
static bool brcmf_p2p_is_gas_action(void* frame, uint32_t frame_len) {
    struct brcmf_p2psd_gas_pub_act_frame* sd_act_frm;

    if (frame == NULL) {
        return false;
    }

    sd_act_frm = (struct brcmf_p2psd_gas_pub_act_frame*)frame;
    if (frame_len < sizeof(struct brcmf_p2psd_gas_pub_act_frame) - 1) {
        return false;
    }

    if (sd_act_frm->category != P2PSD_ACTION_CATEGORY) {
        return false;
    }

    if (sd_act_frm->action == P2PSD_ACTION_ID_GAS_IREQ ||
            sd_act_frm->action == P2PSD_ACTION_ID_GAS_IRESP ||
            sd_act_frm->action == P2PSD_ACTION_ID_GAS_CREQ ||
            sd_act_frm->action == P2PSD_ACTION_ID_GAS_CRESP) {
        return true;
    }

    return false;
}

/**
 * brcmf_p2p_print_actframe() - debug print routine.
 *
 * @tx: Received or to be transmitted
 * @frame: action frame data.
 * @frame_len: length of action frame data.
 *
 * Print information about the p2p action frame
 */

#ifdef DEBUG

static void brcmf_p2p_print_actframe(bool tx, void* frame, uint32_t frame_len) {
    struct brcmf_p2p_pub_act_frame* pact_frm;
    struct brcmf_p2p_action_frame* act_frm;
    struct brcmf_p2psd_gas_pub_act_frame* sd_act_frm;

    if (!frame || frame_len <= 2) {
        return;
    }

    if (brcmf_p2p_is_pub_action(frame, frame_len)) {
        pact_frm = (struct brcmf_p2p_pub_act_frame*)frame;
        switch (pact_frm->subtype) {
        case P2P_PAF_GON_REQ:
            brcmf_dbg(TRACE, "%s P2P Group Owner Negotiation Req Frame\n", (tx) ? "TX" : "RX");
            break;
        case P2P_PAF_GON_RSP:
            brcmf_dbg(TRACE, "%s P2P Group Owner Negotiation Rsp Frame\n", (tx) ? "TX" : "RX");
            break;
        case P2P_PAF_GON_CONF:
            brcmf_dbg(TRACE, "%s P2P Group Owner Negotiation Confirm Frame\n", (tx) ? "TX" : "RX");
            break;
        case P2P_PAF_INVITE_REQ:
            brcmf_dbg(TRACE, "%s P2P Invitation Request  Frame\n", (tx) ? "TX" : "RX");
            break;
        case P2P_PAF_INVITE_RSP:
            brcmf_dbg(TRACE, "%s P2P Invitation Response Frame\n", (tx) ? "TX" : "RX");
            break;
        case P2P_PAF_DEVDIS_REQ:
            brcmf_dbg(TRACE, "%s P2P Device Discoverability Request Frame\n", (tx) ? "TX" : "RX");
            break;
        case P2P_PAF_DEVDIS_RSP:
            brcmf_dbg(TRACE, "%s P2P Device Discoverability Response Frame\n", (tx) ? "TX" : "RX");
            break;
        case P2P_PAF_PROVDIS_REQ:
            brcmf_dbg(TRACE, "%s P2P Provision Discovery Request Frame\n", (tx) ? "TX" : "RX");
            break;
        case P2P_PAF_PROVDIS_RSP:
            brcmf_dbg(TRACE, "%s P2P Provision Discovery Response Frame\n", (tx) ? "TX" : "RX");
            break;
        default:
            brcmf_dbg(TRACE, "%s Unknown P2P Public Action Frame\n", (tx) ? "TX" : "RX");
            break;
        }
    } else if (brcmf_p2p_is_p2p_action(frame, frame_len)) {
        act_frm = (struct brcmf_p2p_action_frame*)frame;
        switch (act_frm->subtype) {
        case P2P_AF_NOTICE_OF_ABSENCE:
            brcmf_dbg(TRACE, "%s P2P Notice of Absence Frame\n", (tx) ? "TX" : "RX");
            break;
        case P2P_AF_PRESENCE_REQ:
            brcmf_dbg(TRACE, "%s P2P Presence Request Frame\n", (tx) ? "TX" : "RX");
            break;
        case P2P_AF_PRESENCE_RSP:
            brcmf_dbg(TRACE, "%s P2P Presence Response Frame\n", (tx) ? "TX" : "RX");
            break;
        case P2P_AF_GO_DISC_REQ:
            brcmf_dbg(TRACE, "%s P2P Discoverability Request Frame\n", (tx) ? "TX" : "RX");
            break;
        default:
            brcmf_dbg(TRACE, "%s Unknown P2P Action Frame\n", (tx) ? "TX" : "RX");
        }

    } else if (brcmf_p2p_is_gas_action(frame, frame_len)) {
        sd_act_frm = (struct brcmf_p2psd_gas_pub_act_frame*)frame;
        switch (sd_act_frm->action) {
        case P2PSD_ACTION_ID_GAS_IREQ:
            brcmf_dbg(TRACE, "%s P2P GAS Initial Request\n", (tx) ? "TX" : "RX");
            break;
        case P2PSD_ACTION_ID_GAS_IRESP:
            brcmf_dbg(TRACE, "%s P2P GAS Initial Response\n", (tx) ? "TX" : "RX");
            break;
        case P2PSD_ACTION_ID_GAS_CREQ:
            brcmf_dbg(TRACE, "%s P2P GAS Comback Request\n", (tx) ? "TX" : "RX");
            break;
        case P2PSD_ACTION_ID_GAS_CRESP:
            brcmf_dbg(TRACE, "%s P2P GAS Comback Response\n", (tx) ? "TX" : "RX");
            break;
        default:
            brcmf_dbg(TRACE, "%s Unknown P2P GAS Frame\n", (tx) ? "TX" : "RX");
            break;
        }
    }
}

#else

static void brcmf_p2p_print_actframe(bool tx, void* frame, uint32_t frame_len) {}

#endif

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
 * brcmf_p2p_cancel_remain_on_channel() - cancel p2p listen state.
 *
 * @ifp: interface control.
 *
 */
void brcmf_p2p_cancel_remain_on_channel(struct brcmf_if* ifp) {
    if (!ifp) {
        return;
    }
    brcmf_p2p_set_discover_state(ifp, WL_P2P_DISC_ST_SCAN, 0, 0);
    brcmf_p2p_notify_listen_complete(ifp, NULL, NULL);
}

/**
 * brcmf_p2p_af_searching_channel() - search channel.
 *
 * @p2p: p2p device info struct.
 *
 */
static int32_t brcmf_p2p_af_searching_channel(struct brcmf_p2p_info* p2p) {
    struct afx_hdl* afx_hdl = &p2p->afx_hdl;
    struct brcmf_cfg80211_vif* pri_vif;
    unsigned long duration;
    int32_t retry;

    brcmf_dbg(TRACE, "Enter\n");

    pri_vif = p2p->bss_idx[P2PAPI_BSSCFG_PRIMARY].vif;

    sync_completion_reset(&afx_hdl->act_frm_scan);
    brcmf_set_bit_in_array(BRCMF_P2P_STATUS_FINDING_COMMON_CHANNEL, &p2p->status);
    afx_hdl->is_active = true;
    afx_hdl->peer_chan = P2P_INVALID_CHANNEL;

    /* Loop to wait until we find a peer's channel or the
     * pending action frame tx is cancelled.
     */
    retry = 0;
    duration = ZX_MSEC(P2P_AF_FRM_SCAN_MAX_WAIT_MSEC);
    while ((retry < P2P_CHANNEL_SYNC_RETRY) && (afx_hdl->peer_chan == P2P_INVALID_CHANNEL)) {
        afx_hdl->is_listen = false;
        brcmf_dbg(TRACE, "Scheduling action frame for sending.. (%d)\n", retry);
        /* search peer on peer's listen channel */
        workqueue_schedule_default(&afx_hdl->afx_work);
        sync_completion_wait(&afx_hdl->act_frm_scan, duration);
        if ((afx_hdl->peer_chan != P2P_INVALID_CHANNEL) ||
                (!brcmf_test_bit_in_array(BRCMF_P2P_STATUS_FINDING_COMMON_CHANNEL, &p2p->status))) {
            break;
        }

        if (afx_hdl->my_listen_chan) {
            brcmf_dbg(TRACE, "Scheduling listen peer, channel=%d\n", afx_hdl->my_listen_chan);
            /* listen on my listen channel */
            afx_hdl->is_listen = true;
            workqueue_schedule_default(&afx_hdl->afx_work);
            sync_completion_wait(&afx_hdl->act_frm_scan, duration);
        }
        if ((afx_hdl->peer_chan != P2P_INVALID_CHANNEL) ||
                (!brcmf_test_bit_in_array(BRCMF_P2P_STATUS_FINDING_COMMON_CHANNEL, &p2p->status))) {
            break;
        }
        retry++;

        /* if sta is connected or connecting, sleep for a while before
         * retry af tx or finding a peer
         */
        if (brcmf_test_bit_in_array(BRCMF_VIF_STATUS_CONNECTED, &pri_vif->sme_state) ||
                brcmf_test_bit_in_array(BRCMF_VIF_STATUS_CONNECTING, &pri_vif->sme_state)) {
            msleep(P2P_DEFAULT_SLEEP_TIME_VSDB);
        }
    }

    brcmf_dbg(TRACE, "Completed search/listen peer_chan=%d\n", afx_hdl->peer_chan);
    afx_hdl->is_active = false;

    brcmf_clear_bit_in_array(BRCMF_P2P_STATUS_FINDING_COMMON_CHANNEL, &p2p->status);

    return afx_hdl->peer_chan;
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
 * brcmf_p2p_stop_wait_next_action_frame() - finish scan if af tx complete.
 *
 * @cfg: common configuration struct.
 *
 */
static void brcmf_p2p_stop_wait_next_action_frame(struct brcmf_cfg80211_info* cfg) {
    struct brcmf_p2p_info* p2p = &cfg->p2p;
    struct brcmf_if* ifp = p2p->bss_idx[P2PAPI_BSSCFG_PRIMARY].vif->ifp;

    if (brcmf_test_bit_in_array(BRCMF_P2P_STATUS_SENDING_ACT_FRAME, &p2p->status) &&
            (brcmf_test_bit_in_array(BRCMF_P2P_STATUS_ACTION_TX_COMPLETED, &p2p->status) ||
             brcmf_test_bit_in_array(BRCMF_P2P_STATUS_ACTION_TX_NOACK, &p2p->status))) {
        brcmf_dbg(TRACE, "*** Wake UP ** abort actframe iovar\n");
        /* if channel is not zero, "actfame" uses off channel scan.
         * So abort scan for off channel completion.
         */
        if (p2p->af_sent_channel) {
            brcmf_notify_escan_complete(cfg, ifp, true, true);
        }
    } else if (brcmf_test_bit_in_array(BRCMF_P2P_STATUS_WAITING_NEXT_AF_LISTEN, &p2p->status)) {
        brcmf_dbg(TRACE, "*** Wake UP ** abort listen for next af frame\n");
        /* So abort scan to cancel listen */
        brcmf_notify_escan_complete(cfg, ifp, true, true);
    }
}

/**
 * brcmf_p2p_gon_req_collision() - Check if go negotiation collision
 *
 * @p2p: p2p device info struct.
 *
 * return true if received action frame is to be dropped.
 */
static bool brcmf_p2p_gon_req_collision(struct brcmf_p2p_info* p2p, uint8_t* mac) {
    struct brcmf_cfg80211_info* cfg = p2p->cfg;
    struct brcmf_if* ifp;

    brcmf_dbg(TRACE, "Enter\n");

    if (!brcmf_test_bit_in_array(BRCMF_P2P_STATUS_WAITING_NEXT_ACT_FRAME, &p2p->status) ||
            !p2p->gon_req_action) {
        return false;
    }

    brcmf_dbg(TRACE, "GO Negotiation Request COLLISION !!!\n");
    /* if sa(peer) addr is less than da(my) addr, then this device
     * process peer's gon request and block to send gon req.
     * if not (sa addr > da addr),
     * this device will process gon request and drop gon req of peer.
     */
    ifp = p2p->bss_idx[P2PAPI_BSSCFG_DEVICE].vif->ifp;
    if (memcmp(mac, ifp->mac_addr, ETH_ALEN) < 0) {
        brcmf_dbg(INFO, "Block transmit gon req !!!\n");
        p2p->block_gon_req_tx = true;
        /* if we are finding a common channel for sending af,
         * do not scan more to block to send current gon req
         */
        if (brcmf_test_and_clear_bit_in_array(BRCMF_P2P_STATUS_FINDING_COMMON_CHANNEL,
                &p2p->status)) {
            sync_completion_signal(&p2p->afx_hdl.act_frm_scan);
        }
        if (brcmf_test_and_clear_bit_in_array(BRCMF_P2P_STATUS_WAITING_NEXT_ACT_FRAME,
                &p2p->status)) {
            brcmf_p2p_stop_wait_next_action_frame(cfg);
        }
        return false;
    }

    /* drop gon request of peer to process gon request by this device. */
    brcmf_dbg(INFO, "Drop received gon req !!!\n");

    return true;
}

/**
 * brcmf_p2p_notify_action_frame_rx() - received action frame.
 *
 * @ifp: interface control.
 * @e: event message. Not used, to make it usable for fweh event dispatcher.
 * @data: payload of message, containing action frame data.
 *
 */
zx_status_t brcmf_p2p_notify_action_frame_rx(struct brcmf_if* ifp, const struct brcmf_event_msg* e,
                                             void* data) {
    struct brcmf_cfg80211_info* cfg = ifp->drvr->config;
    struct brcmf_p2p_info* p2p = &cfg->p2p;
    struct afx_hdl* afx_hdl = &p2p->afx_hdl;
    struct wireless_dev* wdev;
    uint32_t mgmt_frame_len = e->datalen - sizeof(struct brcmf_rx_mgmt_data);
    struct brcmf_rx_mgmt_data* rxframe = (struct brcmf_rx_mgmt_data*)data;
    uint8_t* frame = (uint8_t*)(rxframe + 1);
    struct brcmf_p2p_pub_act_frame* act_frm;
    struct brcmf_p2psd_gas_pub_act_frame* sd_act_frm;
    struct brcmu_chan ch;
    struct ieee80211_mgmt* mgmt_frame;
    int32_t freq;
    uint16_t mgmt_type;
    uint8_t action;

    if (e->datalen < sizeof(*rxframe)) {
        brcmf_dbg(SCAN, "Event data to small. Ignore\n");
        return ZX_OK;
    }

    ch.chspec = be16toh(rxframe->chanspec);
    cfg->d11inf.decchspec(&ch);
    /* Check if wpa_supplicant has registered for this frame */
    brcmf_dbg(INFO, "ifp->vif->mgmt_rx_reg %04x\n", ifp->vif->mgmt_rx_reg);
    mgmt_type = (IEEE80211_STYPE_ACTION & IEEE80211_FCTL_STYPE) >> 4;
    if ((ifp->vif->mgmt_rx_reg & BIT(mgmt_type)) == 0) {
        return ZX_OK;
    }

    brcmf_p2p_print_actframe(false, frame, mgmt_frame_len);

    action = P2P_PAF_SUBTYPE_INVALID;
    if (brcmf_p2p_is_pub_action(frame, mgmt_frame_len)) {
        act_frm = (struct brcmf_p2p_pub_act_frame*)frame;
        action = act_frm->subtype;
        if ((action == P2P_PAF_GON_REQ) && (brcmf_p2p_gon_req_collision(p2p, (uint8_t*)e->addr))) {
            if (brcmf_test_bit_in_array(BRCMF_P2P_STATUS_FINDING_COMMON_CHANNEL, &p2p->status) &&
                    (ether_addr_equal(afx_hdl->tx_dst_addr, e->addr))) {
                afx_hdl->peer_chan = ch.control_ch_num;
                brcmf_dbg(INFO, "GON request: Peer found, channel=%d\n", afx_hdl->peer_chan);
                sync_completion_signal(&afx_hdl->act_frm_scan);
            }
            return ZX_OK;
        }
        /* After complete GO Negotiation, roll back to mpc mode */
        if ((action == P2P_PAF_GON_CONF) || (action == P2P_PAF_PROVDIS_RSP)) {
            brcmf_set_mpc(ifp, 1);
        }
        if (action == P2P_PAF_GON_CONF) {
            brcmf_dbg(TRACE, "P2P: GO_NEG_PHASE status cleared\n");
            brcmf_clear_bit_in_array(BRCMF_P2P_STATUS_GO_NEG_PHASE, &p2p->status);
        }
    } else if (brcmf_p2p_is_gas_action(frame, mgmt_frame_len)) {
        sd_act_frm = (struct brcmf_p2psd_gas_pub_act_frame*)frame;
        action = sd_act_frm->action;
    }

    if (brcmf_test_bit_in_array(BRCMF_P2P_STATUS_WAITING_NEXT_ACT_FRAME, &p2p->status) &&
            (p2p->next_af_subtype == action)) {
        brcmf_dbg(TRACE, "We got a right next frame! (%d)\n", action);
        brcmf_clear_bit_in_array(BRCMF_P2P_STATUS_WAITING_NEXT_ACT_FRAME, &p2p->status);
        /* Stop waiting for next AF. */
        brcmf_p2p_stop_wait_next_action_frame(cfg);
    }

    mgmt_frame = calloc(1, offsetof(struct ieee80211_mgmt, u) + mgmt_frame_len);
    if (!mgmt_frame) {
        brcmf_err("No memory available for action frame\n");
        return ZX_ERR_NO_MEMORY;
    }
    memcpy(mgmt_frame->da, ifp->mac_addr, ETH_ALEN);
    brcmf_fil_cmd_data_get(ifp, BRCMF_C_GET_BSSID, mgmt_frame->bssid, ETH_ALEN);
    memcpy(mgmt_frame->sa, e->addr, ETH_ALEN);
    mgmt_frame->frame_control = IEEE80211_STYPE_ACTION;
    memcpy(&mgmt_frame->u, frame, mgmt_frame_len);
    mgmt_frame_len += offsetof(struct ieee80211_mgmt, u);

    freq = ieee80211_channel_to_frequency(
        ch.control_ch_num, ch.band == BRCMU_CHAN_BAND_2G ? NL80211_BAND_2GHZ : NL80211_BAND_5GHZ);

    wdev = &ifp->vif->wdev;
    cfg80211_rx_mgmt(wdev, freq, 0, (uint8_t*)mgmt_frame, mgmt_frame_len, 0);

    free(mgmt_frame);
    return ZX_OK;
}

/**
 * brcmf_p2p_notify_action_tx_complete() - transmit action frame complete
 *
 * @ifp: interface control.
 * @e: event message. Not used, to make it usable for fweh event dispatcher.
 * @data: not used.
 *
 */
zx_status_t brcmf_p2p_notify_action_tx_complete(struct brcmf_if* ifp,
                                                const struct brcmf_event_msg* e, void* data) {
    struct brcmf_cfg80211_info* cfg = ifp->drvr->config;
    struct brcmf_p2p_info* p2p = &cfg->p2p;

    brcmf_dbg(INFO, "Enter: event %s, status=%d\n",
              e->event_code == BRCMF_E_ACTION_FRAME_OFF_CHAN_COMPLETE
                  ? "ACTION_FRAME_OFF_CHAN_COMPLETE"
                  : "ACTION_FRAME_COMPLETE",
              e->status);

    if (!brcmf_test_bit_in_array(BRCMF_P2P_STATUS_SENDING_ACT_FRAME, &p2p->status)) {
        return ZX_OK;
    }

    if (e->event_code == BRCMF_E_ACTION_FRAME_COMPLETE) {
        if (e->status == BRCMF_E_STATUS_SUCCESS) {
            brcmf_set_bit_in_array(BRCMF_P2P_STATUS_ACTION_TX_COMPLETED, &p2p->status);
        } else {
            brcmf_set_bit_in_array(BRCMF_P2P_STATUS_ACTION_TX_NOACK, &p2p->status);
            /* If there is no ack, we don't need to wait for
             * WLC_E_ACTION_FRAME_OFFCHAN_COMPLETE event
             */
            brcmf_p2p_stop_wait_next_action_frame(cfg);
        }

    } else {
        sync_completion_signal(&p2p->send_af_done);
    }
    return ZX_OK;
}

/**
 * brcmf_p2p_tx_action_frame() - send action frame over fil.
 *
 * @p2p: p2p info struct for vif.
 * @af_params: action frame data/info.
 *
 * Send an action frame immediately without doing channel synchronization.
 *
 * This function waits for a completion event before returning.
 * The WLC_E_ACTION_FRAME_COMPLETE event will be received when the action
 * frame is transmitted.
 */
static zx_status_t brcmf_p2p_tx_action_frame(struct brcmf_p2p_info* p2p,
                                             struct brcmf_fil_af_params_le* af_params) {
    struct brcmf_cfg80211_vif* vif;
    zx_status_t err = ZX_OK;

    brcmf_dbg(TRACE, "Enter\n");

    sync_completion_reset(&p2p->send_af_done);
    brcmf_clear_bit_in_array(BRCMF_P2P_STATUS_ACTION_TX_COMPLETED, &p2p->status);
    brcmf_clear_bit_in_array(BRCMF_P2P_STATUS_ACTION_TX_NOACK, &p2p->status);

    vif = p2p->bss_idx[P2PAPI_BSSCFG_DEVICE].vif;
    err = brcmf_fil_bsscfg_data_set(vif->ifp, "actframe", af_params, sizeof(*af_params));
    if (err != ZX_OK) {
        brcmf_err(" sending action frame has failed\n");
        goto exit;
    }

    p2p->af_sent_channel = af_params->channel;
    p2p->af_tx_sent_time = zx_clock_get(ZX_CLOCK_MONOTONIC);

    sync_completion_wait(&p2p->send_af_done, ZX_MSEC(P2P_AF_MAX_WAIT_TIME_MSEC));

    if (brcmf_test_bit_in_array(BRCMF_P2P_STATUS_ACTION_TX_COMPLETED, &p2p->status)) {
        brcmf_dbg(TRACE, "TX action frame operation is success\n");
    } else {
        err = ZX_ERR_IO;
        brcmf_dbg(TRACE, "TX action frame operation has failed\n");
    }
    /* clear status bit for action tx */
    brcmf_clear_bit_in_array(BRCMF_P2P_STATUS_ACTION_TX_COMPLETED, &p2p->status);
    brcmf_clear_bit_in_array(BRCMF_P2P_STATUS_ACTION_TX_NOACK, &p2p->status);

exit:
    return err;
}

/**
 * brcmf_p2p_pub_af_tx() - public action frame tx routine.
 *
 * @cfg: driver private data for cfg80211 interface.
 * @af_params: action frame data/info.
 * @config_af_params: configuration data for action frame.
 *
 * routine which transmits ation frame public type.
 */
static zx_status_t brcmf_p2p_pub_af_tx(struct brcmf_cfg80211_info* cfg,
                                       struct brcmf_fil_af_params_le* af_params,
                                       struct brcmf_config_af_params* config_af_params) {
    struct brcmf_p2p_info* p2p = &cfg->p2p;
    struct brcmf_fil_action_frame_le* action_frame;
    struct brcmf_p2p_pub_act_frame* act_frm;
    zx_status_t err = ZX_OK;
    uint16_t ie_len;

    action_frame = &af_params->action_frame;
    act_frm = (struct brcmf_p2p_pub_act_frame*)(action_frame->data);

    config_af_params->extra_listen = true;

    switch (act_frm->subtype) {
    case P2P_PAF_GON_REQ:
        brcmf_dbg(TRACE, "P2P: GO_NEG_PHASE status set\n");
        brcmf_set_bit_in_array(BRCMF_P2P_STATUS_GO_NEG_PHASE, &p2p->status);
        config_af_params->mpc_onoff = 0;
        config_af_params->search_channel = true;
        p2p->next_af_subtype = act_frm->subtype + 1;
        p2p->gon_req_action = true;
        /* increase dwell time to wait for RESP frame */
        af_params->dwell_time = P2P_AF_MED_DWELL_TIME;
        break;
    case P2P_PAF_GON_RSP:
        p2p->next_af_subtype = act_frm->subtype + 1;
        /* increase dwell time to wait for CONF frame */
        af_params->dwell_time = P2P_AF_MED_DWELL_TIME;
        break;
    case P2P_PAF_GON_CONF:
        /* If we reached till GO Neg confirmation reset the filter */
        brcmf_dbg(TRACE, "P2P: GO_NEG_PHASE status cleared\n");
        brcmf_clear_bit_in_array(BRCMF_P2P_STATUS_GO_NEG_PHASE, &p2p->status);
        /* turn on mpc again if go nego is done */
        config_af_params->mpc_onoff = 1;
        /* minimize dwell time */
        af_params->dwell_time = P2P_AF_MIN_DWELL_TIME;
        config_af_params->extra_listen = false;
        break;
    case P2P_PAF_INVITE_REQ:
        config_af_params->search_channel = true;
        p2p->next_af_subtype = act_frm->subtype + 1;
        /* increase dwell time */
        af_params->dwell_time = P2P_AF_MED_DWELL_TIME;
        break;
    case P2P_PAF_INVITE_RSP:
        /* minimize dwell time */
        af_params->dwell_time = P2P_AF_MIN_DWELL_TIME;
        config_af_params->extra_listen = false;
        break;
    case P2P_PAF_DEVDIS_REQ:
        config_af_params->search_channel = true;
        p2p->next_af_subtype = act_frm->subtype + 1;
        /* maximize dwell time to wait for RESP frame */
        af_params->dwell_time = P2P_AF_LONG_DWELL_TIME;
        break;
    case P2P_PAF_DEVDIS_RSP:
        /* minimize dwell time */
        af_params->dwell_time = P2P_AF_MIN_DWELL_TIME;
        config_af_params->extra_listen = false;
        break;
    case P2P_PAF_PROVDIS_REQ:
        ie_len = action_frame->len - offsetof(struct brcmf_p2p_pub_act_frame, elts);
        if (cfg80211_get_p2p_attr(&act_frm->elts[0], ie_len, IEEE80211_P2P_ATTR_GROUP_ID, NULL, 0) <
                0) {
            config_af_params->search_channel = true;
        }
        config_af_params->mpc_onoff = 0;
        p2p->next_af_subtype = act_frm->subtype + 1;
        /* increase dwell time to wait for RESP frame */
        af_params->dwell_time = P2P_AF_MED_DWELL_TIME;
        break;
    case P2P_PAF_PROVDIS_RSP:
        /* wpa_supplicant send go nego req right after prov disc */
        p2p->next_af_subtype = P2P_PAF_GON_REQ;
        /* increase dwell time to MED level */
        af_params->dwell_time = P2P_AF_MED_DWELL_TIME;
        config_af_params->extra_listen = false;
        break;
    default:
        brcmf_err("Unknown p2p pub act frame subtype: %d\n", act_frm->subtype);
        err = ZX_ERR_INVALID_ARGS;
    }
    return err;
}

/**
 * brcmf_p2p_send_action_frame() - send action frame .
 *
 * @cfg: driver private data for cfg80211 interface.
 * @ndev: net device to transmit on.
 * @af_params: configuration data for action frame.
 */
bool brcmf_p2p_send_action_frame(struct brcmf_cfg80211_info* cfg, struct net_device* ndev,
                                 struct brcmf_fil_af_params_le* af_params) {
    struct brcmf_p2p_info* p2p = &cfg->p2p;
    struct brcmf_if* ifp = ndev_to_if(ndev);
    struct brcmf_fil_action_frame_le* action_frame;
    struct brcmf_config_af_params config_af_params;
    struct afx_hdl* afx_hdl = &p2p->afx_hdl;
    uint16_t action_frame_len;
    bool ack = false;
    uint8_t category;
    uint8_t action;
    int32_t tx_retry;
    int32_t extra_listen_time;
    uint delta_ms;

    action_frame = &af_params->action_frame;
    action_frame_len = action_frame->len;

    brcmf_p2p_print_actframe(true, action_frame->data, action_frame_len);

    /* Add the default dwell time. Dwell time to stay off-channel */
    /* to wait for a response action frame after transmitting an  */
    /* GO Negotiation action frame                                */
    af_params->dwell_time = P2P_AF_DWELL_TIME;

    category = action_frame->data[DOT11_ACTION_CAT_OFF];
    action = action_frame->data[DOT11_ACTION_ACT_OFF];

    /* initialize variables */
    p2p->next_af_subtype = P2P_PAF_SUBTYPE_INVALID;
    p2p->gon_req_action = false;

    /* config parameters */
    config_af_params.mpc_onoff = -1;
    config_af_params.search_channel = false;
    config_af_params.extra_listen = false;

    if (brcmf_p2p_is_pub_action(action_frame->data, action_frame_len)) {
        /* p2p public action frame process */
        if (brcmf_p2p_pub_af_tx(cfg, af_params, &config_af_params)) {
            /* Just send unknown subtype frame with */
            /* default parameters.                  */
            brcmf_err("P2P Public action frame, unknown subtype.\n");
        }
    } else if (brcmf_p2p_is_gas_action(action_frame->data, action_frame_len)) {
        /* service discovery process */
        if (action == P2PSD_ACTION_ID_GAS_IREQ || action == P2PSD_ACTION_ID_GAS_CREQ) {
            /* configure service discovery query frame */
            config_af_params.search_channel = true;

            /* save next af suptype to cancel */
            /* remaining dwell time           */
            p2p->next_af_subtype = action + 1;

            af_params->dwell_time = P2P_AF_MED_DWELL_TIME;
        } else if (action == P2PSD_ACTION_ID_GAS_IRESP || action == P2PSD_ACTION_ID_GAS_CRESP) {
            /* configure service discovery response frame */
            af_params->dwell_time = P2P_AF_MIN_DWELL_TIME;
        } else {
            brcmf_err("Unknown action type: %d\n", action);
            goto exit;
        }
    } else if (brcmf_p2p_is_p2p_action(action_frame->data, action_frame_len)) {
        /* do not configure anything. it will be */
        /* sent with a default configuration     */
    } else {
        brcmf_err("Unknown Frame: category 0x%x, action 0x%x\n", category, action);
        return false;
    }

    /* if connecting on primary iface, sleep for a while before sending
     * af tx for VSDB
     */
    if (brcmf_test_bit_in_array(BRCMF_VIF_STATUS_CONNECTING,
                                &p2p->bss_idx[P2PAPI_BSSCFG_PRIMARY].vif->sme_state)) {
        msleep(50);
    }

    /* if scan is ongoing, abort current scan. */
    if (brcmf_test_bit_in_array(BRCMF_SCAN_STATUS_BUSY, &cfg->scan_status)) {
        brcmf_abort_scanning(cfg);
    }

    memcpy(afx_hdl->tx_dst_addr, action_frame->da, ETH_ALEN);

    /* To make sure to send successfully action frame, turn off mpc */
    if (config_af_params.mpc_onoff == 0) {
        brcmf_set_mpc(ifp, 0);
    }

    /* set status and destination address before sending af */
    if (p2p->next_af_subtype != P2P_PAF_SUBTYPE_INVALID) {
        /* set status to cancel the remained dwell time in rx process */
        brcmf_set_bit_in_array(BRCMF_P2P_STATUS_WAITING_NEXT_ACT_FRAME, &p2p->status);
    }

    p2p->af_sent_channel = 0;
    brcmf_set_bit_in_array(BRCMF_P2P_STATUS_SENDING_ACT_FRAME, &p2p->status);
    /* validate channel and p2p ies */
    if (config_af_params.search_channel && IS_P2P_SOCIAL_CHANNEL(af_params->channel) &&
            p2p->bss_idx[P2PAPI_BSSCFG_DEVICE].vif->saved_ie.probe_req_ie_len) {
        afx_hdl = &p2p->afx_hdl;
        afx_hdl->peer_listen_chan = af_params->channel;

        if (brcmf_p2p_af_searching_channel(p2p) == P2P_INVALID_CHANNEL) {
            brcmf_err("Couldn't find peer's channel.\n");
            goto exit;
        }

        /* Abort scan even for VSDB scenarios. Scan gets aborted in
         * firmware but after the check of piggyback algorithm. To take
         * care of current piggback algo, lets abort the scan here
         * itself.
         */
        brcmf_notify_escan_complete(cfg, ifp, true, true);

        /* update channel */
        af_params->channel = afx_hdl->peer_chan;
    }

    tx_retry = 0;
    while (!p2p->block_gon_req_tx && (ack == false) && (tx_retry < P2P_AF_TX_MAX_RETRY)) {
        ack = brcmf_p2p_tx_action_frame(p2p, af_params) == ZX_OK;
        tx_retry++;
    }
    if (ack == false) {
        brcmf_err("Failed to send Action Frame(retry %d)\n", tx_retry);
        brcmf_clear_bit_in_array(BRCMF_P2P_STATUS_GO_NEG_PHASE, &p2p->status);
    }

exit:
    brcmf_clear_bit_in_array(BRCMF_P2P_STATUS_SENDING_ACT_FRAME, &p2p->status);

    /* WAR: sometimes dongle does not keep the dwell time of 'actframe'.
     * if we couldn't get the next action response frame and dongle does
     * not keep the dwell time, go to listen state again to get next action
     * response frame.
     */
    if (ack && config_af_params.extra_listen && !p2p->block_gon_req_tx &&
            brcmf_test_bit_in_array(BRCMF_P2P_STATUS_WAITING_NEXT_ACT_FRAME, &p2p->status) &&
            p2p->af_sent_channel == afx_hdl->my_listen_chan) {
        delta_ms = (zx_clock_get(ZX_CLOCK_MONOTONIC) - p2p->af_tx_sent_time) / 1000000;
        if (af_params->dwell_time > delta_ms) {
            extra_listen_time = af_params->dwell_time - delta_ms;
        } else {
            extra_listen_time = 0;
        }
        if (extra_listen_time > 50) {
            brcmf_set_bit_in_array(BRCMF_P2P_STATUS_WAITING_NEXT_AF_LISTEN, &p2p->status);
            brcmf_dbg(INFO, "Wait more time! actual af time:%d, calculated extra listen:%d\n",
                      af_params->dwell_time, extra_listen_time);
            extra_listen_time += 100;
            if (brcmf_p2p_discover_listen(p2p, p2p->af_sent_channel, extra_listen_time) == ZX_OK) {
                unsigned long duration;

                extra_listen_time += 100;
                duration = ZX_MSEC(extra_listen_time);
                sync_completion_wait(&p2p->wait_next_af, duration);
            }
            brcmf_clear_bit_in_array(BRCMF_P2P_STATUS_WAITING_NEXT_AF_LISTEN, &p2p->status);
        }
    }

    if (p2p->block_gon_req_tx) {
        /* if ack is true, supplicant will wait more time(100ms).
         * so we will return it as a success to get more time .
         */
        p2p->block_gon_req_tx = false;
        ack = true;
    }

    brcmf_clear_bit_in_array(BRCMF_P2P_STATUS_WAITING_NEXT_ACT_FRAME, &p2p->status);
    /* if all done, turn mpc on again */
    if (config_af_params.mpc_onoff == 1) {
        brcmf_set_mpc(ifp, 1);
    }

    return ack;
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
