/*
 * Copyright (c) 2016 Broadcom
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

#include "pno.h"

#include <threads.h>

#include "cfg80211.h"
#include "core.h"
#include "debug.h"
#include "fwil.h"
#include "fwil_types.h"
#include "linuxisms.h"

#define BRCMF_PNO_VERSION 2
#define BRCMF_PNO_REPEAT 4
#define BRCMF_PNO_FREQ_EXPO_MAX 3
#define BRCMF_PNO_IMMEDIATE_SCAN_BIT 3
#define BRCMF_PNO_ENABLE_BD_SCAN_BIT 5
#define BRCMF_PNO_ENABLE_ADAPTSCAN_BIT 6
#define BRCMF_PNO_REPORT_SEPARATELY_BIT 11
#define BRCMF_PNO_SCAN_INCOMPLETE 0
#define BRCMF_PNO_WPA_AUTH_ANY 0xFFFFFFFF
#define BRCMF_PNO_HIDDEN_BIT 2
#define BRCMF_PNO_SCHED_SCAN_PERIOD 30

#define BRCMF_PNO_MAX_BUCKETS 16
#define GSCAN_BATCH_NO_THR_SET 101
#define GSCAN_RETRY_THRESHOLD 3

struct brcmf_pno_info {
    int n_reqs;
    struct cfg80211_sched_scan_request* reqs[BRCMF_PNO_MAX_BUCKETS];
    mtx_t req_lock;
};

#define ifp_to_pno(_ifp) ((_ifp)->drvr->config->pno)

static zx_status_t brcmf_pno_store_request(struct brcmf_pno_info* pi,
                                           struct cfg80211_sched_scan_request* req) {
    if (WARN(pi->n_reqs == BRCMF_PNO_MAX_BUCKETS, "pno request storage full\n")) {
        return ZX_ERR_NO_RESOURCES;
    }

    brcmf_dbg(SCAN, "reqid=%lu\n", req->reqid);
    mtx_lock(&pi->req_lock);
    pi->reqs[pi->n_reqs++] = req;
    mtx_unlock(&pi->req_lock);
    return ZX_OK;
}

static zx_status_t brcmf_pno_remove_request(struct brcmf_pno_info* pi, uint64_t reqid) {
    int i;
    zx_status_t err = ZX_OK;

    mtx_lock(&pi->req_lock);

    /* find request */
    for (i = 0; i < pi->n_reqs; i++) {
        if (pi->reqs[i]->reqid == reqid) {
            break;
        }
    }
    /* request not found */
    if (WARN(i == pi->n_reqs, "reqid not found\n")) {
        err = ZX_ERR_NOT_FOUND;
        goto done;
    }

    brcmf_dbg(SCAN, "reqid=%lu\n", reqid);
    pi->n_reqs--;

    /* if last we are done */
    if (!pi->n_reqs || i == pi->n_reqs) {
        goto done;
    }

    /* fill the gap with remaining requests */
    while (i <= pi->n_reqs - 1) {
        pi->reqs[i] = pi->reqs[i + 1];
        i++;
    }

done:
    mtx_unlock(&pi->req_lock);
    return err;
}

static zx_status_t brcmf_pno_channel_config(struct brcmf_if* ifp, struct brcmf_pno_config_le* cfg) {
    cfg->reporttype = 0;
    cfg->flags = 0;

    return brcmf_fil_iovar_data_set(ifp, "pfn_cfg", cfg, sizeof(*cfg));
}

