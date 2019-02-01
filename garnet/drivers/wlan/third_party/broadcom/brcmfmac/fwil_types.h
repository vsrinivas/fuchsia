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

#ifndef GARNET_DRIVERS_WLAN_THIRD_PARTY_BROADCOM_BRCMFMAC_FWIL_TYPES_H_
#define GARNET_DRIVERS_WLAN_THIRD_PARTY_BROADCOM_BRCMFMAC_FWIL_TYPES_H_

#include "linuxisms.h"

#define BRCMF_FIL_ACTION_FRAME_SIZE 1800

// clang-format off

/* ARP Offload feature flags for arp_ol iovar */
#define BRCMF_ARP_OL_AGENT           0x00000001
#define BRCMF_ARP_OL_SNOOP           0x00000002
#define BRCMF_ARP_OL_HOST_AUTO_REPLY 0x00000004
#define BRCMF_ARP_OL_PEER_AUTO_REPLY 0x00000008

#define BRCMF_BSS_INFO_VERSION 109 /* curr ver of brcmf_bss_info_le struct */
#define BRCMF_BSS_RSSI_ON_CHANNEL 0x0002

#define BRCMF_STA_WME       0x00000002    /* WMM association */
#define BRCMF_STA_AUTHE     0x00000008    /* Authenticated */
#define BRCMF_STA_ASSOC     0x00000010    /* Associated */
#define BRCMF_STA_AUTHO     0x00000020    /* Authorized */
#define BRCMF_STA_SCBSTATS  0x00004000    /* Per STA debug stats */

/* size of brcmf_scan_params not including variable length array */
#define BRCMF_SCAN_PARAMS_FIXED_SIZE 64

/* masks for channel and ssid count */
#define BRCMF_SCAN_PARAMS_COUNT_MASK 0x0000ffff
#define BRCMF_SCAN_PARAMS_NSSID_SHIFT 16

/* scan type definitions */
#define BRCMF_SCANTYPE_DEFAULT   0xFF
#define BRCMF_SCANTYPE_ACTIVE    0
#define BRCMF_SCANTYPE_PASSIVE   1

// clang-format on

#define BRCMF_WSEC_MAX_PSK_LEN 32
#define BRCMF_WSEC_PASSPHRASE BIT(0)

/* primary (ie tx) key */
#define BRCMF_PRIMARY_KEY (1 << 1)
#define DOT11_BSSTYPE_ANY 2
#define BRCMF_ESCAN_REQ_VERSION 1

#define BRCMF_MAXRATES_IN_SET 16 /* max # of rates in rateset */

/* OBSS Coex Auto/On/Off */
#define BRCMF_OBSS_COEX_AUTO (-1)
#define BRCMF_OBSS_COEX_OFF 0
#define BRCMF_OBSS_COEX_ON 1

/* WOWL bits */
/* Wakeup on Magic packet: */
#define BRCMF_WOWL_MAGIC (1 << 0)
/* Wakeup on Netpattern */
#define BRCMF_WOWL_NET (1 << 1)
/* Wakeup on loss-of-link due to Disassoc/Deauth: */
#define BRCMF_WOWL_DIS (1 << 2)
/* Wakeup on retrograde TSF: */
#define BRCMF_WOWL_RETR (1 << 3)
/* Wakeup on loss of beacon: */
#define BRCMF_WOWL_BCN (1 << 4)
/* Wakeup after test: */
#define BRCMF_WOWL_TST (1 << 5)
/* Wakeup after PTK refresh: */
#define BRCMF_WOWL_M1 (1 << 6)
/* Wakeup after receipt of EAP-Identity Req: */
#define BRCMF_WOWL_EAPID (1 << 7)
/* Wakeind via PME(0) or GPIO(1): */
#define BRCMF_WOWL_PME_GPIO (1 << 8)
/* need tkip phase 1 key to be updated by the driver: */
#define BRCMF_WOWL_NEEDTKIP1 (1 << 9)
/* enable wakeup if GTK fails: */
#define BRCMF_WOWL_GTK_FAILURE (1 << 10)
/* support extended magic packets: */
#define BRCMF_WOWL_EXTMAGPAT (1 << 11)
/* support ARP/NS/keepalive offloading: */
#define BRCMF_WOWL_ARPOFFLOAD (1 << 12)
/* read protocol version for EAPOL frames: */
#define BRCMF_WOWL_WPA2 (1 << 13)
/* If the bit is set, use key rotaton: */
#define BRCMF_WOWL_KEYROT (1 << 14)
/* If the bit is set, frm received was bcast frame: */
#define BRCMF_WOWL_BCAST (1 << 15)
/* If the bit is set, scan offload is enabled: */
#define BRCMF_WOWL_SCANOL (1 << 16)
/* Wakeup on tcpkeep alive timeout: */
#define BRCMF_WOWL_TCPKEEP_TIME (1 << 17)
/* Wakeup on mDNS Conflict Resolution: */
#define BRCMF_WOWL_MDNS_CONFLICT (1 << 18)
/* Wakeup on mDNS Service Connect: */
#define BRCMF_WOWL_MDNS_SERVICE (1 << 19)
/* tcp keepalive got data: */
#define BRCMF_WOWL_TCPKEEP_DATA (1 << 20)
/* Firmware died in wowl mode: */
#define BRCMF_WOWL_FW_HALT (1 << 21)
/* Enable detection of radio button changes: */
#define BRCMF_WOWL_ENAB_HWRADIO (1 << 22)
/* Offloads detected MIC failure(s): */
#define BRCMF_WOWL_MIC_FAIL (1 << 23)
/* Wakeup in Unassociated state (Net/Magic Pattern): */
#define BRCMF_WOWL_UNASSOC (1 << 24)
/* Wakeup if received matched secured pattern: */
#define BRCMF_WOWL_SECURE (1 << 25)
/* Wakeup on finding preferred network */
#define BRCMF_WOWL_PFN_FOUND (1 << 27)
/* Wakeup on receiving pairwise key EAP packets: */
#define WIPHY_WOWL_EAP_PK (1 << 28)
/* Link Down indication in WoWL mode: */
#define BRCMF_WOWL_LINKDOWN (1 << 31)

