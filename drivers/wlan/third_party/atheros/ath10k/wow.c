/*
 * Copyright (c) 2015 Qualcomm Atheros, Inc.
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

#include "mac.h"

#include <net/mac80211.h>
#include "hif.h"
#include "core.h"
#include "debug.h"
#include "wmi.h"
#include "wmi-ops.h"

static const struct wiphy_wowlan_support ath10k_wowlan_support = {
    .flags = WIPHY_WOWLAN_DISCONNECT |
    WIPHY_WOWLAN_MAGIC_PKT,
    .pattern_min_len = WOW_MIN_PATTERN_SIZE,
    .pattern_max_len = WOW_MAX_PATTERN_SIZE,
    .max_pkt_offset = WOW_MAX_PKT_OFFSET,
};

static int ath10k_wow_vif_cleanup(struct ath10k_vif* arvif) {
    struct ath10k* ar = arvif->ar;
    int i, ret;

    for (i = 0; i < WOW_EVENT_MAX; i++) {
        ret = ath10k_wmi_wow_add_wakeup_event(ar, arvif->vdev_id, i, 0);
        if (ret) {
            ath10k_warn("failed to issue wow wakeup for event %s on vdev %i: %d\n",
                        wow_wakeup_event(i), arvif->vdev_id, ret);
            return ret;
        }
    }

    for (i = 0; i < ar->wow.max_num_patterns; i++) {
        ret = ath10k_wmi_wow_del_pattern(ar, arvif->vdev_id, i);
        if (ret) {
            ath10k_warn("failed to delete wow pattern %d for vdev %i: %d\n",
                        i, arvif->vdev_id, ret);
            return ret;
        }
    }

    return 0;
}

static int ath10k_wow_cleanup(struct ath10k* ar) {
    struct ath10k_vif* arvif;
    int ret;

    ASSERT_MTX_HELD(&ar->conf_mutex);

    list_for_each_entry(arvif, &ar->arvifs, list) {
        ret = ath10k_wow_vif_cleanup(arvif);
        if (ret) {
            ath10k_warn("failed to clean wow wakeups on vdev %i: %d\n",
                        arvif->vdev_id, ret);
            return ret;
        }
    }

    return 0;
}

static int ath10k_vif_wow_set_wakeups(struct ath10k_vif* arvif,
                                      struct cfg80211_wowlan* wowlan) {
    int ret, i;
    BITARR(wow_mask, WOW_EVENT_MAX) = { 0 };
    struct ath10k* ar = arvif->ar;
    const struct cfg80211_pkt_pattern* patterns = wowlan->patterns;
    int pattern_id = 0;

    /* Setup requested WOW features */
    switch (arvif->vdev_type) {
    case WMI_VDEV_TYPE_IBSS:
        BITARR_SET(&wow_mask, WOW_BEACON_EVENT);
    /* fall through */
    case WMI_VDEV_TYPE_AP:
        BITARR_SET(&wow_mask, WOW_DEAUTH_RECVD_EVENT);
        BITARR_SET(&wow_mask, WOW_DISASSOC_RECVD_EVENT);
        BITARR_SET(&wow_mask, WOW_PROBE_REQ_WPS_IE_EVENT);
        BITARR_SET(&wow_mask, WOW_AUTH_REQ_EVENT);
        BITARR_SET(&wow_mask, WOW_ASSOC_REQ_EVENT);
        BITARR_SET(&wow_mask, WOW_HTT_EVENT);
        BITARR_SET(&wow_mask, WOW_RA_MATCH_EVENT);
        break;
    case WMI_VDEV_TYPE_STA:
        if (wowlan->disconnect) {
            BITARR_SET(&wow_mask, WOW_DEAUTH_RECVD_EVENT);
            BITARR_SET(&wow_mask, WOW_DISASSOC_RECVD_EVENT);
            BITARR_SET(&wow_mask, WOW_BMISS_EVENT);
            BITARR_SET(&wow_mask, WOW_CSA_IE_EVENT);
        }

        if (wowlan->magic_pkt) {
            BITARR_SET(&wow_mask, WOW_MAGIC_PKT_RECVD_EVENT);
        }
        break;
    default:
        break;
    }

    for (i = 0; i < wowlan->n_patterns; i++) {
        uint8_t bitmask[WOW_MAX_PATTERN_SIZE] = {};
        int j;

        if (patterns[i].pattern_len > WOW_MAX_PATTERN_SIZE) {
            continue;
        }

        /* convert bytemask to bitmask */
        for (j = 0; j < patterns[i].pattern_len; j++)
            if (patterns[i].mask[j / 8] & BIT(j % 8)) {
                bitmask[j] = 0xff;
            }

        ret = ath10k_wmi_wow_add_pattern(ar, arvif->vdev_id,
                                         pattern_id,
                                         patterns[i].pattern,
                                         bitmask,
                                         patterns[i].pattern_len,
                                         patterns[i].pkt_offset);
        if (ret) {
            ath10k_warn("failed to add pattern %i to vdev %i: %d\n",
                        pattern_id,
                        arvif->vdev_id, ret);
            return ret;
        }

        pattern_id++;
        BITARR_SET(&wow_mask, WOW_PATTERN_MATCH_EVENT);
    }

    for (i = 0; i < WOW_EVENT_MAX; i++) {
        if (!BITARR_TEST(&wow_mask, i)) {
            continue;
        }
        ret = ath10k_wmi_wow_add_wakeup_event(ar, arvif->vdev_id, i, 1);
        if (ret) {
            ath10k_warn("failed to enable wakeup event %s on vdev %i: %d\n",
                        wow_wakeup_event(i), arvif->vdev_id, ret);
            return ret;
        }
    }

    return 0;
}

