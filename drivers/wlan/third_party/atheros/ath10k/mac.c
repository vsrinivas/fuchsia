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

#include "mac.h"

#include <stdlib.h>

#include <zircon/status.h>

#include "hif.h"
#include "core.h"
#include "debug.h"
#include "wmi.h"
#include "htt.h"
#include "ieee80211.h"
#include "macros.h"
#include "txrx.h"
#include "testmode.h"
#include "wmi.h"
#include "wmi-tlv.h"
#include "wmi-ops.h"
#include "wow.h"

#define CHAN2G(_channel, _freq, _flags) { \
    .hw_value           = (_channel), \
    .center_freq        = (_freq), \
    .flags              = (_flags), \
    .max_antenna_gain   = 0, \
    .max_power          = 30, \
    .max_reg_power      = 0, \
}

#define CHAN5G(_channel, _freq, _flags) { \
    .hw_value           = (_channel), \
    .center_freq        = (_freq), \
    .flags              = (_flags), \
    .max_antenna_gain   = 0, \
    .max_power          = 30, \
    .max_reg_power      = 0, \
}

static const struct ath10k_channel ath10k_2ghz_channels[] = {
    CHAN2G(1, 2412, 0),
    CHAN2G(2, 2417, 0),
    CHAN2G(3, 2422, 0),
    CHAN2G(4, 2427, 0),
    CHAN2G(5, 2432, 0),
    CHAN2G(6, 2437, 0),
    CHAN2G(7, 2442, 0),
    CHAN2G(8, 2447, 0),
    CHAN2G(9, 2452, 0),
    CHAN2G(10, 2457, 0),
    CHAN2G(11, 2462, 0),
    CHAN2G(12, 2467, 0),
    CHAN2G(13, 2472, 0),
    CHAN2G(14, 2484, 0),
};

static const struct ath10k_channel ath10k_5ghz_channels[] = {
    CHAN5G(36, 5180, 0),
    CHAN5G(40, 5200, 0),
    CHAN5G(44, 5220, 0),
    CHAN5G(48, 5240, 0),
    CHAN5G(52, 5260, 0),
    CHAN5G(56, 5280, 0),
    CHAN5G(60, 5300, 0),
    CHAN5G(64, 5320, 0),
    CHAN5G(100, 5500, 0),
    CHAN5G(104, 5520, 0),
    CHAN5G(108, 5540, 0),
    CHAN5G(112, 5560, 0),
    CHAN5G(116, 5580, 0),
    CHAN5G(120, 5600, 0),
    CHAN5G(124, 5620, 0),
    CHAN5G(128, 5640, 0),
    CHAN5G(132, 5660, 0),
    CHAN5G(136, 5680, 0),
    CHAN5G(140, 5700, 0),
    CHAN5G(144, 5720, 0),
    CHAN5G(149, 5745, 0),
    CHAN5G(153, 5765, 0),
    CHAN5G(157, 5785, 0),
    CHAN5G(161, 5805, 0),
    CHAN5G(165, 5825, 0),
    CHAN5G(169, 5845, 0),
};

static const struct ath10k_band ath10k_supported_bands[] = {
    {
        .name = "2.4 GHz",
        // FIXME: NET-817
        .ht_caps = { .ht_capability_info = 0x01fe,
                     .ampdu_params = 0x00,
                     .supported_mcs_set = { 0xff, 0x00, 0x00, 0x00,
                                            0x01, 0x00, 0x00, 0x00,
                                            0x00, 0x00, 0x00, 0x00,
                                            0x01, 0x00, 0x00, 0x00 },
                     .ht_ext_capabilities = 0x0000,
                     .tx_beamforming_capabilities = 0x00000000,
                     .asel_capabilities = 0x00 },
        .vht_supported = false,
        .basic_rates = { 2, 4, 11, 22, 12, 18, 24, 36, 48, 72, 96, 108 },
        .base_freq = 2407,
        .n_channels = countof(ath10k_2ghz_channels),
        .channels = ath10k_2ghz_channels,
    },

    {
        .name = "5 GHz",
        // FIXME: NET-817
        .ht_caps = { .ht_capability_info = 0x01fe,
                     .ampdu_params = 0x00,
                     .supported_mcs_set = { 0xff, 0xff, 0x00, 0x00,
                                            0x01, 0x00, 0x00, 0x00,
                                            0x00, 0x00, 0x00, 0x00,
                                            0x01, 0x00, 0x00, 0x00 },
                     .ht_ext_capabilities = 0x0000,
                     .tx_beamforming_capabilities = 0x00000000,
                     .asel_capabilities = 0x00 },
        .vht_supported = false,
        .basic_rates = { 12, 18, 24, 36, 48, 72, 96, 108 },
        .base_freq = 5000,
        .n_channels = countof(ath10k_5ghz_channels),
        .channels = ath10k_5ghz_channels,
    },
};

static zx_status_t ath10k_add_interface(struct ath10k* ar, uint32_t vif_role);

/*********/
/* Rates */
/*********/

#if 0 // NEEDS PORTING
static struct ieee80211_rate ath10k_rates[] = {
    {
        .bitrate = 10,
        .hw_value = ATH10K_HW_RATE_CCK_LP_1M
    },
    {
        .bitrate = 20,
        .hw_value = ATH10K_HW_RATE_CCK_LP_2M,
        .hw_value_short = ATH10K_HW_RATE_CCK_SP_2M,
        .flags = IEEE80211_RATE_SHORT_PREAMBLE
    },
    {
        .bitrate = 55,
        .hw_value = ATH10K_HW_RATE_CCK_LP_5_5M,
        .hw_value_short = ATH10K_HW_RATE_CCK_SP_5_5M,
        .flags = IEEE80211_RATE_SHORT_PREAMBLE
    },
    {
        .bitrate = 110,
        .hw_value = ATH10K_HW_RATE_CCK_LP_11M,
        .hw_value_short = ATH10K_HW_RATE_CCK_SP_11M,
        .flags = IEEE80211_RATE_SHORT_PREAMBLE
    },

    { .bitrate = 60, .hw_value = ATH10K_HW_RATE_OFDM_6M },
    { .bitrate = 90, .hw_value = ATH10K_HW_RATE_OFDM_9M },
    { .bitrate = 120, .hw_value = ATH10K_HW_RATE_OFDM_12M },
    { .bitrate = 180, .hw_value = ATH10K_HW_RATE_OFDM_18M },
    { .bitrate = 240, .hw_value = ATH10K_HW_RATE_OFDM_24M },
    { .bitrate = 360, .hw_value = ATH10K_HW_RATE_OFDM_36M },
    { .bitrate = 480, .hw_value = ATH10K_HW_RATE_OFDM_48M },
    { .bitrate = 540, .hw_value = ATH10K_HW_RATE_OFDM_54M },
};

static struct ieee80211_rate ath10k_rates_rev2[] = {
    {
        .bitrate = 10,
        .hw_value = ATH10K_HW_RATE_REV2_CCK_LP_1M
    },
    {
        .bitrate = 20,
        .hw_value = ATH10K_HW_RATE_REV2_CCK_LP_2M,
        .hw_value_short = ATH10K_HW_RATE_REV2_CCK_SP_2M,
        .flags = IEEE80211_RATE_SHORT_PREAMBLE
    },
    {
        .bitrate = 55,
        .hw_value = ATH10K_HW_RATE_REV2_CCK_LP_5_5M,
        .hw_value_short = ATH10K_HW_RATE_REV2_CCK_SP_5_5M,
        .flags = IEEE80211_RATE_SHORT_PREAMBLE
    },
    {
        .bitrate = 110,
        .hw_value = ATH10K_HW_RATE_REV2_CCK_LP_11M,
        .hw_value_short = ATH10K_HW_RATE_REV2_CCK_SP_11M,
        .flags = IEEE80211_RATE_SHORT_PREAMBLE
    },

    { .bitrate = 60, .hw_value = ATH10K_HW_RATE_OFDM_6M },
    { .bitrate = 90, .hw_value = ATH10K_HW_RATE_OFDM_9M },
    { .bitrate = 120, .hw_value = ATH10K_HW_RATE_OFDM_12M },
    { .bitrate = 180, .hw_value = ATH10K_HW_RATE_OFDM_18M },
    { .bitrate = 240, .hw_value = ATH10K_HW_RATE_OFDM_24M },
    { .bitrate = 360, .hw_value = ATH10K_HW_RATE_OFDM_36M },
    { .bitrate = 480, .hw_value = ATH10K_HW_RATE_OFDM_48M },
    { .bitrate = 540, .hw_value = ATH10K_HW_RATE_OFDM_54M },
};

#define ATH10K_MAC_FIRST_OFDM_RATE_IDX 4

#define ath10k_a_rates (ath10k_rates + ATH10K_MAC_FIRST_OFDM_RATE_IDX)
#define ath10k_a_rates_size (countof(ath10k_rates) - \
                 ATH10K_MAC_FIRST_OFDM_RATE_IDX)
#define ath10k_g_rates (ath10k_rates + 0)
#define ath10k_g_rates_size (countof(ath10k_rates))

#define ath10k_g_rates_rev2 (ath10k_rates_rev2 + 0)
#define ath10k_g_rates_rev2_size (countof(ath10k_rates_rev2))

static bool ath10k_mac_bitrate_is_cck(int bitrate) {
    switch (bitrate) {
    case 10:
    case 20:
    case 55:
    case 110:
        return true;
    }

    return false;
}

static uint8_t ath10k_mac_bitrate_to_rate(int bitrate) {
    return DIV_ROUNDUP(bitrate, 5) |
           (ath10k_mac_bitrate_is_cck(bitrate) ? (1 << 7) : 0);
}

uint8_t ath10k_mac_hw_rate_to_idx(const struct ieee80211_supported_band* sband,
                                  uint8_t hw_rate, bool cck) {
    const struct ieee80211_rate* rate;
    int i;

    for (i = 0; i < sband->n_bitrates; i++) {
        rate = &sband->bitrates[i];

        if (ath10k_mac_bitrate_is_cck(rate->bitrate) != cck) {
            continue;
        }

        if (rate->hw_value == hw_rate) {
            return i;
        } else if (rate->flags & IEEE80211_RATE_SHORT_PREAMBLE &&
                   rate->hw_value_short == hw_rate) {
            return i;
        }
    }

    return 0;
}

uint8_t ath10k_mac_bitrate_to_idx(const struct ieee80211_supported_band* sband,
                                  uint32_t bitrate) {
    int i;

    for (i = 0; i < sband->n_bitrates; i++)
        if (sband->bitrates[i].bitrate == bitrate) {
            return i;
        }

    return 0;
}

static int ath10k_mac_get_max_vht_mcs_map(uint16_t mcs_map, int nss) {
    switch ((mcs_map >> (2 * nss)) & 0x3) {
    case IEEE80211_VHT_MCS_SUPPORT_0_7:
        return (1 << 8) - 1;
    case IEEE80211_VHT_MCS_SUPPORT_0_8:
        return (1 << 9) - 1;
    case IEEE80211_VHT_MCS_SUPPORT_0_9:
        return (1 << 10) - 1;
    }
    return 0;
}

static uint32_t
ath10k_mac_max_ht_nss(const uint8_t ht_mcs_mask[IEEE80211_HT_MCS_MASK_LEN]) {
    int nss;

    for (nss = IEEE80211_HT_MCS_MASK_LEN - 1; nss >= 0; nss--)
        if (ht_mcs_mask[nss]) {
            return nss + 1;
        }

    return 1;
}

static uint32_t
ath10k_mac_max_vht_nss(const uint16_t vht_mcs_mask[NL80211_VHT_NSS_MAX]) {
    int nss;

    for (nss = NL80211_VHT_NSS_MAX - 1; nss >= 0; nss--)
        if (vht_mcs_mask[nss]) {
            return nss + 1;
        }

    return 1;
}
#endif // NEEDS PORTING

zx_status_t ath10k_mac_ext_resource_config(struct ath10k* ar, uint32_t val) {
    enum wmi_host_platform_type platform_type;
    int ret;

    if (BITARR_TEST(ar->wmi.svc_map, WMI_SERVICE_TX_MODE_DYNAMIC)) {
        platform_type = WMI_HOST_PLATFORM_LOW_PERF;
    } else {
        platform_type = WMI_HOST_PLATFORM_HIGH_PERF;
    }

    ret = ath10k_wmi_ext_resource_config(ar, platform_type, val);

    if (ret && ret != ZX_ERR_NOT_SUPPORTED) {
        ath10k_warn("failed to configure ext resource: %d\n", ret);
        return ret;
    }

    return ZX_OK;
}

/**********/
/* Crypto */
/**********/

static zx_status_t ath10k_send_key(struct ath10k_vif* arvif,
                                   wlan_key_config_t* key_config,
                                   const uint8_t* macaddr, uint32_t flags) {
    struct wmi_vdev_install_key_arg arg = {
        .vdev_id = arvif->vdev_id,
        .key_idx = key_config->key_idx,
        .key_len = key_config->key_len,
        .key_data = key_config->key,
        .key_flags = flags,
        .macaddr = macaddr,
    };

    ASSERT_MTX_HELD(&arvif->ar->conf_mutex);

    switch (key_config->cipher_type) {
    case IEEE80211_CIPHER_SUITE_CCMP_128:
    case IEEE80211_CIPHER_SUITE_CCMP_256:
        arg.key_cipher = WMI_CIPHER_AES_CCM;
        break;
    case IEEE80211_CIPHER_SUITE_TKIP:
        arg.key_cipher = WMI_CIPHER_TKIP;
        arg.key_txmic_len = 8;
        arg.key_rxmic_len = 8;
        break;
    case IEEE80211_CIPHER_SUITE_WEP_40:
    case IEEE80211_CIPHER_SUITE_WEP_104:
        arg.key_cipher = WMI_CIPHER_WEP;
        break;
    case IEEE80211_CIPHER_SUITE_CMAC_128:
    case IEEE80211_CIPHER_SUITE_CMAC_256:
        return ZX_ERR_INVALID_ARGS;
    default:
        ath10k_warn("cipher %d is not supported\n", key_config->cipher_type);
        return ZX_ERR_NOT_SUPPORTED;
    }

    return ath10k_wmi_vdev_install_key(arvif->ar, &arg);
}

static zx_status_t ath10k_install_key(struct ath10k_vif* arvif,
                                      wlan_key_config_t* key_config,
                                      const uint8_t* macaddr, uint32_t flags) {
    struct ath10k* ar = arvif->ar;
    zx_status_t ret;

    ASSERT_MTX_HELD(&ar->conf_mutex);

    sync_completion_reset(&ar->install_key_done);

    if (arvif->nohwcrypt) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    ret = ath10k_send_key(arvif, key_config, macaddr, flags);
    if (ret != ZX_OK) {
        return ret;
    }

    if (sync_completion_wait(&ar->install_key_done, ZX_SEC(3)) == ZX_ERR_TIMED_OUT) {
        ath10k_err("Timed out waiting for key install complete message\n");
        return ZX_ERR_TIMED_OUT;
    }

    return ZX_OK;
}

#if 0 // NEEDS PORTING
static int ath10k_clear_peer_keys(struct ath10k_vif* arvif,
                                  const uint8_t* addr) {
    struct ath10k* ar = arvif->ar;
    struct ath10k_peer* peer;
    int first_errno = 0;
    int ret;
    int i;
    uint32_t flags = 0;

    ASSERT_MTX_HELD(&ar->conf_mutex);

    mtx_lock(&ar->data_lock);
    peer = ath10k_peer_find(ar, arvif->vdev_id, addr);
    mtx_unlock(&ar->data_lock);

    if (!peer) {
        return -ENOENT;
    }

    for (i = 0; i < countof(peer->keys); i++) {
        if (peer->keys[i] == NULL) {
            continue;
        }

        /* key flags are not required to delete the key */
        ret = ath10k_install_key(arvif, peer->keys[i],
                                 DISABLE_KEY, addr, flags);
        if (ret < 0 && first_errno == 0) {
            first_errno = ret;
        }

        if (ret < 0)
            ath10k_warn("failed to remove peer wep key %d: %d\n",
                        i, ret);

        mtx_lock(&ar->data_lock);
        peer->keys[i] = NULL;
        mtx_unlock(&ar->data_lock);
    }

    return first_errno;
}

static int ath10k_clear_vdev_key(struct ath10k_vif* arvif,
                                 struct ieee80211_key_conf* key) {
    struct ath10k* ar = arvif->ar;
    struct ath10k_peer* peer;
    uint8_t addr[ETH_ALEN];
    int first_errno = 0;
    int ret;
    int i;
    uint32_t flags = 0;

    ASSERT_MTX_HELD(&ar->conf_mutex);

    for (;;) {
        /* since ath10k_install_key we can't hold data_lock all the
         * time, so we try to remove the keys incrementally
         */
        mtx_lock(&ar->data_lock);
        i = 0;
        list_for_each_entry(peer, &ar->peers, list) {
            for (i = 0; i < countof(peer->keys); i++) {
                if (peer->keys[i] == key) {
                    memcpy(addr, peer->addr, ETH_ALEN);
                    peer->keys[i] = NULL;
                    break;
                }
            }

            if (i < countof(peer->keys)) {
                break;
            }
        }
        mtx_unlock(&ar->data_lock);

        if (i == countof(peer->keys)) {
            break;
        }
        /* key flags are not required to delete the key */
        ret = ath10k_install_key(arvif, key, DISABLE_KEY, addr, flags);
        if (ret < 0 && first_errno == 0) {
            first_errno = ret;
        }

        if (ret)
            ath10k_warn("failed to remove key for %pM: %d\n",
                        addr, ret);
    }

    return first_errno;
}
#endif // NEEDS PORTING

/*********************/
/* General utilities */
/*********************/

static inline enum wmi_phy_mode
chan_to_phymode(wlan_channel_t* wlan_chan) {
    enum wmi_phy_mode phymode = MODE_UNKNOWN;

    if (wlan_chan->primary <= 14) {
        switch(wlan_chan->cbw) {
        case CBW20:
            phymode = MODE_11NG_HT20;
            break;
        case CBW40ABOVE:
        case CBW40BELOW:
            phymode = MODE_11NG_HT40;
            break;
        default:
            phymode = MODE_UNKNOWN;
            break;
        }
    } else {
        switch (wlan_chan->cbw) {
        case CBW20:
            phymode = MODE_11NA_HT20;
            break;
        case CBW40ABOVE:
        case CBW40BELOW:
            phymode = MODE_11NA_HT40;
            break;
        case CBW80:
            phymode = MODE_11AC_VHT80;
            break;
        case CBW160:
            phymode = MODE_11AC_VHT160;
            break;
        case CBW80P80:
            phymode = MODE_11AC_VHT80_80;
            break;
        default:
            phymode = MODE_UNKNOWN;
            break;
        }
    }

    COND_WARN(phymode == MODE_UNKNOWN);
    return phymode;
}

#if 0 // NEEDS PORTING
static uint8_t ath10k_parse_mpdudensity(uint8_t mpdudensity) {
    /*
     * 802.11n D2.0 defined values for "Minimum MPDU Start Spacing":
     *   0 for no restriction
     *   1 for 1/4 us
     *   2 for 1/2 us
     *   3 for 1 us
     *   4 for 2 us
     *   5 for 4 us
     *   6 for 8 us
     *   7 for 16 us
     */
    switch (mpdudensity) {
    case 0:
        return 0;
    case 1:
    case 2:
    case 3:
        /* Our lower layer calculations limit our precision to
         * 1 microsecond
         */
        return 1;
    case 4:
        return 2;
    case 5:
        return 4;
    case 6:
        return 8;
    case 7:
        return 16;
    default:
        return 0;
    }
}

int ath10k_mac_vif_chan(struct ieee80211_vif* vif,
                        struct cfg80211_chan_def* def) {
    struct ieee80211_chanctx_conf* conf;

    rcu_read_lock();
    conf = rcu_dereference(vif->chanctx_conf);
    if (!conf) {
        rcu_read_unlock();
        return -ENOENT;
    }

    *def = conf->def;
    rcu_read_unlock();

    return 0;
}

static void ath10k_mac_num_chanctxs_iter(struct ieee80211_hw* hw,
        struct ieee80211_chanctx_conf* conf,
        void* data) {
    int* num = data;

    (*num)++;
}

static int ath10k_mac_num_chanctxs(struct ath10k* ar) {
    int num = 0;

    ieee80211_iter_chan_contexts_atomic(ar->hw,
                                        ath10k_mac_num_chanctxs_iter,
                                        &num);

    return num;
}

static void
ath10k_mac_get_any_chandef_iter(struct ieee80211_hw* hw,
                                struct ieee80211_chanctx_conf* conf,
                                void* data) {
    struct cfg80211_chan_def** def = data;

    *def = &conf->def;
}

static int ath10k_peer_create(struct ath10k* ar,
                              struct ieee80211_vif* vif,
                              struct ieee80211_sta* sta,
                              uint32_t vdev_id,
                              const uint8_t* addr,
                              enum wmi_peer_type peer_type) {
    struct ath10k_vif* arvif;
    struct ath10k_peer* peer;
    int num_peers = 0;
    int ret;

    ASSERT_MTX_HELD(&ar->conf_mutex);

    num_peers = ar->num_peers;

    /* Each vdev consumes a peer entry as well */
    list_for_each_entry(arvif, &ar->arvifs, list)
    num_peers++;

    if (num_peers >= ar->max_num_peers) {
        return -ENOBUFS;
    }

    ret = ath10k_wmi_peer_create(ar, vdev_id, addr, peer_type);
    if (ret) {
        ath10k_warn("failed to create wmi peer %pM on vdev %i: %i\n",
                    addr, vdev_id, ret);
        return ret;
    }

    ret = ath10k_wait_for_peer_created(ar, vdev_id, addr);
    if (ret) {
        ath10k_warn("failed to wait for created wmi peer %pM on vdev %i: %i\n",
                    addr, vdev_id, ret);
        return ret;
    }

    mtx_lock(&ar->data_lock);

    peer = ath10k_peer_find(ar, vdev_id, addr);
    if (!peer) {
        mtx_unlock(&ar->data_lock);
        ath10k_warn("failed to find peer %pM on vdev %i after creation\n",
                    addr, vdev_id);
        ath10k_wmi_peer_delete(ar, vdev_id, addr);
        return -ENOENT;
    }

    peer->vif = vif;
    peer->sta = sta;

    mtx_unlock(&ar->data_lock);

    ar->num_peers++;

    return 0;
}

static int ath10k_mac_set_kickout(struct ath10k_vif* arvif) {
    struct ath10k* ar = arvif->ar;
    uint32_t param;
    int ret;

    param = ar->wmi.pdev_param->sta_kickout_th;
    ret = ath10k_wmi_pdev_set_param(ar, param,
                                    ATH10K_KICKOUT_THRESHOLD);
    if (ret) {
        ath10k_warn("failed to set kickout threshold on vdev %i: %d\n",
                    arvif->vdev_id, ret);
        return ret;
    }

    param = ar->wmi.vdev_param->ap_keepalive_min_idle_inactive_time_secs;
    ret = ath10k_wmi_vdev_set_param(ar, arvif->vdev_id, param,
                                    ATH10K_KEEPALIVE_MIN_IDLE);
    if (ret) {
        ath10k_warn("failed to set keepalive minimum idle time on vdev %i: %d\n",
                    arvif->vdev_id, ret);
        return ret;
    }

    param = ar->wmi.vdev_param->ap_keepalive_max_idle_inactive_time_secs;
    ret = ath10k_wmi_vdev_set_param(ar, arvif->vdev_id, param,
                                    ATH10K_KEEPALIVE_MAX_IDLE);
    if (ret) {
        ath10k_warn("failed to set keepalive maximum idle time on vdev %i: %d\n",
                    arvif->vdev_id, ret);
        return ret;
    }

    param = ar->wmi.vdev_param->ap_keepalive_max_unresponsive_time_secs;
    ret = ath10k_wmi_vdev_set_param(ar, arvif->vdev_id, param,
                                    ATH10K_KEEPALIVE_MAX_UNRESPONSIVE);
    if (ret) {
        ath10k_warn("failed to set keepalive maximum unresponsive time on vdev %i: %d\n",
                    arvif->vdev_id, ret);
        return ret;
    }

    return 0;
}

static int ath10k_mac_set_rts(struct ath10k_vif* arvif, uint32_t value) {
    struct ath10k* ar = arvif->ar;
    uint32_t vdev_param;

    vdev_param = ar->wmi.vdev_param->rts_threshold;
    return ath10k_wmi_vdev_set_param(ar, arvif->vdev_id, vdev_param, value);
}

static int ath10k_peer_delete(struct ath10k* ar, uint32_t vdev_id, const uint8_t* addr) {
    int ret;

    ASSERT_MTX_HELD(&ar->conf_mutex);

    ret = ath10k_wmi_peer_delete(ar, vdev_id, addr);
    if (ret) {
        return ret;
    }

    ret = ath10k_wait_for_peer_deleted(ar, vdev_id, addr);
    if (ret) {
        return ret;
    }

    ar->num_peers--;

    return 0;
}

static void ath10k_peer_cleanup(struct ath10k* ar, uint32_t vdev_id) {
    struct ath10k_peer* peer, *tmp;
    int peer_id;
    int i;

    ASSERT_MTX_HELD(&ar->conf_mutex);

    mtx_lock(&ar->data_lock);
    list_for_each_entry_safe(peer, tmp, &ar->peers, list) {
        if (peer->vdev_id != vdev_id) {
            continue;
        }

        ath10k_warn("removing stale peer %pM from vdev_id %d\n",
                    peer->addr, vdev_id);

        for_each_set_bit(peer_id, peer->peer_ids,
                         ATH10K_MAX_NUM_PEER_IDS) {
            ar->peer_map[peer_id] = NULL;
        }

        /* Double check that peer is properly un-referenced from
         * the peer_map
         */
        for (i = 0; i < countof(ar->peer_map); i++) {
            if (ar->peer_map[i] == peer) {
                ath10k_warn("removing stale peer_map entry for %pM (ptr %pK idx %d)\n",
                            peer->addr, peer, i);
                ar->peer_map[i] = NULL;
            }
        }

        list_del(&peer->list);
        kfree(peer);
        ar->num_peers--;
    }
    mtx_unlock(&ar->data_lock);
}

static void ath10k_peer_cleanup_all(struct ath10k* ar) {
    struct ath10k_peer* peer, *tmp;
    int i;

    ASSERT_MTX_HELD(&ar->conf_mutex);

    mtx_lock(&ar->data_lock);
    list_for_each_entry_safe(peer, tmp, &ar->peers, list) {
        list_del(&peer->list);
        kfree(peer);
    }

    for (i = 0; i < countof(ar->peer_map); i++) {
        ar->peer_map[i] = NULL;
    }

    mtx_unlock(&ar->data_lock);

    ar->num_peers = 0;
    ar->num_stations = 0;
}

static int ath10k_mac_tdls_peer_update(struct ath10k* ar, uint32_t vdev_id,
                                       struct ieee80211_sta* sta,
                                       enum wmi_tdls_peer_state state) {
    int ret;
    struct wmi_tdls_peer_update_cmd_arg arg = {};
    struct wmi_tdls_peer_capab_arg cap = {};
    struct wmi_channel_arg chan_arg = {};

    ASSERT_MTX_HELD(&ar->conf_mutex);

    arg.vdev_id = vdev_id;
    arg.peer_state = state;
    memcpy(arg.addr, sta->addr, ETH_ALEN);

    cap.peer_max_sp = sta->max_sp;
    cap.peer_uapsd_queues = sta->uapsd_queues;

    if (state == WMI_TDLS_PEER_STATE_CONNECTED &&
            !sta->tdls_initiator) {
        cap.is_peer_responder = 1;
    }

    ret = ath10k_wmi_tdls_peer_update(ar, &arg, &cap, &chan_arg);
    if (ret) {
        ath10k_warn("failed to update tdls peer %pM on vdev %i: %i\n",
                    arg.addr, vdev_id, ret);
        return ret;
    }

    return 0;
}

/************************/
/* Interface management */
/************************/

void ath10k_mac_vif_beacon_free(struct ath10k_vif* arvif) {
    struct ath10k* ar = arvif->ar;

    ASSERT_MTX_HELD(&ar->data_lock);

    if (!arvif->beacon) {
        return;
    }

    if (!arvif->beacon_buf)
        dma_unmap_single(ar->dev, ATH10K_SKB_CB(arvif->beacon)->paddr,
                         arvif->beacon->len, DMA_TO_DEVICE);

    if (COND_WARN(arvif->beacon_state != ATH10K_BEACON_SCHEDULED &&
                  arvif->beacon_state != ATH10K_BEACON_SENT)) {
        return;
    }

    dev_kfree_skb_any(arvif->beacon);

    arvif->beacon = NULL;
    arvif->beacon_state = ATH10K_BEACON_SCHEDULED;
}

static void ath10k_mac_vif_beacon_cleanup(struct ath10k_vif* arvif) {
    struct ath10k* ar = arvif->ar;

    ASSERT_MTX_HELD(&ar->data_lock);

    ath10k_mac_vif_beacon_free(arvif);

    if (arvif->beacon_buf) {
        dma_free_coherent(ar->dev, IEEE80211_MAX_FRAME_LEN,
                          arvif->beacon_buf, arvif->beacon_paddr);
        arvif->beacon_buf = NULL;
    }
}
#endif // NEEDS PORTING

static inline zx_status_t ath10k_vdev_setup_sync(struct ath10k* ar) {

    ASSERT_MTX_HELD(&ar->conf_mutex);

    if (BITARR_TEST(ar->dev_flags, ATH10K_FLAG_CRASH_FLUSH)) {
        return ZX_ERR_BAD_STATE;
    }

    if (sync_completion_wait(&ar->vdev_setup_done, ATH10K_VDEV_SETUP_TIMEOUT) == ZX_ERR_TIMED_OUT) {
        return ZX_ERR_TIMED_OUT;
    }

    return ZX_OK;
}

#if 0 // NEEDS PORTING
static int ath10k_monitor_vdev_start(struct ath10k* ar, int vdev_id) {
    struct cfg80211_chan_def* chandef = NULL;
    struct ieee80211_channel* channel = NULL;
    struct wmi_vdev_start_request_arg arg = {};
    int ret = 0;

    ASSERT_MTX_HELD(&ar->conf_mutex);

    ieee80211_iter_chan_contexts_atomic(ar->hw,
                                        ath10k_mac_get_any_chandef_iter,
                                        &chandef);
    if (COND_WARN_ONCE(!chandef)) {
        return -ENOENT;
    }

    channel = chandef->chan;

    arg.vdev_id = vdev_id;
    arg.channel.freq = channel->center_freq;
    arg.channel.band_center_freq1 = chandef->center_freq1;
    arg.channel.band_center_freq2 = chandef->center_freq2;

    /* TODO setup this dynamically, what in case we
     * don't have any vifs?
     */
    arg.channel.mode = chan_to_phymode(chandef);
    arg.channel.chan_radar =
        !!(channel->flags & IEEE80211_CHAN_RADAR);

    arg.channel.min_power = 0;
    arg.channel.max_power = channel->max_power * 2;
    arg.channel.max_reg_power = channel->max_reg_power * 2;
    arg.channel.max_antenna_gain = channel->max_antenna_gain * 2;

    sync_completion_reset(&ar->vdev_setup_done);

    ret = ath10k_wmi_vdev_start(ar, &arg);
    if (ret) {
        ath10k_warn("failed to request monitor vdev %i start: %d\n",
                    vdev_id, ret);
        return ret;
    }

    ret = ath10k_vdev_setup_sync(ar);
    if (ret) {
        ath10k_warn("failed to synchronize setup for monitor vdev %i start: %d\n",
                    vdev_id, ret);
        return ret;
    }

    ret = ath10k_wmi_vdev_up(ar, vdev_id, 0, ar->mac_addr);
    if (ret) {
        ath10k_warn("failed to put up monitor vdev %i: %d\n",
                    vdev_id, ret);
        goto vdev_stop;
    }

    ar->monitor_vdev_id = vdev_id;

    ath10k_dbg(ar, ATH10K_DBG_MAC, "mac monitor vdev %i started\n",
               ar->monitor_vdev_id);
    return 0;

vdev_stop:
    ret = ath10k_wmi_vdev_stop(ar, ar->monitor_vdev_id);
    if (ret)
        ath10k_warn("failed to stop monitor vdev %i after start failure: %d\n",
                    ar->monitor_vdev_id, ret);

    return ret;
}

static int ath10k_monitor_vdev_stop(struct ath10k* ar) {
    int ret = 0;

    ASSERT_MTX_HELD(&ar->conf_mutex);

    ret = ath10k_wmi_vdev_down(ar, ar->monitor_vdev_id);
    if (ret)
        ath10k_warn("failed to put down monitor vdev %i: %d\n",
                    ar->monitor_vdev_id, ret);

    sync_completion_reset(&ar->vdev_setup_done);

    ret = ath10k_wmi_vdev_stop(ar, ar->monitor_vdev_id);
    if (ret)
        ath10k_warn("failed to to request monitor vdev %i stop: %d\n",
                    ar->monitor_vdev_id, ret);

    ret = ath10k_vdev_setup_sync(ar);
    if (ret)
        ath10k_warn("failed to synchronize monitor vdev %i stop: %d\n",
                    ar->monitor_vdev_id, ret);

    ath10k_dbg(ar, ATH10K_DBG_MAC, "mac monitor vdev %i stopped\n",
               ar->monitor_vdev_id);
    return ret;
}

static int ath10k_monitor_vdev_create(struct ath10k* ar) {
    int bit, ret = 0;

    ASSERT_MTX_HELD(&ar->conf_mutex);

    if (ar->free_vdev_map == 0) {
        ath10k_warn("failed to find free vdev id for monitor vdev\n");
        return -ENOMEM;
    }

    bit = __ffs64(ar->free_vdev_map);

    ar->monitor_vdev_id = bit;

    ret = ath10k_wmi_vdev_create(ar, ar->monitor_vdev_id,
                                 WMI_VDEV_TYPE_MONITOR,
                                 0, ar->mac_addr);
    if (ret) {
        ath10k_warn("failed to request monitor vdev %i creation: %d\n",
                    ar->monitor_vdev_id, ret);
        return ret;
    }

    ar->free_vdev_map &= ~(1LL << ar->monitor_vdev_id);
    ath10k_dbg(ar, ATH10K_DBG_MAC, "mac monitor vdev %d created\n",
               ar->monitor_vdev_id);

    return 0;
}

static int ath10k_monitor_vdev_delete(struct ath10k* ar) {
    int ret = 0;

    ASSERT_MTX_HELD(&ar->conf_mutex);

    ret = ath10k_wmi_vdev_delete(ar, ar->monitor_vdev_id);
    if (ret) {
        ath10k_warn("failed to request wmi monitor vdev %i removal: %d\n",
                    ar->monitor_vdev_id, ret);
        return ret;
    }

    ar->free_vdev_map |= 1LL << ar->monitor_vdev_id;

    ath10k_dbg(ar, ATH10K_DBG_MAC, "mac monitor vdev %d deleted\n",
               ar->monitor_vdev_id);
    return ret;
}

static int ath10k_monitor_start(struct ath10k* ar) {
    int ret;

    ASSERT_MTX_HELD(&ar->conf_mutex);

    ret = ath10k_monitor_vdev_create(ar);
    if (ret) {
        ath10k_warn("failed to create monitor vdev: %d\n", ret);
        return ret;
    }

    ret = ath10k_monitor_vdev_start(ar, ar->monitor_vdev_id);
    if (ret) {
        ath10k_warn("failed to start monitor vdev: %d\n", ret);
        ath10k_monitor_vdev_delete(ar);
        return ret;
    }

    ar->monitor_started = true;
    ath10k_dbg(ar, ATH10K_DBG_MAC, "mac monitor started\n");

    return 0;
}

static int ath10k_monitor_stop(struct ath10k* ar) {
    int ret;

    ASSERT_MTX_HELD(&ar->conf_mutex);

    ret = ath10k_monitor_vdev_stop(ar);
    if (ret) {
        ath10k_warn("failed to stop monitor vdev: %d\n", ret);
        return ret;
    }

    ret = ath10k_monitor_vdev_delete(ar);
    if (ret) {
        ath10k_warn("failed to delete monitor vdev: %d\n", ret);
        return ret;
    }

    ar->monitor_started = false;
    ath10k_dbg(ar, ATH10K_DBG_MAC, "mac monitor stopped\n");

    return 0;
}

static bool ath10k_mac_monitor_vdev_is_needed(struct ath10k* ar) {
    int num_ctx;

    /* At least one chanctx is required to derive a channel to start
     * monitor vdev on.
     */
    num_ctx = ath10k_mac_num_chanctxs(ar);
    if (num_ctx == 0) {
        return false;
    }

    /* If there's already an existing special monitor interface then don't
     * bother creating another monitor vdev.
     */
    if (ar->monitor_arvif) {
        return false;
    }

    return ar->monitor ||
           (!BITARR_TEST(ar->running_fw->fw_file.fw_features,
                         ATH10K_FW_FEATURE_ALLOWS_MESH_BCAST) &&
            (ar->filter_flags & FIF_OTHER_BSS)) ||
           BITARR_TEST(&ar->dev_flags, ATH10K_CAC_RUNNING);
}

static bool ath10k_mac_monitor_vdev_is_allowed(struct ath10k* ar) {
    int num_ctx;

    num_ctx = ath10k_mac_num_chanctxs(ar);

    /* FIXME: Current interface combinations and cfg80211/mac80211 code
     * shouldn't allow this but make sure to prevent handling the following
     * case anyway since multi-channel DFS hasn't been tested at all.
     */
    if (BITARR_TEST(&ar->dev_flags, ATH10K_CAC_RUNNING) && num_ctx > 1) {
        return false;
    }

    return true;
}

static int ath10k_monitor_recalc(struct ath10k* ar) {
    bool needed;
    bool allowed;
    int ret;

    ASSERT_MTX_HELD(&ar->conf_mutex);

    needed = ath10k_mac_monitor_vdev_is_needed(ar);
    allowed = ath10k_mac_monitor_vdev_is_allowed(ar);

    ath10k_dbg(ar, ATH10K_DBG_MAC,
               "mac monitor recalc started? %d needed? %d allowed? %d\n",
               ar->monitor_started, needed, allowed);

    if (COND_WARN(needed && !allowed)) {
        if (ar->monitor_started) {
            ath10k_dbg(ar, ATH10K_DBG_MAC, "mac monitor stopping disallowed monitor\n");

            ret = ath10k_monitor_stop(ar);
            if (ret)
                ath10k_warn("failed to stop disallowed monitor: %d\n",
                            ret);
            /* not serious */
        }

        return -EPERM;
    }

    if (needed == ar->monitor_started) {
        return 0;
    }

    if (needed) {
        return ath10k_monitor_start(ar);
    } else {
        return ath10k_monitor_stop(ar);
    }
}