#define BRCMF_WOWL_MAXPATTERNS 8
#define BRCMF_WOWL_MAXPATTERNSIZE 128

#define BRCMF_COUNTRY_BUF_SZ 4
#define BRCMF_ANT_MAX 4

#define BRCMF_MAX_ASSOCLIST 128

#define BRCMF_TXBF_SU_BFE_CAP BIT(0)
#define BRCMF_TXBF_MU_BFE_CAP BIT(1)
#define BRCMF_TXBF_SU_BFR_CAP BIT(0)
#define BRCMF_TXBF_MU_BFR_CAP BIT(1)

#define BRCMF_MAXPMKID 16 /* max # PMKID cache entries */
#define BRCMF_NUMCHANNELS 64

#define BRCMF_PFN_MACADDR_CFG_VER 1
#define BRCMF_PFN_MAC_OUI_ONLY BIT(0)
#define BRCMF_PFN_SET_MAC_UNASSOC BIT(1)

#define BRCMF_MCSSET_LEN 16

#define BRCMF_RSN_KCK_LENGTH 16
#define BRCMF_RSN_KEK_LENGTH 16
#define BRCMF_RSN_REPLAY_LEN 8

#define BRCMF_MFP_NONE 0
#define BRCMF_MFP_CAPABLE 1
#define BRCMF_MFP_REQUIRED 2

/* MAX_CHUNK_LEN is the maximum length for data passing to firmware in each
 * ioctl. It is relatively small because firmware has small maximum size input
 * playload restriction for ioctls.
 */
#define MAX_CHUNK_LEN 1400

#define DLOAD_HANDLER_VER 1        /* Downloader version */
#define DLOAD_FLAG_VER_MASK 0xf000 /* Downloader version mask */
#define DLOAD_FLAG_VER_SHIFT 12    /* Downloader version shift */

#define DL_BEGIN 0x0002
#define DL_END 0x0004

#define DL_TYPE_CLM 2

/* join preference types for join_pref iovar */
enum brcmf_join_pref_types {
    BRCMF_JOIN_PREF_RSSI = 1,
    BRCMF_JOIN_PREF_WPA,
    BRCMF_JOIN_PREF_BAND,
    BRCMF_JOIN_PREF_RSSI_DELTA,
};

enum brcmf_fil_p2p_if_types {
    BRCMF_FIL_P2P_IF_CLIENT,
    BRCMF_FIL_P2P_IF_GO,
    BRCMF_FIL_P2P_IF_DYNBCN_GO,
    BRCMF_FIL_P2P_IF_DEV,
};

enum brcmf_wowl_pattern_type {
    BRCMF_WOWL_PATTERN_TYPE_BITMAP = 0,
    BRCMF_WOWL_PATTERN_TYPE_ARP,
    BRCMF_WOWL_PATTERN_TYPE_NA
};

struct brcmf_fil_p2p_if_le {
    uint8_t addr[ETH_ALEN];
    uint16_t type;
    uint16_t chspec;
};

struct brcmf_fil_chan_info_le {
    uint32_t hw_channel;
    uint32_t target_channel;
    uint32_t scan_channel;
};

struct brcmf_fil_action_frame_le {
    uint8_t da[ETH_ALEN];
    uint16_t len;
    uint32_t packet_id;
    uint8_t data[BRCMF_FIL_ACTION_FRAME_SIZE];
};

