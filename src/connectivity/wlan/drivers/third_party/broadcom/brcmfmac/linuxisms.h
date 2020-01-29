/*
 * Copyright (c) 2018 The Fuchsia Authors.
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

// This file contains what's needed to make Linux code compile (but not run) on Fuchsia.
// As the driver is ported, symbols will be removed from this file. When the driver is
// fully ported, this file will be empty and can be deleted.
// The symbols were defined by hand, based only on information from compiler errors and
// code in this driver. Do not expect defines/enums to have correct values, or struct fields to have
// correct types. Function prototypes are even less accurate.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_LINUXISMS_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_LINUXISMS_H_

#include <netinet/if_ether.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <zircon/assert.h>
#include <zircon/listnode.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <ddk/debug.h>

typedef uint16_t __be16;
typedef uint32_t __be32;
typedef uint64_t __be64;

// FROM Josh's linuxisms.h

#define BIT(pos) (1UL << (pos))

#define DIV_ROUND_UP(n, m) (((n) + ((m)-1)) / (m))

#define GENMASK1(val) ((1UL << (val)) - 1)
#define GENMASK(start, end) ((GENMASK1((start) + 1) & ~GENMASK1(end)))

#define WARN(cond, msg)                                                                         \
  ({                                                                                            \
    bool ret_cond = cond;                                                                       \
    if (ret_cond) {                                                                             \
      BRCMF_WARN("brcmfmac: unexpected condition %s warns %s at %s:%d\n", #cond, msg, __FILE__, \
                 __LINE__);                                                                     \
    }                                                                                           \
    ret_cond;                                                                                   \
  })

// TODO(cphoenix): Looks like these evaluate cond multiple times. And maybe should
// pass cond, not #cond, into WARN.
#define WARN_ON(cond)          \
  ({                           \
    if (cond) {                \
      WARN(#cond, "it's bad"); \
    }                          \
    cond;                      \
  })

#define WARN_ON_ONCE(cond)                         \
  ({                                               \
    static bool warn_next = true;                  \
    if (cond && warn_next) {                       \
      WARN(#cond, "(future warnings suppressed)"); \
      warn_next = false;                           \
    }                                              \
    cond;                                          \
  })

#define iowrite32(value, addr)                          \
  do {                                                  \
    (*(volatile uint32_t*)(uintptr_t)(addr)) = (value); \
  } while (0)

#define ioread32(addr) (*(volatile uint32_t*)(uintptr_t)(addr))

#define iowrite16(value, addr)                          \
  do {                                                  \
    (*(volatile uint16_t*)(uintptr_t)(addr)) = (value); \
  } while (0)

#define ioread16(addr) (*(volatile uint16_t*)(uintptr_t)(addr))

#define iowrite8(value, addr)                          \
  do {                                                 \
    (*(volatile uint8_t*)(uintptr_t)(addr)) = (value); \
  } while (0)

#define ioread8(addr) (*(volatile uint8_t*)(uintptr_t)(addr))

#define msleep(ms) zx_nanosleep(zx_deadline_after(ZX_MSEC(ms)))
#define PAUSE zx_nanosleep(zx_deadline_after(ZX_MSEC(50)))

#define roundup(n, m) (((n) % (m) == 0) ? (n) : (n) + ((m) - ((n) % (m))))

#define LINUX_FUNC(name, paramtype, rettype)                                               \
  static inline rettype name(paramtype foo, ...) {                                         \
    /*BRCMF_LOGF(ERROR, "brcmfmac: * * ERROR * * Called linux function %s\n", #name);   */ \
    return (rettype)0;                                                                     \
  }
#define LINUX_FUNCX(name)                                                                  \
  static inline int name() {                                                               \
    /*BRCMF_LOGF(ERROR, "brcmfmac: * * ERROR * * Called linux function %s\n", #name);   */ \
    return 0;                                                                              \
  }

// clang-format off
#define LINUX_FUNCII(name) LINUX_FUNC(name, int, int)
#define LINUX_FUNCIV(name) LINUX_FUNC(name, int, void*)
#define LINUX_FUNCVV(name) LINUX_FUNC(name, void*, void*)
#define LINUX_FUNCVI(name) LINUX_FUNC(name, void*, int)
#define LINUX_FUNCVS(name) LINUX_FUNC(name, void*, zx_status_t)
#define LINUX_FUNCcVI(name) LINUX_FUNC(name, const void*, int)
#define LINUX_FUNCcVS(name) LINUX_FUNC(name, const void*, zx_status_t)
#define LINUX_FUNCcVV(name) LINUX_FUNC(name, const void*, void*)
#define LINUX_FUNCVU(name) LINUX_FUNC(name, void*, uint16_t)
#define LINUX_FUNCUU(name) LINUX_FUNC(name, uint32_t, uint32_t)