static bool ath10k_mac_can_set_cts_prot(struct ath10k_vif* arvif) {
    struct ath10k* ar = arvif->ar;

    ASSERT_MTX_HELD(&ar->conf_mutex);

    if (!arvif->is_started) {
        ath10k_dbg(ar, ATH10K_DBG_MAC, "defer cts setup, vdev is not ready yet\n");
        return false;
    }

    return true;
}

static int ath10k_mac_set_cts_prot(struct ath10k_vif* arvif) {
    struct ath10k* ar = arvif->ar;
    uint32_t vdev_param;

    ASSERT_MTX_HELD(&ar->conf_mutex);

    vdev_param = ar->wmi.vdev_param->protection_mode;

    ath10k_dbg(ar, ATH10K_DBG_MAC, "mac vdev %d cts_protection %d\n",
               arvif->vdev_id, arvif->use_cts_prot);

    return ath10k_wmi_vdev_set_param(ar, arvif->vdev_id, vdev_param,
                                     arvif->use_cts_prot ? 1 : 0);
}

static int ath10k_recalc_rtscts_prot(struct ath10k_vif* arvif) {
    struct ath10k* ar = arvif->ar;
    uint32_t vdev_param, rts_cts = 0;

    ASSERT_MTX_HELD(&ar->conf_mutex);

    vdev_param = ar->wmi.vdev_param->enable_rtscts;

    rts_cts |= SM(WMI_RTSCTS_ENABLED, WMI_RTSCTS_SET);

    if (arvif->num_legacy_stations > 0)
        rts_cts |= SM(WMI_RTSCTS_ACROSS_SW_RETRIES,
                      WMI_RTSCTS_PROFILE);
    else
        rts_cts |= SM(WMI_RTSCTS_FOR_SECOND_RATESERIES,
                      WMI_RTSCTS_PROFILE);

    ath10k_dbg(ar, ATH10K_DBG_MAC, "mac vdev %d recalc rts/cts prot %d\n",
               arvif->vdev_id, rts_cts);

    return ath10k_wmi_vdev_set_param(ar, arvif->vdev_id, vdev_param,
                                     rts_cts);
}

static int ath10k_start_cac(struct ath10k* ar) {
    int ret;

    ASSERT_MTX_HELD(&ar->conf_mutex);

    BITARR_SET(&ar->dev_flags, ATH10K_CAC_RUNNING);

    ret = ath10k_monitor_recalc(ar);
    if (ret) {
        ath10k_warn("failed to start monitor (cac): %d\n", ret);
        BITARR_CLEAR(&ar->dev_flags, ATH10K_CAC_RUNNING);
        return ret;
    }

    ath10k_dbg(ar, ATH10K_DBG_MAC, "mac cac start monitor vdev %d\n",
               ar->monitor_vdev_id);

    return 0;
}

static int ath10k_stop_cac(struct ath10k* ar) {
    ASSERT_MTX_HELD(&ar->conf_mutex);

    /* CAC is not running - do nothing */
    if (!BITARR_TEST(&ar->dev_flags, ATH10K_CAC_RUNNING)) {
        return 0;
    }

    BITARR_CLEAR(&ar->dev_flags, ATH10K_CAC_RUNNING);
    ath10k_monitor_stop(ar);

    ath10k_dbg(ar, ATH10K_DBG_MAC, "mac cac finished\n");

    return 0;
}

static void ath10k_mac_has_radar_iter(struct ieee80211_hw* hw,
                                      struct ieee80211_chanctx_conf* conf,
                                      void* data) {
    bool* ret = data;

    if (!*ret && conf->radar_enabled) {
        *ret = true;
    }
}

static bool ath10k_mac_has_radar_enabled(struct ath10k* ar) {
    bool has_radar = false;

    ieee80211_iter_chan_contexts_atomic(ar->hw,
                                        ath10k_mac_has_radar_iter,
                                        &has_radar);

    return has_radar;
}

static void ath10k_recalc_radar_detection(struct ath10k* ar) {
    int ret;

    ASSERT_MTX_HELD(&ar->conf_mutex);

    ath10k_stop_cac(ar);

    if (!ath10k_mac_has_radar_enabled(ar)) {
        return;
    }

    if (ar->num_started_vdevs > 0) {
        return;
    }

    ret = ath10k_start_cac(ar);
    if (ret) {
        /*
         * Not possible to start CAC on current channel so starting
         * radiation is not allowed, make this channel DFS_UNAVAILABLE
         * by indicating that radar was detected.
         */
        ath10k_warn("failed to start CAC: %d\n", ret);
        ieee80211_radar_detected(ar->hw);
    }
}

static int ath10k_vdev_stop(struct ath10k_vif* arvif) {
    struct ath10k* ar = arvif->ar;
    int ret;

    ASSERT_MTX_HELD(&ar->conf_mutex);

    sync_completion_reset(&ar->vdev_setup_done);

    ret = ath10k_wmi_vdev_stop(ar, arvif->vdev_id);
    if (ret) {
        ath10k_warn("failed to stop WMI vdev %i: %d\n",
                    arvif->vdev_id, ret);
        return ret;
    }

    ret = ath10k_vdev_setup_sync(ar);
    if (ret) {
        ath10k_warn("failed to synchronize setup for vdev %i: %d\n",
                    arvif->vdev_id, ret);
        return ret;
    }

    COND_WARN(ar->num_started_vdevs == 0);

    if (ar->num_started_vdevs != 0) {
        ar->num_started_vdevs--;
        ath10k_recalc_radar_detection(ar);
    }

    return ret;
}
#endif // NEEDS PORTING

static zx_status_t ath10k_lookup_chan(uint8_t wlan_chan, const struct ath10k_channel** ath_chan) {
    // TODO: create channel -> channel info map
    for (unsigned band_ndx = 0; band_ndx < countof(ath10k_supported_bands); band_ndx++) {
        const struct ath10k_band* band = &ath10k_supported_bands[band_ndx];
        for (unsigned ch_ndx = 0; ch_ndx < band->n_channels; ch_ndx++) {
            const struct ath10k_channel* ch = &band->channels[ch_ndx];
            if (ch->hw_value == wlan_chan) {
                *ath_chan = ch;
                return ZX_OK;
            }
        }
    }
    return ZX_ERR_NOT_FOUND;
}

static zx_status_t ath10k_vdev_start_restart(struct ath10k_vif* arvif,
                                             wlan_channel_t* chandef,
                                             bool restart) {
    struct ath10k* ar = arvif->ar;

    ASSERT_MTX_HELD(&ar->conf_mutex);

    const struct ath10k_channel* primary_chan;
    zx_status_t status = ath10k_lookup_chan(chandef->primary, &primary_chan);

    if (status != ZX_OK) {
        ath10k_warn("unable to find primary channel %d\n", chandef->primary);
        return status;
    }

    const struct ath10k_channel* secondary_chan;
    if (chandef->cbw == CBW80P80) {
        status = ath10k_lookup_chan(chandef->secondary80, &secondary_chan);

        if (status != ZX_OK) {
            ath10k_warn("unable to find secondary channel %d\n", chandef->secondary80);
            return status;
        }
    }

    struct wmi_vdev_start_request_arg arg = {};

    sync_completion_reset(&ar->vdev_setup_done);

    arg.vdev_id = arvif->vdev_id;
    arg.dtim_period = arvif->dtim_period;
    arg.bcn_intval = arvif->beacon_interval;

    arg.channel.freq = primary_chan->center_freq;

    switch(chandef->cbw) {
    case CBW20:
        arg.channel.band_center_freq1 = primary_chan->center_freq;
        break;
    case CBW40ABOVE:
        arg.channel.band_center_freq1 = primary_chan->center_freq + 10;
        break;
    case CBW40BELOW:
        arg.channel.band_center_freq1 = primary_chan->center_freq - 10;
        break;
    case CBW80:
    case CBW80P80:
        arg.channel.band_center_freq1 = primary_chan->center_freq + 30;
        break;
    case CBW160:
        arg.channel.band_center_freq1 = primary_chan->center_freq + 70;
        break;
    default:
        ZX_DEBUG_ASSERT(0);
        ath10k_err("attempt to start vdev %d with invalid CBW %d\n",
                   arvif->vdev_id, chandef->cbw);
        return ZX_ERR_INVALID_ARGS;
    }

    arg.channel.mode = chan_to_phymode(chandef);

    arg.channel.min_power = 0;
    arg.channel.max_power = primary_chan->max_power * 2;
    arg.channel.max_reg_power = primary_chan->max_reg_power * 2;
    arg.channel.max_antenna_gain = primary_chan->max_antenna_gain * 2;

#if 0 // NEEDS PORTING
    if (arvif->vdev_type == WMI_VDEV_TYPE_AP) {
        arg.ssid = arvif->u.ap.ssid;
        arg.ssid_len = arvif->u.ap.ssid_len;
        arg.hidden_ssid = arvif->u.ap.hidden_ssid;

        /* For now allow DFS for AP mode */
        arg.channel.chan_radar =
            !!(chandef->chan->flags & IEEE80211_CHAN_RADAR);
    } else if (arvif->vdev_type == WMI_VDEV_TYPE_IBSS) {
        arg.ssid = arvif->vif->bss_conf.ssid;
        arg.ssid_len = arvif->vif->bss_conf.ssid_len;
    }
#endif // NEEDS PORTING

    ath10k_dbg(ar, ATH10K_DBG_MAC,
               "mac vdev %d start center_freq %d phymode %s\n",
               arg.vdev_id, arg.channel.freq,
               ath10k_wmi_phymode_str(arg.channel.mode));

    if (restart) {
        status = ath10k_wmi_vdev_restart(ar, &arg);
    } else {
        status = ath10k_wmi_vdev_start(ar, &arg);
    }

    if (status != ZX_OK) {
        ath10k_warn("failed to start WMI vdev %i: %s\n",
                    arg.vdev_id, zx_status_get_string(status));
        return status;
    }

    // TODO: We really don't want to block, but if we don't we have no
    // confirmation that the channel change actually went through.
    status = ath10k_vdev_setup_sync(ar);
    if (status != ZX_OK) {
        ath10k_warn("failed to synchronize setup for vdev %i restart %d: %s\n",
                    arg.vdev_id, restart, zx_status_get_string(status));
        return status;
    }

    ar->num_started_vdevs++;
#if 0 // NEEDS PORTING
    ath10k_recalc_radar_detection(ar);
#endif // NEEDS PORTING

    return status;
}

static zx_status_t ath10k_vdev_start(struct ath10k_vif* arvif, wlan_channel_t* def) {
    return ath10k_vdev_start_restart(arvif, def, false);
}

static zx_status_t ath10k_vdev_restart(struct ath10k_vif* arvif, wlan_channel_t* def) {
    return ath10k_vdev_start_restart(arvif, def, true);
}

#if 0 // NEEDS PORTING
static int ath10k_mac_setup_bcn_p2p_ie(struct ath10k_vif* arvif,
                                       struct sk_buff* bcn) {
    struct ath10k* ar = arvif->ar;
    struct ieee80211_mgmt* mgmt;
    const uint8_t* p2p_ie;
    int ret;

    if (arvif->vif->type != NL80211_IFTYPE_AP || !arvif->vif->p2p) {
        return 0;
    }

    mgmt = (void*)bcn->data;
    p2p_ie = cfg80211_find_vendor_ie(WLAN_OUI_WFA, WLAN_OUI_TYPE_WFA_P2P,
                                     mgmt->u.beacon.variable,
                                     bcn->len - (mgmt->u.beacon.variable -
                                             bcn->data));
    if (!p2p_ie) {
        return -ENOENT;
    }

    ret = ath10k_wmi_p2p_go_bcn_ie(ar, arvif->vdev_id, p2p_ie);
    if (ret) {
        ath10k_warn("failed to submit p2p go bcn ie for vdev %i: %d\n",
                    arvif->vdev_id, ret);
        return ret;
    }

    return 0;
}

static int ath10k_mac_remove_vendor_ie(struct sk_buff* skb, unsigned int oui,
                                       uint8_t oui_type, size_t ie_offset) {
    size_t len;
    const uint8_t* next;
    const uint8_t* end;
    uint8_t* ie;

    if (COND_WARN(skb->len < ie_offset)) {
        return -EINVAL;
    }

    ie = (uint8_t*)cfg80211_find_vendor_ie(oui, oui_type,
                                           skb->data + ie_offset,
                                           skb->len - ie_offset);
    if (!ie) {
        return -ENOENT;
    }

    len = ie[1] + 2;
    end = skb->data + skb->len;
    next = ie + len;

    if (COND_WARN(next > end)) {
        return -EINVAL;
    }

    memmove(ie, next, end - next);
    skb_trim(skb, skb->len - len);

    return 0;
}

static int ath10k_mac_setup_bcn_tmpl(struct ath10k_vif* arvif) {
    struct ath10k* ar = arvif->ar;
    struct ieee80211_hw* hw = ar->hw;
    struct ieee80211_vif* vif = arvif->vif;
    struct ieee80211_mutable_offsets offs = {};
    struct sk_buff* bcn;
    int ret;

    if (!BITARR_TEST(ar->wmi.svc_map, WMI_SERVICE_BEACON_OFFLOAD)) {
        return 0;
    }

    if (arvif->vdev_type != WMI_VDEV_TYPE_AP &&
            arvif->vdev_type != WMI_VDEV_TYPE_IBSS) {
        return 0;
    }

    bcn = ieee80211_beacon_get_template(hw, vif, &offs);
    if (!bcn) {
        ath10k_warn("failed to get beacon template from mac80211\n");
        return -EPERM;
    }

    ret = ath10k_mac_setup_bcn_p2p_ie(arvif, bcn);
    if (ret) {
        ath10k_warn("failed to setup p2p go bcn ie: %d\n", ret);
        kfree_skb(bcn);
        return ret;
    }

    /* P2P IE is inserted by firmware automatically (as configured above)
     * so remove it from the base beacon template to avoid duplicate P2P
     * IEs in beacon frames.
     */
    ath10k_mac_remove_vendor_ie(bcn, WLAN_OUI_WFA, WLAN_OUI_TYPE_WFA_P2P,
                                offsetof(struct ieee80211_mgmt,
                                         u.beacon.variable));

    ret = ath10k_wmi_bcn_tmpl(ar, arvif->vdev_id, offs.tim_offset, bcn, 0,
                              0, NULL, 0);
    kfree_skb(bcn);

    if (ret) {
        ath10k_warn("failed to submit beacon template command: %d\n",
                    ret);
        return ret;
    }

    return 0;
}

static int ath10k_mac_setup_prb_tmpl(struct ath10k_vif* arvif) {
    struct ath10k* ar = arvif->ar;
    struct ieee80211_hw* hw = ar->hw;
    struct ieee80211_vif* vif = arvif->vif;
    struct sk_buff* prb;
    int ret;

    if (!BITARR_TEST(ar->wmi.svc_map, WMI_SERVICE_BEACON_OFFLOAD)) {
        return 0;
    }

    if (arvif->vdev_type != WMI_VDEV_TYPE_AP) {
        return 0;
    }

    prb = ieee80211_proberesp_get(hw, vif);
    if (!prb) {
        ath10k_warn("failed to get probe resp template from mac80211\n");
        return -EPERM;
    }

    ret = ath10k_wmi_prb_tmpl(ar, arvif->vdev_id, prb);
    kfree_skb(prb);

    if (ret) {
        ath10k_warn("failed to submit probe resp template command: %d\n",
                    ret);
        return ret;
    }

    return 0;
}

static int ath10k_mac_vif_fix_hidden_ssid(struct ath10k_vif* arvif) {
    struct ath10k* ar = arvif->ar;
    struct cfg80211_chan_def def;
    int ret;

    /* When originally vdev is started during assign_vif_chanctx() some
     * information is missing, notably SSID. Firmware revisions with beacon
     * offloading require the SSID to be provided during vdev (re)start to
     * handle hidden SSID properly.
     *
     * Vdev restart must be done after vdev has been both started and
     * upped. Otherwise some firmware revisions (at least 10.2) fail to
     * deliver vdev restart response event causing timeouts during vdev
     * syncing in ath10k.
     *
     * Note: The vdev down/up and template reinstallation could be skipped
     * since only wmi-tlv firmware are known to have beacon offload and
     * wmi-tlv doesn't seem to misbehave like 10.2 wrt vdev restart
     * response delivery. It's probably more robust to keep it as is.
     */
    if (!BITARR_TEST(ar->wmi.svc_map, WMI_SERVICE_BEACON_OFFLOAD)) {
        return 0;
    }

    if (COND_WARN(!arvif->is_started)) {
        return -EINVAL;
    }

    if (COND_WARN(!arvif->is_up)) {
        return -EINVAL;
    }

    if (COND_WARN(ath10k_mac_vif_chan(arvif->vif, &def))) {
        return -EINVAL;
    }

    ret = ath10k_wmi_vdev_down(ar, arvif->vdev_id);
    if (ret) {
        ath10k_warn("failed to bring down ap vdev %i: %d\n",
                    arvif->vdev_id, ret);
        return ret;
    }

    /* Vdev down reset beacon & presp templates. Reinstall them. Otherwise
     * firmware will crash upon vdev up.
     */

    ret = ath10k_mac_setup_bcn_tmpl(arvif);
    if (ret) {
        ath10k_warn("failed to update beacon template: %d\n", ret);
        return ret;
    }

    ret = ath10k_mac_setup_prb_tmpl(arvif);
    if (ret) {
        ath10k_warn("failed to update presp template: %d\n", ret);
        return ret;
    }

    ret = ath10k_vdev_restart(arvif, &def);
    if (ret) {
        ath10k_warn("failed to restart ap vdev %i: %d\n",
                    arvif->vdev_id, ret);
        return ret;
    }

    ret = ath10k_wmi_vdev_up(arvif->ar, arvif->vdev_id, arvif->aid,
                             arvif->bssid);
    if (ret) {
        ath10k_warn("failed to bring up ap vdev %i: %d\n",
                    arvif->vdev_id, ret);
        return ret;
    }

    return 0;
}

static void ath10k_control_beaconing(struct ath10k_vif* arvif,
                                     struct ieee80211_bss_conf* info) {
    struct ath10k* ar = arvif->ar;
    int ret = 0;

    ASSERT_MTX_HELD(&arvif->ar->conf_mutex);

    if (!info->enable_beacon) {
        ret = ath10k_wmi_vdev_down(ar, arvif->vdev_id);
        if (ret)
            ath10k_warn("failed to down vdev_id %i: %d\n",
                        arvif->vdev_id, ret);

        arvif->is_up = false;

        mtx_lock(&arvif->ar->data_lock);
        ath10k_mac_vif_beacon_free(arvif);
        mtx_unlock(&arvif->ar->data_lock);

        return;
    }

    arvif->tx_seq_no = 0x1000;

    arvif->aid = 0;
    memcpy(arvif->bssid, info->bssid, ETH_ALEN);

    ret = ath10k_wmi_vdev_up(arvif->ar, arvif->vdev_id, arvif->aid,
                             arvif->bssid);
    if (ret) {
        ath10k_warn("failed to bring up vdev %d: %i\n",
                    arvif->vdev_id, ret);
        return;
    }

    arvif->is_up = true;

    ret = ath10k_mac_vif_fix_hidden_ssid(arvif);
    if (ret) {
        ath10k_warn("failed to fix hidden ssid for vdev %i, expect trouble: %d\n",
                    arvif->vdev_id, ret);
        return;
    }

    ath10k_dbg(ar, ATH10K_DBG_MAC, "mac vdev %d up\n", arvif->vdev_id);
}

static void ath10k_control_ibss(struct ath10k_vif* arvif,
                                struct ieee80211_bss_conf* info,
                                const uint8_t self_peer[ETH_ALEN]) {
    struct ath10k* ar = arvif->ar;
    uint32_t vdev_param;
    int ret = 0;

    ASSERT_MTX_HELD(&arvif->ar->conf_mutex);

    if (!info->ibss_joined) {
        if (is_zero_ether_addr(arvif->bssid)) {
            return;
        }

        eth_zero_addr(arvif->bssid);

        return;
    }

    vdev_param = arvif->ar->wmi.vdev_param->atim_window;
    ret = ath10k_wmi_vdev_set_param(arvif->ar, arvif->vdev_id, vdev_param,
                                    ATH10K_DEFAULT_ATIM);
    if (ret)
        ath10k_warn("failed to set IBSS ATIM for vdev %d: %d\n",
                    arvif->vdev_id, ret);
}

static int ath10k_mac_vif_recalc_ps_wake_threshold(struct ath10k_vif* arvif) {
    struct ath10k* ar = arvif->ar;
    uint32_t param;
    uint32_t value;
    int ret;

    ASSERT_MTX_HELD(&arvif->ar->conf_mutex);

    if (arvif->u.sta.uapsd) {
        value = WMI_STA_PS_TX_WAKE_THRESHOLD_NEVER;
    } else {
        value = WMI_STA_PS_TX_WAKE_THRESHOLD_ALWAYS;
    }

    param = WMI_STA_PS_PARAM_TX_WAKE_THRESHOLD;
    ret = ath10k_wmi_set_sta_ps_param(ar, arvif->vdev_id, param, value);
    if (ret) {
        ath10k_warn("failed to submit ps wake threshold %u on vdev %i: %d\n",
                    value, arvif->vdev_id, ret);
        return ret;
    }

    return 0;
}

static int ath10k_mac_vif_recalc_ps_poll_count(struct ath10k_vif* arvif) {
    struct ath10k* ar = arvif->ar;
    uint32_t param;
    uint32_t value;
    int ret;

    ASSERT_MTX_HELD(&arvif->ar->conf_mutex);

    if (arvif->u.sta.uapsd) {
        value = WMI_STA_PS_PSPOLL_COUNT_UAPSD;
    } else {
        value = WMI_STA_PS_PSPOLL_COUNT_NO_MAX;
    }

    param = WMI_STA_PS_PARAM_PSPOLL_COUNT;
    ret = ath10k_wmi_set_sta_ps_param(ar, arvif->vdev_id,
                                      param, value);
    if (ret) {
        ath10k_warn("failed to submit ps poll count %u on vdev %i: %d\n",
                    value, arvif->vdev_id, ret);
        return ret;
    }

    return 0;
}

static int ath10k_mac_num_vifs_started(struct ath10k* ar) {
    struct ath10k_vif* arvif;
    int num = 0;

    ASSERT_MTX_HELD(&ar->conf_mutex);

    list_for_each_entry(arvif, &ar->arvifs, list)
    if (arvif->is_started) {
        num++;
    }

    return num;
}

static int ath10k_mac_vif_setup_ps(struct ath10k_vif* arvif) {
    struct ath10k* ar = arvif->ar;
    struct ieee80211_vif* vif = arvif->vif;
    struct ieee80211_conf* conf = &ar->hw->conf;
    enum wmi_sta_powersave_param param;
    enum wmi_sta_ps_mode psmode;
    int ret;
    int ps_timeout;
    bool enable_ps;

    ASSERT_MTX_HELD(&arvif->ar->conf_mutex);

    if (arvif->vif->type != NL80211_IFTYPE_STATION) {
        return 0;
    }

    enable_ps = arvif->ps;

    if (enable_ps && ath10k_mac_num_vifs_started(ar) > 1 &&
            !BITARR_TEST(ar->running_fw->fw_file.fw_features,
                         ATH10K_FW_FEATURE_MULTI_VIF_PS_SUPPORT)) {
        ath10k_warn("refusing to enable ps on vdev %i: not supported by fw\n",
                    arvif->vdev_id);
        enable_ps = false;
    }

    if (!arvif->is_started) {
        /* mac80211 can update vif powersave state while disconnected.
         * Firmware doesn't behave nicely and consumes more power than
         * necessary if PS is disabled on a non-started vdev. Hence
         * force-enable PS for non-running vdevs.
         */
        psmode = WMI_STA_PS_MODE_ENABLED;
    } else if (enable_ps) {
        psmode = WMI_STA_PS_MODE_ENABLED;
        param = WMI_STA_PS_PARAM_INACTIVITY_TIME;

        ps_timeout = conf->dynamic_ps_timeout;
        if (ps_timeout == 0) {
            /* Firmware doesn't like 0 */
            ps_timeout = ieee80211_tu_to_usec(
                             vif->bss_conf.beacon_int) / 1000;
        }

        ret = ath10k_wmi_set_sta_ps_param(ar, arvif->vdev_id, param,
                                          ps_timeout);
        if (ret) {
            ath10k_warn("failed to set inactivity time for vdev %d: %i\n",
                        arvif->vdev_id, ret);
            return ret;
        }
    } else {
        psmode = WMI_STA_PS_MODE_DISABLED;
    }

    ath10k_dbg(ar, ATH10K_DBG_MAC, "mac vdev %d psmode %s\n",
               arvif->vdev_id, psmode ? "enable" : "disable");

    ret = ath10k_wmi_set_psmode(ar, arvif->vdev_id, psmode);
    if (ret) {
        ath10k_warn("failed to set PS Mode %d for vdev %d: %d\n",
                    psmode, arvif->vdev_id, ret);
        return ret;
    }

    return 0;
}

static int ath10k_mac_vif_disable_keepalive(struct ath10k_vif* arvif) {
    struct ath10k* ar = arvif->ar;
    struct wmi_sta_keepalive_arg arg = {};
    int ret;

    ASSERT_MTX_HELD(&arvif->ar->conf_mutex);

    if (arvif->vdev_type != WMI_VDEV_TYPE_STA) {
        return 0;
    }

    if (!BITARR_TEST(ar->wmi.svc_map, WMI_SERVICE_STA_KEEP_ALIVE)) {
        return 0;
    }

    /* Some firmware revisions have a bug and ignore the `enabled` field.
     * Instead use the interval to disable the keepalive.
     */
    arg.vdev_id = arvif->vdev_id;
    arg.enabled = 1;
    arg.method = WMI_STA_KEEPALIVE_METHOD_NULL_FRAME;
    arg.interval = WMI_STA_KEEPALIVE_INTERVAL_DISABLE;

    ret = ath10k_wmi_sta_keepalive(ar, &arg);
    if (ret) {
        ath10k_warn("failed to submit keepalive on vdev %i: %d\n",
                    arvif->vdev_id, ret);
        return ret;
    }

    return 0;
}

static void ath10k_mac_vif_ap_csa_count_down(struct ath10k_vif* arvif) {
    struct ath10k* ar = arvif->ar;
    struct ieee80211_vif* vif = arvif->vif;
    int ret;

    ASSERT_MTX_HELD(&arvif->ar->conf_mutex);

    if (COND_WARN(!BITARR_TEST(ar->wmi.svc_map, WMI_SERVICE_BEACON_OFFLOAD))) {
        return;
    }

    if (arvif->vdev_type != WMI_VDEV_TYPE_AP) {
        return;
    }

    if (!vif->csa_active) {
        return;
    }

    if (!arvif->is_up) {
        return;
    }

    if (!ieee80211_csa_is_complete(vif)) {
        ieee80211_csa_update_counter(vif);

        ret = ath10k_mac_setup_bcn_tmpl(arvif);
        if (ret)
            ath10k_warn("failed to update bcn tmpl during csa: %d\n",
                        ret);

        ret = ath10k_mac_setup_prb_tmpl(arvif);
        if (ret)
            ath10k_warn("failed to update prb tmpl during csa: %d\n",
                        ret);
    } else {
        ieee80211_csa_finish(vif);
    }
}

static void ath10k_mac_vif_ap_csa_work(struct work_struct* work) {
    struct ath10k_vif* arvif = container_of(work, struct ath10k_vif,
                                            ap_csa_work);
    struct ath10k* ar = arvif->ar;

    mtx_lock(&ar->conf_mutex);
    ath10k_mac_vif_ap_csa_count_down(arvif);
    mtx_unlock(&ar->conf_mutex);
}

static void ath10k_mac_handle_beacon_iter(void* data, uint8_t* mac,
                                          struct ieee80211_vif* vif) {
    struct sk_buff* skb = data;
    struct ieee80211_mgmt* mgmt = (void*)skb->data;
    struct ath10k_vif* arvif = (void*)vif->drv_priv;

    if (vif->type != NL80211_IFTYPE_STATION) {
        return;
    }

    if (!ether_addr_equal(mgmt->bssid, vif->bss_conf.bssid)) {
        return;
    }

    cancel_delayed_work(&arvif->connection_loss_work);
}

void ath10k_mac_handle_beacon(struct ath10k* ar, struct sk_buff* skb) {
    ieee80211_iterate_active_interfaces_atomic(ar->hw,
            IEEE80211_IFACE_ITER_NORMAL,
            ath10k_mac_handle_beacon_iter,
            skb);
}

static void ath10k_mac_handle_beacon_miss_iter(void* data, uint8_t* mac,
                                               struct ieee80211_vif* vif) {
    uint32_t* vdev_id = data;
    struct ath10k_vif* arvif = (void*)vif->drv_priv;
    struct ath10k* ar = arvif->ar;
    struct ieee80211_hw* hw = ar->hw;

    if (arvif->vdev_id != *vdev_id) {
        return;
    }

    if (!arvif->is_up) {
        return;
    }

    ieee80211_beacon_loss(vif);

    /* Firmware doesn't report beacon loss events repeatedly. If AP probe
     * (done by mac80211) succeeds but beacons do not resume then it
     * doesn't make sense to continue operation. Queue connection loss work
     * which can be cancelled when beacon is received.
     */
    ieee80211_queue_delayed_work(hw, &arvif->connection_loss_work,
                                 ATH10K_CONNECTION_LOSS_HZ);
}

void ath10k_mac_handle_beacon_miss(struct ath10k* ar, uint32_t vdev_id) {
    ieee80211_iterate_active_interfaces_atomic(ar->hw,
            IEEE80211_IFACE_ITER_NORMAL,
            ath10k_mac_handle_beacon_miss_iter,
            &vdev_id);
}

static void ath10k_mac_vif_sta_connection_loss_work(struct work_struct* work) {
    struct ath10k_vif* arvif = container_of(work, struct ath10k_vif,
                                            connection_loss_work.work);
    struct ieee80211_vif* vif = arvif->vif;

    if (!arvif->is_up) {
        return;
    }

    ieee80211_connection_loss(vif);
}

/**********************/
/* Station management */
/**********************/

static uint32_t ath10k_peer_assoc_h_listen_intval(struct ath10k* ar,
        struct ieee80211_vif* vif) {
    /* Some firmware revisions have unstable STA powersave when listen
     * interval is set too high (e.g. 5). The symptoms are firmware doesn't
     * generate NullFunc frames properly even if buffered frames have been
     * indicated in Beacon TIM. Firmware would seldom wake up to pull
     * buffered frames. Often pinging the device from AP would simply fail.
     *
     * As a workaround set it to 1.
     */
    if (vif->type == NL80211_IFTYPE_STATION) {
        return 1;
    }

    return ar->hw->conf.listen_interval;
}

static void ath10k_peer_assoc_h_basic(struct ath10k* ar,
                                      struct ieee80211_vif* vif,
                                      struct ieee80211_sta* sta,
                                      struct wmi_peer_assoc_complete_arg* arg) {
    struct ath10k_vif* arvif = (void*)vif->drv_priv;
    uint32_t aid;

    ASSERT_MTX_HELD(&ar->conf_mutex);

    if (vif->type == NL80211_IFTYPE_STATION) {
        aid = vif->bss_conf.aid;
    } else {
        aid = sta->aid;
    }

    memcpy(arg->addr, sta->addr, ETH_ALEN);
    arg->vdev_id = arvif->vdev_id;
    arg->peer_aid = aid;
    arg->peer_flags |= arvif->ar->wmi.peer_flags->auth;
    arg->peer_listen_intval = ath10k_peer_assoc_h_listen_intval(ar, vif);
    arg->peer_num_spatial_streams = 1;
    arg->peer_caps = vif->bss_conf.assoc_capability;
}

static void ath10k_peer_assoc_h_crypto(struct ath10k* ar,
                                       struct ieee80211_vif* vif,
                                       struct ieee80211_sta* sta,
                                       struct wmi_peer_assoc_complete_arg* arg) {
    struct ieee80211_bss_conf* info = &vif->bss_conf;
    struct cfg80211_chan_def def;
    struct cfg80211_bss* bss;
    const uint8_t* rsnie = NULL;
    const uint8_t* wpaie = NULL;

    ASSERT_MTX_HELD(&ar->conf_mutex);

    if (COND_WARN(ath10k_mac_vif_chan(vif, &def))) {
        return;
    }

    bss = cfg80211_get_bss(ar->hw->wiphy, def.chan, info->bssid, NULL, 0,
                           IEEE80211_BSS_TYPE_ANY, IEEE80211_PRIVACY_ANY);
    if (bss) {
        const struct cfg80211_bss_ies* ies;

        rcu_read_lock();
        rsnie = ieee80211_bss_get_ie(bss, WLAN_EID_RSN);

        ies = rcu_dereference(bss->ies);

        wpaie = cfg80211_find_vendor_ie(WLAN_OUI_MICROSOFT,
                                        WLAN_OUI_TYPE_MICROSOFT_WPA,
                                        ies->data,
                                        ies->len);
        rcu_read_unlock();
        cfg80211_put_bss(ar->hw->wiphy, bss);
    }

    /* FIXME: base on RSN IE/WPA IE is a correct idea? */
    if (rsnie || wpaie) {
        ath10k_dbg(ar, ATH10K_DBG_WMI, "%s: rsn ie found\n", __func__);
        arg->peer_flags |= ar->wmi.peer_flags->need_ptk_4_way;
    }

    if (wpaie) {
        ath10k_dbg(ar, ATH10K_DBG_WMI, "%s: wpa ie found\n", __func__);
        arg->peer_flags |= ar->wmi.peer_flags->need_gtk_2_way;
    }

    if (sta->mfp &&
            BITARR_TEST(ar->running_fw->fw_file.fw_features, ATH10K_FW_FEATURE_MFP_SUPPORT)) {
        arg->peer_flags |= ar->wmi.peer_flags->pmf;
    }
}

static void ath10k_peer_assoc_h_rates(struct ath10k* ar,
                                      struct ieee80211_vif* vif,
                                      struct ieee80211_sta* sta,
                                      struct wmi_peer_assoc_complete_arg* arg) {
    struct ath10k_vif* arvif = (void*)vif->drv_priv;
    struct wmi_rate_set_arg* rateset = &arg->peer_legacy_rates;
    struct cfg80211_chan_def def;
    const struct ieee80211_supported_band* sband;
    const struct ieee80211_rate* rates;
    enum nl80211_band band;
    uint32_t ratemask;
    uint8_t rate;
    int i;

    ASSERT_MTX_HELD(&ar->conf_mutex);

    if (COND_WARN(ath10k_mac_vif_chan(vif, &def))) {
        return;
    }

    band = def.chan->band;
    sband = ar->hw->wiphy->bands[band];
    ratemask = sta->supp_rates[band];
    ratemask &= arvif->bitrate_mask.control[band].legacy;
    rates = sband->bitrates;

    rateset->num_rates = 0;

    for (i = 0; i < 32; i++, ratemask >>= 1, rates++) {
        if (!(ratemask & 1)) {
            continue;
        }

        rate = ath10k_mac_bitrate_to_rate(rates->bitrate);
        rateset->rates[rateset->num_rates] = rate;
        rateset->num_rates++;
    }
}

static bool
ath10k_peer_assoc_h_ht_masked(const uint8_t ht_mcs_mask[IEEE80211_HT_MCS_MASK_LEN]) {
    int nss;

    for (nss = 0; nss < IEEE80211_HT_MCS_MASK_LEN; nss++)
        if (ht_mcs_mask[nss]) {
            return false;
        }

    return true;
}

static bool
ath10k_peer_assoc_h_vht_masked(const uint16_t vht_mcs_mask[NL80211_VHT_NSS_MAX]) {
    int nss;

    for (nss = 0; nss < NL80211_VHT_NSS_MAX; nss++)
        if (vht_mcs_mask[nss]) {
            return false;
        }

    return true;
}

static void ath10k_peer_assoc_h_ht(struct ath10k* ar,
                                   void* assoc_resp_frame,
                                   struct wmi_peer_assoc_complete_arg* arg) {
    const struct ieee80211_sta_ht_cap* ht_cap = &sta->ht_cap;
    struct ath10k_vif* arvif = (void*)vif->drv_priv;
    struct cfg80211_chan_def def;
    enum nl80211_band band;
    const uint8_t* ht_mcs_mask;
    const uint16_t* vht_mcs_mask;
    int i, n;
    uint8_t max_nss;
    uint32_t stbc;

    ASSERT_MTX_HELD(&ar->conf_mutex);

    if (COND_WARN(ath10k_mac_vif_chan(vif, &def))) {
        return;
    }

    if (!ht_cap->ht_supported) {
        return;
    }

    band = def.chan->band;
    ht_mcs_mask = arvif->bitrate_mask.control[band].ht_mcs;
    vht_mcs_mask = arvif->bitrate_mask.control[band].vht_mcs;

    if (ath10k_peer_assoc_h_ht_masked(ht_mcs_mask) &&
            ath10k_peer_assoc_h_vht_masked(vht_mcs_mask)) {
        return;
    }

    arg->peer_flags |= ar->wmi.peer_flags->ht;
    arg->peer_max_mpdu = (1 << (IEEE80211_HT_MAX_AMPDU_FACTOR +
                                ht_cap->ampdu_factor)) - 1;

    arg->peer_mpdu_density =
        ath10k_parse_mpdudensity(ht_cap->ampdu_density);

    arg->peer_ht_caps = ht_cap->cap;
    arg->peer_rate_caps |= WMI_RC_HT_FLAG;

    if (ht_cap->cap & IEEE80211_HT_CAP_LDPC_CODING) {
        arg->peer_flags |= ar->wmi.peer_flags->ldbc;
    }

    if (sta->bandwidth >= IEEE80211_STA_RX_BW_40) {
        arg->peer_flags |= ar->wmi.peer_flags->bw40;
        arg->peer_rate_caps |= WMI_RC_CW40_FLAG;
    }

    if (arvif->bitrate_mask.control[band].gi != NL80211_TXRATE_FORCE_LGI) {
        if (ht_cap->cap & IEEE80211_HT_CAP_SGI_20) {
            arg->peer_rate_caps |= WMI_RC_SGI_FLAG;
        }

        if (ht_cap->cap & IEEE80211_HT_CAP_SGI_40) {
            arg->peer_rate_caps |= WMI_RC_SGI_FLAG;
        }
    }

    if (ht_cap->cap & IEEE80211_HT_CAP_TX_STBC) {
        arg->peer_rate_caps |= WMI_RC_TX_STBC_FLAG;
        arg->peer_flags |= ar->wmi.peer_flags->stbc;
    }

    if (ht_cap->cap & IEEE80211_HT_CAP_RX_STBC) {
        stbc = ht_cap->cap & IEEE80211_HT_CAP_RX_STBC;
        stbc = stbc >> IEEE80211_HT_CAP_RX_STBC_SHIFT;
        stbc = stbc << WMI_RC_RX_STBC_FLAG_S;
        arg->peer_rate_caps |= stbc;
        arg->peer_flags |= ar->wmi.peer_flags->stbc;
    }

    if (ht_cap->mcs.rx_mask[1] && ht_cap->mcs.rx_mask[2]) {
        arg->peer_rate_caps |= WMI_RC_TS_FLAG;
    } else if (ht_cap->mcs.rx_mask[1]) {
        arg->peer_rate_caps |= WMI_RC_DS_FLAG;
    }

    for (i = 0, n = 0, max_nss = 0; i < IEEE80211_HT_MCS_MASK_LEN * 8; i++)
        if ((ht_cap->mcs.rx_mask[i / 8] & BIT(i % 8)) &&
                (ht_mcs_mask[i / 8] & BIT(i % 8))) {
            max_nss = (i / 8) + 1;
            arg->peer_ht_rates.rates[n++] = i;
        }

    /*
     * This is a workaround for HT-enabled STAs which break the spec
     * and have no HT capabilities RX mask (no HT RX MCS map).
     *
     * As per spec, in section 20.3.5 Modulation and coding scheme (MCS),
     * MCS 0 through 7 are mandatory in 20MHz with 800 ns GI at all STAs.
     *
     * Firmware asserts if such situation occurs.
     */
    if (n == 0) {
        arg->peer_ht_rates.num_rates = 8;
        for (i = 0; i < arg->peer_ht_rates.num_rates; i++) {
            arg->peer_ht_rates.rates[i] = i;
        }
    } else {
        arg->peer_ht_rates.num_rates = n;
        arg->peer_num_spatial_streams = MIN(sta->rx_nss, max_nss);
    }

    ath10k_dbg(ar, ATH10K_DBG_MAC, "mac ht peer %pM mcs cnt %d nss %d\n",
               arg->addr,
               arg->peer_ht_rates.num_rates,
               arg->peer_num_spatial_streams);
}