struct brcmf_fil_af_params_le {
    uint32_t channel;
    uint32_t dwell_time;
    uint8_t bssid[ETH_ALEN];
    uint8_t pad[2];
    struct brcmf_fil_action_frame_le action_frame;
};

struct brcmf_fil_bss_enable_le {
    uint32_t bsscfgidx;
    uint32_t enable;
};

struct brcmf_fil_bwcap_le {
    uint32_t band;
    uint32_t bw_cap;
};

/**
 * struct tdls_iovar - common structure for tdls iovars.
 *
 * @ea: ether address of peer station.
 * @mode: mode value depending on specific tdls iovar.
 * @chanspec: channel specification.
 * @pad: unused (for future use).
 */
struct brcmf_tdls_iovar_le {
    uint8_t ea[ETH_ALEN]; /* Station address */
    uint8_t mode;         /* mode: depends on iovar */
    uint16_t chanspec;
    uint32_t pad; /* future */
};

enum brcmf_tdls_manual_ep_ops {
    BRCMF_TDLS_MANUAL_EP_CREATE = 1,
    BRCMF_TDLS_MANUAL_EP_DELETE = 3,
    BRCMF_TDLS_MANUAL_EP_DISCOVERY = 6
};

/* Pattern matching filter. Specifies an offset within received packets to
 * start matching, the pattern to match, the size of the pattern, and a bitmask
 * that indicates which bits within the pattern should be matched.
 */
struct brcmf_pkt_filter_pattern_le {
    /*
     * Offset within received packet to start pattern matching.
     * Offset '0' is the first byte of the ethernet header.
     */
    uint32_t offset;
    /* Size of the pattern.  Bitmask must be the same size.*/
    uint32_t size_bytes;
    /*
     * Variable length mask and pattern data. mask starts at offset 0.
     * Pattern immediately follows mask.
     */
    uint8_t mask_and_pattern[1];
};

/* IOVAR "pkt_filter_add" parameter. Used to install packet filters. */
struct brcmf_pkt_filter_le {
    uint32_t id;                                    /* Unique filter id, specified by app. */
    uint32_t type;                                  /* Filter type (WL_PKT_FILTER_TYPE_xxx). */
    uint32_t negate_match;                          /* Negate the result of filter matches */
    union {                                         /* Filter definitions */
        struct brcmf_pkt_filter_pattern_le pattern; /* Filter pattern */
    } u;
};

/* IOVAR "pkt_filter_enable" parameter. */
struct brcmf_pkt_filter_enable_le {
    uint32_t id;     /* Unique filter id */
    uint32_t enable; /* Enable/disable bool */
};

/* BSS info structure
 * Applications MUST CHECK ie_offset field and length field to access IEs and
 * next bss_info structure in a vector (in struct brcmf_scan_results)
 */
struct brcmf_bss_info_le {
    uint32_t version; /* version field */
    uint32_t length;  /* byte length of data in this record,
                       * starting at version and including IEs
                       */
    uint8_t BSSID[ETH_ALEN];
    uint16_t beacon_period; /* units are Kusec */
    uint16_t capability;    /* Capability information */
    uint8_t SSID_len;
    uint8_t SSID[32];
    struct {
        uint32_t count;    /* # rates in this set */
        uint8_t rates[16]; /* rates in 500kbps units w/hi bit set if basic */
    } rateset;             /* supported rates */
    uint16_t chanspec;     /* chanspec for bss */
    uint16_t atim_window;  /* units are Kusec */
    uint8_t dtim_period;   /* DTIM period */
    uint16_t RSSI;         /* receive signal strength (in dBm) */
    int8_t phy_noise;      /* noise (in dBm) */

    uint8_t n_cap; /* BSS is 802.11N Capable */
    /* 802.11N BSS Capabilities (based on HT_CAP_*): */
    uint32_t nbss_cap;
    uint8_t ctl_ch;                      /* 802.11N BSS control channel number */
    uint32_t reserved32[1];              /* Reserved for expansion of BSS properties */
    uint8_t flags;                       /* flags */
    uint8_t reserved[3];                 /* Reserved for expansion of BSS properties */
    uint8_t basic_mcs[BRCMF_MCSSET_LEN]; /* 802.11N BSS required MCS set */

    uint16_t ie_offset; /* offset at which IEs start, from beginning */
    uint32_t ie_length; /* byte length of Information Elements */
    uint16_t SNR;       /* average SNR of during frame reception */
    /* Add new fields here */
    /* variable length Information Elements */
};

struct brcm_rateset_le {
    /* # rates in this set */
    uint32_t count;
    /* rates in 500kbps units w/hi bit set if basic */
    uint8_t rates[BRCMF_MAXRATES_IN_SET];
};

