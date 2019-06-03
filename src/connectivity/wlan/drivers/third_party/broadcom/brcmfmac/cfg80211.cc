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

/* Toplevel file. Relies on dhd_linux.c to send commands to the dongle. */

#include "cfg80211.h"

#include <ddk/hw/wlan/wlaninfo.h>
#include <wlan/protocol/ieee80211.h>
#include <wlan/protocol/if-impl.h>
#include <wlan/protocol/mac.h>
#include <zircon/status.h>

#include <threads.h>

#include "brcmu_utils.h"
#include "brcmu_wifi.h"
#include "btcoex.h"
#include "bus.h"
#include "common.h"
#include "core.h"
#include "debug.h"
#include "defs.h"
#include "device.h"
#include "feature.h"
#include "fwil.h"
#include "fwil_types.h"
#include "linuxisms.h"
#include "netbuf.h"
#include "p2p.h"
#include "pno.h"
#include "proto.h"
#include "workqueue.h"

#define BRCMF_SCAN_IE_LEN_MAX 2048

#define WPA_OUI "\x00\x50\xF2" /* WPA OUI */
#define WPA_OUI_TYPE 1
#define RSN_OUI "\x00\x0F\xAC" /* RSN OUI */
#define WME_OUI_TYPE 2
#define WPS_OUI_TYPE 4

#define VS_IE_FIXED_HDR_LEN 6
#define WPA_IE_VERSION_LEN 2
#define WPA_IE_MIN_OUI_LEN 4
#define WPA_IE_SUITE_COUNT_LEN 2

// IEEE Std. 802.11-2016, 9.4.2.1, Table 9-77
#define WLAN_IE_TYPE_SSID 0
#define WLAN_IE_TYPE_SUPP_RATES 1
#define WLAN_IE_TYPE_RSNE 48
#define WLAN_IE_TYPE_EXT_SUPP_RATES 50

/* IEEE Std. 802.11-2016, 9.4.2.25.2, Table 9-131 */
#define WPA_CIPHER_NONE      0  /* None */
#define WPA_CIPHER_WEP_40    1  /* WEP (40-bit) */
#define WPA_CIPHER_TKIP      2  /* TKIP: default for WPA */
/*      RESERVED             3  */
#define WPA_CIPHER_CCMP_128  4  /* AES (CCM) */
#define WPA_CIPHER_WEP_104   5  /* WEP (104-bit) */
#define WPA_CIPHER_CMAC_128  6  /* BIP-CMAC-128 */

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

#define BRCMF_SCAN_JOIN_ACTIVE_DWELL_TIME_MS 320
#define BRCMF_SCAN_JOIN_PASSIVE_DWELL_TIME_MS 400
#define BRCMF_SCAN_JOIN_PROBE_INTERVAL_MS 20

#define BRCMF_SCAN_CHANNEL_TIME 40
#define BRCMF_SCAN_UNASSOC_TIME 40
#define BRCMF_SCAN_PASSIVE_TIME 120

#define BRCMF_ND_INFO_TIMEOUT_MSEC 2000

#define BRCMF_ASSOC_PARAMS_FIXED_SIZE (sizeof(struct brcmf_assoc_params_le) - sizeof(uint16_t))

static bool check_vif_up(struct brcmf_cfg80211_vif* vif) {
    if (!brcmf_test_bit_in_array(BRCMF_VIF_STATUS_READY, &vif->sme_state)) {
        brcmf_dbg(INFO, "device is not ready : status (%lu)\n", vif->sme_state.load());
        return false;
    }
    return true;
}

static uint16_t __wl_rates[] = {
    BRCM_RATE_1M,
    BRCM_RATE_2M,
    BRCM_RATE_5M5,
    BRCM_RATE_11M,
    BRCM_RATE_6M,
    BRCM_RATE_9M,
    BRCM_RATE_12M,
    BRCM_RATE_18M,
    BRCM_RATE_24M,
    BRCM_RATE_36M,
    BRCM_RATE_48M,
    BRCM_RATE_54M,
};

#define wl_g_rates (__wl_rates + 0)
#define wl_g_rates_size countof(__wl_rates)
#define wl_a_rates (__wl_rates + 4)
#define wl_a_rates_size (wl_g_rates_size - 4)

/* Vendor specific ie. id = 221, oui and type defines exact ie */
struct brcmf_vs_tlv {
    uint8_t id;
    uint8_t len;
    uint8_t oui[3];
    uint8_t oui_type;
};

struct parsed_vndr_ie_info {
    uint8_t* ie_ptr;
    uint32_t ie_len; /* total length including id & length field */
    struct brcmf_vs_tlv vndrie;
};

struct parsed_vndr_ies {
    uint32_t count;
    struct parsed_vndr_ie_info ie_info[VNDR_IE_PARSE_LIMIT];
};

uint16_t channel_to_chanspec(struct brcmu_d11inf* d11inf, wlan_channel_t* ch) {
    struct brcmu_chan ch_inf;

    ch_inf.chnum = ch->primary;

    switch (ch->cbw) {
    case CBW20:
        ch_inf.bw = BRCMU_CHAN_BW_20;
        ch_inf.sb = BRCMU_CHAN_SB_NONE;
        break;
    case CBW40:
        ch_inf.bw = BRCMU_CHAN_BW_40;
        ch_inf.sb = BRCMU_CHAN_SB_U;
        break;
    case CBW40BELOW:
        ch_inf.bw = BRCMU_CHAN_BW_40;
        ch_inf.sb = BRCMU_CHAN_SB_L;
        break;
    case CBW80:
    case CBW160:
    case CBW80P80:
    default:
        brcmf_err("unsupported channel width\n");
        break;
    }

    // ch_info.band is handled by encchspec

    d11inf->encchspec(&ch_inf);

    return ch_inf.chspec;
}

/* Traverse a string of 1-byte tag/1-byte length/variable-length value
 * triples, returning a pointer to the substring whose first element
 * matches tag
 */
static const struct brcmf_tlv* brcmf_parse_tlvs(const void* buf, int buflen, uint key) {
    const struct brcmf_tlv* elt = static_cast<decltype(elt)>(buf);
    int totlen = buflen;

    /* find tagged parameter */
    while (totlen >= TLV_HDR_LEN) {
        int len = elt->len;

        /* validate remaining totlen */
        if ((elt->id == key) && (totlen >= (len + TLV_HDR_LEN))) {
            return elt;
        }

        elt = (struct brcmf_tlv*)((uint8_t*)elt + (len + TLV_HDR_LEN));
        totlen -= (len + TLV_HDR_LEN);
    }

    return NULL;
}

static zx_status_t brcmf_vif_change_validate(struct brcmf_cfg80211_info* cfg,
                                             struct brcmf_cfg80211_vif* vif,
                                             uint16_t new_type) {
    struct brcmf_cfg80211_vif* pos;
    bool check_combos = false;
    zx_status_t ret = ZX_OK;
    struct iface_combination_params params = {
        .num_different_channels = 1,
    };

    list_for_every_entry(&cfg->vif_list, pos, struct brcmf_cfg80211_vif, list) {
        if (pos == vif) {
            params.iftype_num[new_type]++;
        } else {
            /* concurrent interfaces so need check combinations */
            check_combos = true;
            params.iftype_num[pos->wdev.iftype]++;
        }
    }

    if (check_combos) {
        ret = cfg80211_check_combinations(cfg->wiphy, &params);
    }

    return ret;
}

static zx_status_t brcmf_vif_add_validate(struct brcmf_cfg80211_info* cfg,
                                          uint16_t new_type) {
    struct brcmf_cfg80211_vif* pos;
    struct iface_combination_params params = {
        .num_different_channels = 1,
    };

    list_for_every_entry(&cfg->vif_list, pos, struct brcmf_cfg80211_vif, list) {
        params.iftype_num[pos->wdev.iftype]++;
    }

    params.iftype_num[new_type]++;
    return cfg80211_check_combinations(cfg->wiphy, &params);
}

static void convert_key_from_CPU(struct brcmf_wsec_key* key, struct brcmf_wsec_key_le* key_le) {
    key_le->index = key->index;
    key_le->len = key->len;
    key_le->algo = key->algo;
    key_le->flags = key->flags;
    key_le->rxiv.hi = key->rxiv.hi;
    key_le->rxiv.lo = key->rxiv.lo;
    key_le->iv_initialized = key->iv_initialized;
    memcpy(key_le->data, key->data, sizeof(key->data));
    memcpy(key_le->ea, key->ea, sizeof(key->ea));
}

static zx_status_t send_key_to_dongle(struct brcmf_if* ifp, struct brcmf_wsec_key* key) {
    zx_status_t err;
    struct brcmf_wsec_key_le key_le;

    convert_key_from_CPU(key, &key_le);

    brcmf_netdev_wait_pend8021x(ifp);

    err = brcmf_fil_bsscfg_data_set(ifp, "wsec_key", &key_le, sizeof(key_le));

    if (err != ZX_OK) {
        brcmf_err("wsec_key error (%d)\n", err);
    }
    return err;
}

static void brcmf_cfg80211_update_proto_addr_mode(struct wireless_dev* wdev) {
    struct brcmf_cfg80211_vif* vif;
    struct brcmf_if* ifp;

    vif = containerof(wdev, struct brcmf_cfg80211_vif, wdev);
    ifp = vif->ifp;

    if (wdev->iftype == WLAN_INFO_MAC_ROLE_AP) {
        brcmf_proto_configure_addr_mode(ifp->drvr, ifp->ifidx, ADDR_DIRECT);
    } else {
        brcmf_proto_configure_addr_mode(ifp->drvr, ifp->ifidx, ADDR_INDIRECT);
    }
}

static zx_status_t brcmf_get_first_free_bsscfgidx(struct brcmf_pub* drvr) {
    int bsscfgidx;

    for (bsscfgidx = 0; bsscfgidx < BRCMF_MAX_IFS; bsscfgidx++) {
        /* bsscfgidx 1 is reserved for legacy P2P */
        if (bsscfgidx == 1) {
            continue;
        }
        if (!drvr->iflist[bsscfgidx]) {
            return bsscfgidx;
        }
    }

    return ZX_ERR_NO_MEMORY;
}

static zx_status_t brcmf_cfg80211_request_ap_if(struct brcmf_if* ifp) {
    struct brcmf_mbss_ssid_le mbss_ssid_le;
    int bsscfgidx;
    zx_status_t err;

    memset(&mbss_ssid_le, 0, sizeof(mbss_ssid_le));
    bsscfgidx = brcmf_get_first_free_bsscfgidx(ifp->drvr);
    if (bsscfgidx < 0) {
        return bsscfgidx;
    }

    mbss_ssid_le.bsscfgidx = bsscfgidx;
    mbss_ssid_le.SSID_len = 5;
    sprintf((char*)mbss_ssid_le.SSID, "ssid%d", bsscfgidx);

    err = brcmf_fil_bsscfg_data_set(ifp, "bsscfg:ssid", &mbss_ssid_le, sizeof(mbss_ssid_le));
    if (err != ZX_OK) {
        brcmf_err("setting ssid failed %d\n", err);
    }

    return err;
}

/**
 * brcmf_ap_add_vif() - create a new AP virtual interface for multiple BSS
 *
 * @wiphy: wiphy device of new interface.
 * @name: name of the new interface.
 * @params: contains mac address for AP device.
 */
static zx_status_t brcmf_ap_add_vif(struct wiphy* wiphy, const char* name,
                                    struct vif_params* params, struct wireless_dev** dev_out) {
    struct brcmf_cfg80211_info* cfg = wiphy_to_cfg(wiphy);
    struct brcmf_if* ifp = cfg_to_if(cfg);
    struct brcmf_cfg80211_vif* vif;
    zx_status_t err;

    if (brcmf_cfg80211_vif_event_armed(cfg)) {
        return ZX_ERR_UNAVAILABLE;
    }

    brcmf_dbg(INFO, "Adding vif \"%s\"\n", name);

    err = brcmf_alloc_vif(cfg, WLAN_INFO_MAC_ROLE_AP, &vif);
    if (err != ZX_OK) {
        if (dev_out) {
            *dev_out = NULL;
        }
        return err;
    }

    brcmf_cfg80211_arm_vif_event(cfg, vif, BRCMF_E_IF_ADD);

    err = brcmf_cfg80211_request_ap_if(ifp);
    if (err != ZX_OK) {
        brcmf_cfg80211_disarm_vif_event(cfg);
        goto fail;
    }
    /* wait for firmware event */
    err = brcmf_cfg80211_wait_vif_event(cfg, ZX_MSEC(BRCMF_VIF_EVENT_TIMEOUT_MSEC));
    brcmf_cfg80211_disarm_vif_event(cfg);
    if (err != ZX_OK) {
        brcmf_err("timeout occurred\n");
        err = ZX_ERR_IO;
        goto fail;
    }

    /* interface created in firmware */
    ifp = vif->ifp;
    if (!ifp) {
        brcmf_err("no if pointer provided\n");
        err = ZX_ERR_INVALID_ARGS;
        goto fail;
    }

    strncpy(ifp->ndev->name, name, sizeof(ifp->ndev->name) - 1);
    err = brcmf_net_attach(ifp, true);
    if (err != ZX_OK) {
        brcmf_err("Registering netdevice failed\n");
        brcmf_free_net_device(ifp->ndev);
        goto fail;
    }

    if (dev_out) {
        *dev_out = &ifp->vif->wdev;
    }
    return ZX_OK;

fail:
    brcmf_free_vif(vif);
    if (dev_out) {
        *dev_out = NULL;
    }
    return err;
}

static bool brcmf_is_apmode(struct brcmf_cfg80211_vif* vif) {
    uint16_t iftype;

    iftype = vif->wdev.iftype;
    return iftype == WLAN_INFO_MAC_ROLE_AP;
}

zx_status_t brcmf_cfg80211_add_iface(struct wiphy* wiphy, const char* name,
                                     unsigned char name_assign_type, uint16_t type,
                                     struct vif_params* params, struct wireless_dev** wdev_out) {
    zx_status_t err;

    brcmf_dbg(TRACE, "enter: %s type %d\n", name, type);
    err = brcmf_vif_add_validate(wiphy_to_cfg(wiphy), type);
    if (err != ZX_OK) {
        brcmf_err("iface validation failed: err=%d\n", err);
        if (wdev_out) {
            *wdev_out = NULL;
        }
        return err;
    }
    switch (type) {
    case WLAN_INFO_MAC_ROLE_AP:
        err = brcmf_ap_add_vif(wiphy, name, params, wdev_out);
        break;
    default:
        if (wdev_out) {
            *wdev_out = NULL;
        }
        return ZX_ERR_INVALID_ARGS;
    }

    if (err != ZX_OK) {
        brcmf_err("add iface %s type %d failed: err=%d\n", name, type, err);
        if (wdev_out) {
            *wdev_out = NULL;
        }
        return err;
    } else {
        brcmf_cfg80211_update_proto_addr_mode(*wdev_out);
        return ZX_OK;
    }
}

static void brcmf_scan_config_mpc(struct brcmf_if* ifp, int mpc) {
    if (brcmf_feat_is_quirk_enabled(ifp, BRCMF_FEAT_QUIRK_NEED_MPC)) {
        brcmf_set_mpc(ifp, mpc);
    }
}

void brcmf_set_mpc(struct brcmf_if* ifp, int mpc) {
    zx_status_t err = 0;

    if (check_vif_up(ifp->vif)) {
        err = brcmf_fil_iovar_int_set(ifp, "mpc", mpc, nullptr);
        if (err != ZX_OK) {
            brcmf_err("fail to set mpc\n");
            return;
        }
        brcmf_dbg(INFO, "MPC : %d\n", mpc);
    }
}

static void brcmf_signal_scan_end(struct net_device* ndev, uint64_t txn_id,
                                  uint8_t scan_result_code) {
        wlanif_scan_end_t args;
        args.txn_id = txn_id;
        args.code = scan_result_code;
        if (ndev->if_callbacks != NULL) {
            brcmf_dbg(SCAN, "Signaling on_scan_end with txn_id %ld and code %d", args.txn_id,
                      args.code);
            ndev->if_callbacks->on_scan_end(ndev->if_callback_cookie, &args);
        }
        ndev->scan_busy = false;
}

zx_status_t brcmf_notify_escan_complete(struct brcmf_cfg80211_info* cfg, struct brcmf_if* ifp,
                                        bool aborted, bool fw_abort) {
    struct brcmf_scan_params_le params_le;
    wlanif_scan_req_t* scan_request;
    uint64_t reqid;
    uint32_t bucket;
    zx_status_t err = ZX_OK;

    brcmf_dbg(SCAN, "Enter\n");

    /* clear scan request, because the FW abort can cause a second call */
    /* to this function and might cause a double signal_scan_end        */
    scan_request = cfg->scan_request;
    cfg->scan_request = NULL;

    // Canceling if it's inactive is OK. Checking if it's active just invites race conditions.
    brcmf_timer_stop(&cfg->escan_timeout);

    if (fw_abort) {
        /* Do a scan abort to stop the driver's scan engine */
        brcmf_dbg(SCAN, "ABORT scan in firmware\n");
        memset(&params_le, 0, sizeof(params_le));
        fill_with_broadcast_addr(params_le.bssid);
        params_le.bss_type = DOT11_BSSTYPE_ANY;
        params_le.scan_type = 0;
        params_le.channel_num = 1;
        params_le.nprobes = 1;
        params_le.active_time = -1;
        params_le.passive_time = -1;
        params_le.home_time = -1;
        /* Scan is aborted by setting channel_list[0] to -1 */
        params_le.channel_list[0] = -1;
        /* E-Scan (or anyother type) can be aborted by SCAN */
        err = brcmf_fil_cmd_data_set(ifp, BRCMF_C_SCAN, &params_le, sizeof(params_le), nullptr);
        if (err != ZX_OK) {
            brcmf_err("Scan abort failed\n");
        }
    }

    brcmf_scan_config_mpc(ifp, 1);

    /*
     * e-scan can be initiated internally
     * which takes precedence.
     */
    struct net_device* ndev = cfg_to_ndev(cfg);
    if (cfg->int_escan_map) {
        brcmf_dbg(SCAN, "scheduled scan completed (%x)\n", cfg->int_escan_map);
        while (cfg->int_escan_map) {
            bucket = ffs(cfg->int_escan_map) - 1; // ffs() index is 1-based
            cfg->int_escan_map &= ~BIT(bucket);
            reqid = brcmf_pno_find_reqid_by_bucket(cfg->pno, bucket);
            if (!aborted) {
                // TODO(cphoenix): Figure out how to use internal reqid infrastructure, rather
                // than storing it separately in wiphy->scan_txn_id.
                brcmf_dbg(SCAN, " * * report scan results: internal reqid=%lu\n", reqid);
                brcmf_signal_scan_end(ndev, ndev->scan_txn_id, WLAN_SCAN_RESULT_SUCCESS);
            }
        }
    } else if (scan_request) {
        brcmf_dbg(SCAN, "ESCAN Completed scan: %s\n", aborted ? "Aborted" : "Done");
        brcmf_signal_scan_end(ndev, ndev->scan_txn_id,
                              aborted ? WLAN_SCAN_RESULT_INTERNAL_ERROR : WLAN_SCAN_RESULT_SUCCESS);
    }
    if (!brcmf_test_and_clear_bit_in_array(BRCMF_SCAN_STATUS_BUSY, &cfg->scan_status)) {
        brcmf_dbg(SCAN, "Scan complete, probably P2P scan\n");
    }

    return err;
}

static zx_status_t brcmf_cfg80211_change_iface(struct wiphy* wiphy, struct net_device* ndev,
                                               uint16_t type, struct vif_params* params) {
    struct brcmf_if* ifp = ndev_to_if(ndev);
    struct brcmf_cfg80211_vif* vif = ifp->vif;
    int32_t infra = 0;
    int32_t ap = 0;
    zx_status_t err = ZX_OK;

    brcmf_dbg(TRACE, "Enter");

    err = brcmf_vif_change_validate(wiphy_to_cfg(wiphy), vif, type);
    if (err != ZX_OK) {
        brcmf_err("iface validation failed: err=%d\n", err);
        return err;
    }
    switch (type) {
    case WLAN_INFO_MAC_ROLE_CLIENT:
        infra = 1;
        break;
    case WLAN_INFO_MAC_ROLE_AP:
        ap = 1;
        break;
    default:
        err = ZX_ERR_OUT_OF_RANGE;
        goto done;
    }

    if (ap) {
        brcmf_dbg(INFO, "IF Type = AP\n");
    } else {
        err = brcmf_fil_cmd_int_set(ifp, BRCMF_C_SET_INFRA, infra, nullptr);
        if (err != ZX_OK) {
            brcmf_err("WLC_SET_INFRA error (%d)\n", err);
            err = ZX_ERR_UNAVAILABLE;
            goto done;
        }
        brcmf_dbg(INFO, "IF Type = Infra");
    }
    ndev->ieee80211_ptr->iftype = type;

    brcmf_cfg80211_update_proto_addr_mode(&vif->wdev);

done:
    brcmf_dbg(TRACE, "Exit\n");

    return err;
}

static void brcmf_escan_prep(struct brcmf_cfg80211_info* cfg,
                             struct brcmf_scan_params_le* params_le,
                             wlanif_scan_req_t* request) {
    uint32_t n_ssids;
    uint32_t n_channels;
    int32_t i;
    int32_t offset;
    uint16_t chanspec;
    char* ptr;
    struct brcmf_ssid_le ssid_le;

    fill_with_broadcast_addr(params_le->bssid);
    params_le->bss_type = DOT11_BSSTYPE_ANY;
    if (request->scan_type == WLAN_SCAN_TYPE_ACTIVE) {
        params_le->scan_type = BRCMF_SCANTYPE_ACTIVE;
    } else {
        params_le->scan_type = BRCMF_SCANTYPE_PASSIVE;
    }
    params_le->channel_num = 0;
    params_le->nprobes = -1;
    params_le->active_time = -1;
    params_le->passive_time = -1;
    params_le->home_time = -1;
    params_le->ssid_le.SSID_len = request->ssid.len;
    if (request->ssid.len <= sizeof(params_le->ssid_le.SSID)) {
        memcpy(params_le->ssid_le.SSID, request->ssid.data, request->ssid.len);
    } else {
        memcpy(params_le->ssid_le.SSID, request->ssid.data, sizeof(params_le->ssid_le.SSID));
        brcmf_err("Scan request SSID size too large\n");
    }

    n_ssids = request->num_ssids;
    n_channels = request->num_channels;

    /* Copy channel array if applicable */
    brcmf_dbg(SCAN, "### List of channelspecs to scan ### %d\n", n_channels);
    if (n_channels > 0) {
        for (i = 0; i < (int32_t)n_channels; i++) {
            wlan_channel_t wlan_chan;
            wlan_chan.primary = request->channel_list[i];
            wlan_chan.cbw = CBW20;
            wlan_chan.secondary80 = 0;
            chanspec = channel_to_chanspec(&cfg->d11inf, &wlan_chan);
            brcmf_dbg(SCAN, "Chan : %d, Channel spec: %x\n", request->channel_list[i],
                      chanspec);
            params_le->channel_list[i] = chanspec;
        }
    } else {
        brcmf_dbg(SCAN, "Scanning all channels\n");
    }
    /* Copy ssid array if applicable */
    brcmf_dbg(SCAN, "### List of SSIDs to scan ### %d\n", n_ssids);
    if (n_ssids > 0) {
        if (params_le->scan_type == BRCMF_SCANTYPE_ACTIVE) {
            offset = offsetof(struct brcmf_scan_params_le, channel_list) +
                     n_channels * sizeof(uint16_t);
            offset = roundup(offset, sizeof(uint32_t));
            ptr = (char*)params_le + offset;
            for (i = 0; i < (int32_t)n_ssids; i++) {
                memset(&ssid_le, 0, sizeof(ssid_le));
                ssid_le.SSID_len = request->ssid_list[i].len;
                memcpy(ssid_le.SSID, request->ssid_list[i].data, request->ssid_list[i].len);
                if (!ssid_le.SSID_len) {
                    brcmf_dbg(SCAN, "%d: Broadcast scan\n", i);
                } else {
                    brcmf_dbg(SCAN, "%d: scan for  %.32s size=%d\n", i, ssid_le.SSID,
                              ssid_le.SSID_len);
                }
                memcpy(ptr, &ssid_le, sizeof(ssid_le));
                ptr += sizeof(ssid_le);
            }
        } else {
            brcmf_err("SSID list received for passive scan\n");
        }
    }
    /* Adding mask to channel numbers */
    params_le->channel_num = (n_ssids << BRCMF_SCAN_PARAMS_NSSID_SHIFT) |
        (n_channels & BRCMF_SCAN_PARAMS_COUNT_MASK);
}