static int ath10k_peer_assoc_qos_ap(struct ath10k* ar,
                                    struct ath10k_vif* arvif,
                                    struct ieee80211_sta* sta) {
    uint32_t uapsd = 0;
    uint32_t max_sp = 0;
    int ret = 0;

    ASSERT_MTX_HELD(&ar->conf_mutex);

    if (sta->wme && sta->uapsd_queues) {
        ath10k_dbg(ar, ATH10K_DBG_MAC, "mac uapsd_queues 0x%x max_sp %d\n",
                   sta->uapsd_queues, sta->max_sp);

        if (sta->uapsd_queues & IEEE80211_WMM_IE_STA_QOSINFO_AC_VO)
            uapsd |= WMI_AP_PS_UAPSD_AC3_DELIVERY_EN |
                     WMI_AP_PS_UAPSD_AC3_TRIGGER_EN;
        if (sta->uapsd_queues & IEEE80211_WMM_IE_STA_QOSINFO_AC_VI)
            uapsd |= WMI_AP_PS_UAPSD_AC2_DELIVERY_EN |
                     WMI_AP_PS_UAPSD_AC2_TRIGGER_EN;
        if (sta->uapsd_queues & IEEE80211_WMM_IE_STA_QOSINFO_AC_BK)
            uapsd |= WMI_AP_PS_UAPSD_AC1_DELIVERY_EN |
                     WMI_AP_PS_UAPSD_AC1_TRIGGER_EN;
        if (sta->uapsd_queues & IEEE80211_WMM_IE_STA_QOSINFO_AC_BE)
            uapsd |= WMI_AP_PS_UAPSD_AC0_DELIVERY_EN |
                     WMI_AP_PS_UAPSD_AC0_TRIGGER_EN;

        if (sta->max_sp < MAX_WMI_AP_PS_PEER_PARAM_MAX_SP) {
            max_sp = sta->max_sp;
        }

        ret = ath10k_wmi_set_ap_ps_param(ar, arvif->vdev_id,
                                         sta->addr,
                                         WMI_AP_PS_PEER_PARAM_UAPSD,
                                         uapsd);
        if (ret) {
            ath10k_warn("failed to set ap ps peer param uapsd for vdev %i: %d\n",
                        arvif->vdev_id, ret);
            return ret;
        }

        ret = ath10k_wmi_set_ap_ps_param(ar, arvif->vdev_id,
                                         sta->addr,
                                         WMI_AP_PS_PEER_PARAM_MAX_SP,
                                         max_sp);
        if (ret) {
            ath10k_warn("failed to set ap ps peer param max sp for vdev %i: %d\n",
                        arvif->vdev_id, ret);
            return ret;
        }

        /* TODO setup this based on STA listen interval and
         * beacon interval. Currently we don't know
         * sta->listen_interval - mac80211 patch required.
         * Currently use 10 seconds
         */
        ret = ath10k_wmi_set_ap_ps_param(ar, arvif->vdev_id, sta->addr,
                                         WMI_AP_PS_PEER_PARAM_AGEOUT_TIME,
                                         10);
        if (ret) {
            ath10k_warn("failed to set ap ps peer param ageout time for vdev %i: %d\n",
                        arvif->vdev_id, ret);
            return ret;
        }
    }

    return 0;
}

static uint16_t
ath10k_peer_assoc_h_vht_limit(uint16_t tx_mcs_set,
                              const uint16_t vht_mcs_limit[NL80211_VHT_NSS_MAX]) {
    int idx_limit;
    int nss;
    uint16_t mcs_map;
    uint16_t mcs;

    for (nss = 0; nss < NL80211_VHT_NSS_MAX; nss++) {
        mcs_map = ath10k_mac_get_max_vht_mcs_map(tx_mcs_set, nss) &
                  vht_mcs_limit[nss];

        if (mcs_map) {
            idx_limit = fls(mcs_map) - 1;
        } else {
            idx_limit = -1;
        }

        switch (idx_limit) {
        case 0: /* fall through */
        case 1: /* fall through */
        case 2: /* fall through */
        case 3: /* fall through */
        case 4: /* fall through */
        case 5: /* fall through */
        case 6: /* fall through */
        default:
            /* see ath10k_mac_can_set_bitrate_mask() */
            WARN_ONCE();
        /* fall through */
        case -1:
            mcs = IEEE80211_VHT_MCS_NOT_SUPPORTED;
            break;
        case 7:
            mcs = IEEE80211_VHT_MCS_SUPPORT_0_7;
            break;
        case 8:
            mcs = IEEE80211_VHT_MCS_SUPPORT_0_8;
            break;
        case 9:
            mcs = IEEE80211_VHT_MCS_SUPPORT_0_9;
            break;
        }

        tx_mcs_set &= ~(0x3 << (nss * 2));
        tx_mcs_set |= mcs << (nss * 2);
    }

    return tx_mcs_set;
}

static void ath10k_peer_assoc_h_vht(struct ath10k* ar,
                                    struct ieee80211_vif* vif,
                                    struct ieee80211_sta* sta,
                                    struct wmi_peer_assoc_complete_arg* arg) {
    const struct ieee80211_sta_vht_cap* vht_cap = &sta->vht_cap;
    struct ath10k_vif* arvif = (void*)vif->drv_priv;
    struct cfg80211_chan_def def;
    enum nl80211_band band;
    const uint16_t* vht_mcs_mask;
    uint8_t ampdu_factor;
    uint8_t max_nss, vht_mcs;
    int i;

    if (COND_WARN(ath10k_mac_vif_chan(vif, &def))) {
        return;
    }

    if (!vht_cap->vht_supported) {
        return;
    }

    band = def.chan->band;
    vht_mcs_mask = arvif->bitrate_mask.control[band].vht_mcs;

    if (ath10k_peer_assoc_h_vht_masked(vht_mcs_mask)) {
        return;
    }

    arg->peer_flags |= ar->wmi.peer_flags->vht;

    if (def.chan->band == NL80211_BAND_2GHZ) {
        arg->peer_flags |= ar->wmi.peer_flags->vht_2g;
    }

    arg->peer_vht_caps = vht_cap->cap;

    ampdu_factor = (vht_cap->cap &
                    IEEE80211_VHT_CAP_MAX_A_MPDU_LENGTH_EXPONENT_MASK) >>
                   IEEE80211_VHT_CAP_MAX_A_MPDU_LENGTH_EXPONENT_SHIFT;

    /* Workaround: Some Netgear/Linksys 11ac APs set Rx A-MPDU factor to
     * zero in VHT IE. Using it would result in degraded throughput.
     * arg->peer_max_mpdu at this point contains HT max_mpdu so keep
     * it if VHT max_mpdu is smaller.
     */
    arg->peer_max_mpdu = max(arg->peer_max_mpdu,
                             (1U << (IEEE80211_HT_MAX_AMPDU_FACTOR +
                                     ampdu_factor)) - 1);

    if (sta->bandwidth == IEEE80211_STA_RX_BW_80) {
        arg->peer_flags |= ar->wmi.peer_flags->bw80;
    }

    if (sta->bandwidth == IEEE80211_STA_RX_BW_160) {
        arg->peer_flags |= ar->wmi.peer_flags->bw160;
    }

    /* Calculate peer NSS capability from VHT capabilities if STA
     * supports VHT.
     */
    for (i = 0, max_nss = 0, vht_mcs = 0; i < NL80211_VHT_NSS_MAX; i++) {
        vht_mcs = vht_cap->vht_mcs.rx_mcs_map >>
                  (2 * i) & 3;

        if ((vht_mcs != IEEE80211_VHT_MCS_NOT_SUPPORTED) &&
                vht_mcs_mask[i]) {
            max_nss = i + 1;
        }
    }
    arg->peer_num_spatial_streams = MIN(sta->rx_nss, max_nss);
    arg->peer_vht_rates.rx_max_rate =
        vht_cap->vht_mcs.rx_highest;
    arg->peer_vht_rates.rx_mcs_set =
        vht_cap->vht_mcs.rx_mcs_map;
    arg->peer_vht_rates.tx_max_rate =
        vht_cap->vht_mcs.tx_highest;
    arg->peer_vht_rates.tx_mcs_set = ath10k_peer_assoc_h_vht_limit(
                                         vht_cap->vht_mcs.tx_mcs_map, vht_mcs_mask);

    ath10k_dbg(ar, ATH10K_DBG_MAC, "mac vht peer %pM max_mpdu %d flags 0x%x\n",
               sta->addr, arg->peer_max_mpdu, arg->peer_flags);

    if (arg->peer_vht_rates.rx_max_rate &&
            (sta->vht_cap.cap & IEEE80211_VHT_CAP_SUPP_CHAN_WIDTH_MASK)) {
        switch (arg->peer_vht_rates.rx_max_rate) {
        case 1560:
            /* Must be 2x2 at 160Mhz is all it can do. */
            arg->peer_bw_rxnss_override = 2;
            break;
        case 780:
            /* Can only do 1x1 at 160Mhz (Long Guard Interval) */
            arg->peer_bw_rxnss_override = 1;
            break;
        }
    }
}

static void ath10k_peer_assoc_h_qos(struct ath10k* ar,
                                    struct ieee80211_vif* vif,
                                    struct ieee80211_sta* sta,
                                    struct wmi_peer_assoc_complete_arg* arg) {
    struct ath10k_vif* arvif = (void*)vif->drv_priv;

    switch (arvif->vdev_type) {
    case WMI_VDEV_TYPE_AP:
        if (sta->wme) {
            arg->peer_flags |= arvif->ar->wmi.peer_flags->qos;
        }

        if (sta->wme && sta->uapsd_queues) {
            arg->peer_flags |= arvif->ar->wmi.peer_flags->apsd;
            arg->peer_rate_caps |= WMI_RC_UAPSD_FLAG;
        }
        break;
    case WMI_VDEV_TYPE_STA:
        if (vif->bss_conf.qos) {
            arg->peer_flags |= arvif->ar->wmi.peer_flags->qos;
        }
        break;
    case WMI_VDEV_TYPE_IBSS:
        if (sta->wme) {
            arg->peer_flags |= arvif->ar->wmi.peer_flags->qos;
        }
        break;
    default:
        break;
    }

    ath10k_dbg(ar, ATH10K_DBG_MAC, "mac peer %pM qos %d\n",
               sta->addr, !!(arg->peer_flags&
                             arvif->ar->wmi.peer_flags->qos));
}

static bool ath10k_mac_sta_has_ofdm_only(struct ieee80211_sta* sta) {
    return sta->supp_rates[NL80211_BAND_2GHZ] >>
           ATH10K_MAC_FIRST_OFDM_RATE_IDX;
}

static enum wmi_phy_mode ath10k_mac_get_phymode_vht(struct ath10k* ar,
        struct ieee80211_sta* sta) {
    if (sta->bandwidth == IEEE80211_STA_RX_BW_160) {
        switch (sta->vht_cap.cap & IEEE80211_VHT_CAP_SUPP_CHAN_WIDTH_MASK) {
        case IEEE80211_VHT_CAP_SUPP_CHAN_WIDTH_160MHZ:
                    return MODE_11AC_VHT160;
        case IEEE80211_VHT_CAP_SUPP_CHAN_WIDTH_160_80PLUS80MHZ:
            return MODE_11AC_VHT80_80;
        default:
            /* not sure if this is a valid case? */
            return MODE_11AC_VHT160;
        }
    }

    if (sta->bandwidth == IEEE80211_STA_RX_BW_80) {
        return MODE_11AC_VHT80;
    }

    if (sta->bandwidth == IEEE80211_STA_RX_BW_40) {
        return MODE_11AC_VHT40;
    }

    if (sta->bandwidth == IEEE80211_STA_RX_BW_20) {
        return MODE_11AC_VHT20;
    }

    return MODE_UNKNOWN;
}

static void ath10k_peer_assoc_h_phymode(struct ath10k* ar,
                                        struct ieee80211_vif* vif,
                                        struct ieee80211_sta* sta,
                                        struct wmi_peer_assoc_complete_arg* arg) {
    struct ath10k_vif* arvif = (void*)vif->drv_priv;
    struct cfg80211_chan_def def;
    enum nl80211_band band;
    const uint8_t* ht_mcs_mask;
    const uint16_t* vht_mcs_mask;
    enum wmi_phy_mode phymode = MODE_UNKNOWN;

    if (COND_WARN(ath10k_mac_vif_chan(vif, &def))) {
        return;
    }

    band = def.chan->band;
    ht_mcs_mask = arvif->bitrate_mask.control[band].ht_mcs;
    vht_mcs_mask = arvif->bitrate_mask.control[band].vht_mcs;

    switch (band) {
    case NL80211_BAND_2GHZ:
        if (sta->vht_cap.vht_supported &&
                !ath10k_peer_assoc_h_vht_masked(vht_mcs_mask)) {
            if (sta->bandwidth == IEEE80211_STA_RX_BW_40) {
                phymode = MODE_11AC_VHT40;
            } else {
                phymode = MODE_11AC_VHT20;
            }
        } else if (sta->ht_cap.ht_supported &&
                   !ath10k_peer_assoc_h_ht_masked(ht_mcs_mask)) {
            if (sta->bandwidth == IEEE80211_STA_RX_BW_40) {
                phymode = MODE_11NG_HT40;
            } else {
                phymode = MODE_11NG_HT20;
            }
        } else if (ath10k_mac_sta_has_ofdm_only(sta)) {
            phymode = MODE_11G;
        } else {
            phymode = MODE_11B;
        }

        break;
    case NL80211_BAND_5GHZ:
        /*
         * Check VHT first.
         */
        if (sta->vht_cap.vht_supported &&
                !ath10k_peer_assoc_h_vht_masked(vht_mcs_mask)) {
            phymode = ath10k_mac_get_phymode_vht(ar, sta);
        } else if (sta->ht_cap.ht_supported &&
                   !ath10k_peer_assoc_h_ht_masked(ht_mcs_mask)) {
            if (sta->bandwidth >= IEEE80211_STA_RX_BW_40) {
                phymode = MODE_11NA_HT40;
            } else {
                phymode = MODE_11NA_HT20;
            }
        } else {
            phymode = MODE_11A;
        }

        break;
    default:
        break;
    }

    ath10k_dbg(ar, ATH10K_DBG_MAC, "mac peer %pM phymode %s\n",
               sta->addr, ath10k_wmi_phymode_str(phymode));

    arg->peer_phymode = phymode;
    COND_WARN(phymode == MODE_UNKNOWN);
}

static int ath10k_peer_assoc_prepare(struct ath10k* ar,
                                     struct ieee80211_vif* vif,
                                     struct ieee80211_sta* sta,
                                     struct wmi_peer_assoc_complete_arg* arg) {
    ASSERT_MTX_HELD(&ar->conf_mutex);

    memset(arg, 0, sizeof(*arg));

    ath10k_peer_assoc_h_basic(ar, vif, sta, arg);
    ath10k_peer_assoc_h_crypto(ar, vif, sta, arg);
    ath10k_peer_assoc_h_rates(ar, vif, sta, arg);
    ath10k_peer_assoc_h_ht(ar, vif, sta, arg);
    ath10k_peer_assoc_h_vht(ar, vif, sta, arg);
    ath10k_peer_assoc_h_qos(ar, vif, sta, arg);
    ath10k_peer_assoc_h_phymode(ar, vif, sta, arg);

    return 0;
}

static const uint32_t ath10k_smps_map[] = {
    [WLAN_HT_CAP_SM_PS_STATIC] = WMI_PEER_SMPS_STATIC,
    [WLAN_HT_CAP_SM_PS_DYNAMIC] = WMI_PEER_SMPS_DYNAMIC,
    [WLAN_HT_CAP_SM_PS_INVALID] = WMI_PEER_SMPS_PS_NONE,
    [WLAN_HT_CAP_SM_PS_DISABLED] = WMI_PEER_SMPS_PS_NONE,
};

static int ath10k_setup_peer_smps(struct ath10k* ar, struct ath10k_vif* arvif,
                                  const uint8_t* addr,
                                  const struct ieee80211_sta_ht_cap* ht_cap) {
    int smps;

    if (!ht_cap->ht_supported) {
        return 0;
    }

    smps = ht_cap->cap & IEEE80211_HT_CAP_SM_PS;
    smps >>= IEEE80211_HT_CAP_SM_PS_SHIFT;

    if (smps >= countof(ath10k_smps_map)) {
        return -EINVAL;
    }

    return ath10k_wmi_peer_set_param(ar, arvif->vdev_id, addr,
                                     WMI_PEER_SMPS_STATE,
                                     ath10k_smps_map[smps]);
}

static int ath10k_mac_vif_recalc_txbf(struct ath10k* ar,
                                      struct ieee80211_vif* vif,
                                      struct ieee80211_sta_vht_cap vht_cap) {
    struct ath10k_vif* arvif = (void*)vif->drv_priv;
    int ret;
    uint32_t param;
    uint32_t value;

    if (ath10k_wmi_get_txbf_conf_scheme(ar) != WMI_TXBF_CONF_AFTER_ASSOC) {
        return 0;
    }

    if (!(ar->vht_cap_info &
            (IEEE80211_VHT_CAP_SU_BEAMFORMEE_CAPABLE |
             IEEE80211_VHT_CAP_MU_BEAMFORMEE_CAPABLE |
             IEEE80211_VHT_CAP_SU_BEAMFORMER_CAPABLE |
             IEEE80211_VHT_CAP_MU_BEAMFORMER_CAPABLE))) {
        return 0;
    }

    param = ar->wmi.vdev_param->txbf;
    value = 0;

    if (COND_WARN(param == WMI_VDEV_PARAM_UNSUPPORTED)) {
        return 0;
    }

    /* The following logic is correct. If a remote STA advertises support
     * for being a beamformer then we should enable us being a beamformee.
     */

    if (ar->vht_cap_info &
            (IEEE80211_VHT_CAP_SU_BEAMFORMEE_CAPABLE |
             IEEE80211_VHT_CAP_MU_BEAMFORMEE_CAPABLE)) {
        if (vht_cap.cap & IEEE80211_VHT_CAP_SU_BEAMFORMER_CAPABLE) {
            value |= WMI_VDEV_PARAM_TXBF_SU_TX_BFEE;
        }

        if (vht_cap.cap & IEEE80211_VHT_CAP_MU_BEAMFORMER_CAPABLE) {
            value |= WMI_VDEV_PARAM_TXBF_MU_TX_BFEE;
        }
    }

    if (ar->vht_cap_info &
            (IEEE80211_VHT_CAP_SU_BEAMFORMER_CAPABLE |
             IEEE80211_VHT_CAP_MU_BEAMFORMER_CAPABLE)) {
        if (vht_cap.cap & IEEE80211_VHT_CAP_SU_BEAMFORMEE_CAPABLE) {
            value |= WMI_VDEV_PARAM_TXBF_SU_TX_BFER;
        }

        if (vht_cap.cap & IEEE80211_VHT_CAP_MU_BEAMFORMEE_CAPABLE) {
            value |= WMI_VDEV_PARAM_TXBF_MU_TX_BFER;
        }
    }

    if (value & WMI_VDEV_PARAM_TXBF_MU_TX_BFEE) {
        value |= WMI_VDEV_PARAM_TXBF_SU_TX_BFEE;
    }

    if (value & WMI_VDEV_PARAM_TXBF_MU_TX_BFER) {
        value |= WMI_VDEV_PARAM_TXBF_SU_TX_BFER;
    }

    ret = ath10k_wmi_vdev_set_param(ar, arvif->vdev_id, param, value);
    if (ret) {
        ath10k_warn("failed to submit vdev param txbf 0x%x: %d\n",
                    value, ret);
        return ret;
    }

    return 0;
}
#endif // NEEDS PORTING

static void ethaddr_sprintf(char* str, uint8_t* addr) {
    bool first = true;
    for (unsigned ndx = 0; ndx < ETH_ALEN; ndx++) {
        str += sprintf(str, "%s%02X", first ? "" : ":", *addr);
        addr++;
        first = false;
    }
}

static void ath10k_mac_parse_a_mpdu(uint8_t response_a_mpdu,
                                    struct wmi_peer_assoc_complete_arg* assoc_arg) {
    assoc_arg->peer_max_mpdu = response_a_mpdu & IEEE80211_A_MPDU_MAX_RX_LEN;
    assoc_arg->peer_mpdu_density = (response_a_mpdu & IEEE80211_A_MPDU_DENSITY) >>
                                   IEEE80211_A_MPDU_DENSITY_SHIFT;
}

static void ath10k_mac_parse_assoc_resp(struct ath10k* ar,
                                        const uint8_t* tagged_data,
                                        size_t data_len,
                                        struct wmi_peer_assoc_complete_arg* assoc_arg) {
    size_t legacy_rates_seen = 0;

    while (data_len > 0) {

        if (data_len < 2) {
            goto invalid_data;
        }

        uint8_t tag = *tagged_data++;
        uint8_t tag_len = *tagged_data++;
        data_len -= 2;
        if (tag_len > data_len) {
            goto invalid_data;
        }

        switch (tag) {
        case IEEE80211_ASSOC_TAG_RATES:
            {
                size_t num_rates = MIN(tag_len, MAX_SUPPORTED_RATES);
                legacy_rates_seen = assoc_arg->peer_legacy_rates.num_rates = num_rates;
                memcpy(assoc_arg->peer_legacy_rates.rates, tagged_data, num_rates);
                break;
            }
        case IEEE80211_ASSOC_TAG_HT_CAPS:
            if (tag_len != 26) {
                goto invalid_data;
            }
            assoc_arg->peer_flags |= ar->wmi.peer_flags->ht;
            uint16_t ht_caps = tagged_data[0] | ((uint16_t)tagged_data[1] << 8);
            assoc_arg->peer_ht_caps = ht_caps;
            assoc_arg->peer_rate_caps |= WMI_RC_HT_FLAG;
            if (ht_caps & IEEE80211_HT_CAPS_CHAN_WIDTH) {
                assoc_arg->peer_flags |= ar->wmi.peer_flags->bw40;
                assoc_arg->peer_rate_caps |= WMI_RC_CW40_FLAG;
            }
            if ((ht_caps & IEEE80211_HT_CAPS_SGI_20) ||
                (ht_caps & IEEE80211_HT_CAPS_SGI_40)) {
                assoc_arg->peer_rate_caps |= WMI_RC_SGI_FLAG;
            }
            if (ht_caps & IEEE80211_HT_CAPS_LDPC) {
                assoc_arg->peer_flags |= ar->wmi.peer_flags->ldbc;
            }
            if (ht_caps & IEEE80211_HT_CAPS_TX_STBC) {
                assoc_arg->peer_rate_caps |= WMI_RC_TX_STBC_FLAG;
                assoc_arg->peer_flags |= ar->wmi.peer_flags->stbc;
            }
            if (ht_caps & IEEE80211_HT_CAPS_RX_STBC) {
                uint16_t stbc = ht_caps & IEEE80211_HT_CAPS_RX_STBC;
                stbc >>= IEEE80211_HT_CAPS_RX_STBC_SHIFT;
                stbc <<= WMI_RC_RX_STBC_FLAG_S;
                assoc_arg->peer_rate_caps |= stbc;
                assoc_arg->peer_flags |= ar->wmi.peer_flags->stbc;
            }
            ath10k_mac_parse_a_mpdu(tagged_data[2], assoc_arg);
            break;
        case IEEE80211_ASSOC_TAG_HT_INFO:
            if (tag_len != 22) {
                goto invalid_data;
            }
#if 0 // NEEDS PORTING
            struct ieee80211_ht_info* ht_info = (void*)tagged_data;
            unsigned i, n, max_nss;
            for (i = 0, n = 0, max_nss = 0; i < (10 * 8); i++) {
                if ((ht_info->rx_mcs[i / 8] & (1U << (i % 8))) &&
                    (ht_mcs_mask[i / 8] & (1U << (i % 8)))) {
                    max_nss = (i / 8) + 1;
                    arg->peer_ht_rates.rates[n++] = i;
                }
            }
            /*
             * This is a workaround for HT-enabled STAs which break the spec
             * and have no HT capabilities RX mask (no HT RX MCS map).
             *
             * As per spec, in section 20.3.5 Modulation and coding scheme (MCS),
             * MCS 0 through 7 are mandatory in 20MHz with 800 ns GI at all STAs.
             *
             * Firmware asserts if such situation occurs.
             */
            if (n == 0) {
#endif // NEEDS PORTING
                unsigned i;
                assoc_arg->peer_ht_rates.num_rates = 8;
                for (i = 0; i < assoc_arg->peer_ht_rates.num_rates; i++) {
                    assoc_arg->peer_ht_rates.rates[i] = i;
                }
#if 0 // NEEDS PORTING
            } else {
                arg->peer_ht_rates.num_rates = n;
                arg->peer_num_spatial_streams = MIN(sta->rx_nss, max_nss);
            }
#endif // NEEDS PORTING
            break;
        case IEEE80211_ASSOC_TAG_EXTENDED_RATES:
            {
                size_t num_rates = MIN(tag_len, MAX_SUPPORTED_RATES - legacy_rates_seen);
                assoc_arg->peer_legacy_rates.num_rates += num_rates;
                memcpy(&assoc_arg->peer_legacy_rates.rates[legacy_rates_seen], tagged_data,
                       num_rates);
                legacy_rates_seen += num_rates;
            }
            break;
        default:
            // Ignore
            break;
        }
        tagged_data += tag_len;
        data_len -= tag_len;
    }

    return;

invalid_data:
    ath10k_info("improperly formatted association response seen\n");
}

// Take the vdev down, and tell the firmware to forget about the previous association.
static zx_status_t ath10k_mac_bss_disassoc(struct ath10k* ar) {
    struct ath10k_vif* arvif = &ar->arvif;

    ASSERT_MTX_HELD(&ar->conf_mutex);

    if (!arvif->is_up) {
        return ZX_ERR_BAD_STATE;
    }

    zx_status_t ret = ath10k_wmi_peer_delete(ar, arvif->vdev_id, arvif->bssid);
    if (ret != ZX_OK) {
        char ethaddr_str[ETH_ALEN * 3];
        ethaddr_sprintf(ethaddr_str, arvif->bssid);
        ath10k_err("Failed to delete peer %s in vdev %i: %s\n",
                   ethaddr_str, arvif->vdev_id, zx_status_get_string(ret));
        return ret;
    }

    ret = ath10k_wmi_vdev_down(ar, arvif->vdev_id);
    if (ret != ZX_OK) {
        ath10k_err("Failed to take vdev %i down: %s\n", arvif->vdev_id, zx_status_get_string(ret));
        return ret;
    }
    arvif->is_up = false;

    return ZX_OK;
}

zx_status_t ath10k_mac_set_bss(struct ath10k* ar, wlan_bss_config_t* config) {
    struct ath10k_vif* arvif = &ar->arvif;
    zx_status_t ret = ZX_OK;

    mtx_lock(&ar->conf_mutex);
    memcpy(&arvif->bssid, config->bssid, ETH_ALEN);
    mtx_unlock(&ar->conf_mutex);
    return ret;
}

// Loop for waiting on an association event (triggered by the receipt of a association
// response). Eventually, this function should not be a loop, and should be invoked by
// wlanmac.
int ath10k_mac_bss_assoc(void* thrd_data) {
    struct ath10k* ar = thrd_data;
    zx_status_t status;

    while (1) {
        sync_completion_wait(&ar->assoc_complete, ZX_TIME_INFINITE);
        mtx_lock(&ar->assoc_lock);
        sync_completion_reset(&ar->assoc_complete);

        // assoc_frame is set by ath10k_wmi_event_mgmt_rx before signaling the
        // assoc_complete completion.
        struct ath10k_msg_buf* buf = ar->assoc_frame;
        ZX_DEBUG_ASSERT(buf != NULL);
        mtx_unlock(&ar->assoc_lock);

        mtx_lock(&ar->conf_mutex);
        struct ath10k_vif* arvif = &ar->arvif;
        struct wmi_peer_assoc_complete_arg assoc_arg;
        struct ieee80211_frame_header* frame_hdr;
        struct ieee80211_assoc_resp* assoc_resp;

        ZX_DEBUG_ASSERT(arvif->is_started);
        ZX_DEBUG_ASSERT(!arvif->is_up);

        void* frame_ptr = ath10k_msg_buf_get_payload(buf) + buf->rx.frame_offset;
        frame_hdr = frame_ptr;
        assoc_resp = frame_ptr + sizeof(*frame_hdr);
        arvif->aid = (assoc_resp->assoc_id & 0x3fff);

        size_t total_size = buf->rx.frame_size;
        size_t rate_info_size = total_size - (sizeof(*frame_hdr) + sizeof(*assoc_resp));

        if (assoc_resp->status != 0) {
            goto done;
        }

        uint8_t* frame_bssid = ieee80211_get_bssid(frame_hdr);
        memset(&assoc_arg, 0, sizeof(assoc_arg));
        if (memcmp(frame_bssid, arvif->bssid, ETH_ALEN)) {
            char bssid_expected[ETH_ALEN * 3];
            char bssid_actual[ETH_ALEN * 3];
            ethaddr_sprintf(bssid_expected, arvif->bssid);
            ethaddr_sprintf(bssid_actual, frame_bssid);
            ath10k_warn("expected to associate with %s but got response from %s - ignoring\n",
                        bssid_expected, bssid_actual);
            goto done;
        }
        memcpy(assoc_arg.addr, frame_bssid, ETH_ALEN);

        assoc_arg.vdev_id = arvif->vdev_id;
        assoc_arg.peer_reassoc = false;
        assoc_arg.peer_aid = arvif->aid;
        assoc_arg.peer_flags |= ar->wmi.peer_flags->auth | ar->wmi.peer_flags->qos;
        assoc_arg.peer_listen_intval = 1;
        assoc_arg.peer_num_spatial_streams = 1;
        assoc_arg.peer_caps = assoc_resp->capabilities;

        ath10k_mac_parse_assoc_resp(ar, assoc_resp->info, rate_info_size, &assoc_arg);

        assoc_arg.peer_phymode = chan_to_phymode(&ar->rx_channel);

        // TODO: set crypto flags (as per ath10k_peer_assoc_h_crypto)

#if 0 // TODO: VHT
        assoc_arg.peer_vht_caps
        assoc_arg.peer_vht_rates
        assoc_arg.peer_bw_rxnss_override
#endif

        char bssid_str[ETH_ALEN * 3];
        ethaddr_sprintf(bssid_str, arvif->bssid);

        status = ath10k_wmi_peer_create(ar, arvif->vdev_id, frame_bssid, WMI_PEER_TYPE_BSS);
        if (status != ZX_OK) {
            ath10k_warn("failed to create peer: %s\n", zx_status_get_string(status));
            goto done;
        }

        status = ath10k_wmi_peer_assoc(ar, &assoc_arg);
        if (status != ZX_OK) {
            ath10k_warn("failed to run peer assoc for %pM vdev %i: %s\n",
                        arvif->bssid, arvif->vdev_id, zx_status_get_string(status));
            ath10k_wmi_peer_delete(ar, arvif->vdev_id, frame_bssid);
            goto done;
        }

        ath10k_dbg(ar, ATH10K_DBG_MAC, "mac vdev %d up (associated) bssid %pM aid %d\n",
                   arvif->vdev_id, arvif->bssid, arvif->aid);

        status = ath10k_wmi_vdev_up(ar, arvif->vdev_id, arvif->aid, arvif->bssid);
        if (status != ZX_OK) {
            ath10k_warn("failed to bring vdev %d up with aid: %d bssid: %s (%s)\n",
                        arvif->vdev_id, arvif->aid, bssid_str, zx_status_get_string(status));
        }

        arvif->is_up = true;

        ath10k_info("successfully associated with bssid %s\n", bssid_str);

        /* Workaround: Some firmware revisions (tested with qca6174
         * WLAN.RM.2.0-00073) have buggy powersave state machine and must be
         * poked with peer param command.
         */
        status = ath10k_wmi_peer_set_param(ar, arvif->vdev_id, arvif->bssid,
                                           WMI_PEER_DUMMY_VAR, 1);
        if (status != ZX_OK) {
            ath10k_warn("failed to poke peer %pM param for ps workaround on vdev %i: %s\n",
                        arvif->bssid, arvif->vdev_id, zx_status_get_string(status));
            goto done;
        }

done:
        mtx_unlock(&ar->conf_mutex);

        mtx_lock(&ar->assoc_lock);
        ath10k_msg_buf_free(buf);
        ar->assoc_frame = NULL;
        mtx_unlock(&ar->assoc_lock);
    }
    return 1; // We should never exit...
}

#if 0
/* can be called only in mac80211 callbacks due to `key_count` usage */
static void ath10k_bss_assoc(struct ieee80211_hw* hw,
                             struct ieee80211_vif* vif,
                             struct ieee80211_bss_conf* bss_conf) {
    struct ath10k* ar = hw->priv;
    struct ath10k_vif* arvif = (void*)vif->drv_priv;
    struct ieee80211_sta_ht_cap ht_cap;
    struct ieee80211_sta_vht_cap vht_cap;
    struct wmi_peer_assoc_complete_arg peer_arg;
    struct ieee80211_sta* ap_sta;
    int ret;

    ASSERT_MTX_HELD(&ar->conf_mutex);

    ath10k_dbg(ar, ATH10K_DBG_MAC, "mac vdev %i assoc bssid %pM aid %d\n",
               arvif->vdev_id, arvif->bssid, arvif->aid);

    rcu_read_lock();

    ap_sta = ieee80211_find_sta(vif, bss_conf->bssid);
    if (!ap_sta) {
        ath10k_warn("failed to find station entry for bss %pM vdev %i\n",
                    bss_conf->bssid, arvif->vdev_id);
        rcu_read_unlock();
        return;
    }

    /* ap_sta must be accessed only within rcu section which must be left
     * before calling ath10k_setup_peer_smps() which might sleep.
     */
    ht_cap = ap_sta->ht_cap;
    vht_cap = ap_sta->vht_cap;

    ret = ath10k_peer_assoc_prepare(ar, vif, ap_sta, &peer_arg);
    if (ret) {
        ath10k_warn("failed to prepare peer assoc for %pM vdev %i: %d\n",
                    bss_conf->bssid, arvif->vdev_id, ret);
        rcu_read_unlock();
        return;
    }

    rcu_read_unlock();

    ret = ath10k_wmi_peer_assoc(ar, &peer_arg);
    if (ret) {
        ath10k_warn("failed to run peer assoc for %pM vdev %i: %d\n",
                    bss_conf->bssid, arvif->vdev_id, ret);
        return;
    }

    ret = ath10k_setup_peer_smps(ar, arvif, bss_conf->bssid, &ht_cap);
    if (ret) {
        ath10k_warn("failed to setup peer SMPS for vdev %i: %d\n",
                    arvif->vdev_id, ret);
        return;
    }

    ret = ath10k_mac_vif_recalc_txbf(ar, vif, vht_cap);
    if (ret) {
        ath10k_warn("failed to recalc txbf for vdev %i on bss %pM: %d\n",
                    arvif->vdev_id, bss_conf->bssid, ret);
        return;
    }

    ath10k_dbg(ar, ATH10K_DBG_MAC,
               "mac vdev %d up (associated) bssid %pM aid %d\n",
               arvif->vdev_id, bss_conf->bssid, bss_conf->aid);

    COND_WARN(arvif->is_up);

    arvif->aid = bss_conf->aid;
    memcpy(arvif->bssid, bss_conf->bssid, ETH_ALEN);

    ret = ath10k_wmi_vdev_up(ar, arvif->vdev_id, arvif->aid, arvif->bssid);
    if (ret) {
        ath10k_warn("failed to set vdev %d up: %d\n",
                    arvif->vdev_id, ret);
        return;
    }

    arvif->is_up = true;

    /* Workaround: Some firmware revisions (tested with qca6174
     * WLAN.RM.2.0-00073) have buggy powersave state machine and must be
     * poked with peer param command.
     */
    ret = ath10k_wmi_peer_set_param(ar, arvif->vdev_id, arvif->bssid,
                                    WMI_PEER_DUMMY_VAR, 1);
    if (ret) {
        ath10k_warn("failed to poke peer %pM param for ps workaround on vdev %i: %d\n",
                    arvif->bssid, arvif->vdev_id, ret);
        return;
    }
}

static void ath10k_bss_disassoc(struct ieee80211_hw* hw,
                                struct ieee80211_vif* vif) {
    struct ath10k* ar = hw->priv;
    struct ath10k_vif* arvif = (void*)vif->drv_priv;
    struct ieee80211_sta_vht_cap vht_cap = {};
    int ret;

    ASSERT_MTX_HELD(&ar->conf_mutex);

    ath10k_dbg(ar, ATH10K_DBG_MAC, "mac vdev %i disassoc bssid %pM\n",
               arvif->vdev_id, arvif->bssid);

    ret = ath10k_wmi_vdev_down(ar, arvif->vdev_id);
    if (ret)
        ath10k_warn("failed to down vdev %i: %d\n",
                    arvif->vdev_id, ret);

    arvif->def_wep_key_idx = -1;

    ret = ath10k_mac_vif_recalc_txbf(ar, vif, vht_cap);
    if (ret) {
        ath10k_warn("failed to recalc txbf for vdev %i: %d\n",
                    arvif->vdev_id, ret);
        return;
    }

    arvif->is_up = false;

    cancel_delayed_work_sync(&arvif->connection_loss_work);
}