struct brcmf_ssid_le {
    uint32_t SSID_len;
    unsigned char SSID[IEEE80211_MAX_SSID_LEN];
};

struct brcmf_scan_params_le {
    struct brcmf_ssid_le ssid_le; /* default: {0, ""} */
    uint8_t bssid[ETH_ALEN];      /* default: bcast */
    int8_t bss_type;              /* default: any,
                                   * DOT11_BSSTYPE_ANY/INFRASTRUCTURE/INDEPENDENT
                                   */
    uint8_t scan_type;            /* flags, 0 use default */
    uint32_t nprobes;             /* -1 use default, number of probes per channel */
    uint32_t active_time;         /* -1 use default, dwell time per channel for
                                   * active scanning
                                   */
    uint32_t passive_time;        /* -1 use default, dwell time per channel
                                   * for passive scanning
                                   */
    uint32_t home_time;           /* -1 use default, dwell time for the
                                   * home channel between channel scans
                                   */
    uint32_t channel_num;         /* count of channels and ssids that follow
                                   *
                                   * low half is count of channels in
                                   * channel_list, 0 means default (use all
                                   * available channels)
                                   *
                                   * high half is entries in struct brcmf_ssid
                                   * array that follows channel_list, aligned for
                                   * int32_t (4 bytes) meaning an odd channel count
                                   * implies a 2-byte pad between end of
                                   * channel_list and first ssid
                                   *
                                   * if ssid count is zero, single ssid in the
                                   * fixed parameter portion is assumed, otherwise
                                   * ssid in the fixed portion is ignored
                                   */
    uint16_t channel_list[1];     /* list of chanspecs */
};

struct brcmf_scan_results {
    uint32_t buflen;
    uint32_t version;
    uint32_t count;
    struct brcmf_bss_info_le bss_info_le[];
};

struct brcmf_escan_params_le {
    uint32_t version;
    uint16_t action;
    uint16_t sync_id;
    struct brcmf_scan_params_le params_le;
};

struct brcmf_escan_result_le {
    uint32_t buflen;
    uint32_t version;
    uint16_t sync_id;
    uint16_t bss_count;
    struct brcmf_bss_info_le bss_info_le;
};

#define WL_ESCAN_RESULTS_FIXED_SIZE \
    (sizeof(struct brcmf_escan_result_le) - sizeof(struct brcmf_bss_info_le))

/* used for association with a specific BSSID and chanspec list */
struct brcmf_assoc_params_le {
    /* 00:00:00:00:00:00: broadcast scan */
    uint8_t bssid[ETH_ALEN];
    /* 0: all available channels, otherwise count of chanspecs in
     * chanspec_list */
    uint32_t chanspec_num;
    /* list of chanspecs */
    uint16_t chanspec_list[1];
};

/**
 * struct join_pref params - parameters for preferred join selection.
 *
 * @type: preference type (see enum brcmf_join_pref_types).
 * @len: length of bytes following (currently always 2).
 * @rssi_gain: signal gain for selection (only when @type is RSSI_DELTA).
 * @band: band to which selection preference applies.
 *  This is used if @type is BAND or RSSI_DELTA.
 */
struct brcmf_join_pref_params {
    uint8_t type;
    uint8_t len;
    uint8_t rssi_gain;
    uint8_t band;
};

/* used for join with or without a specific bssid and channel list */
struct brcmf_join_params {
    struct brcmf_ssid_le ssid_le;
    struct brcmf_assoc_params_le params_le;
};

/* scan params for extended join */
struct brcmf_join_scan_params_le {
    uint8_t scan_type;     /* 0 use default, active or passive scan */
    uint32_t nprobes;      /* -1 use default, nr of probes per channel */
    uint32_t active_time;  /* -1 use default, dwell time per channel for
                            * active scanning
                            */
    uint32_t passive_time; /* -1 use default, dwell time per channel
                            * for passive scanning
                            */
    uint32_t home_time;    /* -1 use default, dwell time for the home
                            * channel between channel scans
                            */
};

/* extended join params */
struct brcmf_ext_join_params_le {
    struct brcmf_ssid_le ssid_le; /* {0, ""}: wildcard scan */
    struct brcmf_join_scan_params_le scan_le;
    struct brcmf_assoc_params_le assoc_le;
};