static zx_status_t brcmf_pno_config(struct brcmf_if* ifp, uint32_t scan_freq, uint32_t mscan,
                                    uint32_t bestn) {
    struct brcmf_pno_param_le pfn_param;
    uint16_t flags;
    uint32_t pfnmem;
    zx_status_t err;

    memset(&pfn_param, 0, sizeof(pfn_param));
    pfn_param.version = BRCMF_PNO_VERSION;

    /* set extra pno params */
    flags = BIT(BRCMF_PNO_IMMEDIATE_SCAN_BIT) | BIT(BRCMF_PNO_ENABLE_ADAPTSCAN_BIT);
    pfn_param.repeat = BRCMF_PNO_REPEAT;
    pfn_param.exp = BRCMF_PNO_FREQ_EXPO_MAX;

    /* set up pno scan fr */
    pfn_param.scan_freq = scan_freq;

    if (mscan) {
        pfnmem = bestn;

        /* set bestn in firmware */
        err = brcmf_fil_iovar_int_set(ifp, "pfnmem", pfnmem);
        if (err != ZX_OK) {
            brcmf_err("failed to set pfnmem\n");
            goto exit;
        }
        /* get max mscan which the firmware supports */
        err = brcmf_fil_iovar_int_get(ifp, "pfnmem", &pfnmem);
        if (err != ZX_OK) {
            brcmf_err("failed to get pfnmem\n");
            goto exit;
        }
        mscan = min_t(uint32_t, mscan, pfnmem);
        pfn_param.mscan = mscan;
        pfn_param.bestn = bestn;
        flags |= BIT(BRCMF_PNO_ENABLE_BD_SCAN_BIT);
        brcmf_dbg(INFO, "mscan=%d, bestn=%d\n", mscan, bestn);
    }

    pfn_param.flags = flags;
    err = brcmf_fil_iovar_data_set(ifp, "pfn_set", &pfn_param, sizeof(pfn_param));
    if (err != ZX_OK) {
        brcmf_err("pfn_set failed, err=%d\n", err);
    }

exit:
    return err;
}

static zx_status_t brcmf_pno_set_random(struct brcmf_if* ifp, struct brcmf_pno_info* pi) {
    struct brcmf_pno_macaddr_le pfn_mac;
    uint8_t* mac_addr = NULL;
    uint8_t* mac_mask = NULL;
    int i;
    zx_status_t err;

    for (i = 0; i < pi->n_reqs; i++)
        if (pi->reqs[i]->flags & NL80211_SCAN_FLAG_RANDOM_ADDR) {
            mac_addr = pi->reqs[i]->mac_addr;
            mac_mask = pi->reqs[i]->mac_addr_mask;
            break;
        }

    /* no random mac requested */
    if (!mac_addr) {
        return ZX_OK;
    }

    pfn_mac.version = BRCMF_PFN_MACADDR_CFG_VER;
    pfn_mac.flags = BRCMF_PFN_MAC_OUI_ONLY | BRCMF_PFN_SET_MAC_UNASSOC;

    memcpy(pfn_mac.mac, mac_addr, ETH_ALEN);
    for (i = 0; i < ETH_ALEN; i++) {
        pfn_mac.mac[i] &= mac_mask[i];
        pfn_mac.mac[i] |= get_random_int() & ~(mac_mask[i]);
    }
    /* Clear multi bit */
    pfn_mac.mac[0] &= 0xFE;
    /* Set locally administered */
    pfn_mac.mac[0] |= 0x02;

    brcmf_dbg(SCAN, "enabling random mac: reqid=%lu mac=%pM\n", pi->reqs[i]->reqid, pfn_mac.mac);
    err = brcmf_fil_iovar_data_set(ifp, "pfn_macaddr", &pfn_mac, sizeof(pfn_mac));
    if (err != ZX_OK) {
        brcmf_err("pfn_macaddr failed, err=%d\n", err);
    }

    return err;
}

static zx_status_t brcmf_pno_add_ssid(struct brcmf_if* ifp, struct cfg80211_ssid* ssid,
                                      bool active) {
    struct brcmf_pno_net_param_le pfn;
    zx_status_t err;

    pfn.auth = WLAN_AUTH_OPEN;
    pfn.wpa_auth = BRCMF_PNO_WPA_AUTH_ANY;
    pfn.wsec = 0;
    pfn.infra = 1;
    pfn.flags = 0;
    if (active) {
        pfn.flags = 1 << BRCMF_PNO_HIDDEN_BIT;
    }
    pfn.ssid.SSID_len = ssid->ssid_len;
    memcpy(pfn.ssid.SSID, ssid->ssid, ssid->ssid_len);

    brcmf_dbg(SCAN, "adding ssid=%.32s (active=%d)\n", ssid->ssid, active);
    err = brcmf_fil_iovar_data_set(ifp, "pfn_add", &pfn, sizeof(pfn));
    if (err != ZX_OK) {
        brcmf_err("adding failed: err=%d\n", err);
    }
    return err;
}

static zx_status_t brcmf_pno_add_bssid(struct brcmf_if* ifp, const uint8_t* bssid) {
    struct brcmf_pno_bssid_le bssid_cfg;
    zx_status_t err;

    memcpy(bssid_cfg.bssid, bssid, ETH_ALEN);
    bssid_cfg.flags = 0;

    brcmf_dbg(SCAN, "adding bssid=%pM\n", bssid);
    err = brcmf_fil_iovar_data_set(ifp, "pfn_add_bssid", &bssid_cfg, sizeof(bssid_cfg));
    if (err != ZX_OK) {
        brcmf_err("adding failed: err=%d\n", err);
    }
    return err;
}