static int ath10k_station_assoc(struct ath10k* ar,
                                struct ieee80211_vif* vif,
                                struct ieee80211_sta* sta,
                                bool reassoc) {
    struct ath10k_vif* arvif = (void*)vif->drv_priv;
    struct wmi_peer_assoc_complete_arg peer_arg;
    int ret = 0;

    ASSERT_MTX_HELD(&ar->conf_mutex);

    ret = ath10k_peer_assoc_prepare(ar, vif, sta, &peer_arg);
    if (ret) {
        ath10k_warn("failed to prepare WMI peer assoc for %pM vdev %i: %i\n",
                    sta->addr, arvif->vdev_id, ret);
        return ret;
    }

    ret = ath10k_wmi_peer_assoc(ar, &peer_arg);
    if (ret) {
        ath10k_warn("failed to run peer assoc for STA %pM vdev %i: %d\n",
                    sta->addr, arvif->vdev_id, ret);
        return ret;
    }

    /* Re-assoc is run only to update supported rates for given station. It
     * doesn't make much sense to reconfigure the peer completely.
     */
    if (!reassoc) {
        ret = ath10k_setup_peer_smps(ar, arvif, sta->addr,
                                     &sta->ht_cap);
        if (ret) {
            ath10k_warn("failed to setup peer SMPS for vdev %d: %d\n",
                        arvif->vdev_id, ret);
            return ret;
        }

        ret = ath10k_peer_assoc_qos_ap(ar, arvif, sta);
        if (ret) {
            ath10k_warn("failed to set qos params for STA %pM for vdev %i: %d\n",
                        sta->addr, arvif->vdev_id, ret);
            return ret;
        }

        if (!sta->wme) {
            arvif->num_legacy_stations++;
            ret  = ath10k_recalc_rtscts_prot(arvif);
            if (ret) {
                ath10k_warn("failed to recalculate rts/cts prot for vdev %d: %d\n",
                            arvif->vdev_id, ret);
                return ret;
            }
        }

        /* Plumb cached keys only for static WEP */
        if (arvif->def_wep_key_idx != -1) {
            ret = ath10k_install_peer_wep_keys(arvif, sta->addr);
            if (ret) {
                ath10k_warn("failed to install peer wep keys for vdev %i: %d\n",
                            arvif->vdev_id, ret);
                return ret;
            }
        }
    }

    return ret;
}

static int ath10k_station_disassoc(struct ath10k* ar,
                                   struct ieee80211_vif* vif,
                                   struct ieee80211_sta* sta) {
    struct ath10k_vif* arvif = (void*)vif->drv_priv;
    int ret = 0;

    ASSERT_MTX_HELD(&ar->conf_mutex);

    if (!sta->wme) {
        arvif->num_legacy_stations--;
        ret = ath10k_recalc_rtscts_prot(arvif);
        if (ret) {
            ath10k_warn("failed to recalculate rts/cts prot for vdev %d: %d\n",
                        arvif->vdev_id, ret);
            return ret;
        }
    }

    ret = ath10k_clear_peer_keys(arvif, sta->addr);
    if (ret) {
        ath10k_warn("failed to clear all peer wep keys for vdev %i: %d\n",
                    arvif->vdev_id, ret);
        return ret;
    }

    return ret;
}

/**************/
/* Regulatory */
/**************/

static zx_status_t ath10k_update_channel_list(struct ath10k* ar) {

    ASSERT_MTX_HELD(&ar->conf_mutex);

    size_t num_bands = countof(ath10k_supported_bands);
    struct wmi_scan_chan_list_arg arg = {0};

    for (unsigned band = 0; band < num_bands; band++) {
        for (unsigned i = 0; i < ath10k_supported_bands[band].n_channels; i++) {
            if (ath10k_supported_bands[band].channels[i].flags & IEEE80211_CHAN_DISABLED) {
                continue;
            }
            arg.n_channels++;
        }
    }

    size_t len = sizeof(struct wmi_channel_arg) * arg.n_channels;
    arg.channels = malloc(len);
    if (!arg.channels) {
        return ZX_ERR_NO_MEMORY;
    }

    struct wmi_channel_arg* ch = arg.channels;
    for (unsigned band = 0; band < num_bands; band++) {

        for (unsigned i = 0; i < ath10k_supported_bands[band].n_channels; i++) {
            const struct ath10k_channel* channel = &ath10k_supported_bands[band].channels[i];

            if (channel->flags & IEEE80211_CHAN_DISABLED) {
                continue;
            }

            ch->allow_ht = true;

            /* FIXME: when should we really allow VHT? */
            ch->allow_vht = true;

            ch->allow_ibss =
                !(channel->flags & IEEE80211_CHAN_NO_IR);

            ch->ht40plus =
                !(channel->flags & IEEE80211_CHAN_NO_HT40PLUS);

            ch->chan_radar =
                !!(channel->flags & IEEE80211_CHAN_RADAR);

            bool passive = channel->flags & IEEE80211_CHAN_NO_IR;
            ch->passive = passive;

            ch->freq = channel->center_freq;
            ch->band_center_freq1 = channel->center_freq;
            ch->min_power = 0;
            ch->max_power = channel->max_power * 2;
            ch->max_reg_power = channel->max_reg_power * 2;
            ch->max_antenna_gain = channel->max_antenna_gain * 2;
            ch->reg_class_id = 0; /* FIXME */

            /* FIXME: why use only legacy modes, why not any
             * HT/VHT modes? Would that even make any
             * difference?
             */
            ch->mode = ath10k_supported_bands[band].mode;

            if (COND_WARN_ONCE(ch->mode == MODE_UNKNOWN)) {
                continue;
            }

            ath10k_dbg(ar, ATH10K_DBG_WMI,
                       "mac channel [%zd/%d] freq %d maxpower %d regpower %d antenna %d mode %d\n",
                       ch - arg.channels, arg.n_channels,
                       ch->freq, ch->max_power, ch->max_reg_power,
                       ch->max_antenna_gain, ch->mode);

            ch++;
        }
    }

    zx_status_t ret = ath10k_wmi_scan_chan_list(ar, &arg);
    free(arg.channels);

    return ret;
}

static enum wmi_dfs_region
ath10k_mac_get_dfs_region(enum nl80211_dfs_regions dfs_region) {
    switch (dfs_region) {
    case NL80211_DFS_UNSET:
                return WMI_UNINIT_DFS_DOMAIN;
    case NL80211_DFS_FCC:
        return WMI_FCC_DFS_DOMAIN;
    case NL80211_DFS_ETSI:
        return WMI_ETSI_DFS_DOMAIN;
    case NL80211_DFS_JP:
        return WMI_MKK4_DFS_DOMAIN;
    }
    return WMI_UNINIT_DFS_DOMAIN;
}
#endif // NEEDS PORTING

static void ath10k_regd_update(struct ath10k* ar) {
//    zx_status_t ret;
#if 0 // NEEDS PORTING
    struct reg_dmn_pair_mapping* regpair;
    enum wmi_dfs_region wmi_dfs_reg;
    enum nl80211_dfs_regions nl_dfs_reg;
#endif // NEEDS PORTING

    ASSERT_MTX_HELD(&ar->conf_mutex);

#if 0
    ret = ath10k_update_channel_list(ar);
    if (ret != ZX_OK) {
        ath10k_warn("failed to update channel list: %s\n", zx_status_get_string(ret));
    }
#endif

#if 0 // NEEDS PORTING
    regpair = ar->ath_common.regulatory.regpair;

    if (IS_ENABLED(CONFIG_ATH10K_DFS_CERTIFIED) && ar->dfs_detector) {
        nl_dfs_reg = ar->dfs_detector->region;
        wmi_dfs_reg = ath10k_mac_get_dfs_region(nl_dfs_reg);
    } else {
        wmi_dfs_reg = WMI_UNINIT_DFS_DOMAIN;
    }

    /* Target allows setting up per-band regdomain but ath_common provides
     * a combined one only
     */
    ret = ath10k_wmi_pdev_set_regdomain(ar,
                                        regpair->reg_domain,
                                        regpair->reg_domain, /* 2ghz */
                                        regpair->reg_domain, /* 5ghz */
                                        regpair->reg_2ghz_ctl,
                                        regpair->reg_5ghz_ctl,
                                        wmi_dfs_reg);
    if (ret) {
        ath10k_warn("failed to set pdev regdomain: %d\n", ret);
    }
#endif // NEEDS PORTING
}

void ath10k_foreach_band(void (*cb)(const struct ath10k_band* band, void* cookie),
                         void* cookie) {
    for (size_t band_ndx = 0; band_ndx < countof(ath10k_supported_bands); band_ndx++) {
        const struct ath10k_band* band = &ath10k_supported_bands[band_ndx];
        cb(band, cookie);
    }
}

void ath10k_foreach_channel(const struct ath10k_band* band,
                            void (*cb)(const struct ath10k_channel* ch,
                                       void* cookie),
                            void* cookie) {
    for (size_t ch_ndx = 0; ch_ndx < band->n_channels; ch_ndx++) {
        const struct ath10k_channel* ch = &band->channels[ch_ndx];
        cb(ch, cookie);
    }
}

#if 0 // NEEDS PORTING
static void ath10k_mac_update_channel_list(struct ath10k* ar,
        struct ieee80211_supported_band* band) {
    int i;

    if (ar->low_5ghz_chan && ar->high_5ghz_chan) {
        for (i = 0; i < band->n_channels; i++) {
            if (band->channels[i].center_freq < ar->low_5ghz_chan ||
                    band->channels[i].center_freq > ar->high_5ghz_chan)
                band->channels[i].flags |=
                    IEEE80211_CHAN_DISABLED;
        }
    }
}

static void ath10k_reg_notifier(struct wiphy* wiphy,
                                struct regulatory_request* request) {
    struct ieee80211_hw* hw = wiphy_to_ieee80211_hw(wiphy);
    struct ath10k* ar = hw->priv;
    bool result;

    ath_reg_notifier_apply(wiphy, request, &ar->ath_common.regulatory);

    if (IS_ENABLED(CONFIG_ATH10K_DFS_CERTIFIED) && ar->dfs_detector) {
        ath10k_dbg(ar, ATH10K_DBG_REGULATORY, "dfs region 0x%x\n",
                   request->dfs_region);
        result = ar->dfs_detector->set_dfs_domain(ar->dfs_detector,
                 request->dfs_region);
        if (!result)
            ath10k_warn("DFS region 0x%X not supported, will trigger radar for every pulse\n",
                        request->dfs_region);
    }

    mtx_lock(&ar->conf_mutex);
    if (ar->state == ATH10K_STATE_ON) {
        ath10k_regd_update(ar);
    }
    mtx_unlock(&ar->conf_mutex);

    if (ar->phy_capability & WHAL_WLAN_11A_CAPABILITY)
        ath10k_mac_update_channel_list(ar,
                                       ar->hw->wiphy->bands[NL80211_BAND_5GHZ]);
}
#endif // NEEDS PORTING

/***************/
/* TX handlers */
/***************/

enum ath10k_mac_tx_path {
    ATH10K_MAC_TX_HTT,
    ATH10K_MAC_TX_HTT_MGMT,
    ATH10K_MAC_TX_WMI_MGMT,
    ATH10K_MAC_TX_UNKNOWN,
};

#if 0 // NEEDS PORTING
void ath10k_mac_tx_lock(struct ath10k* ar, int reason) {
    ASSERT_MTX_HELD(&ar->htt.tx_lock);

    COND_WARN(reason >= ATH10K_TX_PAUSE_MAX);
    ar->tx_paused |= BIT(reason);
    ieee80211_stop_queues(ar->hw);
}

static void ath10k_mac_tx_unlock_iter(void* data, uint8_t* mac,
                                      struct ieee80211_vif* vif) {
    struct ath10k* ar = data;
    struct ath10k_vif* arvif = (void*)vif->drv_priv;

    if (arvif->tx_paused) {
        return;
    }

    ieee80211_wake_queue(ar->hw, arvif->vdev_id);
}

void ath10k_mac_tx_unlock(struct ath10k* ar, int reason) {
    ASSERT_MTX_HELD(&ar->htt.tx_lock);

    COND_WARN(reason >= ATH10K_TX_PAUSE_MAX);
    ar->tx_paused &= ~BIT(reason);

    if (ar->tx_paused) {
        return;
    }

    ieee80211_iterate_active_interfaces_atomic(ar->hw,
            IEEE80211_IFACE_ITER_RESUME_ALL,
            ath10k_mac_tx_unlock_iter,
            ar);

    ieee80211_wake_queue(ar->hw, ar->hw->offchannel_tx_hw_queue);
}

void ath10k_mac_vif_tx_lock(struct ath10k_vif* arvif, int reason) {
    struct ath10k* ar = arvif->ar;

    ASSERT_MTX_HELD(&ar->htt.tx_lock);

    COND_WARN(reason >= BITS_PER_LONG);
    arvif->tx_paused |= BIT(reason);
    ieee80211_stop_queue(ar->hw, arvif->vdev_id);
}

void ath10k_mac_vif_tx_unlock(struct ath10k_vif* arvif, int reason) {
    struct ath10k* ar = arvif->ar;

    ASSERT_MTX_HELD(&ar->htt.tx_lock);

    COND_WARN(reason >= BITS_PER_LONG);
    arvif->tx_paused &= ~BIT(reason);

    if (ar->tx_paused) {
        return;
    }

    if (arvif->tx_paused) {
        return;
    }

    ieee80211_wake_queue(ar->hw, arvif->vdev_id);
}

static void ath10k_mac_vif_handle_tx_pause(struct ath10k_vif* arvif,
        enum wmi_tlv_tx_pause_id pause_id,
        enum wmi_tlv_tx_pause_action action) {
    struct ath10k* ar = arvif->ar;

    ASSERT_MTX_HELD(&ar->htt.tx_lock);

    switch (action) {
    case WMI_TLV_TX_PAUSE_ACTION_STOP:
        ath10k_mac_vif_tx_lock(arvif, pause_id);
        break;
    case WMI_TLV_TX_PAUSE_ACTION_WAKE:
        ath10k_mac_vif_tx_unlock(arvif, pause_id);
        break;
    default:
        ath10k_dbg(ar, ATH10K_DBG_BOOT,
                   "received unknown tx pause action %d on vdev %i, ignoring\n",
                   action, arvif->vdev_id);
        break;
    }
}

struct ath10k_mac_tx_pause {
    uint32_t vdev_id;
    enum wmi_tlv_tx_pause_id pause_id;
    enum wmi_tlv_tx_pause_action action;
};

static void ath10k_mac_handle_tx_pause_iter(void* data, uint8_t* mac,
        struct ieee80211_vif* vif) {
    struct ath10k_vif* arvif = (void*)vif->drv_priv;
    struct ath10k_mac_tx_pause* arg = data;

    if (arvif->vdev_id != arg->vdev_id) {
        return;
    }

    ath10k_mac_vif_handle_tx_pause(arvif, arg->pause_id, arg->action);
}

void ath10k_mac_handle_tx_pause_vdev(struct ath10k* ar, uint32_t vdev_id,
                                     enum wmi_tlv_tx_pause_id pause_id,
                                     enum wmi_tlv_tx_pause_action action) {
    struct ath10k_mac_tx_pause arg = {
        .vdev_id = vdev_id,
        .pause_id = pause_id,
        .action = action,
    };

    mtx_lock(&ar->htt.tx_lock);
    ieee80211_iterate_active_interfaces_atomic(ar->hw,
            IEEE80211_IFACE_ITER_RESUME_ALL,
            ath10k_mac_handle_tx_pause_iter,
            &arg);
    mtx_unlock(&ar->htt.tx_lock);
}
#endif // NEEDS PORTING

static enum ath10k_hw_txrx_mode
ath10k_mac_tx_h_get_txmode(struct ath10k* ar, void* packet_head) {
#if 0 // NEEDS PORTING
                           struct ieee80211_vif* vif,
                           struct ieee80211_sta* sta,
                           struct sk_buff* skb) {
#endif // NEEDS PORTING
    const struct ieee80211_frame_header* hdr = packet_head;

#if 0 // NEEDS PORTING
    if (!vif || vif->type == NL80211_IFTYPE_MONITOR) {
        return ATH10K_HW_TXRX_RAW;
    }
#endif // NEEDS PORTING

    if (ieee80211_get_frame_type(hdr) == IEEE80211_FRAME_TYPE_MGMT) {
        return ATH10K_HW_TXRX_MGMT;
    }

#if 0 // NEEDS PORTING
    /* Workaround:
     *
     * NullFunc frames are mostly used to ping if a client or AP are still
     * reachable and responsive. This implies tx status reports must be
     * accurate - otherwise either mac80211 or userspace (e.g. hostapd) can
     * come to a conclusion that the other end disappeared and tear down
     * BSS connection or it can never disconnect from BSS/client (which is
     * the case).
     *
     * Firmware with HTT older than 3.0 delivers incorrect tx status for
     * NullFunc frames to driver. However there's a HTT Mgmt Tx command
     * which seems to deliver correct tx reports for NullFunc frames. The
     * downside of using it is it ignores client powersave state so it can
     * end up disconnecting sleeping clients in AP mode. It should fix STA
     * mode though because AP don't sleep.
     */
    if (ar->htt.target_version_major < 3 &&
            (ieee80211_is_nullfunc(fc) || ieee80211_is_qos_nullfunc(fc)) &&
            !BITARR_TEST(ar->running_fw->fw_file.fw_features, ATH10K_FW_FEATURE_HAS_WMI_MGMT_TX)) {
        return ATH10K_HW_TXRX_MGMT;
    }

    /* Workaround:
     *
     * Some wmi-tlv firmwares for qca6174 have broken Tx key selection for
     * NativeWifi txmode - it selects AP key instead of peer key. It seems
     * to work with Ethernet txmode so use it.
     *
     * FIXME: Check if raw mode works with TDLS.
     */
    if (ieee80211_is_data_present(fc) && sta && sta->tdls) {
        return ATH10K_HW_TXRX_ETHERNET;
    }
#endif // NEEDS PORTING

    if (BITARR_TEST(ar->dev_flags, ATH10K_FLAG_RAW_MODE)) {
        return ATH10K_HW_TXRX_RAW;
    }

    return ATH10K_HW_TXRX_NATIVE_WIFI;
}

static bool ath10k_tx_h_use_hwcrypto(struct ath10k* ar,
                                     struct ath10k_msg_buf* tx_buf,
                                     wlan_tx_info_t* tx_info) {
    if (!(tx_info->tx_flags & WLAN_TX_INFO_FLAGS_PROTECTED)) {
        return false;
    }

    if (ar->arvif.nohwcrypt) {
        return false;
    }

    return true;
}

/* HTT Tx uses Native Wifi tx mode which expects 802.11 frames without QoS
 * Control in the header. We would prefer that wlanmac allow us to specify
 * that we don't want this information in the header so that we don't have
 * to change frames on-the-fly (see NET-903).
 */
static void ath10k_tx_h_nwifi(struct ath10k_msg_buf* tx_buf) {
    void* pkt = ath10k_msg_buf_get_payload(tx_buf);
    struct ieee80211_frame_header* hdr = pkt;

    if (ieee80211_get_frame_type(hdr) != IEEE80211_FRAME_TYPE_DATA) {
        return;
    }

    if (!(ieee80211_get_frame_subtype(hdr) & IEEE80211_FRAME_SUBTYPE_QOS)) {
        return;
    }

    size_t hdr_size = sizeof(struct ieee80211_frame_header);
    void* qos_info = pkt + hdr_size;
    size_t tail_len = tx_buf->used - (hdr_size + IEEE80211_QOS_CTL_LEN);
    memmove(qos_info, qos_info + IEEE80211_QOS_CTL_LEN, tail_len);
    tx_buf->used -= IEEE80211_QOS_CTL_LEN;

    /* Some firmware revisions don't handle sending QoS NullFunc well.
     * These frames are mainly used for CQM purposes so it doesn't really
     * matter whether QoS NullFunc or NullFunc are sent.
     */
    if (ieee80211_get_frame_subtype(hdr) == IEEE80211_FRAME_SUBTYPE_QOS_NULL) {
        tx_buf->tx.flags &= ~ATH10K_TX_BUF_QOS;
    }

    hdr->frame_ctrl &= ~IEEE80211_FRAME_SUBTYPE_QOS;
}

#if 0 // NEEDS PORTING
static void ath10k_tx_h_8023(struct sk_buff* skb) {
    struct ieee80211_hdr* hdr;
    struct rfc1042_hdr* rfc1042;
    struct ethhdr* eth;
    size_t hdrlen;
    uint8_t da[ETH_ALEN];
    uint8_t sa[ETH_ALEN];
    __be16 type;

    hdr = (void*)skb->data;
    hdrlen = ieee80211_hdrlen(hdr->frame_control);
    rfc1042 = (void*)skb->data + hdrlen;

    memcpy(da, ieee80211_get_DA(hdr), ETH_ALEN);
    memcpy(sa, ieee80211_get_SA(hdr), ETH_ALEN);
    type = rfc1042->snap_type;

    skb_pull(skb, hdrlen + sizeof(*rfc1042));
    skb_push(skb, sizeof(*eth));

    eth = (void*)skb->data;
    memcpy(eth->h_dest, da, ETH_ALEN);
    memcpy(eth->h_source, sa, ETH_ALEN);
    eth->h_proto = type;
}

static void ath10k_tx_h_add_p2p_noa_ie(struct ath10k* ar,
                                       struct ieee80211_vif* vif,
                                       struct sk_buff* skb) {
    struct ieee80211_hdr* hdr = (struct ieee80211_hdr*)skb->data;
    struct ath10k_vif* arvif = (void*)vif->drv_priv;

    /* This is case only for P2P_GO */
    if (vif->type != NL80211_IFTYPE_AP || !vif->p2p) {
        return;
    }

    if (unlikely(ieee80211_is_probe_resp(hdr->frame_control))) {
        mtx_lock(&ar->data_lock);
        if (arvif->u.ap.noa_data)
            if (!pskb_expand_head(skb, 0, arvif->u.ap.noa_len,
                                  GFP_ATOMIC))
                skb_put_data(skb, arvif->u.ap.noa_data,
                             arvif->u.ap.noa_len);
        mtx_unlock(&ar->data_lock);
    }
}
#endif // NEEDS PORTING

static void ath10k_mac_tx_h_tx_flags(struct ath10k* ar,
                                     struct ath10k_msg_buf* tx_buf,
                                     wlan_tx_info_t* tx_info) {

    struct ieee80211_frame_header* hdr = ath10k_msg_buf_get_payload(tx_buf);

    tx_buf->tx.flags = 0;
    if (ath10k_tx_h_use_hwcrypto(ar, tx_buf, tx_info)) {
        tx_buf->tx.flags |= ATH10K_TX_BUF_PROTECTED;
    }

    if ((ieee80211_get_frame_type(hdr) == IEEE80211_FRAME_TYPE_DATA)
        && (ieee80211_get_frame_subtype(hdr) & IEEE80211_FRAME_SUBTYPE_QOS)) {
        tx_buf->tx.flags |= ATH10K_TX_BUF_QOS;
    }
}

bool ath10k_mac_tx_frm_has_freq(struct ath10k* ar) {
    /* FIXME: Not really sure since when the behaviour changed. At some
     * point new firmware stopped requiring creation of peer entries for
     * offchannel tx (and actually creating them causes issues with wmi-htc
     * tx credit replenishment and reliability). Assuming it's at least 3.4
     * because that's when the `freq` was introduced to TX_FRM HTT command.
     */
    return (ar->htt.target_version_major >= 3 &&
            ar->htt.target_version_minor >= 4 &&
            ar->running_fw->fw_file.htt_op_version == ATH10K_FW_HTT_OP_VERSION_TLV);
}

static zx_status_t ath10k_mac_tx_wmi_mgmt(struct ath10k* ar, struct ath10k_msg_buf* tx_buf) {
ath10k_err("ath10k_mac_tx_wmi_mgmt unimplemented - dropping tx packet!\n");
#if 0 // NEEDS PORTING
    struct sk_buff_head* q = &ar->wmi_mgmt_tx_queue;
    int ret = 0;

    mtx_lock(&ar->data_lock);

    if (skb_queue_len(q) == ATH10K_MAX_NUM_MGMT_PENDING) {
        ath10k_warn("wmi mgmt tx queue is full\n");
        ret = -ENOSPC;
        goto unlock;
    }

    __skb_queue_tail(q, skb);
    ieee80211_queue_work(ar->hw, &ar->wmi_mgmt_tx_work);

unlock:
    mtx_unlock(&ar->data_lock);

    return ret;
#endif // NEEDS PORTING
return ZX_ERR_NOT_SUPPORTED;
}

static enum ath10k_mac_tx_path
ath10k_mac_tx_h_get_txpath(struct ath10k* ar,
                           enum ath10k_hw_txrx_mode txmode) {
    switch (txmode) {
    case ATH10K_HW_TXRX_RAW:
    case ATH10K_HW_TXRX_NATIVE_WIFI:
    case ATH10K_HW_TXRX_ETHERNET:
                        return ATH10K_MAC_TX_HTT;
    case ATH10K_HW_TXRX_MGMT:
        if (BITARR_TEST(ar->running_fw->fw_file.fw_features, ATH10K_FW_FEATURE_HAS_WMI_MGMT_TX)) {
            return ATH10K_MAC_TX_WMI_MGMT;
        } else if (ar->htt.target_version_major >= 3) {
            return ATH10K_MAC_TX_HTT;
        } else {
            return ATH10K_MAC_TX_HTT_MGMT;
        }
    }

    return ATH10K_MAC_TX_UNKNOWN;
}

static zx_status_t ath10k_mac_tx_submit(struct ath10k* ar,
                                        enum ath10k_hw_txrx_mode txmode,
                                        enum ath10k_mac_tx_path txpath,
                                        struct ath10k_msg_buf* tx_buf) {
    struct ath10k_htt* htt = &ar->htt;
    zx_status_t ret;

    switch (txpath) {
    case ATH10K_MAC_TX_HTT:
        ret = ath10k_htt_tx(htt, txmode, tx_buf);
        break;
    case ATH10K_MAC_TX_HTT_MGMT:
        ret = ath10k_htt_mgmt_tx(htt, tx_buf);
        break;
    case ATH10K_MAC_TX_WMI_MGMT:
        ret = ath10k_mac_tx_wmi_mgmt(ar, tx_buf);
        break;
    case ATH10K_MAC_TX_UNKNOWN:
    default:
        WARN_ONCE();
        ret = ZX_ERR_WRONG_TYPE;
        break;
    }

    if (ret != ZX_OK) {
        ath10k_warn("failed to transmit packet, dropping: %s\n",
                    zx_status_get_string(ret));
        ath10k_msg_buf_free(tx_buf);
    }

    return ret;
}

/* This function consumes the tx_buf regardless of return value as far as
 * caller is concerned so no freeing is necessary afterwards.
 */
static zx_status_t ath10k_mac_tx(struct ath10k* ar,
                                 enum ath10k_hw_txrx_mode txmode,
                                 enum ath10k_mac_tx_path txpath,
                                 struct ath10k_msg_buf* tx_buf) {
#if 0 // NEEDS PORTING
    /* We should disable CCK RATE due to P2P */
    if (info->flags & IEEE80211_TX_CTL_NO_CCK_RATE) {
        ath10k_dbg(ar, ATH10K_DBG_MAC, "IEEE80211_TX_CTL_NO_CCK_RATE\n");
    }
#endif // NEEDS PORTING

    switch (txmode) {
    case ATH10K_HW_TXRX_MGMT:
    case ATH10K_HW_TXRX_NATIVE_WIFI:
        ath10k_tx_h_nwifi(tx_buf);
#if 0 // NEEDS PORTING
        ath10k_tx_h_add_p2p_noa_ie(ar, vif, skb);
        ath10k_tx_h_seq_no(vif, skb);
#endif // NEEDS PORTING
        break;
    case ATH10K_HW_TXRX_ETHERNET:
        ZX_DEBUG_ASSERT(0); // Not supported yet
#if 0 // NEEDS PORTING
        ath10k_tx_h_8023(skb);
#endif // NEEDS PORTING
        break;
    case ATH10K_HW_TXRX_RAW:
        if (!BITARR_TEST(ar->dev_flags, ATH10K_FLAG_RAW_MODE)) {
            WARN_ONCE();
            ath10k_msg_buf_free(tx_buf);
            return ZX_ERR_NOT_SUPPORTED;
        }
    }

#if 0 // NEEDS PORTING
    if (info->flags & IEEE80211_TX_CTL_TX_OFFCHAN) {
        if (!ath10k_mac_tx_frm_has_freq(ar)) {
            ath10k_dbg(ar, ATH10K_DBG_MAC, "queued offchannel skb %pK\n",
                       skb);

            skb_queue_tail(&ar->offchan_tx_queue, skb);
            ieee80211_queue_work(hw, &ar->offchan_tx_work);
            return 0;
        }
    }
#endif // NEEDS PORTING

    zx_status_t ret = ath10k_mac_tx_submit(ar, txmode, txpath, tx_buf);
    if (ret != ZX_OK) {
        ath10k_warn("failed to submit frame: %d\n", ret);
        return ret;
    }

    return ZX_OK;
}

#if 0 // NEEDS PORTING
void ath10k_offchan_tx_purge(struct ath10k* ar) {
    struct sk_buff* skb;

    for (;;) {
        skb = skb_dequeue(&ar->offchan_tx_queue);
        if (!skb) {
            break;
        }

        ieee80211_free_txskb(ar->hw, skb);
    }
}

void ath10k_offchan_tx_work(struct work_struct* work) {
    struct ath10k* ar = container_of(work, struct ath10k, offchan_tx_work);
    struct ath10k_peer* peer;
    struct ath10k_vif* arvif;
    enum ath10k_hw_txrx_mode txmode;
    enum ath10k_mac_tx_path txpath;
    struct ieee80211_hdr* hdr;
    struct ieee80211_vif* vif;
    struct ieee80211_sta* sta;
    struct sk_buff* skb;
    const uint8_t* peer_addr;
    int vdev_id;
    int ret;
    bool tmp_peer_created = false;

    /* FW requirement: We must create a peer before FW will send out
     * an offchannel frame. Otherwise the frame will be stuck and
     * never transmitted. We delete the peer upon tx completion.
     * It is unlikely that a peer for offchannel tx will already be
     * present. However it may be in some rare cases so account for that.
     * Otherwise we might remove a legitimate peer and break stuff.
     */

    for (;;) {
        skb = skb_dequeue(&ar->offchan_tx_queue);
        if (!skb) {
            break;
        }

        mtx_lock(&ar->conf_mutex);

        ath10k_dbg(ar, ATH10K_DBG_MAC, "mac offchannel skb %pK\n",
                   skb);

        hdr = (struct ieee80211_hdr*)skb->data;
        peer_addr = ieee80211_get_DA(hdr);

        mtx_lock(&ar->data_lock);
        vdev_id = ar->scan.vdev_id;
        peer = ath10k_peer_find(ar, vdev_id, peer_addr);
        mtx_unlock(&ar->data_lock);

        if (peer)
            /* FIXME: should this use ath10k_warn()? */
            ath10k_dbg(ar, ATH10K_DBG_MAC, "peer %pM on vdev %d already present\n",
                       peer_addr, vdev_id);

        if (!peer) {
            ret = ath10k_peer_create(ar, NULL, NULL, vdev_id,
                                     peer_addr,
                                     WMI_PEER_TYPE_DEFAULT);
            if (ret)
                ath10k_warn("failed to create peer %pM on vdev %d: %d\n",
                            peer_addr, vdev_id, ret);
            tmp_peer_created = (ret == 0);
        }

        mtx_lock(&ar->data_lock);
        sync_completion_reset(&ar->offchan_tx_completed);
        ar->offchan_tx_skb = skb;
        mtx_unlock(&ar->data_lock);

        /* It's safe to access vif and sta - conf_mutex guarantees that
         * sta_state() and remove_interface() are locked exclusively
         * out wrt to this offchannel worker.
         */
        arvif = ath10k_get_arvif(ar, vdev_id);
        if (arvif) {
            vif = arvif->vif;
            sta = ieee80211_find_sta(vif, peer_addr);
        } else {
            vif = NULL;
            sta = NULL;
        }

        txmode = ath10k_mac_tx_h_get_txmode(ar, vif, sta, skb);
        txpath = ath10k_mac_tx_h_get_txpath(ar, skb, txmode);

        ret = ath10k_mac_tx(ar, vif, txmode, txpath, skb);
        if (ret) {
            ath10k_warn("failed to transmit offchannel frame: %d\n",
                        ret);
            /* not serious */
        }

        if (sync_completion_wait(&ar->offchan_tx_completed, ZX_SEC(3)) == ZX_ERR_TIMED_OUT) {
            ath10k_warn("timed out waiting for offchannel skb %pK\n", skb);
        }

        if (!peer && tmp_peer_created) {
            ret = ath10k_peer_delete(ar, vdev_id, peer_addr);
            if (ret)
                ath10k_warn("failed to delete peer %pM on vdev %d: %d\n",
                            peer_addr, vdev_id, ret);
        }

        mtx_unlock(&ar->conf_mutex);
    }
}

void ath10k_mgmt_over_wmi_tx_purge(struct ath10k* ar) {
    struct sk_buff* skb;

    for (;;) {
        skb = skb_dequeue(&ar->wmi_mgmt_tx_queue);
        if (!skb) {
            break;
        }

        ieee80211_free_txskb(ar->hw, skb);
    }
}
#endif // NEEDS PORTING

/************/
/* Scanning */
/************/

void __ath10k_scan_finish(struct ath10k* ar) {
    ASSERT_MTX_HELD(&ar->data_lock);

    switch (ar->scan.state) {
    case ATH10K_SCAN_IDLE:
        break;
    case ATH10K_SCAN_RUNNING:
    case ATH10K_SCAN_ABORTING:
#if 0 // NEEDS PORTING
        if (!ar->scan.is_roc) {
            struct cfg80211_scan_info info = {
                .aborted = (ar->scan.state ==
                            ATH10K_SCAN_ABORTING),
            };

            ieee80211_scan_completed(ar->hw, &info);
        } else if (ar->scan.roc_notify) {
            ieee80211_remain_on_channel_expired(ar->hw);
        }
#endif // NEEDS PORTING
    /* fall through */
    case ATH10K_SCAN_STARTING:
        ar->scan.state = ATH10K_SCAN_IDLE;
        memset(&ar->scan_channel, 0, sizeof(wlan_channel_t));
        ar->scan.roc_freq = 0;
#if 0  // NEEDS PORTING
        ath10k_offchan_tx_purge(ar);
        cancel_delayed_work(&ar->scan.timeout);
#endif // NEEDS PORTING
        sync_completion_signal(&ar->scan.completed);
        break;
    }
}

void ath10k_scan_finish(struct ath10k* ar) {
    mtx_lock(&ar->data_lock);
    __ath10k_scan_finish(ar);
    mtx_unlock(&ar->data_lock);
}

#if 0 // NEEDS PORTING
static int ath10k_scan_stop(struct ath10k* ar) {
    struct wmi_stop_scan_arg arg = {
        .req_id = 1, /* FIXME */
        .req_type = WMI_SCAN_STOP_ONE,
        .u.scan_id = ATH10K_SCAN_ID,
    };
    int ret;

    ASSERT_MTX_HELD(&ar->conf_mutex);

    ret = ath10k_wmi_stop_scan(ar, &arg);
    if (ret) {
        ath10k_warn("failed to stop wmi scan: %d\n", ret);
        goto out;
    }

    zx_status_t status = sync_completion_wait(&ar->scan.completed, ZX_SEC(3));
    if (status == ZX_ERR_TIMED_OUT) {
        ath10k_warn("failed to receive scan abortion completion: timed out\n");
        ret = -ETIMEDOUT;
    } else if (status == ZX_OK) {
        ret = 0;
    } else {
        ZX_DEBUG_ASSERT(0);
    }

out:
    /* Scan state should be updated upon scan completion but in case
     * firmware fails to deliver the event (for whatever reason) it is
     * desired to clean up scan state anyway. Firmware may have just
     * dropped the scan completion event delivery due to transport pipe
     * being overflown with data and/or it can recover on its own before
     * next scan request is submitted.
     */
    mtx_lock(&ar->data_lock);
    if (ar->scan.state != ATH10K_SCAN_IDLE) {
        __ath10k_scan_finish(ar);
    }
    mtx_unlock(&ar->data_lock);

    return ret;
}

static void ath10k_scan_abort(struct ath10k* ar) {
    int ret;

    ASSERT_MTX_HELD(&ar->conf_mutex);

    mtx_lock(&ar->data_lock);

    switch (ar->scan.state) {
    case ATH10K_SCAN_IDLE:
        /* This can happen if timeout worker kicked in and called
         * abortion while scan completion was being processed.
         */
        break;
    case ATH10K_SCAN_STARTING:
    case ATH10K_SCAN_ABORTING:
        ath10k_warn("refusing scan abortion due to invalid scan state: %s (%d)\n",
                    ath10k_scan_state_str(ar->scan.state),
                    ar->scan.state);
        break;
    case ATH10K_SCAN_RUNNING:
        ar->scan.state = ATH10K_SCAN_ABORTING;
        mtx_unlock(&ar->data_lock);

        ret = ath10k_scan_stop(ar);
        if (ret) {
            ath10k_warn("failed to abort scan: %d\n", ret);
        }

        mtx_lock(&ar->data_lock);
        break;
    }

    mtx_unlock(&ar->data_lock);
}

void ath10k_scan_timeout_work(struct work_struct* work) {
    struct ath10k* ar = container_of(work, struct ath10k,
                                     scan.timeout.work);

    mtx_lock(&ar->conf_mutex);
    ath10k_scan_abort(ar);
    mtx_unlock(&ar->conf_mutex);
}

static int ath10k_start_scan(struct ath10k* ar,
                             const struct wmi_start_scan_arg* arg) {
    int ret;

    ASSERT_MTX_HELD(&ar->conf_mutex);

    ret = ath10k_wmi_start_scan(ar, arg);
    if (ret) {
        return ret;
    }

    if (sync_completion_wait(&ar->scan.started, ZX_SEC(1)) == ZX_ERR_TIMED_OUT) {
        ret = ath10k_scan_stop(ar);
        if (ret) {
            ath10k_warn("failed to stop scan: %d\n", ret);
        }

        return -ETIMEDOUT;
    }

    /* If we failed to start the scan, return error code at
     * this point.  This is probably due to some issue in the
     * firmware, but no need to wedge the driver due to that...
     */
    mtx_lock(&ar->data_lock);
    if (ar->scan.state == ATH10K_SCAN_IDLE) {
        mtx_unlock(&ar->data_lock);
        return -EINVAL;
    }
    mtx_unlock(&ar->data_lock);

    return 0;
}
#endif // NEEDS PORTING

/**********************/
/* mac80211 callbacks */
/**********************/