struct brcmf_wsec_key {
    uint32_t index;                 /* key index */
    uint32_t len;                   /* key length */
    uint8_t data[WLAN_MAX_KEY_LEN]; /* key data */
    uint32_t pad_1[18];
    uint32_t algo;  /* CRYPTO_ALGO_AES_CCM, CRYPTO_ALGO_WEP128, etc */
    uint32_t flags; /* misc flags */
    uint32_t pad_2[3];
    uint32_t iv_initialized; /* has IV been initialized already? */
    uint32_t pad_3;
    /* Rx IV */
    struct {
        uint32_t hi; /* upper 32 bits of IV */
        uint16_t lo; /* lower 16 bits of IV */
    } rxiv;
    uint32_t pad_4[2];
    uint8_t ea[ETH_ALEN]; /* per station */
};

/*
 * dongle requires same struct as above but with fields in little endian order
 */
struct brcmf_wsec_key_le {
    uint32_t index;                 /* key index */
    uint32_t len;                   /* key length */
    uint8_t data[WLAN_MAX_KEY_LEN]; /* key data */
    uint32_t pad_1[18];
    uint32_t algo;  /* CRYPTO_ALGO_AES_CCM, CRYPTO_ALGO_WEP128, etc */
    uint32_t flags; /* misc flags */
    uint32_t pad_2[3];
    uint32_t iv_initialized; /* has IV been initialized already? */
    uint32_t pad_3;
    /* Rx IV */
    struct {
        uint32_t hi; /* upper 32 bits of IV */
        uint16_t lo; /* lower 16 bits of IV */
    } rxiv;
    uint32_t pad_4[2];
    uint8_t ea[ETH_ALEN]; /* per station */
};

/**
 * struct brcmf_wsec_pmk_le - firmware pmk material.
 *
 * @key_len: number of octets in key material.
 * @flags: key handling qualifiers.
 * @key: PMK key material.
 */
struct brcmf_wsec_pmk_le {
    uint16_t key_len;
    uint16_t flags;
    uint8_t key[2 * BRCMF_WSEC_MAX_PSK_LEN + 1];
};

/* Used to get specific STA parameters */
struct brcmf_scb_val_le {
    uint32_t val;
    uint8_t ea[ETH_ALEN];
};

/* channel encoding */
struct brcmf_channel_info_le {
    uint32_t hw_channel;
    uint32_t target_channel;
    uint32_t scan_channel;
};

struct brcmf_sta_info_le {
    uint16_t ver;                          /* version of this struct */
    uint16_t len;                          /* length in bytes of this structure */
    uint16_t cap;                          /* sta's advertised capabilities */
    uint32_t flags;                        /* flags defined below */
    uint32_t idle;                         /* time since data pkt rx'd from sta */
    uint8_t ea[ETH_ALEN];                  /* Station address */
    uint32_t count;                        /* # rates in this set */
    uint8_t rates[BRCMF_MAXRATES_IN_SET];  /* rates in 500kbps units */
                                           /* w/hi bit set if basic */
    uint32_t in;                           /* seconds elapsed since associated */
    uint32_t listen_interval_inms;         /* Min Listen interval in ms for STA */
    uint32_t tx_pkts;                      /* # of packets transmitted */
    uint32_t tx_failures;                  /* # of packets failed */
    uint32_t rx_ucast_pkts;                /* # of unicast packets received */
    uint32_t rx_mcast_pkts;                /* # of multicast packets received */
    uint32_t tx_rate;                      /* Rate of last successful tx frame */
    uint32_t rx_rate;                      /* Rate of last successful rx frame */
    uint32_t rx_decrypt_succeeds;          /* # of packet decrypted successfully */
    uint32_t rx_decrypt_failures;          /* # of packet decrypted failed */
    uint32_t tx_tot_pkts;                  /* # of tx pkts (ucast + mcast) */
    uint32_t rx_tot_pkts;                  /* # of data packets recvd (uni + mcast) */
    uint32_t tx_mcast_pkts;                /* # of mcast pkts txed */
    uint64_t tx_tot_bytes;                 /* data bytes txed (ucast + mcast) */
    uint64_t rx_tot_bytes;                 /* data bytes recvd (ucast + mcast) */
    uint64_t tx_ucast_bytes;               /* data bytes txed (ucast) */
    uint64_t tx_mcast_bytes;               /* # data bytes txed (mcast) */
    uint64_t rx_ucast_bytes;               /* data bytes recvd (ucast) */
    uint64_t rx_mcast_bytes;               /* data bytes recvd (mcast) */
    int8_t rssi[BRCMF_ANT_MAX];            /* per antenna rssi */
    int8_t nf[BRCMF_ANT_MAX];              /* per antenna noise floor */
    uint16_t aid;                          /* association ID */
    uint16_t ht_capabilities;              /* advertised ht caps */
    uint16_t vht_flags;                    /* converted vht flags */
    uint32_t tx_pkts_retry_cnt;            /* # of frames where a retry was
                                            * exhausted.
                                            */
    uint32_t tx_pkts_retry_exhausted;      /* # of user frames where a retry
                                            * was exhausted
                                            */
    int8_t rx_lastpkt_rssi[BRCMF_ANT_MAX]; /* Per antenna RSSI of last
                                            * received data frame.
                                            */
    /* TX WLAN retry/failure statistics:
     * Separated for host requested frames and locally generated frames.
     * Include unicast frame only where the retries/failures can be counted.
     */
    uint32_t tx_pkts_total;              /* # user frames sent successfully */
    uint32_t tx_pkts_retries;            /* # user frames retries */
    uint32_t tx_pkts_fw_total;           /* # FW generated sent successfully */
    uint32_t tx_pkts_fw_retries;         /* # retries for FW generated frames */
    uint32_t tx_pkts_fw_retry_exhausted; /* # FW generated where a retry
                                          * was exhausted
                                          */
    uint32_t rx_pkts_retried;            /* # rx with retry bit set */
    uint32_t tx_rate_fallback;           /* lowest fallback TX rate */
};