static bool brcmf_is_ssid_active(struct cfg80211_ssid* ssid,
                                 struct cfg80211_sched_scan_request* req) {
    int i;

    if (!ssid || !req->ssids || !req->n_ssids) {
        return false;
    }

    for (i = 0; i < req->n_ssids; i++) {
        if (ssid->ssid_len == req->ssids[i].ssid_len) {
            if (!strncmp(ssid->ssid, req->ssids[i].ssid, ssid->ssid_len)) {
                return true;
            }
        }
    }
    return false;
}

static zx_status_t brcmf_pno_clean(struct brcmf_if* ifp) {
    zx_status_t ret;

    /* Disable pfn */
    ret = brcmf_fil_iovar_int_set(ifp, "pfn", 0);
    if (ret == ZX_OK) {
        /* clear pfn */
        ret = brcmf_fil_iovar_data_set(ifp, "pfnclear", NULL, 0);
    }
    if (ret != ZX_OK) {
        brcmf_err("failed code %d\n", ret);
    }

    return ret;
}

static zx_status_t brcmf_pno_get_bucket_channels(struct cfg80211_sched_scan_request* r,
                                                 struct brcmf_pno_config_le* pno_cfg,
                                                 int *channels_out) {
    uint32_t n_chan = pno_cfg->channel_num;
    uint16_t chan;
    int i;
    zx_status_t err = ZX_OK;

    for (i = 0; i < r->n_channels; i++) {
        if (n_chan >= BRCMF_NUMCHANNELS) {
            err = ZX_ERR_NO_RESOURCES;
            goto done;
        }
        chan = r->channels[i]->hw_value;
        brcmf_dbg(SCAN, "[%d] Chan : %u\n", n_chan, chan);
        pno_cfg->channel_list[n_chan++] = chan;
    }
    /* return number of channels */
    err = ZX_OK;
    if (channels_out) {
        *channels_out = n_chan;
    }
done:
    pno_cfg->channel_num = n_chan;
    return err;
}

static zx_status_t brcmf_pno_prep_fwconfig(struct brcmf_pno_info* pi,
                                           struct brcmf_pno_config_le* pno_cfg,
                                           struct brcmf_gscan_bucket_config** buckets,
                                           uint32_t* scan_freq, int* nbuckets_out) {
    struct cfg80211_sched_scan_request* sr;
    struct brcmf_gscan_bucket_config* fw_buckets;
    int i, chidx;
    zx_status_t err;

    brcmf_dbg(SCAN, "n_reqs=%d\n", pi->n_reqs);
    if (WARN_ON(!pi->n_reqs)) {
        return ZX_ERR_INVALID_ARGS;
    }

    /*
     * actual scan period is determined using gcd() for each
     * scheduled scan period.
     */
    *scan_freq = pi->reqs[0]->scan_plans[0].interval;
    for (i = 1; i < pi->n_reqs; i++) {
        sr = pi->reqs[i];
        *scan_freq = gcd(sr->scan_plans[0].interval, *scan_freq);
    }
    if (*scan_freq < BRCMF_PNO_SCHED_SCAN_MIN_PERIOD) {
        brcmf_dbg(SCAN, "scan period too small, using minimum\n");
        *scan_freq = BRCMF_PNO_SCHED_SCAN_MIN_PERIOD;
    }

    *buckets = NULL;
    fw_buckets = calloc(pi->n_reqs, sizeof(*fw_buckets));
    if (!fw_buckets) {
        return ZX_ERR_NO_MEMORY;
    }

    memset(pno_cfg, 0, sizeof(*pno_cfg));
    for (i = 0; i < pi->n_reqs; i++) {
        sr = pi->reqs[i];
        err = brcmf_pno_get_bucket_channels(sr, pno_cfg, &chidx);
        if (err != ZX_OK) {
            goto fail;
        }
        fw_buckets[i].bucket_end_index = chidx - 1;
        fw_buckets[i].bucket_freq_multiple = sr->scan_plans[0].interval / *scan_freq;
        /* assure period is non-zero */
        if (!fw_buckets[i].bucket_freq_multiple) {
            fw_buckets[i].bucket_freq_multiple = 1;
        }
        fw_buckets[i].flag = BRCMF_PNO_REPORT_NO_BATCH;
    }

    if (BRCMF_SCAN_ON()) {
        brcmf_err("base period=%u\n", *scan_freq);
        for (i = 0; i < pi->n_reqs; i++) {
            brcmf_err("[%d] period %u max %u repeat %u flag %x idx %u\n", i,
                      fw_buckets[i].bucket_freq_multiple,
                      fw_buckets[i].max_freq_multiple, fw_buckets[i].repeat,
                      fw_buckets[i].flag, fw_buckets[i].bucket_end_index);
        }
    }
    *buckets = fw_buckets;
    if (nbuckets_out) {
        *nbuckets_out = pi->n_reqs;
    }
    return ZX_OK;

fail:
    free(fw_buckets);
    return err;
}