static zx_status_t ath10k_mac_build_tx_pkt(struct ath10k* ar,
                                           struct ath10k_msg_buf** tx_buf_ptr,
                                           wlan_tx_packet_t* pkt,
                                           enum ath10k_mac_tx_path txpath) {
    enum ath10k_msg_type buf_type;

    switch(txpath) {
    case ATH10K_MAC_TX_HTT:
    case ATH10K_MAC_TX_HTT_MGMT:
    case ATH10K_MAC_TX_WMI_MGMT:
        buf_type = ATH10K_MSG_TYPE_BASE;
        break;
    default:
        return ZX_ERR_INVALID_ARGS;
    }

    struct ath10k_msg_buf* tx_buf;
    ZX_DEBUG_ASSERT(pkt->packet_head);
    size_t head_size = pkt->packet_head->len;
    size_t tail_size = pkt->packet_tail ? (pkt->packet_tail->len - pkt->tail_offset) : 0;
    // This 64 gives us headroom to add fields. It would be nice if we could be more specific...
    size_t extra_bytes = head_size + tail_size + 64;

    zx_status_t ret = ath10k_msg_buf_alloc(ar, &tx_buf, buf_type, extra_bytes);
    if (ret != ZX_OK) {
        ath10k_err("failed to allocate a tx buffer\n");
        return ret;
    }
    tx_buf->used -= 64;

    uint8_t* next_data = ath10k_msg_buf_get_payload(tx_buf);
    memcpy(next_data, pkt->packet_head->data, head_size);
    next_data += head_size;
    if (tail_size > 0) {
        memcpy(next_data, (pkt->packet_tail->data + pkt->tail_offset), tail_size);
    }

    *tx_buf_ptr = tx_buf;
    return ZX_OK;
}

zx_status_t ath10k_mac_op_tx(struct ath10k* ar,
                             wlan_tx_packet_t* pkt) {
    struct ath10k_htt* htt = &ar->htt;

    enum ath10k_hw_txrx_mode txmode = ath10k_mac_tx_h_get_txmode(ar, pkt->packet_head->data);
    enum ath10k_mac_tx_path txpath = ath10k_mac_tx_h_get_txpath(ar, txmode);

    if (txpath == ATH10K_MAC_TX_UNKNOWN) {
        ath10k_err("unable to determine path for tx packet\n");
        return ZX_ERR_INTERNAL;
    }

    struct ath10k_msg_buf* tx_buf;
    zx_status_t ret = ath10k_mac_build_tx_pkt(ar, &tx_buf, pkt, txpath);
    if (ret != ZX_OK) {
        return ret;
    }

    bool is_htt = (txpath == ATH10K_MAC_TX_HTT || txpath == ATH10K_MAC_TX_HTT_MGMT);
    bool is_mgmt = (txpath == ATH10K_MAC_TX_HTT_MGMT);

    ath10k_mac_tx_h_tx_flags(ar, tx_buf, &pkt->info);

    struct ieee80211_frame_header* hdr = pkt->packet_head->data;

    if (is_htt) {
        mtx_lock(&ar->htt.tx_lock);
        bool is_presp = (ieee80211_get_frame_type(hdr) == IEEE80211_FRAME_TYPE_MGMT)
                        && (ieee80211_get_frame_subtype(hdr) == IEEE80211_FRAME_SUBTYPE_PROBE_RESP);

        ret = ath10k_htt_tx_inc_pending(htt);
        if (ret != ZX_OK) {
            ath10k_warn("failed to increase tx pending count: %s, dropping\n",
                        zx_status_get_string(ret));
            mtx_unlock(&ar->htt.tx_lock);
            ath10k_msg_buf_free(tx_buf);
            return ret;
        }

        ret = ath10k_htt_tx_mgmt_inc_pending(htt, is_mgmt, is_presp);
        if (ret != ZX_OK) {
            ath10k_warn("failed to increase tx mgmt pending count: %s, dropping\n",
                        zx_status_get_string(ret));
            ath10k_htt_tx_dec_pending(htt);
            mtx_unlock(&ar->htt.tx_lock);
            ath10k_msg_buf_free(tx_buf);
            return ret;
        }
        mtx_unlock(&ar->htt.tx_lock);
    }

    ret = ath10k_mac_tx(ar, txmode, txpath, tx_buf);
    if (ret != ZX_OK) {
        ath10k_warn("failed to transmit frame: %d\n", ret);
        if (is_htt) {
            mtx_lock(&ar->htt.tx_lock);
            ath10k_htt_tx_dec_pending(htt);
            if (is_mgmt) {
                ath10k_htt_tx_mgmt_dec_pending(htt);
            }
            mtx_unlock(&ar->htt.tx_lock);
        }
        ath10k_msg_buf_free(tx_buf);
        return ret;
    }
    return ZX_OK;
}

#if 0 // NEEDS PORTING
static void ath10k_mac_op_wake_tx_queue(struct ieee80211_hw* hw,
                                        struct ieee80211_txq* txq) {
    struct ath10k* ar = hw->priv;
    struct ath10k_txq* artxq = (void*)txq->drv_priv;
    struct ieee80211_txq* f_txq;
    struct ath10k_txq* f_artxq;
    int ret = 0;
    int max = 16;

    mtx_lock(&ar->txqs_lock);
    if (list_empty(&artxq->list)) {
        list_add_tail(&artxq->list, &ar->txqs);
    }

    f_artxq = list_first_entry(&ar->txqs, struct ath10k_txq, list);
    f_txq = container_of((void*)f_artxq, struct ieee80211_txq, drv_priv);
    list_del_init(&f_artxq->list);

    while (ath10k_mac_tx_can_push(hw, f_txq) && max--) {
        ret = ath10k_mac_tx_push_txq(hw, f_txq);
        if (ret) {
            break;
        }
    }
    if (ret != -ENOENT) {
        list_add_tail(&f_artxq->list, &ar->txqs);
    }
    mtx_unlock(&ar->txqs_lock);

    ath10k_htt_tx_txq_update(hw, f_txq);
    ath10k_htt_tx_txq_update(hw, txq);
}
#endif // NEEDS PORTING

/* Must not be called with conf_mutex held as workers can use that also. */
void ath10k_drain_tx(struct ath10k* ar) {
#if 0 // NEEDS PORTING
    /* make sure rcu-protected mac80211 tx path itself is drained */
    synchronize_net();

    ath10k_offchan_tx_purge(ar);
    ath10k_mgmt_over_wmi_tx_purge(ar);

    cancel_work_sync(&ar->offchan_tx_work);
    cancel_work_sync(&ar->wmi_mgmt_tx_work);
#endif // NEEDS PORTING
}

#if 0 // NEEDS PORTING
void ath10k_halt(struct ath10k* ar) {
    struct ath10k_vif* arvif;

    ASSERT_MTX_HELD(&ar->conf_mutex);

    BITARR_CLEAR(&ar->dev_flags, ATH10K_CAC_RUNNING);
    ar->filter_flags = 0;
    ar->monitor = false;
    ar->monitor_arvif = NULL;

    if (ar->monitor_started) {
        ath10k_monitor_stop(ar);
    }

    ar->monitor_started = false;
    ar->tx_paused = 0;

    ath10k_scan_finish(ar);
    ath10k_peer_cleanup_all(ar);
    ath10k_core_stop(ar);
    ath10k_hif_power_down(ar);

    mtx_lock(&ar->data_lock);
    list_for_each_entry(arvif, &ar->arvifs, list)
    ath10k_mac_vif_beacon_cleanup(arvif);
    mtx_unlock(&ar->data_lock);
}

static int ath10k_get_antenna(struct ieee80211_hw* hw, uint32_t* tx_ant, uint32_t* rx_ant) {
    struct ath10k* ar = hw->priv;

    mtx_lock(&ar->conf_mutex);

    *tx_ant = ar->cfg_tx_chainmask;
    *rx_ant = ar->cfg_rx_chainmask;

    mtx_unlock(&ar->conf_mutex);

    return 0;
}
#endif // NEEDS PORTING

static void ath10k_check_chain_mask(struct ath10k* ar, uint32_t cm, const char* dbg) {
    /* It is not clear that allowing gaps in chainmask
     * is helpful.  Probably it will not do what user
     * is hoping for, so warn in that case.
     */
    if (cm == 15 || cm == 7 || cm == 3 || cm == 1 || cm == 0) {
        return;
    }

    ath10k_warn("mac %s antenna chainmask may be invalid: 0x%x.  "
                "Suggested values: 15, 7, 3, 1 or 0.\n",
                dbg, cm);
}

#if 0 // NEEDS PORTING
static int ath10k_mac_get_vht_cap_bf_sts(struct ath10k* ar) {
    int nsts = ar->vht_cap_info;

    nsts &= IEEE80211_VHT_CAP_BEAMFORMEE_STS_MASK;
    nsts >>= IEEE80211_VHT_CAP_BEAMFORMEE_STS_SHIFT;

    /* If firmware does not deliver to host number of space-time
     * streams supported, assume it support up to 4 BF STS and return
     * the value for VHT CAP: nsts-1)
     */
    if (nsts == 0) {
        return 3;
    }

    return nsts;
}

static int ath10k_mac_get_vht_cap_bf_sound_dim(struct ath10k* ar) {
    int sound_dim = ar->vht_cap_info;

    sound_dim &= IEEE80211_VHT_CAP_SOUNDING_DIMENSIONS_MASK;
    sound_dim >>= IEEE80211_VHT_CAP_SOUNDING_DIMENSIONS_SHIFT;

    /* If the sounding dimension is not advertised by the firmware,
     * let's use a default value of 1
     */
    if (sound_dim == 0) {
        return 1;
    }

    return sound_dim;
}

static struct ieee80211_sta_vht_cap ath10k_create_vht_cap(struct ath10k* ar) {
    struct ieee80211_sta_vht_cap vht_cap = {0};
    struct ath10k_hw_params* hw = &ar->hw_params;
    uint16_t mcs_map;
    uint32_t val;
    int i;

    vht_cap.vht_supported = 1;
    vht_cap.cap = ar->vht_cap_info;

    if (ar->vht_cap_info & (IEEE80211_VHT_CAP_SU_BEAMFORMEE_CAPABLE |
                            IEEE80211_VHT_CAP_MU_BEAMFORMEE_CAPABLE)) {
        val = ath10k_mac_get_vht_cap_bf_sts(ar);
        val <<= IEEE80211_VHT_CAP_BEAMFORMEE_STS_SHIFT;
        val &= IEEE80211_VHT_CAP_BEAMFORMEE_STS_MASK;

        vht_cap.cap |= val;
    }

    if (ar->vht_cap_info & (IEEE80211_VHT_CAP_SU_BEAMFORMER_CAPABLE |
                            IEEE80211_VHT_CAP_MU_BEAMFORMER_CAPABLE)) {
        val = ath10k_mac_get_vht_cap_bf_sound_dim(ar);
        val <<= IEEE80211_VHT_CAP_SOUNDING_DIMENSIONS_SHIFT;
        val &= IEEE80211_VHT_CAP_SOUNDING_DIMENSIONS_MASK;

        vht_cap.cap |= val;
    }

    /* Currently the firmware seems to be buggy, don't enable 80+80
     * mode until that's resolved.
     */
    if ((ar->vht_cap_info & IEEE80211_VHT_CAP_SHORT_GI_160) &&
            (ar->vht_cap_info & IEEE80211_VHT_CAP_SUPP_CHAN_WIDTH_MASK) == 0) {
        vht_cap.cap |= IEEE80211_VHT_CAP_SUPP_CHAN_WIDTH_160MHZ;
    }

    mcs_map = 0;
    for (i = 0; i < 8; i++) {
        if ((i < ar->num_rf_chains) && (ar->cfg_tx_chainmask & BIT(i))) {
            mcs_map |= IEEE80211_VHT_MCS_SUPPORT_0_9 << (i * 2);
        } else {
            mcs_map |= IEEE80211_VHT_MCS_NOT_SUPPORTED << (i * 2);
        }
    }

    if (ar->cfg_tx_chainmask <= 1) {
        vht_cap.cap &= ~IEEE80211_VHT_CAP_TXSTBC;
    }

    vht_cap.vht_mcs.rx_mcs_map = mcs_map;
    vht_cap.vht_mcs.tx_mcs_map = mcs_map;

    /* If we are supporting 160Mhz or 80+80, then the NIC may be able to do
     * a restricted NSS for 160 or 80+80 vs what it can do for 80Mhz.  Give
     * user-space a clue if that is the case.
     */
    if ((vht_cap.cap & IEEE80211_VHT_CAP_SUPP_CHAN_WIDTH_MASK) &&
            (hw->vht160_mcs_rx_highest != 0 ||
             hw->vht160_mcs_tx_highest != 0)) {
        vht_cap.vht_mcs.rx_highest = hw->vht160_mcs_rx_highest;
        vht_cap.vht_mcs.tx_highest = hw->vht160_mcs_tx_highest;
    }

    return vht_cap;
}

static struct ieee80211_sta_ht_cap ath10k_get_ht_cap(struct ath10k* ar) {
    int i;
    struct ieee80211_sta_ht_cap ht_cap = {0};

    if (!(ar->ht_cap_info & WMI_HT_CAP_ENABLED)) {
        return ht_cap;
    }

    ht_cap.ht_supported = 1;
    ht_cap.ampdu_factor = IEEE80211_HT_MAX_AMPDU_64K;
    ht_cap.ampdu_density = IEEE80211_HT_MPDU_DENSITY_8;
    ht_cap.cap |= IEEE80211_HT_CAP_SUP_WIDTH_20_40;
    ht_cap.cap |= IEEE80211_HT_CAP_DSSSCCK40;
    ht_cap.cap |=
        WLAN_HT_CAP_SM_PS_DISABLED << IEEE80211_HT_CAP_SM_PS_SHIFT;

    if (ar->ht_cap_info & WMI_HT_CAP_HT20_SGI) {
        ht_cap.cap |= IEEE80211_HT_CAP_SGI_20;
    }

    if (ar->ht_cap_info & WMI_HT_CAP_HT40_SGI) {
        ht_cap.cap |= IEEE80211_HT_CAP_SGI_40;
    }

    if (ar->ht_cap_info & WMI_HT_CAP_DYNAMIC_SMPS) {
        uint32_t smps;

        smps   = WLAN_HT_CAP_SM_PS_DYNAMIC;
        smps <<= IEEE80211_HT_CAP_SM_PS_SHIFT;

        ht_cap.cap |= smps;
    }

    if (ar->ht_cap_info & WMI_HT_CAP_TX_STBC && (ar->cfg_tx_chainmask > 1)) {
        ht_cap.cap |= IEEE80211_HT_CAP_TX_STBC;
    }

    if (ar->ht_cap_info & WMI_HT_CAP_RX_STBC) {
        uint32_t stbc;

        stbc   = ar->ht_cap_info;
        stbc  &= WMI_HT_CAP_RX_STBC;
        stbc >>= WMI_HT_CAP_RX_STBC_MASK_SHIFT;
        stbc <<= IEEE80211_HT_CAP_RX_STBC_SHIFT;
        stbc  &= IEEE80211_HT_CAP_RX_STBC;

        ht_cap.cap |= stbc;
    }

    if (ar->ht_cap_info & WMI_HT_CAP_LDPC) {
        ht_cap.cap |= IEEE80211_HT_CAP_LDPC_CODING;
    }

    if (ar->ht_cap_info & WMI_HT_CAP_L_SIG_TXOP_PROT) {
        ht_cap.cap |= IEEE80211_HT_CAP_LSIG_TXOP_PROT;
    }

    /* max AMSDU is implicitly taken from vht_cap_info */
    if (ar->vht_cap_info & WMI_VHT_CAP_MAX_MPDU_LEN_MASK) {
        ht_cap.cap |= IEEE80211_HT_CAP_MAX_AMSDU;
    }

    for (i = 0; i < ar->num_rf_chains; i++) {
        if (ar->cfg_rx_chainmask & BIT(i)) {
            ht_cap.mcs.rx_mask[i] = 0xFF;
        }
    }

    ht_cap.mcs.tx_params |= IEEE80211_HT_MCS_TX_DEFINED;

    return ht_cap;
}

static void ath10k_mac_setup_ht_vht_cap(struct ath10k* ar) {
    struct ieee80211_supported_band* band;
    struct ieee80211_sta_vht_cap vht_cap;
    struct ieee80211_sta_ht_cap ht_cap;

    ht_cap = ath10k_get_ht_cap(ar);
    vht_cap = ath10k_create_vht_cap(ar);

    if (ar->phy_capability & WHAL_WLAN_11G_CAPABILITY) {
        band = &ar->mac.sbands[NL80211_BAND_2GHZ];
        band->ht_cap = ht_cap;
    }
    if (ar->phy_capability & WHAL_WLAN_11A_CAPABILITY) {
        band = &ar->mac.sbands[NL80211_BAND_5GHZ];
        band->ht_cap = ht_cap;
        band->vht_cap = vht_cap;
    }
}
#endif // NEEDS PORTING

static zx_status_t __ath10k_set_antenna(struct ath10k* ar, uint32_t tx_ant, uint32_t rx_ant) {
    zx_status_t ret;

    ASSERT_MTX_HELD(&ar->conf_mutex);

    ath10k_check_chain_mask(ar, tx_ant, "tx");
    ath10k_check_chain_mask(ar, rx_ant, "rx");

    ar->cfg_tx_chainmask = tx_ant;
    ar->cfg_rx_chainmask = rx_ant;

    if ((ar->state != ATH10K_STATE_ON) && (ar->state != ATH10K_STATE_RESTARTED)) {
        return ZX_OK;
    }

    ret = ath10k_wmi_pdev_set_param(ar, ar->wmi.pdev_param->tx_chain_mask,
                                    tx_ant);
    if (ret != ZX_OK) {
        ath10k_warn("failed to set tx-chainmask: %d, req 0x%x\n", ret, tx_ant);
        return ret;
    }

    ret = ath10k_wmi_pdev_set_param(ar, ar->wmi.pdev_param->rx_chain_mask, rx_ant);
    if (ret != ZX_OK) {
        ath10k_warn("failed to set rx-chainmask: %d, req 0x%x\n", ret, rx_ant);
        return ret;
    }

#if 0  // NEEDS PORTING
    /* Reload HT/VHT capability */
    ath10k_mac_setup_ht_vht_cap(ar);
#endif // NEEDS PORTING

    return ZX_OK;
}

#if 0 // NEEDS PORTING
static zx_status_t ath10k_set_antenna(struct ieee80211_hw* hw, uint32_t tx_ant, uint32_t rx_ant) {
    struct ath10k* ar = hw->priv;
    zx_status_t ret;

    mtx_lock(&ar->conf_mutex);
    ret = __ath10k_set_antenna(ar, tx_ant, rx_ant);
    mtx_unlock(&ar->conf_mutex);
    return ret;
}
#endif // NEEDS PORTING

enum { IEEE80211_AC_VO, IEEE80211_AC_VI, IEEE80211_AC_BE, IEEE80211_AC_BK };
static int ath10k_conf_tx(struct ath10k* ar, uint16_t ac, struct wmi_wmm_params_arg* params);

zx_status_t ath10k_start(struct ath10k* ar, wlanmac_ifc_t* ifc, void* cookie) {
    zx_status_t ret = ZX_OK;

    mtx_lock(&ar->conf_mutex);
    if (!BITARR_TEST(ar->dev_flags, ATH10K_FLAG_CORE_REGISTERED)) {
        ret = ZX_ERR_BAD_STATE;
        goto err;
    }

    ar->wlanmac.ifc = ifc;
    ar->wlanmac.cookie = cookie;

    /*
     * This makes sense only when restarting hw. It is harmless to call
     * unconditionally. This is necessary to make sure no HTT/WMI tx
     * commands will be submitted while restarting.
     */
    ath10k_drain_tx(ar);

    switch (ar->state) {
    case ATH10K_STATE_OFF:
        ar->state = ATH10K_STATE_ON;
        break;
    case ATH10K_STATE_RESTARTING:
        ar->state = ATH10K_STATE_RESTARTED;
        break;
    case ATH10K_STATE_ON:
    case ATH10K_STATE_RESTARTED:
    case ATH10K_STATE_WEDGED:
        WARN_ONCE();
        ret = ZX_ERR_INVALID_ARGS;
        goto err;
    case ATH10K_STATE_UTF:
        ret = ZX_ERR_BAD_STATE;
        goto err;
    }

#if 0 // NEEDS PORTING
    param = ar->wmi.pdev_param->pmf_qos;
    ret = ath10k_wmi_pdev_set_param(ar, param, 1);
    if (ret) {
        ath10k_warn("failed to enable PMF QOS: %d\n", ret);
        goto err_core_stop;
    }

    param = ar->wmi.pdev_param->dynamic_bw;
    ret = ath10k_wmi_pdev_set_param(ar, param, 1);
    if (ret) {
        ath10k_warn("failed to enable dynamic BW: %d\n", ret);
        goto err_core_stop;
    }

    if (BITARR_TEST(ar->wmi.svc_map, WMI_SERVICE_ADAPTIVE_OCS)) {
        ret = ath10k_wmi_adaptive_qcs(ar, true);
        if (ret) {
            ath10k_warn("failed to enable adaptive qcs: %d\n",
                        ret);
            goto err_core_stop;
        }
    }

    if (BITARR_TEST(ar->wmi.svc_map, WMI_SERVICE_BURST)) {
        param = ar->wmi.pdev_param->burst_enable;
        ret = ath10k_wmi_pdev_set_param(ar, param, 0);
        if (ret) {
            ath10k_warn("failed to disable burst: %d\n", ret);
            goto err_core_stop;
        }
    }
#endif // NEEDS PORTING

    __ath10k_set_antenna(ar, ar->cfg_tx_chainmask, ar->cfg_rx_chainmask);

#if 0 // NEEDS PORTING
    /*
     * By default FW set ARP frames ac to voice (6). In that case ARP
     * exchange is not working properly for UAPSD enabled AP. ARP requests
     * which arrives with access category 0 are processed by network stack
     * and send back with access category 0, but FW changes access category
     * to 6. Set ARP frames access category to best effort (0) solves
     * this problem.
     */

    param = ar->wmi.pdev_param->arp_ac_override;
    ret = ath10k_wmi_pdev_set_param(ar, param, 0);
    if (ret) {
        ath10k_warn("failed to set arp ac override parameter: %d\n",
                    ret);
        goto err_core_stop;
    }

    if (BITARR_TEST(ar->running_fw->fw_file.fw_features,
                    ATH10K_FW_FEATURE_SUPPORTS_ADAPTIVE_CCA)) {
        ret = ath10k_wmi_pdev_enable_adaptive_cca(ar, 1,
                WMI_CCA_DETECT_LEVEL_AUTO,
                WMI_CCA_DETECT_MARGIN_AUTO);
        if (ret) {
            ath10k_warn("failed to enable adaptive cca: %d\n",
                        ret);
            goto err_core_stop;
        }
    }

    param = ar->wmi.pdev_param->ani_enable;
    ret = ath10k_wmi_pdev_set_param(ar, param, 1);
    if (ret) {
        ath10k_warn("failed to enable ani by default: %d\n",
                    ret);
        goto err_core_stop;
    }

    ar->ani_enabled = true;

    if (ath10k_peer_stats_enabled(ar)) {
        param = ar->wmi.pdev_param->peer_stats_update_period;
        ret = ath10k_wmi_pdev_set_param(ar, param,
                                        PEER_DEFAULT_STATS_UPDATE_PERIOD);
        if (ret) {
            ath10k_warn("failed to set peer stats period : %d\n",
                        ret);
            goto err_core_stop;
        }
    }

    param = ar->wmi.pdev_param->enable_btcoex;
    if (BITARR_TEST(ar->wmi.svc_map, WMI_SERVICE_COEX_GPIO) &&
            BITARR_TEST(ar->running_fw->fw_file.fw_features, ATH10K_FW_FEATURE_BTCOEX_PARAM)) {
        ret = ath10k_wmi_pdev_set_param(ar, param, 0);
        if (ret) {
            ath10k_warn("failed to set btcoex param: %d\n", ret);
            goto err_core_stop;
        }
        BITARR_CLEAR(&ar->dev_flags, ATH10K_FLAG_BTCOEX);
    }
#endif // NEEDS PORTING

    ar->num_started_vdevs = 0;
    ath10k_regd_update(ar);
    ath10k_add_interface(ar, WLAN_MAC_ROLE_CLIENT);

    struct wmi_wmm_params_arg wmm_params;

    wmm_params.cwmin = 3;
    wmm_params.cwmax = 7;
    wmm_params.aifs = 2;
    wmm_params.txop = 102 * 32;
    wmm_params.acm = 0;
    wmm_params.no_ack = 0;
    ath10k_conf_tx(ar, IEEE80211_AC_VO, &wmm_params);

    wmm_params.cwmin = 7;
    wmm_params.cwmax = 15;
    wmm_params.aifs = 2;
    wmm_params.txop = 188 * 32;
    wmm_params.acm = 0;
    wmm_params.no_ack = 0;
    ath10k_conf_tx(ar, IEEE80211_AC_VI, &wmm_params);

    wmm_params.cwmin = 15;
    wmm_params.cwmax = 1023;
    wmm_params.aifs = 3;
    wmm_params.txop = 0 * 32;
    wmm_params.acm = 0;
    wmm_params.no_ack = 0;
    ath10k_conf_tx(ar, IEEE80211_AC_BE, &wmm_params);

    wmm_params.cwmin = 15;
    wmm_params.cwmax = 1023;
    wmm_params.aifs = 7;
    wmm_params.txop = 0 * 32;
    wmm_params.acm = 0;
    wmm_params.no_ack = 0;
    ath10k_conf_tx(ar, IEEE80211_AC_BK, &wmm_params);

#if 0 // NEEDS PORTING
    ath10k_spectral_start(ar);
    ath10k_thermal_set_throttling(ar);
#endif // NEEDS PORTING

    mtx_unlock(&ar->conf_mutex);
    return ZX_OK;

#if 0 // NEEDS PORTING
err_core_stop:
    ath10k_core_stop(ar);

err_power_down:
    ath10k_hif_power_down(ar);

err_off:
    ar->state = ATH10K_STATE_OFF;
#endif // NEEDS PORTING

err:
    mtx_unlock(&ar->conf_mutex);
    return ret;
}

#if 0 // NEEDS PORTING
static void ath10k_stop(struct ieee80211_hw* hw) {
    struct ath10k* ar = hw->priv;

    ath10k_drain_tx(ar);

    mtx_lock(&ar->conf_mutex);
    if (ar->state != ATH10K_STATE_OFF) {
        ath10k_halt(ar);
        ar->state = ATH10K_STATE_OFF;
    }
    mtx_unlock(&ar->conf_mutex);

    cancel_work_sync(&ar->set_coverage_class_work);
    cancel_delayed_work_sync(&ar->scan.timeout);
    cancel_work_sync(&ar->restart_work);
}

static int ath10k_config_ps(struct ath10k* ar) {
    struct ath10k_vif* arvif;
    int ret = 0;

    ASSERT_MTX_HELD(&ar->conf_mutex);

    list_for_each_entry(arvif, &ar->arvifs, list) {
        ret = ath10k_mac_vif_setup_ps(arvif);
        if (ret) {
            ath10k_warn("failed to setup powersave: %d\n", ret);
            break;
        }
    }

    return ret;
}
#endif // NEEDS PORTING

static zx_status_t ath10k_mac_txpower_setup(struct ath10k* ar, int txpower) {
    zx_status_t ret;
    uint32_t param;

    ASSERT_MTX_HELD(&ar->conf_mutex);

    ath10k_dbg(ar, ATH10K_DBG_MAC, "mac txpower %d\n", txpower);

    param = ar->wmi.pdev_param->txpower_limit2g;
    ret = ath10k_wmi_pdev_set_param(ar, param, txpower * 2);
    if (ret != ZX_OK) {
        ath10k_warn("failed to set 2g txpower %d: %s\n",
                    txpower, zx_status_get_string(ret));
        return ret;
    }

    param = ar->wmi.pdev_param->txpower_limit5g;
    ret = ath10k_wmi_pdev_set_param(ar, param, txpower * 2);
    if (ret != ZX_OK) {
        ath10k_warn("failed to set 5g txpower %d: %s\n",
                    txpower, zx_status_get_string(ret));
        return ret;
    }

    return ZX_OK;
}

static zx_status_t ath10k_mac_txpower_recalc(struct ath10k* ar) {
    struct ath10k_vif* arvif = &ar->arvif;
    zx_status_t ret;

    ASSERT_MTX_HELD(&ar->conf_mutex);

    int txpower = arvif->txpower;

    if (txpower == -1) {
        return ZX_OK;
    }

    ret = ath10k_mac_txpower_setup(ar, txpower);
    if (ret != ZX_OK) {
        ath10k_warn("failed to setup tx power %d: %s\n",
                    txpower, zx_status_get_string(ret));
        return ret;
    }

    return ZX_OK;
}

#if 0 // NEEDS PORTING
static int ath10k_config(struct ieee80211_hw* hw, uint32_t changed) {
    struct ath10k* ar = hw->priv;
    struct ieee80211_conf* conf = &hw->conf;
    int ret = 0;

    mtx_lock(&ar->conf_mutex);

    if (changed & IEEE80211_CONF_CHANGE_PS) {
        ath10k_config_ps(ar);
    }

    if (changed & IEEE80211_CONF_CHANGE_MONITOR) {
        ar->monitor = conf->flags & IEEE80211_CONF_MONITOR;
        ret = ath10k_monitor_recalc(ar);
        if (ret) {
            ath10k_warn("failed to recalc monitor: %d\n", ret);
        }
    }

    mtx_unlock(&ar->conf_mutex);
    return ret;
}

static uint32_t get_nss_from_chainmask(uint16_t chain_mask) {
    if ((chain_mask & 0xf) == 0xf) {
        return 4;
    } else if ((chain_mask & 0x7) == 0x7) {
        return 3;
    } else if ((chain_mask & 0x3) == 0x3) {
        return 2;
    }
    return 1;
}

static int ath10k_mac_set_txbf_conf(struct ath10k_vif* arvif) {
    uint32_t value = 0;
    struct ath10k* ar = arvif->ar;
    int nsts;
    int sound_dim;

    if (ath10k_wmi_get_txbf_conf_scheme(ar) != WMI_TXBF_CONF_BEFORE_ASSOC) {
        return 0;
    }

    nsts = ath10k_mac_get_vht_cap_bf_sts(ar);
    if (ar->vht_cap_info & (IEEE80211_VHT_CAP_SU_BEAMFORMEE_CAPABLE |
                            IEEE80211_VHT_CAP_MU_BEAMFORMEE_CAPABLE)) {
        value |= SM(nsts, WMI_TXBF_STS_CAP_OFFSET);
    }

    sound_dim = ath10k_mac_get_vht_cap_bf_sound_dim(ar);
    if (ar->vht_cap_info & (IEEE80211_VHT_CAP_SU_BEAMFORMER_CAPABLE |
                            IEEE80211_VHT_CAP_MU_BEAMFORMER_CAPABLE)) {
        value |= SM(sound_dim, WMI_BF_SOUND_DIM_OFFSET);
    }

    if (!value) {
        return 0;
    }

    if (ar->vht_cap_info & IEEE80211_VHT_CAP_SU_BEAMFORMER_CAPABLE) {
        value |= WMI_VDEV_PARAM_TXBF_SU_TX_BFER;
    }

    if (ar->vht_cap_info & IEEE80211_VHT_CAP_MU_BEAMFORMER_CAPABLE)
        value |= (WMI_VDEV_PARAM_TXBF_MU_TX_BFER |
                  WMI_VDEV_PARAM_TXBF_SU_TX_BFER);

    if (ar->vht_cap_info & IEEE80211_VHT_CAP_SU_BEAMFORMEE_CAPABLE) {
        value |= WMI_VDEV_PARAM_TXBF_SU_TX_BFEE;
    }

    if (ar->vht_cap_info & IEEE80211_VHT_CAP_MU_BEAMFORMEE_CAPABLE)
        value |= (WMI_VDEV_PARAM_TXBF_MU_TX_BFEE |
                  WMI_VDEV_PARAM_TXBF_SU_TX_BFEE);

    return ath10k_wmi_vdev_set_param(ar, arvif->vdev_id,
                                     ar->wmi.vdev_param->txbf, value);
}
#endif // NEEDS PORTING