static zx_status_t brcmf_run_escan(struct brcmf_cfg80211_info* cfg, struct brcmf_if* ifp,
                                   wlanif_scan_req_t* request) {
    int32_t params_size =
        BRCMF_SCAN_PARAMS_FIXED_SIZE + offsetof(struct brcmf_escan_params_le, params_le);
    struct brcmf_escan_params_le* params;
    zx_status_t err = ZX_OK;

    brcmf_dbg(SCAN, "E-SCAN START\n");

    if (request != NULL) {
        /* Allocate space for populating ssids in struct */
        params_size += sizeof(uint32_t) * ((request->num_channels + 1) / 2);

        /* Allocate space for populating ssids in struct */
        params_size += sizeof(struct brcmf_ssid_le) * request->num_ssids;
    }

    params = static_cast<decltype(params)>(calloc(1, params_size));
    if (!params) {
        err = ZX_ERR_NO_MEMORY;
        goto exit;
    }
    ZX_ASSERT(params_size + sizeof("escan") < BRCMF_DCMD_MEDLEN);
    brcmf_escan_prep(cfg, &params->params_le, request);
    params->version = BRCMF_ESCAN_REQ_VERSION;
    params->action = WL_ESCAN_ACTION_START;
    params->sync_id = 0x1234;

    err = brcmf_fil_iovar_data_set(ifp, "escan", params, params_size, nullptr);
    if (err != ZX_OK) {
        if (err == ZX_ERR_UNAVAILABLE) {
            brcmf_dbg(INFO, "system busy : escan canceled\n");
        } else {
            brcmf_err("error (%d)\n", err);
        }
    }

    free(params);
exit:
    return err;
}

static zx_status_t brcmf_do_escan(struct brcmf_if* ifp, wlanif_scan_req_t* req) {
    struct brcmf_cfg80211_info* cfg = ifp->drvr->config;
    zx_status_t err;
    struct brcmf_scan_results* results;
    struct escan_info* escan = &cfg->escan_info;

    brcmf_dbg(SCAN, "Enter\n");
    escan->ifp = ifp;
    escan->wiphy = cfg->wiphy;
    escan->escan_state = WL_ESCAN_STATE_SCANNING;

    brcmf_scan_config_mpc(ifp, 0);
    results = (struct brcmf_scan_results*)cfg->escan_info.escan_buf;
    results->version = 0;
    results->count = 0;
    results->buflen = WL_ESCAN_RESULTS_FIXED_SIZE;

    err = escan->run(cfg, ifp, req);
    if (err != ZX_OK) {
        brcmf_scan_config_mpc(ifp, 1);
    }
    return err;
}

zx_status_t brcmf_cfg80211_scan(struct net_device* ndev, wlanif_scan_req_t* req) {
    zx_status_t err;

    brcmf_dbg(TRACE, "Enter\n");
    struct wireless_dev* wdev = ndev_to_wdev(ndev);
    struct brcmf_cfg80211_vif* vif = containerof(wdev, struct brcmf_cfg80211_vif, wdev);
    if (!check_vif_up(vif)) {
        brcmf_dbg(TEMP, "Vif not up");
        return ZX_ERR_IO;
    }

    struct wiphy* wiphy = ndev_to_wiphy(ndev);
    struct brcmf_cfg80211_info* cfg = wiphy_to_cfg(wiphy);

    if (brcmf_test_bit_in_array(BRCMF_SCAN_STATUS_BUSY, &cfg->scan_status)) {
        brcmf_err("Scanning already: status (%lu)\n", cfg->scan_status.load());
        return ZX_ERR_UNAVAILABLE;
    }
    if (brcmf_test_bit_in_array(BRCMF_SCAN_STATUS_ABORT, &cfg->scan_status)) {
        brcmf_err("Scanning being aborted: status (%lu)\n", cfg->scan_status.load());
        return ZX_ERR_UNAVAILABLE;
    }
    if (brcmf_test_bit_in_array(BRCMF_SCAN_STATUS_SUPPRESS, &cfg->scan_status)) {
        brcmf_err("Scanning suppressed: status (%lu)\n", cfg->scan_status.load());
        return ZX_ERR_UNAVAILABLE;
    }
    if (brcmf_test_bit_in_array(BRCMF_VIF_STATUS_CONNECTING, &vif->sme_state)) {
        brcmf_err("Connecting: status (%lu)\n", vif->sme_state.load());
        return ZX_ERR_UNAVAILABLE;
    }
    /* If scan req comes for p2p0, send it over primary I/F */
    if (vif == cfg->p2p.bss_idx[P2PAPI_BSSCFG_DEVICE].vif) {
        vif = cfg->p2p.bss_idx[P2PAPI_BSSCFG_PRIMARY].vif;
    }

    brcmf_dbg(SCAN, "START ESCAN\n");

    cfg->scan_request = req;
    brcmf_set_bit_in_array(BRCMF_SCAN_STATUS_BUSY, &cfg->scan_status);

    cfg->escan_info.run = brcmf_run_escan;

    err = brcmf_do_escan(vif->ifp, req);
    if (err != ZX_OK) {
        goto scan_out;
    }

    /* Arm scan timeout timer */
    brcmf_timer_set(&cfg->escan_timeout, ZX_MSEC(BRCMF_ESCAN_TIMER_INTERVAL_MS));

    return ZX_OK;

scan_out:
    brcmf_err("scan error (%d)\n", err);
    brcmf_clear_bit_in_array(BRCMF_SCAN_STATUS_BUSY, &cfg->scan_status);
    cfg->scan_request = NULL;
    return err;
}

static void brcmf_init_prof(struct brcmf_cfg80211_profile* prof) {
    memset(prof, 0, sizeof(*prof));
}

static uint16_t brcmf_map_fw_linkdown_reason(const struct brcmf_event_msg* e) {
    uint16_t reason;

    switch (e->event_code) {
    case BRCMF_E_DEAUTH:
    case BRCMF_E_DEAUTH_IND:
        reason = WLAN_DEAUTH_REASON_LEAVING_NETWORK_DEAUTH;
        break;
    case BRCMF_E_DISASSOC_IND:
        reason = WLAN_DEAUTH_REASON_LEAVING_NETWORK_DISASSOC;
        break;
    case BRCMF_E_LINK:
    default:
        reason = WLAN_DEAUTH_REASON_UNSPECIFIED;
        break;
    }
    return reason;
}

static zx_status_t brcmf_set_pmk(struct brcmf_if* ifp, const uint8_t* pmk_data, uint16_t pmk_len) {
    struct brcmf_wsec_pmk_le pmk;
    int i;
    zx_status_t err;

    /* convert to firmware key format */
    pmk.key_len = pmk_len << 1;
    pmk.flags = BRCMF_WSEC_PASSPHRASE;
    for (i = 0; i < pmk_len; i++) {
        // TODO(cphoenix): Make sure handling of pmk keys is consistent with their being
        // binary values, not ASCII chars.
        snprintf((char*)&pmk.key[2 * i], 3, "%02x", pmk_data[i]);
    }

    /* store psk in firmware */
    err = brcmf_fil_cmd_data_set(ifp, BRCMF_C_SET_WSEC_PMK, &pmk, sizeof(pmk), nullptr);
    if (err != ZX_OK) {
        brcmf_err("failed to change PSK in firmware (len=%u)\n", pmk_len);
    }

    return err;
}

static void cfg80211_disconnected(struct brcmf_cfg80211_vif* vif, uint16_t reason) {
    struct net_device* ndev = vif->wdev.netdev;
    wlanif_deauth_indication_t ind;

    memcpy(ind.peer_sta_address, vif->profile.bssid, ETH_ALEN);
    ind.reason_code = reason;
    ndev->if_callbacks->deauth_ind(ndev->if_callback_cookie, &ind);
}

static void brcmf_link_down(struct brcmf_cfg80211_vif* vif, uint16_t reason) {
    struct brcmf_cfg80211_info* cfg = wiphy_to_cfg(vif->wdev.wiphy);
    zx_status_t err = ZX_OK;

    brcmf_dbg(TRACE, "Enter\n");

    if (brcmf_test_and_clear_bit_in_array(BRCMF_VIF_STATUS_CONNECTED, &vif->sme_state)) {
        brcmf_dbg(INFO, "Call WLC_DISASSOC to stop excess roaming\n ");
        err = brcmf_fil_cmd_data_set(vif->ifp, BRCMF_C_DISASSOC, NULL, 0, nullptr);
        if (err != ZX_OK) {
            brcmf_err("WLC_DISASSOC failed (%d)\n", err);
        }
        if (vif->wdev.iftype == WLAN_INFO_MAC_ROLE_CLIENT) {
            cfg80211_disconnected(vif, reason);
        }
    }
    brcmf_clear_bit_in_array(BRCMF_VIF_STATUS_CONNECTING, &vif->sme_state);
    brcmf_clear_bit_in_array(BRCMF_SCAN_STATUS_SUPPRESS, &cfg->scan_status);
    brcmf_btcoex_set_mode(vif, BRCMF_BTCOEX_ENABLED, 0);
    if (vif->profile.use_fwsup != BRCMF_PROFILE_FWSUP_NONE) {
        brcmf_set_pmk(vif->ifp, NULL, 0);
        vif->profile.use_fwsup = BRCMF_PROFILE_FWSUP_NONE;
    }
    brcmf_dbg(TRACE, "Exit\n");
}

static zx_status_t brcmf_set_wpa_version(struct net_device* ndev, bool is_protected_bss) {
    struct brcmf_cfg80211_profile* profile = ndev_to_prof(ndev);
    struct brcmf_cfg80211_security* sec;
    int32_t val = 0;
    zx_status_t err = ZX_OK;

    if (is_protected_bss) {
        val = WPA2_AUTH_PSK;// | WPA2_AUTH_UNSPECIFIED;
    } else {
        val = WPA_AUTH_DISABLED;
    }
    brcmf_dbg(CONN, "setting wpa_auth to 0x%0x\n", val);
    err = brcmf_fil_bsscfg_int_set(ndev_to_if(ndev), "wpa_auth", val);
    if (err != ZX_OK) {
        brcmf_err("set wpa_auth failed (%d)\n", err);
        return err;
    }
    sec = &profile->sec;
    // TODO(cphoenix): wpa_versions seems to be used only for WEP in brcmf_set_sharedkey(). Delete.
    sec->wpa_versions = 0;
    return err;
}

static zx_status_t brcmf_set_auth_type_open(struct net_device* ndev) {
    struct brcmf_cfg80211_profile* profile = ndev_to_prof(ndev);
    struct brcmf_cfg80211_security* sec;
    int32_t val = 0;
    zx_status_t err = ZX_OK;

    err = brcmf_fil_bsscfg_int_set(ndev_to_if(ndev), "auth", val);
    if (err != ZX_OK) {
        brcmf_err("set auth failed (%d)\n", err);
        return err;
    }
    sec = &profile->sec;
    sec->auth_type = 0;
    return err;
}

static zx_status_t brcmf_set_wsec_mode(struct net_device* ndev, bool is_protected_bss) {
    struct brcmf_cfg80211_profile* profile = ndev_to_prof(ndev);
    struct brcmf_cfg80211_security* sec;
    int32_t wsec;
    zx_status_t err = ZX_OK;

    if (is_protected_bss) {
        wsec = AES_ENABLED;
    } else {
        wsec = 0;
    }
    err = brcmf_fil_bsscfg_int_set(ndev_to_if(ndev), "wsec", wsec);
    if (err != ZX_OK) {
        brcmf_err("error (%d)\n", err);
        return err;
    }

    sec = &profile->sec;
    sec->cipher_pairwise = 0;
    sec->cipher_group = 0;

    return err;
}