static zx_status_t brcmf_pno_config_networks(struct brcmf_if* ifp, struct brcmf_pno_info* pi) {
    struct cfg80211_sched_scan_request* r;
    struct cfg80211_match_set* ms;
    bool active;
    int i, j;
    zx_status_t err = ZX_OK;

    for (i = 0; i < pi->n_reqs; i++) {
        r = pi->reqs[i];

        for (j = 0; j < r->n_match_sets; j++) {
            ms = &r->match_sets[j];
            if (ms->ssid.ssid_len) {
                active = brcmf_is_ssid_active(&ms->ssid, r);
                err = brcmf_pno_add_ssid(ifp, &ms->ssid, active);
            }
            if (err == ZX_OK && is_valid_ether_addr(ms->bssid)) {
                err = brcmf_pno_add_bssid(ifp, ms->bssid);
            }

            if (err != ZX_OK) {
                return err;
            }
        }
    }
    return ZX_OK;
}

static zx_status_t brcmf_pno_config_sched_scans(struct brcmf_if* ifp) {
    struct brcmf_pno_info* pi;
    struct brcmf_gscan_config* gscan_cfg;
    struct brcmf_gscan_bucket_config* buckets;
    struct brcmf_pno_config_le pno_cfg;
    size_t gsz;
    uint32_t scan_freq;
    int n_buckets;
    zx_status_t err;

    pi = ifp_to_pno(ifp);
    err = brcmf_pno_prep_fwconfig(pi, &pno_cfg, &buckets, &scan_freq, &n_buckets);
    if (err != ZX_OK) {
        return err;
    }

    gsz = sizeof(*gscan_cfg) + (n_buckets - 1) * sizeof(*buckets);
    gscan_cfg = calloc(1, gsz);
    if (!gscan_cfg) {
        err = ZX_ERR_NO_MEMORY;
        goto free_buckets;
    }

    /* clean up everything */
    err = brcmf_pno_clean(ifp);
    if (err != ZX_OK) {
        brcmf_err("failed error=%d\n", err);
        goto free_gscan;
    }

    /* configure pno */
    err = brcmf_pno_config(ifp, scan_freq, 0, 0);
    if (err != ZX_OK) {
        goto free_gscan;
    }

    err = brcmf_pno_channel_config(ifp, &pno_cfg);
    if (err != ZX_OK) {
        goto clean;
    }

    gscan_cfg->version = BRCMF_GSCAN_CFG_VERSION;
    gscan_cfg->retry_threshold = GSCAN_RETRY_THRESHOLD;
    gscan_cfg->buffer_threshold = GSCAN_BATCH_NO_THR_SET;
    gscan_cfg->flags = BRCMF_GSCAN_CFG_ALL_BUCKETS_IN_1ST_SCAN;

    gscan_cfg->count_of_channel_buckets = n_buckets;
    memcpy(&gscan_cfg->bucket[0], buckets, n_buckets * sizeof(*buckets));

    err = brcmf_fil_iovar_data_set(ifp, "pfn_gscan_cfg", gscan_cfg, gsz);

    if (err != ZX_OK) {
        goto clean;
    }

    /* configure random mac */
    err = brcmf_pno_set_random(ifp, pi);
    if (err != ZX_OK) {
        goto clean;
    }

    err = brcmf_pno_config_networks(ifp, pi);
    if (err != ZX_OK) {
        goto clean;
    }

    /* Enable the PNO */
    err = brcmf_fil_iovar_int_set(ifp, "pfn", 1);

clean:
    if (err != ZX_OK) {
        brcmf_pno_clean(ifp);
    }
free_gscan:
    free(gscan_cfg);
free_buckets:
    free(buckets);
    return err;
}

