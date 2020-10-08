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

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_BRCMU_WIFI_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_BRCMU_WIFI_H_

#include <stdint.h>

#include "third_party/bcmdhd/crossdriver/bcmwifi_channels.h"

/*
 * A chanspec (chanspec_t) holds the channel number, band, bandwidth and control
 * sideband
 */

/* channel defines */
#define CH_30MHZ_APART 6
#define CH_MIN_2G_CHANNEL 1
#define CH_MIN_5G_CHANNEL 34

/* bandstate array indices */
#define BAND_2G_INDEX 0 /* wlc->bandstate[x] index */
#define BAND_5G_INDEX 1 /* wlc->bandstate[x] index */

// clang-format on

#define WL_CHAN_VALID_HW (1 << 0)   /* valid with current HW */
#define WL_CHAN_VALID_SW (1 << 1)   /* valid with country sett. */
#define WL_CHAN_BAND_5G (1 << 2)    /* 5GHz-band channel */
#define WL_CHAN_RADAR (1 << 3)      /* radar sensitive  channel */
#define WL_CHAN_INACTIVE (1 << 4)   /* inactive due to radar */
#define WL_CHAN_PASSIVE (1 << 5)    /* channel in passive mode */
#define WL_CHAN_RESTRICTED (1 << 6) /* restricted use channel */

/* values for band specific 40MHz capabilities  */
#define WLC_N_BW_20ALL 0
#define WLC_N_BW_40ALL 1
#define WLC_N_BW_20IN2G_40IN5G 2

#define WLC_BW_20MHZ_BIT BIT(0)
#define WLC_BW_40MHZ_BIT BIT(1)
#define WLC_BW_80MHZ_BIT BIT(2)
#define WLC_BW_160MHZ_BIT BIT(3)

/* Bandwidth capabilities */
#define WLC_BW_CAP_20MHZ (WLC_BW_20MHZ_BIT)
#define WLC_BW_CAP_40MHZ (WLC_BW_40MHZ_BIT | WLC_BW_20MHZ_BIT)
#define WLC_BW_CAP_80MHZ (WLC_BW_80MHZ_BIT | WLC_BW_40MHZ_BIT | WLC_BW_20MHZ_BIT)
#define WLC_BW_CAP_160MHZ \
  (WLC_BW_160MHZ_BIT | WLC_BW_80MHZ_BIT | WLC_BW_40MHZ_BIT | WLC_BW_20MHZ_BIT)
#define WLC_BW_CAP_UNRESTRICTED 0xFF

/* band types */
#define WLC_BAND_AUTO 0 /* auto-select */
#define WLC_BAND_5G 1   /* 5 Ghz */
#define WLC_BAND_2G 2   /* 2.4 Ghz */
#define WLC_BAND_ALL 3  /* all bands */

#define CHSPEC_SB_NONE(chspec) (((chspec)&WL_CHANSPEC_CTL_SB_MASK) == WL_CHANSPEC_CTL_SB_NONE)
#define CHSPEC_CTL_CHAN(chspec)                                                \
  ((CHSPEC_SB_LOWER(chspec)) ? (LOWER_20_SB(((chspec)&WL_CHANSPEC_CHAN_MASK))) \
                             : (UPPER_20_SB(((chspec)&WL_CHANSPEC_CHAN_MASK))))

#define CHSPEC2BAND(chspec) (CHSPEC_IS5G(chspec) ? BRCM_BAND_5G : BRCM_BAND_2G)

/* defined rate in 500kbps */
#define BRCM_MAXRATE 108  /* in 500kbps units */
#define BRCM_RATE_1M 2    /* in 500kbps units */
#define BRCM_RATE_2M 4    /* in 500kbps units */
#define BRCM_RATE_5M5 11  /* in 500kbps units */
#define BRCM_RATE_11M 22  /* in 500kbps units */
#define BRCM_RATE_6M 12   /* in 500kbps units */
#define BRCM_RATE_9M 18   /* in 500kbps units */
#define BRCM_RATE_12M 24  /* in 500kbps units */
#define BRCM_RATE_18M 36  /* in 500kbps units */
#define BRCM_RATE_24M 48  /* in 500kbps units */
#define BRCM_RATE_36M 72  /* in 500kbps units */
#define BRCM_RATE_48M 96  /* in 500kbps units */
#define BRCM_RATE_54M 108 /* in 500kbps units */

#define BRCM_2G_25MHZ_OFFSET 5 /* 2.4GHz band channel offset */

#define MCSSET_LEN 16

/* Enumerate crypto algorithms */
#define CRYPTO_ALGO_OFF 0
#define CRYPTO_ALGO_WEP1 1
#define CRYPTO_ALGO_TKIP 2
#define CRYPTO_ALGO_WEP128 3
#define CRYPTO_ALGO_AES_CCM 4
#define CRYPTO_ALGO_AES_RESERVED1 5
#define CRYPTO_ALGO_AES_RESERVED2 6
#define CRYPTO_ALGO_NALG 7

// clang-format off

/* wireless security bitvec */
#define WSEC_NONE  0x0000
#define WEP_ENABLED  0x0001
#define TKIP_ENABLED 0x0002
#define AES_ENABLED  0x0004
#define WSEC_SWFLAG  0x0008
/* to go into transition mode without setting wep */
#define SES_OW_ENABLED 0x0040
/* MFP */
#define MFP_CAPABLE 0x0200
#define MFP_REQUIRED 0x0400

/* WPA authentication mode bitvec */
#define WPA_AUTH_DISABLED       0x0000  /* Legacy (i.e., non-WPA) */
#define WPA_AUTH_NONE           0x0001  /* none (IBSS) */
#define WPA_AUTH_UNSPECIFIED    0x0002  /* over 802.1x */
#define WPA_AUTH_PSK            0x0004  /* Pre-shared key */
#define WPA_AUTH_RESERVED1      0x0008
#define WPA_AUTH_RESERVED2      0x0010

#define WPA2_AUTH_RESERVED1     0x0020
#define WPA2_AUTH_UNSPECIFIED   0x0040  /* over 802.1x */
#define WPA2_AUTH_PSK           0x0080  /* Pre-shared key */
#define WPA2_AUTH_RESERVED3     0x0200
#define WPA2_AUTH_RESERVED4     0x0400
#define WPA2_AUTH_RESERVED5     0x0800
#define WPA2_AUTH_1X_SHA256     0x1000  /* 1X with SHA256 key derivation */
#define WPA2_AUTH_PSK_SHA256    0x8000  /* PSK with SHA256 key derivation */

// clang-format on

#define DOT11_DEFAULT_RTS_LEN 2347
#define DOT11_DEFAULT_FRAG_LEN 2346

#define DOT11_ICV_AES_LEN 8
#define DOT11_QOS_LEN 2
#define DOT11_IV_MAX_LEN 8
#define DOT11_A4_HDR_LEN 30

#define HT_CAP_RX_STBC_NO 0x0
#define HT_CAP_RX_STBC_ONE_STREAM 0x1

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_BRCMU_WIFI_H_