// Role is one of the supported roles in WLAN_MAC_ROLE_* values
static zx_status_t ath10k_add_interface(struct ath10k* ar, uint32_t vif_role) {
    struct ath10k_vif* arvif = &ar->arvif;
    zx_status_t ret = ZX_OK;

    ASSERT_MTX_HELD(&ar->conf_mutex);

    memset(arvif, 0, sizeof(*arvif));

    arvif->ar = ar;
#if 0 // NEEDS PORTING
    arvif->vif = vif;

    INIT_LIST_HEAD(&arvif->list);
    INIT_WORK(&arvif->ap_csa_work, ath10k_mac_vif_ap_csa_work);
    INIT_DELAYED_WORK(&arvif->connection_loss_work,
                      ath10k_mac_vif_sta_connection_loss_work);

    for (unsigned i = 0; i < countof(arvif->bitrate_mask.control); i++) {
        arvif->bitrate_mask.control[i].legacy = 0xffffffff;
        memset(arvif->bitrate_mask.control[i].ht_mcs, 0xff,
               sizeof(arvif->bitrate_mask.control[i].ht_mcs));
        memset(arvif->bitrate_mask.control[i].vht_mcs, 0xff,
               sizeof(arvif->bitrate_mask.control[i].vht_mcs));
    }
#endif // NEEDS PORTING

    if (ar->num_peers >= ar->max_num_peers) {
        ath10k_warn(
            "refusing vdev creation due to insufficient peer entry resources in firmware\n");
        ret = ZX_ERR_NO_RESOURCES;
        goto err;
    }

    if (ar->free_vdev_map == 0) {
        ath10k_warn("Free vdev map is empty, no more interfaces allowed.\n");
        ret = ZX_ERR_NO_RESOURCES;
        goto err;
    }
    unsigned bit = __builtin_ffsll(ar->free_vdev_map);

    ath10k_dbg(ar, ATH10K_DBG_MAC, "mac create vdev %i map %llx\n",
               bit, ar->free_vdev_map);

    arvif->vdev_id = bit;

    switch (vif_role) {
#if 0 // NEEDS PORTING
    case ATH10K_VIF_TYPE_P2P:
        arvif->vdev_type = WMI_VDEV_TYPE_STA;
        arvif->vdev_subtype = ath10k_wmi_get_vdev_subtype(ar, WMI_VDEV_SUBTYPE_P2P_DEVICE);
        break;
#endif // NEEDS PORTING
    case WLAN_MAC_ROLE_CLIENT:
        arvif->vdev_type = WMI_VDEV_TYPE_STA;
#if 0 // NEEDS PORTING
        if (vif->p2p)
            arvif->vdev_subtype = ath10k_wmi_get_vdev_subtype
                                  (ar, WMI_VDEV_SUBTYPE_P2P_CLIENT);
#endif // NEEDS PORTING
        break;
#if 0 // NEEDS PORTING
    case NL80211_IFTYPE_ADHOC:
        arvif->vdev_type = WMI_VDEV_TYPE_IBSS;
        break;
    case ATH10K_VIF_TYPE_MESH:
        if (BITARR_TEST(ar->wmi.svc_map, WMI_SERVICE_MESH_11S)) {
            arvif->vdev_subtype = ath10k_wmi_get_vdev_subtype
                                  (ar, WMI_VDEV_SUBTYPE_MESH_11S);
        } else if (!BITARR_TEST(ar->dev_flags, ATH10K_FLAG_RAW_MODE)) {
            ret = ZX_ERR_INVALID_ARGS;
            ath10k_warn("must load driver with rawmode=1 to add mesh interfaces\n");
            goto err;
        }
        arvif->vdev_type = WMI_VDEV_TYPE_AP;
        break;
#endif // NEEDS PORTING
    case WLAN_MAC_ROLE_AP:
        arvif->vdev_type = WMI_VDEV_TYPE_AP;

#if 0 // NEEDS PORTING
        if (vif->p2p)
            arvif->vdev_subtype = ath10k_wmi_get_vdev_subtype
                                  (ar, WMI_VDEV_SUBTYPE_P2P_GO);
        break;
    case ATH10K_VIF_TYPE_MONITOR:
        arvif->vdev_type = WMI_VDEV_TYPE_MONITOR;
        break;
#endif // NEEDS PORTING
    default:
        ath10k_warn("invalid network type specified when adding interface\n");
        return ZX_ERR_INVALID_ARGS;
    }

#if 0 // NEEDS PORTING
    /* Using vdev_id as queue number will make it very easy to do per-vif
     * tx queue locking. This shouldn't wrap due to interface combinations
     * but do a modulo for correctness sake and prevent using offchannel tx
     * queues for regular vif tx.
     */
    vif->cab_queue = arvif->vdev_id % (IEEE80211_MAX_QUEUES - 1);
    for (i = 0; i < countof(vif->hw_queue); i++) {
        vif->hw_queue[i] = arvif->vdev_id % (IEEE80211_MAX_QUEUES - 1);
    }

    /* Some firmware revisions don't wait for beacon tx completion before
     * sending another SWBA event. This could lead to hardware using old
     * (freed) beacon data in some cases, e.g. tx credit starvation
     * combined with missed TBTT. This is very very rare.
     *
     * On non-IOMMU-enabled hosts this could be a possible security issue
     * because hw could beacon some random data on the air.  On
     * IOMMU-enabled hosts DMAR faults would occur in most cases and target
     * device would crash.
     *
     * Since there are no beacon tx completions (implicit nor explicit)
     * propagated to host the only workaround for this is to allocate a
     * DMA-coherent buffer for a lifetime of a vif and use it for all
     * beacon tx commands. Worst case for this approach is some beacons may
     * become corrupted, e.g. have garbled IEs or out-of-date TIM bitmap.
     */
    if (vif_type == NL80211_IFTYPE_ADHOC
        || vif_type == NL80211_IFTYPE_MESH_POINT
        || vif_type == NL80211_IFTYPE_AP) {
        arvif->beacon_buf = dma_zalloc_coherent(ar->dev,
                                                IEEE80211_MAX_FRAME_LEN,
                                                &arvif->beacon_paddr,
                                                GFP_ATOMIC);
        if (!arvif->beacon_buf) {
            ret = -ENOMEM;
            ath10k_warn("failed to allocate beacon buffer: %d\n",
                        ret);
            goto err;
        }
    }
#endif // NEEDS PORTING
    if (BITARR_TEST(ar->dev_flags, ATH10K_FLAG_HW_CRYPTO_DISABLED)) {
        arvif->nohwcrypt = true;
    }

    if (arvif->nohwcrypt && !BITARR_TEST(ar->dev_flags, ATH10K_FLAG_RAW_MODE)) {
        ath10k_warn("cryptmode module param needed for sw crypto\n");
        goto err;
    }

    ath10k_dbg(ar, ATH10K_DBG_MAC,
               "mac vdev create %d (add interface) type %d subtype %d bcnmode %s\n",
               arvif->vdev_id, arvif->vdev_type, arvif->vdev_subtype,
               arvif->beacon_buf ? "single-buf" : "per-skb");

    ret = ath10k_wmi_vdev_create(ar, arvif->vdev_id, arvif->vdev_type,
                                 arvif->vdev_subtype, ar->mac_addr);
    if (ret != ZX_OK) {
        ath10k_warn("failed to create WMI vdev %i: %s\n",
                    arvif->vdev_id, zx_status_get_string(ret));
        goto err;
    }

    ar->free_vdev_map &= ~(1LL << arvif->vdev_id);
#if 0 // NEEDS PORTING
    mtx_lock(&ar->data_lock);
    list_add(&arvif->list, &ar->arvifs);
    mtx_unlock(&ar->data_lock);

    /* It makes no sense to have firmware do keepalives. mac80211 already
     * takes care of this with idle connection polling.
     */
    ret = ath10k_mac_vif_disable_keepalive(arvif);
    if (ret) {
        ath10k_warn("failed to disable keepalive on vdev %i: %d\n",
                    arvif->vdev_id, ret);
        goto err_vdev_delete;
    }

    arvif->def_wep_key_idx = -1;
#endif // NEEDS PORTING

    uint32_t vdev_param = ar->wmi.vdev_param->tx_encap_type;
    ret = ath10k_wmi_vdev_set_param(ar, arvif->vdev_id, vdev_param,
                                    ATH10K_HW_TXRX_NATIVE_WIFI);
    /* 10.X firmware does not support this VDEV parameter. Do not warn */
    if (ret != ZX_OK && ret != ZX_ERR_NOT_SUPPORTED) {
        ath10k_warn("failed to set vdev %i TX encapsulation: %d\n",
                    arvif->vdev_id, ret);
        goto err_vdev_delete;
    }

#if 0 // NEEDS PORTING
    /* Configuring number of spatial stream for monitor interface is causing
     * target assert in qca9888 and qca6174.
     */
    if (ar->cfg_tx_chainmask && (vif->type != NL80211_IFTYPE_MONITOR)) {
        uint16_t nss = get_nss_from_chainmask(ar->cfg_tx_chainmask);

        vdev_param = ar->wmi.vdev_param->nss;
        ret = ath10k_wmi_vdev_set_param(ar, arvif->vdev_id, vdev_param,
                                        nss);
        if (ret) {
            ath10k_warn("failed to set vdev %i chainmask 0x%x, nss %i: %d\n",
                        arvif->vdev_id, ar->cfg_tx_chainmask, nss,
                        ret);
            goto err_vdev_delete;
        }
    }

    if (arvif->vdev_type == WMI_VDEV_TYPE_AP ||
            arvif->vdev_type == WMI_VDEV_TYPE_IBSS) {
        ret = ath10k_peer_create(ar, vif, NULL, arvif->vdev_id,
                                 vif->addr, WMI_PEER_TYPE_DEFAULT);
        if (ret) {
            ath10k_warn("failed to create vdev %i peer for AP/IBSS: %d\n",
                        arvif->vdev_id, ret);
            goto err_vdev_delete;
        }

        mtx_lock(&ar->data_lock);

        peer = ath10k_peer_find(ar, arvif->vdev_id, vif->addr);
        if (!peer) {
            ath10k_warn("failed to lookup peer %pM on vdev %i\n",
                        vif->addr, arvif->vdev_id);
            mtx_unlock(&ar->data_lock);
            ret = -ENOENT;
            goto err_peer_delete;
        }

        arvif->peer_id = find_first_bit(peer->peer_ids,
                                        ATH10K_MAX_NUM_PEER_IDS);

        mtx_unlock(&ar->data_lock);
    } else {
        arvif->peer_id = HTT_INVALID_PEERID;
    }

    if (arvif->vdev_type == WMI_VDEV_TYPE_AP) {
        ret = ath10k_mac_set_kickout(arvif);
        if (ret) {
            ath10k_warn("failed to set vdev %i kickout parameters: %d\n",
                        arvif->vdev_id, ret);
            goto err_peer_delete;
        }
    }

    if (arvif->vdev_type == WMI_VDEV_TYPE_STA) {
        param = WMI_STA_PS_PARAM_RX_WAKE_POLICY;
        value = WMI_STA_PS_RX_WAKE_POLICY_WAKE;
        ret = ath10k_wmi_set_sta_ps_param(ar, arvif->vdev_id,
                                          param, value);
        if (ret) {
            ath10k_warn("failed to set vdev %i RX wake policy: %d\n",
                        arvif->vdev_id, ret);
            goto err_peer_delete;
        }

        ret = ath10k_mac_vif_recalc_ps_wake_threshold(arvif);
        if (ret) {
            ath10k_warn("failed to recalc ps wake threshold on vdev %i: %d\n",
                        arvif->vdev_id, ret);
            goto err_peer_delete;
        }

        ret = ath10k_mac_vif_recalc_ps_poll_count(arvif);
        if (ret) {
            ath10k_warn("failed to recalc ps poll count on vdev %i: %d\n",
                        arvif->vdev_id, ret);
            goto err_peer_delete;
        }
    }

    ret = ath10k_mac_set_txbf_conf(arvif);
    if (ret) {
        ath10k_warn("failed to set txbf for vdev %d: %d\n",
                    arvif->vdev_id, ret);
        goto err_peer_delete;
    }

    ret = ath10k_mac_set_rts(arvif, ar->hw->wiphy->rts_threshold);
    if (ret) {
        ath10k_warn("failed to set rts threshold for vdev %d: %d\n",
                    arvif->vdev_id, ret);
        goto err_peer_delete;
    }
#endif // NEEDS PORTING

    arvif->txpower = 30; // TODO -- look up from channel information
    ret = ath10k_mac_txpower_recalc(ar);
    if (ret) {
        ath10k_warn("failed to recalc tx power: %d\n", ret);
        goto err_peer_delete;
    }

#if 0 // NEEDS PORTING
    if (vif->type == NL80211_IFTYPE_MONITOR) {
        ar->monitor_arvif = arvif;
        ret = ath10k_monitor_recalc(ar);
        if (ret) {
            ath10k_warn("failed to recalc monitor: %d\n", ret);
            goto err_peer_delete;
        }
    }

    mtx_lock(&ar->htt.tx_lock);
    if (!ar->tx_paused) {
        ieee80211_wake_queue(ar->hw, arvif->vdev_id);
    }
    mtx_unlock(&ar->htt.tx_lock);
#endif // NEEDS PORTING

    return ZX_OK;

err_peer_delete:
#if 0 // NEEDS PORTING
    if (arvif->vdev_type == WMI_VDEV_TYPE_AP ||
            arvif->vdev_type == WMI_VDEV_TYPE_IBSS) {
        ath10k_wmi_peer_delete(ar, arvif->vdev_id, vif->addr);
    }
#endif // NEEDS PORTING

err_vdev_delete:
    ath10k_wmi_vdev_delete(ar, arvif->vdev_id);
    ar->free_vdev_map |= 1LL << arvif->vdev_id;
#if 0 // NEEDS PORTING
    mtx_lock(&ar->data_lock);
    list_del(&arvif->list);
    mtx_unlock(&ar->data_lock);
#endif // NEEDS PORTING

err:
#if 0 // NEEDS PORTING
    if (arvif->beacon_buf) {
        dma_free_coherent(ar->dev, IEEE80211_MAX_FRAME_LEN,
                          arvif->beacon_buf, arvif->beacon_paddr);
        arvif->beacon_buf = NULL;
    }
#endif // NEEDS PORTING

    return ret;
}

#if 0 // NEEDS PORTING
static void ath10k_mac_vif_tx_unlock_all(struct ath10k_vif* arvif) {
    int i;

    for (i = 0; i < BITS_PER_LONG; i++) {
        ath10k_mac_vif_tx_unlock(arvif, i);
    }
}

static void ath10k_remove_interface(struct ieee80211_hw* hw,
                                    struct ieee80211_vif* vif) {
    struct ath10k* ar = hw->priv;
    struct ath10k_vif* arvif = (void*)vif->drv_priv;
    struct ath10k_peer* peer;
    int ret;
    int i;

    cancel_work_sync(&arvif->ap_csa_work);
    cancel_delayed_work_sync(&arvif->connection_loss_work);

    mtx_lock(&ar->conf_mutex);

    mtx_lock(&ar->data_lock);
    ath10k_mac_vif_beacon_cleanup(arvif);
    mtx_unlock(&ar->data_lock);

    ret = ath10k_spectral_vif_stop(arvif);
    if (ret)
        ath10k_warn("failed to stop spectral for vdev %i: %d\n",
                    arvif->vdev_id, ret);

    ar->free_vdev_map |= 1LL << arvif->vdev_id;
    mtx_lock(&ar->data_lock);
    list_del(&arvif->list);
    mtx_unlock(&ar->data_lock);

    if (arvif->vdev_type == WMI_VDEV_TYPE_AP ||
            arvif->vdev_type == WMI_VDEV_TYPE_IBSS) {
        ret = ath10k_wmi_peer_delete(arvif->ar, arvif->vdev_id,
                                     vif->addr);
        if (ret)
            ath10k_warn("failed to submit AP/IBSS self-peer removal on vdev %i: %d\n",
                        arvif->vdev_id, ret);

        kfree(arvif->u.ap.noa_data);
    }

    ath10k_dbg(ar, ATH10K_DBG_MAC, "mac vdev %i delete (remove interface)\n",
               arvif->vdev_id);

    ret = ath10k_wmi_vdev_delete(ar, arvif->vdev_id);
    if (ret)
        ath10k_warn("failed to delete WMI vdev %i: %d\n",
                    arvif->vdev_id, ret);

    /* Some firmware revisions don't notify host about self-peer removal
     * until after associated vdev is deleted.
     */
    if (arvif->vdev_type == WMI_VDEV_TYPE_AP ||
            arvif->vdev_type == WMI_VDEV_TYPE_IBSS) {
        ret = ath10k_wait_for_peer_deleted(ar, arvif->vdev_id,
                                           vif->addr);
        if (ret)
            ath10k_warn("failed to remove AP self-peer on vdev %i: %d\n",
                        arvif->vdev_id, ret);

        mtx_lock(&ar->data_lock);
        ar->num_peers--;
        mtx_unlock(&ar->data_lock);
    }

    mtx_lock(&ar->data_lock);
    for (i = 0; i < countof(ar->peer_map); i++) {
        peer = ar->peer_map[i];
        if (!peer) {
            continue;
        }

        if (peer->vif == vif) {
            ath10k_warn("found vif peer %pM entry on vdev %i after it was supposedly removed\n",
                        vif->addr, arvif->vdev_id);
            peer->vif = NULL;
        }
    }
    mtx_unlock(&ar->data_lock);

    ath10k_peer_cleanup(ar, arvif->vdev_id);
    ath10k_mac_txq_unref(ar, vif->txq);

    if (vif->type == NL80211_IFTYPE_MONITOR) {
        ar->monitor_arvif = NULL;
        ret = ath10k_monitor_recalc(ar);
        if (ret) {
            ath10k_warn("failed to recalc monitor: %d\n", ret);
        }
    }

    ret = ath10k_mac_txpower_recalc(ar);
    if (ret) {
        ath10k_warn("failed to recalc tx power: %d\n", ret);
    }

    mtx_lock(&ar->htt.tx_lock);
    ath10k_mac_vif_tx_unlock_all(arvif);
    mtx_unlock(&ar->htt.tx_lock);

    ath10k_mac_txq_unref(ar, vif->txq);

    mtx_unlock(&ar->conf_mutex);
}

/*
 * FIXME: Has to be verified.
 */
#define SUPPORTED_FILTERS           \
    (FIF_ALLMULTI |             \
    FIF_CONTROL |               \
    FIF_PSPOLL |                \
    FIF_OTHER_BSS |             \
    FIF_BCN_PRBRESP_PROMISC |       \
    FIF_PROBE_REQ |             \
    FIF_FCSFAIL)

static void ath10k_configure_filter(struct ieee80211_hw* hw,
                                    unsigned int changed_flags,
                                    unsigned int* total_flags,
                                    uint64_t multicast) {
    struct ath10k* ar = hw->priv;
    int ret;

    mtx_lock(&ar->conf_mutex);

    changed_flags &= SUPPORTED_FILTERS;
    *total_flags &= SUPPORTED_FILTERS;
    ar->filter_flags = *total_flags;

    ret = ath10k_monitor_recalc(ar);
    if (ret) {
        ath10k_warn("failed to recalc monitor: %d\n", ret);
    }

    mtx_unlock(&ar->conf_mutex);
}

static void ath10k_bss_info_changed(struct ieee80211_hw* hw,
                                    struct ieee80211_vif* vif,
                                    struct ieee80211_bss_conf* info,
                                    uint32_t changed) {
    struct ath10k* ar = hw->priv;
    struct ath10k_vif* arvif = (void*)vif->drv_priv;
    int ret = 0;
    uint32_t vdev_param, pdev_param, slottime, preamble;

    mtx_lock(&ar->conf_mutex);

    if (changed & BSS_CHANGED_IBSS) {
        ath10k_control_ibss(arvif, info, vif->addr);
    }

    if (changed & BSS_CHANGED_BEACON_INT) {
        arvif->beacon_interval = info->beacon_int;
        vdev_param = ar->wmi.vdev_param->beacon_interval;
        ret = ath10k_wmi_vdev_set_param(ar, arvif->vdev_id, vdev_param,
                                        arvif->beacon_interval);
        ath10k_dbg(ar, ATH10K_DBG_MAC,
                   "mac vdev %d beacon_interval %d\n",
                   arvif->vdev_id, arvif->beacon_interval);

        if (ret)
            ath10k_warn("failed to set beacon interval for vdev %d: %i\n",
                        arvif->vdev_id, ret);
    }

    if (changed & BSS_CHANGED_BEACON) {
        ath10k_dbg(ar, ATH10K_DBG_MAC,
                   "vdev %d set beacon tx mode to staggered\n",
                   arvif->vdev_id);

        pdev_param = ar->wmi.pdev_param->beacon_tx_mode;
        ret = ath10k_wmi_pdev_set_param(ar, pdev_param,
                                        WMI_BEACON_STAGGERED_MODE);
        if (ret)
            ath10k_warn("failed to set beacon mode for vdev %d: %i\n",
                        arvif->vdev_id, ret);

        ret = ath10k_mac_setup_bcn_tmpl(arvif);
        if (ret)
            ath10k_warn("failed to update beacon template: %d\n",
                        ret);

        if (ieee80211_vif_is_mesh(vif)) {
            /* mesh doesn't use SSID but firmware needs it */
            strncpy(arvif->u.ap.ssid, "mesh",
                    sizeof(arvif->u.ap.ssid));
            arvif->u.ap.ssid_len = 4;
        }
    }

    if (changed & BSS_CHANGED_AP_PROBE_RESP) {
        ret = ath10k_mac_setup_prb_tmpl(arvif);
        if (ret)
            ath10k_warn("failed to setup probe resp template on vdev %i: %d\n",
                        arvif->vdev_id, ret);
    }

    if (changed & (BSS_CHANGED_BEACON_INFO | BSS_CHANGED_BEACON)) {
        arvif->dtim_period = info->dtim_period;

        ath10k_dbg(ar, ATH10K_DBG_MAC,
                   "mac vdev %d dtim_period %d\n",
                   arvif->vdev_id, arvif->dtim_period);

        vdev_param = ar->wmi.vdev_param->dtim_period;
        ret = ath10k_wmi_vdev_set_param(ar, arvif->vdev_id, vdev_param,
                                        arvif->dtim_period);
        if (ret)
            ath10k_warn("failed to set dtim period for vdev %d: %i\n",
                        arvif->vdev_id, ret);
    }

    if (changed & BSS_CHANGED_SSID &&
            vif->type == NL80211_IFTYPE_AP) {
        arvif->u.ap.ssid_len = info->ssid_len;
        if (info->ssid_len) {
            memcpy(arvif->u.ap.ssid, info->ssid, info->ssid_len);
        }
        arvif->u.ap.hidden_ssid = info->hidden_ssid;
    }

    if (changed & BSS_CHANGED_BSSID && !is_zero_ether_addr(info->bssid)) {
        memcpy(arvif->bssid, info->bssid, ETH_ALEN);
    }

    if (changed & BSS_CHANGED_BEACON_ENABLED) {
        ath10k_control_beaconing(arvif, info);
    }

    if (changed & BSS_CHANGED_ERP_CTS_PROT) {
        arvif->use_cts_prot = info->use_cts_prot;

        ret = ath10k_recalc_rtscts_prot(arvif);
        if (ret)
            ath10k_warn("failed to recalculate rts/cts prot for vdev %d: %d\n",
                        arvif->vdev_id, ret);

        if (ath10k_mac_can_set_cts_prot(arvif)) {
            ret = ath10k_mac_set_cts_prot(arvif);
            if (ret)
                ath10k_warn("failed to set cts protection for vdev %d: %d\n",
                            arvif->vdev_id, ret);
        }
    }

    if (changed & BSS_CHANGED_ERP_SLOT) {
        if (info->use_short_slot) {
            slottime = WMI_VDEV_SLOT_TIME_SHORT;    /* 9us */
        }

        else {
            slottime = WMI_VDEV_SLOT_TIME_LONG;    /* 20us */
        }

        ath10k_dbg(ar, ATH10K_DBG_MAC, "mac vdev %d slot_time %d\n",
                   arvif->vdev_id, slottime);

        vdev_param = ar->wmi.vdev_param->slot_time;
        ret = ath10k_wmi_vdev_set_param(ar, arvif->vdev_id, vdev_param,
                                        slottime);
        if (ret)
            ath10k_warn("failed to set erp slot for vdev %d: %i\n",
                        arvif->vdev_id, ret);
    }

    if (changed & BSS_CHANGED_ERP_PREAMBLE) {
        if (info->use_short_preamble) {
            preamble = WMI_VDEV_PREAMBLE_SHORT;
        } else {
            preamble = WMI_VDEV_PREAMBLE_LONG;
        }

        ath10k_dbg(ar, ATH10K_DBG_MAC,
                   "mac vdev %d preamble %dn",
                   arvif->vdev_id, preamble);

        vdev_param = ar->wmi.vdev_param->preamble;
        ret = ath10k_wmi_vdev_set_param(ar, arvif->vdev_id, vdev_param,
                                        preamble);
        if (ret)
            ath10k_warn("failed to set preamble for vdev %d: %i\n",
                        arvif->vdev_id, ret);
    }

    if (changed & BSS_CHANGED_ASSOC) {
        if (info->assoc) {
            /* Workaround: Make sure monitor vdev is not running
             * when associating to prevent some firmware revisions
             * (e.g. 10.1 and 10.2) from crashing.
             */
            if (ar->monitor_started) {
                ath10k_monitor_stop(ar);
            }
            ath10k_bss_assoc(hw, vif, info);
            ath10k_monitor_recalc(ar);
        } else {
            ath10k_bss_disassoc(hw, vif);
        }
    }

    if (changed & BSS_CHANGED_TXPOWER) {
        ath10k_dbg(ar, ATH10K_DBG_MAC, "mac vdev_id %i txpower %d\n",
                   arvif->vdev_id, info->txpower);

        arvif->txpower = info->txpower;
        ret = ath10k_mac_txpower_recalc(ar);
        if (ret) {
            ath10k_warn("failed to recalc tx power: %d\n", ret);
        }
    }

    if (changed & BSS_CHANGED_PS) {
        arvif->ps = vif->bss_conf.ps;

        ret = ath10k_config_ps(ar);
        if (ret)
            ath10k_warn("failed to setup ps on vdev %i: %d\n",
                        arvif->vdev_id, ret);
    }

    mtx_unlock(&ar->conf_mutex);
}

static void ath10k_mac_op_set_coverage_class(struct ieee80211_hw* hw, int16_t value) {
    struct ath10k* ar = hw->priv;

    /* This function should never be called if setting the coverage class
     * is not supported on this hardware.
     */
    if (!ar->hw_params.hw_ops->set_coverage_class) {
        WARN_ONCE();
        return;
    }
    ar->hw_params.hw_ops->set_coverage_class(ar, value);
}

static int ath10k_hw_scan(struct ieee80211_hw* hw,
                          struct ieee80211_vif* vif,
                          struct ieee80211_scan_request* hw_req) {
    struct ath10k* ar = hw->priv;
    struct ath10k_vif* arvif = (void*)vif->drv_priv;
    struct cfg80211_scan_request* req = &hw_req->req;
    struct wmi_start_scan_arg arg;
    int ret = 0;
    int i;

    mtx_lock(&ar->conf_mutex);

    mtx_lock(&ar->data_lock);
    switch (ar->scan.state) {
    case ATH10K_SCAN_IDLE:
        sync_completion_reset(&ar->scan.started);
        sync_completion_reset(&ar->scan.completed);
        ar->scan.state = ATH10K_SCAN_STARTING;
        ar->scan.is_roc = false;
        ar->scan.vdev_id = arvif->vdev_id;
        ret = 0;
        break;
    case ATH10K_SCAN_STARTING:
    case ATH10K_SCAN_RUNNING:
    case ATH10K_SCAN_ABORTING:
        ret = -EBUSY;
        break;
    }
    mtx_unlock(&ar->data_lock);

    if (ret) {
        goto exit;
    }

    memset(&arg, 0, sizeof(arg));
    ath10k_wmi_start_scan_init(ar, &arg);
    arg.vdev_id = arvif->vdev_id;
    arg.scan_id = ATH10K_SCAN_ID;

    if (req->ie_len) {
        arg.ie_len = req->ie_len;
        memcpy(arg.ie, req->ie, arg.ie_len);
    }

    if (req->n_ssids) {
        arg.n_ssids = req->n_ssids;
        for (i = 0; i < arg.n_ssids; i++) {
            arg.ssids[i].len  = req->ssids[i].ssid_len;
            arg.ssids[i].ssid = req->ssids[i].ssid;
        }
    } else {
        arg.scan_ctrl_flags |= WMI_SCAN_FLAG_PASSIVE;
    }

    if (req->n_channels) {
        arg.n_channels = req->n_channels;
        for (i = 0; i < arg.n_channels; i++) {
            arg.channels[i] = req->channels[i]->center_freq;
        }
    }

    ret = ath10k_start_scan(ar, &arg);
    if (ret) {
        ath10k_warn("failed to start hw scan: %d\n", ret);
        mtx_lock(&ar->data_lock);
        ar->scan.state = ATH10K_SCAN_IDLE;
        mtx_unlock(&ar->data_lock);
    }

    /* Add a 200ms margin to account for event/command processing */
    ieee80211_queue_delayed_work(ar->hw, &ar->scan.timeout,
                                 msecs_to_jiffies(arg.max_scan_time +
                                         200));

exit:
    mtx_unlock(&ar->conf_mutex);
    return ret;
}

static void ath10k_cancel_hw_scan(struct ieee80211_hw* hw,
                                  struct ieee80211_vif* vif) {
    struct ath10k* ar = hw->priv;

    mtx_lock(&ar->conf_mutex);
    ath10k_scan_abort(ar);
    mtx_unlock(&ar->conf_mutex);

    cancel_delayed_work_sync(&ar->scan.timeout);
}
#endif // NEEDS PORTING

static void ath10k_set_key_h_def_keyidx(struct ath10k* ar,
                                        wlan_key_config_t* key_config) {
    struct ath10k_vif* arvif = &ar->arvif;
    uint32_t vdev_param = arvif->ar->wmi.vdev_param->def_keyid;
    zx_status_t status;

    /* 10.1 firmware branch requires default key index to be set to group
     * key index after installing it. Otherwise FW/HW Txes corrupted
     * frames with multi-vif APs. This is not required for main firmware
     * branch (e.g. 636).
     *
     * This is also needed for 636 fw for IBSS-RSN to work more reliably.
     *
     * FIXME: It remains unknown if this is required for multi-vif STA
     * interfaces on 10.1.
     */

    if (arvif->vdev_type != WMI_VDEV_TYPE_AP &&
            arvif->vdev_type != WMI_VDEV_TYPE_IBSS) {
        return;
    }

    if (key_config->cipher_type == IEEE80211_CIPHER_SUITE_WEP_40) {
        return;
    }

    if (key_config->cipher_type == IEEE80211_CIPHER_SUITE_WEP_104) {
        return;
    }

    if (key_config->key_type == WLAN_KEY_TYPE_PAIRWISE) {
        return;
    }

    status = ath10k_wmi_vdev_set_param(ar, arvif->vdev_id, vdev_param, key_config->key_idx);
    if (status != ZX_OK) {
        ath10k_warn("failed to set vdev %i group key as default key: %s\n",
                    arvif->vdev_id, zx_status_get_string(status));
    } else {
        ath10k_info("set vdev %i group key as default key\n", arvif->vdev_id);
    }
}

zx_status_t ath10k_mac_set_key(struct ath10k* ar, wlan_key_config_t* key_config) {
    struct ath10k_vif* arvif = &ar->arvif;
    const uint8_t* peer_addr;
    zx_status_t ret = ZX_OK;
    uint32_t flags = 0;

    if (arvif->nohwcrypt) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    if (key_config->key_idx > WMI_MAX_KEY_INDEX) {
        return ZX_ERR_INVALID_ARGS;
    }

    // TODO: We should retrieve this value from key_config, but it is currently unavailable.
    peer_addr = arvif->bssid;

    mtx_lock(&ar->conf_mutex);

#if 0 // NEEDS PORTING
    struct ath10k_peer* peer;

    /* the peer should not disappear in mid-way (unless FW goes awry) since
     * we already hold conf_mutex. we just make sure its there now.
     */
    mtx_lock(&ar->data_lock);
    peer = ath10k_peer_find(ar, arvif->vdev_id, peer_addr);
    mtx_unlock(&ar->data_lock);

    if (!peer) {
        ath10k_warn("failed to install key for non-existent peer %pM\n", peer_addr);
        ret = ZX_ERR_NOT_FOUND;
        goto exit;
    }
#endif // NEEDS PORTING

    switch(key_config->key_type) {
    case WLAN_KEY_TYPE_PAIRWISE:
        flags |= WMI_KEY_PAIRWISE;
        break;
    case WLAN_KEY_TYPE_GROUP:
        flags |= WMI_KEY_GROUP;
        break;
    case WLAN_KEY_TYPE_IGTK:
    case WLAN_KEY_TYPE_PEER:
    default:
        ZX_ASSERT(0);
    }

    ret = ath10k_install_key(arvif, key_config, peer_addr, flags);
    if (ret != ZX_OK) {
        ath10k_warn("failed to install key for vdev %i peer %pM: %d\n",
                    arvif->vdev_id, peer_addr, ret);
        goto exit;
    }

    ath10k_set_key_h_def_keyidx(ar, key_config);

#if 0 // NEEDS PORTING
    mtx_lock(&ar->data_lock);
    peer = ath10k_peer_find(ar, arvif->vdev_id, peer_addr);
    if (peer) {
        peer->keys[key->keyidx] = key;
    } else  {
        /* impossible unless FW goes crazy */
        ath10k_warn("Peer %pM disappeared!\n", peer_addr);
    }
    mtx_unlock(&ar->data_lock);
#endif // NEEDS PORTING

exit:
    mtx_unlock(&ar->conf_mutex);
    return ret;
}

#if 0 // NEEDS PORTING
static void ath10k_set_default_unicast_key(struct ieee80211_hw* hw,
        struct ieee80211_vif* vif,
        int keyidx) {
    struct ath10k* ar = hw->priv;
    struct ath10k_vif* arvif = (void*)vif->drv_priv;
    int ret;

    mtx_lock(&arvif->ar->conf_mutex);

    if (arvif->ar->state != ATH10K_STATE_ON) {
        goto unlock;
    }

    ath10k_dbg(ar, ATH10K_DBG_MAC, "mac vdev %d set keyidx %d\n",
               arvif->vdev_id, keyidx);

    ret = ath10k_wmi_vdev_set_param(arvif->ar,
                                    arvif->vdev_id,
                                    arvif->ar->wmi.vdev_param->def_keyid,
                                    keyidx);

    if (ret) {
        ath10k_warn("failed to update wep key index for vdev %d: %d\n",
                    arvif->vdev_id,
                    ret);
        goto unlock;
    }

    arvif->def_wep_key_idx = keyidx;

unlock:
    mtx_unlock(&arvif->ar->conf_mutex);
}

static void ath10k_sta_rc_update_wk(struct work_struct* wk) {
    struct ath10k* ar;
    struct ath10k_vif* arvif;
    struct ath10k_sta* arsta;
    struct ieee80211_sta* sta;
    struct cfg80211_chan_def def;
    enum nl80211_band band;
    const uint8_t* ht_mcs_mask;
    const uint16_t* vht_mcs_mask;
    uint32_t changed, bw, nss, smps;
    int err;

    arsta = container_of(wk, struct ath10k_sta, update_wk);
    sta = container_of((void*)arsta, struct ieee80211_sta, drv_priv);
    arvif = arsta->arvif;
    ar = arvif->ar;

    if (COND_WARN(ath10k_mac_vif_chan(arvif->vif, &def))) {
        return;
    }

    band = def.chan->band;
    ht_mcs_mask = arvif->bitrate_mask.control[band].ht_mcs;
    vht_mcs_mask = arvif->bitrate_mask.control[band].vht_mcs;

    mtx_lock(&ar->data_lock);

    changed = arsta->changed;
    arsta->changed = 0;

    bw = arsta->bw;
    nss = arsta->nss;
    smps = arsta->smps;

    mtx_unlock(&ar->data_lock);

    mtx_lock(&ar->conf_mutex);

    nss = max_t(uint32_t, 1, nss);
    nss = MIN(nss, max(ath10k_mac_max_ht_nss(ht_mcs_mask),
                       ath10k_mac_max_vht_nss(vht_mcs_mask)));

    if (changed & IEEE80211_RC_BW_CHANGED) {
        ath10k_dbg(ar, ATH10K_DBG_MAC, "mac update sta %pM peer bw %d\n",
                   sta->addr, bw);

        err = ath10k_wmi_peer_set_param(ar, arvif->vdev_id, sta->addr,
                                        WMI_PEER_CHAN_WIDTH, bw);
        if (err)
            ath10k_warn("failed to update STA %pM peer bw %d: %d\n",
                        sta->addr, bw, err);
    }

    if (changed & IEEE80211_RC_NSS_CHANGED) {
        ath10k_dbg(ar, ATH10K_DBG_MAC, "mac update sta %pM nss %d\n",
                   sta->addr, nss);

        err = ath10k_wmi_peer_set_param(ar, arvif->vdev_id, sta->addr,
                                        WMI_PEER_NSS, nss);
        if (err)
            ath10k_warn("failed to update STA %pM nss %d: %d\n",
                        sta->addr, nss, err);
    }

    if (changed & IEEE80211_RC_SMPS_CHANGED) {
        ath10k_dbg(ar, ATH10K_DBG_MAC, "mac update sta %pM smps %d\n",
                   sta->addr, smps);

        err = ath10k_wmi_peer_set_param(ar, arvif->vdev_id, sta->addr,
                                        WMI_PEER_SMPS_STATE, smps);
        if (err)
            ath10k_warn("failed to update STA %pM smps %d: %d\n",
                        sta->addr, smps, err);
    }

    if (changed & IEEE80211_RC_SUPP_RATES_CHANGED ||
            changed & IEEE80211_RC_NSS_CHANGED) {
        ath10k_dbg(ar, ATH10K_DBG_MAC, "mac update sta %pM supp rates/nss\n",
                   sta->addr);

        err = ath10k_station_assoc(ar, arvif->vif, sta, true);
        if (err)
            ath10k_warn("failed to reassociate station: %pM\n",
                        sta->addr);
    }

    mtx_unlock(&ar->conf_mutex);
}

static int ath10k_mac_inc_num_stations(struct ath10k_vif* arvif,
                                       struct ieee80211_sta* sta) {
    struct ath10k* ar = arvif->ar;

    ASSERT_MTX_HELD(&ar->conf_mutex);

    if (arvif->vdev_type == WMI_VDEV_TYPE_STA && !sta->tdls) {
        return 0;
    }

    if (ar->num_stations >= ar->max_num_stations) {
        return -ENOBUFS;
    }

    ar->num_stations++;

    return 0;
}

static void ath10k_mac_dec_num_stations(struct ath10k_vif* arvif,
                                        struct ieee80211_sta* sta) {
    struct ath10k* ar = arvif->ar;

    ASSERT_MTX_HELD(&ar->conf_mutex);

    if (arvif->vdev_type == WMI_VDEV_TYPE_STA && !sta->tdls) {
        return;
    }

    ar->num_stations--;
}

struct ath10k_mac_tdls_iter_data {
    uint32_t num_tdls_stations;
    struct ieee80211_vif* curr_vif;
};

static void ath10k_mac_tdls_vif_stations_count_iter(void* data,
        struct ieee80211_sta* sta) {
    struct ath10k_mac_tdls_iter_data* iter_data = data;
    struct ath10k_sta* arsta = (struct ath10k_sta*)sta->drv_priv;
    struct ieee80211_vif* sta_vif = arsta->arvif->vif;

    if (sta->tdls && sta_vif == iter_data->curr_vif) {
        iter_data->num_tdls_stations++;
    }
}

static int ath10k_mac_tdls_vif_stations_count(struct ieee80211_hw* hw,
        struct ieee80211_vif* vif) {
    struct ath10k_mac_tdls_iter_data data = {};

    data.curr_vif = vif;

    ieee80211_iterate_stations_atomic(hw,
                                      ath10k_mac_tdls_vif_stations_count_iter,
                                      &data);
    return data.num_tdls_stations;
}

static void ath10k_mac_tdls_vifs_count_iter(void* data, uint8_t* mac,
        struct ieee80211_vif* vif) {
    struct ath10k_vif* arvif = (void*)vif->drv_priv;
    int* num_tdls_vifs = data;

    if (vif->type != NL80211_IFTYPE_STATION) {
        return;
    }

    if (ath10k_mac_tdls_vif_stations_count(arvif->ar->hw, vif) > 0) {
        (*num_tdls_vifs)++;
    }
}

static int ath10k_mac_tdls_vifs_count(struct ieee80211_hw* hw) {
    int num_tdls_vifs = 0;

    ieee80211_iterate_active_interfaces_atomic(hw,
            IEEE80211_IFACE_ITER_NORMAL,
            ath10k_mac_tdls_vifs_count_iter,
            &num_tdls_vifs);
    return num_tdls_vifs;
}