static int ath10k_wow_set_wakeups(struct ath10k* ar,
                                  struct cfg80211_wowlan* wowlan) {
    struct ath10k_vif* arvif;
    int ret;

    ASSERT_MTX_HELD(&ar->conf_mutex);

    list_for_each_entry(arvif, &ar->arvifs, list) {
        ret = ath10k_vif_wow_set_wakeups(arvif, wowlan);
        if (ret) {
            ath10k_warn("failed to set wow wakeups on vdev %i: %d\n",
                        arvif->vdev_id, ret);
            return ret;
        }
    }

    return 0;
}

static int ath10k_wow_enable(struct ath10k* ar) {
    int ret;

    ASSERT_MTX_HELD(&ar->conf_mutex);

    sync_completion_reset(&ar->target_suspend);

    ret = ath10k_wmi_wow_enable(ar);
    if (ret) {
        ath10k_warn("failed to issue wow enable: %d\n", ret);
        return ret;
    }

    if (sync_completion_wait(&ar->target_suspend, ZX_SEC(3)) == ZX_ERR_TIMED_OUT) {
        ath10k_warn("timed out while waiting for suspend completion\n");
        return -ETIMEDOUT;
    }

    return 0;
}

static int ath10k_wow_wakeup(struct ath10k* ar) {
    int ret;

    ASSERT_MTX_HELD(&ar->conf_mutex);

    sync_completion_reset(&ar->wow.wakeup_completed);

    ret = ath10k_wmi_wow_host_wakeup_ind(ar);
    if (ret) {
        ath10k_warn("failed to send wow wakeup indication: %d\n",
                    ret);
        return ret;
    }

    if (sync_completion_wait(&ar->wow.wakeup_completed, ZX_SEC(3)) == ZX_ERR_TIMED_OUT) {
        ath10k_warn("timed out while waiting for wow wakeup completion\n");
        return -ETIMEDOUT;
    }

    return 0;
}

int ath10k_wow_op_suspend(struct ieee80211_hw* hw,
                          struct cfg80211_wowlan* wowlan) {
    struct ath10k* ar = hw->priv;
    int ret;

    mtx_lock(&ar->conf_mutex);

    if (COND_WARN(!BITARR_TEST(ar->running_fw->fw_file.fw_features,
                               ATH10K_FW_FEATURE_WOWLAN_SUPPORT))) {
        ret = 1;
        goto exit;
    }

    ret =  ath10k_wow_cleanup(ar);
    if (ret) {
        ath10k_warn("failed to clear wow wakeup events: %d\n",
                    ret);
        goto exit;
    }

    ret = ath10k_wow_set_wakeups(ar, wowlan);
    if (ret) {
        ath10k_warn("failed to set wow wakeup events: %d\n",
                    ret);
        goto cleanup;
    }

    ret = ath10k_wow_enable(ar);
    if (ret) {
        ath10k_warn("failed to start wow: %d\n", ret);
        goto cleanup;
    }

    ret = ath10k_hif_suspend(ar);
    if (ret) {
        ath10k_warn("failed to suspend hif: %d\n", ret);
        goto wakeup;
    }

    goto exit;

wakeup:
    ath10k_wow_wakeup(ar);

cleanup:
    ath10k_wow_cleanup(ar);

exit:
    mtx_unlock(&ar->conf_mutex);
    return ret ? 1 : 0;
}

int ath10k_wow_op_resume(struct ieee80211_hw* hw) {
    struct ath10k* ar = hw->priv;
    int ret;

    mtx_lock(&ar->conf_mutex);

    if (COND_WARN(!BITARR_TEST(ar->running_fw->fw_file.fw_features,
                               ATH10K_FW_FEATURE_WOWLAN_SUPPORT))) {
        ret = 1;
        goto exit;
    }

    ret = ath10k_hif_resume(ar);
    if (ret) {
        ath10k_warn("failed to resume hif: %d\n", ret);
        goto exit;
    }

    ret = ath10k_wow_wakeup(ar);
    if (ret) {
        ath10k_warn("failed to wakeup from wow: %d\n", ret);
    }

exit:
    if (ret) {
        switch (ar->state) {
        case ATH10K_STATE_ON:
            ar->state = ATH10K_STATE_RESTARTING;
            ret = 1;
            break;
        case ATH10K_STATE_OFF:
        case ATH10K_STATE_RESTARTING:
        case ATH10K_STATE_RESTARTED:
        case ATH10K_STATE_UTF:
        case ATH10K_STATE_WEDGED:
            ath10k_warn("encountered unexpected device state %d on resume, cannot recover\n",
                        ar->state);
            ret = -EIO;
            break;
        }
    }

    mtx_unlock(&ar->conf_mutex);
    return ret;
}

int ath10k_wow_init(struct ath10k* ar) {
    if (!BITARR_TEST(ar->running_fw->fw_file.fw_features, ATH10K_FW_FEATURE_WOWLAN_SUPPORT)) {
        return 0;
    }

    if (COND_WARN(!BITARR_TEST(ar->wmi.svc_map, WMI_SERVICE_WOW))) {
        return -EINVAL;
    }

    ar->wow.wowlan_support = ath10k_wowlan_support;
    ar->wow.wowlan_support.n_patterns = ar->wow.max_num_patterns;
    ar->hw->wiphy->wowlan = &ar->wow.wowlan_support;

    return 0;
}