LINUX_FUNCVI(netif_carrier_on)
LINUX_FUNCVI(netif_carrier_ok)
LINUX_FUNCVI(is_valid_ether_addr)
LINUX_FUNCVI(eth_type_trans)
LINUX_FUNCII(gcd)
LINUX_FUNCX(get_random_int)
LINUX_FUNCII(round_up)
LINUX_FUNCII(MBM_TO_DBM)

LINUX_FUNCVI(netdev_mc_count) // In core.c - Count of multicast addresses in netdev.
LINUX_FUNCX(rtnl_lock) // In core.c
LINUX_FUNCX(rtnl_unlock) // In core.c
LINUX_FUNCVV(bcm47xx_nvram_get_contents) // In firmware.c
LINUX_FUNCVI(bcm47xx_nvram_release_contents) // In firmware.c

// Last parameter of this returns an error code. Must be a zx_status_t (0 or negative).
// #define SDIO_DEVICE(a,b) (a)
LINUX_FUNCVI(pm_runtime_allow) // SDIO only
LINUX_FUNCVI(pm_runtime_forbid) // SDIO only
// Leave enable/disable_irq_wake() NOPs for now. TODO(cphoenix): Use the ZX equivalent.
LINUX_FUNCII(enable_irq_wake) // SDIO only
LINUX_FUNCII(disable_irq_wake) // SDIO only

LINUX_FUNCVI(device_release_driver)

LINUX_FUNCVI(netif_stop_queue)
LINUX_FUNCVI(cfg80211_classify8021d)
LINUX_FUNCVI(cfg80211_crit_proto_stopped)
LINUX_FUNCVI(cfg80211_check_combinations)
LINUX_FUNCVI(cfg80211_connect_done)
LINUX_FUNCVV(cfg80211_michael_mic_failure)
LINUX_FUNCVI(netif_carrier_off)

#define netdev_for_each_mc_addr(a, b) for (({BRCMF_ERR("Calling netdev_for_each_mc_addr\n"); \
                                             a = nullptr;});1;)

#define KBUILD_MODNAME "brcmfmac"

#define IEEE80211_MAX_SSID_LEN (32)

enum {
    IFNAMSIZ = (16),
    WLAN_PMKID_LEN = (16),
    WLAN_MAX_KEY_LEN = (32),
    NET_NETBUF_PAD = 1,
    IFF_PROMISC,
    NETIF_F_IP_CSUM,
    CHECKSUM_PARTIAL,
    CHECKSUM_UNNECESSARY,
};

enum nl80211_key_type {
    NL80211_KEYTYPE_GROUP,
    NL80211_KEYTYPE_PAIRWISE,
};

struct brcmfmac_pd_cc_entry {
    uint8_t* iso3166;
    uint32_t rev;
    uint8_t* cc;
};

struct brcmfmac_pd_cc {
    int table_size;
    struct brcmfmac_pd_cc_entry* table;
};

struct ieee80211_channel {
    int hw_value;
    uint32_t flags;
    int center_freq;
    int max_antenna_gain;
    int max_power;
    int band;
    uint32_t orig_flags;
};

struct vif_params {
    uint8_t macaddr[ETH_ALEN];
};

struct wireless_dev {
    struct net_device* netdev;
    uint16_t iftype;
    uint8_t address[ETH_ALEN];
};

// This stubs the use of struct sdio_func, which we only use for locking.

struct sdio_func {
    pthread_mutex_t lock;
};

void sdio_claim_host(struct sdio_func* func);
void sdio_release_host(struct sdio_func* func);

struct cfg80211_ssid {
    size_t ssid_len;
    char* ssid;
};

typedef uint64_t dma_addr_t;

struct cfg80211_match_set {
    struct cfg80211_ssid ssid;
    uint8_t bssid[ETH_ALEN];
};

struct cfg80211_sched_scan_request {
    int n_ssids;
    int n_match_sets;
    uint64_t reqid;
    int flags;
    uint8_t mac_addr[ETH_ALEN];
    struct cfg80211_ssid* ssids;
    int n_channels;
    struct ieee80211_channel* channels[555];
    struct {
        int interval;
    } * scan_plans;
    uint8_t mac_addr_mask[ETH_ALEN];
    struct cfg80211_match_set match_sets[123];
};

struct iface_combination_params {
    int num_different_channels;
    int iftype_num[555];
};

struct cfg80211_wowlan {
    int disconnect;
    struct {
        uint8_t* pattern;
        uint32_t pattern_len;
        uint8_t* mask;
        uint32_t pkt_offset;
    } * patterns;
    uint32_t n_patterns;
    int magic_pkt;
    void* nd_config;
    int gtk_rekey_failure;
};

struct cfg80211_wowlan_nd_match {
    struct {
        void* ssid;
        uint32_t ssid_len;
    } ssid;
    int n_channels;
    int* channels;
};

struct cfg80211_wowlan_nd_info {
    int n_matches;
    struct cfg80211_wowlan_nd_match* matches[555];
    int disconnect;
    int* patterns;
    int n_patterns;
};

struct netdev_hw_addr {
    uint8_t addr[ETH_ALEN];
};

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_LINUXISMS_H_