// Retrieve information about the station with the specified MAC address. Note that
// association ID is only available when operating in AP mode (for our clients).
static zx_status_t brcmf_cfg80211_get_station(struct net_device* ndev, const uint8_t* mac,
                                              struct brcmf_sta_info_le* sta_info_le) {
    struct brcmf_if* ifp = ndev_to_if(ndev);
    zx_status_t err = ZX_OK;

    brcmf_dbg(TRACE, "Enter, MAC %02x:%02x:%02x:%02x:%02x:%02x\n",
              mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    if (!check_vif_up(ifp->vif)) {
        return ZX_ERR_IO;
    }

    memset(sta_info_le, 0, sizeof(*sta_info_le));
    memcpy(sta_info_le, mac, ETH_ALEN);

    // First, see if we have a TDLS peer
    err = brcmf_fil_iovar_data_get(ifp, "tdls_sta_info", sta_info_le, sizeof(*sta_info_le),
                                   nullptr);
    if (err != ZX_OK) {
        err = brcmf_fil_iovar_data_get(ifp, "sta_info", sta_info_le, sizeof(*sta_info_le),
                                       nullptr);
        if (err != ZX_OK) {
            brcmf_err("GET STA INFO failed, %d\n", err);
        }
    }
    brcmf_dbg(TRACE, "Exit\n");
    return err;
}

void brcmf_return_assoc_result(struct net_device* ndev, uint8_t result_code) {
    wlanif_assoc_confirm_t conf;

    conf.result_code = result_code;
    brcmf_dbg(TEMP, " * Hard-coding association_id to 42; this will likely break something!");
    conf.association_id = 42; // TODO: Use brcmf_cfg80211_get_station() to get aid

    ndev->if_callbacks->assoc_conf(ndev->if_callback_cookie, &conf);
}

zx_status_t brcmf_cfg80211_connect(struct net_device* ndev, wlanif_assoc_req_t* req) {
    struct brcmf_if* ifp = ndev_to_if(ndev);
    struct brcmf_cfg80211_info* cfg = ifp->drvr->config;
    struct brcmf_ext_join_params_le join_params;
    uint16_t chanspec;
    size_t join_params_size = 0;
    const void* ie;
    uint32_t ie_len;
    zx_status_t err = ZX_OK;
    uint32_t ssid_len = 0;

    brcmf_dbg(TRACE, "Enter\n");
    if (!check_vif_up(ifp->vif)) {
        return ZX_ERR_IO;
    }

    // Pass RSNE to firmware
    ie_len = req->rsne_len;
    ie = (req->rsne_len > 0) ? req->rsne : NULL;
    brcmf_fil_iovar_data_set(ifp, "wpaie", ie, ie_len, nullptr);

    // TODO(WLAN-733): We should be getting the IEs from SME. Passing a null entry seems
    // to work for now, presumably because the firmware uses its defaults.
    err = brcmf_vif_set_mgmt_ie(ifp->vif, BRCMF_VNDR_IE_ASSOCREQ_FLAG, NULL, 0);
    if (err != ZX_OK) {
        brcmf_err("Set Assoc REQ IE Failed\n");
    } else {
        brcmf_dbg(TRACE, "Applied Vndr IEs for Assoc request\n");
    }

    brcmf_set_bit_in_array(BRCMF_VIF_STATUS_CONNECTING, &ifp->vif->sme_state);
    chanspec = channel_to_chanspec(&cfg->d11inf, &ifp->bss.chan);
    cfg->channel = chanspec;

    // TODO(WLAN-733): Currently fails if a network only supports TKIP for its pairwise cipher
    bool using_wpa = req->rsne_len != 0;

    err = brcmf_set_wpa_version(ndev, using_wpa); // wpa_auth
    if (err != ZX_OK) {
        brcmf_err("wl_set_wpa_version failed (%d)\n", err);
        goto done;
    }

    // Open because we are using WPA2-PSK. With SAE support, this will need to change.
    err = brcmf_set_auth_type_open(ndev);
    if (err != ZX_OK) {
        brcmf_err("wl_set_auth_type failed (%d)\n", err);
        goto done;
    }

    err = brcmf_set_wsec_mode(ndev, using_wpa);
    if (err != ZX_OK) {
        brcmf_err("wl_set_set_cipher failed (%d)\n", err);
        goto done;
    }

    ssid_len = min_t(uint32_t, ifp->bss.ssid.len, WLAN_MAX_SSID_LEN);
    join_params_size = sizeof(join_params);
    memset(&join_params, 0, join_params_size);

    memcpy(&join_params.ssid_le.SSID, ifp->bss.ssid.data, ssid_len);
    join_params.ssid_le.SSID_len = ssid_len;

    memcpy(join_params.assoc_le.bssid, ifp->bss.bssid, ETH_ALEN);
    join_params.assoc_le.chanspec_num = 1;
    join_params.assoc_le.chanspec_list[0] = chanspec;

    join_params.scan_le.scan_type = 0; // use default
    join_params.scan_le.home_time = -1; // use default

    /* Increase dwell time to receive probe response or detect beacon from target AP at a noisy
       air only during connect command. */
    join_params.scan_le.active_time = BRCMF_SCAN_JOIN_ACTIVE_DWELL_TIME_MS;
    join_params.scan_le.passive_time = BRCMF_SCAN_JOIN_PASSIVE_DWELL_TIME_MS;
    /* To sync with presence period of VSDB GO send probe request more frequently. Probe request
       will be stopped when it gets probe response from target AP/GO. */
    join_params.scan_le.nprobes = BRCMF_SCAN_JOIN_ACTIVE_DWELL_TIME_MS /
                                  BRCMF_SCAN_JOIN_PROBE_INTERVAL_MS;

    brcmf_dbg(CONN, "Sending join request\n");
    err = brcmf_fil_bsscfg_data_set(ifp, "join", &join_params, join_params_size);
    if (err != ZX_OK) {
        brcmf_err("join failed (%d)\n", err);
    }

done:
    if (err != ZX_OK) {
        brcmf_clear_bit_in_array(BRCMF_VIF_STATUS_CONNECTING, &ifp->vif->sme_state);
        brcmf_dbg(CONN, "Failed during join: %s", zx_status_get_string(err));
        brcmf_return_assoc_result(ndev, WLAN_ASSOC_RESULT_REFUSED_REASON_UNSPECIFIED);
    }
    brcmf_dbg(TRACE, "Exit\n");
    return err;
}

static void brcmf_notify_deauth(struct net_device* ndev, uint8_t peer_sta_address[ETH_ALEN]) {
    wlanif_deauth_confirm_t resp;
    memcpy(resp.peer_sta_address, peer_sta_address, ETH_ALEN);
    ndev->if_callbacks->deauth_conf(ndev->if_callback_cookie, &resp);
}

static void brcmf_notify_disassoc(struct net_device* ndev, zx_status_t status) {
    wlanif_disassoc_confirm_t resp;
    resp.status = status;
    ndev->if_callbacks->disassoc_conf(ndev->if_callback_cookie, &resp);
}

static void brcmf_disconnect_done(struct brcmf_cfg80211_info* cfg) {
    struct net_device* ndev = cfg_to_ndev(cfg);
    struct brcmf_if* ifp = ndev_to_if(ndev);
    struct brcmf_cfg80211_profile* profile = &ifp->vif->profile;

    brcmf_dbg(TRACE, "Enter\n");

    if (brcmf_test_and_clear_bit_in_array(BRCMF_VIF_STATUS_DISCONNECTING, &ifp->vif->sme_state)) {
        brcmf_timer_stop(&cfg->disconnect_timeout);
        if (cfg->disconnect_mode == BRCMF_DISCONNECT_DEAUTH) {
            brcmf_notify_deauth(ndev, profile->bssid);
        } else {
            brcmf_notify_disassoc(ndev, ZX_OK);
        }
    }

    brcmf_dbg(TRACE, "Exit\n");
}

static void brcmf_disconnect_timeout_worker(struct work_struct* work) {
    struct brcmf_cfg80211_info* cfg = containerof(work, struct brcmf_cfg80211_info,
                                                 disconnect_timeout_work);
    brcmf_disconnect_done(cfg);
}

static void brcmf_disconnect_timeout(void* data) {
    pthread_mutex_lock(&irq_callback_lock);

    struct brcmf_cfg80211_info* cfg = static_cast<decltype(cfg)>(data);
    brcmf_dbg(TRACE, "Enter\n");
    workqueue_schedule_default(&cfg->disconnect_timeout_work);

    pthread_mutex_unlock(&irq_callback_lock);
}

static zx_status_t brcmf_cfg80211_disconnect(struct net_device* ndev,
                                             uint8_t peer_sta_address[ETH_ALEN],
                                             uint16_t reason_code,
                                             bool deauthenticate) {
    struct brcmf_if* ifp = ndev_to_if(ndev);
    struct brcmf_cfg80211_profile* profile = &ifp->vif->profile;
    struct brcmf_cfg80211_info* cfg = ifp->drvr->config;
    struct brcmf_scb_val_le scbval;
    zx_status_t status = ZX_OK;

    brcmf_dbg(TRACE, "Enter. Reason code = %d\n", reason_code);
    if (!check_vif_up(ifp->vif)) {
        status = ZX_ERR_IO;
        goto done;
    }

    if (!brcmf_test_bit_in_array(BRCMF_VIF_STATUS_CONNECTED, &ifp->vif->sme_state) &&
        !brcmf_test_bit_in_array(BRCMF_VIF_STATUS_CONNECTING, &ifp->vif->sme_state)) {
        status = ZX_ERR_BAD_STATE;
        goto done;
    }

    if (memcmp(peer_sta_address, profile->bssid, ETH_ALEN)) {
        status = ZX_ERR_INVALID_ARGS;
        goto done;
    }

    brcmf_clear_bit_in_array(BRCMF_VIF_STATUS_CONNECTED, &ifp->vif->sme_state);
    brcmf_clear_bit_in_array(BRCMF_VIF_STATUS_CONNECTING, &ifp->vif->sme_state);

    brcmf_dbg(CONN, "Disconnecting\n");

    memcpy(&scbval.ea, peer_sta_address, ETH_ALEN);
    scbval.val = reason_code;
    cfg->disconnect_mode = deauthenticate ? BRCMF_DISCONNECT_DEAUTH : BRCMF_DISCONNECT_DISASSOC;
    brcmf_set_bit_in_array(BRCMF_VIF_STATUS_DISCONNECTING, &ifp->vif->sme_state);
    status = brcmf_fil_cmd_data_set(ifp, BRCMF_C_DISASSOC, &scbval, sizeof(scbval), nullptr);
    if (status != ZX_OK) {
        brcmf_err("Failed to disassociate: %s\n", zx_status_get_string(status));
        goto done;
    }

    brcmf_timer_init(&cfg->disconnect_timeout, brcmf_disconnect_timeout, cfg);
    brcmf_timer_set(&cfg->disconnect_timeout, BRCMF_DISCONNECT_TIMEOUT);

done:
    brcmf_dbg(TRACE, "Exit\n");
    return status;
}

static zx_status_t brcmf_cfg80211_del_key(struct wiphy* wiphy, struct net_device* ndev,
                                          uint8_t key_idx, bool pairwise, const uint8_t* mac_addr) {
    struct brcmf_if* ifp = ndev_to_if(ndev);
    struct brcmf_wsec_key* key;
    zx_status_t err;

    brcmf_dbg(TRACE, "Enter\n");
    brcmf_dbg(CONN, "key index (%d)\n", key_idx);

    if (!check_vif_up(ifp->vif)) {
        return ZX_ERR_IO;
    }

    if (key_idx >= BRCMF_MAX_DEFAULT_KEYS) {
        /* we ignore this key index in this case */
        return ZX_ERR_INVALID_ARGS;
    }

    key = &ifp->vif->profile.key[key_idx];

    if (key->algo == CRYPTO_ALGO_OFF) {
        brcmf_dbg(CONN, "Ignore clearing of (never configured) key\n");
        return ZX_ERR_BAD_STATE;
    }

    memset(key, 0, sizeof(*key));
    key->index = (uint32_t)key_idx;
    key->flags = BRCMF_PRIMARY_KEY;

    /* Clear the key/index */
    err = send_key_to_dongle(ifp, key);

    brcmf_dbg(TRACE, "Exit\n");
    return err;
}

static zx_status_t brcmf_cfg80211_add_key(struct wiphy* wiphy, struct net_device* ndev,
                                          set_key_descriptor_t* req) {
    struct brcmf_if* ifp = ndev_to_if(ndev);
    struct brcmf_wsec_key* key;
    int32_t val;
    int32_t wsec;
    zx_status_t err;
    bool ext_key;
    uint8_t key_idx = req->key_id;
    bool pairwise = (req->key_type == WLAN_KEY_TYPE_PAIRWISE);
    const uint8_t* mac_addr = req->address;

    brcmf_dbg(TRACE, "Enter\n");
    brcmf_dbg(CONN, "key index (%d)\n", key_idx);
    if (!check_vif_up(ifp->vif)) {
        return ZX_ERR_IO;
    }

    if (key_idx >= BRCMF_MAX_DEFAULT_KEYS) {
        /* we ignore this key index in this case */
        brcmf_err("invalid key index (%d)\n", key_idx);
        return ZX_ERR_INVALID_ARGS;
    }

    if (req->length == 0) {
        return brcmf_cfg80211_del_key(wiphy, ndev, key_idx, pairwise, mac_addr);
    }

    if (req->length > sizeof(key->data)) {
        brcmf_err("Too long key length (%u)\n", req->length);
        return ZX_ERR_INVALID_ARGS;
    }

    ext_key = false;
    if (mac_addr && (req->cipher_suite_type != WPA_CIPHER_WEP_40) &&
            (req->cipher_suite_type != WPA_CIPHER_WEP_104)) {
        brcmf_dbg(TRACE, "Ext key, mac %02x:%02x:%02x:%02x:%02x:%02x",
                  mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
        ext_key = true;
    }

    key = &ifp->vif->profile.key[key_idx];
    memset(key, 0, sizeof(*key));
    if ((ext_key) && (!address_is_multicast(mac_addr))) {
        memcpy((char*)&key->ea, (void*)mac_addr, ETH_ALEN);
    }
    key->len = req->length;
    key->index = key_idx;
    memcpy(key->data, req->key, key->len);
    if (!ext_key) {
        key->flags = BRCMF_PRIMARY_KEY;
    }

    switch (req->cipher_suite_type) {
    case WPA_CIPHER_WEP_40:
        key->algo = CRYPTO_ALGO_WEP1;
        val = WEP_ENABLED;
        brcmf_dbg(CONN, "WPA_CIPHER_WEP_40\n");
        break;
    case WPA_CIPHER_WEP_104:
        key->algo = CRYPTO_ALGO_WEP128;
        val = WEP_ENABLED;
        brcmf_dbg(CONN, "WPA_CIPHER_WEP_104\n");
        break;
    case WPA_CIPHER_TKIP:
        /* Note: Linux swaps the Tx and Rx MICs in client mode, but this doesn't work for us (see
           NET-1679). It's unclear why this would be necessary. */
        key->algo = CRYPTO_ALGO_TKIP;
        val = TKIP_ENABLED;
        brcmf_dbg(CONN, "WPA_CIPHER_TKIP\n");
        break;
    case WPA_CIPHER_CMAC_128:
        key->algo = CRYPTO_ALGO_AES_CCM;
        val = AES_ENABLED;
        brcmf_dbg(CONN, "WPA_CIPHER_CMAC_128\n");
        break;
    case WPA_CIPHER_CCMP_128:
        key->algo = CRYPTO_ALGO_AES_CCM;
        val = AES_ENABLED;
        brcmf_dbg(CONN, "WPA_CIPHER_CCMP_128\n");
        break;
    default:
        brcmf_err("Unsupported cipher (0x%x)\n", req->cipher_suite_type);
        err = ZX_ERR_INVALID_ARGS;
        goto done;
    }

    err = send_key_to_dongle(ifp, key);
    if (ext_key || err != ZX_OK) {
        goto done;
    }

    err = brcmf_fil_bsscfg_int_get(ifp, "wsec", (uint32_t*)&wsec); // TODO(cphoenix): This cast?!?
    if (err != ZX_OK) {
        brcmf_err("get wsec error (%d)\n", err);
        goto done;
    }
    wsec |= val;
    err = brcmf_fil_bsscfg_int_set(ifp, "wsec", wsec);
    if (err != ZX_OK) {
        brcmf_err("set wsec error (%d)\n", err);
        goto done;
    }

done:
    brcmf_dbg(TRACE, "Exit\n");
    return err;
}

static void brcmf_parse_ies(uint8_t* ie, size_t ie_len, wlanif_bss_description_t* bss) {
    size_t offset = 0;
    while (offset < ie_len) {
        uint8_t type = ie[offset];
        uint8_t length = ie[offset + 1];
        switch (type) {
        case WLAN_IE_TYPE_SSID: {
            uint8_t ssid_len = min(length, sizeof(bss->ssid.data));
            memcpy(bss->ssid.data, ie + offset + 2, ssid_len);
            bss->ssid.len = ssid_len;
            break;
        }
        case WLAN_IE_TYPE_SUPP_RATES: {
            uint8_t num_supp_rates = min(length, WLAN_MAC_MAX_SUPP_RATES);
            memcpy(bss->rates, ie + offset + 2, num_supp_rates);
            bss->num_rates = num_supp_rates;
            break;
        }
        case WLAN_IE_TYPE_EXT_SUPP_RATES: {
            uint8_t num_ext_supp_rates = min(length, WLAN_MAC_MAX_EXT_RATES);
            memcpy(bss->rates + bss->num_rates, ie + offset + 2, num_ext_supp_rates);
            bss->num_rates += num_ext_supp_rates;
            break;
        }
        default:
            break;
        }
        offset += length + 2;
    }
}

static void brcmf_ies_extract_rsne(uint8_t* ie_chain, size_t ie_chain_len, uint8_t* rsne_data,
                                   size_t* rsne_len) {
    size_t offset = 0;
    *rsne_len = 0;
    while (offset < ie_chain_len) {
        uint8_t type = ie_chain[offset];
        uint8_t ie_total_length = ie_chain[offset + 1] + 2;
        if (type == WLAN_IE_TYPE_RSNE) {
            memcpy(rsne_data, &ie_chain[offset], ie_total_length);
            *rsne_len = ie_total_length;
            break;
        }
        offset += ie_total_length;
    }
}

static void brcmf_iedump(uint8_t* ies, size_t total_len) {
    size_t offset = 0;
    while (offset + TLV_HDR_LEN <= total_len) {
        uint8_t elem_type = ies[offset];
        uint8_t elem_len = ies[offset + TLV_LEN_OFF];
        offset += TLV_HDR_LEN;
        if (offset + elem_len > total_len) {
            break;
        }
        if (elem_type == 0) {
            brcmf_dbg(ALL, "IE 0 (name), len %d:", elem_len);
            brcmf_alphadump(ies + offset, elem_len);
        } else {
            brcmf_dbg_hex_dump(true, ies + offset, elem_len,
                               "IE %d, len %d:\n", elem_type, elem_len);
        }
        offset += elem_len;
    }
    if (offset != total_len) {
        brcmf_dbg(ALL, " * * Offset %ld didn't match length %ld", offset, total_len);
    }
}

#define EAPOL_ETHERNET_TYPE_UINT16 0x8e88

void brcmf_cfg80211_rx(struct brcmf_if* ifp, struct brcmf_netbuf* packet) {
    struct net_device* ndev = ifp->ndev;
    THROTTLE(10, brcmf_dbg_hex_dump(BRCMF_BYTES_ON() && BRCMF_DATA_ON(), packet->data,
                                    min(packet->len, 64),
                                    "Data received (%d bytes, max 64 shown):\n", packet->len););
    // IEEE Std. 802.3-2015, 3.1.1
    uint16_t eth_type = ((uint16_t*)(packet->data))[6];
    if (eth_type == EAPOL_ETHERNET_TYPE_UINT16) {
        wlanif_eapol_indication_t eapol_ind;
        // IEEE Std. 802.1X-2010, 11.3, Figure 11-1
        memcpy(&eapol_ind.dst_addr, packet->data, ETH_ALEN);
        memcpy(&eapol_ind.src_addr, packet->data + 6, ETH_ALEN);
        eapol_ind.data_len = packet->len - 14;
        eapol_ind.data = packet->data + 14;
        brcmf_dbg(TEMP, "EAPOL received");
        ndev->if_callbacks->eapol_ind(ndev->if_callback_cookie, &eapol_ind);
    } else {
        ndev->if_callbacks->data_recv(ndev->if_callback_cookie, packet->data, packet->len, 0);
    }
    brcmu_pkt_buf_free_netbuf(packet);
}

static void brcmf_return_scan_result(struct wiphy* wiphy, uint16_t channel, const uint8_t* bssid,
                                        uint16_t capability, uint16_t interval, uint8_t* ie,
                                        size_t ie_len, int16_t rssi_dbm) {
    struct net_device* ndev = wiphy_to_ndev(wiphy);
    wlanif_scan_result_t result = {};

    if (!ndev->scan_busy) {
        return;
    }
    result.txn_id = ndev->scan_txn_id;
    memcpy(result.bss.bssid, bssid, ETH_ALEN);
    brcmf_parse_ies(ie, ie_len, &result.bss);
    brcmf_ies_extract_rsne(ie, ie_len, result.bss.rsne, &result.bss.rsne_len);
    result.bss.bss_type = WLAN_BSS_TYPE_ANY_BSS;
    result.bss.beacon_period = 0;
    result.bss.dtim_period = 0;
    result.bss.timestamp = 0;
    result.bss.local_time = 0;
    result.bss.cap = capability;
    result.bss.chan.primary = (uint8_t)channel;
    result.bss.chan.cbw = CBW20; // TODO(cphoenix): Don't hard-code this.
    result.bss.rssi_dbm = (uint8_t)(min(0, max(-255, rssi_dbm)));
    result.bss.rcpi_dbmh = 0;
    result.bss.rsni_dbh = 0;
    brcmf_dbg(SCAN, "Returning scan result %.*s, channel %d, dbm %d, id %lu", result.bss.ssid.len,
              result.bss.ssid.data, result.bss.chan.primary, result.bss.rssi_dbm, result.txn_id);
    ndev->if_callbacks->on_scan_result(ndev->if_callback_cookie, &result);
}

static zx_status_t brcmf_inform_single_bss(struct brcmf_cfg80211_info* cfg,
                                           struct brcmf_bss_info_le* bi) {
    struct wiphy* wiphy = cfg_to_wiphy(cfg);
    struct brcmu_chan ch;
    uint16_t channel;
    uint16_t notify_capability;
    uint16_t notify_interval;
    uint8_t* notify_ie;
    size_t notify_ielen;
    int16_t notify_rssi_dbm;

    if (bi->length > WL_BSS_INFO_MAX) {
        brcmf_err("Bss info is larger than buffer. Discarding\n");
        brcmf_dbg(TEMP, "Early return, due to length.");
        return ZX_OK;
    }

    if (!bi->ctl_ch) {
        ch.chspec = bi->chanspec;
        cfg->d11inf.decchspec(&ch);
        bi->ctl_ch = ch.control_ch_num;
    }
    channel = bi->ctl_ch;

    notify_capability = bi->capability;
    notify_interval = bi->beacon_period;
    notify_ie = (uint8_t*)bi + bi->ie_offset;
    notify_ielen = bi->ie_length;
    notify_rssi_dbm = (int16_t)bi->RSSI;

    brcmf_dbg(CONN, "Scan result received  BSS: %02x:%02x:%02x:%02x:%02x:%02x"
                    "  Channel: %3d  Capability: %#6x  Beacon interval: %5d  Signal: %4d\n",
              bi->BSSID[0], bi->BSSID[1], bi->BSSID[2], bi->BSSID[3], bi->BSSID[4], bi->BSSID[5],
              channel, notify_capability, notify_interval, notify_rssi_dbm);
    if (BRCMF_CONN_ON() && BRCMF_BYTES_ON()) {
        brcmf_iedump(notify_ie, notify_ielen);
    }

    brcmf_return_scan_result(wiphy, (uint8_t)channel,
                                (const uint8_t*)bi->BSSID, notify_capability, notify_interval,
                                notify_ie, notify_ielen, notify_rssi_dbm);

    return ZX_OK;
}

static struct brcmf_bss_info_le* next_bss_le(struct brcmf_scan_results* list,
                                             struct brcmf_bss_info_le* bss) {
    if (bss == NULL) {
        return list->bss_info_le;
    }
    return (struct brcmf_bss_info_le*)((unsigned long)bss + bss->length);
}

static zx_status_t brcmf_inform_bss(struct brcmf_cfg80211_info* cfg) {
    struct brcmf_scan_results* bss_list;
    struct brcmf_bss_info_le* bi = NULL; /* must be initialized */
    zx_status_t err = ZX_OK;
    int i;

    bss_list = (struct brcmf_scan_results*)cfg->escan_info.escan_buf;
    if (bss_list->count != 0 && bss_list->version != BRCMF_BSS_INFO_VERSION) {
        brcmf_err("Version %d != WL_BSS_INFO_VERSION\n", bss_list->version);
        return ZX_ERR_NOT_SUPPORTED;
    }
    brcmf_dbg(SCAN, "scanned AP count (%d), wiphy %p", bss_list->count, cfg_to_wiphy(cfg));
    for (i = 0; i < (int32_t)bss_list->count; i++) {
        bi = next_bss_le(bss_list, bi);
        err = brcmf_inform_single_bss(cfg, bi);
        if (err != ZX_OK) {
            break;
        }
    }
    return err;
}

void brcmf_abort_scanning(struct brcmf_cfg80211_info* cfg) {
    struct escan_info* escan = &cfg->escan_info;

    brcmf_set_bit_in_array(BRCMF_SCAN_STATUS_ABORT, &cfg->scan_status);
    if (cfg->int_escan_map || cfg->scan_request) {
        escan->escan_state = WL_ESCAN_STATE_IDLE;
        brcmf_notify_escan_complete(cfg, escan->ifp, true, true);
    }
    brcmf_clear_bit_in_array(BRCMF_SCAN_STATUS_BUSY, &cfg->scan_status);
    brcmf_clear_bit_in_array(BRCMF_SCAN_STATUS_ABORT, &cfg->scan_status);
}

static void brcmf_cfg80211_escan_timeout_worker(struct work_struct* work) {
    struct brcmf_cfg80211_info* cfg =
        containerof(work, struct brcmf_cfg80211_info, escan_timeout_work);

    brcmf_inform_bss(cfg);
    brcmf_notify_escan_complete(cfg, cfg->escan_info.ifp, true, true);
}

static void brcmf_escan_timeout(void* data) {
    pthread_mutex_lock(&irq_callback_lock);
    struct brcmf_cfg80211_info* cfg = static_cast<decltype(cfg)>(data);

    if (cfg->int_escan_map || cfg->scan_request) {
        brcmf_err("timer expired\n");
        workqueue_schedule_default(&cfg->escan_timeout_work);
    }
    pthread_mutex_unlock(&irq_callback_lock);
}

static bool brcmf_compare_update_same_bss(struct brcmf_cfg80211_info* cfg,
                                             struct brcmf_bss_info_le* bss,
                                             struct brcmf_bss_info_le* bss_info_le) {
    struct brcmu_chan ch_bss, ch_bss_info_le;

    ch_bss.chspec = bss->chanspec;
    cfg->d11inf.decchspec(&ch_bss);
    ch_bss_info_le.chspec = bss_info_le->chanspec;
    cfg->d11inf.decchspec(&ch_bss_info_le);

    if (!memcmp(&bss_info_le->BSSID, &bss->BSSID, ETH_ALEN) && ch_bss.band == ch_bss_info_le.band &&
            bss_info_le->SSID_len == bss->SSID_len &&
            !memcmp(bss_info_le->SSID, bss->SSID, bss_info_le->SSID_len)) {
        if ((bss->flags & BRCMF_BSS_RSSI_ON_CHANNEL) ==
                (bss_info_le->flags & BRCMF_BSS_RSSI_ON_CHANNEL)) {
            int16_t bss_rssi = bss->RSSI;
            int16_t bss_info_rssi = bss_info_le->RSSI;

            /* preserve max RSSI if the measurements are
             * both on-channel or both off-channel
             */
            if (bss_info_rssi > bss_rssi) {
                bss->RSSI = bss_info_le->RSSI;
            }
        } else if ((bss->flags & BRCMF_BSS_RSSI_ON_CHANNEL) &&
                   (bss_info_le->flags & BRCMF_BSS_RSSI_ON_CHANNEL) == 0) {
            /* preserve the on-channel rssi measurement
             * if the new measurement is off channel
             */
            bss->RSSI = bss_info_le->RSSI;
            bss->flags |= BRCMF_BSS_RSSI_ON_CHANNEL;
        }
        return true;
    }
    return false;
}

static zx_status_t brcmf_cfg80211_escan_handler(struct brcmf_if* ifp,
                                                const struct brcmf_event_msg* e, void* data) {
    struct brcmf_cfg80211_info* cfg = ifp->drvr->config;
    int32_t status;
    struct brcmf_escan_result_le* escan_result_le;
    uint32_t escan_buflen;
    struct brcmf_bss_info_le* bss_info_le;
    struct brcmf_bss_info_le* bss = NULL;
    uint32_t bi_length = 0;
    struct brcmf_scan_results* list = NULL;
    uint32_t i;
    bool aborted;

    status = e->status;

    if (status == BRCMF_E_STATUS_ABORT) {
        goto exit;
    }

    if (!brcmf_test_bit_in_array(BRCMF_SCAN_STATUS_BUSY, &cfg->scan_status)) {
        brcmf_err("scan not ready, bsscfgidx=%d\n", ifp->bsscfgidx);
        return ZX_ERR_UNAVAILABLE;
    }

    if (status == BRCMF_E_STATUS_PARTIAL) {
        brcmf_dbg(SCAN, "ESCAN Partial result\n");
        if (e->datalen < sizeof(*escan_result_le)) {
            brcmf_err("invalid event data length\n");
            goto exit;
        }
        escan_result_le = (struct brcmf_escan_result_le*)data;
        if (!escan_result_le) {
            brcmf_err("Invalid escan result (NULL pointer)\n");
            goto exit;
        }
        escan_buflen = escan_result_le->buflen;
        if (escan_buflen > BRCMF_ESCAN_BUF_SIZE || escan_buflen > e->datalen ||
                escan_buflen < sizeof(*escan_result_le)) {
            brcmf_err("Invalid escan buffer length: %d\n", escan_buflen);
            goto exit;
        }
        if (escan_result_le->bss_count != 1) {
            brcmf_err("Invalid bss_count %d: ignoring\n", escan_result_le->bss_count);
            goto exit;
        }
        bss_info_le = &escan_result_le->bss_info_le;

        if (brcmf_p2p_scan_finding_common_channel(cfg, bss_info_le)) {
            goto exit;
        }

        if (!cfg->int_escan_map && !cfg->scan_request) {
            brcmf_dbg(SCAN, "result without cfg80211 request\n");
            goto exit;
        }

        bi_length = bss_info_le->length;
        if (bi_length != escan_buflen - WL_ESCAN_RESULTS_FIXED_SIZE) {
            brcmf_err("Ignoring invalid bss_info length: %d\n", bi_length);
            goto exit;
        }

        list = (struct brcmf_scan_results*)cfg->escan_info.escan_buf;
        if (bi_length > BRCMF_ESCAN_BUF_SIZE - list->buflen) {
            brcmf_err("Buffer is too small: ignoring\n");
            goto exit;
        }

        for (i = 0; i < list->count; i++) {
            bss = bss ? (struct brcmf_bss_info_le*)((unsigned char*)bss + bss->length)
                  : list->bss_info_le;
            if (brcmf_compare_update_same_bss(cfg, bss, bss_info_le)) {
                goto exit;
            }
        }
        memcpy(&cfg->escan_info.escan_buf[list->buflen], bss_info_le, bi_length);
        list->version = bss_info_le->version;
        list->buflen += bi_length;
        list->count++;
    } else {
        cfg->escan_info.escan_state = WL_ESCAN_STATE_IDLE;
        if (brcmf_p2p_scan_finding_common_channel(cfg, NULL)) {
            goto exit;
        }
        if (cfg->int_escan_map || cfg->scan_request) {
            brcmf_inform_bss(cfg);
            aborted = status != BRCMF_E_STATUS_SUCCESS;
            brcmf_notify_escan_complete(cfg, ifp, aborted, false);
        } else {
            brcmf_dbg(SCAN, "Ignored scan complete result 0x%x\n", status);
        }
    }
exit:
    return ZX_OK;
}

static void brcmf_init_escan(struct brcmf_cfg80211_info* cfg) {
    brcmf_fweh_register(cfg->pub, BRCMF_E_ESCAN_RESULT, brcmf_cfg80211_escan_handler);
    cfg->escan_info.escan_state = WL_ESCAN_STATE_IDLE;
    /* Init scan_timeout timer */
    cfg->escan_timeout.data = cfg;
    brcmf_timer_init(&cfg->escan_timeout, brcmf_escan_timeout, cfg);
    workqueue_init_work(&cfg->escan_timeout_work, brcmf_cfg80211_escan_timeout_worker);
}

static wlanif_scan_req_t* brcmf_alloc_internal_escan_request(void) {
    return static_cast<wlanif_scan_req_t*>(calloc(1, sizeof(wlanif_scan_req_t)));
}

static zx_status_t brcmf_internal_escan_add_info(wlanif_scan_req_t* req, uint8_t* ssid,
                                                 uint8_t ssid_len, uint8_t channel) {
    size_t i;

    for (i = 0; i < req->num_channels; i++) {
        if (req->channel_list[i] == channel) {
            break;
        }
    }
    if (i == req->num_channels) {
        if (req->num_channels < WLAN_INFO_CHANNEL_LIST_MAX_CHANNELS) {
            req->channel_list[req->num_channels++] = channel;
        } else {
            brcmf_err("escan channel list full, suppressing channel %d\n", channel);
        }
    }

    for (i = 0; i < req->num_ssids; i++) {
        if (req->ssid_list[i].len == ssid_len &&
            !memcmp(req->ssid_list[i].data, ssid, ssid_len)) {
            break;
        }
    }
    if (i == req->num_ssids) {
        if (req->num_ssids < WLAN_SCAN_MAX_SSIDS) {
            memcpy(req->ssid_list[req->num_ssids].data, ssid, ssid_len);
            req->ssid_list[req->num_ssids++].len = ssid_len;
        } else {
            brcmf_err("escan ssid list full, suppressing '%.*s'\n", ssid_len, ssid);
        }
    }

    return ZX_OK;
}

static zx_status_t brcmf_start_internal_escan(struct brcmf_if* ifp, uint32_t fwmap,
                                              wlanif_scan_req_t* req) {
    struct brcmf_cfg80211_info* cfg = ifp->drvr->config;
    zx_status_t err;

    if (brcmf_test_bit_in_array(BRCMF_SCAN_STATUS_BUSY, &cfg->scan_status)) {
        if (cfg->int_escan_map) {
            brcmf_dbg(SCAN, "aborting internal scan: map=%u\n", cfg->int_escan_map);
        }
        /* Abort any on-going scan */
        brcmf_abort_scanning(cfg);
    }

    brcmf_dbg(SCAN, "start internal scan: map=%u\n", fwmap);
    brcmf_set_bit_in_array(BRCMF_SCAN_STATUS_BUSY, &cfg->scan_status);
    cfg->escan_info.run = brcmf_run_escan;
    err = brcmf_do_escan(ifp, req);
    if (err != ZX_OK) {
        brcmf_clear_bit_in_array(BRCMF_SCAN_STATUS_BUSY, &cfg->scan_status);
        return err;
    }
    cfg->int_escan_map = fwmap;
    return ZX_OK;
}

static struct brcmf_pno_net_info_le* brcmf_get_netinfo_array(
    struct brcmf_pno_scanresults_le* pfn_v1) {
    struct brcmf_pno_scanresults_v2_le* pfn_v2;
    struct brcmf_pno_net_info_le* netinfo;

    switch (pfn_v1->version) {
    default:
        WARN_ON(1);
        /* fall-thru */
    case 1:
        netinfo = (struct brcmf_pno_net_info_le*)(pfn_v1 + 1);
        break;
    case 2:
        pfn_v2 = (struct brcmf_pno_scanresults_v2_le*)pfn_v1;
        netinfo = (struct brcmf_pno_net_info_le*)(pfn_v2 + 1);
        break;
    }

    return netinfo;
}

/* PFN result doesn't have all the info which are required by the supplicant
 * (For e.g IEs) Do a target Escan so that sched scan results are reported
 * via wl_inform_single_bss in the required format.
 */
static zx_status_t brcmf_notify_sched_scan_results(struct brcmf_if* ifp,
                                                   const struct brcmf_event_msg* e, void* data) {
    struct brcmf_cfg80211_info* cfg = ifp->drvr->config;
    struct net_device* ndev = cfg_to_ndev(cfg);
    wlanif_scan_req_t* req = NULL;
    struct brcmf_pno_net_info_le* netinfo;
    struct brcmf_pno_net_info_le* netinfo_start;
    int i;
    zx_status_t err = ZX_OK;
    struct brcmf_pno_scanresults_le* pfn_result;
    uint32_t bucket_map;
    uint32_t result_count;
    uint32_t status;
    uint32_t datalen;

    brcmf_dbg(SCAN, "Enter\n");

    if (e->datalen < (sizeof(*pfn_result) + sizeof(*netinfo))) {
        brcmf_dbg(SCAN, "Event data to small. Ignore\n");
        return ZX_OK;
    }

    if (e->event_code == BRCMF_E_PFN_NET_LOST) {
        brcmf_dbg(SCAN, "PFN NET LOST event. Do Nothing\n");
        return ZX_OK;
    }

    pfn_result = (struct brcmf_pno_scanresults_le*)data;
    result_count = pfn_result->count;
    status = pfn_result->status;

    /* PFN event is limited to fit 512 bytes so we may get
     * multiple NET_FOUND events. For now place a warning here.
     */
    WARN_ON(status != BRCMF_PNO_SCAN_COMPLETE);
    brcmf_dbg(SCAN, "PFN NET FOUND event. count: %d\n", result_count);
    if (!result_count) {
        brcmf_err("FALSE PNO Event. (pfn_count == 0)\n");
        // TODO(cphoenix): err isn't set here. Should it be?
        goto out_err;
    }

    netinfo_start = brcmf_get_netinfo_array(pfn_result);
    datalen = e->datalen - ((char*)netinfo_start - (char*)pfn_result);
    if (datalen < result_count * sizeof(*netinfo)) {
        brcmf_err("insufficient event data\n");
        // TODO(cphoenix): err isn't set here. Should it be?
        goto out_err;
    }

    req = brcmf_alloc_internal_escan_request();
    if (!req) {
        err = ZX_ERR_NO_MEMORY;
        goto out_err;
    }

    bucket_map = 0;
    for (i = 0; i < (int32_t)result_count; i++) {
        netinfo = &netinfo_start[i];

        if (netinfo->SSID_len > WLAN_MAX_SSID_LEN) {
            netinfo->SSID_len = WLAN_MAX_SSID_LEN;
        }
        brcmf_dbg(SCAN, "SSID:%.32s Channel:%d\n", netinfo->SSID, netinfo->channel);
        bucket_map |= brcmf_pno_get_bucket_map(cfg->pno, netinfo);
        err = brcmf_internal_escan_add_info(req, netinfo->SSID, netinfo->SSID_len,
                                            netinfo->channel);
        if (err != ZX_OK) {
            goto out_err;
        }
    }

    if (!bucket_map) {
        goto free_req;
    }

    err = brcmf_start_internal_escan(ifp, bucket_map, req);
    if (err == ZX_OK) {
        goto free_req;
    }

out_err:
    if (ndev->scan_busy) {
        brcmf_dbg(TEMP, "out_err %d, signaling scan end", err);
        brcmf_signal_scan_end(ndev, ndev->scan_txn_id, WLAN_SCAN_RESULT_INTERNAL_ERROR);
    }
free_req:
    free(req);
    return err;
}

static zx_status_t brcmf_configure_opensecurity(struct brcmf_if* ifp) {
    zx_status_t err;
    int32_t wpa_val;

    /* set auth */
    err = brcmf_fil_bsscfg_int_set(ifp, "auth", 0);
    if (err != ZX_OK) {
        brcmf_err("auth error %d\n", err);
        return err;
    }
    /* set wsec */
    err = brcmf_fil_bsscfg_int_set(ifp, "wsec", 0);
    if (err != ZX_OK) {
        brcmf_err("wsec error %d\n", err);
        return err;
    }
    /* set upper-layer auth */
    wpa_val = WPA_AUTH_DISABLED;
    err = brcmf_fil_bsscfg_int_set(ifp, "wpa_auth", wpa_val);
    if (err != ZX_OK) {
        brcmf_err("wpa_auth error %d\n", err);
        return err;
    }

    return ZX_OK;
}

static bool brcmf_valid_wpa_oui(uint8_t* oui, bool is_rsn_ie) {
    if (is_rsn_ie) {
        return (memcmp(oui, RSN_OUI, TLV_OUI_LEN) == 0);
    }

    return (memcmp(oui, WPA_OUI, TLV_OUI_LEN) == 0);
}

static zx_status_t brcmf_configure_wpaie(struct brcmf_if* ifp, const struct brcmf_vs_tlv* wpa_ie,
                                         bool is_rsn_ie) {
    uint32_t auth = 0; /* d11 open authentication */
    uint16_t count;
    zx_status_t err = ZX_OK;
    int32_t len;
    uint32_t i;
    uint32_t wsec;
    uint32_t pval = 0;
    uint32_t gval = 0;
    uint32_t wpa_auth = 0;
    uint32_t offset;
    uint8_t* data;
    uint16_t rsn_cap;
    uint32_t wme_bss_disable;
    uint32_t mfp;

    brcmf_dbg(TRACE, "Enter\n");
    if (wpa_ie == NULL) {
        goto exit;
    }

    len = wpa_ie->len + TLV_HDR_LEN;
    data = (uint8_t*)wpa_ie;
    offset = TLV_HDR_LEN;
    if (!is_rsn_ie) {
        offset += VS_IE_FIXED_HDR_LEN;
    } else {
        offset += WPA_IE_VERSION_LEN;
    }

    /* check for multicast cipher suite */
    if ((int32_t)offset + WPA_IE_MIN_OUI_LEN > len) {
        err = ZX_ERR_INVALID_ARGS;
        brcmf_err("no multicast cipher suite\n");
        goto exit;
    }

    if (!brcmf_valid_wpa_oui(&data[offset], is_rsn_ie)) {
        err = ZX_ERR_INVALID_ARGS;
        brcmf_err("invalid OUI\n");
        goto exit;
    }
    offset += TLV_OUI_LEN;

    /* pick up multicast cipher */
    switch (data[offset]) {
    case WPA_CIPHER_NONE:
        gval = 0;
        break;
    case WPA_CIPHER_WEP_40:
    case WPA_CIPHER_WEP_104:
        gval = WEP_ENABLED;
        break;
    case WPA_CIPHER_TKIP:
        gval = TKIP_ENABLED;
        break;
    case WPA_CIPHER_CCMP_128:
        gval = AES_ENABLED;
        break;
    default:
        err = ZX_ERR_INVALID_ARGS;
        brcmf_err("Invalid multi cast cipher info\n");
        goto exit;
    }

    offset++;
    /* walk thru unicast cipher list and pick up what we recognize */
    count = data[offset] + (data[offset + 1] << 8);
    offset += WPA_IE_SUITE_COUNT_LEN;
    /* Check for unicast suite(s) */
    if ((int32_t)(offset + (WPA_IE_MIN_OUI_LEN * count)) > len) {
        err = ZX_ERR_INVALID_ARGS;
        brcmf_err("no unicast cipher suite\n");
        goto exit;
    }
    for (i = 0; i < count; i++) {
        if (!brcmf_valid_wpa_oui(&data[offset], is_rsn_ie)) {
            err = ZX_ERR_INVALID_ARGS;
            brcmf_err("ivalid OUI\n");
            goto exit;
        }
        offset += TLV_OUI_LEN;
        switch (data[offset]) {
        case WPA_CIPHER_NONE:
            break;
        case WPA_CIPHER_WEP_40:
        case WPA_CIPHER_WEP_104:
            pval |= WEP_ENABLED;
            break;
        case WPA_CIPHER_TKIP:
            pval |= TKIP_ENABLED;
            break;
        case WPA_CIPHER_CCMP_128:
            pval |= AES_ENABLED;
            break;
        default:
            brcmf_err("Invalid unicast security info\n");
        }
        offset++;
    }
    /* walk thru auth management suite list and pick up what we recognize */
    count = data[offset] + (data[offset + 1] << 8);
    offset += WPA_IE_SUITE_COUNT_LEN;
    /* Check for auth key management suite(s) */
    if ((int32_t)(offset + (WPA_IE_MIN_OUI_LEN * count)) > len) {
        err = ZX_ERR_INVALID_ARGS;
        brcmf_err("no auth key mgmt suite\n");
        goto exit;
    }
    for (i = 0; i < count; i++) {
        if (!brcmf_valid_wpa_oui(&data[offset], is_rsn_ie)) {
            err = ZX_ERR_INVALID_ARGS;
            brcmf_err("ivalid OUI\n");
            goto exit;
        }
        offset += TLV_OUI_LEN;
        switch (data[offset]) {
        case RSN_AKM_NONE:
            brcmf_dbg(TRACE, "RSN_AKM_NONE\n");
            wpa_auth |= WPA_AUTH_NONE;
            break;
        case RSN_AKM_UNSPECIFIED:
            brcmf_dbg(TRACE, "RSN_AKM_UNSPECIFIED\n");
            is_rsn_ie ? (wpa_auth |= WPA2_AUTH_UNSPECIFIED) : (wpa_auth |= WPA_AUTH_UNSPECIFIED);
            break;
        case RSN_AKM_PSK:
            brcmf_dbg(TRACE, "RSN_AKM_PSK\n");
            is_rsn_ie ? (wpa_auth |= WPA2_AUTH_PSK) : (wpa_auth |= WPA_AUTH_PSK);
            break;
        case RSN_AKM_SHA256_PSK:
            brcmf_dbg(TRACE, "RSN_AKM_MFP_PSK\n");
            wpa_auth |= WPA2_AUTH_PSK_SHA256;
            break;
        case RSN_AKM_SHA256_1X:
            brcmf_dbg(TRACE, "RSN_AKM_MFP_1X\n");
            wpa_auth |= WPA2_AUTH_1X_SHA256;
            break;
        default:
            brcmf_err("Invalid key mgmt info\n");
        }
        offset++;
    }

    mfp = BRCMF_MFP_NONE;
    if (is_rsn_ie) {
        wme_bss_disable = 1;
        if (((int32_t)offset + RSN_CAP_LEN) <= len) {
            rsn_cap = data[offset] + (data[offset + 1] << 8);
            if (rsn_cap & RSN_CAP_PTK_REPLAY_CNTR_MASK) {
                wme_bss_disable = 0;
            }
            if (rsn_cap & RSN_CAP_MFPR_MASK) {
                brcmf_dbg(TRACE, "MFP Required\n");
                mfp = BRCMF_MFP_REQUIRED;
                /* Firmware only supports mfp required in
                 * combination with WPA2_AUTH_PSK_SHA256 or
                 * WPA2_AUTH_1X_SHA256.
                 */
                if (!(wpa_auth & (WPA2_AUTH_PSK_SHA256 | WPA2_AUTH_1X_SHA256))) {
                    err = ZX_ERR_INVALID_ARGS;
                    goto exit;
                }
                /* Firmware has requirement that WPA2_AUTH_PSK/
                 * WPA2_AUTH_UNSPECIFIED be set, if SHA256 OUI
                 * is to be included in the rsn ie.
                 */
                if (wpa_auth & WPA2_AUTH_PSK_SHA256) {
                    wpa_auth |= WPA2_AUTH_PSK;
                } else if (wpa_auth & WPA2_AUTH_1X_SHA256) {
                    wpa_auth |= WPA2_AUTH_UNSPECIFIED;
                }
            } else if (rsn_cap & RSN_CAP_MFPC_MASK) {
                brcmf_dbg(TRACE, "MFP Capable\n");
                mfp = BRCMF_MFP_CAPABLE;
            }
        }
        offset += RSN_CAP_LEN;
        /* set wme_bss_disable to sync RSN Capabilities */
        err = brcmf_fil_bsscfg_int_set(ifp, "wme_bss_disable", wme_bss_disable);
        if (err != ZX_OK) {
            brcmf_err("wme_bss_disable error %d\n", err);
            goto exit;
        }

        /* Skip PMKID cnt as it is know to be 0 for AP. */
        offset += RSN_PMKID_COUNT_LEN;

        /* See if there is BIP wpa suite left for MFP */
        if (brcmf_feat_is_enabled(ifp, BRCMF_FEAT_MFP) &&
                ((int32_t)(offset + WPA_IE_MIN_OUI_LEN) <= len)) {
            err = brcmf_fil_bsscfg_data_set(ifp, "bip", &data[offset], WPA_IE_MIN_OUI_LEN);
            if (err != ZX_OK) {
                brcmf_err("bip error %d\n", err);
                goto exit;
            }
        }
    }
    /* FOR WPS , set SES_OW_ENABLED */
    wsec = (pval | gval | SES_OW_ENABLED);

    /* set auth */
    err = brcmf_fil_bsscfg_int_set(ifp, "auth", auth);
    if (err != ZX_OK) {
        brcmf_err("auth error %d\n", err);
        goto exit;
    }
    /* set wsec */
    err = brcmf_fil_bsscfg_int_set(ifp, "wsec", wsec);
    if (err != ZX_OK) {
        brcmf_err("wsec error %d\n", err);
        goto exit;
    }
    /* Configure MFP, this needs to go after wsec otherwise the wsec command
     * will overwrite the values set by MFP
     */
    if (brcmf_feat_is_enabled(ifp, BRCMF_FEAT_MFP)) {
        err = brcmf_fil_bsscfg_int_set(ifp, "mfp", mfp);
        if (err != ZX_OK) {
            brcmf_err("mfp error %s\n", zx_status_get_string(err));
            goto exit;
        }
    }
    /* set upper-layer auth */
    err = brcmf_fil_bsscfg_int_set(ifp, "wpa_auth", wpa_auth);
    if (err != ZX_OK) {
        brcmf_err("wpa_auth error %d\n", err);
        goto exit;
    }

exit:
    return err;
}

static zx_status_t brcmf_parse_vndr_ies(const uint8_t* vndr_ie_buf, uint32_t vndr_ie_len,
                                    struct parsed_vndr_ies* vndr_ies) {
    struct brcmf_vs_tlv* vndrie;
    struct brcmf_tlv* ie;
    struct parsed_vndr_ie_info* parsed_info;
    int32_t remaining_len;

    remaining_len = (int32_t)vndr_ie_len;
    memset(vndr_ies, 0, sizeof(*vndr_ies));

    ie = (struct brcmf_tlv*)vndr_ie_buf;
    while (ie) {
        if (ie->id != WLAN_EID_VENDOR_SPECIFIC) {
            goto next;
        }
        vndrie = (struct brcmf_vs_tlv*)ie;
        /* len should be bigger than OUI length + one */
        if (vndrie->len < (VS_IE_FIXED_HDR_LEN - TLV_HDR_LEN + 1)) {
            brcmf_err("invalid vndr ie. length is too small %d\n", vndrie->len);
            goto next;
        }
        /* if wpa or wme ie, do not add ie */
        if (!memcmp(vndrie->oui, (uint8_t*)WPA_OUI, TLV_OUI_LEN) &&
                ((vndrie->oui_type == WPA_OUI_TYPE) || (vndrie->oui_type == WME_OUI_TYPE))) {
            brcmf_dbg(TRACE, "Found WPA/WME oui. Do not add it\n");
            goto next;
        }

        parsed_info = &vndr_ies->ie_info[vndr_ies->count];

        /* save vndr ie information */
        parsed_info->ie_ptr = (uint8_t*)vndrie;
        parsed_info->ie_len = vndrie->len + TLV_HDR_LEN;
        memcpy(&parsed_info->vndrie, vndrie, sizeof(*vndrie));

        vndr_ies->count++;

        brcmf_dbg(TRACE, "** OUI %02x %02x %02x, type 0x%02x\n", parsed_info->vndrie.oui[0],
                  parsed_info->vndrie.oui[1], parsed_info->vndrie.oui[2],
                  parsed_info->vndrie.oui_type);

        if (vndr_ies->count >= VNDR_IE_PARSE_LIMIT) {
            break;
        }
next:
        remaining_len -= (ie->len + TLV_HDR_LEN);
        if (remaining_len <= TLV_HDR_LEN) {
            ie = NULL;
        } else {
            ie = (struct brcmf_tlv*)(((uint8_t*)ie) + ie->len + TLV_HDR_LEN);
        }
    }
    return ZX_OK;
}

static uint32_t brcmf_vndr_ie(uint8_t* iebuf, int32_t pktflag, uint8_t* ie_ptr, uint32_t ie_len,
                              int8_t* add_del_cmd) {
    strncpy((char*)iebuf, (char*)add_del_cmd, VNDR_IE_CMD_LEN - 1);
    iebuf[VNDR_IE_CMD_LEN - 1] = '\0';

    *(uint32_t*)&iebuf[VNDR_IE_COUNT_OFFSET] = 1;

    *(uint32_t*)&iebuf[VNDR_IE_PKTFLAG_OFFSET] = pktflag;

    memcpy(&iebuf[VNDR_IE_VSIE_OFFSET], ie_ptr, ie_len);

    return ie_len + VNDR_IE_HDR_SIZE;
}

zx_status_t brcmf_vif_set_mgmt_ie(struct brcmf_cfg80211_vif* vif, int32_t pktflag,
                                  const uint8_t* vndr_ie_buf, uint32_t vndr_ie_len) {
    struct brcmf_if* ifp;
    struct vif_saved_ie* saved_ie;
    zx_status_t err = ZX_OK;
    uint8_t* iovar_ie_buf;
    uint8_t* curr_ie_buf;
    uint8_t* mgmt_ie_buf = NULL;
    int mgmt_ie_buf_len;
    uint32_t* mgmt_ie_len;
    uint32_t del_add_ie_buf_len = 0;
    uint32_t total_ie_buf_len = 0;
    uint32_t parsed_ie_buf_len = 0;
    struct parsed_vndr_ies old_vndr_ies;
    struct parsed_vndr_ies new_vndr_ies;
    struct parsed_vndr_ie_info* vndrie_info;
    int32_t i;
    uint8_t* ptr;
    int remained_buf_len;

    if (!vif) {
        return ZX_ERR_IO_NOT_PRESENT;
    }
    ifp = vif->ifp;
    saved_ie = &vif->saved_ie;

    brcmf_dbg(TRACE, "bsscfgidx %d, pktflag : 0x%02X\n", ifp->bsscfgidx, pktflag);
    iovar_ie_buf = static_cast<decltype(iovar_ie_buf)>(calloc(1, WL_EXTRA_BUF_MAX));
    if (!iovar_ie_buf) {
        return ZX_ERR_NO_MEMORY;
    }
    curr_ie_buf = iovar_ie_buf;
    switch (pktflag) {
    case BRCMF_VNDR_IE_PRBREQ_FLAG:
        mgmt_ie_buf = saved_ie->probe_req_ie;
        mgmt_ie_len = &saved_ie->probe_req_ie_len;
        mgmt_ie_buf_len = sizeof(saved_ie->probe_req_ie);
        break;
    case BRCMF_VNDR_IE_PRBRSP_FLAG:
        mgmt_ie_buf = saved_ie->probe_res_ie;
        mgmt_ie_len = &saved_ie->probe_res_ie_len;
        mgmt_ie_buf_len = sizeof(saved_ie->probe_res_ie);
        break;
    case BRCMF_VNDR_IE_BEACON_FLAG:
        mgmt_ie_buf = saved_ie->beacon_ie;
        mgmt_ie_len = &saved_ie->beacon_ie_len;
        mgmt_ie_buf_len = sizeof(saved_ie->beacon_ie);
        break;
    case BRCMF_VNDR_IE_ASSOCREQ_FLAG:
        mgmt_ie_buf = saved_ie->assoc_req_ie;
        mgmt_ie_len = &saved_ie->assoc_req_ie_len;
        mgmt_ie_buf_len = sizeof(saved_ie->assoc_req_ie);
        break;
    default:
        err = ZX_ERR_WRONG_TYPE;
        brcmf_err("not suitable type\n");
        goto exit;
    }

    if ((int)vndr_ie_len > mgmt_ie_buf_len) {
        err = ZX_ERR_NO_MEMORY;
        brcmf_err("extra IE size too big\n");
        goto exit;
    }

    /* parse and save new vndr_ie in curr_ie_buff before comparing it */
    if (vndr_ie_buf && vndr_ie_len && curr_ie_buf) {
        ptr = curr_ie_buf;
        brcmf_parse_vndr_ies(vndr_ie_buf, vndr_ie_len, &new_vndr_ies);
        for (i = 0; i < (int32_t)new_vndr_ies.count; i++) {
            vndrie_info = &new_vndr_ies.ie_info[i];
            memcpy(ptr + parsed_ie_buf_len, vndrie_info->ie_ptr, vndrie_info->ie_len);
            parsed_ie_buf_len += vndrie_info->ie_len;
        }
    }

    if (mgmt_ie_buf && *mgmt_ie_len) {
        if (parsed_ie_buf_len && (parsed_ie_buf_len == *mgmt_ie_len) &&
                (memcmp(mgmt_ie_buf, curr_ie_buf, parsed_ie_buf_len) == 0)) {
            brcmf_dbg(TRACE, "Previous mgmt IE equals to current IE\n");
            goto exit;
        }

        /* parse old vndr_ie */
        brcmf_parse_vndr_ies(mgmt_ie_buf, *mgmt_ie_len, &old_vndr_ies);

        /* make a command to delete old ie */
        for (i = 0; i < (int32_t)old_vndr_ies.count; i++) {
            vndrie_info = &old_vndr_ies.ie_info[i];

            brcmf_dbg(TRACE, "DEL ID : %d, Len: %d , OUI:%02x:%02x:%02x\n", vndrie_info->vndrie.id,
                      vndrie_info->vndrie.len, vndrie_info->vndrie.oui[0],
                      vndrie_info->vndrie.oui[1], vndrie_info->vndrie.oui[2]);

            del_add_ie_buf_len = brcmf_vndr_ie(curr_ie_buf, pktflag, vndrie_info->ie_ptr,
                                               vndrie_info->ie_len, (int8_t*)"del");
            curr_ie_buf += del_add_ie_buf_len;
            total_ie_buf_len += del_add_ie_buf_len;
        }
    }

    *mgmt_ie_len = 0;
    /* Add if there is any extra IE */
    if (mgmt_ie_buf && parsed_ie_buf_len) {
        ptr = mgmt_ie_buf;

        remained_buf_len = mgmt_ie_buf_len;

        /* make a command to add new ie */
        for (i = 0; i < (int32_t)new_vndr_ies.count; i++) {
            vndrie_info = &new_vndr_ies.ie_info[i];

            /* verify remained buf size before copy data */
            if (remained_buf_len < (vndrie_info->vndrie.len + VNDR_IE_VSIE_OFFSET)) {
                brcmf_err("no space in mgmt_ie_buf: len left %d", remained_buf_len);
                break;
            }
            remained_buf_len -= (vndrie_info->ie_len + VNDR_IE_VSIE_OFFSET);

            brcmf_dbg(TRACE, "ADDED ID : %d, Len: %d, OUI:%02x:%02x:%02x\n", vndrie_info->vndrie.id,
                      vndrie_info->vndrie.len, vndrie_info->vndrie.oui[0],
                      vndrie_info->vndrie.oui[1], vndrie_info->vndrie.oui[2]);

            del_add_ie_buf_len = brcmf_vndr_ie(curr_ie_buf, pktflag, vndrie_info->ie_ptr,
                                               vndrie_info->ie_len, (int8_t*)"add");

            /* save the parsed IE in wl struct */
            memcpy(ptr + (*mgmt_ie_len), vndrie_info->ie_ptr, vndrie_info->ie_len);
            *mgmt_ie_len += vndrie_info->ie_len;

            curr_ie_buf += del_add_ie_buf_len;
            total_ie_buf_len += del_add_ie_buf_len;
        }
    }
    if (total_ie_buf_len) {
        err = brcmf_fil_bsscfg_data_set(ifp, "vndr_ie", iovar_ie_buf, total_ie_buf_len);
        if (err != ZX_OK) {
            brcmf_err("vndr ie set error : %d\n", err);
        }
    }

exit:
    free(iovar_ie_buf);
    return err;
}

zx_status_t brcmf_vif_clear_mgmt_ies(struct brcmf_cfg80211_vif* vif) {
    int32_t pktflags[] = {
        BRCMF_VNDR_IE_PRBREQ_FLAG,
        BRCMF_VNDR_IE_PRBRSP_FLAG,
        BRCMF_VNDR_IE_BEACON_FLAG
    };
    int i;

    for (i = 0; i < (int)countof(pktflags); i++) {
        brcmf_vif_set_mgmt_ie(vif, pktflags[i], NULL, 0);
    }

    memset(&vif->saved_ie, 0, sizeof(vif->saved_ie));
    return ZX_OK;
}

// Returns an MLME result code (WLAN_START_RESULT_*)
static uint8_t brcmf_cfg80211_start_ap(struct net_device* ndev, wlanif_start_req_t* req) {
    if (req->bss_type != WLAN_BSS_TYPE_INFRASTRUCTURE) {
        brcmf_err("Attempt to start AP in unsupported mode (%d)\n", req->bss_type);
        return WLAN_START_RESULT_NOT_SUPPORTED;
    }
    brcmf_dbg(TRACE, "ssid: %*s  beacon period: %d  dtim_period: %d  channel: %d  rsne_len: %zd",
              req->ssid.len, req->ssid.data, req->beacon_period, req->dtim_period, req->channel,
              req->rsne_len);

    struct brcmf_if* ifp = ndev_to_if(ndev);

    if (ifp->vif->mbss) {
        brcmf_err("Mesh role not yet supported\n");
        return WLAN_START_RESULT_NOT_SUPPORTED;
    }

    struct brcmf_cfg80211_info* cfg = ifp->drvr->config;
    wlan_channel_t channel = {};
    uint16_t chanspec = 0;
    zx_status_t status;

    struct brcmf_ssid_le ssid_le;
    memset(&ssid_le, 0, sizeof(ssid_le));
    memcpy(ssid_le.SSID, req->ssid.data, req->ssid.len);
    ssid_le.SSID_len = req->ssid.len;

    brcmf_set_mpc(ifp, 0);
    brcmf_configure_arp_nd_offload(ifp, false);

    // Configure RSN IE
    if (req->rsne_len != 0) {
        struct brcmf_vs_tlv* tmp_ie = (struct brcmf_vs_tlv*)req->rsne;
        status = brcmf_configure_wpaie(ifp, tmp_ie, true);
        if (status != ZX_OK) {
            brcmf_err("Failed to install RSNE\n");
            goto fail;
        }
    } else {
        brcmf_configure_opensecurity(ifp);
    }

    status = brcmf_fil_cmd_int_set(ifp, BRCMF_C_SET_BCNPRD, req->beacon_period, nullptr);
    if (status != ZX_OK) {
        brcmf_err("Beacon Interval Set Error, %s\n", zx_status_get_string(status));
        goto fail;
    }
    ifp->vif->profile.beacon_period = req->beacon_period;

    status = brcmf_fil_cmd_int_set(ifp, BRCMF_C_SET_DTIMPRD, req->dtim_period, nullptr);
    if (status != ZX_OK) {
        brcmf_err("DTIM Interval Set Error, %s\n", zx_status_get_string(status));
        goto fail;
    }

    status = brcmf_fil_cmd_int_set(ifp, BRCMF_C_DOWN, 1, nullptr);
    if (status != ZX_OK) {
        brcmf_err("BRCMF_C_DOWN error %s\n", zx_status_get_string(status));
        goto fail;
    }

    // Disable simultaneous STA/AP operation, aka Real Simultaneous Dual Band (RSDB)
    brcmf_fil_iovar_int_set(ifp, "apsta", 0, nullptr);

    status = brcmf_fil_cmd_int_set(ifp, BRCMF_C_SET_INFRA, 1, nullptr);
    if (status != ZX_OK) {
        brcmf_err("SET INFRA error %s\n", zx_status_get_string(status));
        goto fail;
    }

    status = brcmf_fil_cmd_int_set(ifp, BRCMF_C_SET_AP, 1, nullptr);
    if (status != ZX_OK) {
        brcmf_err("setting AP mode failed %s\n", zx_status_get_string(status));
        goto fail;
    }

    channel = {.primary = req->channel, .cbw = CBW20, .secondary80 = 0};
    chanspec = channel_to_chanspec(&cfg->d11inf, &channel);
    status = brcmf_fil_iovar_int_set(ifp, "chanspec", chanspec, nullptr);
    if (status != ZX_OK) {
        brcmf_err("Set Channel failed: chspec=%d, status=%s\n", chanspec,
                  zx_status_get_string(status));
        goto fail;
    }

    status = brcmf_fil_cmd_int_set(ifp, BRCMF_C_UP, 1, nullptr);
    if (status != ZX_OK) {
        brcmf_err("BRCMF_C_UP error (%s)\n", zx_status_get_string(status));
        goto fail;
    }

    struct brcmf_join_params join_params;
    memset(&join_params, 0, sizeof(join_params));
    // join parameters starts with ssid
    memcpy(&join_params.ssid_le, &ssid_le, sizeof(ssid_le));
    // create softap
    status = brcmf_fil_cmd_data_set(ifp, BRCMF_C_SET_SSID, &join_params, sizeof(join_params),
                                    nullptr);
    if (status != ZX_OK) {
        brcmf_err("SET SSID error (%s)\n", zx_status_get_string(status));
        goto fail;
    }

    brcmf_dbg(TRACE, "AP mode configuration complete\n");

    brcmf_set_bit_in_array(BRCMF_VIF_STATUS_AP_CREATED, &ifp->vif->sme_state);
    brcmf_net_setcarrier(ifp, true);

    return WLAN_START_RESULT_SUCCESS;

fail:
    brcmf_set_mpc(ifp, 1);
    brcmf_configure_arp_nd_offload(ifp, true);
    return WLAN_START_RESULT_NOT_SUPPORTED;
}

// Returns an MLME result code (WLAN_STOP_RESULT_*)
static uint8_t brcmf_cfg80211_stop_ap(struct net_device* ndev, wlanif_stop_req_t* req) {
    struct brcmf_if* ifp = ndev_to_if(ndev);
    zx_status_t status;
    uint8_t result = WLAN_STOP_RESULT_SUCCESS;
    struct brcmf_join_params join_params;

    if (!brcmf_test_bit_in_array(BRCMF_VIF_STATUS_AP_CREATED, &ifp->vif->sme_state)) {
        brcmf_err("attempt to stop already stopped AP\n");
        return WLAN_STOP_RESULT_BSS_ALREADY_STOPPED;
    }

    memset(&join_params, 0, sizeof(join_params));
    status = brcmf_fil_cmd_data_set(ifp, BRCMF_C_SET_SSID, &join_params, sizeof(join_params),
                                    nullptr);
    if (status != ZX_OK) {
        brcmf_err("SET SSID error (%s)\n", zx_status_get_string(status));
        result = WLAN_STOP_RESULT_INTERNAL_ERROR;
    }

    status = brcmf_fil_cmd_int_set(ifp, BRCMF_C_DOWN, 1, nullptr);
    if (status != ZX_OK) {
        brcmf_err("BRCMF_C_DOWN error (%s)\n", zx_status_get_string(status));
        result = WLAN_STOP_RESULT_INTERNAL_ERROR;
    }

    status = brcmf_fil_cmd_int_set(ifp, BRCMF_C_SET_AP, 0, nullptr);
    if (status != ZX_OK) {
        brcmf_err("setting AP mode failed (%s)\n", zx_status_get_string(status));
        result = WLAN_STOP_RESULT_INTERNAL_ERROR;
    }

    /* Bring device back up so it can be used again */
    status = brcmf_fil_cmd_int_set(ifp, BRCMF_C_UP, 1, nullptr);
    if (status != ZX_OK) {
        brcmf_err("BRCMF_C_UP error (%s)\n", zx_status_get_string(status));
        result = WLAN_STOP_RESULT_INTERNAL_ERROR;
    }

    brcmf_vif_clear_mgmt_ies(ifp->vif);
    brcmf_set_mpc(ifp, 1);
    brcmf_configure_arp_nd_offload(ifp, true);
    brcmf_clear_bit_in_array(BRCMF_VIF_STATUS_AP_CREATED, &ifp->vif->sme_state);
    brcmf_net_setcarrier(ifp, false);

    return result;
}

// Deauthenticate with specified STA. The reason provided should be from WLAN_DEAUTH_REASON_*
static zx_status_t brcmf_cfg80211_del_station(struct net_device* ndev, uint8_t* mac,
                                              uint8_t reason) {
    brcmf_dbg(TRACE, "Enter: reason: %d\n", reason);

    struct brcmf_if* ifp = ndev_to_if(ndev);
    struct brcmf_scb_val_le scbval;
    memset(&scbval, 0, sizeof(scbval));
    memcpy(&scbval.ea, mac, ETH_ALEN);
    scbval.val = reason;
    zx_status_t status = brcmf_fil_cmd_data_set(ifp, BRCMF_C_SCB_DEAUTHENTICATE_FOR_REASON,
                                                &scbval, sizeof(scbval), nullptr);
    if (status != ZX_OK) {
        brcmf_err("SCB_DEAUTHENTICATE_FOR_REASON failed %s\n", zx_status_get_string(status));
    }

    brcmf_dbg(TRACE, "Exit\n");
    return status;
}

static zx_status_t brcmf_notify_tdls_peer_event(struct brcmf_if* ifp,
                                                const struct brcmf_event_msg* e, void* data) {
    switch (e->reason) {
    case BRCMF_E_REASON_TDLS_PEER_DISCOVERED:
        brcmf_dbg(TRACE, "TDLS Peer Discovered\n");
        break;
    case BRCMF_E_REASON_TDLS_PEER_CONNECTED:
        brcmf_dbg(TRACE, "TDLS Peer Connected\n");
        brcmf_proto_add_tdls_peer(ifp->drvr, ifp->ifidx, (uint8_t*)e->addr);
        break;
    case BRCMF_E_REASON_TDLS_PEER_DISCONNECTED:
        brcmf_dbg(TRACE, "TDLS Peer Disconnected\n");
        brcmf_proto_delete_peer(ifp->drvr, ifp->ifidx, (uint8_t*)e->addr);
        break;
    }

    return ZX_OK;
}

#ifdef CONFIG_PM
static zx_status_t brcmf_cfg80211_set_rekey_data(struct wiphy* wiphy, struct net_device* ndev,
                                                 struct cfg80211_gtk_rekey_data* gtk) {
    struct brcmf_if* ifp = ndev_to_if(ndev);
    struct brcmf_gtk_keyinfo_le gtk_le;
    zx_status_t ret;

    brcmf_dbg(TRACE, "Enter, bssidx=%d\n", ifp->bsscfgidx);

    memcpy(gtk_le.kck, gtk->kck, sizeof(gtk_le.kck));
    memcpy(gtk_le.kek, gtk->kek, sizeof(gtk_le.kek));
    memcpy(gtk_le.replay_counter, gtk->replay_ctr, sizeof(gtk_le.replay_counter));

    ret = brcmf_fil_iovar_data_set(ifp, "gtk_key_info", &gtk_le, sizeof(gtk_le), nullptr);
    if (ret != ZX_OK) {
        brcmf_err("gtk_key_info iovar failed: ret=%d\n", ret);
    }

    return ret;
}
#endif

static void brcmf_cfg80211_set_country(struct wiphy* wiphy, const char code[3]) {
    struct brcmf_cfg80211_info* cfg = wiphy_to_cfg(wiphy);
    struct brcmf_if* ifp = cfg_to_if(cfg);
    struct brcmf_fil_country_le ccreq;
    zx_status_t err;

    brcmf_dbg(TRACE, "Enter: code=%c%c\n", code[0], code[1]);

    err = brcmf_fil_iovar_data_get(ifp, "country", &ccreq, sizeof(ccreq), nullptr);
    if (err != ZX_OK) {
        brcmf_err("Country code iovar returned err = %d\n", err);
        return;
    }

    //memset(&ccreq, 0, sizeof(ccreq));
    //ccreq.rev = country_codes->table[found_index].rev;
    brcmf_dbg(TEMP, "ccode 0x%x, abbrev 0x%x, rev %d", *(uint32_t*)ccreq.ccode,
              *(uint32_t*)ccreq.country_abbrev, ccreq.rev);
    ccreq.ccode[0] = code[0];
    ccreq.ccode[1] = code[1];
    ccreq.ccode[2] = 0;
    ccreq.country_abbrev[0] = code[0];
    ccreq.country_abbrev[1] = code[1];
    ccreq.country_abbrev[2] = 0;

    err = brcmf_fil_iovar_data_set(ifp, "country", &ccreq, sizeof(ccreq), nullptr);
    if (err != ZX_OK) {
        brcmf_err("Firmware rejected country setting\n");
        return;
    }
}

static zx_status_t brcmf_if_start(void* ctx, wlanif_impl_ifc_t* ifc, zx_handle_t* out_sme_channel,
        void* cookie) {
    struct net_device* ndev = static_cast<decltype(ndev)>(ctx);

    brcmf_dbg(TRACE, "Enter");
    ndev->if_callbacks = static_cast<decltype(ndev->if_callbacks)>(malloc(sizeof(*ifc)));
    memcpy(ndev->if_callbacks, ifc, sizeof(*ifc));
    ndev->if_callback_cookie = cookie;
    brcmf_netdev_open(ndev);
    ndev->flags = IFF_UP;
    brcmf_cfg80211_set_country(ndev_to_wiphy(ndev), "US");
    return ZX_OK;
}

static void brcmf_if_stop(void* ctx) {
    struct net_device* ndev = static_cast<decltype(ndev)>(ctx);

    brcmf_dbg(TRACE, "Enter");
    free(ndev->if_callbacks);
    ndev->if_callbacks = NULL;
}

void brcmf_hook_start_scan(void* ctx, wlanif_scan_req_t* req) {
    struct net_device* ndev = static_cast<decltype(ndev)>(ctx);
    zx_status_t result;

    brcmf_dbg(TRACE, "Enter");
    if (ndev->scan_busy) {
        brcmf_signal_scan_end(ndev, req->txn_id, WLAN_SCAN_RESULT_INTERNAL_ERROR);
        return;
    }

    ndev->scan_txn_id = req->txn_id;
    ndev->scan_busy = true;

    brcmf_dbg(SCAN, "About to scan! Txn ID %lu", ndev->scan_txn_id);
    result = brcmf_cfg80211_scan(ndev, req);
    if (result != ZX_OK) {
        brcmf_dbg(SCAN, "Couldn't start scan: %d %s", result, zx_status_get_string(result));
        brcmf_signal_scan_end(ndev, req->txn_id, WLAN_SCAN_RESULT_INTERNAL_ERROR);
        ndev->scan_busy = false;
    }
}

// Because brcm's join/assoc is handled in a single operation (BRCMF_C_SET_SSID), we save off the
// bss information, but otherwise wait until an ASSOCIATE.request is received to join so that we
// have the negotiated RSNE.
void brcmf_hook_join_req(void* ctx, wlanif_join_req_t* req) {
    struct net_device* ndev = static_cast<decltype(ndev)>(ctx);
    struct brcmf_if* ifp = ndev_to_if(ndev);

    brcmf_dbg(TRACE, "Enter\n");
    const uint8_t* bssid = req->selected_bss.bssid;
    brcmf_dbg(CONN, "Join requested: ssid %.*s, bssid %02x:%02x:%02x:%02x:%02x:%02x",
              req->selected_bss.ssid.len, req->selected_bss.ssid.data,
              bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]);
    memcpy(&ifp->bss, &req->selected_bss, sizeof(ifp->bss));

    wlanif_join_confirm_t result;
    result.result_code = WLAN_JOIN_RESULT_SUCCESS;
    ndev->if_callbacks->join_conf(ndev->if_callback_cookie, &result);
}

void brcmf_hook_auth_req(void* ctx, wlanif_auth_req_t* req) {
    struct net_device* ndev = static_cast<decltype(ndev)>(ctx);
    struct brcmf_if* ifp = ndev_to_if(ndev);
    wlanif_auth_confirm_t response;

    brcmf_dbg(TRACE, "Enter");
    response.result_code = WLAN_AUTH_RESULT_SUCCESS;
    response.auth_type = req->auth_type;
    // Ensure that join bssid matches auth bssid
    if (memcmp(req->peer_sta_address, ifp->bss.bssid, ETH_ALEN)) {
        const uint8_t* old_mac = ifp->bss.bssid;
        const uint8_t* new_mac = req->peer_sta_address;
        brcmf_err("Auth MAC (%02x:%02x:%02x:%02x:%02x:%02x) != "
                  "join MAC (%02x:%02x:%02x:%02x:%02x:%02x).\n",
                  new_mac[0], new_mac[1], new_mac[2], new_mac[3], new_mac[4], new_mac[5],
                  old_mac[0], old_mac[1], old_mac[2], old_mac[3], old_mac[4], old_mac[5]);

        // In debug builds, we should investigate why the MLME is giving us inconsitent
        // requests.
        ZX_DEBUG_ASSERT(0);

        // In release builds, ignore and continue.
        brcmf_err("Ignoring mismatch and using join MAC address\n");
    }
    memcpy(&response.peer_sta_address, ifp->bss.bssid, ETH_ALEN);
    ndev->if_callbacks->auth_conf(ndev->if_callback_cookie, &response);
}

// In AP mode, receive a response from wlanif confirming that a client was successfully
// authenticated.
void brcmf_hook_auth_resp(void* ctx, wlanif_auth_resp_t* ind) {
    struct net_device* ndev = static_cast<decltype(ndev)>(ctx);
    struct brcmf_if* ifp = ndev_to_if(ndev);

    brcmf_dbg(TRACE, "Enter");

    if (!brcmf_is_apmode(ifp->vif)) {
        brcmf_err("Received AUTHENTICATE.response but not in AP mode - ignoring\n");
        return;
    }

    if (ind->result_code == WLAN_AUTH_RESULT_SUCCESS) {
        const uint8_t* mac = ind->peer_sta_address;
        brcmf_dbg(CONN, "Successfully authenticated client %02x:%02x:%02x:%02x:%02x:%02x\n",
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        return;
    }

    uint8_t reason;
    switch (ind->result_code) {
    case WLAN_AUTH_RESULT_REFUSED:
    case WLAN_AUTH_RESULT_REJECTED:
        reason = WLAN_DEAUTH_REASON_NOT_AUTHENTICATED;
        break;
    case WLAN_AUTH_RESULT_FAILURE_TIMEOUT:
        reason = WLAN_DEAUTH_REASON_TIMEOUT;
        break;
    case WLAN_AUTH_RESULT_ANTI_CLOGGING_TOKEN_REQUIRED:
    case WLAN_AUTH_RESULT_FINITE_CYCLIC_GROUP_NOT_SUPPORTED:
    default:
        reason = WLAN_DEAUTH_REASON_UNSPECIFIED;
        break;
    }
    brcmf_cfg80211_del_station(ndev, ind->peer_sta_address, reason);
}

// Respond to a MLME-DEAUTHENTICATE.request message. Note that we are required to respond with a
// MLME-DEAUTHENTICATE.confirm on completion (or failure), even though there is no status
// reported.
void brcmf_hook_deauth_req(void* ctx, wlanif_deauth_req_t* req) {
    brcmf_dbg(TRACE, "Enter");
    struct net_device* ndev = static_cast<decltype(ndev)>(ctx);
    if (brcmf_cfg80211_disconnect(ndev, req->peer_sta_address, req->reason_code, true) != ZX_OK) {
        // Request to disconnect failed, so respond immediately
        brcmf_notify_deauth(ndev, req->peer_sta_address);
    } // else wait for disconnect to complete before sending response

    // Workaround for NET-1574: allow time for disconnect to complete
    zx_nanosleep(zx_deadline_after(ZX_MSEC(50)));
}

void brcmf_hook_assoc_req(void* ctx, wlanif_assoc_req_t* req) {
    struct net_device* ndev = static_cast<decltype(ndev)>(ctx);
    struct brcmf_if* ifp = ndev_to_if(ndev);

    brcmf_dbg(TRACE, "Enter");
    if (req->rsne_len != 0) {
        brcmf_dbg(TEMP, " * * RSNE non-zero! %ld", req->rsne_len);
        brcmf_dbg_hex_dump(BRCMF_BYTES_ON(), req->rsne, req->rsne_len, "RSNE:\n");
    }
    if (memcmp(req->peer_sta_address, ifp->bss.bssid, ETH_ALEN)) {
        const uint8_t* old_mac = ifp->bss.bssid;
        const uint8_t* new_mac = req->peer_sta_address;
        brcmf_err("Requested MAC %02x:%02x:%02x:%02x:%02x:%02x != "
                  "connected MAC %02x:%02x:%02x:%02x:%02x:%02x\n",
                  new_mac[0], new_mac[1], new_mac[2], new_mac[3], new_mac[4], new_mac[5],
                  old_mac[0], old_mac[1], old_mac[2], old_mac[3], old_mac[4], old_mac[5]);
        brcmf_return_assoc_result(ndev, WLAN_ASSOC_RESULT_REFUSED_REASON_UNSPECIFIED);
    } else {
        brcmf_cfg80211_connect(ndev, req);
    }
}

void brcmf_hook_assoc_resp(void* ctx, wlanif_assoc_resp_t* ind) {
    struct net_device* ndev = static_cast<decltype(ndev)>(ctx);
    struct brcmf_if* ifp = ndev_to_if(ndev);

    brcmf_dbg(TRACE, "Enter");

    if (!brcmf_is_apmode(ifp->vif)) {
        brcmf_err("Received ASSOCIATE.response but not in AP mode - ignoring\n");
        return;
    }

    if (ind->result_code == WLAN_ASSOC_RESULT_SUCCESS) {
        const uint8_t* mac = ind->peer_sta_address;
        brcmf_dbg(CONN, "Successfully associated client %02x:%02x:%02x:%02x:%02x:%02x\n",
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        return;
    }

    uint8_t reason;
    switch(ind->result_code) {
    case WLAN_ASSOC_RESULT_REFUSED_NOT_AUTHENTICATED:
        reason = WLAN_DEAUTH_REASON_NOT_AUTHENTICATED;
        break;
    case WLAN_ASSOC_RESULT_REFUSED_CAPABILITIES_MISMATCH:
        reason = WLAN_DEAUTH_REASON_INVALID_RSNE_CAPABILITIES;
        break;
    case WLAN_ASSOC_RESULT_REFUSED_REASON_UNSPECIFIED:
    case WLAN_ASSOC_RESULT_REFUSED_EXTERNAL_REASON:
    case WLAN_ASSOC_RESULT_REFUSED_AP_OUT_OF_MEMORY:
    case WLAN_ASSOC_RESULT_REFUSED_BASIC_RATES_MISMATCH:
    case WLAN_ASSOC_RESULT_REJECTED_EMERGENCY_SERVICES_NOT_SUPPORTED:
    case WLAN_ASSOC_RESULT_REFUSED_TEMPORARILY:
    default:
        reason = WLAN_DEAUTH_REASON_UNSPECIFIED;
        break;
    }
    brcmf_cfg80211_del_station(ndev, ind->peer_sta_address, reason);
}

void brcmf_hook_disassoc_req(void* ctx, wlanif_disassoc_req_t* req) {
    brcmf_dbg(TRACE, "Enter");
    struct net_device* ndev = static_cast<decltype(ndev)>(ctx);
    zx_status_t status = brcmf_cfg80211_disconnect(ndev, req->peer_sta_address, req->reason_code,
                                                   false);
    if (status != ZX_OK) {
        brcmf_notify_disassoc(ndev, status);
    } // else notification will happen asynchronously
}

void brcmf_hook_reset_req(void* ctx, wlanif_reset_req_t* req) {
    brcmf_dbg(TRACE, "Enter");
    brcmf_err("Unimplemented\n");
}

/* Start AP mode */
void brcmf_hook_start_req(void* ctx, wlanif_start_req_t* req) {
    brcmf_dbg(TRACE, "Enter");
    struct net_device* ndev = static_cast<decltype(ndev)>(ctx);
    uint8_t result_code = brcmf_cfg80211_start_ap(ndev, req);
    wlanif_start_confirm_t result = {.result_code = result_code};
    ndev->if_callbacks->start_conf(ndev->if_callback_cookie, &result);
}

/* Stop AP mode */
void brcmf_hook_stop_req(void* ctx, wlanif_stop_req_t* req) {
    brcmf_dbg(TRACE, "Enter");
    struct net_device* ndev = static_cast<decltype(ndev)>(ctx);

    uint8_t result_code = brcmf_cfg80211_stop_ap(ndev, req);

    wlanif_stop_confirm_t result = {.result_code = result_code};
    ndev->if_callbacks->stop_conf(ndev->if_callback_cookie, &result);
}

void brcmf_hook_set_keys_req(void* ctx, wlanif_set_keys_req_t* req) {
    brcmf_dbg(TRACE, "Enter");
    struct net_device* ndev = static_cast<decltype(ndev)>(ctx);
    struct wiphy* wiphy = ndev_to_wiphy(ndev);
    zx_status_t result;

    // TODO(WLAN-733)
    if (req->num_keys != 1) {
        brcmf_err("Help! num_keys needs to be 1! But it's %ld.", req->num_keys);
        return;
    }
    result = brcmf_cfg80211_add_key(wiphy, ndev, &req->keylist[0]);
}

void brcmf_hook_del_keys_req(void* ctx, wlanif_del_keys_req_t* req) {
    brcmf_dbg(TRACE, "Enter");
    brcmf_err("Unimplemented\n");
}

void brcmf_hook_eapol_req(void* ctx, wlanif_eapol_req_t* req) {
    brcmf_dbg(TRACE, "Enter");
    struct net_device* ndev = static_cast<decltype(ndev)>(ctx);
    wlanif_eapol_confirm_t confirm;
    int packet_length;

    // Ethernet header length + EAPOL PDU length
    packet_length = 2 * ETH_ALEN + sizeof(uint16_t) + req->data_len;
    uint8_t* packet = static_cast<decltype(packet)>(malloc(packet_length));
    if (packet == NULL) {
        confirm.result_code = WLAN_EAPOL_RESULT_TRANSMISSION_FAILURE;
    } else {
        // IEEE Std. 802.3-2015, 3.1.1
        memcpy(packet, req->dst_addr, ETH_ALEN);
        memcpy(packet + ETH_ALEN, req->src_addr, ETH_ALEN);
        *(uint16_t*)(packet + 2 * ETH_ALEN) = EAPOL_ETHERNET_TYPE_UINT16;
        memcpy(packet + 2 * ETH_ALEN + sizeof(uint16_t), req->data, req->data_len);
        ethmac_netbuf_t netbuf;
        netbuf.data_buffer = packet;
        netbuf.data_size = packet_length;
        brcmf_netdev_start_xmit(ndev, &netbuf);
        free(packet);
        confirm.result_code = WLAN_EAPOL_RESULT_SUCCESS;
    }
    zx_nanosleep(zx_deadline_after(ZX_MSEC(5)));
    ndev->if_callbacks->eapol_conf(ndev->if_callback_cookie, &confirm);
}

static void brcmf_get_bwcap(struct brcmf_if *ifp, uint32_t bw_cap[]) {
    // 2.4 GHz
    uint32_t val = WLC_BAND_2G;
    zx_status_t status = brcmf_fil_iovar_int_get(ifp, "bw_cap", &val, nullptr);
    if (status == ZX_OK) {
        bw_cap[WLAN_INFO_BAND_2GHZ] = val;

        // 5 GHz
        val = WLC_BAND_5G;
        status = brcmf_fil_iovar_int_get(ifp, "bw_cap", &val, nullptr);
        if (status == ZX_OK) {
            bw_cap[WLAN_INFO_BAND_5GHZ] = val;
            return;
        }
        brcmf_err("Unable to get bw_cap for 5GHz bands\n");
        return;
    }

    // bw_cap not supported in this version of fw
    brcmf_dbg(INFO, "fallback to mimo_bw_cap info\n");
    uint32_t mimo_bwcap = 0;
    status = brcmf_fil_iovar_int_get(ifp, "mimo_bw_cap", &mimo_bwcap, nullptr);
    if (status != ZX_OK) {
        /* assume 20MHz if firmware does not give a clue */
        mimo_bwcap = WLC_N_BW_20ALL;
    }

    switch (mimo_bwcap) {
    case WLC_N_BW_40ALL:
        bw_cap[WLAN_INFO_BAND_2GHZ] |= WLC_BW_40MHZ_BIT;
        /* fall-thru */
    case WLC_N_BW_20IN2G_40IN5G:
        bw_cap[WLAN_INFO_BAND_5GHZ] |= WLC_BW_40MHZ_BIT;
        /* fall-thru */
    case WLC_N_BW_20ALL:
        bw_cap[WLAN_INFO_BAND_2GHZ] |= WLC_BW_20MHZ_BIT;
        bw_cap[WLAN_INFO_BAND_5GHZ] |= WLC_BW_20MHZ_BIT;
        break;
    default:
        brcmf_err("invalid mimo_bw_cap value\n");
    }
}

static uint16_t brcmf_get_mcs_map(uint32_t nchain, uint16_t supp) {
    uint16_t mcs_map = 0xffff;
    for (uint32_t i = 0; i < nchain; i++) {
        mcs_map = (mcs_map << 2) | supp;
    }

    return mcs_map;
}

static void brcmf_update_ht_cap(struct brcmf_if* ifp, wlanif_band_capabilities_t* band,
                                uint32_t bw_cap[2], uint32_t ldpc_cap, uint32_t nchain,
                                uint32_t max_ampdu_len_exp) {
    zx_status_t status;

    band->ht_supported = true;

    // LDPC Support
    if (ldpc_cap) {
        band->ht_caps.ht_capability_info |= IEEE80211_HT_CAPS_LDPC;
    }

    // Bandwidth-related flags
    if (bw_cap[band->band_id] & WLC_BW_40MHZ_BIT) {
        band->ht_caps.ht_capability_info |= IEEE80211_HT_CAPS_CHAN_WIDTH;
        band->ht_caps.ht_capability_info |= IEEE80211_HT_CAPS_SGI_40;
    }
    band->ht_caps.ht_capability_info |= IEEE80211_HT_CAPS_SGI_20;
    band->ht_caps.ht_capability_info |= IEEE80211_HT_CAPS_DSSS_CCK_40;

    // SM Power Save
    // At present SMPS appears to never be enabled in firmware (see WLAN-1030)
    band->ht_caps.ht_capability_info |= IEEE80211_HT_CAPS_SMPS_DISABLED;

    // Rx STBC
    uint32_t rx_stbc = 0;
    (void)brcmf_fil_iovar_int_get(ifp, "stbc_rx", &rx_stbc, nullptr);
    band->ht_caps.ht_capability_info |= ((rx_stbc & 0x3) << IEEE80211_HT_CAPS_RX_STBC_SHIFT);

    // Tx STBC
    // According to Broadcom, Tx STBC capability should be induced from the value of the
    // "stbc_rx" iovar and not "stbc_tx".
    if (rx_stbc != 0) {
        band->ht_caps.ht_capability_info |= IEEE80211_HT_CAPS_TX_STBC;
    }

    // AMPDU Parameters
    uint32_t ampdu_rx_density = 0;
    status = brcmf_fil_iovar_int_get(ifp, "ampdu_rx_density", &ampdu_rx_density, nullptr);
    if (status != ZX_OK) {
        brcmf_err("Unable to retrieve value for AMPDU Rx density from firmware, using 16 us\n");
        ampdu_rx_density = 7;
    }
    band->ht_caps.ampdu_params |= ((ampdu_rx_density & 0x7) << IEEE80211_AMPDU_DENSITY_SHIFT);
    if (max_ampdu_len_exp > 3) {
        // Cap A-MPDU length at 64K
        max_ampdu_len_exp = 3;
    }
    band->ht_caps.ampdu_params |= (max_ampdu_len_exp << IEEE80211_AMPDU_RX_LEN_SHIFT);

    // Supported MCS Set
    ZX_ASSERT(nchain <= sizeof(band->ht_caps.supported_mcs_set.bytes));
    memset(&band->ht_caps.supported_mcs_set.bytes[0], 0xff, nchain);
}

static void brcmf_update_vht_cap(struct brcmf_if* ifp, wlanif_band_capabilities_t* band,
                                 uint32_t bw_cap[2], uint32_t nchain, uint32_t ldpc_cap,
                                 uint32_t max_ampdu_len_exp) {
    uint16_t mcs_map;

    band->vht_supported = true;

    // Set Max MPDU length to 11454
    // TODO (WLAN-485): Value hardcoded from firmware behavior of the BCM4356 and BCM4359 chips.
    band->vht_caps.vht_capability_info |= (2 << IEEE80211_VHT_CAPS_MAX_MPDU_LEN_SHIFT);

    /* 80MHz is mandatory */
    band->vht_caps.vht_capability_info |= IEEE80211_VHT_CAPS_SGI_80;
    if (bw_cap[band->band_id] & WLC_BW_160MHZ_BIT) {
        band->vht_caps.vht_capability_info |= (1 << IEEE80211_VHT_CAPS_SUPP_CHAN_WIDTH_SHIFT);
        band->vht_caps.vht_capability_info |= IEEE80211_VHT_CAPS_SGI_160;
    }

    if (ldpc_cap) {
        band->vht_caps.vht_capability_info |= IEEE80211_VHT_CAPS_RX_LDPC;
    }

    // Tx STBC
    // TODO (WLAN-485): Value is hardcoded for now
    if (brcmf_feat_is_quirk_enabled(ifp, BRCMF_FEAT_QUIRK_IS_4359)) {
        band->vht_caps.vht_capability_info |= IEEE80211_VHT_CAPS_TX_STBC;
    }

    /* all support 256-QAM */
    mcs_map = brcmf_get_mcs_map(nchain, IEEE80211_VHT_MCS_0_9);
    /* Rx MCS map (B0:15) */
    band->vht_caps.supported_vht_mcs_and_nss_set = (uint64_t)mcs_map;
    /* Tx MCS map (B0:15) */
    band->vht_caps.supported_vht_mcs_and_nss_set |= ((uint64_t)mcs_map << 32);

    /* Beamforming support information */
    uint32_t txbf_bfe_cap = 0;
    uint32_t txbf_bfr_cap = 0;

    // Use the *_cap_hw value when possible, since the reflects the capabilities of the device
    // regardless of current operating mode.
    zx_status_t status;
    status = brcmf_fil_iovar_int_get(ifp, "txbf_bfe_cap_hw", &txbf_bfe_cap, nullptr);
    if (status != ZX_OK) {
        (void)brcmf_fil_iovar_int_get(ifp, "txbf_bfe_cap", &txbf_bfe_cap, nullptr);
    }
    status = brcmf_fil_iovar_int_get(ifp, "txbf_bfr_cap_hw", &txbf_bfr_cap, nullptr);
    if (status != ZX_OK) {
        (void)brcmf_fil_iovar_int_get(ifp, "txbf_bfr_cap", &txbf_bfr_cap, nullptr);
    }

    if (txbf_bfe_cap & BRCMF_TXBF_SU_BFE_CAP) {
        band->vht_caps.vht_capability_info |= IEEE80211_VHT_CAPS_SU_BEAMFORMEE;
    }
    if (txbf_bfe_cap & BRCMF_TXBF_MU_BFE_CAP) {
        band->vht_caps.vht_capability_info |= IEEE80211_VHT_CAPS_MU_BEAMFORMEE;
    }
    if (txbf_bfr_cap & BRCMF_TXBF_SU_BFR_CAP) {
        band->vht_caps.vht_capability_info |= IEEE80211_VHT_CAPS_SU_BEAMFORMER;
    }
    if (txbf_bfr_cap & BRCMF_TXBF_MU_BFR_CAP) {
        band->vht_caps.vht_capability_info |= IEEE80211_VHT_CAPS_MU_BEAMFORMER;
    }

    uint32_t txstreams = 0;
    // txstreams_cap is not supported in all firmware versions, but when it is supported it
    // provides capability info regardless of current operating state.
    status = brcmf_fil_iovar_int_get(ifp, "txstreams_cap", &txstreams, nullptr);
    if (status != ZX_OK) {
        (void)brcmf_fil_iovar_int_get(ifp, "txstreams", &txstreams, nullptr);
    }

    if ((txbf_bfe_cap || txbf_bfr_cap) && (txstreams > 1)) {
        band->vht_caps.vht_capability_info |= (2 << IEEE80211_VHT_CAPS_BEAMFORMEE_STS_SHIFT);
        band->vht_caps.vht_capability_info |= (((txstreams - 1) <<
                                                IEEE80211_VHT_CAPS_SOUND_DIM_SHIFT) &
                                               IEEE80211_VHT_CAPS_SOUND_DIM);
        // Link adapt = Both
        band->vht_caps.vht_capability_info |= (3 << IEEE80211_VHT_CAPS_VHT_LINK_ADAPT_SHIFT);
    }

    // Maximum A-MPDU Length Exponent
    band->vht_caps.vht_capability_info |=((max_ampdu_len_exp & 0x7) <<
                                          IEEE80211_VHT_CAPS_MAX_AMPDU_LEN_SHIFT);

}

static void brcmf_dump_ht_caps(ieee80211_ht_capabilities_t* caps) {
    BRCMF_LOGF(INFO, "brcmfmac:     ht_capability_info: %#x\n", caps->ht_capability_info);
    BRCMF_LOGF(INFO, "brcmfmac:     ampdu_params: %#x\n", caps->ampdu_params);

    char mcs_set_str[countof(caps->supported_mcs_set.bytes) * 5 + 1];
    char* str = mcs_set_str;
    for (unsigned i = 0; i < countof(caps->supported_mcs_set.bytes); i++) {
        str += sprintf(str, "%s0x%02hhx", i > 0 ? " " : "", caps->supported_mcs_set.bytes[i]);
    }

    BRCMF_LOGF(INFO, "brcmfmac:     mcs_set: %s\n", mcs_set_str);
    BRCMF_LOGF(INFO, "brcmfmac:     ht_ext_capabilities: %#x\n", caps->ht_ext_capabilities);
    BRCMF_LOGF(INFO, "brcmfmac:     asel_capabilities: %#x\n", caps->asel_capabilities);
}

static void brcmf_dump_vht_caps(ieee80211_vht_capabilities_t* caps) {
    BRCMF_LOGF(INFO, "brcmfmac:     vht_capability_info: %#x\n", caps->vht_capability_info);
    BRCMF_LOGF(INFO, "brcmfmac:     supported_vht_mcs_and_nss_set: %#" PRIx64 "\n",
               caps->supported_vht_mcs_and_nss_set);
}

static void brcmf_dump_band_caps(wlanif_band_capabilities_t* band) {
    char band_id_str[32];
    switch (band->band_id) {
    case WLAN_INFO_BAND_2GHZ:
        sprintf(band_id_str, "2GHz");
        break;
    case WLAN_INFO_BAND_5GHZ:
        sprintf(band_id_str, "5GHz");
        break;
    default:
        sprintf(band_id_str, "unknown (%d)", band->band_id);
        break;
    }
    BRCMF_LOGF(INFO, "brcmfmac:   band_id: %s\n", band_id_str);

    ZX_ASSERT(band->num_basic_rates <= WLAN_INFO_BAND_INFO_MAX_BASIC_RATES);
    char basic_rates_str[WLAN_INFO_BAND_INFO_MAX_BASIC_RATES * 6 + 1];
    char* str = basic_rates_str;
    for (unsigned i = 0; i < band->num_basic_rates; i++) {
        str += sprintf(str, "%s%d", i > 0 ? " " : "", band->basic_rates[i]);
    }
    BRCMF_LOGF(INFO, "brcmfmac:     basic_rates: %s\n", basic_rates_str);

    BRCMF_LOGF(INFO, "brcmfmac:     base_frequency: %d\n", band->base_frequency);

    ZX_ASSERT(band->num_channels <= WLAN_INFO_CHANNEL_LIST_MAX_CHANNELS);
    char channels_str[WLAN_INFO_CHANNEL_LIST_MAX_CHANNELS * 4 + 1];
    str = channels_str;
    for (unsigned i = 0; i < band->num_channels; i++) {
        str += sprintf(str, "%s%d", i > 0 ? " " : "", band->channels[i]);
    }
    BRCMF_LOGF(INFO, "brcmfmac:     channels: %s\n", channels_str);

    BRCMF_LOGF(INFO, "brcmfmac:     ht_supported: %s\n", band->ht_supported ? "true" : "false");
    if (band->ht_supported) {
        brcmf_dump_ht_caps(&band->ht_caps);
    }

    BRCMF_LOGF(INFO, "brcmfmac:     vht_supported: %s\n", band->vht_supported ? "true" : "false");
    if (band->vht_supported) {
        brcmf_dump_vht_caps(&band->vht_caps);
    }
}

static void brcmf_dump_query_info(wlanif_query_info_t* info) {
    BRCMF_LOGF(INFO, "brcmfmac: Device capabilities as reported to wlanif:\n");
    BRCMF_LOGF(INFO, "brcmfmac:   mac_addr: %02x:%02x:%02x:%02x:%02x:%02x\n",
               info->mac_addr[0], info->mac_addr[1], info->mac_addr[2],
               info->mac_addr[3], info->mac_addr[4], info->mac_addr[5]);
    BRCMF_LOGF(INFO, "brcmfmac:   role(s): %s%s%s\n",
               info->role & WLAN_INFO_MAC_ROLE_CLIENT ? "client " : "",
               info->role & WLAN_INFO_MAC_ROLE_AP ? "ap " : "",
               info->role & WLAN_INFO_MAC_ROLE_MESH ? "mesh " : "");
    BRCMF_LOGF(INFO, "brcmfmac:   feature(s): %s%s\n",
               info->features & WLANIF_FEATURE_DMA ? "DMA " : "",
               info->features & WLANIF_FEATURE_SYNTH ? "SYNTH " : "");
    for (unsigned i = 0; i < info->num_bands; i++) {
        brcmf_dump_band_caps(&info->bands[i]);
    }
}

void brcmf_hook_query(void* ctx, wlanif_query_info_t* info) {
    struct net_device* ndev = static_cast<decltype(ndev)>(ctx);
    struct brcmf_if* ifp = ndev_to_if(ndev);
    struct wireless_dev* wdev = ndev_to_wdev(ndev);
    struct brcmf_cfg80211_info* cfg = ifp->drvr->config;


    struct brcmf_chanspec_list *list = NULL;
    uint32_t nmode = 0;
    uint32_t vhtmode = 0;
    uint32_t rxchain = 0, nchain = 0;
    uint32_t bw_cap[2] = {WLC_BW_20MHZ_BIT, WLC_BW_20MHZ_BIT};
    uint32_t ldpc_cap = 0;
    uint32_t max_ampdu_len_exp = 0;
    zx_status_t status;

    brcmf_dbg(TRACE, "Enter");

    memset(info, 0, sizeof(*info));

    // mac_addr
    memcpy(info->mac_addr, ifp->mac_addr, ETH_ALEN);

    // role
    info->role = wdev->iftype;

    // features
    info->driver_features |= WLAN_INFO_DRIVER_FEATURE_DFS;

    // bands
    uint32_t bandlist[3];
    status = brcmf_fil_cmd_data_get(ifp, BRCMF_C_GET_BANDLIST, &bandlist, sizeof(bandlist),
                                    nullptr);
    if (status != ZX_OK) {
        brcmf_err("could not obtain band info: %s\n", zx_status_get_string(status));
        return;
    }

    wlanif_band_capabilities_t* band_2ghz = NULL;
    wlanif_band_capabilities_t* band_5ghz = NULL;

    /* first entry in bandlist is number of bands */
    info->num_bands = bandlist[0];
    for (unsigned i = 1; i <= info->num_bands && i < countof(bandlist); i++) {
        if (i > countof(info->bands)) {
            brcmf_err("insufficient space in query response for all bands, truncating\n");
            continue;
        }
        wlanif_band_capabilities_t* band = &info->bands[i - 1];
        if (bandlist[i] == WLC_BAND_2G) {
            band->band_id = WLAN_INFO_BAND_2GHZ;
            band->num_basic_rates = min(WLAN_INFO_BAND_INFO_MAX_BASIC_RATES, wl_g_rates_size);
            memcpy(band->basic_rates, wl_g_rates, band->num_basic_rates * sizeof(uint16_t));
            band->base_frequency = 2407;
            band_2ghz = band;
        } else if (bandlist[i] == WLC_BAND_5G) {
            band->band_id = WLAN_INFO_BAND_5GHZ;
            band->num_basic_rates = min(WLAN_INFO_BAND_INFO_MAX_BASIC_RATES, wl_a_rates_size);
            memcpy(band->basic_rates, wl_a_rates, band->num_basic_rates * sizeof(uint16_t));
            band->base_frequency = 5000;
            band_5ghz = band;
        }
    }

    // channels
    uint8_t* pbuf = static_cast<decltype(pbuf)>(calloc(BRCMF_DCMD_MEDLEN, 1));
    if (pbuf == NULL) {
        brcmf_err("unable to allocate memory for channel information\n");
        return;
    }

    status = brcmf_fil_iovar_data_get(ifp, "chanspecs", pbuf, BRCMF_DCMD_MEDLEN, nullptr);
    if (status != ZX_OK) {
        brcmf_err("get chanspecs error (%s)\n", zx_status_get_string(status));
        goto fail_pbuf;
    }
    list = (struct brcmf_chanspec_list*)pbuf;
    for (uint32_t i = 0; i < list->count; i++) {
        struct brcmu_chan ch;
        ch.chspec = list->element[i];
        cfg->d11inf.decchspec(&ch);

        // Find the appropriate band
        wlanif_band_capabilities_t* band = NULL;
        if (ch.band == BRCMU_CHAN_BAND_2G) {
            band = band_2ghz;
        } else if (ch.band == BRCMU_CHAN_BAND_5G) {
            band = band_5ghz;
        } else {
            brcmf_err("unrecognized band for channel %d\n", ch.control_ch_num);
            continue;
        }
        if (band == NULL) {
            continue;
        }

        // Fuchsia's wlan channels are simply the control channel (for now), whereas
        // brcm specifies each channel + bw + sb configuration individually. Until we
        // offer that level of resolution, just filter out duplicates.
        uint32_t j;
        for (j = 0; j < band->num_channels; j++) {
            if (band->channels[j] == ch.control_ch_num) {
                break;
            }
        }
        if (j != band->num_channels) {
            continue;
        }

        if (band->num_channels + 1 >= sizeof(band->channels)) {
            brcmf_err("insufficient space for channel %d, skipping\n", ch.control_ch_num);
            continue;
        }
        band->channels[band->num_channels++] = ch.control_ch_num;
    }

    // Parse HT/VHT information
    nmode = 0;
    vhtmode = 0;
    rxchain = 0;
    nchain = 0;
    (void) brcmf_fil_iovar_int_get(ifp, "vhtmode", &vhtmode, nullptr);
    status = brcmf_fil_iovar_int_get(ifp, "nmode", &nmode, nullptr);
    if (status != ZX_OK) {
        brcmf_err("nmode error (%s)\n", zx_status_get_string(status));
        // VHT requires HT support
        vhtmode = 0;
    } else {
        brcmf_get_bwcap(ifp, bw_cap);
    }
    brcmf_dbg(INFO, "nmode=%d, vhtmode=%d, bw_cap=(%d, %d)\n",
              nmode, vhtmode, bw_cap[WLAN_INFO_BAND_2GHZ],
              bw_cap[WLAN_INFO_BAND_5GHZ]);

    // LDPC support, applies to both HT and VHT
    ldpc_cap = 0;
    (void)brcmf_fil_iovar_int_get(ifp, "ldpc_cap", &ldpc_cap, nullptr);

    // Max AMPDU length
    max_ampdu_len_exp = 0;
    status = brcmf_fil_iovar_int_get(ifp, "ampdu_rx_factor", &max_ampdu_len_exp, nullptr);
    if (status != ZX_OK) {
        brcmf_err("Unable to retrieve value for AMPDU maximum Rx length, using 8191 bytes\n");
    }

    // Rx chains (and streams)
    // The "rxstreams_cap" iovar, when present, indicates the maximum number of Rx streams
    // possible, encoded as one bit per stream (i.e., a value of 0x3 indicates 2 streams/chains).
    if (brcmf_feat_is_quirk_enabled(ifp, BRCMF_FEAT_QUIRK_IS_4359)) {
         // TODO (WLAN-485): The BCM4359 firmware supports rxstreams_cap, but it returns 0x2
         // instead of 0x3, which is incorrect.
         rxchain = 0x3;
    } else {
        // According to Broadcom, rxstreams_cap, when available, is an accurate representation of
        // the number of rx chains.
        status = brcmf_fil_iovar_int_get(ifp, "rxstreams_cap", &rxchain, nullptr);
        if (status != ZX_OK) {
            // TODO (WLAN-485): The rxstreams_cap iovar isn't yet supported in the BCM4356
            // firmware. For now we use a hard-coded value (another option would be to parse the
            // nvram contents ourselves (looking for the value associated with the key "rxchain").
            rxchain = 0x3;
        }
    }

    for (nchain = 0; rxchain; nchain++) {
        rxchain = rxchain & (rxchain - 1);
    }
    brcmf_dbg(INFO, "nchain=%d\n", nchain);

    if (nmode) {
        if (band_2ghz) {
            brcmf_update_ht_cap(ifp, band_2ghz, bw_cap, ldpc_cap, nchain, max_ampdu_len_exp);
        }
        if (band_5ghz) {
            brcmf_update_ht_cap(ifp, band_5ghz, bw_cap, ldpc_cap, nchain, max_ampdu_len_exp);
        }
    }
    if (vhtmode && band_5ghz) {
        brcmf_update_vht_cap(ifp, band_5ghz, bw_cap, nchain, ldpc_cap, max_ampdu_len_exp);
    }

    if (BRCMF_INFO_ON()) {
        brcmf_dump_query_info(info);
    }

fail_pbuf:
    free(pbuf);
}

void brcmf_hook_stats_query_req(void* ctx) {
    struct net_device* ndev = static_cast<decltype(ndev)>(ctx);
    struct wireless_dev* wdev = ndev_to_wdev(ndev);

    brcmf_dbg(TRACE, "Enter");
    wlanif_stats_query_response_t response = {};
    wlanif_mlme_stats_t mlme_stats = {};
    response.stats.mlme_stats = &mlme_stats;

    // TODO(cphoenix): Fill in all the stats fields.
    switch (wdev->iftype) {
    case WLAN_INFO_MAC_ROLE_CLIENT:
        {
            mlme_stats.tag = WLANIF_MLME_STATS_TYPE_CLIENT;
            wlanif_client_mlme_stats_t* stats = &mlme_stats.client_mlme_stats;
            memset(stats, 0, sizeof(*stats));
            break;
        }
    case WLAN_INFO_MAC_ROLE_AP:
        {
            mlme_stats.tag = WLANIF_MLME_STATS_TYPE_AP;
            wlanif_ap_mlme_stats_t* stats = &mlme_stats.ap_mlme_stats;
            memset(stats, 0, sizeof(*stats));
            break;
        }
    default:
        response.stats.mlme_stats = NULL;
        break;
    }


    ndev->if_callbacks->stats_query_resp(ndev->if_callback_cookie, &response);
}

zx_status_t brcmf_hook_data_queue_tx(void* ctx, uint32_t options, ethmac_netbuf_t* netbuf) {
    struct net_device* ndev = static_cast<decltype(ndev)>(ctx);
    //brcmf_dbg(TEMP, "Enter. Options %d 0x%x, len %d", options, options, netbuf->len);
    brcmf_netdev_start_xmit(ndev, netbuf);
    return ZX_OK;
}

void brcmf_hook_start_capture_frames(void* ctx, wlanif_start_capture_frames_req_t* req,
                                     wlanif_start_capture_frames_resp_t* resp) {
    brcmf_err("start_capture_frames not supported\n");
    resp->status = ZX_ERR_NOT_SUPPORTED;
    resp->supported_mgmt_frames = 0;
}

void brcmf_hook_stop_capture_frames(void* ctx) {
    brcmf_err("stop_capture_frames not supported\n");
}

static wlanif_impl_protocol_ops_t if_impl_proto_ops = {
    .start = brcmf_if_start,
    .stop = brcmf_if_stop,
    .start_scan = brcmf_hook_start_scan,
    .join_req = brcmf_hook_join_req,
    .auth_req = brcmf_hook_auth_req,
    .auth_resp = brcmf_hook_auth_resp,
    .deauth_req = brcmf_hook_deauth_req,
    .assoc_req = brcmf_hook_assoc_req,
    .assoc_resp = brcmf_hook_assoc_resp,
    .disassoc_req = brcmf_hook_disassoc_req,
    .reset_req = brcmf_hook_reset_req,
    .start_req = brcmf_hook_start_req,
    .stop_req = brcmf_hook_stop_req,
    .set_keys_req = brcmf_hook_set_keys_req,
    .del_keys_req = brcmf_hook_del_keys_req,
    .eapol_req = brcmf_hook_eapol_req,
    .query = brcmf_hook_query,
    .stats_query_req = brcmf_hook_stats_query_req,
    .data_queue_tx = brcmf_hook_data_queue_tx,
    .start_capture_frames = brcmf_hook_start_capture_frames,
    .stop_capture_frames = brcmf_hook_stop_capture_frames,
};

static void brcmf_release_zx_if_device(void* ctx) {
    // TODO(cphoenix): Implement unbind/release
    // Unbind - remove device from tree
    // Release - dealloc resources
    brcmf_err("* * Need to unload and release all driver structs");
}

static zx_protocol_device_t if_impl_device_ops = {
    .version = DEVICE_OPS_VERSION,
    .release = brcmf_release_zx_if_device,
};

zx_status_t brcmf_phy_create_iface(void* ctx, const wlanphy_impl_create_iface_req_t* req,
                                   uint16_t* out_iface_id) {
    struct brcmf_if* ifp = static_cast<decltype(ifp)>(ctx);
    struct net_device* ndev = ifp->ndev;
    struct wireless_dev* wdev = ndev_to_wdev(ndev);
    zx_status_t result;

    brcmf_dbg(TEMP, "brcmf_phy_create_iface called!");

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "broadcom-wlanif", // TODO(cphoenix): Uniquify this?
        .ctx = ndev,
        .ops = &if_impl_device_ops,
        .proto_id = ZX_PROTOCOL_WLANIF_IMPL,
        .proto_ops = &if_impl_proto_ops,
    };

    struct brcmf_device* device = ifp->drvr->bus_if->dev;
    struct brcmf_bus* bus = device->bus;

    brcmf_dbg(TEMP, "About to add if_dev");
    result = brcmf_bus_device_add(bus, device->phy_zxdev, &args, &device->if_zxdev);
    if (result != ZX_OK) {
        brcmf_err("Failed to device_add: %s", zx_status_get_string(result));
        return result;
    }
    brcmf_dbg(TEMP, "device_add() succeeded. Added iface hooks.");

    *out_iface_id = 42;

    wdev->iftype = req->role;

    /* set appropriate operations */
    ndev->initialized_for_ap = true;

    /* set the mac address & netns */
    memcpy(ndev->dev_addr, ifp->mac_addr, ETH_ALEN);
    ndev->priv_destructor = &brcmf_free_net_device_vif;
    brcmf_dbg(INFO, "%s: Broadcom Dongle Host Driver\n", ndev->name);

    return ZX_OK;
}

zx_status_t brcmf_alloc_vif(struct brcmf_cfg80211_info* cfg, uint16_t type,
                            struct brcmf_cfg80211_vif** vif_out) {
    struct brcmf_cfg80211_vif* vif_walk;
    struct brcmf_cfg80211_vif* vif;
    bool mbss;

    brcmf_dbg(TRACE, "allocating virtual interface (size=%zu)\n", sizeof(*vif));
    vif = static_cast<decltype(vif)>(calloc(1, sizeof(*vif)));
    if (!vif) {
        if (vif_out) {
            *vif_out = NULL;
        }
        return ZX_ERR_NO_MEMORY;
    }

    vif->wdev.wiphy = cfg->wiphy;
    vif->wdev.iftype = type;

    brcmf_init_prof(&vif->profile);

    if (type == WLAN_INFO_MAC_ROLE_AP) {
        mbss = false;
        list_for_every_entry(&cfg->vif_list, vif_walk, struct brcmf_cfg80211_vif, list) {
            if (vif_walk->wdev.iftype == WLAN_INFO_MAC_ROLE_AP) {
                mbss = true;
                break;
            }
        }
        vif->mbss = mbss;
    }

    list_add_tail(&cfg->vif_list, &vif->list);
    if (vif_out) {
        *vif_out = vif;
    }
    return ZX_OK;
}

void brcmf_free_vif(struct brcmf_cfg80211_vif* vif) {
    list_delete(&vif->list);
    free(vif);
}

void brcmf_free_net_device_vif(struct net_device* ndev) {
    struct brcmf_cfg80211_vif* vif = ndev_to_vif(ndev);

    if (vif) {
        brcmf_free_vif(vif);
    }
}

// TODO(cphoenix): Rename and/or refactor this function - it has way too many side effects for a
// function that looks like it just returns info about state.
static bool brcmf_is_linkup(struct brcmf_cfg80211_vif* vif, const struct brcmf_event_msg* e) {
    uint32_t event = e->event_code;
    uint32_t status = e->status;

    //brcmf_dbg(TEMP, "Enter, event %d, status %d, sme_state 0x%lx", event, status,
    //          atomic_load(&vif->sme_state));
    if (vif->profile.use_fwsup == BRCMF_PROFILE_FWSUP_PSK && event == BRCMF_E_PSK_SUP &&
            status == BRCMF_E_STATUS_FWSUP_COMPLETED) {
        brcmf_set_bit_in_array(BRCMF_VIF_STATUS_EAP_SUCCESS, &vif->sme_state);
    }
    if (event == BRCMF_E_SET_SSID && status == BRCMF_E_STATUS_SUCCESS) {
        brcmf_dbg(CONN, "Processing set ssid\n");
        memcpy(vif->profile.bssid, e->addr, ETH_ALEN);
        if (vif->profile.use_fwsup != BRCMF_PROFILE_FWSUP_PSK) {
            //brcmf_dbg(TEMP, "Ret true");
            return true;
        }

        brcmf_set_bit_in_array(BRCMF_VIF_STATUS_ASSOC_SUCCESS, &vif->sme_state);
    }

    if (brcmf_test_bit_in_array(BRCMF_VIF_STATUS_EAP_SUCCESS, &vif->sme_state) &&
            brcmf_test_bit_in_array(BRCMF_VIF_STATUS_ASSOC_SUCCESS, &vif->sme_state)) {
        brcmf_clear_bit_in_array(BRCMF_VIF_STATUS_EAP_SUCCESS, &vif->sme_state);
        brcmf_clear_bit_in_array(BRCMF_VIF_STATUS_ASSOC_SUCCESS, &vif->sme_state);
        //brcmf_dbg(TEMP, "Ret true");
        return true;
    }
    //brcmf_dbg(TEMP, "Ret false");
    return false;
}

static bool brcmf_is_linkdown(const struct brcmf_event_msg* e) {
    uint32_t event = e->event_code;
    uint16_t flags = e->flags;

    if ((event == BRCMF_E_DEAUTH) || (event == BRCMF_E_DEAUTH_IND) ||
            (event == BRCMF_E_DISASSOC_IND) ||
            ((event == BRCMF_E_LINK) && (!(flags & BRCMF_EVENT_MSG_LINK)))) {
        brcmf_dbg(CONN, "Processing link down\n");
        return true;
    }
    return false;
}

static bool brcmf_is_nonetwork(struct brcmf_cfg80211_info* cfg, const struct brcmf_event_msg* e) {
    uint32_t event = e->event_code;
    uint32_t status = e->status;

    if (event == BRCMF_E_LINK && status == BRCMF_E_STATUS_NO_NETWORKS) {
        brcmf_dbg(CONN, "Processing Link %s & no network found\n",
                  e->flags & BRCMF_EVENT_MSG_LINK ? "up" : "down");
        return true;
    }

    if (event == BRCMF_E_SET_SSID && status != BRCMF_E_STATUS_SUCCESS) {
        brcmf_dbg(CONN, "Processing connecting & no network found: %d\n", status);
        return true;
    }

    if (event == BRCMF_E_PSK_SUP && status != BRCMF_E_STATUS_FWSUP_COMPLETED) {
        brcmf_dbg(CONN, "Processing failed supplicant state: %u\n", status);
        return true;
    }

    return false;
}

static void brcmf_clear_assoc_ies(struct brcmf_cfg80211_info* cfg) {
    struct brcmf_cfg80211_connect_info* conn_info = cfg_to_conn(cfg);

    free(conn_info->req_ie);
    conn_info->req_ie = NULL;
    conn_info->req_ie_len = 0;
    free(conn_info->resp_ie);
    conn_info->resp_ie = NULL;
    conn_info->resp_ie_len = 0;
}

static zx_status_t brcmf_get_assoc_ies(struct brcmf_cfg80211_info* cfg, struct brcmf_if* ifp) {
    struct brcmf_cfg80211_assoc_ielen_le* assoc_info;
    struct brcmf_cfg80211_connect_info* conn_info = cfg_to_conn(cfg);
    uint32_t req_len;
    uint32_t resp_len;
    zx_status_t err = ZX_OK;

    brcmf_clear_assoc_ies(cfg);

    err = brcmf_fil_iovar_data_get(ifp, "assoc_info", cfg->extra_buf, WL_ASSOC_INFO_MAX, nullptr);
    if (err != ZX_OK) {
        brcmf_err("could not get assoc info (%d)\n", err);
        return err;
    }
    assoc_info = (struct brcmf_cfg80211_assoc_ielen_le*)cfg->extra_buf;
    req_len = assoc_info->req_len;
    resp_len = assoc_info->resp_len;
    if (req_len) {
        err = brcmf_fil_iovar_data_get(ifp, "assoc_req_ies", cfg->extra_buf, WL_ASSOC_INFO_MAX,
                                       nullptr);
        if (err != ZX_OK) {
            brcmf_err("could not get assoc req (%d)\n", err);
            return err;
        }
        conn_info->req_ie_len = req_len;
        conn_info->req_ie = static_cast<decltype(conn_info->req_ie)>(
            brcmu_alloc_and_copy(cfg->extra_buf, conn_info->req_ie_len));
    } else {
        conn_info->req_ie_len = 0;
        conn_info->req_ie = NULL;
    }
    if (resp_len) {
        err = brcmf_fil_iovar_data_get(ifp, "assoc_resp_ies", cfg->extra_buf, WL_ASSOC_INFO_MAX,
                                       nullptr);
        if (err != ZX_OK) {
            brcmf_err("could not get assoc resp (%d)\n", err);
            return err;
        }
        conn_info->resp_ie_len = resp_len;
        conn_info->resp_ie =
            static_cast<decltype(conn_info->resp_ie)>(
                brcmu_alloc_and_copy(cfg->extra_buf, conn_info->resp_ie_len));
    } else {
        conn_info->resp_ie_len = 0;
        conn_info->resp_ie = NULL;
    }
    brcmf_dbg(CONN, "req len (%d) resp len (%d)\n", conn_info->req_ie_len, conn_info->resp_ie_len);

    return err;
}

static zx_status_t brcmf_bss_connect_done(struct brcmf_cfg80211_info* cfg, struct net_device* ndev,
                                          const struct brcmf_event_msg* e, bool completed) {
    struct brcmf_if* ifp = ndev_to_if(ndev);

    brcmf_dbg(TRACE, "Enter\n");

    if (brcmf_test_and_clear_bit_in_array(BRCMF_VIF_STATUS_CONNECTING, &ifp->vif->sme_state)) {
        if (completed) {
            brcmf_get_assoc_ies(cfg, ifp);
            brcmf_set_bit_in_array(BRCMF_VIF_STATUS_CONNECTED, &ifp->vif->sme_state);
        }
        // Connected bssid is in profile->bssid.
        // connection IEs are in conn_info->req_ie, req_ie_len, resp_ie, resp_ie_len.
        brcmf_dbg(CONN, "Report connect result - connection %s\n",
                  completed ? "succeeded" : "timed out");
        brcmf_return_assoc_result(ndev, completed ? WLAN_ASSOC_RESULT_SUCCESS :
                                        WLAN_ASSOC_RESULT_REFUSED_REASON_UNSPECIFIED);
    }
    brcmf_dbg(TRACE, "Exit\n");
    return ZX_OK;
}

static zx_status_t brcmf_notify_connect_status_ap(struct brcmf_cfg80211_info* cfg,
                                                  struct net_device* ndev,
                                                  const struct brcmf_event_msg* e, void* data) {
    uint32_t event = e->event_code;
    uint32_t reason = e->reason;
    struct brcmf_if* ifp = ndev_to_if(ndev);

    brcmf_dbg(CONN, "event %s (%u), reason %d\n",
              brcmf_fweh_event_name(static_cast<brcmf_fweh_event_code>(event)), event, reason);
    if (event == BRCMF_E_LINK && reason == BRCMF_E_REASON_LINK_BSSCFG_DIS &&
            ndev != cfg_to_ndev(cfg)) {
        brcmf_dbg(CONN, "AP mode link down\n");
        sync_completion_signal(&cfg->vif_disabled);
        return ZX_OK;
    }

    // Client has authenticated
    if ((event == BRCMF_E_AUTH_IND) && (reason == BRCMF_E_STATUS_SUCCESS)) {
        wlanif_auth_ind_t auth_ind_params;
        memset(&auth_ind_params, 0, sizeof(auth_ind_params));
        memcpy(auth_ind_params.peer_sta_address, e->addr, ETH_ALEN);
        // We always authenticate as an open system for WPA
        auth_ind_params.auth_type = WLAN_AUTH_TYPE_OPEN_SYSTEM;
        ndev->if_callbacks->auth_ind(ndev->if_callback_cookie, &auth_ind_params);

    // Client has associated
    } else if (((event == BRCMF_E_ASSOC_IND) || (event == BRCMF_E_REASSOC_IND)) &&
               (reason == BRCMF_E_STATUS_SUCCESS)) {
        if (data == NULL || e->datalen == 0) {
            brcmf_err("Received ASSOC_IND with no IEs\n");
            return ZX_ERR_INVALID_ARGS;
        }

        const struct brcmf_tlv* ssid_ie = brcmf_parse_tlvs(data, e->datalen, WLAN_IE_TYPE_SSID);
        if (ssid_ie == NULL) {
            brcmf_err("Received ASSOC_IND with no SSID IE\n");
            return ZX_ERR_INVALID_ARGS;
        }

        if (ssid_ie->len > WLAN_MAX_SSID_LEN) {
            brcmf_err("Received ASSOC_IND with invalid SSID IE\n");
            return ZX_ERR_INVALID_ARGS;
        }

        const struct brcmf_tlv* rsn_ie = brcmf_parse_tlvs(data, e->datalen, WLAN_IE_TYPE_RSNE);
        if (rsn_ie && rsn_ie->len > WLAN_RSNE_MAX_LEN) {
            brcmf_err("Received ASSOC_IND with invalid RSN IE\n");
            return ZX_ERR_INVALID_ARGS;
        }

        wlanif_assoc_ind_t assoc_ind_params;
        memset(&assoc_ind_params, 0, sizeof(assoc_ind_params));
        memcpy(assoc_ind_params.peer_sta_address, e->addr, ETH_ALEN);

        // Unfortunately, we have to ask the firmware to provide the associated station's
        // listen interval.
        struct brcmf_sta_info_le sta_info;
        uint8_t mac[ETH_ALEN];
        memcpy(mac, e->addr, ETH_ALEN);
        brcmf_cfg80211_get_station(ndev, mac, &sta_info);
        // convert from ms to beacon periods
        assoc_ind_params.listen_interval = sta_info.listen_interval_inms /
                                           ifp->vif->profile.beacon_period;

        // Extract the SSID from the IEs
        assoc_ind_params.ssid.len = ssid_ie->len;
        memcpy(assoc_ind_params.ssid.data, ssid_ie->data, ssid_ie->len);

        // Extract the RSN information from the IEs
        if (rsn_ie != NULL) {
            assoc_ind_params.rsne_len = rsn_ie->len + TLV_HDR_LEN;
            memcpy(assoc_ind_params.rsne, rsn_ie, assoc_ind_params.rsne_len);
        }

        ndev->if_callbacks->assoc_ind(ndev->if_callback_cookie, &assoc_ind_params);

    // Client has disassociated
    } else if (event == BRCMF_E_DISASSOC_IND) {
        wlanif_disassoc_indication_t disassoc_ind_params;
        memset(&disassoc_ind_params, 0, sizeof(disassoc_ind_params));
        memcpy(disassoc_ind_params.peer_sta_address, e->addr, ETH_ALEN);
        disassoc_ind_params.reason_code = e->reason;
        ndev->if_callbacks->disassoc_ind(ndev->if_callback_cookie, &disassoc_ind_params);

    // Client has deauthenticated
    } else if ((event == BRCMF_E_DEAUTH_IND) || (event == BRCMF_E_DEAUTH)) {
        wlanif_deauth_indication_t deauth_ind_params;
        memset(&deauth_ind_params, 0, sizeof(deauth_ind_params));
        memcpy(deauth_ind_params.peer_sta_address, e->addr, ETH_ALEN);
        deauth_ind_params.reason_code = e->reason;
        ndev->if_callbacks->deauth_ind(ndev->if_callback_cookie, &deauth_ind_params);
    }
    return ZX_OK;
}

static zx_status_t brcmf_notify_connect_status(struct brcmf_if* ifp,
                                               const struct brcmf_event_msg* e, void* data) {
    struct brcmf_cfg80211_info* cfg = ifp->drvr->config;
    struct net_device* ndev = ifp->ndev;
    zx_status_t err = ZX_OK;

    brcmf_dbg(TRACE, "Enter\n");
    brcmf_dbg(CONN, "Event code %d, status %d", e->event_code, e->status);
    if ((e->event_code == BRCMF_E_DEAUTH) || (e->event_code == BRCMF_E_DEAUTH_IND) ||
            (e->event_code == BRCMF_E_DISASSOC_IND) ||
            ((e->event_code == BRCMF_E_LINK) && (!e->flags))) {
        brcmf_proto_delete_peer(ifp->drvr, ifp->ifidx, (uint8_t*)e->addr);
    }

    if (brcmf_is_apmode(ifp->vif)) {
        err = brcmf_notify_connect_status_ap(cfg, ndev, e, data);
    } else if (brcmf_is_linkup(ifp->vif, e)) {
        brcmf_dbg(CONN, "Linkup\n");
        brcmf_bss_connect_done(cfg, ndev, e, true);
        brcmf_net_setcarrier(ifp, true);
    } else if (brcmf_is_linkdown(e)) {
        brcmf_dbg(CONN, "Linkdown\n");
        brcmf_bss_connect_done(cfg, ndev, e, false);
        brcmf_disconnect_done(cfg);
        brcmf_link_down(ifp->vif, brcmf_map_fw_linkdown_reason(e));
        brcmf_init_prof(ndev_to_prof(ndev));
        if (ndev != cfg_to_ndev(cfg)) {
            sync_completion_signal(&cfg->vif_disabled);
        }
        brcmf_net_setcarrier(ifp, false);
    } else if (brcmf_is_nonetwork(cfg, e)) {
        brcmf_dbg(CONN, "No network\n");
        brcmf_bss_connect_done(cfg, ndev, e, false);
        brcmf_disconnect_done(cfg);
    }

    brcmf_dbg(TRACE, "Exit\n");
    return err;
}

static zx_status_t brcmf_notify_roaming_status(struct brcmf_if* ifp,
                                               const struct brcmf_event_msg* e, void* data) {
    struct brcmf_cfg80211_info* cfg = ifp->drvr->config;
    uint32_t event = e->event_code;
    uint32_t status = e->status;

    if (event == BRCMF_E_ROAM && status == BRCMF_E_STATUS_SUCCESS) {
        if (brcmf_test_bit_in_array(BRCMF_VIF_STATUS_CONNECTED, &ifp->vif->sme_state)) {
            brcmf_err("Received roaming notification - unsupported\n");
        } else {
            brcmf_bss_connect_done(cfg, ifp->ndev, e, true);
            brcmf_net_setcarrier(ifp, true);
        }
    }

    return ZX_OK;
}

static zx_status_t brcmf_notify_mic_status(struct brcmf_if* ifp, const struct brcmf_event_msg* e,
                                           void* data) {
    uint16_t flags = e->flags;
    enum nl80211_key_type key_type;

    if (flags & BRCMF_EVENT_MSG_GROUP) {
        key_type = NL80211_KEYTYPE_GROUP;
    } else {
        key_type = NL80211_KEYTYPE_PAIRWISE;
    }

    cfg80211_michael_mic_failure(ifp->ndev, (uint8_t*)&e->addr, key_type, -1, NULL);

    return ZX_OK;
}

static zx_status_t brcmf_notify_vif_event(struct brcmf_if* ifp, const struct brcmf_event_msg* e,
                                          void* data) {
    struct brcmf_cfg80211_info* cfg = ifp->drvr->config;
    struct brcmf_if_event* ifevent = (struct brcmf_if_event*)data;
    struct brcmf_cfg80211_vif_event* event = &cfg->vif_event;
    struct brcmf_cfg80211_vif* vif;

    brcmf_dbg(TRACE, "Enter: action %u flags %u ifidx %u bsscfgidx %u\n", ifevent->action,
              ifevent->flags, ifevent->ifidx, ifevent->bsscfgidx);

    mtx_lock(&event->vif_event_lock);
    event->action = ifevent->action;
    vif = event->vif;

    switch (ifevent->action) {
    case BRCMF_E_IF_ADD:
        /* waiting process may have timed out */
        if (!cfg->vif_event.vif) {
            mtx_unlock(&event->vif_event_lock);
            return ZX_ERR_SHOULD_WAIT;
        }

        ifp->vif = vif;
        vif->ifp = ifp;
        if (ifp->ndev) {
            vif->wdev.netdev = ifp->ndev;
            ifp->ndev->ieee80211_ptr = &vif->wdev;
        }
        mtx_unlock(&event->vif_event_lock);
        if (event->action == cfg->vif_event_pending_action) {
            sync_completion_signal(&event->vif_event_wait);
        }
        return ZX_OK;

    case BRCMF_E_IF_DEL:
        mtx_unlock(&event->vif_event_lock);
        /* event may not be upon user request */
        if (brcmf_cfg80211_vif_event_armed(cfg) && event->action == cfg->vif_event_pending_action) {
            sync_completion_signal(&event->vif_event_wait);
        }
        return ZX_OK;

    case BRCMF_E_IF_CHANGE:
        mtx_unlock(&event->vif_event_lock);
        if (event->action == cfg->vif_event_pending_action) {
            sync_completion_signal(&event->vif_event_wait);
        }
        return ZX_OK;

    default:
        mtx_unlock(&event->vif_event_lock);
        break;
    }
    return ZX_ERR_INVALID_ARGS;
}

static void brcmf_init_conf(struct brcmf_cfg80211_conf* conf) {
    conf->frag_threshold = (uint32_t)-1;
    conf->rts_threshold = (uint32_t)-1;
    conf->retry_short = (uint32_t)-1;
    conf->retry_long = (uint32_t)-1;
}

static void brcmf_register_event_handlers(struct brcmf_cfg80211_info* cfg) {
    brcmf_fweh_register(cfg->pub, BRCMF_E_LINK, brcmf_notify_connect_status);
    brcmf_fweh_register(cfg->pub, BRCMF_E_AUTH_IND, brcmf_notify_connect_status);
    brcmf_fweh_register(cfg->pub, BRCMF_E_DEAUTH_IND, brcmf_notify_connect_status);
    brcmf_fweh_register(cfg->pub, BRCMF_E_DEAUTH, brcmf_notify_connect_status);
    brcmf_fweh_register(cfg->pub, BRCMF_E_DISASSOC_IND, brcmf_notify_connect_status);
    brcmf_fweh_register(cfg->pub, BRCMF_E_ASSOC_IND, brcmf_notify_connect_status);
    brcmf_fweh_register(cfg->pub, BRCMF_E_REASSOC_IND, brcmf_notify_connect_status);
    brcmf_fweh_register(cfg->pub, BRCMF_E_ROAM, brcmf_notify_roaming_status);
    brcmf_fweh_register(cfg->pub, BRCMF_E_MIC_ERROR, brcmf_notify_mic_status);
    brcmf_fweh_register(cfg->pub, BRCMF_E_SET_SSID, brcmf_notify_connect_status);
    brcmf_fweh_register(cfg->pub, BRCMF_E_PFN_NET_FOUND, brcmf_notify_sched_scan_results);
    brcmf_fweh_register(cfg->pub, BRCMF_E_IF, brcmf_notify_vif_event);
    brcmf_fweh_register(cfg->pub, BRCMF_E_PSK_SUP, brcmf_notify_connect_status);
}

static void brcmf_deinit_priv_mem(struct brcmf_cfg80211_info* cfg) {
    free(cfg->conf);
    cfg->conf = NULL;
    free(cfg->extra_buf);
    cfg->extra_buf = NULL;
    free(cfg->wowl.nd);
    cfg->wowl.nd = NULL;
    free(cfg->wowl.nd_info);
    cfg->wowl.nd_info = NULL;
    free(cfg->escan_info.escan_buf);
    cfg->escan_info.escan_buf = NULL;
}

static zx_status_t brcmf_init_priv_mem(struct brcmf_cfg80211_info* cfg) {
    cfg->conf = static_cast<decltype(cfg->conf)>(calloc(1, sizeof(*cfg->conf)));
    if (!cfg->conf) {
        goto init_priv_mem_out;
    }
    cfg->extra_buf = static_cast<decltype(cfg->extra_buf)>(calloc(1, WL_EXTRA_BUF_MAX));
    if (!cfg->extra_buf) {
        goto init_priv_mem_out;
    }
    cfg->wowl.nd =
        static_cast<decltype(cfg->wowl.nd)>(calloc(1, sizeof(*cfg->wowl.nd) + sizeof(uint32_t)));
    if (!cfg->wowl.nd) {
        goto init_priv_mem_out;
    }
    cfg->wowl.nd_info =
        static_cast<decltype(cfg->wowl.nd_info)>(
            calloc(1, sizeof(*cfg->wowl.nd_info) + sizeof(struct cfg80211_wowlan_nd_match*)));
    if (!cfg->wowl.nd_info) {
        goto init_priv_mem_out;
    }
    cfg->escan_info.escan_buf =
        static_cast<decltype(cfg->escan_info.escan_buf)>(calloc(1, BRCMF_ESCAN_BUF_SIZE));
    if (!cfg->escan_info.escan_buf) {
        goto init_priv_mem_out;
    }

    return ZX_OK;

init_priv_mem_out:
    brcmf_deinit_priv_mem(cfg);

    return ZX_ERR_NO_MEMORY;
}

static zx_status_t wl_init_priv(struct brcmf_cfg80211_info* cfg) {
    zx_status_t err = ZX_OK;

    cfg->scan_request = NULL;
    cfg->pwr_save = true;
    cfg->dongle_up = false; /* dongle is not up yet */
    err = brcmf_init_priv_mem(cfg);
    if (err != ZX_OK) {
        return err;
    }
    brcmf_register_event_handlers(cfg);
    mtx_init(&cfg->usr_sync, mtx_plain);
    brcmf_init_escan(cfg);
    brcmf_init_conf(cfg->conf);
    workqueue_init_work(&cfg->disconnect_timeout_work, brcmf_disconnect_timeout_worker);
    cfg->vif_disabled = {};
    return err;
}

static void wl_deinit_priv(struct brcmf_cfg80211_info* cfg) {
    cfg->dongle_up = false; /* dongle down */
    brcmf_abort_scanning(cfg);
    brcmf_deinit_priv_mem(cfg);
}

static void init_vif_event(struct brcmf_cfg80211_vif_event* event) {
    event->vif_event_wait = {};
    mtx_init(&event->vif_event_lock, mtx_plain);
}

static zx_status_t brcmf_dongle_roam(struct brcmf_if* ifp) {
    zx_status_t err;
    uint32_t bcn_timeout;
    uint32_t roamtrigger[2];
    uint32_t roam_delta[2];

    if (brcmf_feat_is_quirk_enabled(ifp, BRCMF_FEAT_QUIRK_IS_4359)) {
        return ZX_OK; // TODO(WLAN-733) Find out why, and document.
    }
    /* Configure beacon timeout value based upon roaming setting */
    if (ifp->drvr->settings->roamoff) {
        bcn_timeout = BRCMF_DEFAULT_BCN_TIMEOUT_ROAM_OFF;
    } else {
        bcn_timeout = BRCMF_DEFAULT_BCN_TIMEOUT_ROAM_ON;
    }
    err = brcmf_fil_iovar_int_set(ifp, "bcn_timeout", bcn_timeout, nullptr);
    if (err != ZX_OK) {
        brcmf_err("bcn_timeout error (%d)\n", err);
        goto roam_setup_done;
    }

    /* Enable/Disable built-in roaming to allow supplicant to take care of
     * roaming.
     */
    brcmf_dbg(INFO, "Internal Roaming = %s\n", ifp->drvr->settings->roamoff ? "Off" : "On");
    err = brcmf_fil_iovar_int_set(ifp, "roam_off", ifp->drvr->settings->roamoff, nullptr);
    if (err != ZX_OK) {
        brcmf_err("roam_off error (%d)\n", err);
        goto roam_setup_done;
    }

    roamtrigger[0] = WL_ROAM_TRIGGER_LEVEL;
    roamtrigger[1] = BRCM_BAND_ALL;
    err = brcmf_fil_cmd_data_set(ifp, BRCMF_C_SET_ROAM_TRIGGER, (void*)roamtrigger,
                                 sizeof(roamtrigger), nullptr);
    if (err != ZX_OK) {
        brcmf_err("WLC_SET_ROAM_TRIGGER error (%d)\n", err);
        goto roam_setup_done;
    }

    roam_delta[0] = WL_ROAM_DELTA;
    roam_delta[1] = BRCM_BAND_ALL;
    err =
        brcmf_fil_cmd_data_set(ifp, BRCMF_C_SET_ROAM_DELTA, (void*)roam_delta, sizeof(roam_delta),
                               nullptr);
    if (err != ZX_OK) {
        brcmf_err("WLC_SET_ROAM_DELTA error (%d)\n", err);
        goto roam_setup_done;
    }

roam_setup_done:
    return err;
}

static zx_status_t brcmf_dongle_scantime(struct brcmf_if* ifp) {
    zx_status_t err = ZX_OK;

    err = brcmf_fil_cmd_int_set(ifp, BRCMF_C_SET_SCAN_CHANNEL_TIME, BRCMF_SCAN_CHANNEL_TIME,
                                nullptr);
    if (err != ZX_OK) {
        brcmf_err("Scan assoc time error (%d)\n", err);
        goto dongle_scantime_out;
    }
    err = brcmf_fil_cmd_int_set(ifp, BRCMF_C_SET_SCAN_UNASSOC_TIME, BRCMF_SCAN_UNASSOC_TIME,
                                nullptr);
    if (err != ZX_OK) {
        brcmf_err("Scan unassoc time error (%d)\n", err);
        goto dongle_scantime_out;
    }

    err = brcmf_fil_cmd_int_set(ifp, BRCMF_C_SET_SCAN_PASSIVE_TIME, BRCMF_SCAN_PASSIVE_TIME,
                                nullptr);
    if (err != ZX_OK) {
        brcmf_err("Scan passive time error (%d)\n", err);
        goto dongle_scantime_out;
    }

dongle_scantime_out:
    return err;
}

static zx_status_t brcmf_enable_bw40_2g(struct brcmf_cfg80211_info* cfg) {
    struct brcmf_if* ifp = cfg_to_if(cfg);
    struct brcmf_fil_bwcap_le band_bwcap;
    uint32_t val;
    zx_status_t err;

    /* verify support for bw_cap command */
    val = WLC_BAND_5G;
    err = brcmf_fil_iovar_int_get(ifp, "bw_cap", &val, nullptr);

    if (err == ZX_OK) {
        /* only set 2G bandwidth using bw_cap command */
        band_bwcap.band = WLC_BAND_2G;
        band_bwcap.bw_cap = WLC_BW_CAP_40MHZ;
        err = brcmf_fil_iovar_data_set(ifp, "bw_cap", &band_bwcap, sizeof(band_bwcap), nullptr);
    } else {
        brcmf_dbg(INFO, "fallback to mimo_bw_cap\n");
        val = WLC_N_BW_40ALL;
        err = brcmf_fil_iovar_int_set(ifp, "mimo_bw_cap", val, nullptr);
    }

    return err;
}

static zx_status_t brcmf_config_dongle(struct brcmf_cfg80211_info* cfg) {
    struct net_device* ndev;
    struct wireless_dev* wdev;
    struct brcmf_if* ifp;
    int32_t power_mode;
    zx_status_t err = ZX_OK;

    brcmf_dbg(TEMP, "Enter");
    if (cfg->dongle_up) {
        brcmf_dbg(TEMP, "Early done");
        return err;
    }

    ndev = cfg_to_ndev(cfg);
    wdev = ndev->ieee80211_ptr;
    ifp = ndev_to_if(ndev);

    /* make sure RF is ready for work */
    brcmf_fil_cmd_int_set(ifp, BRCMF_C_UP, 0, nullptr);

    brcmf_dongle_scantime(ifp);

    power_mode = cfg->pwr_save ? PM_FAST : PM_OFF;
    err = brcmf_fil_cmd_int_set(ifp, BRCMF_C_SET_PM, power_mode, nullptr);
    if (err != ZX_OK) {
        goto default_conf_out;
    }
    brcmf_dbg(INFO, "power save set to %s\n", (power_mode ? "enabled" : "disabled"));

    err = brcmf_dongle_roam(ifp);
    if (err != ZX_OK) {
        goto default_conf_out;
    }
    err = brcmf_cfg80211_change_iface(wdev->wiphy, ndev, wdev->iftype, NULL);
    if (err != ZX_OK) {
        goto default_conf_out;
    }

    brcmf_configure_arp_nd_offload(ifp, true);

    cfg->dongle_up = true;
default_conf_out:
    brcmf_dbg(TEMP, "Returning %d", err);

    return err;
}

static zx_status_t __brcmf_cfg80211_up(struct brcmf_if* ifp) {
    brcmf_set_bit_in_array(BRCMF_VIF_STATUS_READY, &ifp->vif->sme_state);

    return brcmf_config_dongle(ifp->drvr->config);
}

static zx_status_t __brcmf_cfg80211_down(struct brcmf_if* ifp) {
    struct brcmf_cfg80211_info* cfg = ifp->drvr->config;

    /*
     * While going down, if associated with AP disassociate
     * from AP to save power
     */
    if (check_vif_up(ifp->vif)) {
        brcmf_link_down(ifp->vif, WLAN_DEAUTH_REASON_UNSPECIFIED);

        /* Make sure WPA_Supplicant receives all the event
           generated due to DISASSOC call to the fw to keep
           the state fw and WPA_Supplicant state consistent
         */
        msleep(500);
    }

    brcmf_abort_scanning(cfg);
    brcmf_clear_bit_in_array(BRCMF_VIF_STATUS_READY, &ifp->vif->sme_state);

    return ZX_OK;
}

zx_status_t brcmf_cfg80211_up(struct net_device* ndev) {
    struct brcmf_if* ifp = ndev_to_if(ndev);
    struct brcmf_cfg80211_info* cfg = ifp->drvr->config;
    zx_status_t err = ZX_OK;

    mtx_lock(&cfg->usr_sync);
    err = __brcmf_cfg80211_up(ifp);
    mtx_unlock(&cfg->usr_sync);

    return err;
}

zx_status_t brcmf_cfg80211_down(struct net_device* ndev) {
    struct brcmf_if* ifp = ndev_to_if(ndev);
    struct brcmf_cfg80211_info* cfg = ifp->drvr->config;
    zx_status_t err = ZX_OK;

    mtx_lock(&cfg->usr_sync);
    err = __brcmf_cfg80211_down(ifp);
    mtx_unlock(&cfg->usr_sync);

    return err;
}

uint16_t brcmf_cfg80211_get_iftype(struct brcmf_if* ifp) {
    struct wireless_dev* wdev = &ifp->vif->wdev;

    return wdev->iftype;
}

bool brcmf_get_vif_state_any(struct brcmf_cfg80211_info* cfg, unsigned long state) {
    struct brcmf_cfg80211_vif* vif;

    list_for_every_entry(&cfg->vif_list, vif, struct brcmf_cfg80211_vif, list) {
        if (brcmf_test_bit_in_array(state, &vif->sme_state)) {
            return true;
        }
    }
    return false;
}

void brcmf_cfg80211_arm_vif_event(struct brcmf_cfg80211_info* cfg, struct brcmf_cfg80211_vif* vif,
                                  uint8_t pending_action) {
    struct brcmf_cfg80211_vif_event* event = &cfg->vif_event;

    mtx_lock(&event->vif_event_lock);
    event->vif = vif;
    event->action = 0;
    sync_completion_reset(&event->vif_event_wait);
    cfg->vif_event_pending_action = pending_action;
    mtx_unlock(&event->vif_event_lock);
}

void brcmf_cfg80211_disarm_vif_event(struct brcmf_cfg80211_info* cfg) {
    struct brcmf_cfg80211_vif_event* event = &cfg->vif_event;

    mtx_lock(&event->vif_event_lock);
    event->vif = NULL;
    event->action = 0;
    mtx_unlock(&event->vif_event_lock);
}

bool brcmf_cfg80211_vif_event_armed(struct brcmf_cfg80211_info* cfg) {
    struct brcmf_cfg80211_vif_event* event = &cfg->vif_event;
    bool armed;

    mtx_lock(&event->vif_event_lock);
    armed = event->vif != NULL;
    mtx_unlock(&event->vif_event_lock);

    return armed;
}

zx_status_t brcmf_cfg80211_wait_vif_event(struct brcmf_cfg80211_info* cfg, zx_duration_t timeout) {
    struct brcmf_cfg80211_vif_event* event = &cfg->vif_event;

    return sync_completion_wait(&event->vif_event_wait, timeout);
}

#if 0 // NEEDS PORTING
static zx_status_t brcmf_translate_country_code(struct brcmf_pub* drvr, char alpha2[2],
                                                struct brcmf_fil_country_le* ccreq) {
    struct brcmfmac_pd_cc* country_codes;
    struct brcmfmac_pd_cc_entry* cc;
    int32_t found_index;
    int i;

    country_codes = drvr->settings->country_codes;
    if (!country_codes) {
        brcmf_dbg(TRACE, "No country codes configured for device\n");
        return ZX_ERR_INVALID_ARGS;
    }

    if ((alpha2[0] == ccreq->country_abbrev[0]) && (alpha2[1] == ccreq->country_abbrev[1])) {
        brcmf_dbg(TRACE, "Country code already set\n");
        return ZX_ERR_ALREADY_EXISTS;
    }

    found_index = -1;
    for (i = 0; i < country_codes->table_size; i++) {
        cc = &country_codes->table[i];
        if ((cc->iso3166[0] == '\0') && (found_index == -1)) {
            found_index = i;
        }
        if ((cc->iso3166[0] == alpha2[0]) && (cc->iso3166[1] == alpha2[1])) {
            found_index = i;
            break;
        }
    }
    if (found_index == -1) {
        brcmf_dbg(TRACE, "No country code match found\n");
        return ZX_ERR_INVALID_ARGS;
    }
    memset(ccreq, 0, sizeof(*ccreq));
    ccreq->rev = country_codes->table[found_index].rev;
    memcpy(ccreq->ccode, country_codes->table[found_index].cc, BRCMF_COUNTRY_BUF_SZ);
    ccreq->country_abbrev[0] = alpha2[0];
    ccreq->country_abbrev[1] = alpha2[1];
    ccreq->country_abbrev[2] = 0;

    return ZX_OK;
}
#endif

static void brcmf_free_wiphy(struct wiphy* wiphy) {
    if (!wiphy) {
        return;
    }

#if IS_ENABLED(CONFIG_PM)
    if (wiphy->wowlan != &brcmf_wowlan_support) {
        free(wiphy->wowlan);
    }
#endif
    free(wiphy_to_cfg(wiphy));
    free(wiphy);
}

struct brcmf_cfg80211_info* brcmf_cfg80211_attach(struct brcmf_pub* drvr,
                                                  struct brcmf_device* busdev,
                                                  bool p2pdev_forced) {
    struct net_device* ndev = brcmf_get_ifp(drvr, 0)->ndev;
    struct brcmf_cfg80211_info* cfg;
    struct wiphy* wiphy;
    struct brcmf_cfg80211_vif* vif;
    struct brcmf_if* ifp;
    zx_status_t err = ZX_OK;
    int32_t io_type;

    brcmf_dbg(TEMP, "Enter");
    if (!ndev) {
        brcmf_err("ndev is invalid\n");
        return NULL;
    }

    ifp = ndev_to_if(ndev);
    wiphy = static_cast<decltype(wiphy)>(calloc(1, sizeof(struct wiphy)));
    if (!wiphy) {
        brcmf_err("Could not allocate wiphy device\n");
        return NULL;
    }
    wiphy->cfg80211_info =
        static_cast<decltype(wiphy->cfg80211_info)>(calloc(1, sizeof(struct brcmf_cfg80211_info)));
    if (wiphy->cfg80211_info == NULL) {
        goto wiphy_out;
    }
    memcpy(wiphy->perm_addr, drvr->mac, ETH_ALEN);
    wiphy->dev = busdev;

    cfg = wiphy_to_cfg(wiphy);
    cfg->wiphy = wiphy;
    cfg->pub = drvr;
    init_vif_event(&cfg->vif_event);
    list_initialize(&cfg->vif_list);

    err = brcmf_alloc_vif(cfg, WLAN_INFO_MAC_ROLE_CLIENT, &vif);
    if (err != ZX_OK) {
        goto wiphy_out;
    }

    vif->ifp = ifp;
    vif->wdev.netdev = ndev;
    ndev->ieee80211_ptr = &vif->wdev;

    err = wl_init_priv(cfg);
    if (err != ZX_OK) {
        brcmf_err("Failed to init iwm_priv (%d)\n", err);
        brcmf_free_vif(vif);
        goto wiphy_out;
    }
    ifp->vif = vif;

    /* determine d11 io type before wiphy setup */
    err = brcmf_fil_cmd_int_get(ifp, BRCMF_C_GET_VERSION, (uint32_t*)&io_type, nullptr);
    if (err != ZX_OK) {
        brcmf_err("Failed to get D11 version (%d)\n", err);
        goto priv_out;
    }
    cfg->d11inf.io_type = (uint8_t)io_type;
    brcmu_d11_attach(&cfg->d11inf);

    // NOTE: linux first verifies that 40 MHz operation is enabled in 2.4 GHz channels.
    err = brcmf_enable_bw40_2g(cfg);
    if (err == ZX_OK) {
        err = brcmf_fil_iovar_int_set(ifp, "obss_coex", BRCMF_OBSS_COEX_AUTO, nullptr);
    }

    /* p2p might require that "if-events" get processed by fweh. So
     * activate the already registered event handlers now and activate
     * the rest when initialization has completed. drvr->config needs to
     * be assigned before activating events.
     */
    drvr->config = cfg;
    err = brcmf_fweh_activate_events(ifp);
    if (err != ZX_OK) {
        brcmf_err("FWEH activation failed (%d)\n", err);
        goto wiphy_unreg_out;
    }

    err = brcmf_btcoex_attach(cfg);
    if (err != ZX_OK) {
        brcmf_err("BT-coex initialisation failed (%d)\n", err);
        goto wiphy_unreg_out;
    }
    err = brcmf_pno_attach(cfg);
    if (err != ZX_OK) {
        brcmf_err("PNO initialisation failed (%d)\n", err);
        brcmf_btcoex_detach(cfg);
        goto wiphy_unreg_out;
    }

    if (brcmf_feat_is_enabled(ifp, BRCMF_FEAT_TDLS)) {
        err = brcmf_fil_iovar_int_set(ifp, "tdls_enable", 1, nullptr);
        if (err != ZX_OK) {
            brcmf_dbg(INFO, "TDLS not enabled (%d)\n", err);
            wiphy->flags &= ~WIPHY_FLAG_SUPPORTS_TDLS;
        } else {
            brcmf_fweh_register(cfg->pub, BRCMF_E_TDLS_PEER_EVENT, brcmf_notify_tdls_peer_event);
        }
    }

    /* (re-) activate FWEH event handling */
    err = brcmf_fweh_activate_events(ifp);
    if (err != ZX_OK) {
        brcmf_err("FWEH activation failed (%d)\n", err);
        goto detach;
    }

    brcmf_dbg(TEMP, "Exit");
    return cfg;

detach:
    brcmf_pno_detach(cfg);
    brcmf_btcoex_detach(cfg);
wiphy_unreg_out:
    brcmf_dbg(TEMP, "* * Would have called wiphy_unregister(cfg->wiphy);");
priv_out:
    wl_deinit_priv(cfg);
    brcmf_free_vif(vif);
    ifp->vif = NULL;
wiphy_out:
    brcmf_free_wiphy(wiphy);
    return NULL;
}

void brcmf_cfg80211_detach(struct brcmf_cfg80211_info* cfg) {
    if (!cfg) {
        return;
    }

    brcmf_pno_detach(cfg);
    brcmf_btcoex_detach(cfg);
    brcmf_dbg(TEMP, "* * Would have called wiphy_unregister(cfg->wiphy);");
    wl_deinit_priv(cfg);
    brcmf_free_wiphy(cfg->wiphy);
}
