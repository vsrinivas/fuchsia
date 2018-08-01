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

#ifndef GARNET_DRIVERS_WLAN_THIRD_PARTY_BROADCOM_INCLUDE_LINUXISMS_H_
#define GARNET_DRIVERS_WLAN_THIRD_PARTY_BROADCOM_INCLUDE_LINUXISMS_H_

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/protocol/usb.h> // Remove when the USB structs move out
#include <netinet/if_ether.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <zircon/assert.h>
#include <zircon/listnode.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

typedef uint16_t __be16;
typedef uint32_t __be32;
typedef uint64_t __be64;

// FROM Josh's linuxisms.h

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof(arr[0]))

#define BIT(pos) (1UL << (pos))

#define DIV_ROUND_UP(n, m) (((n) + ((m)-1)) / (m))

#define GENMASK1(val) ((1UL << (val)) - 1)
#define GENMASK(start, end) ((GENMASK1((start) + 1) & ~GENMASK1(end)))

#define WARN(cond, msg)                                                           \
    ({  bool ret_cond = cond;                                                     \
        if (ret_cond) {                                                           \
            zxlogf(WARN, "brcmfmac: unexpected condition %s warns %s at %s:%d\n", \
                #cond, msg, __FILE__, __LINE__);                                  \
        }                                                                         \
        ret_cond;                                                                 \
    })

// TODO(cphoenix): Looks like these evaluate cond multiple times. And maybe should
// pass cond, not #cond, into WARN.
#define WARN_ON(cond)                          \
    ({                                         \
        if (cond) { WARN(#cond, "it's bad"); } \
        cond;                                  \
    })

#define WARN_ON_ONCE(cond)                               \
    ({                                                   \
        static bool warn_next = true;                    \
        if (cond && warn_next) {                         \
            WARN(#cond, "(future warnings suppressed)"); \
            warn_next = false;                           \
        }                                                \
        cond;                                            \
    })

#define iowrite32(value, addr)                              \
    do {                                                    \
        (*(volatile uint32_t*)(uintptr_t)(addr)) = (value); \
    } while (0)

#define ioread32(addr) (*(volatile uint32_t*)(uintptr_t)(addr))

#define iowrite16(value, addr)                              \
    do {                                                    \
        (*(volatile uint16_t*)(uintptr_t)(addr)) = (value); \
    } while (0)

#define ioread16(addr) (*(volatile uint16_t*)(uintptr_t)(addr))

#define iowrite8(value, addr)                              \
    do {                                                    \
        (*(volatile uint8_t*)(uintptr_t)(addr)) = (value); \
    } while (0)

#define ioread8(addr) (*(volatile uint8_t*)(uintptr_t)(addr))

#define usleep(us) zx_nanosleep(zx_deadline_after(ZX_USEC(us)))
#define usleep_range(early, late) usleep(early)
#define msleep(ms) zx_nanosleep(zx_deadline_after(ZX_MSEC(ms)))
#define PAUSE zx_nanosleep(zx_deadline_after(ZX_MSEC(50)))

#define min(a, b) (((a) < (b)) ? (a) : (b))
#define min_t(t, a, b) (((t)(a) < (t)(b)) ? (t)(a) : (t)(b))

#define roundup(n, m) (((n) % (m) == 0) ? (n) : (n) + ((m) - ((n) % (m))))

#define LINUX_FUNC(name, paramtype, rettype)                                                   \
    static inline rettype name(paramtype foo, ...) {                                           \
        zxlogf(ERROR, "brcmfmac: * * ERROR * * Called linux function %s\n", #name);            \
        return (rettype)0;                                                                     \
    }
#define LINUX_FUNCX(name)                                                                      \
    static inline int name() {                                                                 \
        zxlogf(ERROR, "brcmfmac: * * ERROR * * Called linux function %s\n", #name);            \
        return 0;                                                                              \
    }

#define CHIPSET_ARM_CM3_CORE      0x82a
#define CHIPSET_INTERNAL_MEM_CORE 0x80e
#define CHIPSET_ARM_CR4_CORE      0x83e
#define CHIPSET_ARM_CA7_CORE      0x847
#define CHIPSET_80211_CORE        0x812
#define CHIPSET_PCIE2_CORE        0x83c
#define CHIPSET_SDIO_DEV_CORE     0x829
#define CHIPSET_CHIPCOMMON_CORE   0x800
#define CHIPSET_SYS_MEM_CORE      0x849
#define CHIPSET_PMU_CORE          0x827

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
LINUX_FUNCVI(ether_addr_equal) // Trivial
LINUX_FUNCVI(is_valid_ether_addr)
LINUX_FUNCVI(eth_type_trans)
LINUX_FUNCII(BITS_TO_LONGS)
LINUX_FUNCII(gcd)
LINUX_FUNCX(get_random_int)
LINUX_FUNCII(round_up)
LINUX_FUNCVI(nla_put) // Add netlink attribute to netbuf
LINUX_FUNCVI(nla_put_u16) // Add u16 attribute to netbuf
LINUX_FUNCII(MBM_TO_DBM)
LINUX_FUNCX(prandom_u32)
LINUX_FUNCVU(get_unaligned_le16)
LINUX_FUNCUU(put_unaligned_le32)
static inline uint32_t get_unaligned_le32(void* addr) {
    uint32_t value;
    memcpy(&value, addr, sizeof(uint32_t));
    return value;
}

LINUX_FUNCVI(netdev_mc_count) // In core.c
LINUX_FUNCVI(waitqueue_active) // In core.c
LINUX_FUNCX(rtnl_lock) // In core.c and p2p.c
LINUX_FUNCX(rtnl_unlock) // In core.c and p2p.c
LINUX_FUNCVV(bcm47xx_nvram_get_contents) // In firmware.c
LINUX_FUNCVI(bcm47xx_nvram_release_contents) // In firmware.c
LINUX_FUNCX(in_interrupt) // In core.c and sdio.c

LINUX_FUNCVI(device_set_wakeup_enable) // USB only
LINUX_FUNCVI(usb_deregister) // USB only
LINUX_FUNCVI(driver_for_each_device) // In usb.c only

LINUX_FUNCII(send_sig) // SDIO only
LINUX_FUNCVI(kthread_stop) // SDIO only
LINUX_FUNCVI(pr_warn) // SDIO only
LINUX_FUNCII(enable_irq) // SDIO only
// Last parameter of this returns an error code. Must be a zx_status_t (0 or negative).
LINUX_FUNCVI(sdio_readb) // SDIO only
// Last parameter of this returns an error code. Must be a zx_status_t (0 or negative).
LINUX_FUNCVI(sdio_writeb) // SDIO only
LINUX_FUNCVI(sdio_claim_host) // SDIO only
LINUX_FUNCVI(sdio_release_host) // SDIO only
LINUX_FUNCVI(sdio_claim_irq)
LINUX_FUNCVI(sdio_release_irq)
LINUX_FUNCVI(sdio_readl)  // Last param is zx_status_t
LINUX_FUNCVI(sdio_writel) // Last param is zx_status_t
LINUX_FUNCVS(sdio_memcpy_fromio)
LINUX_FUNCVS(sdio_readsb)
LINUX_FUNCVS(sdio_set_block_size)
#define SDIO_DEVICE(a,b) (a)
LINUX_FUNCVS(sdio_register_driver)
LINUX_FUNCVV(sdio_unregister_driver)
LINUX_FUNCVI(sdio_f0_writeb)
LINUX_FUNCVI(sdio_memcpy_toio)
LINUX_FUNCVI(sdio_f0_readb)
LINUX_FUNCVI(pm_runtime_allow) // SDIO only
LINUX_FUNCVI(pm_runtime_forbid) // SDIO only
LINUX_FUNCII(disable_irq_nosync) // SDIO only
LINUX_FUNCII(request_irq) // SDIO only
LINUX_FUNCII(enable_irq_wake) // SDIO only
LINUX_FUNCII(disable_irq_wake) // SDIO only
LINUX_FUNCVI(of_device_is_compatible)
LINUX_FUNCVI(of_property_read_u32)
LINUX_FUNCVI(of_find_property)
LINUX_FUNCVI(irq_of_parse_and_map) // OF only
LINUX_FUNCII(irqd_get_trigger_type) // OF only
LINUX_FUNCII(irq_get_irq_data) // OF only
LINUX_FUNCII(free_irq) // PCI & SDIO only
LINUX_FUNCII(allow_signal) // SDIO only
LINUX_FUNCX(kthread_should_stop) // SDIO only
LINUX_FUNCVS(kthread_run) // SDIO only
LINUX_FUNCX(wmb) // SDIO only
LINUX_FUNCX(rmb) // SDIO only

LINUX_FUNCVI(device_release_driver)
#define module_param_string(a, b, c, d)
#define module_exit(a) \
    void* __modexit() { return a; }
#define module_init(a) \
    void* __modinit() { return a; }

LINUX_FUNCVI(netif_stop_queue)
LINUX_FUNCVI(cfg80211_classify8021d)
LINUX_FUNCVI(cfg80211_crit_proto_stopped)
LINUX_FUNCVV(cfg80211_vendor_cmd_alloc_reply_netbuf)
LINUX_FUNCVI(cfg80211_vendor_cmd_reply)
LINUX_FUNCVI(cfg80211_ready_on_channel)
LINUX_FUNCcVS(cfg80211_get_p2p_attr) // TODO(cphoenix): Can this return >0? If so, adjust usage.
LINUX_FUNCVI(cfg80211_remain_on_channel_expired)
LINUX_FUNCVI(cfg80211_unregister_wdev)
LINUX_FUNCVI(cfg80211_sched_scan_stopped)
LINUX_FUNCVI(cfg80211_rx_mgmt)
LINUX_FUNCVI(cfg80211_mgmt_tx_status)
LINUX_FUNCVI(cfg80211_check_combinations)
LINUX_FUNCVI(cfg80211_scan_done)
LINUX_FUNCVI(cfg80211_disconnected)
LINUX_FUNCVI(cfg80211_roamed)
LINUX_FUNCVI(cfg80211_connect_done)
LINUX_FUNCVV(cfg80211_inform_bss)
LINUX_FUNCVV(cfg80211_put_bss)
LINUX_FUNCVV(cfg80211_new_sta)
LINUX_FUNCVV(cfg80211_del_sta)
LINUX_FUNCVV(cfg80211_ibss_joined)
LINUX_FUNCVV(cfg80211_michael_mic_failure)
LINUX_FUNCII(ieee80211_channel_to_frequency)
LINUX_FUNCVV(ieee80211_get_channel)
LINUX_FUNCII(ieee80211_is_mgmt)
LINUX_FUNCII(ieee80211_is_action)
LINUX_FUNCII(ieee80211_is_probe_resp)
LINUX_FUNCVS(wiphy_register)
LINUX_FUNCVI(netif_rx)
LINUX_FUNCVI(netif_rx_ni)
LINUX_FUNCVI(netif_carrier_off)

LINUX_FUNCVI(seq_printf)
LINUX_FUNCVS(seq_write)
LINUX_FUNCVI(seq_puts)
LINUX_FUNCVI(dev_coredumpv)
// I can't find this defined (or even declared) anywhere in the Linux codebase.
//LINUX_FUNCVI(trace_brcmf_bcdchdr)

#define pci_write_config_dword(pdev, offset, value) \
    pci_config_write32(&pdev->pci_proto, offset, value)
#define pci_read_config_dword(pdev, offset, value) \
    pci_config_read32(&pdev->pci_proto, offset, value)
LINUX_FUNCcVI(pci_enable_msi)
LINUX_FUNCcVI(pci_disable_msi)
//LINUX_FUNCII(free_irq) // PCI & SDIO only
LINUX_FUNCII(request_threaded_irq) // PCI only
LINUX_FUNCVV(dma_alloc_coherent) // PCI only
LINUX_FUNCVV(dma_free_coherent) // PCI only
LINUX_FUNCVI(memcpy_fromio) // PCI only
LINUX_FUNCVS(memcpy_toio) // PCI only
LINUX_FUNCVV(dma_zalloc_coherent) // PCI only
LINUX_FUNCVI(dma_map_single) // PCI only
LINUX_FUNCVI(dma_mapping_error) // PCI only
LINUX_FUNCVI(dma_unmap_single) // PCI only

#define netdev_for_each_mc_addr(a, b) for (({brcmf_err("Calling netdev_for_each_mc_addr"); \
                                             a = (void*)0;});1;)
#define for_each_set_bit(a, b, c) for (({brcmf_err("Calling for_each_set_bit"); a = 0;});1;)

#define DEBUG                         // Turns on struct members that debug.c needs
#define CONFIG_OF                     // Turns on functions that of.c needs
#define CONFIG_BRCMFMAC_PROTO_MSGBUF  // turns on msgbuf.h
#define CONFIG_BRCMFMAC_PROTO_BCDC    // Needed to see func defs in bcdc.h

#define KBUILD_MODNAME "brcmfmac"
#define BRCMFMAC_PDATA_NAME ("pdata name")
enum {
    IEEE80211_P2P_ATTR_DEVICE_INFO = 2,
    IEEE80211_P2P_ATTR_DEVICE_ID = 3,
    IEEE80211_STYPE_ACTION = 0,
    IEEE80211_FCTL_STYPE = 0,
    IEEE80211_P2P_ATTR_GROUP_ID = 0,
    IEEE80211_STYPE_PROBE_REQ = 0,
    IEEE80211_P2P_ATTR_LISTEN_CHANNEL = (57),
    SIGTERM = (55),
    TASK_INTERRUPTIBLE = (0),
    TASK_RUNNING = (1),
    IFNAMSIZ = (16),
    WLAN_PMKID_LEN = (16),
    WLAN_MAX_KEY_LEN = (128),
    IEEE80211_MAX_SSID_LEN = (32),
    IRQF_SHARED,
    IEEE80211_RATE_SHORT_PREAMBLE,
    WLAN_CIPHER_SUITE_AES_CMAC,
    WLAN_CIPHER_SUITE_CCMP,
    WLAN_CIPHER_SUITE_TKIP,
    WLAN_CIPHER_SUITE_WEP40,
    WLAN_CIPHER_SUITE_WEP104,
    WLAN_EID_VENDOR_SPECIFIC,
    WIPHY_PARAM_RETRY_SHORT,
    WIPHY_PARAM_RTS_THRESHOLD,
    WIPHY_PARAM_FRAG_THRESHOLD,
    WIPHY_PARAM_RETRY_LONG,
    WLAN_REASON_DEAUTH_LEAVING,
    WLAN_REASON_UNSPECIFIED,
    NL80211_WPA_VERSION_1,
    NL80211_WPA_VERSION_2,
    NL80211_AUTHTYPE_OPEN_SYSTEM,
    NL80211_AUTHTYPE_SHARED_KEY,
    WLAN_EID_RSN,
    WLAN_EID_TIM,
    WLAN_EID_COUNTRY,
    WLAN_EID_SSID,
    NL80211_AUTHTYPE_AUTOMATIC,
    WLAN_AKM_SUITE_PSK,
    WLAN_AKM_SUITE_8021X,
    WLAN_AKM_SUITE_8021X_SHA256,
    WLAN_AKM_SUITE_PSK_SHA256,
    NL80211_BSS_SELECT_ATTR_BAND_PREF = 0,
    __NL80211_BSS_SELECT_ATTR_INVALID,
    NL80211_BSS_SELECT_ATTR_RSSI_ADJUST,
    NL80211_BSS_SELECT_ATTR_RSSI,
    NL80211_STA_INFO_STA_FLAGS,
    NL80211_STA_FLAG_WME = 0,
    NL80211_STA_FLAG_ASSOCIATED,
    NL80211_STA_FLAG_AUTHENTICATED,
    NL80211_STA_FLAG_AUTHORIZED,
    NL80211_STA_INFO_BSS_PARAM,
    NL80211_STA_INFO_CONNECTED_TIME,
    NL80211_STA_INFO_RX_BITRATE,
    NL80211_STA_INFO_TX_BYTES,
    NL80211_STA_INFO_RX_BYTES,
    NL80211_STA_INFO_CHAIN_SIGNAL,
    IEEE80211_HT_STBC_PARAM_DUAL_CTS_PROT,
    BSS_PARAM_FLAGS_CTS_PROT,
    BSS_PARAM_FLAGS_SHORT_PREAMBLE,
    WLAN_CAPABILITY_SHORT_SLOT_TIME,
    BSS_PARAM_FLAGS_SHORT_SLOT_TIME,
    IEEE80211_CHAN_RADAR,
    IEEE80211_CHAN_NO_IR,
    IEEE80211_CHAN_NO_HT40,
    IEEE80211_CHAN_NO_HT40PLUS,
    IEEE80211_CHAN_DISABLED,
    IEEE80211_CHAN_NO_HT40MINUS,
    IEEE80211_CHAN_NO_80MHZ,
    NL80211_STA_INFO_TX_BITRATE,
    NL80211_STA_INFO_SIGNAL,
    NL80211_STA_INFO_TX_PACKETS,
    NL80211_STA_INFO_RX_DROP_MISC,
    NL80211_STA_INFO_TX_FAILED,
    NL80211_STA_INFO_RX_PACKETS,
    WLAN_CAPABILITY_SHORT_PREAMBLE,
    NL80211_STA_FLAG_TDLS_PEER,
    NL80211_STA_INFO_INACTIVE_TIME,
    CFG80211_BSS_FTYPE_UNKNOWN,
    WLAN_CAPABILITY_IBSS,
    UPDATE_ASSOC_IES,
    WLAN_STATUS_SUCCESS,
    WLAN_STATUS_AUTH_TIMEOUT,
    IEEE80211_HT_CAP_SGI_40,
    IEEE80211_HT_CAP_SUP_WIDTH_20_40,
    IEEE80211_HT_CAP_DSSSCCK40,
    IEEE80211_HT_MAX_AMPDU_64K,
    IEEE80211_HT_MPDU_DENSITY_16,
    IEEE80211_HT_MCS_TX_DEFINED,
    IEEE80211_HT_CAP_SGI_20,
    IEEE80211_VHT_CAP_SUPP_CHAN_WIDTH_160MHZ,
    IEEE80211_VHT_CAP_SHORT_GI_160,
    IEEE80211_VHT_MCS_SUPPORT_0_9,
    IEEE80211_VHT_CAP_SHORT_GI_80,
    IEEE80211_VHT_CAP_SU_BEAMFORMEE_CAPABLE,
    IEEE80211_VHT_CAP_SU_BEAMFORMER_CAPABLE,
    IEEE80211_VHT_CAP_BEAMFORMEE_STS_SHIFT = 0,
    IEEE80211_VHT_CAP_SOUNDING_DIMENSIONS_SHIFT,
    IEEE80211_VHT_CAP_MU_BEAMFORMEE_CAPABLE,
    IEEE80211_VHT_CAP_MU_BEAMFORMER_CAPABLE,
    IEEE80211_VHT_CAP_VHT_LINK_ADAPTATION_VHT_MRQ_MFB,
    IEEE80211_STYPE_ASSOC_REQ,
    IEEE80211_STYPE_REASSOC_REQ,
    IEEE80211_STYPE_DISASSOC,
    IEEE80211_STYPE_AUTH,
    IEEE80211_STYPE_DEAUTH,
    CFG80211_SIGNAL_TYPE_MBM,
    WIPHY_FLAG_PS_ON_BY_DEFAULT,
    WIPHY_FLAG_HAS_REMAIN_ON_CHANNEL,
    WIPHY_FLAG_SUPPORTS_TDLS,
    WIPHY_FLAG_SUPPORTS_FW_ROAM,
    WIPHY_FLAG_NETNS_OK,
    WIPHY_FLAG_OFFCHAN_TX,
    REGULATORY_CUSTOM_REG,
    NL80211_FEATURE_SCHED_SCAN_RANDOM_MAC_ADDR,
    IFF_ALLMULTI = 0,
    NET_NETBUF_PAD,
    IFF_PROMISC,
    NETDEV_TX_OK,
    IFF_UP,
    NETIF_F_IP_CSUM,
    NETREG_REGISTERED,
    CHECKSUM_PARTIAL,
    CHECKSUM_UNNECESSARY,
    BRCMF_H2D_TXFLOWRING_MAX_ITEM,
    BRCMF_H2D_TXFLOWRING_ITEMSIZE,
    NL80211_SCAN_FLAG_RANDOM_ADDR,
    WLAN_AUTH_OPEN,
    IRQF_TRIGGER_HIGH,
    SDIO_CCCR_IENx,
    SSB_IMSTATE_BUSY,
    SSB_IDLOW_INITIATOR,
    SSB_TMSHIGH_SERR,
    SSB_IMSTATE_IBE,
    SSB_IMSTATE_TO,
    BCMA_CC_CAP_EXT_AOB_PRESENT,
    SSB_TMSLOW_FGC,
    MMC_RSP_SPI_R5,
    MMC_RSP_R5,
    MMC_CMD_ADTC,
    WIPHY_VENDOR_CMD_NEED_WDEV,
    WIPHY_VENDOR_CMD_NEED_NETDEV,
    SDIO_CCCR_ABORT,
    SDIO_IO_RW_EXTENDED,
    MMC_DATA_READ,
    MMC_DATA_WRITE,
    BRCMF_SCAN_IE_LEN_MAX,
    SD_IO_RW_EXTENDED,
    MMC_CAP_NONREMOVABLE,
    MMC_QUIRK_LENIENT_FN0,
    DUMP_PREFIX_OFFSET,
};

typedef enum { IRQ_WAKE_THREAD, IRQ_NONE, IRQ_HANDLED } irqreturn_t;

enum ieee80211_vht_mcs_support { FOOVMS };

enum dma_data_direction {
    DMA_TO_DEVICE,
    DMA_FROM_DEVICE,
};

enum nl80211_tx_power_setting {
    NL80211_TX_POWER_AUTOMATIC,
    NL80211_TX_POWER_LIMITED,
    NL80211_TX_POWER_FIXED,

};

enum nl80211_key_type {
    NL80211_KEYTYPE_GROUP,
    NL80211_KEYTYPE_PAIRWISE,
};
enum nl80211_chan_width {
    NL80211_CHAN_WIDTH_20,
    NL80211_CHAN_WIDTH_20_NOHT,
    NL80211_CHAN_WIDTH_40,
    NL80211_CHAN_WIDTH_80,
    NL80211_CHAN_WIDTH_80P80,
    NL80211_CHAN_WIDTH_160,
    NL80211_CHAN_WIDTH_5,
    NL80211_CHAN_WIDTH_10,
};

enum nl80211_auth_type { FOONLAT };

enum nl80211_crit_proto_id {
    NL80211_CRIT_PROTO_DHCP,
};

enum nl80211_tdls_operation {
    NL80211_TDLS_DISCOVERY_REQ,
    NL80211_TDLS_SETUP,
    NL80211_TDLS_TEARDOWN,
};

enum nl80211_band {
    NL80211_BAND_2GHZ,
    NL80211_BAND_5GHZ,
    NL80211_BAND_60GHZ,
};

#define CONFIG_BRCMDBG 0
#define CONFIG_BRCM_TRACING 0

enum brcmf_bus_type { BRCMF_BUSTYPE_SDIO, BRCMF_BUSTYPE_USB, BRCMF_BUSTYPE_PCIE };

#define TP_PROTO(args...) args
#define MODULE_FIRMWARE(a)
#define MODULE_AUTHOR(a)
#define MODULE_DESCRIPTION(a)
#define MODULE_LICENSE(a)
#define module_param_named(a, b, c, d)
#define MODULE_PARM_DESC(a, b)
#define MODULE_SUPPORTED_DEVICE(a)

#define __iomem            // May want it later
#define IS_ENABLED(a) (a)  // not in compiler.h
#define HZ (60)

struct brcmfmac_pd_cc_entry {
    uint8_t* iso3166;
    uint32_t rev;
    uint8_t* cc;
};

struct brcmfmac_pd_cc {
    int table_size;
    struct brcmfmac_pd_cc_entry* table;
};

struct net_device_ops { // Probably all return zx_status_t
    void* ndo_open;
    void* ndo_stop;
    void* ndo_start_xmit;
    void* ndo_set_mac_address;
    void* ndo_set_rx_mode;
};

struct ethtool_ops {
    void* get_drvinfo;
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

struct ieee80211_rate {
    int bitrate;
    uint32_t flags;
    uint32_t hw_value;
};

struct ieee80211_supported_band {
    int band;
    struct ieee80211_rate* bitrates;
    int n_bitrates;
    struct ieee80211_channel* channels;
    uint32_t n_channels;
    struct {
        int ht_supported;
        uint16_t cap;
        int ampdu_factor;
        int ampdu_density;
        struct {
            uint8_t rx_mask[32]; // At most 32 bytes are set; it's never read in this driver.
            uint32_t tx_params;
        } mcs;
    } ht_cap;
    struct {
        int vht_supported;
        uint32_t cap;
        struct {
            uint16_t rx_mcs_map;
            uint16_t tx_mcs_map;
        } vht_mcs;
    } vht_cap;
};

struct mac_address {
    uint8_t addr[ETH_ALEN];
};

struct regulatory_request {
    char alpha2[44];
    int initiator;
};

struct wiphy {
    int max_sched_scan_reqs;
    int max_sched_scan_plan_interval;
    int max_sched_scan_ie_len;
    int max_match_sets;
    int max_sched_scan_ssids;
    uint32_t rts_threshold;
    uint32_t frag_threshold;
    uint32_t retry_long;
    uint32_t retry_short;
    uint32_t interface_modes;
    struct ieee80211_supported_band* bands[5];
    int n_iface_combinations;
    struct ieee80211_iface_combination* iface_combinations;
    uint32_t max_scan_ssids;
    uint32_t max_scan_ie_len;
    uint32_t max_num_pmkids;
    struct mac_address* addresses;
    uint32_t n_addresses;
    uint32_t signal_type;
    const uint32_t* cipher_suites;
    uint32_t n_cipher_suites;
    uint32_t bss_select_support;
    uint32_t flags;
    const struct ieee80211_txrx_stypes* mgmt_stypes;
    uint32_t max_remain_on_channel_duration;
    uint32_t n_vendor_commands;
    const struct wiphy_vendor_command* vendor_commands;
    uint8_t perm_addr[ETH_ALEN];
    void (*reg_notifier)(struct wiphy*, struct regulatory_request*);
    uint32_t regulatory_flags;
    uint32_t features;
    struct brcmf_cfg80211_info* cfg80211_info;
    struct cfg80211_ops* ops;
    struct brcmf_device* dev;
};

struct vif_params {
    uint8_t macaddr[ETH_ALEN];
};

struct wireless_dev {
    struct net_device* netdev;
    int iftype;
    uint8_t address[ETH_ALEN];
    struct wiphy* wiphy;
    struct brcmf_cfg80211_info* cfg80211_info;
};

struct cfg80211_ssid {
    size_t ssid_len;
    char* ssid;
};

struct cfg80211_scan_request {
    int n_ssids;
    int n_channels;
    uint8_t* ie;
    struct wireless_dev* wdev;
    int ie_len;
    struct ieee80211_channel* channels[555];
    struct cfg80211_ssid* ssids;
    struct wiphy* wiphy;
};

enum nl80211_iftype {
    NL80211_IFTYPE_P2P_GO,
    NL80211_IFTYPE_P2P_CLIENT,
    NL80211_IFTYPE_P2P_DEVICE,
    NL80211_IFTYPE_AP,
    NL80211_IFTYPE_ADHOC,
    NL80211_IFTYPE_STATION,
    NL80211_IFTYPE_AP_VLAN,
    NL80211_IFTYPE_WDS,
    NL80211_IFTYPE_MONITOR,
    NL80211_IFTYPE_MESH_POINT,
    NL80211_IFTYPE_UNSPECIFIED,
    NUM_NL80211_IFTYPES,
};

struct ieee80211_mgmt {
    int u;
    uint8_t bssid[ETH_ALEN];
    uint8_t da[ETH_ALEN];
    uint8_t sa[ETH_ALEN];
    uint16_t frame_control;
};

struct notifier_block {
    int foo;
};

struct in6_addr {
    int foo;
};

struct seq_file {
    void* private;
};

typedef uint64_t dma_addr_t;

struct pci_device_id {
    int a, b, c, d, e, f, g;
};

struct ieee80211_regdomain {
    int n_reg_rules;
    char* alpha2;
    struct {
        struct {
            int start_freq_khz;
            int end_freq_khz;
            int max_bandwidth_khz;
        } freq_range;
        struct {
            int max_antenna_gain;
            int max_eirp;
        } power_rule;
        uint32_t flags;
        uint32_t dfs_cac_ms;
    } reg_rules[];
};
#define REG_RULE(...) \
    { .flags = 0 }  // Fill up reg_rules

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

struct wiphy_vendor_command {
    struct {
        int vendor_id;
        int subcmd;
    } unknown_name;
    uint32_t flags;
    zx_status_t (*doit)(struct wiphy* wiphy, struct wireless_dev* wdev, const void* data, int len);
};

struct cfg80211_chan_def {
    struct ieee80211_channel* chan;
    int center_freq1;
    int center_freq2;
    int width;
};

struct iface_combination_params {
    int num_different_channels;
    int iftype_num[555];
};

struct cfg80211_scan_info {
    int aborted;
};

struct cfg80211_ibss_params {
    char* ssid;
    int privacy;
    int beacon_interval;
    int ssid_len;
    uint8_t* bssid;
    int channel_fixed;
    struct cfg80211_chan_def chandef;
    uint8_t* ie;
    int ie_len;
    int basic_rates;
};

struct cfg80211_bss_selection {
    int behaviour;
    struct {
        int band_pref;
        struct {
            int band;
            int delta;
        } adjust;
    } param;
};

struct cfg80211_connect_params {
    struct {
        int wpa_versions;
        int ciphers_pairwise[555];
        int n_ciphers_pairwise;
        int cipher_group;
        int n_akm_suites;
        int akm_suites[555];
        uint8_t* psk;
    } crypto;
    int auth_type;
    uint8_t* ie;
    int ie_len;
    int privacy;
    uint32_t key_len;
    int key_idx;
    void* key;
    int want_1x;
    struct ieee80211_channel* channel;
    uint8_t* ssid;
    int ssid_len;
    uint8_t* bssid;
    struct cfg80211_bss_selection bss_select;
};

struct key_params {
    uint32_t key_len;
    int cipher;
    void* key;
};

struct nl80211_sta_flag_update {
    int mask;
    int set;
};

struct station_info {
    unsigned long filled;
    struct nl80211_sta_flag_update sta_flags;
    struct {
        uint32_t flags;
        uint32_t dtim_period;
        uint32_t beacon_interval;
    } bss_param;
    struct {
        uint32_t legacy;
    } txrate;
    struct {
        uint32_t legacy;
    } rxrate;
    uint32_t signal;
    uint32_t rx_packets;
    uint32_t rx_dropped_misc;
    uint32_t tx_packets;
    uint32_t tx_failed;
    uint32_t inactive_time;
    uint32_t connected_time;
    uint32_t tx_bytes;
    uint32_t rx_bytes;
    uint32_t chain_signal_avg[555];
    uint32_t chain_signal[555];
    uint32_t chains;
    void* assoc_req_ies;
    uint32_t assoc_req_ies_len;
    uint32_t generation;
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

struct cfg80211_pmksa {
    uint8_t bssid[ETH_ALEN];
    uint8_t* pmkid;
};

struct cfg80211_beacon_data {
    void* tail;
    int tail_len;
    void* head;
    int head_len;
    void* proberesp_ies;
    int proberesp_ies_len;
};

struct cfg80211_ap_settings {
    struct cfg80211_chan_def chandef;
    int beacon_interval;
    int dtim_period;
    void* ssid;
    size_t ssid_len;
    int auth_type;
    int inactivity_timeout;
    struct cfg80211_beacon_data beacon;
    int hidden_ssid;
};

struct station_del_parameters {
    uint8_t* mac;
    int reason_code;
};

struct station_parameters {
    uint32_t sta_flags_mask;
    uint32_t sta_flags_set;
};

struct cfg80211_mgmt_tx_params {
    struct ieee80211_channel* chan;
    uint8_t* buf;
    size_t len;
};

struct cfg80211_pmk_conf {
    void* pmk;
    int pmk_len;
};

struct cfg80211_ops { // Most of these return zx_status_t
    void* add_virtual_intf;
    void* del_virtual_intf;
    void* change_virtual_intf;
    void* scan;
    void* set_wiphy_params;
    void* join_ibss;
    void* leave_ibss;
    void* get_station;
    void* dump_station;
    void* set_tx_power;
    void* get_tx_power;
    void* add_key;
    void* del_key;
    void* get_key;
    void* set_default_key;
    void* set_default_mgmt_key;
    void* set_power_mgmt;
    void* connect;
    void* disconnect;
    void* suspend;
    void* resume;
    void* set_pmksa;
    void* del_pmksa;
    void* flush_pmksa;
    void* start_ap;
    void* stop_ap;
    void* change_beacon;
    void* del_station;
    void* change_station;
    void* sched_scan_start;
    void* sched_scan_stop;
    void* mgmt_frame_register;
    void* mgmt_tx;
    void* remain_on_channel;
    void* cancel_remain_on_channel;
    void* get_channel;
    void* start_p2p_device;
    void* stop_p2p_device;
    void* crit_proto_start;
    void* crit_proto_stop;
    void* tdls_oper;
    void* update_connect_params;
    void* set_pmk;
    void* del_pmk;
};

struct cfg80211_roam_info {
    struct ieee80211_channel* channel;
    uint8_t* bssid;
    void* req_ie;
    int req_ie_len;
    void* resp_ie;
    int resp_ie_len;
};

struct cfg80211_connect_resp_params {
    int status;
    uint8_t* bssid;
    void* req_ie;
    int req_ie_len;
    void* resp_ie;
    int resp_ie_len;
};

struct ieee80211_iface_combination {
    int num_different_channels;
    struct ieee80211_iface_limit* limits;
    int max_interfaces;
    int beacon_int_infra_match;
    int n_limits;
};

struct ieee80211_txrx_stypes {
    uint32_t tx;
    uint32_t rx;
};

struct ieee80211_iface_limit {
    int max;
    int types;
};

struct netdev_hw_addr {
    uint8_t addr[ETH_ALEN];
};

struct ethtool_drvinfo {
    void* driver;
    void* version;
    void* fw_version;
    void* bus_info;
};

struct va_format {
    va_list* va;
    const char* fmt;
};

#endif  // GARNET_DRIVERS_WLAN_THIRD_PARTY_BROADCOM_INCLUDE_LINUXISMS_H_