struct brcmf_chanspec_list {
    uint32_t count;      /* # of entries */
    uint32_t element[1]; /* variable length uint32 list */
};

/*
 * WLC_E_PROBRESP_MSG
 * WLC_E_P2P_PROBREQ_MSG
 * WLC_E_ACTION_FRAME_RX
 */
struct brcmf_rx_mgmt_data {
    __be16 version;
    __be16 chanspec;
    __be32 rssi;
    __be32 mactime;
    __be32 rate;
};

/**
 * struct brcmf_fil_wowl_pattern_le - wowl pattern configuration struct.
 *
 * @cmd: "add", "del" or "clr".
 * @masksize: Size of the mask in #of bytes
 * @offset: Pattern byte offset in packet
 * @patternoffset: Offset of start of pattern. Starting from field masksize.
 * @patternsize: Size of the pattern itself in #of bytes
 * @id: id
 * @reasonsize: Size of the wakeup reason code
 * @type: Type of pattern (enum brcmf_wowl_pattern_type)
 */
struct brcmf_fil_wowl_pattern_le {
    uint8_t cmd[4];
    uint32_t masksize;
    uint32_t offset;
    uint32_t patternoffset;
    uint32_t patternsize;
    uint32_t id;
    uint32_t reasonsize;
    uint32_t type;
    /* uint8_t mask[] - Mask follows the structure above */
    /* uint8_t pattern[] - Pattern follows the mask is at 'patternoffset' */
};

struct brcmf_mbss_ssid_le {
    uint32_t bsscfgidx;
    uint32_t SSID_len;
    unsigned char SSID[32];
};

/**
 * struct brcmf_fil_country_le - country configuration structure.
 *
 * @country_abbrev: null-terminated country code used in the country IE.
 * @rev: revision specifier for ccode. on set, -1 indicates unspecified.
 * @ccode: null-terminated built-in country code.
 */
struct brcmf_fil_country_le {
    char country_abbrev[BRCMF_COUNTRY_BUF_SZ];
    uint32_t rev;
    char ccode[BRCMF_COUNTRY_BUF_SZ];
};

/**
 * struct brcmf_rev_info_le - device revision info.
 *
 * @vendorid: PCI vendor id.
 * @deviceid: device id of chip.
 * @radiorev: radio revision.
 * @chiprev: chip revision.
 * @corerev: core revision.
 * @boardid: board identifier (usu. PCI sub-device id).
 * @boardvendor: board vendor (usu. PCI sub-vendor id).
 * @boardrev: board revision.
 * @driverrev: driver version.
 * @ucoderev: microcode version.
 * @bus: bus type.
 * @chipnum: chip number.
 * @phytype: phy type.
 * @phyrev: phy revision.
 * @anarev: anacore rev.
 * @chippkg: chip package info.
 * @nvramrev: nvram revision number.
 */
struct brcmf_rev_info_le {
    uint32_t vendorid;
    uint32_t deviceid;
    uint32_t radiorev;
    uint32_t chiprev;
    uint32_t corerev;
    uint32_t boardid;
    uint32_t boardvendor;
    uint32_t boardrev;
    uint32_t driverrev;
    uint32_t ucoderev;
    uint32_t bus;
    uint32_t chipnum;
    uint32_t phytype;
    uint32_t phyrev;
    uint32_t anarev;
    uint32_t chippkg;
    uint32_t nvramrev;
};

/**
 * struct brcmf_assoclist_le - request assoc list.
 *
 * @count: indicates number of stations.
 * @mac: MAC addresses of stations.
 */
struct brcmf_assoclist_le {
    uint32_t count;
    uint8_t mac[BRCMF_MAX_ASSOCLIST][ETH_ALEN];
};