zx_status_t brcmf_pno_start_sched_scan(struct brcmf_if* ifp,
                                       struct cfg80211_sched_scan_request* req) {
    struct brcmf_pno_info* pi;
    zx_status_t ret;

    brcmf_dbg(TRACE, "reqid=%lu\n", req->reqid);

    pi = ifp_to_pno(ifp);
    ret = brcmf_pno_store_request(pi, req);
    if (ret != ZX_OK) {
        return ret;
    }

    ret = brcmf_pno_config_sched_scans(ifp);
    if (ret != ZX_OK) {
        brcmf_pno_remove_request(pi, req->reqid);
        if (pi->n_reqs) {
            (void)brcmf_pno_config_sched_scans(ifp);
        }
        return ret;
    }
    return ZX_OK;
}

zx_status_t brcmf_pno_stop_sched_scan(struct brcmf_if* ifp, uint64_t reqid) {
    struct brcmf_pno_info* pi;
    zx_status_t err;

    brcmf_dbg(TRACE, "reqid=%lu\n", reqid);

    pi = ifp_to_pno(ifp);
    err = brcmf_pno_remove_request(pi, reqid);
    if (err != ZX_OK) {
        return err;
    }

    brcmf_pno_clean(ifp);

    if (pi->n_reqs) {
        (void)brcmf_pno_config_sched_scans(ifp);
    }

    return ZX_OK;
}

zx_status_t brcmf_pno_attach(struct brcmf_cfg80211_info* cfg) {
    struct brcmf_pno_info* pi;

    brcmf_dbg(TRACE, "enter\n");
    pi = calloc(1, sizeof(*pi));
    if (!pi) {
        return ZX_ERR_NO_MEMORY;
    }

    cfg->pno = pi;
    mtx_init(&pi->req_lock, mtx_plain);
    return ZX_OK;
}

void brcmf_pno_detach(struct brcmf_cfg80211_info* cfg) {
    struct brcmf_pno_info* pi;

    brcmf_dbg(TRACE, "enter\n");
    pi = cfg->pno;
    cfg->pno = NULL;

    WARN_ON(pi->n_reqs);
    mtx_destroy(&pi->req_lock);
    free(pi);
}

void brcmf_pno_wiphy_params(struct wiphy* wiphy, bool gscan) {
    /* scheduled scan settings */
    wiphy->max_sched_scan_reqs = gscan ? BRCMF_PNO_MAX_BUCKETS : 1;
    wiphy->max_sched_scan_ssids = BRCMF_PNO_MAX_PFN_COUNT;
    wiphy->max_match_sets = BRCMF_PNO_MAX_PFN_COUNT;
    wiphy->max_sched_scan_ie_len = BRCMF_SCAN_IE_LEN_MAX;
    wiphy->max_sched_scan_plan_interval = BRCMF_PNO_SCHED_SCAN_MAX_PERIOD;
}

uint64_t brcmf_pno_find_reqid_by_bucket(struct brcmf_pno_info* pi, uint32_t bucket) {
    uint64_t reqid = 0;

    mtx_lock(&pi->req_lock);

    if ((int)bucket < pi->n_reqs) {
        reqid = pi->reqs[bucket]->reqid;
    }

    mtx_unlock(&pi->req_lock);
    return reqid;
}

uint32_t brcmf_pno_get_bucket_map(struct brcmf_pno_info* pi, struct brcmf_pno_net_info_le* ni) {
    struct cfg80211_sched_scan_request* req;
    struct cfg80211_match_set* ms;
    uint32_t bucket_map = 0;
    int i, j;

    mtx_lock(&pi->req_lock);
    for (i = 0; i < pi->n_reqs; i++) {
        req = pi->reqs[i];

        if (!req->n_match_sets) {
            continue;
        }
        for (j = 0; j < req->n_match_sets; j++) {
            ms = &req->match_sets[j];
            if (ms->ssid.ssid_len == ni->SSID_len &&
                    !memcmp(ms->ssid.ssid, ni->SSID, ni->SSID_len)) {
                bucket_map |= BIT(i);
                break;
            }
            if (is_valid_ether_addr(ms->bssid) && !memcmp(ms->bssid, ni->bssid, ETH_ALEN)) {
                bucket_map |= BIT(i);
                break;
            }
        }
    }
    mtx_unlock(&pi->req_lock);
    return bucket_map;
}