static int ath10k_sta_state(struct ieee80211_hw* hw,
                            struct ieee80211_vif* vif,
                            struct ieee80211_sta* sta,
                            enum ieee80211_sta_state old_state,
                            enum ieee80211_sta_state new_state) {
    struct ath10k* ar = hw->priv;
    struct ath10k_vif* arvif = (void*)vif->drv_priv;
    struct ath10k_sta* arsta = (struct ath10k_sta*)sta->drv_priv;
    struct ath10k_peer* peer;
    int ret = 0;
    int i;

    if (old_state == IEEE80211_STA_NOTEXIST &&
            new_state == IEEE80211_STA_NONE) {
        memset(arsta, 0, sizeof(*arsta));
        arsta->arvif = arvif;
        INIT_WORK(&arsta->update_wk, ath10k_sta_rc_update_wk);

        for (i = 0; i < countof(sta->txq); i++) {
            ath10k_mac_txq_init(sta->txq[i]);
        }
    }

    /* cancel must be done outside the mutex to avoid deadlock */
    if ((old_state == IEEE80211_STA_NONE &&
            new_state == IEEE80211_STA_NOTEXIST)) {
        cancel_work_sync(&arsta->update_wk);
    }

    mtx_lock(&ar->conf_mutex);

    if (old_state == IEEE80211_STA_NOTEXIST &&
            new_state == IEEE80211_STA_NONE) {
        /*
         * New station addition.
         */
        enum wmi_peer_type peer_type = WMI_PEER_TYPE_DEFAULT;
        uint32_t num_tdls_stations;
        uint32_t num_tdls_vifs;

        ath10k_dbg(ar, ATH10K_DBG_MAC,
                   "mac vdev %d peer create %pM (new sta) sta %d / %d peer %d / %d\n",
                   arvif->vdev_id, sta->addr,
                   ar->num_stations + 1, ar->max_num_stations,
                   ar->num_peers + 1, ar->max_num_peers);

        num_tdls_stations = ath10k_mac_tdls_vif_stations_count(hw, vif);
        num_tdls_vifs = ath10k_mac_tdls_vifs_count(hw);

        if (sta->tdls) {
            if (num_tdls_stations >= ar->max_num_tdls_vdevs) {
                ath10k_warn("vdev %i exceeded maximum number of tdls vdevs %i\n",
                            arvif->vdev_id,
                            ar->max_num_tdls_vdevs);
                ret = -ELNRNG;
                goto exit;
            }
            peer_type = WMI_PEER_TYPE_TDLS;
        }

        ret = ath10k_mac_inc_num_stations(arvif, sta);
        if (ret) {
            ath10k_warn("refusing to associate station: too many connected already (%d)\n",
                        ar->max_num_stations);
            goto exit;
        }

        ret = ath10k_peer_create(ar, vif, sta, arvif->vdev_id,
                                 sta->addr, peer_type);
        if (ret) {
            ath10k_warn("failed to add peer %pM for vdev %d when adding a new sta: %i\n",
                        sta->addr, arvif->vdev_id, ret);
            ath10k_mac_dec_num_stations(arvif, sta);
            goto exit;
        }

        mtx_lock(&ar->data_lock);

        peer = ath10k_peer_find(ar, arvif->vdev_id, sta->addr);
        if (!peer) {
            ath10k_warn("failed to lookup peer %pM on vdev %i\n",
                        vif->addr, arvif->vdev_id);
            mtx_unlock(&ar->data_lock);
            ath10k_peer_delete(ar, arvif->vdev_id, sta->addr);
            ath10k_mac_dec_num_stations(arvif, sta);
            ret = -ENOENT;
            goto exit;
        }

        arsta->peer_id = find_first_bit(peer->peer_ids,
                                        ATH10K_MAX_NUM_PEER_IDS);

        mtx_unlock(&ar->data_lock);

        if (!sta->tdls) {
            goto exit;
        }

        ret = ath10k_wmi_update_fw_tdls_state(ar, arvif->vdev_id,
                                              WMI_TDLS_ENABLE_ACTIVE);
        if (ret) {
            ath10k_warn("failed to update fw tdls state on vdev %i: %i\n",
                        arvif->vdev_id, ret);
            ath10k_peer_delete(ar, arvif->vdev_id,
                               sta->addr);
            ath10k_mac_dec_num_stations(arvif, sta);
            goto exit;
        }

        ret = ath10k_mac_tdls_peer_update(ar, arvif->vdev_id, sta,
                                          WMI_TDLS_PEER_STATE_PEERING);
        if (ret) {
            ath10k_warn("failed to update tdls peer %pM for vdev %d when adding a new sta: %i\n",
                        sta->addr, arvif->vdev_id, ret);
            ath10k_peer_delete(ar, arvif->vdev_id, sta->addr);
            ath10k_mac_dec_num_stations(arvif, sta);

            if (num_tdls_stations != 0) {
                goto exit;
            }
            ath10k_wmi_update_fw_tdls_state(ar, arvif->vdev_id,
                                            WMI_TDLS_DISABLE);
        }
    } else if ((old_state == IEEE80211_STA_NONE &&
                new_state == IEEE80211_STA_NOTEXIST)) {
        /*
         * Existing station deletion.
         */
        ath10k_dbg(ar, ATH10K_DBG_MAC,
                   "mac vdev %d peer delete %pM sta %pK (sta gone)\n",
                   arvif->vdev_id, sta->addr, sta);

        ret = ath10k_peer_delete(ar, arvif->vdev_id, sta->addr);
        if (ret)
            ath10k_warn("failed to delete peer %pM for vdev %d: %i\n",
                        sta->addr, arvif->vdev_id, ret);

        ath10k_mac_dec_num_stations(arvif, sta);

        mtx_lock(&ar->data_lock);
        for (i = 0; i < countof(ar->peer_map); i++) {
            peer = ar->peer_map[i];
            if (!peer) {
                continue;
            }

            if (peer->sta == sta) {
                ath10k_warn("found sta peer %pM (ptr %pK id %d) entry on vdev %i after it was supposedly removed\n",
                            sta->addr, peer, i, arvif->vdev_id);
                peer->sta = NULL;

                /* Clean up the peer object as well since we
                 * must have failed to do this above.
                 */
                list_del(&peer->list);
                ar->peer_map[i] = NULL;
                kfree(peer);
                ar->num_peers--;
            }
        }
        mtx_unlock(&ar->data_lock);

        for (i = 0; i < countof(sta->txq); i++) {
            ath10k_mac_txq_unref(ar, sta->txq[i]);
        }

        if (!sta->tdls) {
            goto exit;
        }

        if (ath10k_mac_tdls_vif_stations_count(hw, vif)) {
            goto exit;
        }

        /* This was the last tdls peer in current vif */
        ret = ath10k_wmi_update_fw_tdls_state(ar, arvif->vdev_id,
                                              WMI_TDLS_DISABLE);
        if (ret) {
            ath10k_warn("failed to update fw tdls state on vdev %i: %i\n",
                        arvif->vdev_id, ret);
        }
    } else if (old_state == IEEE80211_STA_AUTH &&
               new_state == IEEE80211_STA_ASSOC &&
               (vif->type == NL80211_IFTYPE_AP ||
                vif->type == NL80211_IFTYPE_MESH_POINT ||
                vif->type == NL80211_IFTYPE_ADHOC)) {
        /*
         * New association.
         */
        ath10k_dbg(ar, ATH10K_DBG_MAC, "mac sta %pM associated\n",
                   sta->addr);

        ret = ath10k_station_assoc(ar, vif, sta, false);
        if (ret)
            ath10k_warn("failed to associate station %pM for vdev %i: %i\n",
                        sta->addr, arvif->vdev_id, ret);
    } else if (old_state == IEEE80211_STA_ASSOC &&
               new_state == IEEE80211_STA_AUTHORIZED &&
               sta->tdls) {
        /*
         * Tdls station authorized.
         */
        ath10k_dbg(ar, ATH10K_DBG_MAC, "mac tdls sta %pM authorized\n",
                   sta->addr);

        ret = ath10k_station_assoc(ar, vif, sta, false);
        if (ret) {
            ath10k_warn("failed to associate tdls station %pM for vdev %i: %i\n",
                        sta->addr, arvif->vdev_id, ret);
            goto exit;
        }

        ret = ath10k_mac_tdls_peer_update(ar, arvif->vdev_id, sta,
                                          WMI_TDLS_PEER_STATE_CONNECTED);
        if (ret)
            ath10k_warn("failed to update tdls peer %pM for vdev %i: %i\n",
                        sta->addr, arvif->vdev_id, ret);
    } else if (old_state == IEEE80211_STA_ASSOC &&
               new_state == IEEE80211_STA_AUTH &&
               (vif->type == NL80211_IFTYPE_AP ||
                vif->type == NL80211_IFTYPE_MESH_POINT ||
                vif->type == NL80211_IFTYPE_ADHOC)) {
        /*
         * Disassociation.
         */
        ath10k_dbg(ar, ATH10K_DBG_MAC, "mac sta %pM disassociated\n",
                   sta->addr);

        ret = ath10k_station_disassoc(ar, vif, sta);
        if (ret)
            ath10k_warn("failed to disassociate station: %pM vdev %i: %i\n",
                        sta->addr, arvif->vdev_id, ret);
    }
exit:
    mtx_unlock(&ar->conf_mutex);
    return ret;
}

static int ath10k_conf_tx_uapsd(struct ath10k* ar, struct ieee80211_vif* vif,
                                uint16_t ac, bool enable) {
    struct ath10k_vif* arvif = (void*)vif->drv_priv;
    struct wmi_sta_uapsd_auto_trig_arg arg = {};
    uint32_t prio = 0, acc = 0;
    uint32_t value = 0;
    int ret = 0;

    ASSERT_MTX_HELD(&ar->conf_mutex);

    if (arvif->vdev_type != WMI_VDEV_TYPE_STA) {
        return 0;
    }

    switch (ac) {
    case IEEE80211_AC_VO:
        value = WMI_STA_PS_UAPSD_AC3_DELIVERY_EN |
                WMI_STA_PS_UAPSD_AC3_TRIGGER_EN;
        prio = 7;
        acc = 3;
        break;
    case IEEE80211_AC_VI:
        value = WMI_STA_PS_UAPSD_AC2_DELIVERY_EN |
                WMI_STA_PS_UAPSD_AC2_TRIGGER_EN;
        prio = 5;
        acc = 2;
        break;
    case IEEE80211_AC_BE:
        value = WMI_STA_PS_UAPSD_AC1_DELIVERY_EN |
                WMI_STA_PS_UAPSD_AC1_TRIGGER_EN;
        prio = 2;
        acc = 1;
        break;
    case IEEE80211_AC_BK:
        value = WMI_STA_PS_UAPSD_AC0_DELIVERY_EN |
                WMI_STA_PS_UAPSD_AC0_TRIGGER_EN;
        prio = 0;
        acc = 0;
        break;
    }

    if (enable) {
        arvif->u.sta.uapsd |= value;
    } else {
        arvif->u.sta.uapsd &= ~value;
    }

    ret = ath10k_wmi_set_sta_ps_param(ar, arvif->vdev_id,
                                      WMI_STA_PS_PARAM_UAPSD,
                                      arvif->u.sta.uapsd);
    if (ret) {
        ath10k_warn("failed to set uapsd params: %d\n", ret);
        goto exit;
    }

    if (arvif->u.sta.uapsd) {
        value = WMI_STA_PS_RX_WAKE_POLICY_POLL_UAPSD;
    } else {
        value = WMI_STA_PS_RX_WAKE_POLICY_WAKE;
    }

    ret = ath10k_wmi_set_sta_ps_param(ar, arvif->vdev_id,
                                      WMI_STA_PS_PARAM_RX_WAKE_POLICY,
                                      value);
    if (ret) {
        ath10k_warn("failed to set rx wake param: %d\n", ret);
    }

    ret = ath10k_mac_vif_recalc_ps_wake_threshold(arvif);
    if (ret) {
        ath10k_warn("failed to recalc ps wake threshold on vdev %i: %d\n",
                    arvif->vdev_id, ret);
        return ret;
    }

    ret = ath10k_mac_vif_recalc_ps_poll_count(arvif);
    if (ret) {
        ath10k_warn("failed to recalc ps poll count on vdev %i: %d\n",
                    arvif->vdev_id, ret);
        return ret;
    }

    if (BITARR_TEST(ar->wmi.svc_map, WMI_SERVICE_STA_UAPSD_BASIC_AUTO_TRIG) ||
            BITARR_TEST(ar->wmi.svc_map, WMI_SERVICE_STA_UAPSD_VAR_AUTO_TRIG)) {
        /* Only userspace can make an educated decision when to send
         * trigger frame. The following effectively disables u-UAPSD
         * autotrigger in firmware (which is enabled by default
         * provided the autotrigger service is available).
         */

        arg.wmm_ac = acc;
        arg.user_priority = prio;
        arg.service_interval = 0;
        arg.suspend_interval = WMI_STA_UAPSD_MAX_INTERVAL_MSEC;
        arg.delay_interval = WMI_STA_UAPSD_MAX_INTERVAL_MSEC;

        ret = ath10k_wmi_vdev_sta_uapsd(ar, arvif->vdev_id,
                                        arvif->bssid, &arg, 1);
        if (ret) {
            ath10k_warn("failed to set uapsd auto trigger %d\n",
                        ret);
            return ret;
        }
    }

exit:
    return ret;
}
#endif // NEEDS PORTING

static zx_status_t ath10k_conf_tx(struct ath10k* ar, uint16_t ac,
                                  struct wmi_wmm_params_arg* params) {
    struct ath10k_vif* arvif = &ar->arvif;
    struct wmi_wmm_params_arg* p = NULL;
    int ret;

    ASSERT_MTX_HELD(&ar->conf_mutex);

    switch (ac) {
    case IEEE80211_AC_VO:
        p = &arvif->wmm_params.ac_vo;
        break;
    case IEEE80211_AC_VI:
        p = &arvif->wmm_params.ac_vi;
        break;
    case IEEE80211_AC_BE:
        p = &arvif->wmm_params.ac_be;
        break;
    case IEEE80211_AC_BK:
        p = &arvif->wmm_params.ac_bk;
        break;
    default:
        ath10k_warn("internal err: ath10k_conf_tx called with an invalid AC value\n");
        return ZX_ERR_INVALID_ARGS;
    }

    memcpy(p, params, sizeof(*p));

    if (ar->wmi.ops->gen_vdev_wmm_conf) {
        ret = ath10k_wmi_vdev_wmm_conf(ar, arvif->vdev_id,
                                       &arvif->wmm_params);
        if (ret != ZX_OK) {
            ath10k_warn("failed to set vdev wmm params on vdev %i: %d\n",
                        arvif->vdev_id, ret);
            goto exit;
        }
    } else {
        /* This won't work well with multi-interface cases but it's
         * better than nothing.
         */
        ret = ath10k_wmi_pdev_set_wmm_params(ar, &arvif->wmm_params);
        if (ret != ZX_OK) {
            ath10k_warn("failed to set wmm params: %d\n", ret);
            goto exit;
        }
    }

#if 0 // NEEDS PORTING
    ret = ath10k_conf_tx_uapsd(ar, vif, ac, params->uapsd);
    if (ret) {
        ath10k_warn("failed to set sta uapsd: %d\n", ret);
    }
#endif // NEEDS PORTING

exit:
    return ret;
}

#if 0 // NEEDS PORTING
#define ATH10K_ROC_TIMEOUT_HZ (2 * HZ)

static int ath10k_remain_on_channel(struct ieee80211_hw* hw,
                                    struct ieee80211_vif* vif,
                                    struct ieee80211_channel* chan,
                                    int duration,
                                    enum ieee80211_roc_type type) {
    struct ath10k* ar = hw->priv;
    struct ath10k_vif* arvif = (void*)vif->drv_priv;
    struct wmi_start_scan_arg arg;
    int ret = 0;
    uint32_t scan_time_msec;

    mtx_lock(&ar->conf_mutex);

    mtx_lock(&ar->data_lock);
    switch (ar->scan.state) {
    case ATH10K_SCAN_IDLE:
        sync_completion_reset(&ar->scan.started);
        sync_completion_reset(&ar->scan.completed);
        sync_completion_reset(&ar->scan.on_channel);
        ar->scan.state = ATH10K_SCAN_STARTING;
        ar->scan.is_roc = true;
        ar->scan.vdev_id = arvif->vdev_id;
        ar->scan.roc_freq = chan->center_freq;
        ar->scan.roc_notify = true;
        ret = 0;
        break;
    case ATH10K_SCAN_STARTING:
    case ATH10K_SCAN_RUNNING:
    case ATH10K_SCAN_ABORTING:
        ret = -EBUSY;
        break;
    }
    mtx_unlock(&ar->data_lock);

    if (ret) {
        goto exit;
    }

    scan_time_msec = ar->hw->wiphy->max_remain_on_channel_duration * 2;

    memset(&arg, 0, sizeof(arg));
    ath10k_wmi_start_scan_init(ar, &arg);
    arg.vdev_id = arvif->vdev_id;
    arg.scan_id = ATH10K_SCAN_ID;
    arg.n_channels = 1;
    arg.channels[0] = chan->center_freq;
    arg.dwell_time_active = scan_time_msec;
    arg.dwell_time_passive = scan_time_msec;
    arg.max_scan_time = scan_time_msec;
    arg.scan_ctrl_flags |= WMI_SCAN_FLAG_PASSIVE;
    arg.scan_ctrl_flags |= WMI_SCAN_FILTER_PROBE_REQ;
    arg.burst_duration_ms = duration;

    ret = ath10k_start_scan(ar, &arg);
    if (ret) {
        ath10k_warn("failed to start roc scan: %d\n", ret);
        mtx_lock(&ar->data_lock);
        ar->scan.state = ATH10K_SCAN_IDLE;
        mtx_unlock(&ar->data_lock);
        goto exit;
    }

    if (sync_completion_wait(&ar->scan.on_channel, ZX_SEC(3)) == ZX_ERR_TIMED_OUT) {
        ath10k_warn("failed to switch to channel for roc scan\n");

        ret = ath10k_scan_stop(ar);
        if (ret) {
            ath10k_warn("failed to stop scan: %d\n", ret);
        }

        ret = -ETIMEDOUT;
        goto exit;
    }

    ieee80211_queue_delayed_work(ar->hw, &ar->scan.timeout,
                                 msecs_to_jiffies(duration));

    ret = 0;
exit:
    mtx_unlock(&ar->conf_mutex);
    return ret;
}

static int ath10k_cancel_remain_on_channel(struct ieee80211_hw* hw) {
    struct ath10k* ar = hw->priv;

    mtx_lock(&ar->conf_mutex);

    mtx_lock(&ar->data_lock);
    ar->scan.roc_notify = false;
    mtx_unlock(&ar->data_lock);

    ath10k_scan_abort(ar);

    mtx_unlock(&ar->conf_mutex);

    cancel_delayed_work_sync(&ar->scan.timeout);

    return 0;
}

/*
 * Both RTS and Fragmentation threshold are interface-specific
 * in ath10k, but device-specific in mac80211.
 */

static int ath10k_set_rts_threshold(struct ieee80211_hw* hw, uint32_t value) {
    struct ath10k* ar = hw->priv;
    struct ath10k_vif* arvif;
    int ret = 0;

    mtx_lock(&ar->conf_mutex);
    list_for_each_entry(arvif, &ar->arvifs, list) {
        ath10k_dbg(ar, ATH10K_DBG_MAC, "mac vdev %d rts threshold %d\n",
                   arvif->vdev_id, value);

        ret = ath10k_mac_set_rts(arvif, value);
        if (ret) {
            ath10k_warn("failed to set rts threshold for vdev %d: %d\n",
                        arvif->vdev_id, ret);
            break;
        }
    }
    mtx_unlock(&ar->conf_mutex);

    return ret;
}

static int ath10k_mac_op_set_frag_threshold(struct ieee80211_hw* hw, uint32_t value) {
    /* Even though there's a WMI enum for fragmentation threshold no known
     * firmware actually implements it. Moreover it is not possible to rely
     * frame fragmentation to mac80211 because firmware clears the "more
     * fragments" bit in frame control making it impossible for remote
     * devices to reassemble frames.
     *
     * Hence implement a dummy callback just to say fragmentation isn't
     * supported. This effectively prevents mac80211 from doing frame
     * fragmentation in software.
     */
    return -EOPNOTSUPP;
}

static void ath10k_flush(struct ieee80211_hw* hw, struct ieee80211_vif* vif,
                         uint32_t queues, bool drop) {
    struct ath10k* ar = hw->priv;
    bool skip;
    long time_left;

    /* mac80211 doesn't care if we really xmit queued frames or not
     * we'll collect those frames either way if we stop/delete vdevs
     */
    if (drop) {
        return;
    }

    mtx_lock(&ar->conf_mutex);

    if (ar->state == ATH10K_STATE_WEDGED) {
        goto skip;
    }

    time_left = wait_event_timeout(ar->htt.empty_tx_wq, ({
        bool empty;

        mtx_lock(&ar->htt.tx_lock);
        empty = (ar->htt.num_pending_tx == 0);
        mtx_unlock(&ar->htt.tx_lock);

        skip = (ar->state == ATH10K_STATE_WEDGED) ||
        BITARR_TEST(&ar->dev_flags, ATH10K_FLAG_CRASH_FLUSH);

        (empty || skip);
    }), ATH10K_FLUSH_TIMEOUT_HZ);

    if (time_left == 0 || skip)
        ath10k_warn("failed to flush transmit queue (skip %i ar-state %i): %ld\n",
                    skip, ar->state, time_left);

skip:
    mtx_unlock(&ar->conf_mutex);
}

/* TODO: Implement this function properly
 * For now it is needed to reply to Probe Requests in IBSS mode.
 * Propably we need this information from FW.
 */
static int ath10k_tx_last_beacon(struct ieee80211_hw* hw) {
    return 1;
}

static void ath10k_reconfig_complete(struct ieee80211_hw* hw,
                                     enum ieee80211_reconfig_type reconfig_type) {
    struct ath10k* ar = hw->priv;

    if (reconfig_type != IEEE80211_RECONFIG_TYPE_RESTART) {
        return;
    }

    mtx_lock(&ar->conf_mutex);

    /* If device failed to restart it will be in a different state, e.g.
     * ATH10K_STATE_WEDGED
     */
    if (ar->state == ATH10K_STATE_RESTARTED) {
        ath10k_trace("device successfully recovered\n");
        ar->state = ATH10K_STATE_ON;
        ieee80211_wake_queues(ar->hw);
    }

    mtx_unlock(&ar->conf_mutex);
}

static void
ath10k_mac_update_bss_chan_survey(struct ath10k* ar,
                                  struct ieee80211_channel* channel) {
    int ret;
    enum wmi_bss_survey_req_type type = WMI_BSS_SURVEY_REQ_TYPE_READ_CLEAR;

    ASSERT_MTX_HELD(&ar->conf_mutex);

    if (!BITARR_TEST(ar->wmi.svc_map, WMI_SERVICE_BSS_CHANNEL_INFO_64) ||
            (ar->rx_channel != channel)) {
        return;
    }

    if (ar->scan.state != ATH10K_SCAN_IDLE) {
        ath10k_dbg(ar, ATH10K_DBG_MAC, "ignoring bss chan info request while scanning..\n");
        return;
    }

    sync_completion_reset(&ar->bss_survey_done);

    ret = ath10k_wmi_pdev_bss_chan_info_request(ar, type);
    if (ret) {
        ath10k_warn("failed to send pdev bss chan info request\n");
        return;
    }

    if (sync_completion_wait(&ar->bss_survey_done, ZX_SEC(3)) == ZX_ERR_TIMED_OUT) {
        ath10k_warn("bss channel survey timed out\n");
        return;
    }
}

static int ath10k_get_survey(struct ieee80211_hw* hw, int idx,
                             struct survey_info* survey) {
    struct ath10k* ar = hw->priv;
    struct ieee80211_supported_band* sband;
    struct survey_info* ar_survey = &ar->survey[idx];
    int ret = 0;

    mtx_lock(&ar->conf_mutex);

    sband = hw->wiphy->bands[NL80211_BAND_2GHZ];
    if (sband && idx >= sband->n_channels) {
        idx -= sband->n_channels;
        sband = NULL;
    }

    if (!sband) {
        sband = hw->wiphy->bands[NL80211_BAND_5GHZ];
    }

    if (!sband || idx >= sband->n_channels) {
        ret = -ENOENT;
        goto exit;
    }

    ath10k_mac_update_bss_chan_survey(ar, &sband->channels[idx]);

    mtx_lock(&ar->data_lock);
    memcpy(survey, ar_survey, sizeof(*survey));
    mtx_unlock(&ar->data_lock);

    survey->channel = &sband->channels[idx];

    if (ar->rx_channel == survey->channel) {
        survey->filled |= SURVEY_INFO_IN_USE;
    }

exit:
    mtx_unlock(&ar->conf_mutex);
    return ret;
}

static bool
ath10k_mac_bitrate_mask_has_single_rate(struct ath10k* ar,
                                        enum nl80211_band band,
                                        const struct cfg80211_bitrate_mask* mask) {
    int num_rates = 0;
    int i;

    num_rates += hweight32(mask->control[band].legacy);

    for (i = 0; i < countof(mask->control[band].ht_mcs); i++) {
        num_rates += hweight8(mask->control[band].ht_mcs[i]);
    }

    for (i = 0; i < countof(mask->control[band].vht_mcs); i++) {
        num_rates += hweight16(mask->control[band].vht_mcs[i]);
    }

    return num_rates == 1;
}

static bool
ath10k_mac_bitrate_mask_get_single_nss(struct ath10k* ar,
                                       enum nl80211_band band,
                                       const struct cfg80211_bitrate_mask* mask,
                                       int* nss) {
    struct ieee80211_supported_band* sband = &ar->mac.sbands[band];
    uint16_t vht_mcs_map = sband->vht_cap.vht_mcs.tx_mcs_map;
    uint8_t ht_nss_mask = 0;
    uint8_t vht_nss_mask = 0;
    int i;

    if (mask->control[band].legacy) {
        return false;
    }

    for (i = 0; i < countof(mask->control[band].ht_mcs); i++) {
        if (mask->control[band].ht_mcs[i] == 0) {
            continue;
        } else if (mask->control[band].ht_mcs[i] ==
                   sband->ht_cap.mcs.rx_mask[i]) {
            ht_nss_mask |= BIT(i);
        } else {
            return false;
        }
    }

    for (i = 0; i < countof(mask->control[band].vht_mcs); i++) {
        if (mask->control[band].vht_mcs[i] == 0) {
            continue;
        } else if (mask->control[band].vht_mcs[i] ==
                   ath10k_mac_get_max_vht_mcs_map(vht_mcs_map, i)) {
            vht_nss_mask |= BIT(i);
        } else {
            return false;
        }
    }

    if (ht_nss_mask != vht_nss_mask) {
        return false;
    }

    if (ht_nss_mask == 0) {
        return false;
    }

    if (BIT(fls(ht_nss_mask)) - 1 != ht_nss_mask) {
        return false;
    }

    *nss = fls(ht_nss_mask);

    return true;
}

static int
ath10k_mac_bitrate_mask_get_single_rate(struct ath10k* ar,
                                        enum nl80211_band band,
                                        const struct cfg80211_bitrate_mask* mask,
                                        uint8_t* rate, uint8_t* nss) {
    struct ieee80211_supported_band* sband = &ar->mac.sbands[band];
    int rate_idx;
    int i;
    uint16_t bitrate;
    uint8_t preamble;
    uint8_t hw_rate;

    if (hweight32(mask->control[band].legacy) == 1) {
        rate_idx = ffs(mask->control[band].legacy) - 1;

        hw_rate = sband->bitrates[rate_idx].hw_value;
        bitrate = sband->bitrates[rate_idx].bitrate;

        if (ath10k_mac_bitrate_is_cck(bitrate)) {
            preamble = WMI_RATE_PREAMBLE_CCK;
        } else {
            preamble = WMI_RATE_PREAMBLE_OFDM;
        }

        *nss = 1;
        *rate = preamble << 6 |
                (*nss - 1) << 4 |
                hw_rate << 0;

        return 0;
    }

    for (i = 0; i < countof(mask->control[band].ht_mcs); i++) {
        if (hweight8(mask->control[band].ht_mcs[i]) == 1) {
            *nss = i + 1;
            *rate = WMI_RATE_PREAMBLE_HT << 6 |
                    (*nss - 1) << 4 |
                    (ffs(mask->control[band].ht_mcs[i]) - 1);

            return 0;
        }
    }

    for (i = 0; i < countof(mask->control[band].vht_mcs); i++) {
        if (hweight16(mask->control[band].vht_mcs[i]) == 1) {
            *nss = i + 1;
            *rate = WMI_RATE_PREAMBLE_VHT << 6 |
                    (*nss - 1) << 4 |
                    (ffs(mask->control[band].vht_mcs[i]) - 1);

            return 0;
        }
    }

    return -EINVAL;
}

static int ath10k_mac_set_fixed_rate_params(struct ath10k_vif* arvif,
        uint8_t rate, uint8_t nss, uint8_t sgi, uint8_t ldpc) {
    struct ath10k* ar = arvif->ar;
    uint32_t vdev_param;
    int ret;

    ASSERT_MTX_HELD(&ar->conf_mutex);

    ath10k_dbg(ar, ATH10K_DBG_MAC, "mac set fixed rate params vdev %i rate 0x%02hhx nss %hhu sgi %hhu\n",
               arvif->vdev_id, rate, nss, sgi);

    vdev_param = ar->wmi.vdev_param->fixed_rate;
    ret = ath10k_wmi_vdev_set_param(ar, arvif->vdev_id, vdev_param, rate);
    if (ret) {
        ath10k_warn("failed to set fixed rate param 0x%02x: %d\n",
                    rate, ret);
        return ret;
    }

    vdev_param = ar->wmi.vdev_param->nss;
    ret = ath10k_wmi_vdev_set_param(ar, arvif->vdev_id, vdev_param, nss);
    if (ret) {
        ath10k_warn("failed to set nss param %d: %d\n", nss, ret);
        return ret;
    }

    vdev_param = ar->wmi.vdev_param->sgi;
    ret = ath10k_wmi_vdev_set_param(ar, arvif->vdev_id, vdev_param, sgi);
    if (ret) {
        ath10k_warn("failed to set sgi param %d: %d\n", sgi, ret);
        return ret;
    }

    vdev_param = ar->wmi.vdev_param->ldpc;
    ret = ath10k_wmi_vdev_set_param(ar, arvif->vdev_id, vdev_param, ldpc);
    if (ret) {
        ath10k_warn("failed to set ldpc param %d: %d\n", ldpc, ret);
        return ret;
    }

    return 0;
}

static bool
ath10k_mac_can_set_bitrate_mask(struct ath10k* ar,
                                enum nl80211_band band,
                                const struct cfg80211_bitrate_mask* mask) {
    int i;
    uint16_t vht_mcs;

    /* Due to firmware limitation in WMI_PEER_ASSOC_CMDID it is impossible
     * to express all VHT MCS rate masks. Effectively only the following
     * ranges can be used: none, 0-7, 0-8 and 0-9.
     */
    for (i = 0; i < NL80211_VHT_NSS_MAX; i++) {
        vht_mcs = mask->control[band].vht_mcs[i];

        switch (vht_mcs) {
        case 0:
        case (1 << 8) - 1:
        case (1 << 9) - 1:
        case (1 << 10) - 1:
            break;
        default:
            ath10k_warn("refusing bitrate mask with missing 0-7 VHT MCS rates\n");
            return false;
        }
    }

    return true;
}

static void ath10k_mac_set_bitrate_mask_iter(void* data,
        struct ieee80211_sta* sta) {
    struct ath10k_vif* arvif = data;
    struct ath10k_sta* arsta = (struct ath10k_sta*)sta->drv_priv;
    struct ath10k* ar = arvif->ar;

    if (arsta->arvif != arvif) {
        return;
    }

    mtx_lock(&ar->data_lock);
    arsta->changed |= IEEE80211_RC_SUPP_RATES_CHANGED;
    mtx_unlock(&ar->data_lock);

    ieee80211_queue_work(ar->hw, &arsta->update_wk);
}

static int ath10k_mac_op_set_bitrate_mask(struct ieee80211_hw* hw,
        struct ieee80211_vif* vif,
        const struct cfg80211_bitrate_mask* mask) {
    struct ath10k_vif* arvif = (void*)vif->drv_priv;
    struct cfg80211_chan_def def;
    struct ath10k* ar = arvif->ar;
    enum nl80211_band band;
    const uint8_t* ht_mcs_mask;
    const uint16_t* vht_mcs_mask;
    uint8_t rate;
    uint8_t nss;
    uint8_t sgi;
    uint8_t ldpc;
    int single_nss;
    int ret;

    if (ath10k_mac_vif_chan(vif, &def)) {
        return -EPERM;
    }

    band = def.chan->band;
    ht_mcs_mask = mask->control[band].ht_mcs;
    vht_mcs_mask = mask->control[band].vht_mcs;
    ldpc = !!(ar->ht_cap_info & WMI_HT_CAP_LDPC);

    sgi = mask->control[band].gi;
    if (sgi == NL80211_TXRATE_FORCE_LGI) {
        return -EINVAL;
    }

    if (ath10k_mac_bitrate_mask_has_single_rate(ar, band, mask)) {
        ret = ath10k_mac_bitrate_mask_get_single_rate(ar, band, mask,
                &rate, &nss);
        if (ret) {
            ath10k_warn("failed to get single rate for vdev %i: %d\n",
                        arvif->vdev_id, ret);
            return ret;
        }
    } else if (ath10k_mac_bitrate_mask_get_single_nss(ar, band, mask,
               &single_nss)) {
        rate = WMI_FIXED_RATE_NONE;
        nss = single_nss;
    } else {
        rate = WMI_FIXED_RATE_NONE;
        nss = MIN(ar->num_rf_chains,
                  max(ath10k_mac_max_ht_nss(ht_mcs_mask),
                      ath10k_mac_max_vht_nss(vht_mcs_mask)));

        if (!ath10k_mac_can_set_bitrate_mask(ar, band, mask)) {
            return -EINVAL;
        }

        mtx_lock(&ar->conf_mutex);

        arvif->bitrate_mask = *mask;
        ieee80211_iterate_stations_atomic(ar->hw,
                                          ath10k_mac_set_bitrate_mask_iter,
                                          arvif);

        mtx_unlock(&ar->conf_mutex);
    }

    mtx_lock(&ar->conf_mutex);

    ret = ath10k_mac_set_fixed_rate_params(arvif, rate, nss, sgi, ldpc);
    if (ret) {
        ath10k_warn("failed to set fixed rate params on vdev %i: %d\n",
                    arvif->vdev_id, ret);
        goto exit;
    }

exit:
    mtx_unlock(&ar->conf_mutex);

    return ret;
}

static void ath10k_sta_rc_update(struct ieee80211_hw* hw,
                                 struct ieee80211_vif* vif,
                                 struct ieee80211_sta* sta,
                                 uint32_t changed) {
    struct ath10k* ar = hw->priv;
    struct ath10k_sta* arsta = (struct ath10k_sta*)sta->drv_priv;
    uint32_t bw, smps;

    mtx_lock(&ar->data_lock);

    ath10k_dbg(ar, ATH10K_DBG_MAC,
               "mac sta rc update for %pM changed %08x bw %d nss %d smps %d\n",
               sta->addr, changed, sta->bandwidth, sta->rx_nss,
               sta->smps_mode);

    if (changed & IEEE80211_RC_BW_CHANGED) {
        bw = WMI_PEER_CHWIDTH_20MHZ;

        switch (sta->bandwidth) {
        case IEEE80211_STA_RX_BW_20:
            bw = WMI_PEER_CHWIDTH_20MHZ;
            break;
        case IEEE80211_STA_RX_BW_40:
            bw = WMI_PEER_CHWIDTH_40MHZ;
            break;
        case IEEE80211_STA_RX_BW_80:
            bw = WMI_PEER_CHWIDTH_80MHZ;
            break;
        case IEEE80211_STA_RX_BW_160:
            bw = WMI_PEER_CHWIDTH_160MHZ;
            break;
        default:
            ath10k_warn("Invalid bandwidth %d in rc update for %pM\n",
                        sta->bandwidth, sta->addr);
            bw = WMI_PEER_CHWIDTH_20MHZ;
            break;
        }

        arsta->bw = bw;
    }

    if (changed & IEEE80211_RC_NSS_CHANGED) {
        arsta->nss = sta->rx_nss;
    }

    if (changed & IEEE80211_RC_SMPS_CHANGED) {
        smps = WMI_PEER_SMPS_PS_NONE;

        switch (sta->smps_mode) {
        case IEEE80211_SMPS_AUTOMATIC:
        case IEEE80211_SMPS_OFF:
            smps = WMI_PEER_SMPS_PS_NONE;
            break;
        case IEEE80211_SMPS_STATIC:
            smps = WMI_PEER_SMPS_STATIC;
            break;
        case IEEE80211_SMPS_DYNAMIC:
            smps = WMI_PEER_SMPS_DYNAMIC;
            break;
        case IEEE80211_SMPS_NUM_MODES:
            ath10k_warn("Invalid smps %d in sta rc update for %pM\n",
                        sta->smps_mode, sta->addr);
            smps = WMI_PEER_SMPS_PS_NONE;
            break;
        }

        arsta->smps = smps;
    }

    arsta->changed |= changed;

    mtx_unlock(&ar->data_lock);

    ieee80211_queue_work(hw, &arsta->update_wk);
}

static void ath10k_offset_tsf(struct ieee80211_hw* hw,
                              struct ieee80211_vif* vif, int64_t tsf_offset) {
    struct ath10k* ar = hw->priv;
    struct ath10k_vif* arvif = (void*)vif->drv_priv;
    uint32_t offset, vdev_param;
    int ret;

    if (tsf_offset < 0) {
        vdev_param = ar->wmi.vdev_param->dec_tsf;
        offset = -tsf_offset;
    } else {
        vdev_param = ar->wmi.vdev_param->inc_tsf;
        offset = tsf_offset;
    }

    ret = ath10k_wmi_vdev_set_param(ar, arvif->vdev_id,
                                    vdev_param, offset);

    if (ret && ret != -EOPNOTSUPP)
        ath10k_warn("failed to set tsf offset %d cmd %d: %d\n",
                    offset, vdev_param, ret);
}

static int ath10k_ampdu_action(struct ieee80211_hw* hw,
                               struct ieee80211_vif* vif,
                               struct ieee80211_ampdu_params* params) {
    struct ath10k* ar = hw->priv;
    struct ath10k_vif* arvif = (void*)vif->drv_priv;
    struct ieee80211_sta* sta = params->sta;
    enum ieee80211_ampdu_mlme_action action = params->action;
    uint16_t tid = params->tid;

    ath10k_dbg(ar, ATH10K_DBG_MAC, "mac ampdu vdev_id %i sta %pM tid %hu action %d\n",
               arvif->vdev_id, sta->addr, tid, action);

    switch (action) {
    case IEEE80211_AMPDU_RX_START:
    case IEEE80211_AMPDU_RX_STOP:
        /* HTT AddBa/DelBa events trigger mac80211 Rx BA session
         * creation/removal. Do we need to verify this?
         */
        return 0;
    case IEEE80211_AMPDU_TX_START:
    case IEEE80211_AMPDU_TX_STOP_CONT:
    case IEEE80211_AMPDU_TX_STOP_FLUSH:
    case IEEE80211_AMPDU_TX_STOP_FLUSH_CONT:
    case IEEE80211_AMPDU_TX_OPERATIONAL:
        /* Firmware offloads Tx aggregation entirely so deny mac80211
         * Tx aggregation requests.
         */
        return -EOPNOTSUPP;
    }

    return -EINVAL;
}

static void
ath10k_mac_update_rx_channel(struct ath10k* ar,
                             struct ieee80211_chanctx_conf* ctx,
                             struct ieee80211_vif_chanctx_switch* vifs,
                             int n_vifs) {
    struct cfg80211_chan_def* def = NULL;

    /* Both locks are required because ar->rx_channel is modified. This
     * allows readers to hold either lock.
     */
    ASSERT_MTX_HELD(&ar->conf_mutex);
    ASSERT_MTX_HELD(&ar->data_lock);

    COND_WARN(ctx && vifs);
    COND_WARN(vifs && !n_vifs);

    /* FIXME: Sort of an optimization and a workaround. Peers and vifs are
     * on a linked list now. Doing a lookup peer -> vif -> chanctx for each
     * ppdu on Rx may reduce performance on low-end systems. It should be
     * possible to make tables/hashmaps to speed the lookup up (be vary of
     * cpu data cache lines though regarding sizes) but to keep the initial
     * implementation simple and less intrusive fallback to the slow lookup
     * only for multi-channel cases. Single-channel cases will remain to
     * use the old channel derival and thus performance should not be
     * affected much.
     */
    rcu_read_lock();
    if (!ctx && ath10k_mac_num_chanctxs(ar) == 1) {
        ieee80211_iter_chan_contexts_atomic(ar->hw,
                                            ath10k_mac_get_any_chandef_iter,
                                            &def);

        if (vifs) {
            def = &vifs[0].new_ctx->def;
        }

        ar->rx_channel = def->chan;
    } else if ((ctx && ath10k_mac_num_chanctxs(ar) == 0) ||
               (ctx && (ar->state == ATH10K_STATE_RESTARTED))) {
        /* During driver restart due to firmware assert, since mac80211
         * already has valid channel context for given radio, channel
         * context iteration return num_chanctx > 0. So fix rx_channel
         * when restart is in progress.
         */
        ar->rx_channel = ctx->def.chan;
    } else {
        ar->rx_channel = NULL;
    }
    rcu_read_unlock();
}

static void
ath10k_mac_update_vif_chan(struct ath10k* ar,
                           struct ieee80211_vif_chanctx_switch* vifs,
                           int n_vifs) {
    struct ath10k_vif* arvif;
    int ret;
    int i;

    ASSERT_MTX_HELD(&ar->conf_mutex);

    /* First stop monitor interface. Some FW versions crash if there's a
     * lone monitor interface.
     */
    if (ar->monitor_started) {
        ath10k_monitor_stop(ar);
    }

    for (i = 0; i < n_vifs; i++) {
        arvif = (void*)vifs[i].vif->drv_priv;

        ath10k_dbg(ar, ATH10K_DBG_MAC,
                   "mac chanctx switch vdev_id %i freq %hu->%hu width %d->%d\n",
                   arvif->vdev_id,
                   vifs[i].old_ctx->def.chan->center_freq,
                   vifs[i].new_ctx->def.chan->center_freq,
                   vifs[i].old_ctx->def.width,
                   vifs[i].new_ctx->def.width);

        if (COND_WARN(!arvif->is_started)) {
            continue;
        }

        if (COND_WARN(!arvif->is_up)) {
            continue;
        }

        ret = ath10k_wmi_vdev_down(ar, arvif->vdev_id);
        if (ret) {
            ath10k_warn("failed to down vdev %d: %d\n",
                        arvif->vdev_id, ret);
            continue;
        }
    }

    /* All relevant vdevs are downed and associated channel resources
     * should be available for the channel switch now.
     */

    mtx_lock(&ar->data_lock);
    ath10k_mac_update_rx_channel(ar, NULL, vifs, n_vifs);
    mtx_unlock(&ar->data_lock);

    for (i = 0; i < n_vifs; i++) {
        arvif = (void*)vifs[i].vif->drv_priv;

        if (COND_WARN(!arvif->is_started)) {
            continue;
        }

        if (COND_WARN(!arvif->is_up)) {
            continue;
        }

        ret = ath10k_mac_setup_bcn_tmpl(arvif);
        if (ret)
            ath10k_warn("failed to update bcn tmpl during csa: %d\n",
                        ret);

        ret = ath10k_mac_setup_prb_tmpl(arvif);
        if (ret)
            ath10k_warn("failed to update prb tmpl during csa: %d\n",
                        ret);

        ret = ath10k_vdev_restart(arvif, &vifs[i].new_ctx->def);
        if (ret) {
            ath10k_warn("failed to restart vdev %d: %d\n",
                        arvif->vdev_id, ret);
            continue;
        }

        ret = ath10k_wmi_vdev_up(arvif->ar, arvif->vdev_id, arvif->aid,
                                 arvif->bssid);
        if (ret) {
            ath10k_warn("failed to bring vdev up %d: %d\n",
                        arvif->vdev_id, ret);
            continue;
        }
    }

    ath10k_monitor_recalc(ar);
}