/**
 * struct brcmf_wowl_wakeind_le - Wakeup indicators
 *  Note: note both fields contain same information.
 *
 * @pci_wakeind: Whether PCI PMECSR PMEStatus bit was set.
 * @ucode_wakeind: What wakeup-event indication was set by ucode
 */
struct brcmf_wowl_wakeind_le {
    uint32_t pci_wakeind;
    uint32_t ucode_wakeind;
};

/**
 * struct brcmf_pmksa - PMK Security Association
 *
 * @bssid: The AP's BSSID.
 * @pmkid: he PMK material itself.
 */
struct brcmf_pmksa {
    uint8_t bssid[ETH_ALEN];
    uint8_t pmkid[WLAN_PMKID_LEN];
};

/**
 * struct brcmf_pmk_list_le - List of pmksa's.
 *
 * @npmk: Number of pmksa's.
 * @pmk: PMK SA information.
 */
struct brcmf_pmk_list_le {
    uint32_t npmk;
    struct brcmf_pmksa pmk[BRCMF_MAXPMKID];
};

/**
 * struct brcmf_pno_param_le - PNO scan configuration parameters
 *
 * @version: PNO parameters version.
 * @scan_freq: scan frequency.
 * @lost_network_timeout: #sec. to declare discovered network as lost.
 * @flags: Bit field to control features of PFN such as sort criteria auto
 *  enable switch and background scan.
 * @rssi_margin: Margin to avoid jitter for choosing a PFN based on RSSI sort
 *  criteria.
 * @bestn: number of best networks in each scan.
 * @mscan: number of scans recorded.
 * @repeat: minimum number of scan intervals before scan frequency changes
 *  in adaptive scan.
 * @exp: exponent of 2 for maximum scan interval.
 * @slow_freq: slow scan period.
 */
struct brcmf_pno_param_le {
    uint32_t version;
    uint32_t scan_freq;
    uint32_t lost_network_timeout;
    uint16_t flags;
    uint16_t rssi_margin;
    uint8_t bestn;
    uint8_t mscan;
    uint8_t repeat;
    uint8_t exp;
    uint32_t slow_freq;
};

/**
 * struct brcmf_pno_config_le - PNO channel configuration.
 *
 * @reporttype: determines what is reported.
 * @channel_num: number of channels specified in @channel_list.
 * @channel_list: channels to use in PNO scan.
 * @flags: reserved.
 */
struct brcmf_pno_config_le {
    uint32_t reporttype;
    uint32_t channel_num;
    uint16_t channel_list[BRCMF_NUMCHANNELS];
    uint32_t flags;
};

/**
 * struct brcmf_pno_net_param_le - scan parameters per preferred network.
 *
 * @ssid: ssid name and its length.
 * @flags: bit2: hidden.
 * @infra: BSS vs IBSS.
 * @auth: Open vs Closed.
 * @wpa_auth: WPA type.
 * @wsec: wsec value.
 */
struct brcmf_pno_net_param_le {
    struct brcmf_ssid_le ssid;
    uint32_t flags;
    uint32_t infra;
    uint32_t auth;
    uint32_t wpa_auth;
    uint32_t wsec;
};

/**
 * struct brcmf_pno_net_info_le - information per found network.
 *
 * @bssid: BSS network identifier.
 * @channel: channel number only.
 * @SSID_len: length of ssid.
 * @SSID: ssid characters.
 * @RSSI: receive signal strength (in dBm).
 * @timestamp: age in seconds.
 */
struct brcmf_pno_net_info_le {
    uint8_t bssid[ETH_ALEN];
    uint8_t channel;
    uint8_t SSID_len;
    uint8_t SSID[32];
    uint16_t RSSI;
    uint16_t timestamp;
};

/**
 * struct brcmf_pno_scanresults_le - result returned in PNO NET FOUND event.
 *
 * @version: PNO version identifier.
 * @status: indicates completion status of PNO scan.
 * @count: amount of brcmf_pno_net_info_le entries appended.
 */
struct brcmf_pno_scanresults_le {
    uint32_t version;
    uint32_t status;
    uint32_t count;
};

struct brcmf_pno_scanresults_v2_le {
    uint32_t version;
    uint32_t status;
    uint32_t count;
    uint32_t scan_ch_bucket;
};

/**
 * struct brcmf_pno_macaddr_le - to configure PNO macaddr randomization.
 *
 * @version: PNO version identifier.
 * @flags: Flags defining how mac addrss should be used.
 * @mac: MAC address.
 */
struct brcmf_pno_macaddr_le {
    uint8_t version;
    uint8_t flags;
    uint8_t mac[ETH_ALEN];
};

/**
 * struct brcmf_dload_data_le - data passing to firmware for downloading
 * @flag: flags related to download data.
 * @dload_type: type of download data.
 * @len: length in bytes of download data.
 * @crc: crc of download data.
 * @data: download data.
 */
struct brcmf_dload_data_le {
    uint16_t flag;
    uint16_t dload_type;
    uint32_t len;
    uint32_t crc;
    uint8_t data[1];
};

/**
 * struct brcmf_pno_bssid_le - bssid configuration for PNO scan.
 *
 * @bssid: BSS network identifier.
 * @flags: flags for this BSSID.
 */
struct brcmf_pno_bssid_le {
    uint8_t bssid[ETH_ALEN];
    uint16_t flags;
};

/**
 * struct brcmf_pktcnt_le - packet counters.
 *
 * @rx_good_pkt: packets (MSDUs & MMPDUs) received from this station
 * @rx_bad_pkt: failed rx packets
 * @tx_good_pkt: packets (MSDUs & MMPDUs) transmitted to this station
 * @tx_bad_pkt: failed tx packets
 * @rx_ocast_good_pkt: unicast packets destined for others
 */
struct brcmf_pktcnt_le {
    uint32_t rx_good_pkt;
    uint32_t rx_bad_pkt;
    uint32_t tx_good_pkt;
    uint32_t tx_bad_pkt;
    uint32_t rx_ocast_good_pkt;
};

/**
 * struct brcmf_gtk_keyinfo_le - GTP rekey data
 *
 * @kck: key confirmation key.
 * @kek: key encryption key.
 * @replay_counter: replay counter.
 */
struct brcmf_gtk_keyinfo_le {
    uint8_t kck[BRCMF_RSN_KCK_LENGTH];
    uint8_t kek[BRCMF_RSN_KEK_LENGTH];
    uint8_t replay_counter[BRCMF_RSN_REPLAY_LEN];
};

#define BRCMF_PNO_REPORT_NO_BATCH BIT(2)

/**
 * struct brcmf_gscan_bucket_config - configuration data for channel bucket.
 *
 * @bucket_end_index: last channel index in @channel_list in
 *  @struct brcmf_pno_config_le.
 * @bucket_freq_multiple: scan interval expressed in N * @scan_freq.
 * @flag: channel bucket report flags.
 * @reserved: for future use.
 * @repeat: number of scan at interval for exponential scan.
 * @max_freq_multiple: maximum scan interval for exponential scan.
 */
struct brcmf_gscan_bucket_config {
    uint8_t bucket_end_index;
    uint8_t bucket_freq_multiple;
    uint8_t flag;
    uint8_t reserved;
    uint16_t repeat;
    uint16_t max_freq_multiple;
};

/* version supported which must match firmware */
#define BRCMF_GSCAN_CFG_VERSION 2

/**
 * enum brcmf_gscan_cfg_flags - bit values for gscan flags.
 *
 * @BRCMF_GSCAN_CFG_FLAGS_ALL_RESULTS: send probe responses/beacons to host.
 * @BRCMF_GSCAN_CFG_ALL_BUCKETS_IN_1ST_SCAN: all buckets will be included in
 *  first scan cycle.
 * @BRCMF_GSCAN_CFG_FLAGS_CHANGE_ONLY: indicated only flags member is changed.
 */
enum brcmf_gscan_cfg_flags {
    BRCMF_GSCAN_CFG_FLAGS_ALL_RESULTS = BIT(0),
    BRCMF_GSCAN_CFG_ALL_BUCKETS_IN_1ST_SCAN = BIT(3),
    BRCMF_GSCAN_CFG_FLAGS_CHANGE_ONLY = BIT(7),
};

/**
 * struct brcmf_gscan_config - configuration data for gscan.
 *
 * @version: version of the api to match firmware.
 * @flags: flags according %enum brcmf_gscan_cfg_flags.
 * @buffer_threshold: percentage threshold of buffer to generate an event.
 * @swc_nbssid_threshold: number of BSSIDs with significant change that
 *  will generate an event.
 * @swc_rssi_window_size: size of rssi cache buffer (max=8).
 * @count_of_channel_buckets: number of array members in @bucket.
 * @retry_threshold: !unknown!
 * @lost_ap_window: !unknown!
 * @bucket: array of channel buckets.
 */
struct brcmf_gscan_config {
    uint16_t version;
    uint8_t flags;
    uint8_t buffer_threshold;
    uint8_t swc_nbssid_threshold;
    uint8_t swc_rssi_window_size;
    uint8_t count_of_channel_buckets;
    uint8_t retry_threshold;
    uint16_t lost_ap_window;
    struct brcmf_gscan_bucket_config bucket[1];
};

#endif /* GARNET_DRIVERS_WLAN_THIRD_PARTY_BROADCOM_BRCMFMAC_FWIL_TYPES_H_ */