static int
ath10k_mac_op_add_chanctx(struct ieee80211_hw* hw,
                          struct ieee80211_chanctx_conf* ctx) {
    struct ath10k* ar = hw->priv;

    ath10k_dbg(ar, ATH10K_DBG_MAC,
               "mac chanctx add freq %hu width %d ptr %pK\n",
               ctx->def.chan->center_freq, ctx->def.width, ctx);

    mtx_lock(&ar->conf_mutex);

    mtx_lock(&ar->data_lock);
    ath10k_mac_update_rx_channel(ar, ctx, NULL, 0);
    mtx_unlock(&ar->data_lock);

    ath10k_recalc_radar_detection(ar);
    ath10k_monitor_recalc(ar);

    mtx_unlock(&ar->conf_mutex);

    return 0;
}

static void
ath10k_mac_op_remove_chanctx(struct ieee80211_hw* hw,
                             struct ieee80211_chanctx_conf* ctx) {
    struct ath10k* ar = hw->priv;

    ath10k_dbg(ar, ATH10K_DBG_MAC,
               "mac chanctx remove freq %hu width %d ptr %pK\n",
               ctx->def.chan->center_freq, ctx->def.width, ctx);

    mtx_lock(&ar->conf_mutex);

    mtx_lock(&ar->data_lock);
    ath10k_mac_update_rx_channel(ar, NULL, NULL, 0);
    mtx_unlock(&ar->data_lock);

    ath10k_recalc_radar_detection(ar);
    ath10k_monitor_recalc(ar);

    mtx_unlock(&ar->conf_mutex);
}

struct ath10k_mac_change_chanctx_arg {
    struct ieee80211_chanctx_conf* ctx;
    struct ieee80211_vif_chanctx_switch* vifs;
    int n_vifs;
    int next_vif;
};

static void
ath10k_mac_change_chanctx_cnt_iter(void* data, uint8_t* mac,
                                   struct ieee80211_vif* vif) {
    struct ath10k_mac_change_chanctx_arg* arg = data;

    if (rcu_access_pointer(vif->chanctx_conf) != arg->ctx) {
        return;
    }

    arg->n_vifs++;
}

static void
ath10k_mac_change_chanctx_fill_iter(void* data, uint8_t* mac,
                                    struct ieee80211_vif* vif) {
    struct ath10k_mac_change_chanctx_arg* arg = data;
    struct ieee80211_chanctx_conf* ctx;

    ctx = rcu_access_pointer(vif->chanctx_conf);
    if (ctx != arg->ctx) {
        return;
    }

    if (COND_WARN(arg->next_vif == arg->n_vifs)) {
        return;
    }

    arg->vifs[arg->next_vif].vif = vif;
    arg->vifs[arg->next_vif].old_ctx = ctx;
    arg->vifs[arg->next_vif].new_ctx = ctx;
    arg->next_vif++;
}

static void
ath10k_mac_op_change_chanctx(struct ieee80211_hw* hw,
                             struct ieee80211_chanctx_conf* ctx,
                             uint32_t changed) {
    struct ath10k* ar = hw->priv;
    struct ath10k_mac_change_chanctx_arg arg = { .ctx = ctx };

    mtx_lock(&ar->conf_mutex);

    ath10k_dbg(ar, ATH10K_DBG_MAC,
               "mac chanctx change freq %hu width %d ptr %pK changed %x\n",
               ctx->def.chan->center_freq, ctx->def.width, ctx, changed);

    /* This shouldn't really happen because channel switching should use
     * switch_vif_chanctx().
     */
    if (COND_WARN(changed & IEEE80211_CHANCTX_CHANGE_CHANNEL)) {
        goto unlock;
    }

    if (changed & IEEE80211_CHANCTX_CHANGE_WIDTH) {
        ieee80211_iterate_active_interfaces_atomic(
            hw,
            IEEE80211_IFACE_ITER_NORMAL,
            ath10k_mac_change_chanctx_cnt_iter,
            &arg);
        if (arg.n_vifs == 0) {
            goto radar;
        }

        arg.vifs = kcalloc(arg.n_vifs, sizeof(arg.vifs[0]),
                           GFP_KERNEL);
        if (!arg.vifs) {
            goto radar;
        }

        ieee80211_iterate_active_interfaces_atomic(
            hw,
            IEEE80211_IFACE_ITER_NORMAL,
            ath10k_mac_change_chanctx_fill_iter,
            &arg);
        ath10k_mac_update_vif_chan(ar, arg.vifs, arg.n_vifs);
        kfree(arg.vifs);
    }

radar:
    ath10k_recalc_radar_detection(ar);

    /* FIXME: How to configure Rx chains properly? */

    /* No other actions are actually necessary. Firmware maintains channel
     * definitions per vdev internally and there's no host-side channel
     * context abstraction to configure, e.g. channel width.
     */

unlock:
    mtx_unlock(&ar->conf_mutex);
}
#endif // NEEDS PORTING

// (Re-)start vif on the specified channel. A different flow will be needed if we
// want to support continued association transferring to a new channel (likely
// ath10k_mac_update_vif_channel). Upon successful completion, we will be in a started,
// but not up, state.
zx_status_t
ath10k_mac_assign_vif_chanctx(struct ath10k* ar, wlan_channel_t* chan) {
    struct ath10k_vif* arvif = &ar->arvif;
    zx_status_t ret;

    mtx_lock(&ar->conf_mutex);

    ath10k_dbg(ar, ATH10K_DBG_MAC,
               "mac chanctx assign ptr %pK vdev_id %i\n",
               chan, arvif->vdev_id);

    if (arvif->is_started) {
        if (arvif->is_up) {
            ret = ath10k_mac_bss_disassoc(ar);
            if (ret != ZX_OK) {
                ath10k_warn("failed to disassociate vdev %i: %s\n",
                            arvif->vdev_id, zx_status_get_string(ret));
            }
        }
        ret = ath10k_vdev_restart(arvif, chan);
    } else {
        ret = ath10k_vdev_start(arvif, chan);
    }

    if (ret != ZX_OK) {
        if (chan->cbw == CBW80P80) {
            ath10k_warn("failed to start vdev %i on channels %d + %d: %s\n",
                        arvif->vdev_id, chan->primary, chan->secondary80,
                        zx_status_get_string(ret));
        } else {
            ath10k_warn("failed to start vdev %i on channel %d: %s\n",
                        arvif->vdev_id, chan->primary,
                        zx_status_get_string(ret));
        }
        goto err;
    }

    arvif->is_started = true;

#if 0 // NEEDS PORTING
    ret = ath10k_mac_vif_setup_ps(arvif);
    if (ret) {
        ath10k_warn("failed to update vdev %i ps: %d\n",
                    arvif->vdev_id, ret);
        goto err_stop;
    }

    if (vif->type == NL80211_IFTYPE_MONITOR) {
        ret = ath10k_wmi_vdev_up(ar, arvif->vdev_id, 0, vif->addr);
        if (ret) {
            ath10k_warn("failed to up monitor vdev %i: %d\n",
                        arvif->vdev_id, ret);
            goto err_stop;
        }

        arvif->is_up = true;
    }

    if (ath10k_mac_can_set_cts_prot(arvif)) {
        ret = ath10k_mac_set_cts_prot(arvif);
        if (ret)
            ath10k_warn("failed to set cts protection for vdev %d: %d\n",
                        arvif->vdev_id, ret);
    }
#endif // NEEDS PORTING

    mtx_unlock(&ar->conf_mutex);
    return ZX_OK;

#if 0 // NEEDS PORTING
err_stop:
    ath10k_vdev_stop(arvif);
    arvif->is_started = false;
    ath10k_mac_vif_setup_ps(arvif);
#endif // NEEDS PORTING

err:
    mtx_unlock(&ar->conf_mutex);
    return ret;
}

#if 0 // NEEDS PORTING
static void
ath10k_mac_op_unassign_vif_chanctx(struct ieee80211_hw* hw,
                                   struct ieee80211_vif* vif,
                                   struct ieee80211_chanctx_conf* ctx) {
    struct ath10k* ar = hw->priv;
    struct ath10k_vif* arvif = (void*)vif->drv_priv;
    int ret;

    mtx_lock(&ar->conf_mutex);

    ath10k_dbg(ar, ATH10K_DBG_MAC,
               "mac chanctx unassign ptr %pK vdev_id %i\n",
               ctx, arvif->vdev_id);

    COND_WARN(!arvif->is_started);

    if (vif->type == NL80211_IFTYPE_MONITOR) {
        COND_WARN(!arvif->is_up);

        ret = ath10k_wmi_vdev_down(ar, arvif->vdev_id);
        if (ret)
            ath10k_warn("failed to down monitor vdev %i: %d\n",
                        arvif->vdev_id, ret);

        arvif->is_up = false;
    }

    ret = ath10k_vdev_stop(arvif);
    if (ret)
        ath10k_warn("failed to stop vdev %i: %d\n",
                    arvif->vdev_id, ret);

    arvif->is_started = false;

    mtx_unlock(&ar->conf_mutex);
}

static int
ath10k_mac_op_switch_vif_chanctx(struct ieee80211_hw* hw,
                                 struct ieee80211_vif_chanctx_switch* vifs,
                                 int n_vifs,
                                 enum ieee80211_chanctx_switch_mode mode) {
    struct ath10k* ar = hw->priv;

    mtx_lock(&ar->conf_mutex);

    ath10k_dbg(ar, ATH10K_DBG_MAC,
               "mac chanctx switch n_vifs %d mode %d\n",
               n_vifs, mode);
    ath10k_mac_update_vif_chan(ar, vifs, n_vifs);

    mtx_unlock(&ar->conf_mutex);
    return 0;
}

static void ath10k_mac_op_sta_pre_rcu_remove(struct ieee80211_hw* hw,
        struct ieee80211_vif* vif,
        struct ieee80211_sta* sta) {
    struct ath10k* ar;
    struct ath10k_peer* peer;

    ar = hw->priv;

    list_for_each_entry(peer, &ar->peers, list)
    if (peer->sta == sta) {
        peer->removed = true;
    }
}

static const struct ieee80211_ops ath10k_ops = {
    .tx                         = ath10k_mac_op_tx,
    .wake_tx_queue              = ath10k_mac_op_wake_tx_queue,
    .start                      = ath10k_start,
    .stop                       = ath10k_stop,
    .config                     = ath10k_config,
    .add_interface              = ath10k_add_interface,
    .remove_interface           = ath10k_remove_interface,
    .configure_filter           = ath10k_configure_filter,
    .bss_info_changed           = ath10k_bss_info_changed,
    .set_coverage_class         = ath10k_mac_op_set_coverage_class,
    .hw_scan                    = ath10k_hw_scan,
    .cancel_hw_scan             = ath10k_cancel_hw_scan,
    .set_key                    = ath10k_set_key,
    .set_default_unicast_key    = ath10k_set_default_unicast_key,
    .sta_state                  = ath10k_sta_state,
    .conf_tx                    = ath10k_conf_tx,
    .remain_on_channel          = ath10k_remain_on_channel,
    .cancel_remain_on_channel   = ath10k_cancel_remain_on_channel,
    .set_rts_threshold          = ath10k_set_rts_threshold,
    .set_frag_threshold         = ath10k_mac_op_set_frag_threshold,
    .flush                      = ath10k_flush,
    .tx_last_beacon             = ath10k_tx_last_beacon,
    .set_antenna                = ath10k_set_antenna,
    .get_antenna                = ath10k_get_antenna,
    .reconfig_complete          = ath10k_reconfig_complete,
    .get_survey                 = ath10k_get_survey,
    .set_bitrate_mask           = ath10k_mac_op_set_bitrate_mask,
    .sta_rc_update              = ath10k_sta_rc_update,
    .offset_tsf                 = ath10k_offset_tsf,
    .ampdu_action               = ath10k_ampdu_action,
    .get_et_sset_count          = ath10k_debug_get_et_sset_count,
    .get_et_stats               = ath10k_debug_get_et_stats,
    .get_et_strings             = ath10k_debug_get_et_strings,
    .add_chanctx                = ath10k_mac_op_add_chanctx,
    .remove_chanctx             = ath10k_mac_op_remove_chanctx,
    .change_chanctx             = ath10k_mac_op_change_chanctx,
    .assign_vif_chanctx         = ath10k_mac_op_assign_vif_chanctx,
    .unassign_vif_chanctx       = ath10k_mac_op_unassign_vif_chanctx,
    .switch_vif_chanctx         = ath10k_mac_op_switch_vif_chanctx,
    .sta_pre_rcu_remove         = ath10k_mac_op_sta_pre_rcu_remove,

    CFG80211_TESTMODE_CMD(ath10k_tm_cmd)

#ifdef CONFIG_PM
    .suspend            = ath10k_wow_op_suspend,
    .resume             = ath10k_wow_op_resume,
#endif
#ifdef CONFIG_MAC80211_DEBUGFS
    .sta_add_debugfs        = ath10k_sta_add_debugfs,
    .sta_statistics         = ath10k_sta_statistics,
#endif
};
#endif // NEEDS PORTING

struct ath10k* ath10k_mac_create(size_t priv_size) {
    struct ath10k* ar;
    void* hif_ctx;

    ar = calloc(1, sizeof(struct ath10k));
    if (!ar) {
        return NULL;
    }

    hif_ctx = calloc(1, priv_size);
    if (!hif_ctx) {
        free(ar);
        return NULL;
    }

    ar->drv_priv = hif_ctx;
    return ar;
}

void ath10k_mac_destroy(struct ath10k* ar) {
    free(ar->drv_priv);
    free(ar);
}

#if 0 // NEEDS PORTING
static const struct ieee80211_iface_limit ath10k_if_limits[] = {
    {
        .max    = 8,
        .types  = BIT(NL80211_IFTYPE_STATION)
        | BIT(NL80211_IFTYPE_P2P_CLIENT)
    },
    {
        .max    = 3,
        .types  = BIT(NL80211_IFTYPE_P2P_GO)
    },
    {
        .max    = 1,
        .types  = BIT(NL80211_IFTYPE_P2P_DEVICE)
    },
    {
        .max    = 7,
        .types  = BIT(NL80211_IFTYPE_AP)
#ifdef CONFIG_MAC80211_MESH
        | BIT(NL80211_IFTYPE_MESH_POINT)
#endif
    },
};

static const struct ieee80211_iface_limit ath10k_10x_if_limits[] = {
    {
        .max    = 8,
        .types  = BIT(NL80211_IFTYPE_AP)
#ifdef CONFIG_MAC80211_MESH
        | BIT(NL80211_IFTYPE_MESH_POINT)
#endif
    },
    {
        .max    = 1,
        .types  = BIT(NL80211_IFTYPE_STATION)
    },
};

static const struct ieee80211_iface_combination ath10k_if_comb[] = {
    {
        .limits = ath10k_if_limits,
        .n_limits = countof(ath10k_if_limits),
        .max_interfaces = 8,
        .num_different_channels = 1,
        .beacon_int_infra_match = true,
    },
};

static const struct ieee80211_iface_combination ath10k_10x_if_comb[] = {
    {
        .limits = ath10k_10x_if_limits,
        .n_limits = countof(ath10k_10x_if_limits),
        .max_interfaces = 8,
        .num_different_channels = 1,
        .beacon_int_infra_match = true,
#ifdef CONFIG_ATH10K_DFS_CERTIFIED
        .radar_detect_widths =  BIT(NL80211_CHAN_WIDTH_20_NOHT) |
        BIT(NL80211_CHAN_WIDTH_20) |
        BIT(NL80211_CHAN_WIDTH_40) |
        BIT(NL80211_CHAN_WIDTH_80),
#endif
    },
};

static const struct ieee80211_iface_limit ath10k_tlv_if_limit[] = {
    {
        .max = 2,
        .types = BIT(NL80211_IFTYPE_STATION),
    },
    {
        .max = 2,
        .types = BIT(NL80211_IFTYPE_AP) |
#ifdef CONFIG_MAC80211_MESH
        BIT(NL80211_IFTYPE_MESH_POINT) |
#endif
        BIT(NL80211_IFTYPE_P2P_CLIENT) |
        BIT(NL80211_IFTYPE_P2P_GO),
    },
    {
        .max = 1,
        .types = BIT(NL80211_IFTYPE_P2P_DEVICE),
    },
};

static const struct ieee80211_iface_limit ath10k_tlv_qcs_if_limit[] = {
    {
        .max = 2,
        .types = BIT(NL80211_IFTYPE_STATION),
    },
    {
        .max = 2,
        .types = BIT(NL80211_IFTYPE_P2P_CLIENT),
    },
    {
        .max = 1,
        .types = BIT(NL80211_IFTYPE_AP) |
#ifdef CONFIG_MAC80211_MESH
        BIT(NL80211_IFTYPE_MESH_POINT) |
#endif
        BIT(NL80211_IFTYPE_P2P_GO),
    },
    {
        .max = 1,
        .types = BIT(NL80211_IFTYPE_P2P_DEVICE),
    },
};

static const struct ieee80211_iface_limit ath10k_tlv_if_limit_ibss[] = {
    {
        .max = 1,
        .types = BIT(NL80211_IFTYPE_STATION),
    },
    {
        .max = 1,
        .types = BIT(NL80211_IFTYPE_ADHOC),
    },
};

/* FIXME: This is not thouroughly tested. These combinations may over- or
 * underestimate hw/fw capabilities.
 */
static struct ieee80211_iface_combination ath10k_tlv_if_comb[] = {
    {
        .limits = ath10k_tlv_if_limit,
        .num_different_channels = 1,
        .max_interfaces = 4,
        .n_limits = countof(ath10k_tlv_if_limit),
    },
    {
        .limits = ath10k_tlv_if_limit_ibss,
        .num_different_channels = 1,
        .max_interfaces = 2,
        .n_limits = countof(ath10k_tlv_if_limit_ibss),
    },
};

static struct ieee80211_iface_combination ath10k_tlv_qcs_if_comb[] = {
    {
        .limits = ath10k_tlv_if_limit,
        .num_different_channels = 1,
        .max_interfaces = 4,
        .n_limits = countof(ath10k_tlv_if_limit),
    },
    {
        .limits = ath10k_tlv_qcs_if_limit,
        .num_different_channels = 2,
        .max_interfaces = 4,
        .n_limits = countof(ath10k_tlv_qcs_if_limit),
    },
    {
        .limits = ath10k_tlv_if_limit_ibss,
        .num_different_channels = 1,
        .max_interfaces = 2,
        .n_limits = countof(ath10k_tlv_if_limit_ibss),
    },
};

static const struct ieee80211_iface_limit ath10k_10_4_if_limits[] = {
    {
        .max = 1,
        .types = BIT(NL80211_IFTYPE_STATION),
    },
    {
        .max    = 16,
        .types  = BIT(NL80211_IFTYPE_AP)
#ifdef CONFIG_MAC80211_MESH
        | BIT(NL80211_IFTYPE_MESH_POINT)
#endif
    },
};

static const struct ieee80211_iface_combination ath10k_10_4_if_comb[] = {
    {
        .limits = ath10k_10_4_if_limits,
        .n_limits = countof(ath10k_10_4_if_limits),
        .max_interfaces = 16,
        .num_different_channels = 1,
        .beacon_int_infra_match = true,
#ifdef CONFIG_ATH10K_DFS_CERTIFIED
        .radar_detect_widths =  BIT(NL80211_CHAN_WIDTH_20_NOHT) |
        BIT(NL80211_CHAN_WIDTH_20) |
        BIT(NL80211_CHAN_WIDTH_40) |
        BIT(NL80211_CHAN_WIDTH_80),
#endif
    },
};

static void ath10k_get_arvif_iter(void* data, uint8_t* mac,
                                  struct ieee80211_vif* vif) {
    struct ath10k_vif_iter* arvif_iter = data;
    struct ath10k_vif* arvif = (void*)vif->drv_priv;

    if (arvif->vdev_id == arvif_iter->vdev_id) {
        arvif_iter->arvif = arvif;
    }
}

struct ath10k_vif* ath10k_get_arvif(struct ath10k* ar, uint32_t vdev_id) {
    struct ath10k_vif_iter arvif_iter;
    uint32_t flags;

    memset(&arvif_iter, 0, sizeof(struct ath10k_vif_iter));
    arvif_iter.vdev_id = vdev_id;

    flags = IEEE80211_IFACE_ITER_RESUME_ALL;
    ieee80211_iterate_active_interfaces_atomic(ar->hw,
            flags,
            ath10k_get_arvif_iter,
            &arvif_iter);
    if (!arvif_iter.arvif) {
        ath10k_warn("No VIF found for vdev %d\n", vdev_id);
        return NULL;
    }

    return arvif_iter.arvif;
}

#define WRD_METHOD "WRDD"
#define WRDD_WIFI  (0x07)

static uint32_t ath10k_mac_wrdd_get_mcc(struct ath10k* ar, union acpi_object* wrdd) {
    union acpi_object* mcc_pkg;
    union acpi_object* domain_type;
    union acpi_object* mcc_value;
    uint32_t i;

    if (wrdd->type != ACPI_TYPE_PACKAGE ||
            wrdd->package.count < 2 ||
            wrdd->package.elements[0].type != ACPI_TYPE_INTEGER ||
            wrdd->package.elements[0].integer.value != 0) {
        ath10k_warn("ignoring malformed/unsupported wrdd structure\n");
        return 0;
    }

    for (i = 1; i < wrdd->package.count; ++i) {
        mcc_pkg = &wrdd->package.elements[i];

        if (mcc_pkg->type != ACPI_TYPE_PACKAGE) {
            continue;
        }
        if (mcc_pkg->package.count < 2) {
            continue;
        }
        if (mcc_pkg->package.elements[0].type != ACPI_TYPE_INTEGER ||
                mcc_pkg->package.elements[1].type != ACPI_TYPE_INTEGER) {
            continue;
        }

        domain_type = &mcc_pkg->package.elements[0];
        if (domain_type->integer.value != WRDD_WIFI) {
            continue;
        }

        mcc_value = &mcc_pkg->package.elements[1];
        return mcc_value->integer.value;
    }
    return 0;
}

static int ath10k_mac_get_wrdd_regulatory(struct ath10k* ar, uint16_t* rd) {
    struct pci_dev __maybe_unused* pdev = to_pci_dev(ar->dev);
    acpi_handle root_handle;
    acpi_handle handle;
    struct acpi_buffer wrdd = {ACPI_ALLOCATE_BUFFER, NULL};
    acpi_status status;
    uint32_t alpha2_code;
    char alpha2[3];

    root_handle = ACPI_HANDLE(&pdev->dev);
    if (!root_handle) {
        return -EOPNOTSUPP;
    }

    status = acpi_get_handle(root_handle, (acpi_string)WRD_METHOD, &handle);
    if (ACPI_FAILURE(status)) {
        ath10k_dbg(ar, ATH10K_DBG_BOOT,
                   "failed to get wrd method %d\n", status);
        return -EIO;
    }

    status = acpi_evaluate_object(handle, NULL, NULL, &wrdd);
    if (ACPI_FAILURE(status)) {
        ath10k_dbg(ar, ATH10K_DBG_BOOT,
                   "failed to call wrdc %d\n", status);
        return -EIO;
    }

    alpha2_code = ath10k_mac_wrdd_get_mcc(ar, wrdd.pointer);
    kfree(wrdd.pointer);
    if (!alpha2_code) {
        return -EIO;
    }

    alpha2[0] = (alpha2_code >> 8) & 0xff;
    alpha2[1] = (alpha2_code >> 0) & 0xff;
    alpha2[2] = '\0';

    ath10k_dbg(ar, ATH10K_DBG_BOOT,
               "regulatory hint from WRDD (alpha2-code): %s\n", alpha2);

    *rd = ath_regd_find_country_by_name(alpha2);
    if (*rd == 0xffff) {
        return -EIO;
    }

    *rd |= COUNTRY_ERD_FLAG;
    return 0;
}

static int ath10k_mac_init_rd(struct ath10k* ar) {
    int ret;
    uint16_t rd;

    ret = ath10k_mac_get_wrdd_regulatory(ar, &rd);
    if (ret) {
        ath10k_dbg(ar, ATH10K_DBG_BOOT,
                   "fallback to eeprom programmed regulatory settings\n");
        rd = ar->hw_eeprom_rd;
    }

    ar->ath_common.regulatory.current_rd = rd;
    return 0;
}

int ath10k_mac_register(struct ath10k* ar) {
    static const uint32_t cipher_suites[] = {
        WLAN_CIPHER_SUITE_WEP40,
        WLAN_CIPHER_SUITE_WEP104,
        WLAN_CIPHER_SUITE_TKIP,
        WLAN_CIPHER_SUITE_CCMP,
        WLAN_CIPHER_SUITE_AES_CMAC,
    };
    struct ieee80211_supported_band* band;
    void* channels;
    int ret;

    SET_IEEE80211_PERM_ADDR(ar->hw, ar->mac_addr);

    SET_IEEE80211_DEV(ar->hw, ar->dev);

    BUILD_BUG_ON((countof(ath10k_2ghz_channels) +
                  countof(ath10k_5ghz_channels)) !=
                 ATH10K_NUM_CHANS);

    if (ar->phy_capability & WHAL_WLAN_11G_CAPABILITY) {
        channels = kmemdup(ath10k_2ghz_channels,
                           sizeof(ath10k_2ghz_channels),
                           GFP_KERNEL);
        if (!channels) {
            ret = -ENOMEM;
            goto err_free;
        }

        band = &ar->mac.sbands[NL80211_BAND_2GHZ];
        band->n_channels = countof(ath10k_2ghz_channels);
        band->channels = channels;

        if (ar->hw_params.cck_rate_map_rev2) {
            band->n_bitrates = ath10k_g_rates_rev2_size;
            band->bitrates = ath10k_g_rates_rev2;
        } else {
            band->n_bitrates = ath10k_g_rates_size;
            band->bitrates = ath10k_g_rates;
        }

        ar->hw->wiphy->bands[NL80211_BAND_2GHZ] = band;
    }

    if (ar->phy_capability & WHAL_WLAN_11A_CAPABILITY) {
        channels = kmemdup(ath10k_5ghz_channels,
                           sizeof(ath10k_5ghz_channels),
                           GFP_KERNEL);
        if (!channels) {
            ret = -ENOMEM;
            goto err_free;
        }

        band = &ar->mac.sbands[NL80211_BAND_5GHZ];
        band->n_channels = countof(ath10k_5ghz_channels);
        band->channels = channels;
        band->n_bitrates = ath10k_a_rates_size;
        band->bitrates = ath10k_a_rates;
        ar->hw->wiphy->bands[NL80211_BAND_5GHZ] = band;
    }

    ath10k_mac_setup_ht_vht_cap(ar);

    ar->hw->wiphy->interface_modes =
        BIT(NL80211_IFTYPE_STATION) |
        BIT(NL80211_IFTYPE_AP) |
        BIT(NL80211_IFTYPE_MESH_POINT);

    ar->hw->wiphy->available_antennas_rx = ar->cfg_rx_chainmask;
    ar->hw->wiphy->available_antennas_tx = ar->cfg_tx_chainmask;

    if (!BITARR_TEST(ar->normal_mode_fw.fw_file.fw_features, ATH10K_FW_FEATURE_NO_P2P))
        ar->hw->wiphy->interface_modes |=
            BIT(NL80211_IFTYPE_P2P_DEVICE) |
            BIT(NL80211_IFTYPE_P2P_CLIENT) |
            BIT(NL80211_IFTYPE_P2P_GO);

    ieee80211_hw_set(ar->hw, SIGNAL_DBM);
    ieee80211_hw_set(ar->hw, SUPPORTS_PS);
    ieee80211_hw_set(ar->hw, SUPPORTS_DYNAMIC_PS);
    ieee80211_hw_set(ar->hw, MFP_CAPABLE);
    ieee80211_hw_set(ar->hw, REPORTS_TX_ACK_STATUS);
    ieee80211_hw_set(ar->hw, HAS_RATE_CONTROL);
    ieee80211_hw_set(ar->hw, AP_LINK_PS);
    ieee80211_hw_set(ar->hw, SPECTRUM_MGMT);
    ieee80211_hw_set(ar->hw, SUPPORT_FAST_XMIT);
    ieee80211_hw_set(ar->hw, CONNECTION_MONITOR);
    ieee80211_hw_set(ar->hw, SUPPORTS_PER_STA_GTK);
    ieee80211_hw_set(ar->hw, WANT_MONITOR_VIF);
    ieee80211_hw_set(ar->hw, CHANCTX_STA_CSA);
    ieee80211_hw_set(ar->hw, QUEUE_CONTROL);
    ieee80211_hw_set(ar->hw, SUPPORTS_TX_FRAG);
    ieee80211_hw_set(ar->hw, REPORTS_LOW_ACK);

    if (!BITARR_TEST(&ar->dev_flags, ATH10K_FLAG_RAW_MODE)) {
        ieee80211_hw_set(ar->hw, SW_CRYPTO_CONTROL);
    }

    ar->hw->wiphy->features |= NL80211_FEATURE_STATIC_SMPS;
    ar->hw->wiphy->flags |= WIPHY_FLAG_IBSS_RSN;

    if (ar->ht_cap_info & WMI_HT_CAP_DYNAMIC_SMPS) {
        ar->hw->wiphy->features |= NL80211_FEATURE_DYNAMIC_SMPS;
    }

    if (ar->ht_cap_info & WMI_HT_CAP_ENABLED) {
        ieee80211_hw_set(ar->hw, AMPDU_AGGREGATION);
        ieee80211_hw_set(ar->hw, TX_AMPDU_SETUP_IN_HW);
    }

    ar->hw->wiphy->max_scan_ssids = WLAN_SCAN_PARAMS_MAX_SSID;
    ar->hw->wiphy->max_scan_ie_len = WLAN_SCAN_PARAMS_MAX_IE_LEN;

    ar->hw->vif_data_size = sizeof(struct ath10k_vif);
    ar->hw->sta_data_size = sizeof(struct ath10k_sta);
    ar->hw->txq_data_size = sizeof(struct ath10k_txq);

    ar->hw->max_listen_interval = ATH10K_MAX_HW_LISTEN_INTERVAL;

    if (BITARR_TEST(ar->wmi.svc_map, WMI_SERVICE_BEACON_OFFLOAD)) {
        ar->hw->wiphy->flags |= WIPHY_FLAG_AP_PROBE_RESP_OFFLOAD;

        /* Firmware delivers WPS/P2P Probe Requests frames to driver so
         * that userspace (e.g. wpa_supplicant/hostapd) can generate
         * correct Probe Responses. This is more of a hack advert..
         */
        ar->hw->wiphy->probe_resp_offload |=
            NL80211_PROBE_RESP_OFFLOAD_SUPPORT_WPS |
            NL80211_PROBE_RESP_OFFLOAD_SUPPORT_WPS2 |
            NL80211_PROBE_RESP_OFFLOAD_SUPPORT_P2P;
    }

    if (BITARR_TEST(ar->wmi.svc_map, WMI_SERVICE_TDLS)) {
        ar->hw->wiphy->flags |= WIPHY_FLAG_SUPPORTS_TDLS;
    }

    ar->hw->wiphy->flags |= WIPHY_FLAG_HAS_REMAIN_ON_CHANNEL;
    ar->hw->wiphy->flags |= WIPHY_FLAG_HAS_CHANNEL_SWITCH;
    ar->hw->wiphy->max_remain_on_channel_duration = 5000;

    ar->hw->wiphy->flags |= WIPHY_FLAG_AP_UAPSD;
    ar->hw->wiphy->features |= NL80211_FEATURE_AP_MODE_CHAN_WIDTH_CHANGE |
                               NL80211_FEATURE_AP_SCAN;

    ar->hw->wiphy->max_ap_assoc_sta = ar->max_num_stations;

    ret = ath10k_wow_init(ar);
    if (ret) {
        ath10k_warn("failed to init wow: %d\n", ret);
        goto err_free;
    }

    wiphy_ext_feature_set(ar->hw->wiphy, NL80211_EXT_FEATURE_VHT_IBSS);

    /*
     * on LL hardware queues are managed entirely by the FW
     * so we only advertise to mac we can do the queues thing
     */
    ar->hw->queues = IEEE80211_MAX_QUEUES;

    /* vdev_ids are used as hw queue numbers. Make sure offchan tx queue is
     * something that vdev_ids can't reach so that we don't stop the queue
     * accidentally.
     */
    ar->hw->offchannel_tx_hw_queue = IEEE80211_MAX_QUEUES - 1;

    switch (ar->running_fw->fw_file.wmi_op_version) {
    case ATH10K_FW_WMI_OP_VERSION_MAIN:
        ar->hw->wiphy->iface_combinations = ath10k_if_comb;
        ar->hw->wiphy->n_iface_combinations =
            countof(ath10k_if_comb);
        ar->hw->wiphy->interface_modes |= BIT(NL80211_IFTYPE_ADHOC);
        break;
    case ATH10K_FW_WMI_OP_VERSION_TLV:
        if (BITARR_TEST(ar->wmi.svc_map, WMI_SERVICE_ADAPTIVE_OCS)) {
            ar->hw->wiphy->iface_combinations =
                ath10k_tlv_qcs_if_comb;
            ar->hw->wiphy->n_iface_combinations =
                countof(ath10k_tlv_qcs_if_comb);
        } else {
            ar->hw->wiphy->iface_combinations = ath10k_tlv_if_comb;
            ar->hw->wiphy->n_iface_combinations =
                countof(ath10k_tlv_if_comb);
        }
        ar->hw->wiphy->interface_modes |= BIT(NL80211_IFTYPE_ADHOC);
        break;
    case ATH10K_FW_WMI_OP_VERSION_10_1:
    case ATH10K_FW_WMI_OP_VERSION_10_2:
    case ATH10K_FW_WMI_OP_VERSION_10_2_4:
        ar->hw->wiphy->iface_combinations = ath10k_10x_if_comb;
        ar->hw->wiphy->n_iface_combinations =
            countof(ath10k_10x_if_comb);
        break;
    case ATH10K_FW_WMI_OP_VERSION_10_4:
        ar->hw->wiphy->iface_combinations = ath10k_10_4_if_comb;
        ar->hw->wiphy->n_iface_combinations =
            countof(ath10k_10_4_if_comb);
        break;
    case ATH10K_FW_WMI_OP_VERSION_UNSET:
    case ATH10K_FW_WMI_OP_VERSION_MAX:
        WARN_ONCE();
        ret = -EINVAL;
        goto err_free;
    }

    if (!BITARR_TEST(&ar->dev_flags, ATH10K_FLAG_RAW_MODE)) {
        ar->hw->netdev_features = NETIF_F_HW_CSUM;
    }

    if (IS_ENABLED(CONFIG_ATH10K_DFS_CERTIFIED)) {
        /* Init ath dfs pattern detector */
        ar->ath_common.debug_mask = ATH_DBG_DFS;
        ar->dfs_detector = dfs_pattern_detector_init(&ar->ath_common,
                           NL80211_DFS_UNSET);

        if (!ar->dfs_detector) {
            ath10k_warn("failed to initialise DFS pattern detector\n");
        }
    }

    /* Current wake_tx_queue implementation imposes a significant
     * performance penalty in some setups. The tx scheduling code needs
     * more work anyway so disable the wake_tx_queue unless firmware
     * supports the pull-push mechanism.
     */
    if (!BITARR_TEST(ar->running_fw->fw_file.fw_features, ATH10K_FW_FEATURE_PEER_FLOW_CONTROL)) {
        ar->ops->wake_tx_queue = NULL;
    }

    ret = ath10k_mac_init_rd(ar);
    if (ret) {
        ath10k_err("failed to derive regdom: %d\n", ret);
        goto err_dfs_detector_exit;
    }

    /* Disable set_coverage_class for chipsets that do not support it. */
    if (!ar->hw_params.hw_ops->set_coverage_class) {
        ar->ops->set_coverage_class = NULL;
    }

    ret = ath_regd_init(&ar->ath_common.regulatory, ar->hw->wiphy,
                        ath10k_reg_notifier);
    if (ret) {
        ath10k_err("failed to initialise regulatory: %i\n", ret);
        goto err_dfs_detector_exit;
    }

    ar->hw->wiphy->cipher_suites = cipher_suites;
    ar->hw->wiphy->n_cipher_suites = countof(cipher_suites);

    wiphy_ext_feature_set(ar->hw->wiphy, NL80211_EXT_FEATURE_CQM_RSSI_LIST);

    ret = ieee80211_register_hw(ar->hw);
    if (ret) {
        ath10k_err("failed to register ieee80211: %d\n", ret);
        goto err_dfs_detector_exit;
    }

    if (!ath_is_world_regd(&ar->ath_common.regulatory)) {
        ret = regulatory_hint(ar->hw->wiphy,
                              ar->ath_common.regulatory.alpha2);
        if (ret) {
            goto err_unregister;
        }
    }

    return 0;

err_unregister:
    ieee80211_unregister_hw(ar->hw);

err_dfs_detector_exit:
    if (IS_ENABLED(CONFIG_ATH10K_DFS_CERTIFIED) && ar->dfs_detector) {
        ar->dfs_detector->exit(ar->dfs_detector);
    }

err_free:
    kfree(ar->mac.sbands[NL80211_BAND_2GHZ].channels);
    kfree(ar->mac.sbands[NL80211_BAND_5GHZ].channels);

    SET_IEEE80211_DEV(ar->hw, NULL);
    return ret;
}

void ath10k_mac_unregister(struct ath10k* ar) {
    ieee80211_unregister_hw(ar->hw);

    if (IS_ENABLED(CONFIG_ATH10K_DFS_CERTIFIED) && ar->dfs_detector) {
        ar->dfs_detector->exit(ar->dfs_detector);
    }

    kfree(ar->mac.sbands[NL80211_BAND_2GHZ].channels);
    kfree(ar->mac.sbands[NL80211_BAND_5GHZ].channels);

    SET_IEEE80211_DEV(ar->hw, NULL);
}
#endif // NEEDS PORTING
