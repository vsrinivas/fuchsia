/** @file mlan_fw.h
 *
 *  @brief This file contains firmware specific defines.
 *  structures and declares global function prototypes used
 *  in MLAN module.
 *
 *
 *  Copyright 2008-2021 NXP
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are met:
 *
 *  1. Redistributions of source code must retain the above copyright notice,
 *  this list of conditions and the following disclaimer.
 *
 *  2. Redistributions in binary form must reproduce the above copyright notice,
 *  this list of conditions and the following disclaimer in the documentation
 *  and/or other materials provided with the distribution.
 *
 *  3. Neither the name of the copyright holder nor the names of its
 *  contributors may be used to endorse or promote products derived from this
 *  software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS ASIS AND
 *  ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 *  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 */

/******************************************************
Change log:
    10/27/2008: initial version
******************************************************/

#ifndef _MLAN_FW_H_
#define _MLAN_FW_H_

/** Interface header length */
#ifdef USB
#define USB_INTF_HEADER_LEN 0
#endif /* USB */
#ifdef SDIO
#define SDIO_INTF_HEADER_LEN 4
#endif /* SDIO */
#ifdef PCIE
#define PCIE_INTF_HEADER_LEN 4
#endif /* PCIE */

#ifdef PRAGMA_PACK
#pragma pack(push, 1)
#endif

#define WPA_GCMP_KEY_LEN 32

#define WPA_CCMP_256_KEY_LEN 32

/** Ethernet header */
typedef MLAN_PACK_START struct {
	/** Ethernet header destination address */
	t_u8 dest_addr[MLAN_MAC_ADDR_LENGTH];
	/** Ethernet header source address */
	t_u8 src_addr[MLAN_MAC_ADDR_LENGTH];
	/** Ethernet header length */
	t_u16 h803_len;

} MLAN_PACK_END Eth803Hdr_t;

/** RFC 1042 header */
typedef MLAN_PACK_START struct {
	/** LLC DSAP */
	t_u8 llc_dsap;
	/** LLC SSAP */
	t_u8 llc_ssap;
	/** LLC CTRL */
	t_u8 llc_ctrl;
	/** SNAP OUI */
	t_u8 snap_oui[3];
	/** SNAP type */
	t_u16 snap_type;

} MLAN_PACK_END Rfc1042Hdr_t;

/** Rx packet header */
typedef MLAN_PACK_START struct {
	/** Etherner header */
	Eth803Hdr_t eth803_hdr;
	/** RFC 1042 header */
	Rfc1042Hdr_t rfc1042_hdr;

} MLAN_PACK_END RxPacketHdr_t;

/** Rates supported in band B */
#define B_SUPPORTED_RATES 5
/** Rates supported in band G */
#define G_SUPPORTED_RATES 9
/** Rates supported in band BG */
#define BG_SUPPORTED_RATES 13

/** Setup the number of rates passed in the driver/firmware API */
#define A_SUPPORTED_RATES 9

/** CapInfo Short Slot Time Disabled */
/* #define SHORT_SLOT_TIME_DISABLED(CapInfo)
 * ((IEEEtypes_CapInfo_t)(CapInfo).short_slot_time = 0) */
#define SHORT_SLOT_TIME_DISABLED(CapInfo) (CapInfo &= ~MBIT(10))
/** CapInfo Short Slot Time Enabled */
#define SHORT_SLOT_TIME_ENABLED(CapInfo) (CapInfo |= MBIT(10))
/** CapInfo Spectrum Mgmt Disabled */
#define SPECTRUM_MGMT_DISABLED(CapInfo) (CapInfo &= ~MBIT(8))
/** CapInfo Spectrum Mgmt Enabled */
#define SPECTRUM_MGMT_ENABLED(CapInfo) (CapInfo |= MBIT(8))
/** CapInfo Radio Measurement Disabled */
#define RADIO_MEASUREMENT_DISABLED(CapInfo) (CapInfo &= ~MBIT(12))
/** CapInfo Radio Measurement Enabled */
#define RADIO_MEASUREMENT_ENABLED(CapInfo) (CapInfo |= MBIT(12))

/** Setup the number of rates passed in the driver/firmware API */
#define HOSTCMD_SUPPORTED_RATES 14

/** Rates supported in band N */
#define N_SUPPORTED_RATES 3
#ifdef STA_SUPPORT
/** All bands (B, G, N, AAC, GAC) */
#define ALL_802_11_BANDS                                                       \
	(BAND_A | BAND_B | BAND_G | BAND_GN | BAND_AAC | BAND_GAC)
#else
/** All bands (B, G, A) */
#define ALL_802_11_BANDS (BAND_B | BAND_G | BAND_A)
#endif /* STA_SUPPORT */

#ifdef STA_SUPPORT
/** Firmware multiple bands support */
#define FW_MULTI_BANDS_SUPPORT                                                 \
	(MBIT(8) | MBIT(9) | MBIT(10) | MBIT(11) | MBIT(12) | MBIT(13))
#else
/** Firmware multiple bands support */
#define FW_MULTI_BANDS_SUPPORT (MBIT(8) | MBIT(9) | MBIT(10))
#endif /* STA_SUPPORT */
/** Check if multiple bands support is enabled in firmware */
#define IS_SUPPORT_MULTI_BANDS(_adapter)                                       \
	(_adapter->fw_cap_info & FW_MULTI_BANDS_SUPPORT)
/** Get default bands of the firmware */
/* need to shift bit 12 and bit 13 in fw_cap_info from the firmware
 * to bit 13 and 14 for 11ac so that bit 11 is for GN, bit 12 for AN,
 * bit 13 for GAC, and bit 14 for AAC, in order to be compatible with
 * the band capability defined in the driver after right shift of 8 bits */
#define GET_FW_DEFAULT_BANDS(_adapter)                                         \
	(((((_adapter->fw_cap_info & 0x3000) << 1) |                           \
	   (_adapter->fw_cap_info & ~0xF000)) >>                               \
	  8) &                                                                 \
	 ALL_802_11_BANDS)

extern t_u8 SupportedRates_B[B_SUPPORTED_RATES];
extern t_u8 SupportedRates_G[G_SUPPORTED_RATES];
extern t_u8 SupportedRates_BG[BG_SUPPORTED_RATES];
extern t_u8 SupportedRates_A[A_SUPPORTED_RATES];
extern t_u8 SupportedRates_N[N_SUPPORTED_RATES];
extern t_u8 AdhocRates_G[G_SUPPORTED_RATES];
extern t_u8 AdhocRates_B[B_SUPPORTED_RATES];
extern t_u8 AdhocRates_BG[BG_SUPPORTED_RATES];
extern t_u8 AdhocRates_A[A_SUPPORTED_RATES];

/** Default auto deep sleep mode */
#define DEFAULT_AUTO_DS_MODE MTRUE
/** Default power save mode */
#define DEFAULT_PS_MODE Wlan802_11PowerModePSP

/** WEP Key index mask */
#define HostCmd_WEP_KEY_INDEX_MASK 0x3fff
/** Length of WEP 40 bit key */
#define WEP_40_BIT_LEN 5
/** Length of WEP 104 bit key */
#define WEP_104_BIT_LEN 13

/** Key information enabled */
#define KEY_INFO_ENABLED 0x01
/** KEY_TYPE_ID */
typedef enum _KEY_TYPE_ID {
	/** Key type : WEP */
	KEY_TYPE_ID_WEP = 0,
	/** Key type : TKIP */
	KEY_TYPE_ID_TKIP = 1,
	/** Key type : AES */
	KEY_TYPE_ID_AES = 2,
	KEY_TYPE_ID_WAPI = 3,
	KEY_TYPE_ID_AES_CMAC = 4,
	/** Key type : GCMP */
	KEY_TYPE_ID_GCMP = 5,
	/** Key type : GCMP_256 */
	KEY_TYPE_ID_GCMP_256 = 6,
	/** Key type : CCMP_256 */
	KEY_TYPE_ID_CCMP_256 = 7,
	/** Key type : GMAC_128 */
	KEY_TYPE_ID_BIP_GMAC_128 = 8,
	/** Key type : GMAC_256 */
	KEY_TYPE_ID_BIP_GMAC_256 = 9,
} KEY_TYPE_ID;

/** Key Info flag for multicast key */
#define KEY_INFO_MCAST_KEY 0x01
/** Key Info flag for unicast key */
#define KEY_INFO_UCAST_KEY 0x02

/** KEY_INFO_WEP*/
typedef enum _KEY_INFO_WEP {
	KEY_INFO_WEP_MCAST = 0x01,
	KEY_INFO_WEP_UNICAST = 0x02,
	KEY_INFO_WEP_ENABLED = 0x04
} KEY_INFO_WEP;

/** KEY_INFO_TKIP */
typedef enum _KEY_INFO_TKIP {
	KEY_INFO_TKIP_MCAST = 0x01,
	KEY_INFO_TKIP_UNICAST = 0x02,
	KEY_INFO_TKIP_ENABLED = 0x04
} KEY_INFO_TKIP;

/** KEY_INFO_AES*/
typedef enum _KEY_INFO_AES {
	KEY_INFO_AES_MCAST = 0x01,
	KEY_INFO_AES_UNICAST = 0x02,
	KEY_INFO_AES_ENABLED = 0x04,
	KEY_INFO_AES_MCAST_IGTK = 0x400,
} KEY_INFO_AES;

/** WPA AES key length */
#define WPA_AES_KEY_LEN 16
/** WPA TKIP key length */
#define WPA_TKIP_KEY_LEN 32
/** WPA AES IGTK key length */
#define CMAC_AES_KEY_LEN 16
/** IGTK key length */
#define WPA_IGTK_KEY_LEN 16
#define WPA_IGTK_256_KEY_LEN 32

/** WAPI key length */
#define WAPI_KEY_LEN 50
/** KEY_INFO_WAPI*/
typedef enum _KEY_INFO_WAPI {
	KEY_INFO_WAPI_MCAST = 0x01,
	KEY_INFO_WAPI_UNICAST = 0x02,
	KEY_INFO_WAPI_ENABLED = 0x04
} KEY_INFO_WAPI;

/** Maximum ethernet frame length sans FCS */
#define MV_ETH_FRAME_LEN 1514

#if defined(SDIO) || defined(PCIE)
/** Length of SNAP header */
#define MRVDRV_SNAP_HEADER_LEN 8

/** The number of times to try when polling for status bits */
#define MAX_POLL_TRIES 100

/** The number of times to try when waiting for downloaded firmware to
     become active when multiple interface is present */
#define MAX_MULTI_INTERFACE_POLL_TRIES 150
/** The number of times to try when waiting for downloaded firmware to
     become active. (polling the scratch register). */
#define MAX_FIRMWARE_POLL_TRIES 100

/** FW fill in rx_len with extra 204 bytes */
#define EXTRA_LEN 256

/** Buffer size for ethernet Tx packets */
#define MRVDRV_ETH_TX_PACKET_BUFFER_SIZE                                       \
	(MV_ETH_FRAME_LEN + sizeof(TxPD) + EXTRA_LEN)

/** Buffer size for ethernet Rx packets */
#define MRVDRV_ETH_RX_PACKET_BUFFER_SIZE                                       \
	(MV_ETH_FRAME_LEN + sizeof(RxPD) + MRVDRV_SNAP_HEADER_LEN + EXTRA_LEN)
#endif /* SDIO || PCIE */

#ifdef SDIO
/* Macros in interface module */
/** Firmware ready */
#define SDIO_FIRMWARE_READY 0xfedc
#endif /* SDIO */

#ifdef PCIE
/* Macros in interface module */
/** Firmware ready */
#define PCIE_FIRMWARE_READY 0xfedcba00
#endif

/** Enumeration definition*/
/** WLAN_802_11_PRIVACY_FILTER */
typedef enum _WLAN_802_11_PRIVACY_FILTER {
	Wlan802_11PrivFilterAcceptAll,
	Wlan802_11PrivFilter8021xWEP
} WLAN_802_11_PRIVACY_FILTER;

/** WLAN_802_11_WEP_STATUS */
typedef enum _WLAN_802_11_WEP_STATUS {
	Wlan802_11WEPEnabled,
	Wlan802_11WEPDisabled,
	Wlan802_11WEPKeyAbsent,
	Wlan802_11WEPNotSupported
} WLAN_802_11_WEP_STATUS;

/** SNR calculation */
#define CAL_SNR(RSSI, NF) ((t_s16)((t_s16)(RSSI) - (t_s16)(NF)))

/** 2K buf size */
#define MLAN_TX_DATA_BUF_SIZE_2K 2048

/** Terminating TLV Type */
#define MRVL_TERMINATE_TLV_ID 0xffff

/** TLV type : SSID */
#define TLV_TYPE_SSID 0x0000
/** TLV type : Rates */
#define TLV_TYPE_RATES 0x0001
/** TLV type : PHY FH */
#define TLV_TYPE_PHY_FH 0x0002
/** TLV type : PHY DS */
#define TLV_TYPE_PHY_DS 0x0003
/** TLV type : CF */
#define TLV_TYPE_CF 0x0004
/** TLV type : IBSS */
#define TLV_TYPE_IBSS 0x0006

/** TLV type : Domain */
#define TLV_TYPE_DOMAIN 0x0007

/** TLV type : Power constraint */
#define TLV_TYPE_POWER_CONSTRAINT 0x0020

/** TLV type : Power capability */
#define TLV_TYPE_POWER_CAPABILITY 0x0021

#define TLV_TYPE_HT_CAPABILITY 0x002d

#define TLV_TYPE_EXTENSION_ID 0x00ff

/**TLV type : Host MLME Flag*/
#define TLV_TYPE_HOST_MLME (PROPRIETARY_TLV_BASE_ID + 307)

/** TLV type : AP wacp mode */
#define TLV_TYPE_UAP_WACP_MODE (PROPRIETARY_TLV_BASE_ID + 0x147)	/* 0x0247 */

/** TLV type : Vendor Specific IE */
#define TLV_TYPE_VENDOR_SPECIFIC_IE 0x00dd

/** TLV type : Key material */
#define TLV_TYPE_KEY_MATERIAL (PROPRIETARY_TLV_BASE_ID + 0x00)	/* 0x0100 */
/** TLV type : Channel list */
#define TLV_TYPE_CHANLIST (PROPRIETARY_TLV_BASE_ID + 0x01)	/* 0x0101 */
/** TLV type : Number of probes */
#define TLV_TYPE_NUMPROBES (PROPRIETARY_TLV_BASE_ID + 0x02)	/* 0x0102 */
/** TLV type : Beacon RSSI low */
#define TLV_TYPE_RSSI_LOW (PROPRIETARY_TLV_BASE_ID + 0x04)	/* 0x0104 */
/** TLV type : Beacon SNR low */
#define TLV_TYPE_SNR_LOW (PROPRIETARY_TLV_BASE_ID + 0x05)	/* 0x0105 */
/** TLV type : Fail count */
#define TLV_TYPE_FAILCOUNT (PROPRIETARY_TLV_BASE_ID + 0x06)	/* 0x0106 */
/** TLV type : BCN miss */
#define TLV_TYPE_BCNMISS (PROPRIETARY_TLV_BASE_ID + 0x07)	/* 0x0107 */
/** TLV type : LED behavior */
#define TLV_TYPE_LEDBEHAVIOR (PROPRIETARY_TLV_BASE_ID + 0x09)	/* 0x0109 */
/** TLV type : Passthrough */
#define TLV_TYPE_PASSTHROUGH (PROPRIETARY_TLV_BASE_ID + 0x0a)	/* 0x010a */
/** TLV type : Power TBL 2.4 Ghz */
#define TLV_TYPE_POWER_TBL_2_4GHZ (PROPRIETARY_TLV_BASE_ID + 0x0c)	/* 0x010c   \
									 */
/** TLV type : Power TBL 5 GHz */
#define TLV_TYPE_POWER_TBL_5GHZ (PROPRIETARY_TLV_BASE_ID + 0x0d)	/* 0x010d */
/** TLV type : WMM queue status */
#define TLV_TYPE_WMMQSTATUS (PROPRIETARY_TLV_BASE_ID + 0x10)	/* 0x0110 */
/** TLV type : Wildcard SSID */
#define TLV_TYPE_WILDCARDSSID (PROPRIETARY_TLV_BASE_ID + 0x12)	/* 0x0112 */
/** TLV type : TSF timestamp */
#define TLV_TYPE_TSFTIMESTAMP (PROPRIETARY_TLV_BASE_ID + 0x13)	/* 0x0113 */
/** TLV type : ARP filter */
#define TLV_TYPE_ARP_FILTER (PROPRIETARY_TLV_BASE_ID + 0x15)	/* 0x0115 */
/** TLV type : Beacon RSSI high */
#define TLV_TYPE_RSSI_HIGH (PROPRIETARY_TLV_BASE_ID + 0x16)	/* 0x0116 */
/** TLV type : Beacon SNR high */
#define TLV_TYPE_SNR_HIGH (PROPRIETARY_TLV_BASE_ID + 0x17)	/* 0x0117 */
/** TLV type : Start BG scan later */
#define TLV_TYPE_STARTBGSCANLATER (PROPRIETARY_TLV_BASE_ID + 0x1e)	/* 0x011e   \
									 */
/** TLV type: BG scan repeat count */
#define TLV_TYPE_REPEAT_COUNT (PROPRIETARY_TLV_BASE_ID + 0xb0)	/* 0x01b0 */
/** TLV type : Authentication type */
#define TLV_TYPE_AUTH_TYPE (PROPRIETARY_TLV_BASE_ID + 0x1f)	/* 0x011f */
/** TLV type : BSSID */
#define TLV_TYPE_BSSID (PROPRIETARY_TLV_BASE_ID + 0x23)	/* 0x0123 */

/** TLV type : Link Quality */
#define TLV_TYPE_LINK_QUALITY (PROPRIETARY_TLV_BASE_ID + 0x24)	/* 0x0124 */

/** TLV type : Data RSSI low */
#define TLV_TYPE_RSSI_LOW_DATA (PROPRIETARY_TLV_BASE_ID + 0x26)	/* 0x0126 */
/** TLV type : Data SNR low */
#define TLV_TYPE_SNR_LOW_DATA (PROPRIETARY_TLV_BASE_ID + 0x27)	/* 0x0127 */
/** TLV type : Data RSSI high */
#define TLV_TYPE_RSSI_HIGH_DATA (PROPRIETARY_TLV_BASE_ID + 0x28)	/* 0x0128 */
/** TLV type : Data SNR high */
#define TLV_TYPE_SNR_HIGH_DATA (PROPRIETARY_TLV_BASE_ID + 0x29)	/* 0x0129 */

/** TLV type : Channel band list */
#define TLV_TYPE_CHANNELBANDLIST (PROPRIETARY_TLV_BASE_ID + 0x2a)	/* 0x012a */

/** TLV type : Security Cfg */
#define TLV_TYPE_SECURITY_CFG (PROPRIETARY_TLV_BASE_ID + 0x3a)	/* 0x013a */

/** TLV type : Passphrase */
#define TLV_TYPE_PASSPHRASE (PROPRIETARY_TLV_BASE_ID + 0x3c)	/* 0x013c */
/** TLV type : SAE Password */
#define TLV_TYPE_SAE_PASSWORD (PROPRIETARY_TLV_BASE_ID + 0x141)	/* 0x0241 */
/** TLV type : SAE PWE Derivation Mode */
#define TLV_TYPE_WPA3_SAE_PWE_DERIVATION_MODE (PROPRIETARY_TLV_BASE_ID + 339)	/* 0x0100 + 0x153 */
/** TLV type : Encryption Protocol TLV */
#define TLV_TYPE_ENCRYPTION_PROTO (PROPRIETARY_TLV_BASE_ID + 0x40)	/* 0x0140   \
									 */
/** TLV type : Cipher TLV */
#define TLV_TYPE_CIPHER (PROPRIETARY_TLV_BASE_ID + 0x42)	/* 0x0142 */
/** TLV type : PMK */
#define TLV_TYPE_PMK (PROPRIETARY_TLV_BASE_ID + 0x44)	/* 0x0144 */

/** TLV type : BCN miss */
#define TLV_TYPE_PRE_BCNMISS (PROPRIETARY_TLV_BASE_ID + 0x49)	/* 0x0149 */

/** TLV type: WAPI IE */
#define TLV_TYPE_WAPI_IE (PROPRIETARY_TLV_BASE_ID + 0x5e)	/* 0x015e */

/** TLV type: MGMT IE */
#define TLV_TYPE_MGMT_IE (PROPRIETARY_TLV_BASE_ID + 0x69)	/* 0x0169 */
/** TLV type: MAX_MGMT_IE */
#define TLV_TYPE_MAX_MGMT_IE (PROPRIETARY_TLV_BASE_ID + 0xaa)	/* 0x01aa */

/** TLV type: key param v2 */
#define TLV_TYPE_KEY_PARAM_V2 (PROPRIETARY_TLV_BASE_ID + 0x9C)	/* 0x019C */

/** TLV type: ps params in hs */
#define TLV_TYPE_PS_PARAMS_IN_HS (PROPRIETARY_TLV_BASE_ID + 0xB5)	/* 0x01b5 */
/** TLV type: hs wake hold off */
#define TLV_TYPE_HS_WAKE_HOLDOFF (PROPRIETARY_TLV_BASE_ID + 0xB6)	/* 0x01b6 */
/** TLV type: wake up source */
#define TLV_TYPE_HS_WAKEUP_SOURCE_GPIO                                         \
	(PROPRIETARY_TLV_BASE_ID + 0x105)	/* 0x0205 */
/** TLV type: management filter  */
#define TLV_TYPE_MGMT_FRAME_WAKEUP                                             \
	(PROPRIETARY_TLV_BASE_ID + 0x116)	/* 0x0216 */
/** TLV type: extend wakeup source */
#define TLV_TYPE_WAKEUP_EXTEND (PROPRIETARY_TLV_BASE_ID + 0x118)	/* 0x0218 */
/** TLV type: HS antenna mode */
#define TLV_TYPE_HS_ANTMODE (PROPRIETARY_TLV_BASE_ID + 0x119)	/* 0x0219 */

/** TLV type: robustcoex mode */
#define TLV_TYPE_ROBUSTCOEX (PROPRIETARY_TLV_BASE_ID + 0x11B)	/* 0x021B */

#define TLV_TYPE_DMCS_STATUS (PROPRIETARY_TLV_BASE_ID + 0x13A)	/* 0x023A */

/** TLV type : TDLS IDLE TIMEOUT */
#define TLV_TYPE_TDLS_IDLE_TIMEOUT (PROPRIETARY_TLV_BASE_ID + 0xC2)	/* 0x01C2  \
									 */

/** TLV type : HT Capabilities */
#define TLV_TYPE_HT_CAP (PROPRIETARY_TLV_BASE_ID + 0x4a)	/* 0x014a */
/** TLV type : HT Information */
#define TLV_TYPE_HT_INFO (PROPRIETARY_TLV_BASE_ID + 0x4b)	/* 0x014b */
/** TLV type : Secondary Channel Offset */
#define TLV_SECONDARY_CHANNEL_OFFSET                                           \
	(PROPRIETARY_TLV_BASE_ID + 0x4c)	/* 0x014c */
/** TLV type : 20/40 BSS Coexistence */
#define TLV_TYPE_2040BSS_COEXISTENCE                                           \
	(PROPRIETARY_TLV_BASE_ID + 0x4d)	/* 0x014d */
/** TLV type : Overlapping BSS Scan Parameters */
#define TLV_TYPE_OVERLAP_BSS_SCAN_PARAM                                        \
	(PROPRIETARY_TLV_BASE_ID + 0x4e)	/* 0x014e */
/** TLV type : Extended capabilities */
#define TLV_TYPE_EXTCAP (PROPRIETARY_TLV_BASE_ID + 0x4f)	/* 0x014f */
/** TLV type : Set of MCS values that STA desires to use within the BSS */
#define TLV_TYPE_HT_OPERATIONAL_MCS_SET                                        \
	(PROPRIETARY_TLV_BASE_ID + 0x50)	/* 0x0150 */
/** TLV ID : Management Frame */
#define TLV_TYPE_MGMT_FRAME (PROPRIETARY_TLV_BASE_ID + 0x68)	/* 0x0168 */
/** TLV type : RXBA_SYNC */
#define TLV_TYPE_RXBA_SYNC (PROPRIETARY_TLV_BASE_ID + 0x99)	/* 0x0199 */

#ifdef WIFI_DIRECT_SUPPORT
/** TLV type : AP PSK */
#define TLV_TYPE_UAP_PSK (PROPRIETARY_TLV_BASE_ID + 0xa8)	/* 0x01a8 */
/** TLV type : p2p NOA */
#define TLV_TYPE_WIFI_DIRECT_NOA (PROPRIETARY_TLV_BASE_ID + 0x83)
/** TLV type : p2p opp ps */
#define TLV_TYPE_WIFI_DIRECT_OPP_PS (PROPRIETARY_TLV_BASE_ID + 0x84)
#endif /* WIFI_DIRECT_SUPPORT */
/** TLV type : GPIO TSF LATCH CONFIG */
#define TLV_TYPE_GPIO_TSF_LATCH_CONFIG (PROPRIETARY_TLV_BASE_ID + 0x153)
/** TLV type : GPIO TSF LATCH REPORT*/
#define TLV_TYPE_GPIO_TSF_LATCH_REPORT (PROPRIETARY_TLV_BASE_ID + 0x154)

/** TLV : 20/40 coex config */
#define TLV_TYPE_2040_BSS_COEX_CONTROL                                         \
	(PROPRIETARY_TLV_BASE_ID + 0x98)	/* 0x0198 */

/** TLV type :  aggr win size */
#define TLV_BTCOEX_WL_AGGR_WINSIZE (PROPRIETARY_TLV_BASE_ID + 0xca)
/** TLV type :  scan time */
#define TLV_BTCOEX_WL_SCANTIME (PROPRIETARY_TLV_BASE_ID + 0Xcb)
/** TLV type : Ewpa_eapol_pkt */
#define TLV_TYPE_EAPOL_PKT (PROPRIETARY_TLV_BASE_ID + 0xcf)

#define TLV_TYPE_COALESCE_RULE (PROPRIETARY_TLV_BASE_ID + 0x9a)

/** TLV type :  EES Configuration */
#define TLV_TYPE_EES_CFG (PROPRIETARY_TLV_BASE_ID + 0xda)
/** TLV type :  EES Network Configuration */
#define TLV_TYPE_EES_NET_CFG (PROPRIETARY_TLV_BASE_ID + 0xdb)

#define TLV_TYPE_LL_STAT_IFACE (PROPRIETARY_TLV_BASE_ID + 300)
#define TLV_TYPE_LL_STAT_RADIO (PROPRIETARY_TLV_BASE_ID + 301)

/** TLV type: fw cap info */
#define TLV_TYPE_FW_CAP_INFO (PROPRIETARY_TLV_BASE_ID + 318)

/** ADDBA TID mask */
#define ADDBA_TID_MASK (MBIT(2) | MBIT(3) | MBIT(4) | MBIT(5))
/** DELBA TID mask */
#define DELBA_TID_MASK (MBIT(12) | MBIT(13) | MBIT(14) | MBIT(15))
/** ADDBA Starting Sequence Number Mask */
#define SSN_MASK 0xfff0

/** Block Ack result status */
/** Block Ack Result : Success */
#define BA_RESULT_SUCCESS 0x0
/** Block Ack Result : Execution failure */
#define BA_RESULT_FAILURE 0x1
/** Block Ack Result : Timeout */
#define BA_RESULT_TIMEOUT 0x2
/** Block Ack Result : Data invalid */
#define BA_RESULT_DATA_INVALID 0x3

/** Get the baStatus (NOT_SETUP, COMPLETE, IN_PROGRESS)
 *  in Tx BA stream table */
#define IS_BASTREAM_SETUP(ptr) (ptr->ba_status)

/** An AMPDU/AMSDU could be disallowed for certain TID. 0xff means
 *  no aggregation is enabled for the assigned TID */
#define BA_STREAM_NOT_ALLOWED 0xff

#ifdef STA_SUPPORT
#endif

/** Test if 11n is enabled by checking the HTCap IE */
#define IS_11N_ENABLED(priv)                                                   \
	((priv->config_bands & BAND_GN || priv->config_bands & BAND_AN) &&     \
	 priv->curr_bss_params.bss_descriptor.pht_cap &&                       \
	 !priv->curr_bss_params.bss_descriptor.disable_11n)
/** Find out if we are the initiator or not */
#define INITIATOR_BIT(DelBAParamSet)                                           \
	(((DelBAParamSet)&MBIT(DELBA_INITIATOR_POS)) >> DELBA_INITIATOR_POS)

/** 4K buf size */
#define MLAN_TX_DATA_BUF_SIZE_4K 4096
/** 8K buf size */
#define MLAN_TX_DATA_BUF_SIZE_8K 8192
/** 12K buf size */
#define MLAN_TX_DATA_BUF_SIZE_12K 12288
/** Max Rx AMPDU Size */
#define MAX_RX_AMPDU_SIZE_64K 0x03
/** Non green field station */
#define NON_GREENFIELD_STAS 0x04

/** Max AMSDU size support */
#define HWSPEC_MAX_AMSDU_SUPP MBIT(31)
/** Greenfield support */
#define HWSPEC_GREENFIELD_SUPP MBIT(29)
/** SM Power Save enable */
#define CAPINFO_SMPS_ENABLE MBIT(27)
/** RX STBC support */
#define HWSPEC_RXSTBC_SUPP MBIT(26)
/** ShortGI @ 40Mhz support */
#define HWSPEC_SHORTGI40_SUPP MBIT(24)
/** ShortGI @ 20Mhz support */
#define HWSPEC_SHORTGI20_SUPP MBIT(23)
/** RX LDPC support */
#define HWSPEC_LDPC_SUPP MBIT(22)
/** Channel width 40Mhz support */
#define HWSPEC_CHANBW40_SUPP MBIT(17)
/** SM Power Save mode */
#define CAPINFO_SMPS_MODE MBIT(9)
/** 40Mhz intolarent enable */
#define CAPINFO_40MHZ_INTOLARENT MBIT(8)

/** Default 11n capability mask for 2.4GHz */
#define DEFAULT_11N_CAP_MASK_BG                                                \
	(HWSPEC_SHORTGI20_SUPP | HWSPEC_RXSTBC_SUPP | HWSPEC_LDPC_SUPP)
/** Default 11n capability mask for 5GHz */
#define DEFAULT_11N_CAP_MASK_A                                                 \
	(HWSPEC_CHANBW40_SUPP | HWSPEC_SHORTGI20_SUPP|HWSPEC_MAX_AMSDU_SUPP |                        \
	 HWSPEC_SHORTGI40_SUPP | HWSPEC_RXSTBC_SUPP | HWSPEC_LDPC_SUPP)

/** Default 11n TX BF capability 2X2 chip **/
#define DEFAULT_11N_TX_BF_CAP_2X2 0x19E74618
/** Default 11n TX BF capability 1X1 chip **/
#define DEFAULT_11N_TX_BF_CAP_1X1 0x19E74608

/** Bits to ignore in hw_dev_cap as these bits are set in get_hw_spec */
#define IGN_HW_DEV_CAP (CAPINFO_40MHZ_INTOLARENT | (CAPINFO_SMPS_ENABLE | CAPINFO_SMPS_MODE))

/** HW_SPEC FwCapInfo : If FW support RSN Replay Detection */
#define ISSUPP_RSN_REPLAY_DETECTION(FwCapInfo) (FwCapInfo & MBIT(28))

/** HW_SPEC FwCapInfo */
#define ISSUPP_11NENABLED(FwCapInfo) (FwCapInfo & MBIT(11))

/** HW_SPEC Dot11nDevCap : MAX AMSDU supported */
#define ISSUPP_MAXAMSDU(Dot11nDevCap) (Dot11nDevCap & MBIT(31))
/** HW_SPEC Dot11nDevCap : Beamforming support */
#define ISSUPP_BEAMFORMING(Dot11nDevCap) (Dot11nDevCap & MBIT(30))
/** HW_SPEC Dot11nDevCap : Green field support */
#define ISSUPP_GREENFIELD(Dot11nDevCap) (Dot11nDevCap & MBIT(29))
/** HW_SPEC Dot11nDevCap : AMPDU support */
#define ISSUPP_AMPDU(Dot11nDevCap) (Dot11nDevCap & MBIT(28))
/** HW_SPEC Dot11nDevCap : MIMO PS support  */
#define ISSUPP_MIMOPS(Dot11nDevCap) (Dot11nDevCap & MBIT(27))
/** HW_SPEC Dot11nDevCap : Rx STBC support */
#define ISSUPP_RXSTBC(Dot11nDevCap) (Dot11nDevCap & MBIT(26))
/** HW_SPEC Dot11nDevCap : Tx STBC support */
#define ISSUPP_TXSTBC(Dot11nDevCap) (Dot11nDevCap & MBIT(25))
/** HW_SPEC Dot11nDevCap : Short GI @ 40Mhz support */
#define ISSUPP_SHORTGI40(Dot11nDevCap) (Dot11nDevCap & MBIT(24))
/** HW_SPEC Dot11nDevCap : Reset Short GI @ 40Mhz support */
#define RESETSUPP_SHORTGI40(Dot11nDevCap) (Dot11nDevCap &= ~MBIT(24))
/** HW_SPEC Dot11nDevCap : Short GI @ 20Mhz support */
#define ISSUPP_SHORTGI20(Dot11nDevCap) (Dot11nDevCap & MBIT(23))
/** HW_SPEC Dot11nDevCap : Rx LDPC support */
#define ISSUPP_RXLDPC(Dot11nDevCap) (Dot11nDevCap & MBIT(22))
/** HW_SPEC Dot11nDevCap : Number of TX BA streams supported */
#define ISSUPP_GETTXBASTREAM(Dot11nDevCap) ((Dot11nDevCap >> 18) & 0xF)
/** HW_SPEC Dot11nDevCap : Channel BW support @ 40Mhz  support */
#define ISSUPP_CHANWIDTH40(Dot11nDevCap) (Dot11nDevCap & MBIT(17))
/** HW_SPEC Dot11nDevCap : Channel BW support @ 20Mhz  support */
#define ISSUPP_CHANWIDTH20(Dot11nDevCap) (Dot11nDevCap & MBIT(16))
/** HW_SPEC Dot11nDevCap : Channel BW support @ 10Mhz  support */
#define ISSUPP_CHANWIDTH10(Dot11nDevCap) (Dot11nDevCap & MBIT(15))
/** Dot11nUsrCap : SMPS static/dynamic mode if BIT27 MIMO PS support eanbled */
#define ISSUPP_SMPS_DYNAMIC_MODE(Dot11nDevCap) (Dot11nDevCap & MBIT(9))
/** Dot11nUsrCap : 40Mhz intolarance enabled */
#define ISENABLED_40MHZ_INTOLARENT(Dot11nDevCap) (Dot11nDevCap & MBIT(8))
/** Dot11nUsrCap : Reset 40Mhz intolarance enabled */
#define RESET_40MHZ_INTOLARENT(Dot11nDevCap) (Dot11nDevCap &= ~MBIT(8))
/** HW_SPEC Dot11nDevCap : Rx AntennaD support */
#define ISSUPP_RXANTENNAD(Dot11nDevCap) (Dot11nDevCap & MBIT(7))
/** HW_SPEC Dot11nDevCap : Rx AntennaC support */
#define ISSUPP_RXANTENNAC(Dot11nDevCap) (Dot11nDevCap & MBIT(6))
/** HW_SPEC Dot11nDevCap : Rx AntennaB support */
#define ISSUPP_RXANTENNAB(Dot11nDevCap) (Dot11nDevCap & MBIT(5))
/** HW_SPEC Dot11nDevCap : Rx AntennaA support */
#define ISSUPP_RXANTENNAA(Dot11nDevCap) (Dot11nDevCap & MBIT(4))
/** HW_SPEC Dot11nDevCap : Tx AntennaD support */
#define ISSUPP_TXANTENNAD(Dot11nDevCap) (Dot11nDevCap & MBIT(3))
/** HW_SPEC Dot11nDevCap : Tx AntennaC support */
#define ISSUPP_TXANTENNAC(Dot11nDevCap) (Dot11nDevCap & MBIT(2))
/** HW_SPEC Dot11nDevCap : Tx AntennaB support */
#define ISSUPP_TXANTENNAB(Dot11nDevCap) (Dot11nDevCap & MBIT(1))
/** HW_SPEC Dot11nDevCap : Tx AntennaA support */
#define ISSUPP_TXANTENNAA(Dot11nDevCap) (Dot11nDevCap & MBIT(0))

/** HW_SPEC Dot11nDevCap : Set support of channel bw @ 40Mhz */
#define SETSUPP_CHANWIDTH40(Dot11nDevCap) (Dot11nDevCap |= MBIT(17))
/** HW_SPEC Dot11nDevCap : Reset support of channel bw @ 40Mhz */
#define RESETSUPP_CHANWIDTH40(Dot11nDevCap) (Dot11nDevCap &= ~MBIT(17))

/** DevMCSSupported : Tx MCS supported */
#define GET_TXMCSSUPP(DevMCSSupported) (DevMCSSupported >> 4)
/** DevMCSSupported : Rx MCS supported */
#define GET_RXMCSSUPP(DevMCSSupported) (DevMCSSupported & 0x0f)

/** GET HTCapInfo : Supported Channel BW */
#define GETHT_SUPPCHANWIDTH(HTCapInfo) (HTCapInfo & MBIT(1))
/** GET HTCapInfo : Support for Greenfield */
#define GETHT_GREENFIELD(HTCapInfo) (HTCapInfo & MBIT(4))
/** GET HTCapInfo : Support for Short GI @ 20Mhz */
#define GETHT_SHORTGI20(HTCapInfo) (HTCapInfo & MBIT(5))
/** GET HTCapInfo : Support for Short GI @ 40Mhz */
#define GETHT_SHORTGI40(HTCapInfo) (HTCapInfo & MBIT(6))
/** GET HTCapInfo : Support for Tx STBC */
#define GETHT_TXSTBC(HTCapInfo) (HTCapInfo & MBIT(7))

/** GET HTCapInfo : Support for Rx STBC */
#define GETHT_RXSTBC(HTCapInfo) ((HTCapInfo >> 8) & 0x03)
/** GET HTCapInfo : Support for Delayed ACK */
#define GETHT_DELAYEDBACK(HTCapInfo) (HTCapInfo & MBIT(10))
/** GET HTCapInfo : Support for Max AMSDU */
#define GETHT_MAXAMSDU(HTCapInfo) (HTCapInfo & MBIT(11))

/** GET HTCapInfo : Support 40Mhz Intolarence */
#define GETHT_40MHZ_INTOLARANT(HTCapInfo) (HTCapInfo & MBIT(14))

/** SET HTCapInfo : Set support for LDPC coding capability */
#define SETHT_LDPCCODINGCAP(HTCapInfo) (HTCapInfo |= MBIT(0))
/** SET HTCapInfo : Set support for Channel BW */
#define SETHT_SUPPCHANWIDTH(HTCapInfo) (HTCapInfo |= MBIT(1))
/** SET HTCapInfo : Set support for Greenfield */
#define SETHT_GREENFIELD(HTCapInfo) (HTCapInfo |= MBIT(4))
/** SET HTCapInfo : Set support for Short GI @ 20Mhz */
#define SETHT_SHORTGI20(HTCapInfo) (HTCapInfo |= MBIT(5))
/** SET HTCapInfo : Set support for Short GI @ 40Mhz */
#define SETHT_SHORTGI40(HTCapInfo) (HTCapInfo |= MBIT(6))
/** SET HTCapInfo : Set support for Tx STBC */
#define SETHT_TXSTBC(HTCapInfo) (HTCapInfo |= MBIT(7))
/** SET HTCapInfo : Set support for Rx STBC */
#define SETHT_RXSTBC(HTCapInfo, value) (HTCapInfo |= (value << 8))
/** SET HTCapInfo : Set support for delayed block ack */
#define SETHT_DELAYEDBACK(HTCapInfo) (HTCapInfo |= MBIT(10))
/** SET HTCapInfo : Set support for Max size AMSDU */
#define SETHT_MAXAMSDU(HTCapInfo) (HTCapInfo |= MBIT(11))
/** SET HTCapInfo : Set support for DSSS/CCK Rates @ 40Mhz */
#define SETHT_DSSSCCK40(HTCapInfo) (HTCapInfo |= MBIT(12))
/** SET HTCapInfo : Enable 40Mhz Intolarence */
#define SETHT_40MHZ_INTOLARANT(HTCapInfo) (HTCapInfo |= MBIT(14))

/** SET HTCapInfo : Set SM power save disabled */
#define SETHT_SMPS_DISABLE(HTCapInfo) ((HTCapInfo) |= (MBIT(2) | MBIT(3)))
/** SET HTCapInfo : Set Dynamic SM power save */
#define SETHT_SMPS_DYNAMIC(HTCapInfo) ((HTCapInfo) |= MBIT(2))

/** RESET HTCapInfo : Set support for LDPC coding capability */
#define RESETHT_LDPCCODINGCAP(HTCapInfo) (HTCapInfo &= ~MBIT(0))
/** RESET HTCapInfo : Set support for Channel BW */
#define RESETHT_SUPPCHANWIDTH(HTCapInfo) (HTCapInfo &= ~MBIT(1))
/** RESET HTCapInfo : Set support for Greenfield */
#define RESETHT_GREENFIELD(HTCapInfo) (HTCapInfo &= ~MBIT(4))
/** RESET HTCapInfo : Set support for Short GI @ 20Mhz */
#define RESETHT_SHORTGI20(HTCapInfo) (HTCapInfo &= ~MBIT(5))
/** RESET HTCapInfo : Set support for Short GI @ 40Mhz */
#define RESETHT_SHORTGI40(HTCapInfo) (HTCapInfo &= ~MBIT(6))
/** RESET HTCapInfo : Set support for Tx STBC */
#define RESETHT_TXSTBC(HTCapInfo) (HTCapInfo &= ~MBIT(7))
/** RESET HTCapInfo : Set support for Rx STBC */
#define RESETHT_RXSTBC(HTCapInfo) (HTCapInfo &= ~(0x03 << 8))
/** RESET HTCapInfo : Set support for delayed block ack */
#define RESETHT_DELAYEDBACK(HTCapInfo) (HTCapInfo &= ~MBIT(10))
/** RESET HTCapInfo : Set support for Max size AMSDU */
#define RESETHT_MAXAMSDU(HTCapInfo) (HTCapInfo &= ~MBIT(11))
/** RESET HTCapInfo : Disable 40Mhz Intolarence */
#define RESETHT_40MHZ_INTOLARANT(HTCapInfo) (HTCapInfo &= ~MBIT(14))
/** RESET HTCapInfo: Enable SM power save */
#define RESETHT_SM_POWERSAVE(HTCapInfo) ((HTCapInfo) &= ~(MBIT(2) | MBIT(3)))
/** RESET HTExtCap : Clear RD Responder bit */
#define RESETHT_EXTCAP_RDG(HTExtCap) (HTExtCap &= ~MBIT(11))
/** SET MCS32 */
#define SETHT_MCS32(x) (x[4] |= 1)
/** Set mcs set defined bit */
#define SETHT_MCS_SET_DEFINED(x) (x[12] |= 1)
/** Set the highest Rx data rate */
#define SETHT_RX_HIGHEST_DT_SUPP(x, y) ((*(t_u16 *)(x + 10)) = y)
/** AMPDU factor size */
#define AMPDU_FACTOR_64K 0x03
/** Set AMPDU size in A-MPDU paramter field */
#define SETAMPDU_SIZE(x, y)                                                    \
	do {                                                                   \
		x = x & ~0x03;                                                 \
		x |= y & 0x03;                                                 \
	} while (0) /** Set AMPDU spacing in A-MPDU paramter field */
#define SETAMPDU_SPACING(x, y)                                                 \
	do {                                                                   \
		x = x & ~0x1c;                                                 \
		x |= (y & 0x07) << 2;                                          \
	} while (0)

/** RadioType : Support for Band A */
#define ISSUPP_BANDA(FwCapInfo) (FwCapInfo & MBIT(10))
/** RadioType : Support for 40Mhz channel BW */
#define ISALLOWED_CHANWIDTH40(Field2) (Field2 & MBIT(2))
/** RadioType : Set support 40Mhz channel */
#define SET_CHANWIDTH40(Field2) (Field2 |= MBIT(2))
/** RadioType : Reset support 40Mhz channel */
#define RESET_CHANWIDTH40(Field2) (Field2 &= ~(MBIT(0) | MBIT(1) | MBIT(2)))
/** RadioType : Get secondary channel */
#define GET_SECONDARYCHAN(Field2) (Field2 & (MBIT(0) | MBIT(1)))

/** ExtCap : Support for FILS */
#define ISSUPP_EXTCAP_FILS(ext_cap) (ext_cap.FILS)
/** ExtCap : Set support FILS */
#define SET_EXTCAP_FILS(ext_cap) (ext_cap.FILS = 1)
/** ExtCap : Reset support FILS */
#define RESET_EXTCAP_FILS(ext_cap) (ext_cap.FILS = 0)

/** ExtCap : Support for TDLS */
#define ISSUPP_EXTCAP_TDLS(ext_cap) (ext_cap.TDLSSupport)
/** ExtCap : Set support TDLS */
#define SET_EXTCAP_TDLS(ext_cap) (ext_cap.TDLSSupport = 1)
/** ExtCap : Reset support TDLS */
#define RESET_EXTCAP_TDLS(ext_cap) (ext_cap.TDLSSupport = 0)
/** ExtCap : Support for TDLS UAPSD */
#define ISSUPP_EXTCAP_TDLS_UAPSD(ext_cap) (ext_cap.TDLSPeerUAPSDSupport)
/** ExtCap : Set support TDLS UAPSD */
#define SET_EXTCAP_TDLS_UAPSD(ext_cap) (ext_cap.TDLSPeerUAPSDSupport = 1)
/** ExtCap : Reset support TDLS UAPSD */
#define RESET_EXTCAP_TDLS_UAPSD(ext_cap) (ext_cap.TDLSPeerUAPSDSupport = 0)
/** ExtCap : Support for TDLS CHANNEL SWITCH */
#define ISSUPP_EXTCAP_TDLS_CHAN_SWITCH(ext_cap) (ext_cap.TDLSChannelSwitching)
/** ExtCap : Set support TDLS CHANNEL SWITCH */
#define SET_EXTCAP_TDLS_CHAN_SWITCH(ext_cap) (ext_cap.TDLSChannelSwitching = 1)
/** ExtCap : Reset support TDLS CHANNEL SWITCH */
#define RESET_EXTCAP_TDLS_CHAN_SWITCH(ext_cap)                                 \
	(ext_cap.TDLSChannelSwitching = 0)
/** ExtCap : Set support Multi BSSID */
#define SET_EXTCAP_MULTI_BSSID(ext_cap) (ext_cap.MultipleBSSID = 1)
/** ExtCap : Support for Interworking */
#define ISSUPP_EXTCAP_INTERWORKING(ext_cap) (ext_cap.Interworking)
/** ExtCap : Set support Interworking */
#define SET_EXTCAP_INTERWORKING(ext_cap) (ext_cap.Interworking = 1)
/** ExtCap : Reset support Interworking */
#define RESET_EXTCAP_INTERWORKING(ext_cap) (ext_cap.Interworking = 0)
/** ExtCap : Support for Operation Mode Notification */
#define ISSUPP_EXTCAP_OPERMODENTF(ext_cap) (ext_cap.OperModeNtf)
/** ExtCap : Set support Operation Mode Notification */
#define SET_EXTCAP_OPERMODENTF(ext_cap) (ext_cap.OperModeNtf = 1)
/** ExtCap : Reset support Operation Mode Notification */
#define RESET_EXTCAP_OPERMODENTF(ext_cap) (ext_cap.OperModeNtf = 0)
/** ExtCap : Support for QosMap */
#define ISSUPP_EXTCAP_QOS_MAP(ext_cap) (ext_cap.Qos_Map)
/** ExtCap : Set Support QosMap */
#define SET_EXTCAP_QOS_MAP(ext_cap) (ext_cap.Qos_Map = 1)
/** ExtCap : Reset support QosMap */
#define RESET_EXTCAP_QOS_MAP(ext_cap) (ext_cap.Qos_Map = 0)
/** ExtCap : Support for BSS_Transition */
#define ISSUPP_EXTCAP_BSS_TRANSITION(ext_cap) (ext_cap.BSS_Transition)
/** ExtCap : Set Support BSS_Transition */
#define SET_EXTCAP_BSS_TRANSITION(ext_cap) (ext_cap.BSS_Transition = 1)
/** ExtCap : Reset support BSS_Transition */
#define RESET_EXTCAP_BSS_TRANSITION(ext_cap) (ext_cap.BSS_Transition = 0)

/** ExtCap : Support for TDLS wider bandwidth */
#define ISSUPP_EXTCAP_TDLS_WIDER_BANDWIDTH(ext_cap) (ext_cap.TDLSWildBandwidth)
/** ExtCap : Set support TDLS wider bandwidth */
#define SET_EXTCAP_TDLS_WIDER_BANDWIDTH(ext_cap) (ext_cap.TDLSWildBandwidth = 1)
/** ExtCap : Reset support TDLS wider bandwidth */
#define RESET_EXTCAP_TDLS_WIDER_BANDWIDTH(ext_cap)                             \
	(ext_cap.TDLSWildBandwidth = 0)

/** ExtCap : Support for extend channel switch */
#define ISSUPP_EXTCAP_EXT_CHANNEL_SWITCH(ext_cap) (ext_cap.ExtChanSwitching)
/** ExtCap : Set support Ext Channel Switch */
#define SET_EXTCAP_EXT_CHANNEL_SWITCH(ext_cap) (ext_cap.ExtChanSwitching = 1)
/** ExtCap: Set Timing Measurement */
#define SET_EXTCAP_EXT_TIMING_MEASUREMENT(ext_cap)                             \
	(ext_cap.TimingMeasurement = 1)
/** ExtCap : Reset support Ext Channel Switch */
#define RESET_EXTCAP_EXT_CHANNEL_SWITCH(ext_cap) (ext_cap.ExtChanSwitching = 0)

/** ExtCap : Support for TWT RESP */
#define ISSUPP_EXTCAP_EXT_TWT_RESP(ext_cap) (ext_cap.TWTResp)
/** ExtCap : Set support Ext TWT_REQ */
#define SET_EXTCAP_TWT_REQ(ext_cap) (ext_cap.TWTReq = 1)
/** ExtCap : ReSet support Ext TWT REQ */
#define RESET_EXTCAP_TWT_REQ(ext_cap) (ext_cap.TWTReq = 0)

/** LLC/SNAP header len   */
#define LLC_SNAP_LEN 8

/** bandwidth following HTCAP */
#define BW_FOLLOW_HTCAP 0
/** bandwidth following VHTCAP */
#define BW_FOLLOW_VHTCAP 1

/** HW_SPEC FwCapInfo */
#define HWSPEC_11ACSGI80_SUPP MBIT(5)
#define HWSPEC_11ACRXSTBC_SUPP MBIT(8)

#define ISSUPP_11ACENABLED(FwCapInfo) (FwCapInfo & (MBIT(12) | MBIT(13)))

#define ISSUPP_11AC2GENABLED(FwCapInfo) (FwCapInfo & MBIT(12))
#define ISSUPP_11AC5GENABLED(FwCapInfo) (FwCapInfo & MBIT(13))

/** HW_SPEC Dot11acDevCap : HTC-VHT supported */
#define ISSUPP_11ACVHTHTCVHT(Dot11acDevCap) (Dot11acDevCap & MBIT(22))
/** HW_SPEC Dot11acDevCap : VHT TXOP PS support */
#define ISSUPP_11ACVHTTXOPPS(Dot11acDevCap) (Dot11acDevCap & MBIT(21))
/** HW_SPEC Dot11acDevCap : MU RX beamformee support */
#define ISSUPP_11ACMURXBEAMFORMEE(Dot11acDevCap) (Dot11acDevCap & MBIT(20))
/** HW_SPEC Dot11acDevCap : MU TX beamformee support */
#define ISSUPP_11ACMUTXBEAMFORMEE(Dot11acDevCap) (Dot11acDevCap & MBIT(19))
/** HW_SPEC Dot11acDevCap : SU Beamformee support */
#define ISSUPP_11ACSUBEAMFORMEE(Dot11acDevCap) (Dot11acDevCap & MBIT(12))
/** HW_SPEC Dot11acDevCap : SU Beamformer support */
#define ISSUPP_11ACSUBEAMFORMER(Dot11acDevCap) (Dot11acDevCap & MBIT(11))
/** HW_SPEC Dot11acDevCap : Rx STBC support */
#define ISSUPP_11ACRXSTBC(Dot11acDevCap) (Dot11acDevCap & MBIT(8))
/** HW_SPEC Dot11acDevCap : Tx STBC support */
#define ISSUPP_11ACTXSTBC(Dot11acDevCap) (Dot11acDevCap & MBIT(7))
/** HW_SPEC Dot11acDevCap : Short GI support for 160MHz BW */
#define ISSUPP_11ACSGI160(Dot11acDevCap) (Dot11acDevCap & MBIT(6))
/** HW_SPEC Dot11acDevCap : Short GI support for 80MHz BW */
#define ISSUPP_11ACSGI80(Dot11acDevCap) (Dot11acDevCap & MBIT(5))
/** HW_SPEC Dot11acDevCap : LDPC coding support */
#define ISSUPP_11ACLDPC(Dot11acDevCap) (Dot11acDevCap & MBIT(4))
/** HW_SPEC Dot11acDevCap : Channel BW 20/40/80/160/80+80 MHz support */
#define ISSUPP_11ACBW8080(Dot11acDevCap) (Dot11acDevCap & MBIT(3))
/** HW_SPEC Dot11acDevCap : Channel BW 20/40/80/160 MHz support */
#define ISSUPP_11ACBW160(Dot11acDevCap) (Dot11acDevCap & MBIT(2))

/** Set VHT Cap Info: Max MPDU length */
#define SET_VHTCAP_MAXMPDULEN(VHTCapInfo, value) (VHTCapInfo |= (value & 0x03))
/** Reset VHT Cap Info: Max MPDU length */
#define RESET_VHTCAP_MAXMPDULEN(VHTCapInfo) (VHTCapInfo &= ~(MBIT(0) | MBIT(1)))

/** SET VHT CapInfo:  Supported Channel Width SET (2 bits)*/
#define SET_VHTCAP_CHWDSET(VHTCapInfo, value)                                  \
	(VHTCapInfo |= ((value & 0x3) << 2))
/** SET VHT CapInfo:  Rx STBC (3 bits) */
#define SET_VHTCAP_RXSTBC(VHTCapInfo, value)                                   \
	(VHTCapInfo |= ((value & 0x7) << 8))
/** SET VHT CapInfo:  Commpressed Steering Num of BFer Ant Supported (3 bits) */
#define SET_VHTCAP_SNBFERANT(VHTCapInfo, value)                                \
	(VHTCapInfo |= ((value & 0x7) << 13))
/** SET VHT CapInfo:  Num of Sounding Dimensions (3 bits) */
#define SET_VHTCAP_NUMSNDDM(VHTCapInfo, value)                                 \
	(VHTCapInfo |= ((value & 0x7) << 16))
/** SET VHT CapInfo:  Max AMPDU Length Exponent (3 bits) */
#define SET_VHTCAP_MAXAMPDULENEXP(VHTCapInfo, value)                           \
	(VHTCapInfo |= ((value & 0x7) << 23))
/** SET VHT CapInfo:  VHT Link Adaptation Capable (2 bits) */
#define SET_VHTCAP_LINKADPCAP(VHTCapInfo, value)                               \
	(VHTCapInfo |= ((value & 0x3) << 26))

/** HW_SPEC Dot11acDevCap : ReSet VHT Link Adapation Capable */
#define RESET_11ACVHTLINKCAPA(Dot11acDevCap, value) (Dot11acDevCap &= ~(0x03))
/** HW_SPEC Dot11acDevCap : ReSet Maximum AMPDU Length Exponent */
#define RESET_11ACAMPDULENEXP(Dot11acDevCap, value) (Dot11acDevCap &= ~(0x07))
/** HW_SPEC Dot11acDevCap : ReSet support of HTC-VHT */
#define RESET_11ACVHTHTCVHT(Dot11acDevCap) (Dot11acDevCap &= ~MBIT(22))
/** HW_SPEC Dot11acDevCap : ReSet support of VHT TXOP PS */
#define RESET_11ACVHTTXOPPS(Dot11acDevCap) (Dot11acDevCap &= ~MBIT(21))
/** HW_SPEC Dot11acDevCap : ReSet support of MU RX beamformee */
#define RESET_11ACMURXBEAMFORMEE(Dot11acDevCap) (Dot11acDevCap &= ~MBIT(20))
/** HW_SPEC Dot11acDevCap : ReSet support of MU TX beamformee */
#define RESET_11ACMUTXBEAMFORMEE(Dot11acDevCap) (Dot11acDevCap &= ~MBIT(19))
/** HW_SPEC Dot11acDevCap : ReSet Number of Sounding Dimensions */
#define RESET_11ACSOUNDINGNUM(Dot11acDevCap) (Dot11acDevCap &= ~((0x07) << 16))
/** HW_SPEC Dot11acDevCap : ReSet Compressed Steering Number
 * of Beamformer Antenna */
#define RESET_11ACBFANTNUM(Dot11acDevCap) (Dot11acDevCap &= ~((0x07) << 13))
/** HW_SPEC Dot11acDevCap : ReSet support of SU Beamformee */
#define RESET_11ACSUBEAMFORMEE(Dot11acDevCap) (Dot11acDevCap &= ~MBIT(12))
/** HW_SPEC Dot11acDevCap : ReSet support of SU Beamformer */
#define RESET_11ACSUBEAMFORMER(Dot11acDevCap) (Dot11acDevCap &= ~MBIT(11))
/** HW_SPEC Dot11acDevCap : ReSet support of Rx STBC */
#define RESET_11ACRXSTBC(Dot11acDevCap) (Dot11acDevCap &= ~((0x07) << 8))
/** HW_SPEC Dot11acDevCap : ReSet support of Tx STBC */
#define RESET_11ACTXSTBC(Dot11acDevCap) (Dot11acDevCap &= ~MBIT(7))
/** HW_SPEC Dot11acDevCap : ReSet support of Short GI support for 160MHz BW */
#define RESET_11ACSGI160(Dot11acDevCap) (Dot11acDevCap &= ~MBIT(6))
/** HW_SPEC Dot11acDevCap : ReSet support of Short GI support for 80MHz BW */
#define RESET_11ACSGI80(Dot11acDevCap) (Dot11acDevCap &= ~MBIT(5))
/** HW_SPEC Dot11acDevCap : ReSet support of LDPC coding */
#define RESET_11ACLDPC(Dot11acDevCap) (Dot11acDevCap &= ~MBIT(4))
/** HW_SPEC Dot11acDevCap : ReSet support of
 * Channel BW 20/40/80/160/80+80 MHz */
#define RESET_11ACBW8080(Dot11acDevCap) (Dot11acDevCap &= ~MBIT(3))
/** HW_SPEC Dot11acDevCap : ReSet support of
 * Channel BW 20/40/80/160 MHz */
#define RESET_11ACBW160(Dot11acDevCap) (Dot11acDevCap &= ~MBIT(2))
/** HW_SPEC Dot11acDevCap : ReSet Max MPDU length */
#define RESET_11ACMAXMPDULEN(Dot11acDevCap) (Dot11acDevCap &= ~(0x03))

/** Default 11ac capability mask for 2.4GHz */
#define DEFAULT_11AC_CAP_MASK_BG                                               \
	(HWSPEC_11ACSGI80_SUPP | HWSPEC_11ACRXSTBC_SUPP)
/** Default 11ac capability mask for 5GHz */
#define DEFAULT_11AC_CAP_MASK_A (HWSPEC_11ACSGI80_SUPP | HWSPEC_11ACRXSTBC_SUPP)
/** GET VHT CapInfo : MAX MPDU Length */
#define GET_VHTCAP_MAXMPDULEN(VHTCapInfo) (VHTCapInfo & 0x3)
/** GET VHT CapInfo:  Supported Channel Width SET (2 bits)*/
#define GET_VHTCAP_CHWDSET(VHTCapInfo) ((VHTCapInfo >> 2) & 0x3)
/** GET VHT CapInfo:  Rx STBC (3 bits) */
#define GET_VHTCAP_RXSTBC(VHTCapInfo) ((VHTCapInfo >> 8) & 0x7)
/** GET VHT CapInfo:  Compressed Steering Num of BFer Ant Supported (3 bits) */
#define GET_VHTCAP_SNBFERANT(VHTCapInfo) ((VHTCapInfo >> 13) & 0x7)
/** GET VHT CapInfo:  Num of Sounding Dimensions (3 bits) */
#define GET_VHTCAP_NUMSNDDM(VHTCapInfo) ((VHTCapInfo >> 16) & 0x7)
/** GET VHT CapInfo:  Max AMPDU Length Exponent (3 bits) */
#define GET_VHTCAP_MAXAMPDULENEXP(VHTCapInfo) ((VHTCapInfo >> 23) & 0x7)
/** GET VHT CapInfo:  VHT Link Adaptation Capable (2 bits) */
#define GET_VHTCAP_LINKADPCAP(VHTCapInfo) ((VHTCapInfo >> 26) & 0x3)
/**SET OPERATING MODE:Channel Width:80M*/
#define SET_OPER_MODE_80M(oper_mode)                                           \
	(oper_mode = (oper_mode & ~MBIT(0)) | MBIT(1))
/**SET OPERATING MODE:Channel Width:40M*/
#define SET_OPER_MODE_40M(oper_mode)                                           \
	(oper_mode = (oper_mode & ~MBIT(1)) | MBIT(0))
/**SET OPERATING MODE:Channel Width:20M*/
#define SET_OPER_MODE_20M(oper_mode) (oper_mode &= ~(0x03))
#define IS_OPER_MODE_20M(oper_mode) (((oper_mode) & (MBIT(0) | MBIT(1))) == 0)
/**SET OPERATING MODE:Rx NSS:2*/
#define SET_OPER_MODE_2NSS(oper_mode)                                          \
	(oper_mode = (oper_mode & ~(MBIT(5) | MBIT(6))) | MBIT(4))
/**SET OPERATING MODE:Rx NSS:1*/
#define SET_OPER_MODE_1NSS(oper_mode)                                          \
	(oper_mode &= ~(MBIT(4) | MBIT(5) | MBIT(6)))

#define NO_NSS_SUPPORT 0x3
#define GET_VHTMCS(MCSMapSet) (MCSMapSet & 0xFFFF)
#define GET_VHTNSSMCS(MCSMapSet, nss) ((MCSMapSet >> (2 * (nss - 1))) & 0x3)
#define RET_VHTNSSMCS(MCSMapSet, nss) ((MCSMapSet >> (2 * (nss - 1))) & 0x3)
#define SET_VHTNSSMCS(MCSMapSet, nss, value)                                   \
	(MCSMapSet |= (value & 0x3) << (2 * (nss - 1)))

/** DevMCSSupported : Tx MCS supported */
#define GET_DEVTXMCSMAP(DevMCSMap) (DevMCSMap >> 16)
#define GET_DEVNSSTXMCS(DevMCSMap, nss)                                        \
	((DevMCSMap >> (2 * (nss - 1) + 16)) & 0x3)
#define SET_DEVNSSTXMCS(DevMCSMap, nss, value)                                 \
	(DevMCSMap |= (value & 0x3) << (2 * (nss - 1) + 16))
#define RESET_DEVTXMCSMAP(DevMCSMap) (DevMCSMap &= 0xFFFF)
/** DevMCSSupported : Rx MCS supported */
#define GET_DEVRXMCSMAP(DevMCSMap) (DevMCSMap & 0xFFFF)
#define GET_DEVNSSRXMCS(DevMCSMap, nss) ((DevMCSMap >> (2 * (nss - 1))) & 0x3)
#define SET_DEVNSSRXMCS(DevMCSMap, nss, value)                                 \
	(DevMCSMap |= (value & 0x3) << (2 * (nss - 1)))
#define RESET_DEVRXMCSMAP(DevMCSMap) (DevMCSMap &= 0xFFFF0000)

/** TLV type : Rate scope */
#define TLV_TYPE_RATE_DROP_PATTERN (PROPRIETARY_TLV_BASE_ID + 0x51)	/* 0x0151  \
									 */
/** TLV type : Rate drop pattern */
#define TLV_TYPE_RATE_DROP_CONTROL (PROPRIETARY_TLV_BASE_ID + 0x52)	/* 0x0152  \
									 */
/** TLV type : Rate scope */
#define TLV_TYPE_RATE_SCOPE (PROPRIETARY_TLV_BASE_ID + 0x53)	/* 0x0153 */

/** TLV type : Power group */
#define TLV_TYPE_POWER_GROUP (PROPRIETARY_TLV_BASE_ID + 0x54)	/* 0x0154 */

/** Modulation class for DSSS Rates */
#define MOD_CLASS_HR_DSSS 0x03
/** Modulation class for OFDM Rates */
#define MOD_CLASS_OFDM 0x07
/** Modulation class for HT Rates */
#define MOD_CLASS_HT 0x08
/** Modulation class for VHT Rates */
#define MOD_CLASS_VHT 0x09
/** HT bandwidth 20 MHz */
#define HT_BW_20 0
/** HT bandwidth 40 MHz */
#define HT_BW_40 1
/** HT bandwidth 80 MHz */
#define HT_BW_80 2

/** TLV type : TX RATE CFG, rename from TLV_TYPE_GI_LTF_SIZE to include CMD and
 * HE ER SU settings to this tlv */
#define TLV_TYPE_TX_RATE_CFG (PROPRIETARY_TLV_BASE_ID + 319)	/* 0x023f */

/** TLV type : Scan Response */
#define TLV_TYPE_BSS_SCAN_RSP (PROPRIETARY_TLV_BASE_ID + 0x56)	/* 0x0156 */
/** TLV type : Scan Response Stats */
#define TLV_TYPE_BSS_SCAN_INFO (PROPRIETARY_TLV_BASE_ID + 0x57)	/* 0x0157 */

/** TLV type : 11h Basic Rpt */
#define TLV_TYPE_CHANRPT_11H_BASIC (PROPRIETARY_TLV_BASE_ID + 0x5b)	/* 0x015b  \
									 */

/** TLV type : DFS W53 Configuration */
#define TLV_TYPE_DFS_W53_CFG (PROPRIETARY_TLV_BASE_ID + 0x145)	// + 325
#ifdef OPCHAN
/** TLV type : OpChannel control */
#define TLV_TYPE_OPCHAN_CONTROL_DESC                                           \
	(PROPRIETARY_TLV_BASE_ID + 0x79)	/* 0x0179 */
/** TLV type : OpChannel channel group control */
#define TLV_TYPE_OPCHAN_CHANGRP_CTRL                                           \
	(PROPRIETARY_TLV_BASE_ID + 0x7a)	/* 0x017a */
#endif

/** TLV type : Action frame */
#define TLV_TYPE_IEEE_ACTION_FRAME (PROPRIETARY_TLV_BASE_ID + 0x8c)	/* 0x018c  \
									 */

/** TLV type : SCAN channel gap */
#define TLV_TYPE_SCAN_CHANNEL_GAP (PROPRIETARY_TLV_BASE_ID + 0xc5)	/* 0x01c5   \
									 */
/** TLV type : Channel statistics */
#define TLV_TYPE_CHANNEL_STATS (PROPRIETARY_TLV_BASE_ID + 0xc6)	/* 0x01c6 */
/** TLV type : BSS_MODE */
#define TLV_TYPE_BSS_MODE (PROPRIETARY_TLV_BASE_ID + 0xce)	/* 0x01ce */

/** Firmware Host Command ID Constants */
/** Host Command ID : Get hardware specifications */
#define HostCmd_CMD_GET_HW_SPEC 0x0003
/** Host Command ID : 802.11 scan */
#define HostCmd_CMD_802_11_SCAN 0x0006
/** Host Command ID : 802.11 get log */
#define HostCmd_CMD_802_11_GET_LOG 0x000b

/** Host Command id: GET_TX_RX_PKT_STATS */
#define HOST_CMD_TX_RX_PKT_STATS 0x008d

/** Host Command ID : 802.11 get/set link layer statistic */
#define HostCmd_CMD_802_11_LINK_STATS 0x0256

/** Host Command ID : MAC multicast address */
#define HostCmd_CMD_MAC_MULTICAST_ADR 0x0010
/** Host Command ID : 802.11 EEPROM access */
#define HostCmd_CMD_802_11_EEPROM_ACCESS 0x0059
/** Host Command ID : 802.11 associate */
#define HostCmd_CMD_802_11_ASSOCIATE 0x0012

/** Host Command ID : 802.11 SNMP MIB */
#define HostCmd_CMD_802_11_SNMP_MIB 0x0016
/** Host Command ID : MAC register access */
#define HostCmd_CMD_MAC_REG_ACCESS 0x0019
/** Host Command ID : BBP register access */
#define HostCmd_CMD_BBP_REG_ACCESS 0x001a
/** Host Command ID : RF register access */
#define HostCmd_CMD_RF_REG_ACCESS 0x001b

/** Host Command ID : 802.11 radio control */
#define HostCmd_CMD_802_11_RADIO_CONTROL 0x001c
/** Host Command ID : 802.11 RF channel */
#define HostCmd_CMD_802_11_RF_CHANNEL 0x001d
/** Host Command ID : 802.11 RF Tx power */
#define HostCmd_CMD_802_11_RF_TX_POWER 0x001e

/** Host Command ID : 802.11 RF antenna */
#define HostCmd_CMD_802_11_RF_ANTENNA 0x0020

/** Host Command ID : 802.11 deauthenticate */
#define HostCmd_CMD_802_11_DEAUTHENTICATE 0x0024
/** Host Command ID: 802.11 disassoicate */
#define HostCmd_CMD_802_11_DISASSOCIATE 0x0026
/** Host Command ID : MAC control */
#define HostCmd_CMD_MAC_CONTROL 0x0028
/** Host Command ID : 802.11 Ad-Hoc start */
#define HostCmd_CMD_802_11_AD_HOC_START 0x002b
/** Host Command ID : 802.11 Ad-Hoc join */
#define HostCmd_CMD_802_11_AD_HOC_JOIN 0x002c

/** Host Command ID: CW Mode */
#define HostCmd_CMD_CW_MODE_CTRL 0x0239
/** Host Command ID : 802.11 key material */
#define HostCmd_CMD_802_11_KEY_MATERIAL 0x005e

/** Host Command ID : 802.11 Ad-Hoc stop */
#define HostCmd_CMD_802_11_AD_HOC_STOP 0x0040

/** Host Command ID : 802.22 MAC address */
#define HostCmd_CMD_802_11_MAC_ADDRESS 0x004D

/** Host Command ID : WMM Traffic Stream Status */
#define HostCmd_CMD_WMM_TS_STATUS 0x005d

/** Host Command ID : 802.11 D domain information */
#define HostCmd_CMD_802_11D_DOMAIN_INFO 0x005b

/*This command gets/sets the Transmit Rate-based Power Control (TRPC) channel
 * configuration.*/
#define HostCmd_CHANNEL_TRPC_CONFIG 0x00fb

/** Host Command ID : 802.11 TPC information */
#define HostCmd_CMD_802_11_TPC_INFO 0x005f
/** Host Command ID : 802.11 TPC adapt req */
#define HostCmd_CMD_802_11_TPC_ADAPT_REQ 0x0060
/** Host Command ID : 802.11 channel SW ann */
#define HostCmd_CMD_802_11_CHAN_SW_ANN 0x0061

/** Host Command ID : Measurement request */
#define HostCmd_CMD_MEASUREMENT_REQUEST 0x0062
/** Host Command ID : Measurement report */
#define HostCmd_CMD_MEASUREMENT_REPORT 0x0063

/** Host Command ID : 802.11 sleep parameters */
#define HostCmd_CMD_802_11_SLEEP_PARAMS 0x0066

/** Host Command ID : 802.11 ps inactivity timeout */
#define HostCmd_CMD_802_11_PS_INACTIVITY_TIMEOUT 0x0067

/** Host Command ID : 802.11 sleep period */
#define HostCmd_CMD_802_11_SLEEP_PERIOD 0x0068

/** Host Command ID: 802.11 BG scan config */
#define HostCmd_CMD_802_11_BG_SCAN_CONFIG 0x006b
/** Host Command ID : 802.11 BG scan query */
#define HostCmd_CMD_802_11_BG_SCAN_QUERY 0x006c

/** Host Command ID : WMM ADDTS req */
#define HostCmd_CMD_WMM_ADDTS_REQ 0x006E
/** Host Command ID : WMM DELTS req */
#define HostCmd_CMD_WMM_DELTS_REQ 0x006F
/** Host Command ID : WMM queue configuration */
#define HostCmd_CMD_WMM_QUEUE_CONFIG 0x0070
/** Host Command ID : 802.11 get status */
#define HostCmd_CMD_WMM_GET_STATUS 0x0071

/** Host Command ID : 802.11 subscribe event */
#define HostCmd_CMD_802_11_SUBSCRIBE_EVENT 0x0075

/** Host Command ID : 802.11 Tx rate query */
#define HostCmd_CMD_802_11_TX_RATE_QUERY 0x007f
/** Host Command ID :Get timestamp value */
#define HostCmd_CMD_GET_TSF 0x0080

/** Host Command ID : WMM queue stats */
#define HostCmd_CMD_WMM_QUEUE_STATS 0x0081

/** Host Command ID : KEEP ALIVE command */
#define HostCmd_CMD_AUTO_TX 0x0082

/** Host Command ID : 802.11 IBSS coalescing status */
#define HostCmd_CMD_802_11_IBSS_COALESCING_STATUS 0x0083

/** Host Command ID : Memory access */
#define HostCmd_CMD_MEM_ACCESS 0x0086

#if defined(SDIO)
/** Host Command ID : SDIO GPIO interrupt configuration */
#define HostCmd_CMD_SDIO_GPIO_INT_CONFIG 0x0088
#endif

/** Host Command ID : Mfg command */
#define HostCmd_CMD_MFG_COMMAND 0x0089
/** Host Command ID : Inactivity timeout ext */
#define HostCmd_CMD_INACTIVITY_TIMEOUT_EXT 0x008a

/** Host Command ID : DBGS configuration */
#define HostCmd_CMD_DBGS_CFG 0x008b
/** Host Command ID : Get memory */
#define HostCmd_CMD_GET_MEM 0x008c

/** Host Command ID : Cal data dnld */
#define HostCmd_CMD_CFG_DATA 0x008f

/** Host Command ID : SDIO pull control */
#define HostCmd_CMD_SDIO_PULL_CTRL 0x0093

/** Host Command ID : ECL system clock configuration */
#define HostCmd_CMD_ECL_SYSTEM_CLOCK_CONFIG 0x0094

/** Host Command ID : Extended version */
#define HostCmd_CMD_VERSION_EXT 0x0097

/** Host Command ID : MEF configuration */
#define HostCmd_CMD_MEF_CFG 0x009a
/** Host Command ID : 802.11 RSSI INFO*/
#define HostCmd_CMD_RSSI_INFO 0x00a4
/** Host Command ID : Function initialization */
#define HostCmd_CMD_FUNC_INIT 0x00a9
/** Host Command ID : Function shutdown */
#define HostCmd_CMD_FUNC_SHUTDOWN 0x00aa

/** Host Command ID : Robustcoex */
#define HostCmd_CMD_802_11_ROBUSTCOEX 0x00e0

/** Host Command ID :EAPOL PKT */
#define HostCmd_CMD_802_11_EAPOL_PKT 0x012e

/** Host Command ID :MIMO SWITCH **/
#define HostCmd_CMD_802_11_MIMO_SWITCH 0x0235

/** Host Command ID : 802.11 RSSI INFO EXT*/
#define HostCmd_CMD_RSSI_INFO_EXT 0x0237

#ifdef RX_PACKET_COALESCE
/** TLV ID for RX pkt coalesce config */
#define TLV_TYPE_RX_PKT_COAL_CONFIG (PROPRIETARY_TLV_BASE_ID + 0xC9)
#endif

#define TLV_TYPE_PREV_BSSID (PROPRIETARY_TLV_BASE_ID + 330)

/** Host Command ID : Channel report request */
#define HostCmd_CMD_CHAN_REPORT_REQUEST 0x00dd

/** Host Command ID: SUPPLICANT_PMK */
#define HostCmd_CMD_SUPPLICANT_PMK 0x00c4
/** Host Command ID: SUPPLICANT_PROFILE */
#define HostCmd_CMD_SUPPLICANT_PROFILE 0x00c5

/** Host Command ID : Add Block Ack Request */
#define HostCmd_CMD_11N_ADDBA_REQ 0x00ce
/** Host Command ID : Delete a Block Ack Request */
#define HostCmd_CMD_11N_CFG 0x00cd
/** Host Command ID : Add Block Ack Response */
#define HostCmd_CMD_11N_ADDBA_RSP 0x00cf
/** Host Command ID : Delete a Block Ack Request */
#define HostCmd_CMD_11N_DELBA 0x00d0
/** Host Command ID: Configure Tx Buf size */
#define HostCmd_CMD_RECONFIGURE_TX_BUFF 0x00d9
/** Host Command ID: AMSDU Aggr Ctrl */
#define HostCmd_CMD_AMSDU_AGGR_CTRL 0x00df
/** Host Command ID: 11AC config */
#define HostCmd_CMD_11AC_CFG 0x0112
/** Host Command ID: Configure TX Beamforming capability */
#define HostCmd_CMD_TX_BF_CFG 0x0104

/** Host Command ID : 802.11 TX power configuration */
#define HostCmd_CMD_TXPWR_CFG 0x00d1

/** Host Command ID : Soft Reset */
#define HostCmd_CMD_SOFT_RESET 0x00d5

/** Host Command ID : 802.11 b/g/n rate configration */
#define HostCmd_CMD_TX_RATE_CFG 0x00d6

/** Host Command ID : Enhanced PS mode */
#define HostCmd_CMD_802_11_PS_MODE_ENH 0x00e4

/** Host command action : Host sleep configuration */
#define HostCmd_CMD_802_11_HS_CFG_ENH 0x00e5

/** Host Command ID : CAU register access */
#define HostCmd_CMD_CAU_REG_ACCESS 0x00ed

/** Host Command ID : mgmt IE list */
#define HostCmd_CMD_MGMT_IE_LIST 0x00f2

#define HostCmd_CMD_802_11_BAND_STEERING 0x026f

/** Host Command ID : TDLS configuration */
#define HostCmd_CMD_TDLS_CONFIG 0x0100
/** Host Command ID : TDLS operation */
#define HostCmd_CMD_TDLS_OPERATION 0x0122

#ifdef SDIO
/** Host Command ID : SDIO single port RX aggr */
#define HostCmd_CMD_SDIO_SP_RX_AGGR_CFG 0x0223

/** fw_cap_info bit16 for sdio sp rx aggr flag*/
#define SDIO_SP_RX_AGGR_ENABLE MBIT(16)

#endif

/* fw_cap_info bit18 for ecsa support*/
#define FW_CAPINFO_ECSA MBIT(18)

/* fw_cap_info bit20 for get log*/
#define FW_CAPINFO_GET_LOG MBIT(20)

/** fw_cap_info bit22 for embedded supplicant support*/
#define FW_CAPINFO_SUPPLICANT_SUPPORT MBIT(21)

/** fw_cap_info bit23 for embedded authenticator support*/
#define FW_CAPINFO_AUTH_SUPPORT MBIT(22)

/** fw_cap_info bit25 for adhoc support*/
#define FW_CAPINFO_ADHOC_SUPPORT MBIT(25)
/** Check if adhoc is supported by firmware */
#define IS_FW_SUPPORT_ADHOC(_adapter)                                          \
	(_adapter->fw_cap_info & FW_CAPINFO_ADHOC_SUPPORT)

/** Check if supplicant is supported by firmware */
#define IS_FW_SUPPORT_SUPPLICANT(_adapter)                                     \
	(_adapter->fw_cap_info & FW_CAPINFO_SUPPLICANT_SUPPORT)

/** Check if authenticator is supported by firmware */
#define IS_FW_SUPPORT_AUTHENTICATOR(_adapter)                                  \
	(_adapter->fw_cap_info & FW_CAPINFO_AUTH_SUPPORT)

/** Ext fw cap info bit0 only 1x1 5G is available */
#define FW_CAPINFO_EXT_5G_1X1_ONLY MBIT(0)
/** Ext fw cap info bit1 1x1 5G is not available */
#define FW_CAPINFO_EXT_NO_5G_1X1 MBIT(1)
/** Ext fw cap info bit 2 only 1x1 2G is available */
#define FW_CAPINFO_EXT_2G_1X1_ONLY MBIT(2)
/**Ext fw cap info bit3 1x1 2G is not available */
#define FW_CAPINFO_EXT_NO_2G_1X1 MBIT(3)
/** Ext fw cap info bit4 1x1 + 1x1 5G mode is unavailable */
#define FW_CAPINFO_EXT_NO_5G_1X1_PLUS_1X1 MBIT(4)
/** Ext fw cap info bit5 80 + 80 MHz capability disabled */
#define FW_CAPINFO_EXT_NO_80MHz_PLUS_80MHz MBIT(5)
/** Ext fw cap info bit6 1024 QAM is disabled */
#define FW_CAPINFO_EXT_NO_1024_QAM MBIT(6)
/** FW cap info bit 7 11AX */
#define FW_CAPINFO_EXT_802_11AX MBIT(7)
/** FW cap info bit 8: 80MHZ disabled */
#define FW_CAPINFO_EXT_NO_80MHZ MBIT(8)
/** FW cap info bit 9: Multi BSSID Support */
#define FW_CAPINFO_EXT_MULTI_BSSID MBIT(9)
/** FW cap info bit 10: Beacon Protection Support */
#define FW_CAPINFO_EXT_BEACON_PROT MBIT(10)

/** Check if 5G 1x1 only is supported by firmware */
#define IS_FW_SUPPORT_5G_1X1_ONLY(_adapter)                                    \
	(_adapter->fw_cap_ext & FW_CAPINFO_EXT_5G_1X1_ONLY)
/** Check if 5G 1x1 is unavailable in firmware */
#define IS_FW_SUPPORT_NO_5G_1X1(_adapter)                                      \
	(_adapter->fw_cap_ext & FW_CAPINFO_EXT_NO_5G_1X1)
/** Check if 2G 1x1 only is supported by firmware */
#define IS_FW_SUPPORT_2G_1X1_ONLY(_adapter)                                    \
	(_adapter->fw_cap_ext & FW_CAPINFO_EXT_2G_1X1_ONLY)
/** Check if 2G 1x1 is unavailable in firmware */
#define IS_FW_SUPPORT_NO_2G_1X1(_adapter)                                      \
	(_adapter->fw_cap_ext & FW_CAPINFO_EXT_NO_2G_1X1)
/** Check if 5G 1x1 + 1x1 mode is disabled in firmware */
#define IS_FW_SUPPORT_NO_5G_1X1_PLUS_1X1(_adapter)                             \
	(_adapter->fw_cap_ext & FW_CAPINFO_EXT_NO_5G_1X1_PLUS_1X1)
/** Check if 80 + 80MHz is disabled in firmware */
#define IS_FW_SUPPORT_NO_80MHz_PLUS_80MHz(_adapter)                            \
	(_adapter->fw_cap_ext & FW_CAPINFO_EXT_NO_80MHz_PLUS_80MHz)
/** Check if 1024 QAM disabled in firmware */
#define IS_FW_SUPPORT_NO_1024_QAM(_adapter)                                    \
	(_adapter->fw_cap_ext & FW_CAPINFO_EXT_NO_1024_QAM)
/** Check if 80MHZ disabled in firmware */
#define IS_FW_SUPPORT_NO_80MHZ(_adapter)                                       \
	(_adapter->fw_cap_ext & FW_CAPINFO_EXT_NO_80MHZ)
/** Check if Multi BSSID supported by firmware */
#define IS_FW_SUPPORT_MULTIBSSID(_adapter)                                     \
	(_adapter->fw_cap_ext & FW_CAPINFO_EXT_MULTI_BSSID)
/** Check if Beacon Protection supported by firmware */
#define IS_FW_SUPPORT_BEACON_PROT(_adapter)                                     \
	(_adapter->fw_cap_ext & FW_CAPINFO_EXT_BEACON_PROT)

/** MrvlIEtypes_PrevBssid_t */
typedef MLAN_PACK_START struct _MrvlIEtypes_PrevBssid_t {
	/** Header */
	MrvlIEtypesHeader_t header;
	/** prev_bssid **/
	t_u8 prev_bssid[6];
} MLAN_PACK_END MrvlIEtypes_PrevBssid_t;

/** FW cap info TLV */
typedef MLAN_PACK_START struct _MrvlIEtypes_fw_cap_info_t {
	/** Header type */
	t_u16 type;
	/** Header length */
	t_u16 len;
	/** Fw cap info bitmap */
	t_u32 fw_cap_info;
	/** Extended fw cap info bitmap */
	t_u32 fw_cap_ext;
} MLAN_PACK_END MrvlIEtypes_fw_cap_info_t, *pMrvlIEtypes_fw_cap_info_t;

/** Check if 11AX is supported by firmware */
#define IS_FW_SUPPORT_11AX(_adapter)                                           \
	(_adapter->fw_cap_ext & FW_CAPINFO_EXT_802_11AX)

typedef MLAN_PACK_START struct _MrvlIEtypes_Extension_t {
	/** Header type */
	t_u16 type;
	/** Header length */
	t_u16 len;
	/** Element id extension */
	t_u8 ext_id;
	/** payload */
	t_u8 data[1];
} MLAN_PACK_END MrvlIEtypes_Extension_t, *pMrvlIEtypes_Extension_t;

/* HE MAC Capabilities Information field BIT 1 for TWT Req */
#define HE_MAC_CAP_TWT_REQ_SUPPORT MBIT(1)
/* HE MAC Capabilities Information field BIT 2 for TWT Resp*/
#define HE_MAC_CAP_TWT_RESP_SUPPORT MBIT(2)
typedef MLAN_PACK_START struct _MrvlIEtypes_He_cap_t {
	/** Header type */
	t_u16 type;
	/** Header length */
	t_u16 len;
	/** Element id extension */
	t_u8 ext_id;
	/** he mac capability info */
	t_u8 he_mac_cap[6];
	/** he phy capability info */
	t_u8 he_phy_cap[11];
    /** rx mcs for 80 */
	t_u16 rx_mcs_80;
    /** tx mcs for 80 */
	t_u16 tx_mcs_80;
    /** rx mcs for bw 160 */
	t_u16 rx_mcs_160;
    /** tx mcs for bw 160 */
	t_u16 tx_mcs_160;
    /** rx mcs for bw 80+80 */
	t_u16 rx_mcs_80p80;
    /** tx mcs for bw 80+80 */
	t_u16 tx_mcs_80p80;
	/** PPE Thresholds (optional) */
	t_u8 val[20];
} MLAN_PACK_END MrvlIEtypes_He_cap_t, *pMrvlIEtypes_he_cap_t;

#ifdef RX_PACKET_COALESCE
/** Host Command ID : Rx packet coalescing configuration */
#define HostCmd_CMD_RX_PKT_COALESCE_CFG 0x012c
#endif

/** Host Command ID : Extended scan support */
#define HostCmd_CMD_802_11_SCAN_EXT 0x0107

/** Host Command ID : Forward mgmt frame */
#define HostCmd_CMD_RX_MGMT_IND 0x010c

#ifdef PCIE
/** Host Command ID: Host buffer description */
#define HostCmd_CMD_PCIE_HOST_BUF_DETAILS 0x00fa
#endif

/** Host Command ID : Set BSS_MODE */
#define HostCmd_CMD_SET_BSS_MODE 0x00f7

#ifdef UAP_SUPPORT
/**  Host Command id: SYS_INFO */
#define HOST_CMD_APCMD_SYS_INFO 0x00ae
/** Host Command id: sys_reset */
#define HOST_CMD_APCMD_SYS_RESET 0x00af
/** Host Command id: SYS_CONFIGURE  */
#define HOST_CMD_APCMD_SYS_CONFIGURE 0x00b0
/** Host Command id: BSS_START */
#define HOST_CMD_APCMD_BSS_START 0x00b1
/** Host Command id: BSS_STOP  */
#define HOST_CMD_APCMD_BSS_STOP 0x00b2
/** Host Command id: sta_list */
#define HOST_CMD_APCMD_STA_LIST 0x00b3
/** Host Command id: STA_DEAUTH */
#define HOST_CMD_APCMD_STA_DEAUTH 0x00b5

/** Host Command id: REPORT_MIC */
#define HOST_CMD_APCMD_REPORT_MIC 0x00ee
/** Host Command id: UAP_OPER_CTRL */
#define HOST_CMD_APCMD_OPER_CTRL 0x0233
#endif /* UAP_SUPPORT */

/** Host Command id: PMIC CONFIGURE*/
#define HOST_CMD_PMIC_CONFIGURE 0x23E

/** Host Command ID: Tx data pause */
#define HostCmd_CMD_CFG_TX_DATA_PAUSE 0x0103

#ifdef WIFI_DIRECT_SUPPORT
/** Host Command ID: P2P PARAMS CONFIG */
#define HOST_CMD_P2P_PARAMS_CONFIG 0x00ea
/** Host Command ID: WIFI_DIRECT_MODE_CONFIG */
#define HOST_CMD_WIFI_DIRECT_MODE_CONFIG 0x00eb
#endif

/** Host Command ID: GPIO TSF LATCH */
#define HOST_CMD_GPIO_TSF_LATCH_PARAM_CONFIG 0x0278
/** Host Command ID: Remain On Channel */
#define HostCmd_CMD_802_11_REMAIN_ON_CHANNEL 0x010d

#define HostCmd_CMD_COALESCE_CFG 0x010a

/** Host Command ID: GTK REKEY OFFLOAD CFG */
#define HostCmd_CMD_GTK_REKEY_OFFLOAD_CFG 0x010f

/** Host Command ID : OTP user data */
#define HostCmd_CMD_OTP_READ_USER_DATA 0x0114

/** Host Command ID: HS wakeup reason */
#define HostCmd_CMD_HS_WAKEUP_REASON 0x0116

/** Host Command ID: reject addba request */
#define HostCmd_CMD_REJECT_ADDBA_REQ 0x0119

#define HostCmd_CMD_FW_DUMP_EVENT 0x0125

#define HostCMD_CONFIG_LOW_POWER_MODE 0x0128

/** Host Command ID : Target device access */
#define HostCmd_CMD_TARGET_ACCESS 0x012a

/** Host Command ID: BCA device access */
#define HostCmd_CMD_BCA_REG_ACCESS 0x0272

/** Host Command ID: DFS repeater mode */
#define HostCmd_DFS_REPEATER_MODE 0x012b

/** Host Command ID: ACS scan */
#define HostCMD_APCMD_ACS_SCAN 0x0224

/** Host Command ID: Get sensor temp*/
#define HostCmd_DS_GET_SENSOR_TEMP 0x0227

/** Host Command ID : Configure ADHOC_OVER_IP parameters */
#define HostCmd_CMD_WMM_PARAM_CONFIG 0x023a

#define HostCmd_CMD_IPV6_RA_OFFLOAD_CFG 0x0238

#ifdef STA_SUPPORT
/** Host Command ID :  set/get sta configure */
#define HostCmd_CMD_STA_CONFIGURE 0x023f
#endif

/** Host Command ID : GPIO independent reset configure */
#define HostCmd_CMD_INDEPENDENT_RESET_CFG 0x0243

#if defined(PCIE9098) || defined(SD9098) || defined(USB9098) || defined(PCIE9097) || defined(USB9097) || defined(SD9097)
/* TLV type: reg type */
#define TLV_TYPE_REG_ACCESS_CTRL (PROPRIETARY_TLV_BASE_ID + 0x13C)	/* 0x023c */
/** MrvlIEtypes_Reg_type_t*/
typedef MLAN_PACK_START struct _MrvlIEtypes_Reg_type_t {
	/** Header */
	MrvlIEtypesHeader_t header;
	/** type: 0x81/0x82/0x83 */
	t_u8 type;
} MLAN_PACK_END MrvlIEtypes_Reg_type_t;

#endif
/** use to query chan region cfg setting in firmware */
#define HostCmd_CMD_CHAN_REGION_CFG 0x0242
/** used in hostcmd to download region power cfg setting to firmware */
#define HostCmd_CMD_REGION_POWER_CFG 0x0249

/* mod_grp */
typedef enum _mod_grp {
	MOD_CCK,		// 0
	MOD_OFDM_PSK,		// 1
	MOD_OFDM_QAM16,		// 2
	MOD_OFDM_QAM64,		// 3
	MOD_HT_20_PSK,		// 4
	MOD_HT_20_QAM16,	// 5
	MOD_HT_20_QAM64,	// 6
	MOD_HT_40_PSK,		// 7
	MOD_HT_40_QAM16,	// 8
	MOD_HT_40_QAM64,	// 9
#ifdef STREAM_2x2
	MOD_HT2_20_PSK,		// 10
	MOD_HT2_20_QAM16,	// 11
	MOD_HT2_20_QAM64,	// 12
	MOD_HT2_40_PSK,		// 13
	MOD_HT2_40_QAM16,	// 14
	MOD_HT2_40_QAM64,	// 15
#endif

	MOD_VHT_20_QAM256,	// 16
	MOD_VHT_40_QAM256,	// 17
	MOD_VHT_80_PSK,		// 18
	MOD_VHT_80_QAM16,	// 19
	MOD_VHT_80_QAM64,	// 20
	MOD_VHT_80_QAM256,	// 21
#ifdef STREAM_2x2
	MOD_VHT2_20_QAM256,	// 22
	MOD_VHT2_40_QAM256,	// 23
	MOD_VHT2_80_PSK,	// 24
	MOD_VHT2_80_QAM16,	// 25
	MOD_VHT2_80_QAM64,	// 26
	MOD_VHT2_80_QAM256,	// 27
#endif
} mod_grp;

typedef MLAN_PACK_START struct _power_table_attr {
	t_u8 rows_2g;
	t_u8 cols_2g;
	t_u8 rows_5g;
	t_u8 cols_5g;
} MLAN_PACK_END power_table_attr_t;

#define FW_CFP_TABLE_MAX_ROWS_BG 14
#define FW_CFP_TABLE_MAX_COLS_BG 17

#define FW_CFP_TABLE_MAX_ROWS_A 39
#define FW_CFP_TABLE_MAX_COLS_A 29

#define HostCmd_CMD_DYN_BW 0x0252

#define HostCmd_CMD_BOOT_SLEEP 0x0258

#define HostCmd_CMD_RX_ABORT_CFG 0x0261
#define HostCmd_CMD_RX_ABORT_CFG_EXT 0x0262
#define HostCmd_CMD_TX_AMPDU_PROT_MODE 0x0263
#define HostCmd_CMD_RATE_ADAPT_CFG 0x0264
#define HostCmd_CMD_CCK_DESENSE_CFG 0x0265

#define HostCmd_CMD_VDLL 0x0240
#if defined(PCIE)
#define HostCmd_CMD_SSU 0x0259
#endif

#define HostCmd_CMD_DMCS_CONFIG 0x0260

/** Host Command ID: 11AX config */
#define HostCmd_CMD_11AX_CFG 0x0266
/** Host Command ID: 11AX command */
#define HostCmd_CMD_11AX_CMD 0x026d
/** Host Command ID: Range ext command */
#define HostCmd_CMD_RANGE_EXT 0x0274
/** Host Command ID: TWT cfg command */
#define HostCmd_CMD_TWT_CFG 0x0270

#define HostCmd_CMD_LOW_POWER_MODE_CFG 0x026e
#define HostCmd_CMD_UAP_BEACON_STUCK_CFG    0x0271
#define HostCmd_CMD_ARB_CONFIG 0x0273
#define HostCmd_CMD_DOT11MC_UNASSOC_FTM_CFG 0x0275
#define HostCmd_CMD_HAL_PHY_CFG 0x0276

/** Enhanced PS modes */
typedef enum _ENH_PS_MODES {
	GET_PS = 0,
	SLEEP_CONFIRM = 5,
	DIS_AUTO_PS = 0xfe,
	EN_AUTO_PS = 0xff,
} ENH_PS_MODES;

/** Command RET code, MSB is set to 1 */
#define HostCmd_RET_BIT 0x8000

/** General purpose action : Get */
#define HostCmd_ACT_GEN_GET 0x0000
/** General purpose action : Set */
#define HostCmd_ACT_GEN_SET 0x0001
/** General purpose action : Set Default */
#define HostCmd_ACT_GEN_SET_DEFAULT 0x0002
/** General purpose action : Get_Current */
#define HostCmd_ACT_GEN_GET_CURRENT 0x0003
/** General purpose action : Remove */
#define HostCmd_ACT_GEN_REMOVE 0x0004
/** General purpose action : Reset */
#define HostCmd_ACT_GEN_RESET 0x0005

/** Host command action : Set Rx */
#define HostCmd_ACT_SET_RX 0x0001
/** Host command action : Set Tx */
#define HostCmd_ACT_SET_TX 0x0002
/** Host command action : Set both Rx and Tx */
#define HostCmd_ACT_SET_BOTH 0x0003
/** Host command action : Get Rx */
#define HostCmd_ACT_GET_RX 0x0004
/** Host command action : Get Tx */
#define HostCmd_ACT_GET_TX 0x0008
/** Host command action : Get both Rx and Tx */
#define HostCmd_ACT_GET_BOTH 0x000c

/** General Result Code*/
/** General result code OK */
#define HostCmd_RESULT_OK 0x0000
/** Genenral error */
#define HostCmd_RESULT_ERROR 0x0001
/** Command is not valid */
#define HostCmd_RESULT_NOT_SUPPORT 0x0002
/** Command is pending */
#define HostCmd_RESULT_PENDING 0x0003
/** System is busy (command ignored) */
#define HostCmd_RESULT_BUSY 0x0004
/** Data buffer is not big enough */
#define HostCmd_RESULT_PARTIAL_DATA 0x0005

/* Define action or option for HostCmd_CMD_MAC_CONTROL */
/** MAC action : Rx on */
#define HostCmd_ACT_MAC_RX_ON 0x0001
/** MAC action : Tx on */
#define HostCmd_ACT_MAC_TX_ON 0x0002
/** MAC action : WEP enable */
#define HostCmd_ACT_MAC_WEP_ENABLE 0x0008
/** MAC action : EthernetII enable */
#define HostCmd_ACT_MAC_ETHERNETII_ENABLE 0x0010
/** MAC action : Promiscous mode enable */
#define HostCmd_ACT_MAC_PROMISCUOUS_ENABLE 0x0080
/** MAC action : All multicast enable */
#define HostCmd_ACT_MAC_ALL_MULTICAST_ENABLE 0x0100
/** MAC action : RTS/CTS enable */
#define HostCmd_ACT_MAC_RTS_CTS_ENABLE 0x0200
/** MAC action : Strict protection enable */
#define HostCmd_ACT_MAC_STRICT_PROTECTION_ENABLE 0x0400
/** MAC action : Force 11n protection disable */
#define HostCmd_ACT_MAC_FORCE_11N_PROTECTION_OFF 0x0800
/** MAC action : Ad-Hoc G protection on */
#define HostCmd_ACT_MAC_ADHOC_G_PROTECTION_ON 0x2000
/** MAC action : Static-Dynamic BW enable */
#define HostCmd_ACT_MAC_STATIC_DYNAMIC_BW_ENABLE MBIT(16)
/** MAC action : Dynamic BW */
#define HostCmd_ACT_MAC_DYNAMIC_BW MBIT(17)

/* Define action or option for HostCmd_CMD_802_11_SCAN */
/** Scan type : BSS */
#define HostCmd_BSS_MODE_BSS 0x0001
/** Scan type : IBSS */
#define HostCmd_BSS_MODE_IBSS 0x0002
/** Scan type : Any */
#define HostCmd_BSS_MODE_ANY 0x0003

/** Define bitmap conditions for HOST_SLEEP_CFG : GPIO FF */
#define HOST_SLEEP_CFG_GPIO_FF 0xff
/** Define bitmap conditions for HOST_SLEEP_CFG : GAP FF */
#define HOST_SLEEP_CFG_GAP_FF 0xff

/** Buffer Constants */
/** Number of command buffers */
#define MRVDRV_NUM_OF_CMD_BUFFER 40
/** Maximum number of BSS Descriptors */
#define MRVDRV_MAX_BSSID_LIST 200

/** Host command flag in command */
#define CMD_F_HOSTCMD (1 << 0)
/** command cancel flag in command */
#define CMD_F_CANCELED (1 << 1)
/** scan command flag */
#define CMD_F_SCAN (1 << 2)

/** Host Command ID bit mask (bit 11:0) */
#define HostCmd_CMD_ID_MASK 0x0fff

/** Host Command Sequence number mask (bit 7:0) */
#define HostCmd_SEQ_NUM_MASK 0x00ff

/** Host Command BSS number mask (bit 11:8) */
#define HostCmd_BSS_NUM_MASK 0x0f00

/** Host Command BSS type mask (bit 15:12) */
#define HostCmd_BSS_TYPE_MASK 0xf000

/** Set BSS information to Host Command */
#define HostCmd_SET_SEQ_NO_BSS_INFO(seq, num, type)                            \
	((((seq)&0x00ff) | (((num)&0x000f) << 8)) | (((type)&0x000f) << 12))

/** Get Sequence Number from Host Command (bit 7:0) */
#define HostCmd_GET_SEQ_NO(seq) ((seq)&HostCmd_SEQ_NUM_MASK)

/** Get BSS number from Host Command (bit 11:8) */
#define HostCmd_GET_BSS_NO(seq) (((seq)&HostCmd_BSS_NUM_MASK) >> 8)

/** Get BSS type from Host Command (bit 15:12) */
#define HostCmd_GET_BSS_TYPE(seq) (((seq)&HostCmd_BSS_TYPE_MASK) >> 12)

/** Card Event definition : Dummy host wakeup signal */
#define EVENT_DUMMY_HOST_WAKEUP_SIGNAL 0x00000001
/** Card Event definition : Link lost */
#define EVENT_LINK_LOST 0x00000003
/** Card Event definition : Link sensed */
#define EVENT_LINK_SENSED 0x00000004
/** Card Event definition : MIB changed */
#define EVENT_MIB_CHANGED 0x00000006
/** Card Event definition : Init done */
#define EVENT_INIT_DONE 0x00000007
/** Card Event definition : Deauthenticated */
#define EVENT_DEAUTHENTICATED 0x00000008
/** Card Event definition : Disassociated */
#define EVENT_DISASSOCIATED 0x00000009
/** Card Event definition : Power save awake */
#define EVENT_PS_AWAKE 0x0000000a
/** Card Event definition : Power save sleep */
#define EVENT_PS_SLEEP 0x0000000b
/** Card Event definition : MIC error multicast */
#define EVENT_MIC_ERR_MULTICAST 0x0000000d
/** Card Event definition : MIC error unicast */
#define EVENT_MIC_ERR_UNICAST 0x0000000e

/** Card Event definition : Ad-Hoc BCN lost */
#define EVENT_ADHOC_BCN_LOST 0x00000011

/** Card Event definition : Stop Tx */
#define EVENT_STOP_TX 0x00000013
/** Card Event definition : Start Tx */
#define EVENT_START_TX 0x00000014
/** Card Event definition : Channel switch */
#define EVENT_CHANNEL_SWITCH 0x00000015

/** Card Event definition : MEAS report ready */
#define EVENT_MEAS_REPORT_RDY 0x00000016

/** Card Event definition : WMM status change */
#define EVENT_WMM_STATUS_CHANGE 0x00000017

/** Card Event definition : BG scan report */
#define EVENT_BG_SCAN_REPORT 0x00000018
/** Card Event definition : BG scan stopped */
#define EVENT_BG_SCAN_STOPPED 0x00000065

/** Card Event definition : Beacon RSSI low */
#define EVENT_RSSI_LOW 0x00000019
/** Card Event definition : Beacon SNR low */
#define EVENT_SNR_LOW 0x0000001a
/** Card Event definition : Maximum fail */
#define EVENT_MAX_FAIL 0x0000001b
/** Card Event definition : Beacon RSSI high */
#define EVENT_RSSI_HIGH 0x0000001c
/** Card Event definition : Beacon SNR high */
#define EVENT_SNR_HIGH 0x0000001d

/** Card Event definition : IBSS coalsced */
#define EVENT_IBSS_COALESCED 0x0000001e

/** Event definition : IBSS station connected */
#define EVENT_IBSS_STATION_CONNECT 0x00000020
/** Event definition : IBSS station dis-connected */
#define EVENT_IBSS_STATION_DISCONNECT 0x00000021

/** Card Event definition : Data RSSI low */
#define EVENT_DATA_RSSI_LOW 0x00000024
/** Card Event definition : Data SNR low */
#define EVENT_DATA_SNR_LOW 0x00000025
/** Card Event definition : Data RSSI high */
#define EVENT_DATA_RSSI_HIGH 0x00000026
/** Card Event definition : Data SNR high */
#define EVENT_DATA_SNR_HIGH 0x00000027

/** Card Event definition : Link Quality */
#define EVENT_LINK_QUALITY 0x00000028

/** Card Event definition : Port release event */
#define EVENT_PORT_RELEASE 0x0000002b

/** Card Event definition : Pre-Beacon Lost */
#define EVENT_PRE_BEACON_LOST 0x00000031

#define EVENT_WATCHDOG_TMOUT            0x00000032

/** Card Event definition : Add BA event */
#define EVENT_ADDBA 0x00000033
/** Card Event definition : Del BA event */
#define EVENT_DELBA 0x00000034
/** Card Event definition: BA stream timeout*/
#define EVENT_BA_STREAM_TIMEOUT 0x00000037

/** Card Event definition : AMSDU aggr control */
#define EVENT_AMSDU_AGGR_CTRL 0x00000042

/** Card Event definition: WEP ICV error */
#define EVENT_WEP_ICV_ERR 0x00000046

/** Card Event definition : Host sleep enable */
#define EVENT_HS_ACT_REQ 0x00000047

/** Card Event definition : BW changed */
#define EVENT_BW_CHANGE 0x00000048

#ifdef WIFI_DIRECT_SUPPORT
/** WIFIDIRECT generic event */
#define EVENT_WIFIDIRECT_GENERIC_EVENT 0x00000049
/** WIFIDIRECT service discovery event */
#define EVENT_WIFIDIRECT_SERVICE_DISCOVERY 0x0000004a
#endif
/** Remain on Channel expired event */
#define EVENT_REMAIN_ON_CHANNEL_EXPIRED 0x0000005f

/** TDLS generic event */
#define EVENT_TDLS_GENERIC_EVENT 0x00000052

#define EVENT_MEF_HOST_WAKEUP 0x0000004f

/** Card Event definition: Channel switch pending announcment */
#define EVENT_CHANNEL_SWITCH_ANN 0x00000050

/** Event definition:  Radar Detected by card */
#define EVENT_RADAR_DETECTED 0x00000053

/** Event definition:  Radar Detected by card */
#define EVENT_CHANNEL_REPORT_RDY 0x00000054

/** Event definition:  Scan results through event */
#define EVENT_EXT_SCAN_REPORT 0x00000058
/** Enhance ext scan done event */
#define EVENT_EXT_SCAN_STATUS_REPORT 0x0000007f

/** Event definition : FW debug information */
#define EVENT_FW_DEBUG_INFO 0x00000063

/** Event definition: RXBA_SYNC */
#define EVENT_RXBA_SYNC 0x00000059

#ifdef UAP_SUPPORT
/** Event ID: STA deauth */
#define EVENT_MICRO_AP_STA_DEAUTH 0x0000002c
/** Event ID: STA assoicated */
#define EVENT_MICRO_AP_STA_ASSOC 0x0000002d
/** Event ID: BSS started */
#define EVENT_MICRO_AP_BSS_START 0x0000002e
/** Event ID: BSS idle event */
#define EVENT_MICRO_AP_BSS_IDLE 0x00000043
/** Event ID: BSS active event */
#define EVENT_MICRO_AP_BSS_ACTIVE 0x00000044

/** Event ID: MIC countermeasures event */
#define EVENT_MICRO_AP_MIC_COUNTERMEASURES 0x0000004c
#endif /* UAP_SUPPORT */

/** Event ID: TX data pause event */
#define EVENT_TX_DATA_PAUSE 0x00000055

/** Event ID: SAD Report */
#define EVENT_SAD_REPORT 0x00000066

/** Event ID: Tx status */
#define EVENT_TX_STATUS_REPORT 0x00000074

#define EVENT_BT_COEX_WLAN_PARA_CHANGE 0x00000076

#if defined(PCIE)
#define EVENT_SSU_DUMP_DMA 0x0000008C
#endif

#define EVENT_VDLL_IND 0x00000081
#define EVENT_EXCEED_MAX_P2P_CONN 0x00000089

/** Card Event definition : RESET PN */

#define EVENT_FW_HANG_REPORT 0x0000008F

#define EVENT_FW_DUMP_INFO 0x00000073
/** Event ID mask */
#define EVENT_ID_MASK 0xffff

/** BSS number mask */
#define BSS_NUM_MASK 0xf

/** Get BSS number from event cause (bit 23:16) */
#define EVENT_GET_BSS_NUM(event_cause) (((event_cause) >> 16) & BSS_NUM_MASK)

/** Get BSS type from event cause (bit 31:24) */
#define EVENT_GET_BSS_TYPE(event_cause) (((event_cause) >> 24) & 0x00ff)

/** event type for tdls setup failure */
#define TDLS_EVENT_TYPE_SETUP_FAILURE 1
/** event type for tdls setup request received */
#define TDLS_EVENT_TYPE_SETUP_REQ 2
/** event type for tdls link torn down */
#define TDLS_EVENT_TYPE_LINK_TORN_DOWN 3
/** event type for tdls link established */
#define TDLS_EVENT_TYPE_LINK_ESTABLISHED 4
/** event type for tdls debug */
#define TDLS_EVENT_TYPE_DEBUG 5
/** event type for tdls packet */
#define TDLS_EVENT_TYPE_PACKET 6
/** event type for channel switch result */
#define TDLS_EVENT_TYPE_CHAN_SWITCH_RESULT 7
/** event type for start channel switch */
#define TDLS_EVENT_TYPE_START_CHAN_SWITCH 8
/** event type for stop channel switch */
#define TDLS_EVENT_TYPE_CHAN_SWITCH_STOPPED 9

/** Packet received on direct link */
#define RXPD_FLAG_PKT_DIRECT_LINK MBIT(0)
/** TDLS base channel */
#define TDLS_BASE_CHANNEL 0
/** TDLS off channel */
#define TDLS_OFF_CHANNEL 1

/** structure for channel switch result from TDLS FW */
typedef MLAN_PACK_START struct _chan_switch_result {
	/** current channel, 0 - base channel, 1 - off channel*/
	t_u8 current_channel;
	/** channel switch status*/
	t_u8 status;
	/** channel switch fauilure reason code*/
	t_u8 reason;
} MLAN_PACK_END chan_switch_result;

typedef MLAN_PACK_START struct _ie_data {
	/** IE Length */
	t_u16 ie_length;
	/** IE pointer */
	t_u8 ie_ptr[1];
} MLAN_PACK_END tdls_ie_data;

/** Event structure for generic events from TDLS FW */
typedef MLAN_PACK_START struct _Event_tdls_generic {
	/** Event Type */
	t_u16 event_type;
	/** Peer mac address */
	t_u8 peer_mac_addr[MLAN_MAC_ADDR_LENGTH];
	union {
		/** channel switch result structure*/
		chan_switch_result switch_result;
		/** channel switch stop reason*/
		t_u8 cs_stop_reason;
		/** Reason code */
		t_u16 reason_code;
		/** IE data */
		tdls_ie_data ie_data;
	} u;
} MLAN_PACK_END Event_tdls_generic;

typedef enum _tdls_error_code_e {
	NO_ERROR = 0,
	INTERNAL_ERROR,
	MAX_TDLS_LINKS_EST,
	TDLS_LINK_EXISTS,
	TDLS_LINK_NONEXISTENT,
	TDLS_PEER_STA_UNREACHABLE = 25,
} tdls_error_code_e;

/** Event_WEP_ICV_ERR structure */
typedef MLAN_PACK_START struct _Event_WEP_ICV_ERR {
	/** Reason code */
	t_u16 reason_code;
	/** Source MAC address */
	t_u8 src_mac_addr[MLAN_MAC_ADDR_LENGTH];
	/** WEP decryption used key */
	t_u8 wep_key_index;
	/** WEP key length */
	t_u8 wep_key_length;
	/** WEP key */
	t_u8 key[MAX_WEP_KEY_SIZE];
} MLAN_PACK_END Event_WEP_ICV_ERR;

/** WLAN_802_11_FIXED_IEs */
typedef MLAN_PACK_START struct _WLAN_802_11_FIXED_IEs {
	/** Timestamp */
	t_u8 time_stamp[8];
	/** Beacon interval */
	t_u16 beacon_interval;
	/** Capabilities*/
	t_u16 capabilities;
} MLAN_PACK_END WLAN_802_11_FIXED_IEs;

/** WLAN_802_11_VARIABLE_IEs */
typedef MLAN_PACK_START struct _WLAN_802_11_VARIABLE_IEs {
	/** Element ID */
	t_u8 element_id;
	/** Length */
	t_u8 length;
	/** IE data */
	t_u8 data[1];
} MLAN_PACK_END WLAN_802_11_VARIABLE_IEs;

/** TLV related data structures*/
/*TDLS TIMEOUT VALUE (seconds)*/
#define TDLS_IDLE_TIMEOUT 60
/** MrvlIEtypes_Data_t */
typedef MLAN_PACK_START struct _MrvlIEtypes_TDLS_Idle_Timeout_t {
	/** Header */
	MrvlIEtypesHeader_t header;
	/** value */
	t_u16 value;
} MLAN_PACK_END MrvlIEtypes_TDLS_Idle_Timeout_t;
#if defined(STA_SUPPORT)
/** Pairwise Cipher Suite length */
#define PAIRWISE_CIPHER_SUITE_LEN 4
/** AKM Suite length */
#define AKM_SUITE_LEN 4
/** MFPC bit in RSN capability */
#define MFPC_BIT 7
/** MFPR bit in RSN capability */
#define MFPR_BIT 6
#endif
/** Bit mask for TxPD status field for null packet */
#define MRVDRV_TxPD_POWER_MGMT_NULL_PACKET 0x01
/** Bit mask for TxPD status field for last packet */
#define MRVDRV_TxPD_POWER_MGMT_LAST_PACKET 0x08

/** Bit mask for TxPD flags field for TDLS packet */
#define MRVDRV_TxPD_FLAGS_TDLS_PACKET MBIT(4)

/** Bit mask for TxPD flags field for Tx status report */
#define MRVDRV_TxPD_FLAGS_TX_PACKET_STATUS MBIT(5)

/** Packet type: 802.11 */
#define PKT_TYPE_802DOT11 0x05

#define PKT_TYPE_MGMT_FRAME 0xE5
/** Packet type: AMSDU */
#define PKT_TYPE_AMSDU 0xE6
/** Packet type: BAR */
#define PKT_TYPE_BAR 0xE7

/** Packet type: debugging */
#define PKT_TYPE_DEBUG 0xEF

/** channel number at bit 5-13 */
#define RXPD_CHAN_MASK 0x3FE0
/** Rate control mask  15-23 */
#define TXPD_RATE_MASK 0xff8000
/** enable bw ctrl in TxPD */
#define TXPD_BW_ENABLE MBIT(20)
/** enable tx power ctrl in TxPD */
#define TXPD_TXPW_ENABLE MBIT(7)
/** sign of power */
#define TXPD_TXPW_NEGATIVE MBIT(6)
/** Enable Rate ctrl in TxPD */
#define TXPD_TXRATE_ENABLE MBIT(15)
/** enable retry limit in TxPD */
#define TXPD_RETRY_ENABLE MBIT(12)

/** TxPD descriptor */
typedef MLAN_PACK_START struct _TxPD {
	/** BSS type */
	t_u8 bss_type;
	/** BSS number */
	t_u8 bss_num;
	/** Tx packet length */
	t_u16 tx_pkt_length;
	/** Tx packet offset */
	t_u16 tx_pkt_offset;
	/** Tx packet type */
	t_u16 tx_pkt_type;
	/** Tx Control */
	t_u32 tx_control;
	/** Pkt Priority */
	t_u8 priority;
	/** Transmit Pkt Flags*/
	t_u8 flags;
	/** Amount of time the packet has been queued
	 * in the driver (units = 2ms)*/
	t_u8 pkt_delay_2ms;
	/** reserved */
	t_u8 reserved;
	/** Tx Control */
	t_u32 tx_control_1;
} MLAN_PACK_END TxPD, *PTxPD;

/** RxPD Descriptor */
typedef MLAN_PACK_START struct _RxPD {
	/** BSS type */
	t_u8 bss_type;
	/** BSS number */
	t_u8 bss_num;
	/** Rx Packet Length */
	t_u16 rx_pkt_length;
	/** Rx Pkt offset */
	t_u16 rx_pkt_offset;
	/** Rx packet type */
	t_u16 rx_pkt_type;
	/** Sequence number */
	t_u16 seq_num;
	/** Packet Priority */
	t_u8 priority;
	/** Rx Packet Rate */
	t_u8 rx_rate;
	/** SNR */
	t_s8 snr;
	/** Noise Floor */
	t_s8 nf;
	/** [Bit 1] [Bit 0] RxRate format: legacy rate = 00 HT = 01 VHT = 10
	 *  [Bit 3] [Bit 2] HT/VHT Bandwidth BW20 = 00 BW40 = 01 BW80 = 10 BW160
	 * = 11 [Bit 4] HT/VHT Guard interval LGI = 0 SGI = 1 [Bit 5] STBC
	 * support Enabled = 1 [Bit 6] LDPC support Enabled = 1 [Bit 7] [Bit4,
	 * Bit7] AX Guard interval, 00, 01, 10 */
	t_u8 rate_info;
	/** Reserved */
	t_u8 reserved[3];
	/** TDLS flags, bit 0: 0=InfraLink, 1=DirectLink */
	t_u8 flags;
	/**For SD8887 antenna info: 0 = 2.4G antenna a; 1 = 2.4G antenna b; 3 =
	 * 5G antenna; 0xff = invalid value */
	t_u8 antenna;
	/* [31:0] ToA of the rx packet, [63:32] ToD of the ack for the rx packet
	 * Both ToA and ToD are in nanoseconds */
	t_u64 toa_tod_tstamps;
	/** rx info */
	t_u32 rx_info;

    /** Reserved */
	t_u8 reserved3[8];

} MLAN_PACK_END RxPD, *PRxPD;

/** IEEEtypes_FrameCtl_t*/
#ifdef BIG_ENDIAN_SUPPORT
typedef MLAN_PACK_START struct _IEEEtypes_FrameCtl_t {
	/** Order */
	t_u8 order:1;
	/** Wep */
	t_u8 wep:1;
	/** More Data */
	t_u8 more_data:1;
	/** Power Mgmt */
	t_u8 pwr_mgmt:1;
	/** Retry */
	t_u8 retry:1;
	/** More Frag */
	t_u8 more_frag:1;
	/** From DS */
	t_u8 from_ds:1;
	/** To DS */
	t_u8 to_ds:1;
	/** Sub Type */
	t_u8 sub_type:4;
	/** Type */
	t_u8 type:2;
	/** Protocol Version */
	t_u8 protocol_version:2;
} MLAN_PACK_END IEEEtypes_FrameCtl_t;
#else
typedef MLAN_PACK_START struct _IEEEtypes_FrameCtl_t {
	/** Protocol Version */
	t_u8 protocol_version:2;
	/** Type */
	t_u8 type:2;
	/** Sub Type */
	t_u8 sub_type:4;
	/** To DS */
	t_u8 to_ds:1;
	/** From DS */
	t_u8 from_ds:1;
	/** More Frag */
	t_u8 more_frag:1;
	/** Retry */
	t_u8 retry:1;
	/** Power Mgmt */
	t_u8 pwr_mgmt:1;
	/** More Data */
	t_u8 more_data:1;
	/** Wep */
	t_u8 wep:1;
	/** Order */
	t_u8 order:1;
} MLAN_PACK_END IEEEtypes_FrameCtl_t;
#endif

/** MrvlIETypes_MgmtFrameSet_t */
typedef MLAN_PACK_START struct _MrvlIETypes_MgmtFrameSet_t {
	/** Type */
	t_u16 type;
	/** Length */
	t_u16 len;
	/** Frame Control */
	IEEEtypes_FrameCtl_t frame_control;
	/* t_u8 frame_contents[]; */
} MLAN_PACK_END MrvlIETypes_MgmtFrameSet_t;

/** Beacon */
typedef MLAN_PACK_START struct _IEEEtypes_Beacon_t {
	/** time stamp */
	t_u8 time_stamp[8];
	/** beacon interval */
	t_u16 beacon_interval;
	/** cap info */
	t_u16 cap_info;
} MLAN_PACK_END IEEEtypes_Beacon_t;

/** Fixed size of station association event */
#define ASSOC_EVENT_FIX_SIZE 12

/** MrvlIEtypes_channel_band_t */
typedef MLAN_PACK_START struct _MrvlIEtypes_channel_band_t {
	/** Header */
	MrvlIEtypesHeader_t header;
	/** Band Configuration */
	Band_Config_t bandcfg;
	/** channel */
	t_u8 channel;
} MLAN_PACK_END MrvlIEtypes_channel_band_t;

#ifdef UAP_SUPPORT
/** IEEEtypes_AssocRqst_t */
typedef MLAN_PACK_START struct _IEEEtypes_AssocRqst_t {
	/** Capability Info */
	t_u16 cap_info;
	/** Listen Interval */
	t_u16 listen_interval;
	/* t_u8 ie_buffer[]; */
} MLAN_PACK_END IEEEtypes_AssocRqst_t;

/** IEEEtypes_ReAssocRqst_t */
typedef MLAN_PACK_START struct _IEEEtypes_ReAssocRqst_t {
	/** Capability Info */
	t_u16 cap_info;
	/** Listen Interval */
	t_u16 listen_interval;
	/** Current AP Address */
	t_u8 current_ap_addr[MLAN_MAC_ADDR_LENGTH];
	/* t_u8 ie_buffer[]; */
} MLAN_PACK_END IEEEtypes_ReAssocRqst_t;
#endif /* UAP_SUPPORT */

/** wlan_802_11_header */
typedef MLAN_PACK_START struct _wlan_802_11_header {
	/** Frame Control */
	t_u16 frm_ctl;
	/** Duration ID */
	t_u16 duration_id;
	/** Address1 */
	mlan_802_11_mac_addr addr1;
	/** Address2 */
	mlan_802_11_mac_addr addr2;
	/** Address3 */
	mlan_802_11_mac_addr addr3;
	/** Sequence Control */
	t_u16 seq_ctl;
	/** Address4 */
	mlan_802_11_mac_addr addr4;
} MLAN_PACK_END wlan_802_11_header;

/** wlan_802_11_header packet from FW with length */
typedef MLAN_PACK_START struct _wlan_mgmt_pkt {
	/** Packet Length */
	t_u16 frm_len;
	/** wlan_802_11_header */
	wlan_802_11_header wlan_header;
} MLAN_PACK_END wlan_mgmt_pkt;

#ifdef STA_SUPPORT
/** (Beaconsize(256)-5(IEId,len,contrystr(3))/3(FirstChan,NoOfChan,MaxPwr) */
#define MAX_NO_OF_CHAN 40

/** Channel-power table entries */
typedef MLAN_PACK_START struct _chan_power_11d {
	/** 11D channel */
	t_u8 chan;
	/** Band for channel */
	t_u8 band;
	/** 11D channel power */
	t_u8 pwr;
	/** AP seen on channel */
	t_u8 ap_seen;
} MLAN_PACK_END chan_power_11d_t;

/** Region channel info */
typedef MLAN_PACK_START struct _parsed_region_chan_11d {
	/** 11D channel power per channel */
	chan_power_11d_t chan_pwr[MAX_NO_OF_CHAN];
	/** 11D number of channels */
	t_u8 no_of_chan;
} MLAN_PACK_END parsed_region_chan_11d_t;
#endif /* STA_SUPPORT */

/** ChanScanMode_t */
typedef MLAN_PACK_START struct _ChanScanMode_t {
#ifdef BIG_ENDIAN_SUPPORT
	/** Reserved */
	t_u8 reserved_7:1;
	/** First passive scan then active scan */
	t_u8 passive_to_active_scan:1;
	/** First channel in scan */
	t_u8 first_chan:1;
	/** Enable hidden ssid report */
	t_u8 hidden_ssid_report:1;
	/** Enable probe response timeout */
	t_u8 rsp_timeout_en:1;
	/** Multidomain scan mode */
	t_u8 multidomain_scan:1;
	/** Disble channel filtering flag */
	t_u8 disable_chan_filt:1;
	/** Channel scan mode passive flag */
	t_u8 passive_scan:1;
#else
	/** Channel scan mode passive flag */
	t_u8 passive_scan:1;
	/** Disble channel filtering flag */
	t_u8 disable_chan_filt:1;
	/** Multidomain scan mode */
	t_u8 multidomain_scan:1;
	/** Enable probe response timeout */
	t_u8 rsp_timeout_en:1;
	/** Enable hidden ssid report */
	t_u8 hidden_ssid_report:1;
	/** First channel in scan */
	t_u8 first_chan:1;
	/** First passive scan then active scan */
	t_u8 passive_to_active_scan:1;
	/** Reserved */
	t_u8 reserved_7:1;
#endif
} MLAN_PACK_END ChanScanMode_t;

/** ChanScanParamSet_t */
typedef MLAN_PACK_START struct _ChanScanParamSet_t {
	/** Channel scan parameter : band config */
	Band_Config_t bandcfg;
	/** Channel scan parameter : Channel number */
	t_u8 chan_number;
	/** Channel scan parameter : Channel scan mode */
	ChanScanMode_t chan_scan_mode;
	/** Channel scan parameter : Minimum scan time */
	t_u16 min_scan_time;
	/** Channel scan parameter : Maximum scan time */
	t_u16 max_scan_time;
} MLAN_PACK_END ChanScanParamSet_t;

/** MrvlIEtypes_ChanListParamSet_t */
typedef MLAN_PACK_START struct _MrvlIEtypes_ChanListParamSet_t {
	/** Header */
	MrvlIEtypesHeader_t header;
	/** Channel scan parameters */
	ChanScanParamSet_t chan_scan_param[1];
} MLAN_PACK_END MrvlIEtypes_ChanListParamSet_t;

/** MrvlIEtypes_EESParamSet_t */
typedef MLAN_PACK_START struct _MrvlIEtypes_EESParamSet_t {
	/** Header */
	MrvlIEtypesHeader_t header;
	/** EES scan mode */
	t_u16 ees_mode;
	/** EES report condition */
	t_u16 report_cond;
	/** EES High Period scan interval */
	t_u16 high_period;
	/** EES High Period scan count */
	t_u16 high_period_count;
	/** EES Medium Period scan interval */
	t_u16 mid_period;
	/** EES Medium Period scan count */
	t_u16 mid_period_count;
	/** EES Low Period scan interval */
	t_u16 low_period;
	/** EES Low Period scan count */
	t_u16 low_period_count;
} MLAN_PACK_END MrvlIEtypes_EESParamSet_t;

/** MrvlIEtype_EESNetworkCfg_t */
typedef MLAN_PACK_START struct _MrvlIEtype_EESNetworkCfg_t {
	/** Header */
	MrvlIEtypesHeader_t header;
	/** Number of networks in the list */
	t_u8 network_count;
	/** Maximum number of connection */
	t_u8 max_conn_count;
	/** Black List Exp */
	t_u8 black_list_exp;
} MLAN_PACK_END MrvlIEtype_EESNetworkCfg_t;

/** ChanBandParamSet_t */
typedef struct _ChanBandParamSet_t {
	/** Channel scan parameter : band config */
	Band_Config_t bandcfg;
	/** Channel number */
	t_u8 chan_number;
} ChanBandParamSet_t;

/** MrvlIEtypes_ChanBandListParamSet_t */
typedef MLAN_PACK_START struct _MrvlIEtypes_ChanBandListParamSet_t {
	/** Header */
	MrvlIEtypesHeader_t header;
	/** Channel Band parameters */
	ChanBandParamSet_t chan_band_param[1];
} MLAN_PACK_END MrvlIEtypes_ChanBandListParamSet_t;

/** MrvlIEtypes_RatesParamSet_t */
typedef MLAN_PACK_START struct _MrvlIEtypes_RatesParamSet_t {
	/** Header */
	MrvlIEtypesHeader_t header;
	/** Rates */
	t_u8 rates[1];
} MLAN_PACK_END MrvlIEtypes_RatesParamSet_t;

/** _MrvlIEtypes_Bssid_List_t */
typedef MLAN_PACK_START struct _MrvlIEtypes_Bssid_List_t {
	/** Header */
	MrvlIEtypesHeader_t header;
	/** BSSID */
	t_u8 bssid[MLAN_MAC_ADDR_LENGTH];
} MLAN_PACK_END MrvlIEtypes_Bssid_List_t;

/** MrvlIEtypes_SsIdParamSet_t */
typedef MLAN_PACK_START struct _MrvlIEtypes_SsIdParamSet_t {
	/** Header */
	MrvlIEtypesHeader_t header;
	/** SSID */
	t_u8 ssid[1];
} MLAN_PACK_END MrvlIEtypes_SsIdParamSet_t;

/**MrvlIEtypes_AssocType_t */
typedef MLAN_PACK_START struct _MrvlIEtypes_HostMlme_t {
	/** Header */
	MrvlIEtypesHeader_t header;
	/** Authentication type */
	t_u8 host_mlme;
} MLAN_PACK_END MrvlIEtypes_HostMlme_t;

/** MrvlIEtypes_NumProbes_t */
typedef MLAN_PACK_START struct _MrvlIEtypes_NumProbes_t {
	/** Header */
	MrvlIEtypesHeader_t header;
	/** Number of probes */
	t_u16 num_probes;
} MLAN_PACK_END MrvlIEtypes_NumProbes_t;

/** MrvlIEtypes_WildCardSsIdParamSet_t */
typedef MLAN_PACK_START struct _MrvlIEtypes_WildCardSsIdParamSet_t {
	/** Header */
	MrvlIEtypesHeader_t header;
	/** Maximum SSID length */
	t_u8 max_ssid_length;
	/** SSID */
	t_u8 ssid[1];
} MLAN_PACK_END MrvlIEtypes_WildCardSsIdParamSet_t;

/**TSF data size */
#define TSF_DATA_SIZE 8
/** Table of TSF values returned in the scan result */
typedef MLAN_PACK_START struct _MrvlIEtypes_TsfTimestamp_t {
	/** Header */
	MrvlIEtypesHeader_t header;
	/** the length of each TSF data is 8 bytes, could be multiple TSF here
	 */
	t_u8 tsf_data[1];
} MLAN_PACK_END MrvlIEtypes_TsfTimestamp_t;

/** CfParamSet_t */
typedef MLAN_PACK_START struct _CfParamSet_t {
	/** CF parameter : Count */
	t_u8 cfp_cnt;
	/** CF parameter : Period */
	t_u8 cfp_period;
	/** CF parameter : Duration */
	t_u16 cfp_max_duration;
	/** CF parameter : Duration remaining */
	t_u16 cfp_duration_remaining;
} MLAN_PACK_END CfParamSet_t;

/** IbssParamSet_t */
typedef MLAN_PACK_START struct _IbssParamSet_t {
	/** ATIM window value */
	t_u16 atim_window;
} MLAN_PACK_END IbssParamSet_t;

/** MrvlIEtypes_SsParamSet_t */
typedef MLAN_PACK_START struct _MrvlIEtypes_SsParamSet_t {
	/** Header */
	MrvlIEtypesHeader_t header;
	/** CF/IBSS parameters sets */
	union {
		/** CF parameter set */
		CfParamSet_t cf_param_set[1];
		/** IBSS parameter set */
		IbssParamSet_t ibss_param_set[1];
	} cf_ibss;
} MLAN_PACK_END MrvlIEtypes_SsParamSet_t;

/** FhParamSet_t */
typedef MLAN_PACK_START struct _FhParamSet_t {
	/** FH parameter : Dwell time */
	t_u16 dwell_time;
	/** FH parameter : Hop set */
	t_u8 hop_set;
	/** FH parameter : Hop pattern */
	t_u8 hop_pattern;
	/** FH parameter : Hop index */
	t_u8 hop_index;
} MLAN_PACK_END FhParamSet_t;

/** DsParamSet_t */
typedef MLAN_PACK_START struct _DsParamSet_t {
	/** Current channel number */
	t_u8 current_chan;
} MLAN_PACK_END DsParamSet_t;

/** MrvlIEtypes_PhyParamSet_t */
typedef MLAN_PACK_START struct _MrvlIEtypes_PhyParamSet_t {
	/** Header */
	MrvlIEtypesHeader_t header;
	/** FH/DS parameters */
	union {
		/** FH parameter set */
		FhParamSet_t fh_param_set[1];
		/** DS parameter set */
		DsParamSet_t ds_param_set[1];
	} fh_ds;
} MLAN_PACK_END MrvlIEtypes_PhyParamSet_t;

/* Auth type to be used in the Authentication portion of an Assoc seq */
/** MrvlIEtypes_AuthType_t */
typedef MLAN_PACK_START struct _MrvlIEtypes_AuthType_t {
	/** Header */
	MrvlIEtypesHeader_t header;
	/** Authentication type */
	t_u16 auth_type;
} MLAN_PACK_END MrvlIEtypes_AuthType_t;

/** MrvlIEtypes_ScanChanGap_t */
typedef MLAN_PACK_START struct _MrvlIEtypes_ScanChanGap_t {
	/** Header */
	MrvlIEtypesHeader_t header;
	/** Time gap in units to TUs to be used between
	 * two consecutive channels scan */
	t_u16 gap;
} MLAN_PACK_END MrvlIEtypes_ScanChanGap_t;

/** channel statictics */
typedef MLAN_PACK_START struct _chan_statistics_t {
	/** channle number */
	t_u8 chan_num;
	/** band info */
	Band_Config_t bandcfg;
	/** flags */
	t_u8 flags;
	/** noise */
	t_s8 noise;
	/** total network */
	t_u16 total_networks;
	/** scan duration */
	t_u16 cca_scan_duration;
	/** busy duration */
	t_u16 cca_busy_duration;
} MLAN_PACK_END chan_statistics_t;

/** channel statictics tlv */
typedef MLAN_PACK_START struct _MrvlIEtypes_ChannelStats_t {
	/** Header */
	MrvlIEtypesHeader_t header;
	/** channel statictics */
	chan_statistics_t chanStat[];
} MLAN_PACK_END MrvlIEtypes_ChannelStats_t;

/** MrvlIETypes_ActionFrame_t */
typedef MLAN_PACK_START struct {
	MrvlIEtypesHeader_t header; /**< Header */

	t_u8 srcAddr[MLAN_MAC_ADDR_LENGTH];
	t_u8 dstAddr[MLAN_MAC_ADDR_LENGTH];

	IEEEtypes_ActionFrame_t actionFrame;

} MLAN_PACK_END MrvlIETypes_ActionFrame_t;

/** MrvlIEtypes_RxBaSync_t */
typedef MLAN_PACK_START struct _MrvlIEtypes_RxBaSync_t {
	/** Header */
	MrvlIEtypesHeader_t header;
	/** mac address */
	t_u8 mac[MLAN_MAC_ADDR_LENGTH];
	/** tid */
	t_u8 tid;
	/** reserved field */
	t_u8 reserved;
	/** start seq num */
	t_u16 seq_num;
	/** bitmap len */
	t_u16 bitmap_len;
	/** bitmap */
	t_u8 bitmap[1];
} MLAN_PACK_END MrvlIEtypes_RxBaSync_t;

/** MrvlIEtypes_RsnParamSet_t */
typedef MLAN_PACK_START struct _MrvlIEtypes_RsnParamSet_t {
	/** Header */
	MrvlIEtypesHeader_t header;
	/** RSN IE */
	t_u8 rsn_ie[];
} MLAN_PACK_END MrvlIEtypes_RsnParamSet_t;

/** MrvlIEtypes_SecurityCfg_t */
typedef MLAN_PACK_START struct _MrvlIEtypes_SecurityCfg_t {
	/** Header */
	MrvlIEtypesHeader_t header;
	/** enable 11w */
	t_u8 use_mfp;
} MLAN_PACK_END MrvlIEtypes_SecurityCfg_t;

/** Host Command ID : _HostCmd_DS_BEACON_STUCK_CFG */
typedef MLAN_PACK_START struct _HostCmd_DS_BEACON_STUCK_CFG {
    /** ACT_GET/ACT_SET */
	t_u8 action;
    /** No of beacon interval after which firmware will check if beacon Tx is going fine */
	t_u8 beacon_stuck_detect_count;
    /** Upon performing MAC reset, no of beacon interval after which firmware will check if recovery was successful */
	t_u8 recovery_confirm_count;
} MLAN_PACK_END HostCmd_DS_BEACON_STUCK_CFG;

/** Key Info flag for multicast key */
#define KEY_INFO_MCAST_KEY 0x01
/** Key Info flag for unicast key */
#define KEY_INFO_UCAST_KEY 0x02
/** Key Info flag for enable key */
#define KEY_INFO_ENABLE_KEY 0x04
/** Key Info flag for default key */
#define KEY_INFO_DEFAULT_KEY 0x08
/** Key Info flag for TX key */
#define KEY_INFO_TX_KEY 0x10
/** Key Info flag for RX key */
#define KEY_INFO_RX_KEY 0x20
#define KEY_INFO_CMAC_AES_KEY 0x400
/** PN size for WPA/WPA2 */
#define WPA_PN_SIZE 8
/** PN size for PMF IGTK */
#define IGTK_PN_SIZE 8
/** WAPI KEY size */
#define WAPI_KEY_SIZE 32
/** key params fix size */
#define KEY_PARAMS_FIXED_LEN 10
/** key index mask */
#define KEY_INDEX_MASK 0xf

/** wep_param */
typedef MLAN_PACK_START struct _wep_param_t {
	/** key_len */
	t_u16 key_len;
	/** wep key */
	t_u8 key[MAX_WEP_KEY_SIZE];
} MLAN_PACK_END wep_param_t;

/** tkip_param */
typedef MLAN_PACK_START struct _tkip_param {
	/** Rx packet num */
	t_u8 pn[WPA_PN_SIZE];
	/** key_len */
	t_u16 key_len;
	/** tkip key */
	t_u8 key[WPA_TKIP_KEY_LEN];
} MLAN_PACK_END tkip_param;

/** aes_param */
typedef MLAN_PACK_START struct _aes_param {
	/** Rx packet num */
	t_u8 pn[WPA_PN_SIZE];
	/** key_len */
	t_u16 key_len;
	/** aes key */
	t_u8 key[WPA_AES_KEY_LEN];
} MLAN_PACK_END aes_param;

/** wapi_param */
typedef MLAN_PACK_START struct _wapi_param {
	/** Rx packet num */
	t_u8 pn[PN_SIZE];
	/** key_len */
	t_u16 key_len;
	/** wapi key */
	t_u8 key[WAPI_KEY_SIZE];
} MLAN_PACK_END wapi_param;

/** cmac_aes_param */
typedef MLAN_PACK_START struct _cmac_aes_param {
	/** IGTK pn */
	t_u8 ipn[IGTK_PN_SIZE];
	/** key_len */
	t_u16 key_len;
	/** aes key */
	t_u8 key[CMAC_AES_KEY_LEN];
} MLAN_PACK_END cmac_aes_param;

/** gmac_aes_256_param */
typedef MLAN_PACK_START struct _gmac_aes_256_param {
	/** IGTK pn */
	t_u8 ipn[IGTK_PN_SIZE];
	/** key_len */
	t_u16 key_len;
	/** aes key */
	t_u8 key[WPA_IGTK_256_KEY_LEN];
} MLAN_PACK_END gmac_aes_256_param;

/** gmac_param */
typedef MLAN_PACK_START struct _gcmp_param {
	/** GCMP pn */
	t_u8 pn[WPA_PN_SIZE];
	/** key_len */
	t_u16 key_len;
	/** aes key */
	t_u8 key[WPA_GCMP_KEY_LEN];
} MLAN_PACK_END gcmp_param;

/** ccmp256_param */
typedef MLAN_PACK_START struct _ccmp256_param {
	/** CCMP pn */
	t_u8 pn[WPA_PN_SIZE];
	/** key_len */
	t_u16 key_len;
	/** ccmp256 key */
	t_u8 key[WPA_CCMP_256_KEY_LEN];
} MLAN_PACK_END ccmp_256_param;

/** MrvlIEtype_KeyParamSet_t */
typedef MLAN_PACK_START struct _MrvlIEtype_KeyParamSetV2_t {
	/** Type ID */
	t_u16 type;
	/** Length of Payload */
	t_u16 length;
	/** mac address */
	t_u8 mac_addr[MLAN_MAC_ADDR_LENGTH];
	/** key index */
	t_u8 key_idx;
	/** Type of Key: WEP=0, TKIP=1, AES=2, WAPI=3 AES_CMAC=4 */
	t_u8 key_type;
	/** Key Control Info specific to a key_type_id */
	t_u16 key_info;
	union {
		/** wep key param */
		wep_param_t wep;
		/** tkip key param */
		tkip_param tkip;
		/** aes key param */
		aes_param aes;
		/** wapi key param */
		wapi_param wapi;
		/** IGTK key param */
		cmac_aes_param cmac_aes;
		/** gcmp key param */
		gcmp_param gcmp;
		/** ccmp 256 key parameters */
		ccmp_256_param ccmp256;
	} key_params;
} MLAN_PACK_END MrvlIEtype_KeyParamSetV2_t;

/** HostCmd_DS_802_11_KEY_MATERIAL */
typedef MLAN_PACK_START struct _HostCmd_DS_802_11_KEY_MATERIAL {
	/** Action */
	t_u16 action;
	/** Key parameter set */
	MrvlIEtype_KeyParamSetV2_t key_param_set;
} MLAN_PACK_END HostCmd_DS_802_11_KEY_MATERIAL;

/** HostCmd_DS_GTK_REKEY_PARAMS */
typedef MLAN_PACK_START struct _HostCmd_DS_GTK_REKEY_PARAMS {
	/** Action */
	t_u16 action;
	/** Key confirmation key */
	t_u8 kck[MLAN_KCK_LEN];
	/** Key encryption key */
	t_u8 kek[MLAN_KEK_LEN];
	/** Replay counter low 32 bit */
	t_u32 replay_ctr_low;
	/** Replay counter high 32 bit */
	t_u32 replay_ctr_high;
} MLAN_PACK_END HostCmd_DS_GTK_REKEY_PARAMS;

/** Data structure of WMM QoS information */
typedef MLAN_PACK_START struct _WmmQosInfo_t {
#ifdef BIG_ENDIAN_SUPPORT
	/** QoS UAPSD */
	t_u8 qos_uapsd:1;
	/** Reserved */
	t_u8 reserved:3;
	/** Parameter set count */
	t_u8 para_set_count:4;
#else
	/** Parameter set count */
	t_u8 para_set_count:4;
	/** Reserved */
	t_u8 reserved:3;
	/** QoS UAPSD */
	t_u8 qos_uapsd:1;
#endif				/* BIG_ENDIAN_SUPPORT */
} MLAN_PACK_END WmmQosInfo_t, *pWmmQosInfo_t;

/** Data structure of WMM ECW */
typedef MLAN_PACK_START struct _WmmEcw_t {
#ifdef BIG_ENDIAN_SUPPORT
	/** Maximum Ecw */
	t_u8 ecw_max:4;
	/** Minimum Ecw */
	t_u8 ecw_min:4;
#else
	/** Minimum Ecw */
	t_u8 ecw_min:4;
	/** Maximum Ecw */
	t_u8 ecw_max:4;
#endif				/* BIG_ENDIAN_SUPPORT */
} MLAN_PACK_END WmmEcw_t, *pWmmEcw_t;

/** Data structure of WMM Aci/Aifsn */
typedef MLAN_PACK_START struct _WmmAciAifsn_t {
#ifdef BIG_ENDIAN_SUPPORT
	/** Reserved */
	t_u8 reserved:1;
	/** Aci */
	t_u8 aci:2;
	/** Acm */
	t_u8 acm:1;
	/** Aifsn */
	t_u8 aifsn:4;
#else
	/** Aifsn */
	t_u8 aifsn:4;
	/** Acm */
	t_u8 acm:1;
	/** Aci */
	t_u8 aci:2;
	/** Reserved */
	t_u8 reserved:1;
#endif				/* BIG_ENDIAN_SUPPORT */
} MLAN_PACK_END WmmAciAifsn_t, *pWmmAciAifsn_t;

/** Data structure of WMM AC parameters  */
typedef MLAN_PACK_START struct _WmmAcParameters_t {
	WmmAciAifsn_t aci_aifsn; /**< AciAifSn */
	WmmEcw_t ecw; /**< Ecw */
	t_u16 tx_op_limit; /**< Tx op limit */
} MLAN_PACK_END WmmAcParameters_t, *pWmmAcParameters_t;

/** Data structure of WMM parameter  */
typedef MLAN_PACK_START struct _WmmParameter_t {
	/** OuiType:  00:50:f2:02 */
	t_u8 ouitype[4];
	/** Oui subtype: 01 */
	t_u8 ouisubtype;
	/** version: 01 */
	t_u8 version;
	/** QoS information */
	t_u8 qos_info;
	/** Reserved */
	t_u8 reserved;
	/** AC Parameters Record WMM_AC_BE, WMM_AC_BK, WMM_AC_VI, WMM_AC_VO */
	WmmAcParameters_t ac_params[MAX_AC_QUEUES];
} MLAN_PACK_END WmmParameter_t, *pWmmParameter_t;

/** Data structure of Host command WMM_PARAM_CFG  */
typedef MLAN_PACK_START struct _HostCmd_DS_WMM_PARAM_CONFIG {
	/** action */
	t_u16 action;
	/** AC Parameters Record WMM_AC_BE, WMM_AC_BK, WMM_AC_VI, WMM_AC_VO */
	WmmAcParameters_t ac_params[MAX_AC_QUEUES];
} MLAN_PACK_END HostCmd_DS_WMM_PARAM_CONFIG;

/* Definition of firmware host command */
/** HostCmd_DS_GEN */
typedef MLAN_PACK_START struct _HostCmd_DS_GEN {
	/** Command */
	t_u16 command;
	/** Size */
	t_u16 size;
	/** Sequence number */
	t_u16 seq_num;
	/** Result */
	t_u16 result;
} MLAN_PACK_END HostCmd_DS_GEN
;

/** Size of HostCmd_DS_GEN */
#define S_DS_GEN sizeof(HostCmd_DS_GEN)

/** mod_group_setting */
typedef MLAN_PACK_START struct _mod_group_setting {
	/** modulation group */
	t_u8 mod_group;
	/** power */
	t_u8 power;
} MLAN_PACK_END mod_group_setting;

/** MrvlIETypes_ChanTRPCConfig_t */
typedef MLAN_PACK_START struct _MrvlIETypes_ChanTRPCConfig_t {
	/** Header */
	MrvlIEtypesHeader_t header;
	/** start freq */
	t_u16 start_freq;
	/* channel width */
	t_u8 width;
	/** channel number */
	t_u8 chan_num;
	/** mode groups */
	mod_group_setting mod_group[1];
} MLAN_PACK_END MrvlIETypes_ChanTRPCConfig_t;

/* HostCmd_DS_CHANNEL_TRPC_CONFIG */
typedef MLAN_PACK_START struct _HostCmd_DS_CHANNEL_TRPC_CONFIG {
	/** action */
	t_u16 action;
	/** 0/1/2/3 */
	t_u16 sub_band;
	/** chan TRPC config */
	//MrvlIETypes_ChanTRPCConfig_t tlv[];
} MLAN_PACK_END HostCmd_DS_CHANNEL_TRPC_CONFIG;

/** Address type: broadcast */
#define ADDR_TYPE_BROADCAST 1
/* Address type: unicast */
#define ADDR_TYPE_UNICAST 2
/* Address type: multicast */
#define ADDR_TYPE_MULTICAST 3

/** Ether type: any */
#define ETHER_TYPE_ANY 0xffff
/** Ether type: ARP */
#define ETHER_TYPE_ARP 0x0608

/** IPv4 address any */
#define IPV4_ADDR_ANY 0xffffffff

/** Header structure for ARP filter */
typedef MLAN_PACK_START struct _arpfilter_header {
	/** Type */
	t_u16 type;
	/** TLV length */
	t_u16 len;
} MLAN_PACK_END arpfilter_header;

/** Filter entry structure */
typedef MLAN_PACK_START struct _filter_entry {
	/** Address type */
	t_u16 addr_type;
	/** Ether type */
	t_u16 eth_type;
	/** IPv4 address */
	t_u32 ipv4_addr;
} MLAN_PACK_END filter_entry;

typedef MLAN_PACK_START struct _HostCmd_DS_MEF_CFG {
	/** Criteria */
	t_u32 criteria;
	/** Number of entries */
	t_u16 nentries;
} MLAN_PACK_END HostCmd_DS_MEF_CFG;

#define MAX_NUM_STACK_BYTES 100
/** mef stack struct*/
typedef MLAN_PACK_START struct _mef_stack {
	/** length of byte*/
	t_u16 sp;
	/** data of filter items*/
	t_u8 byte[MAX_NUM_STACK_BYTES];
} MLAN_PACK_END mef_stack;

/** mef entry struct */
typedef MLAN_PACK_START struct _mef_entry_header {
	/**mode:1->hostsleep;2->non hostsleep mode*/
	t_u8 mode;
	/**action=0->discard and not wake host
	 * action=1->discard and wake host
	 * action=3->allow and wake host*/
	t_u8 action;
} MLAN_PACK_END mef_entry_header;

/** mef op struct is to help to generate mef data*/
typedef MLAN_PACK_START struct _mef_op {
	/** operand_type*/
	t_u8 operand_type;
	/** reserved*/
	t_u8 rsvd[3];
	/** data */
	t_u8 val[MAX_NUM_BYTE_SEQ + 1];
} MLAN_PACK_END mef_op;

/* HostCmd_DS_802_11_SLEEP_PERIOD */
typedef MLAN_PACK_START struct _HostCmd_DS_802_11_SLEEP_PERIOD {
	/** ACT_GET/ACT_SET */
	t_u16 action;

	/** Sleep Period in msec */
	t_u16 sleep_pd;
} MLAN_PACK_END HostCmd_DS_802_11_SLEEP_PERIOD;

/* HostCmd_DS_802_11_SLEEP_PARAMS */
typedef MLAN_PACK_START struct _HostCmd_DS_802_11_SLEEP_PARAMS {
	/** ACT_GET/ACT_SET */
	t_u16 action;
	/** Sleep clock error in ppm */
	t_u16 error;
	/** Wakeup offset in usec */
	t_u16 offset;
	/** Clock stabilization time in usec */
	t_u16 stable_time;
	/** Control periodic calibration */
	t_u8 cal_control;
	/** Control the use of external sleep clock */
	t_u8 external_sleep_clk;
	/** Reserved field, should be set to zero */
	t_u16 reserved;
} MLAN_PACK_END HostCmd_DS_802_11_SLEEP_PARAMS;

/** Sleep response control */
typedef enum _sleep_resp_ctrl {
	RESP_NOT_NEEDED = 0,
	RESP_NEEDED,
} sleep_resp_ctrl;

/** Structure definition for the new ieee power save parameters*/
typedef MLAN_PACK_START struct __ps_param {
	/** Null packet interval */
	t_u16 null_pkt_interval;
	/** Num dtims */
	t_u16 multiple_dtims;
	/** becaon miss interval */
	t_u16 bcn_miss_timeout;
	/** local listen interval */
	t_u16 local_listen_interval;
	/** Adhoc awake period */
	t_u16 adhoc_wake_period;
	/** mode - (0x01 - firmware to automatically choose PS_POLL or NULL
	 * mode, 0x02 - PS_POLL, 0x03 - NULL mode )
	 */
	t_u16 mode;
	/** Delay to PS in milliseconds */
	t_u16 delay_to_ps;
} MLAN_PACK_END ps_param;

/** Structure definition for the new auto deep sleep command */
typedef MLAN_PACK_START struct __auto_ds_param {
	/** Deep sleep inactivity timeout */
	t_u16 deep_sleep_timeout;
} MLAN_PACK_END auto_ds_param;

/** Structure definition for sleep confirmation in the new ps command */
typedef MLAN_PACK_START struct __sleep_confirm_param {
	/** response control 0x00 - response not needed, 0x01 - response needed
	 */
	t_u16 resp_ctrl;
} MLAN_PACK_END sleep_confirm_param;

/** bitmap for get auto deepsleep */
#define BITMAP_AUTO_DS 0x01
/** bitmap for sta power save */
#define BITMAP_STA_PS 0x10
/** bitmap for beacon timeout */
#define BITMAP_BCN_TMO 0x20
/** bitmap for uap inactivity based PS */
#define BITMAP_UAP_INACT_PS 0x100
/** bitmap for uap DTIM PS */
#define BITMAP_UAP_DTIM_PS 0x200
/** Structure definition for the new ieee power save parameters*/
typedef MLAN_PACK_START struct _auto_ps_param {
	/** bitmap for enable power save mode */
	t_u16 ps_bitmap;
	/* auto deep sleep parameter,
	 * sta power save parameter
	 * uap inactivity parameter
	 * uap DTIM parameter */
} MLAN_PACK_END auto_ps_param;

/** fix size for auto ps */
#define AUTO_PS_FIX_SIZE 4

/** TLV type : auto ds param */
#define TLV_TYPE_AUTO_DS_PARAM (PROPRIETARY_TLV_BASE_ID + 0x71)	/* 0x0171 */
/** TLV type : ps param */
#define TLV_TYPE_PS_PARAM (PROPRIETARY_TLV_BASE_ID + 0x72)	/* 0x0172 */
/** TLV type : beacon timeout */
#define TLV_TYPE_BCN_TIMEOUT (PROPRIETARY_TLV_BASE_ID + 0x11F)	/* 0x011F */

/** MrvlIEtypes_auto_ds_param_t */
typedef MLAN_PACK_START struct _MrvlIEtypes_auto_ds_param_t {
	/** Header */
	MrvlIEtypesHeader_t header;
	/** auto ds param */
	auto_ds_param param;
} MLAN_PACK_END MrvlIEtypes_auto_ds_param_t;

/** MrvlIEtypes_ps_param_t */
typedef MLAN_PACK_START struct _MrvlIEtypes_ps_param_t {
	/** Header */
	MrvlIEtypesHeader_t header;
	/** ps param */
	ps_param param;
} MLAN_PACK_END MrvlIEtypes_ps_param_t;

/** MrvlIEtypes_bcn_timeout_t */
typedef MLAN_PACK_START struct _MrvlIEtypes_bcn_timeout_t {
	/** Header */
	MrvlIEtypesHeader_t header;
	/** Beacon miss timeout period window */
	t_u16 bcn_miss_tmo_window;
	/** Beacon miss timeout period */
	t_u16 bcn_miss_tmo_period;
	/** Beacon reacquire timeout period window */
	t_u16 bcn_rq_tmo_window;
	/** Beacon reacquire timeout period */
	t_u16 bcn_rq_tmo_period;
} MLAN_PACK_END MrvlIEtypes_bcn_timeout_t;

/** Structure definition for low power mode cfg command */
typedef MLAN_PACK_START struct _HostCmd_DS_LOW_POWER_MODE_CFG {
	/** Action */
	t_u16 action;
	/** Low power mode */
	t_u16 lpm;
} MLAN_PACK_END HostCmd_DS_LOW_POWER_MODE_CFG;

/** Structure definition for new power save command */
typedef MLAN_PACK_START struct _HostCmd_DS_PS_MODE_ENH {
	/** Action */
	t_u16 action;
	/** Data speciifc to action */
	/* For IEEE power save data will be as
	 * UINT16 mode (0x01 - firmware to automatically choose PS_POLL or NULL
	 * mode, 0x02 - PS_POLL, 0x03 - NULL mode ) UINT16 NullpacketInterval
	 * UINT16 NumDtims
	 * UINT16 BeaconMissInterval
	 * UINT16 locallisteninterval
	 * UINT16 adhocawakeperiod */

	/* For auto deep sleep */
	/* UINT16 Deep sleep inactivity timeout */

	/* For PS sleep confirm
	 * UINT16 responeCtrl - 0x00 - reponse from fw not needed, 0x01 -
	 * response from fw is needed */

	union {
		/** PS param definition */
		ps_param opt_ps;
		/** Auto ds param definition */
		auto_ds_param auto_ds;
		/** Sleep comfirm param definition */
		sleep_confirm_param sleep_cfm;
		/** bitmap for get PS info and Disable PS mode */
		t_u16 ps_bitmap;
		/** auto ps param */
		auto_ps_param auto_ps;
	} params;
} MLAN_PACK_END HostCmd_DS_802_11_PS_MODE_ENH;

/** FW VERSION tlv */
#define TLV_TYPE_FW_VER_INFO (PROPRIETARY_TLV_BASE_ID + 0xC7)	/* 0x1C7 */

/** MrvlIEtypes_fw_ver_info_t */
typedef MLAN_PACK_START struct _MrvlIEtypes_fw_ver_info_t {
	/** Header */
	MrvlIEtypesHeader_t header;
	/** API id */
	t_u16 api_id;
	/** major version */
	t_u8 major_ver;
	/** minor version */
	t_u8 minor_ver;
} MLAN_PACK_END MrvlIEtypes_fw_ver_info_t;

/** API ID */
enum API_VER_ID {
	KEY_API_VER_ID = 1,
	FW_API_VER_ID = 2,
	UAP_FW_API_VER_ID = 3,
	CHANRPT_API_VER_ID = 4,
	FW_HOTFIX_VER_ID = 5,
};

/** FW AP V15 */
#define HOST_API_VERSION_V15 15
/** FW minor version 1 */
#define FW_MINOR_VERSION_1 1

/** UAP FW version 2 */
#define UAP_FW_VERSION_2 0x2

/** HostCMD_DS_APCMD_ACS_SCAN */
typedef MLAN_PACK_START struct _HostCMD_DS_APCMD_ACS_SCAN {
	/** band */
	Band_Config_t bandcfg;
	/** channel */
	t_u8 chan;
} MLAN_PACK_END HostCMD_DS_APCMD_ACS_SCAN;

/** HostCmd_DS_GET_HW_SPEC */
typedef MLAN_PACK_START struct _HostCmd_DS_GET_HW_SPEC {
	/** HW Interface version number */
	t_u16 hw_if_version;
	/** HW version number */
	t_u16 version;
	/** Reserved field */
	t_u16 reserved;
	/** Max no of Multicast address  */
	t_u16 num_of_mcast_adr;
	/** MAC address */
	t_u8 permanent_addr[MLAN_MAC_ADDR_LENGTH];
	/** Region Code */
	t_u16 region_code;
	/** Number of antenna used */
	t_u16 number_of_antenna;
	/** FW release number, example 0x1234=1.2.3.4 */
	t_u32 fw_release_number;
	/** Reserved field */
	t_u32 reserved_1;
	/** Reserved field */
	t_u32 reserved_2;
	/** Reserved field */
	t_u32 reserved_3;
	/** FW/HW Capability */
	t_u32 fw_cap_info;
	/** 802.11n Device Capabilities */
	t_u32 dot_11n_dev_cap;
	/** MIMO abstraction of MCSs supported by device */
	t_u8 dev_mcs_support;
	/** Valid end port at init */
	t_u16 mp_end_port;
	/** mgmt IE buffer count */
	t_u16 mgmt_buf_count;
	/** Reserved */
	t_u32 reserved_8;
	/** Reserved */
	t_u32 reserved_9;
	/** 802.11ac Device Capabilities */
	t_u32 Dot11acDevCap;
	/** MCSs supported by 802.11ac device */
	t_u32 Dot11acMcsSupport;
} MLAN_PACK_END HostCmd_DS_GET_HW_SPEC;

#ifdef SDIO
/* HostCmd_DS_SDIO_SP_RX_AGGR_CFG */
typedef MLAN_PACK_START struct _HostCmd_DS_SDIO_SP_RX_AGGR_CFG {
	t_u8 action;
	t_u8 enable;
	t_u16 sdio_block_size;
} MLAN_PACK_END HostCmd_DS_SDIO_SP_RX_AGGR_CFG;
#endif

/**  HostCmd_DS_802_11_CFG_DATA */
typedef MLAN_PACK_START struct _HostCmd_DS_802_11_CFG_DATA {
	/** Action */
	t_u16 action;
	/** Type */
	t_u16 type;
	/** Data length */
	t_u16 data_len;
	/** Data */
} MLAN_PACK_END HostCmd_DS_802_11_CFG_DATA;

/**  HostCmd_DS_CMD_802_11_RSSI_INFO_EXT */
typedef MLAN_PACK_START struct _HostCmd_DS_802_11_RSSI_INFO_EXT {
	/** Action */
	t_u16 action;
	/** Parameter used for exponential averaging for Data */
	t_u16 ndata;
	/** Parameter used for exponential averaging for Beacon */
	t_u16 nbcn;
	/** Last RSSI beacon TSF(only for Get action) */
	t_u64 tsfbcn;
	/** TLV info**/
	t_u8 *tlv_buf[];
} MLAN_PACK_END HostCmd_DS_802_11_RSSI_INFO_EXT;

/** TLV rssi info */
#define TLV_TYPE_RSSI_INFO (PROPRIETARY_TLV_BASE_ID + 0xe5)	/* 0x01E5 */

/** MrvlIEtypes_eapol_pkt_t */
typedef MLAN_PACK_START struct _MrvlIEtypes_RSSI_EXT_t {
	/** Header */
	MrvlIEtypesHeader_t header;
	/** Path ID
	     [Bit1:Bit0] = [0:1]: path A
	     [Bit1:Bit0] = [1:0]: path B
	     [Bit1:Bit0] = [1:1]: combined signal of path A and path B
	     [Bit7:Bit2] : Reserved
	**/
	t_u16 path_id;
	/** Last Data RSSI in dBm */
	t_s16 data_rssi_last;
	/** Last Data NF in dBm */
	t_s16 data_nf_last;
	/** AVG DATA RSSI in dBm */
	t_s16 data_rssi_avg;
	/** AVG DATA NF in dBm */
	t_s16 data_nf_avg;
	/** Last BEACON RSSI in dBm */
	t_s16 bcn_rssi_last;
	/** Last BEACON NF in dBm */
	t_s16 bcn_nf_last;
	/** AVG BEACON RSSI in dBm */
	t_s16 bcn_rssi_avg;
	/** AVG BEACON NF in dBm */
	t_s16 bcn_nf_avg;
} MLAN_PACK_END MrvlIEtypes_RSSI_EXT_t;

/**  HostCmd_DS_CMD_802_11_RSSI_INFO */
typedef MLAN_PACK_START struct _HostCmd_DS_802_11_RSSI_INFO {
	/** Action */
	t_u16 action;
	/** Parameter used for exponential averaging for Data */
	t_u16 ndata;
	/** Parameter used for exponential averaging for Beacon */
	t_u16 nbcn;
	/** Reserved field 0 */
	t_u16 reserved[9];
	/** Reserved field 1 */
	t_u64 reserved_1;
} MLAN_PACK_END HostCmd_DS_802_11_RSSI_INFO;

/** HostCmd_DS_802_11_RSSI_INFO_RSP */
typedef MLAN_PACK_START struct _HostCmd_DS_802_11_RSSI_INFO_RSP {
	/** Action */
	t_u16 action;
	/** Parameter used for exponential averaging for Data */
	t_u16 ndata;
	/** Parameter used for exponential averaging for beacon */
	t_u16 nbcn;
	/** Last Data RSSI in dBm */
	t_s16 data_rssi_last;
	/** Last Data NF in dBm */
	t_s16 data_nf_last;
	/** AVG DATA RSSI in dBm */
	t_s16 data_rssi_avg;
	/** AVG DATA NF in dBm */
	t_s16 data_nf_avg;
	/** Last BEACON RSSI in dBm */
	t_s16 bcn_rssi_last;
	/** Last BEACON NF in dBm */
	t_s16 bcn_nf_last;
	/** AVG BEACON RSSI in dBm */
	t_s16 bcn_rssi_avg;
	/** AVG BEACON NF in dBm */
	t_s16 bcn_nf_avg;
	/** Last RSSI Beacon TSF */
	t_u64 tsf_bcn;
} MLAN_PACK_END HostCmd_DS_802_11_RSSI_INFO_RSP;

/** HostCmd_DS_802_11_MAC_ADDRESS */
typedef MLAN_PACK_START struct _HostCmd_DS_802_11_MAC_ADDRESS {
	/** Action */
	t_u16 action;
	/** MAC address */
	t_u8 mac_addr[MLAN_MAC_ADDR_LENGTH];
} MLAN_PACK_END HostCmd_DS_802_11_MAC_ADDRESS;

/** HostCmd_DS_MAC_CONTROL */
typedef MLAN_PACK_START struct _HostCmd_DS_MAC_CONTROL {
	/** Action */
	t_u32 action;
} MLAN_PACK_END HostCmd_DS_MAC_CONTROL;

/** HostCmd_DS_CMD_TX_DATA_PAUSE */
typedef MLAN_PACK_START struct _HostCmd_DS_CMD_TX_DATA_PAUSE {
	/** Action */
	t_u16 action;
	/** Enable/disable Tx data pause */
	t_u8 enable_tx_pause;
	/** Max number of TX buffers allowed for all PS clients*/
	t_u8 pause_tx_count;
} MLAN_PACK_END HostCmd_DS_CMD_TX_DATA_PAUSE;

/** TLV type : TX pause TLV */
#define TLV_TYPE_TX_PAUSE (PROPRIETARY_TLV_BASE_ID + 0x94)	/* 0x0194 */
/** MrvlIEtypes_SsIdParamSet_t */
typedef MLAN_PACK_START struct _MrvlIEtypes_tx_pause_t {
	/** Header */
	MrvlIEtypesHeader_t header;
	/** peer mac address */
	t_u8 peermac[MLAN_MAC_ADDR_LENGTH];
	/** Tx pause state, 1--pause, 0--free flowing */
	t_u8 tx_pause;
	/** total packets queued for the client */
	t_u8 pkt_cnt;
} MLAN_PACK_END MrvlIEtypes_tx_pause_t;

/**  HostCmd_CMD_MAC_MULTICAST_ADR */
typedef MLAN_PACK_START struct _HostCmd_DS_MAC_MULTICAST_ADR {
	/** Action */
	t_u16 action;
	/** Number of addresses */
	t_u16 num_of_adrs;
	/** List of MAC */
	t_u8 mac_list[MLAN_MAC_ADDR_LENGTH * MLAN_MAX_MULTICAST_LIST_SIZE];
} MLAN_PACK_END HostCmd_DS_MAC_MULTICAST_ADR;

/**  HostCmd_CMD_802_11_DEAUTHENTICATE */
typedef MLAN_PACK_START struct _HostCmd_DS_802_11_DEAUTHENTICATE {
	/** MAC address */
	t_u8 mac_addr[MLAN_MAC_ADDR_LENGTH];
	/** Deauthentication resaon code */
	t_u16 reason_code;
} MLAN_PACK_END HostCmd_DS_802_11_DEAUTHENTICATE;

/** HostCmd_DS_802_11_ASSOCIATE */
typedef MLAN_PACK_START struct _HostCmd_DS_802_11_ASSOCIATE {
	/** Peer STA address */
	t_u8 peer_sta_addr[MLAN_MAC_ADDR_LENGTH];
	/** Capability information */
	IEEEtypes_CapInfo_t cap_info;
	/** Listen interval */
	t_u16 listen_interval;
	/** Beacon period */
	t_u16 beacon_period;
	/** DTIM period */
	t_u8 dtim_period;

	/**
	 *  MrvlIEtypes_SsIdParamSet_t  SsIdParamSet;
	 *  MrvlIEtypes_PhyParamSet_t   PhyParamSet;
	 *  MrvlIEtypes_SsParamSet_t    SsParamSet;
	 *  MrvlIEtypes_RatesParamSet_t RatesParamSet;
	 */
} MLAN_PACK_END HostCmd_DS_802_11_ASSOCIATE;

/** HostCmd_CMD_802_11_ASSOCIATE response */
typedef MLAN_PACK_START struct _HostCmd_DS_802_11_ASSOCIATE_RSP {
	/** Association response structure */
	IEEEtypes_AssocRsp_t assoc_rsp;
} MLAN_PACK_END HostCmd_DS_802_11_ASSOCIATE_RSP;

/** HostCmd_DS_802_11_AD_HOC_START*/
typedef MLAN_PACK_START struct _HostCmd_DS_802_11_AD_HOC_START {
	/** AdHoc SSID */
	t_u8 ssid[MLAN_MAX_SSID_LENGTH];
	/** BSS mode */
	t_u8 bss_mode;
	/** Beacon period */
	t_u16 beacon_period;
	/** DTIM period */
	t_u8 dtim_period;
	/** SS parameter set */
	IEEEtypes_SsParamSet_t ss_param_set;
	/** PHY parameter set */
	IEEEtypes_PhyParamSet_t phy_param_set;
	/** Reserved field */
	t_u16 reserved1;
	/** Capability information */
	IEEEtypes_CapInfo_t cap;
	/** Supported data rates */
	t_u8 DataRate[HOSTCMD_SUPPORTED_RATES];
} MLAN_PACK_END HostCmd_DS_802_11_AD_HOC_START;

/**  HostCmd_CMD_802_11_AD_HOC_START response */
typedef MLAN_PACK_START struct _HostCmd_DS_802_11_AD_HOC_START_RESULT {
	/** Padding */
	t_u8 pad[3];
	/** AdHoc BSSID */
	t_u8 bssid[MLAN_MAC_ADDR_LENGTH];
	/** Padding to sync with FW structure*/
	t_u8 pad2[2];
	/** Result */
	t_u8 result;
} MLAN_PACK_END HostCmd_DS_802_11_AD_HOC_START_RESULT;

/**  HostCmd_CMD_802_11_AD_HOC_START response */
typedef MLAN_PACK_START struct _HostCmd_DS_802_11_AD_HOC_JOIN_RESULT {
	/** Result */
	t_u8 result;
} MLAN_PACK_END HostCmd_DS_802_11_AD_HOC_JOIN_RESULT;

/** AdHoc_BssDesc_t */
typedef MLAN_PACK_START struct _AdHoc_BssDesc_t {
	/** BSSID */
	t_u8 bssid[MLAN_MAC_ADDR_LENGTH];
	/** SSID */
	t_u8 ssid[MLAN_MAX_SSID_LENGTH];
	/** BSS mode */
	t_u8 bss_mode;
	/** Beacon period */
	t_u16 beacon_period;
	/** DTIM period */
	t_u8 dtim_period;
	/** Timestamp */
	t_u8 time_stamp[8];
	/** Local time */
	t_u8 local_time[8];
	/** PHY parameter set */
	IEEEtypes_PhyParamSet_t phy_param_set;
	/** SS parameter set */
	IEEEtypes_SsParamSet_t ss_param_set;
	/** Capability information */
	IEEEtypes_CapInfo_t cap;
	/** Supported data rates */
	t_u8 data_rates[HOSTCMD_SUPPORTED_RATES];

	/*
	 *  DO NOT ADD ANY FIELDS TO THIS STRUCTURE.
	 *  It is used in the Adhoc join command and will cause a
	 *  binary layout mismatch with the firmware
	 */
} MLAN_PACK_END AdHoc_BssDesc_t;

/** HostCmd_DS_802_11_AD_HOC_JOIN */
typedef MLAN_PACK_START struct _HostCmd_DS_802_11_AD_HOC_JOIN {
	/** AdHoc BSS descriptor */
	AdHoc_BssDesc_t bss_descriptor;
	/** Reserved field */
	t_u16 reserved1;
	/** Reserved field */
	t_u16 reserved2;
} MLAN_PACK_END HostCmd_DS_802_11_AD_HOC_JOIN;

#if defined(SDIO)
/** Interrupt Raising Edge */
#define INT_RASING_EDGE 0
/** Interrupt Falling Edge */
#define INT_FALLING_EDGE 1

/** Delay 1 usec */
#define DELAY_1_US 1

typedef MLAN_PACK_START struct _HostCmd_DS_SDIO_GPIO_INT_CONFIG {
	/** Action */
	t_u16 action;
	/** GPIO interrupt pin */
	t_u16 gpio_pin;
	/** GPIO interrupt edge, 1: failing edge; 0: raising edge */
	t_u16 gpio_int_edge;
	/** GPIO interrupt pulse widthin usec units */
	t_u16 gpio_pulse_width;
} MLAN_PACK_END HostCmd_DS_SDIO_GPIO_INT_CONFIG;
#endif /* GPIO_SDIO_INT_CTRL */

typedef MLAN_PACK_START struct _HostCmd_DS_SDIO_PULL_CTRL {
	/** Action */
	t_u16 action;
	/** The delay of pulling up in us */
	t_u16 pull_up;
	/** The delay of pulling down in us */
	t_u16 pull_down;
} MLAN_PACK_END HostCmd_DS_SDIO_PULL_CTRL;

/** HostCmd_DS_802_11_GET_LOG */
typedef MLAN_PACK_START struct _HostCmd_DS_802_11_GET_LOG {
	/** Number of multicast transmitted frames */
	t_u32 mcast_tx_frame;
	/** Number of failures */
	t_u32 failed;
	/** Number of retries */
	t_u32 retry;
	/** Number of multiretries */
	t_u32 multiretry;
	/** Number of duplicate frames */
	t_u32 frame_dup;
	/** Number of RTS success */
	t_u32 rts_success;
	/** Number of RTS failure */
	t_u32 rts_failure;
	/** Number of acknowledgement failure */
	t_u32 ack_failure;
	/** Number of fragmented packets received */
	t_u32 rx_frag;
	/** Number of multicast frames received */
	t_u32 mcast_rx_frame;
	/** FCS error */
	t_u32 fcs_error;
	/** Number of transmitted frames */
	t_u32 tx_frame;
	/** Reserved field */
	t_u32 reserved;
	/** Number of WEP icv error for each key */
	t_u32 wep_icv_err_cnt[4];
	/** Beacon received count */
	t_u32 bcn_rcv_cnt;
	/** Beacon missed count */
	t_u32 bcn_miss_cnt;
	/** Tx frag count */
	t_u32 tx_frag_cnt;
	/** Qos Tx frag count */
	t_u32 qos_tx_frag_cnt[8];
	/** Qos failed count */
	t_u32 qos_failed_cnt[8];
	/** Qos retry count */
	t_u32 qos_retry_cnt[8];
	/** Qos multi retry count */
	t_u32 qos_multi_retry_cnt[8];
	/** Qos frame dup count */
	t_u32 qos_frm_dup_cnt[8];
	/** Qos rts success count */
	t_u32 qos_rts_suc_cnt[8];
	/** Qos rts failure count */
	t_u32 qos_rts_failure_cnt[8];
	/** Qos ack failure count */
	t_u32 qos_ack_failure_cnt[8];
	/** Qos Rx frag count */
	t_u32 qos_rx_frag_cnt[8];
	/** Qos Tx frame count */
	t_u32 qos_tx_frm_cnt[8];
	/** Qos discarded frame count */
	t_u32 qos_discarded_frm_cnt[8];
	/** Qos mpdus Rx count */
	t_u32 qos_mpdus_rx_cnt[8];
	/** Qos retry rx count */
	t_u32 qos_retries_rx_cnt[8];
	/** CMAC ICV errors count */
	t_u32 cmacicv_errors;
	/** CMAC replays count */
	t_u32 cmac_replays;
	/** mgmt CCMP replays count */
	t_u32 mgmt_ccmp_replays;
	/** TKIP ICV errors count */
	t_u32 tkipicv_errors;
	/** TKIP replays count */
	t_u32 tkip_replays;
	/** CCMP decrypt errors count */
	t_u32 ccmp_decrypt_errors;
	/** CCMP replays count */
	t_u32 ccmp_replays;
	/** Tx amsdu count */
	t_u32 tx_amsdu_cnt;
	/** failed amsdu count */
	t_u32 failed_amsdu_cnt;
	/** retry amsdu count */
	t_u32 retry_amsdu_cnt;
	/** multi-retry amsdu count */
	t_u32 multi_retry_amsdu_cnt;
	/** Tx octets in amsdu count */
	t_u64 tx_octets_in_amsdu_cnt;
	/** amsdu ack failure count */
	t_u32 amsdu_ack_failure_cnt;
	/** Rx amsdu count */
	t_u32 rx_amsdu_cnt;
	/** Rx octets in amsdu count */
	t_u64 rx_octets_in_amsdu_cnt;
	/** Tx ampdu count */
	t_u32 tx_ampdu_cnt;
	/** tx mpdus in ampdu count */
	t_u32 tx_mpdus_in_ampdu_cnt;
	/** tx octets in ampdu count */
	t_u64 tx_octets_in_ampdu_cnt;
	/** ampdu Rx count */
	t_u32 ampdu_rx_cnt;
	/** mpdu in Rx ampdu count */
	t_u32 mpdu_in_rx_ampdu_cnt;
	/** Rx octets ampdu count */
	t_u64 rx_octets_in_ampdu_cnt;
	/** ampdu delimiter CRC error count */
	t_u32 ampdu_delimiter_crc_error_cnt;
    /** Rx Stuck Related Info*/
    /** Rx Stuck Issue count */
	t_u32 rx_stuck_issue_cnt[2];
    /** Rx Stuck Recovery count */
	t_u32 rx_stuck_recovery_cnt;
    /** Rx Stuck TSF */
	t_u64 rx_stuck_tsf[2];
    /** Tx Watchdog Recovery Related Info */
    /** Tx Watchdog Recovery count */
	t_u32 tx_watchdog_recovery_cnt;
    /** Tx Watchdog TSF */
	t_u64 tx_watchdog_tsf[2];
    /** Channel Switch Related Info */
    /** Channel Switch Announcement Sent */
	t_u32 channel_switch_ann_sent;
    /** Channel Switch State */
	t_u32 channel_switch_state;
    /** Register Class */
	t_u32 reg_class;
    /** Channel Number */
	t_u32 channel_number;
    /** Channel Switch Mode */
	t_u32 channel_switch_mode;
    /** Reset Rx Mac Count */
	t_u32 rx_reset_mac_recovery_cnt;
    /** ISR2 Not Done Count*/
	t_u32 rx_Isr2_NotDone_Cnt;
    /** GDMA Abort Count */
	t_u32 gdma_abort_cnt;
    /** Rx Reset MAC Count */
	t_u32 g_reset_rx_mac_cnt;
	//Ownership error counters
	/*Error Ownership error count */
	t_u32 dwCtlErrCnt;
	/*Control Ownership error count */
	t_u32 dwBcnErrCnt;
	/*Control Ownership error count */
	t_u32 dwMgtErrCnt;
	/*Control Ownership error count */
	t_u32 dwDatErrCnt;
	/*BIGTK MME good count */
	t_u32 bigtk_mmeGoodCnt;
	/*BIGTK Replay error count */
	t_u32 bigtk_replayErrCnt;
	/*BIGTK MIC error count */
	t_u32 bigtk_micErrCnt;
	/*BIGTK MME not included count */
	t_u32 bigtk_mmeNotFoundCnt;
} MLAN_PACK_END HostCmd_DS_802_11_GET_LOG;

/* maln wifi rate */
typedef MLAN_PACK_START struct _mlan_wifi_rate {
	/** 0: OFDM, 1:CCK, 2:HT 3:VHT 4..7 reserved */
	t_u8 preamble;
	/** 0:1x1, 1:2x2, 3:3x3, 4:4x4 */
	t_u8 nss;
	/** 0:20MHz, 1:40Mhz, 2:80Mhz, 3:160Mhz */
	t_u8 bw;
	/** OFDM/CCK rate code would be as per ieee std in the units of 0.5mbps
	 */
	/** HT/VHT it would be mcs index */
	t_u8 rateMcsIdx;
	/** units of 100 Kbps */
	t_u32 bitrate;
} MLAN_PACK_START mlan_wifi_rate;

/** channel information */
typedef MLAN_PACK_START struct {
	/** channel width (20, 40, 80, 80+80, 160) */
	t_u32 width;
	/** primary 20 MHz channel */
	t_u32 center_freq;
	/** center frequency (MHz) first segment */
	t_u32 center_freq0;
	/** center frequency (MHz) second segment */
	t_u32 center_freq1;
} MLAN_PACK_END mlan_wifi_channel_info;

/** channel statistics */
typedef MLAN_PACK_START struct {
	/** channel */
	mlan_wifi_channel_info channel;
	/** msecs the radio is awake (32 bits number accruing over time) */
	t_u32 on_time;
	/** msecs the CCA register is busy (32 bits number accruing over time)
	 */
	t_u32 cca_busy_time;
} MLAN_PACK_END mlan_wifi_channel_stat;

/** radio statistics */
typedef MLAN_PACK_START struct {
	/** supported wifi in case of multi radio */
	t_u32 radio;
	/** msecs the radio is awake */
	t_u32 on_time;
	/** msecs the radio is transmitting */
	t_u32 tx_time;
	/**  TBD: num_tx_levels: number of radio transmit power levels */
	t_u32 reserved0;
	/** TBD: tx_time_per_levels: pointer to an array of radio transmit per
	 * power levels in msecs accured over time */
	t_u32 reserved1;
	/** msecs the radio is in active receive */
	t_u32 rx_time;
	/** msecs the radio is awake due to all scan */
	t_u32 on_time_scan;
	/** msecs the radio is awake due to NAN */
	t_u32 on_time_nbd;
	/** msecs the radio is awake due to G?scan */
	t_u32 on_time_gscan;
	/** msecs the radio is awake due to roam?scan */
	t_u32 on_time_roam_scan;
	/** msecs the radio is awake due to PNO scan */
	t_u32 on_time_pno_scan;
	/** msecs the radio is awake due to HS2.0 scans and GAS exchange */
	t_u32 on_time_hs20;
	/** number of channels */
	t_u32 num_channels;
	/** channel statistics */
	mlan_wifi_channel_stat channels[MAX_NUM_CHAN];	// support only 1
	// channel, so keep it.
} MLAN_PACK_END mlan_wifi_radio_stat;

/** per rate statistics */
typedef MLAN_PACK_START struct {
	/** rate information */
	mlan_wifi_rate rate;
	/** number of successfully transmitted data pkts (ACK rcvd) */
	t_u32 tx_mpdu;
	/** number of received data pkts */
	t_u32 rx_mpdu;
	/** number of data packet losses (no ACK) */
	t_u32 mpdu_lost;
	/** total number of data pkt retries */
	t_u32 retries;
	/** number of short data pkt retries */
	t_u32 retries_short;
	/** number of long data pkt retries */
	t_u32 retries_long;
} MLAN_PACK_END mlan_wifi_rate_stat;

/** per peer statistics */
typedef MLAN_PACK_START struct {
	/** peer type (AP, TDLS, GO etc.) */
	t_u8 type;
	/** mac address */
	t_u8 peer_mac_address[6];
	/** peer WIFI_CAPABILITY_XXX */
	t_u32 capabilities;
	/** number of rates */
	t_u32 num_rate;
	/** per rate statistics, number of entries  = num_rate */
	mlan_wifi_rate_stat rate_stats[];
} MLAN_PACK_END mlan_wifi_peer_info;

/* per access category statistics */
typedef MLAN_PACK_START struct {
	/** access category (VI, VO, BE, BK) */
	t_u32 ac;
	/** number of successfully transmitted unicast data pkts (ACK rcvd) */
	t_u32 tx_mpdu;
	/** number of received unicast mpdus */
	t_u32 rx_mpdu;
	/** number of succesfully transmitted multicast data packets */
	/** STA case: implies ACK received from AP for the unicast packet in
	 * which mcast pkt was sent */
	t_u32 tx_mcast;
	/** number of received multicast data packets */
	t_u32 rx_mcast;
	/** number of received unicast a-mpdus */
	t_u32 rx_ampdu;
	/** number of transmitted unicast a-mpdus */
	t_u32 tx_ampdu;
	/** number of data pkt losses (no ACK) */
	t_u32 mpdu_lost;
	/** total number of data pkt retries */
	t_u32 retries;
	/** number of short data pkt retries */
	t_u32 retries_short;
	/** number of long data pkt retries */
	t_u32 retries_long;
	/** data pkt min contention time (usecs) */
	t_u32 contention_time_min;
	/** data pkt max contention time (usecs) */
	t_u32 contention_time_max;
	/** data pkt avg contention time (usecs) */
	t_u32 contention_time_avg;
	/** num of data pkts used for contention statistics */
	t_u32 contention_num_samples;
} MLAN_PACK_END mlan_wifi_wmm_ac_stat;

/** interface statistics */
typedef MLAN_PACK_START struct {
	/** access point beacon received count from connected AP */
	t_u32 beacon_rx;
	/** Average beacon offset encountered (beacon_TSF - TBTT)
	 *    the average_tsf_offset field is used so as to calculate the
	 *    typical beacon contention time on the channel as well may be
	 *    used to debug beacon synchronization and related power consumption
	 * issue
	 */
	t_u64 average_tsf_offset;
	/** indicate that this AP typically leaks packets beyond the driver
	 * guard time */
	t_u32 leaky_ap_detected;
	/** average number of frame leaked by AP after frame with PM bit set was
	 * ACK'ed by AP */
	t_u32 leaky_ap_avg_num_frames_leaked;
	/** Guard time currently in force (when implementing IEEE power
	 * management based on frame control PM bit), How long driver waits
	 * before shutting down the radio and after receiving an ACK for a data
	 * frame with PM bit set)
	 */
	t_u32 leaky_ap_guard_time;
	/** access point mgmt frames received count from connected AP (including
	 * Beacon) */
	t_u32 mgmt_rx;
	/** action frames received count */
	t_u32 mgmt_action_rx;
	/** action frames transmit count */
	t_u32 mgmt_action_tx;
	/** access Point Beacon and Management frames RSSI (averaged) */
	t_u32 rssi_mgmt;
	/** access Point Data Frames RSSI (averaged) from connected AP */
	t_u32 rssi_data;
	/** access Point ACK RSSI (averaged) from connected AP */
	t_u32 rssi_ack;
	/** per ac data packet statistics */
	mlan_wifi_wmm_ac_stat ac[MAX_AC_QUEUES];
	/** number of peers */
	t_u32 num_peers;
	/** per peer statistics */
	mlan_wifi_peer_info peer_info[];
} MLAN_PACK_END mlan_wifi_iface_stat;

/** MrvlIETypes_llStatIface_t */
typedef MLAN_PACK_START struct _MrvlIETypes_llStatIface_t {
	/** Type */
	t_u16 type;
	/** Length */
	t_u16 len;
	/** Frame Control */
	mlan_wifi_iface_stat ifaceStat;
	/* t_u8 frame_contents[]; */
} MLAN_PACK_END MrvlIETypes_llStatIface_t;

/** MrvlIETypes_llStatRadio_t */
typedef MLAN_PACK_START struct _MrvlIETypes_llStatRadio_t {
	/** Type */
	t_u16 type;
	/** Length */
	t_u16 len;
	/** Frame Control */
	mlan_wifi_radio_stat radioStat[MAX_RADIO];
	/* t_u8 frame_contents[]; */
} MLAN_PACK_END MrvlIETypes_llStatRadio_t;

#define TYPE_IFACE_STAT MBIT(0)
#define TYPE_RADIO_STAT MBIT(1)
#define TYPE_PEER_INFO MBIT(2)
/** HostCmd_DS_802_11_LINK_STATISTIC */
typedef MLAN_PACK_START struct _HostCmd_DS_802_11_LINK_STATISTIC {
	/** Action : HostCmd_ACT_GEN_GET/SET/REMOVE */
	t_u16 action;
	/** statistic which would be get in action HostCmd_ACT_GEN_GET :
	 * TYPE_IFACE_STAT/RADIO_STAT/PEER_INFO */
	t_u16 stat_type;
	/* threshold to classify the pkts as short or long, packet size <
	 * mpdu_size_threshold => short */
	t_u32 mpdu_size_threshold;
	/* set for field debug mode. Driver should collect all statistics
	 * regardless of performance impact. */
	t_u32 aggressive_statistics_gathering;
	/** Value */
	t_u8 value[];
} MLAN_PACK_END HostCmd_DS_802_11_LINK_STATISTIC;

/**_HostCmd_TX_RATE_QUERY */
typedef MLAN_PACK_START struct _HostCmd_TX_RATE_QUERY {
	/** Tx rate */
	t_u8 tx_rate;
    /** V14 FW: Ht Info
     * [Bit 0] RxRate format: LG=0, HT=1
     * [Bit 1] HT Bandwidth: BW20 = 0, BW40 = 1
     * [Bit 2] HT Guard Interval: LGI = 0, SGI = 1 */
	/** Tx Rate Info:
	 * [Bit 0-1] tx rate formate: LG = 0, HT = 1, VHT = 2
	 * [Bit 2-3] HT/VHT Bandwidth: BW20 = 0, BW40 = 1, BW80 = 2, BW160 = 3
	 * [Bit 4]   HT/VHT Guard Interval: LGI = 0, SGI = 1
	 * [Bit4,Bit7] AX Guard Interval: 00, 01, 02 */
	t_u8 tx_rate_info;
	/**
	 * BIT0: DCM
	 * BIT3-BIT1: tone mode
	 **  000: 26  tone
	 **  001: 52  tone
	 **  010: 106 tone
	 **  011: 242 tone
	 **  100: 484 tone
	 **  101: 996 tone
	 * BIT7-BIT4: resvd
	 **/
	t_u8 ext_tx_rate_info;
} MLAN_PACK_END HostCmd_TX_RATE_QUERY;

typedef MLAN_PACK_START struct _hs_config_param {
	/** bit0=1: broadcast data
	 * bit1=1: unicast data
	 * bit2=1: mac events
	 * bit3=1: multicast data
	 */
	t_u32 conditions;
	/** GPIO pin or 0xff for interface */
	t_u8 gpio;
	/** gap in milliseconds or or 0xff for special setting when
	 *  GPIO is used to wakeup host
	 */
	t_u8 gap;
} MLAN_PACK_END hs_config_param;

/** HS Action 0x0001 - Configure enhanced host sleep mode,
 * 0x0002 - Activate enhanced host sleep mode
 */
typedef enum _Host_Sleep_Action {
	HS_CONFIGURE = 0x0001,
	HS_ACTIVATE = 0x0002,
} Host_Sleep_Action;

/** Structure definition for activating enhanced hs */
typedef MLAN_PACK_START struct __hs_activate_param {
	/** response control 0x00 - response not needed, 0x01 - response needed
	 */
	t_u16 resp_ctrl;
} MLAN_PACK_END hs_activate_param;

/** HostCmd_DS_802_11_HS_CFG_ENH */
typedef MLAN_PACK_START struct _HostCmd_DS_802_11_HS_CFG_ENH {
	/** Action 0x0001 - Configure enhanced host sleep mode,
	 *  0x0002 - Activate enhanced host sleep mode
	 */
	t_u16 action;

	union {
		/** Configure enhanced hs */
		hs_config_param hs_config;
		/** Activate enhanced hs */
		hs_activate_param hs_activate;
	} params;
} MLAN_PACK_END HostCmd_DS_802_11_HS_CFG_ENH;

/** HostCmd_CMD_802_11_ROBUSTCOEX */
typedef MLAN_PACK_START struct _HostCmd_DS_802_11_ROBUSTCOEX {
	/** Action */
	t_u16 action;
	/** RSVD */
	t_u16 rsvd;
	t_u8 tlv_buf[];
} MLAN_PACK_END HostCmd_DS_802_11_ROBUSTCOEX;

/** HostCmd_CMD_DMCS_CFG */
typedef MLAN_PACK_START struct _HostCmd_DS_DMCS_CFG {
	/** Action */
	t_u16 action;
	/** SubCmd of DMCS */
	t_u16 subcmd;
	t_u8 tlv_buf[];
} MLAN_PACK_END HostCmd_DS_DMCS_CFG;

#if defined(PCIE)
/** HostCmd_CMD_SSU */
typedef MLAN_PACK_START struct _HostCmd_DS_SSU_CFG {
	/** Action */
	t_u16 action;
	/** # of FFT sample to skip */
	t_u32 nskip;
	/** # of FFT sample selected to dump */
	t_u32 nsel;
	/** Down sample ADC input for buffering */
	t_u32 adcdownsample;
	/** Mask Out ADC Data From Spectral Packet */
	t_u32 mask_adc_pkt;
	/** Enable 16-Bit FFT Output Data Precision in Spectral Packet */
	t_u32 out_16bits;
	/** Enable power spectrum in dB for spectral packet */
	t_u32 spec_pwr_enable;
	/** Enable Spectral Packet Rate Reduction in dB output format */
	t_u32 rate_deduction;
	/** # of Spectral packets over which spectral data to be averaged */
	t_u32 n_pkt_avg;
	/** ret: Calculated fft length in dw */
	t_u32 fft_len;
	/** ret: Calculated adc length in dw */
	t_u32 adc_len;
	/** ret: Calculated record length in dw */
	t_u32 rec_len;
	/** Mapped address of DMA buffer */
	t_u32 buffer_base_addr[2];
	/** Total size of allocated buffer for SSU DMA */
	t_u32 buffer_pool_size;
	/** ret: Calculated buffer numbers */
	t_u32 number_of_buffers;
	/** ret: Calculated buffer size in byte for each descriptor */
	t_u32 buffer_size;
} MLAN_PACK_END HostCmd_DS_SSU_CFG;
#endif

typedef MLAN_PACK_START struct _HostCmd_DS_HAL_PHY_CFG {
	/** Action */
	t_u16 action;
	/** 11b pwr spectral density mask enable/disable */
	t_u8 dot11b_psd_mask_cfg;
	/** reserved fields for future hal/phy cfg use */
	t_u8 reserved[7];
} MLAN_PACK_END HostCmd_DS_HAL_PHY_CFG;

/** SNMP_MIB_INDEX */
typedef enum _SNMP_MIB_INDEX {
	OpRateSet_i = 1,
	DtimPeriod_i = 3,
	RtsThresh_i = 5,
	ShortRetryLim_i = 6,
	LongRetryLim_i = 7,
	FragThresh_i = 8,
	Dot11D_i = 9,
	Dot11H_i = 10,
	WwsMode_i = 17,
	Thermal_i = 34,
	NullPktPeriod_i = 37,
	SignalextEnable_i = 41,
	ECSAEnable_i = 42,
	StopDeauth_i = 44,
} SNMP_MIB_INDEX;

/** max SNMP buf size */
#define MAX_SNMP_BUF_SIZE 128

#ifdef UAP_SUPPORT
/**  HostCmd_CMD_802_11_SNMP_MIB */
typedef MLAN_PACK_START struct _HostCmd_DS_UAP_802_11_SNMP_MIB {
	/** SNMP query type */
	t_u16 query_type;
	/** snmp oid buf */
	t_u8 snmp_data[];
} MLAN_PACK_END HostCmd_DS_UAP_802_11_SNMP_MIB;
#endif

/**  HostCmd_CMD_802_11_SNMP_MIB */
typedef MLAN_PACK_START struct _HostCmd_DS_802_11_SNMP_MIB {
	/** SNMP query type */
	t_u16 query_type;
	/** SNMP object ID */
	t_u16 oid;
	/** SNMP buffer size */
	t_u16 buf_size;
	/** Value */
	t_u8 value[1];
} MLAN_PACK_END HostCmd_DS_802_11_SNMP_MIB;

/** Radio on */
#define RADIO_ON 0x01
/** Radio off */
#define RADIO_OFF 0x00

/** HostCmd_CMD_802_11_RADIO_CONTROL */
typedef MLAN_PACK_START struct _HostCmd_DS_802_11_RADIO_CONTROL {
	/** Action */
	t_u16 action;
	/** Control */
	t_u16 control;
} MLAN_PACK_END HostCmd_DS_802_11_RADIO_CONTROL;

/** MrvlRateScope_t */
typedef MLAN_PACK_START struct _MrvlRateScope_t {
	/** Header Type */
	t_u16 type;
	/** Header Length */
	t_u16 length;
	/** Bitmap of HR/DSSS rates */
	t_u16 hr_dsss_rate_bitmap;
	/** Bitmap of OFDM rates */
	t_u16 ofdm_rate_bitmap;
	/** Bitmap of HT-MCSs allowed for initial rate */
	t_u16 ht_mcs_rate_bitmap[8];
	t_u16 vht_mcs_rate_bitmap[8];
	t_u16 he_mcs_rate_bitmap[8];
} MLAN_PACK_END MrvlRateScope_t;

/** MrvlRateDropPattern_t */
typedef MLAN_PACK_START struct _MrvlRateDropPattern_t {
	/** Header Type */
	t_u16 type;
	/** Header Length */
	t_u16 length;
	/** Rate Drop Mode */
	t_u32 rate_drop_mode;
	/* MrvlRateDropControl_t RateDropControl[]; */
} MLAN_PACK_END MrvlRateDropPattern_t;

typedef MLAN_PACK_START struct _MrvlIETypes_rate_setting_t {
	/** Header */
	MrvlIEtypesHeader_t header;
	/** Rate Setting */
	t_u16 rate_setting;
} MLAN_PACK_END MrvlIETypes_rate_setting_t;

/** HostCmd_DS_TX_RATE_CFG */
typedef MLAN_PACK_START struct _HostCmd_DS_TX_RATE_CFG {
	/** Action */
	t_u16 action;
    /** V14 FW: cfg_index */
    /** V15+ FW: reserved_1 */
	t_u16 cfg_index;
	/* MrvlRateScope_t RateScope;
	 * MrvlRateDropPattern_t RateDrop; */
	t_u8 tlv_buf[];
} MLAN_PACK_END HostCmd_DS_TX_RATE_CFG;

/** Power_Group_t */
typedef MLAN_PACK_START struct _Power_Group_t {
	/** Modulation Class */
	t_u8 modulation_class;
	/** MCS Code or Legacy RateID */
	t_u8 first_rate_code;
	/** MCS Code or Legacy RateID */
	t_u8 last_rate_code;
	/** Power Adjustment Step */
	t_s8 power_step;
	/** Minimal Tx Power Level [dBm] */
	t_s8 power_min;
	/** Maximal Tx Power Level [dBm] */
	t_s8 power_max;
	/** 0: HTBW20, 1: HTBW40 */
	t_u8 ht_bandwidth;
	/** Reserved */
	t_u8 reserved;
} MLAN_PACK_END Power_Group_t;

/** MrvlTypes_Power_Group_t */
typedef MLAN_PACK_START struct _MrvlTypes_Power_Group_t {
	/** Header Type */
	t_u16 type;
	/** Header Length */
	t_u16 length;
	/* Power_Group_t PowerGroups */
} MLAN_PACK_END MrvlTypes_Power_Group_t;

/** HostCmd_CMD_TXPWR_CFG */
typedef MLAN_PACK_START struct _HostCmd_DS_TXPWR_CFG {
	/** Action */
	t_u16 action;
	/** Power group configuration index */
	t_u16 cfg_index;
	/** Power group configuration mode */
	t_u32 mode;
	/* MrvlTypes_Power_Group_t PowerGrpCfg[] */
	t_u8 tlv_buf[];
} MLAN_PACK_END HostCmd_DS_TXPWR_CFG;

/** HostCmd_CMD_802_11_RF_TX_POWER */
typedef MLAN_PACK_START struct _HostCmd_DS_802_11_RF_TX_POWER {
	/** Action */
	t_u16 action;
	/** Current power level */
	t_s16 current_level;
	/** Maximum power */
	t_s8 max_power;
	/** Minimum power */
	t_s8 min_power;
} MLAN_PACK_END HostCmd_DS_802_11_RF_TX_POWER;

/** Connection type infra */
#define CONNECTION_TYPE_INFRA 0
/** Connection type adhoc */
#define CONNECTION_TYPE_ADHOC 1
#ifdef WIFI_DIRECT_SUPPORT
/** BSS Mode: WIFIDIRECT Client */
#define BSS_MODE_WIFIDIRECT_CLIENT 0
/** BSS Mode: WIFIDIRECT GO */
#define BSS_MODE_WIFIDIRECT_GO 2
#endif
/** HostCmd_DS_SET_BSS_MODE */
typedef MLAN_PACK_START struct _HostCmd_DS_SET_BSS_MODE {
	/** connection type */
	t_u8 con_type;
} MLAN_PACK_END HostCmd_DS_SET_BSS_MODE;

/** HT Capabilities element */
typedef MLAN_PACK_START struct _MrvlIETypes_HTCap_t {
	/** Header */
	MrvlIEtypesHeader_t header;

	/** HTCap struct */
	HTCap_t ht_cap;
} MLAN_PACK_END MrvlIETypes_HTCap_t;
/** VHT Capabilities element */
typedef MLAN_PACK_START struct _MrvlIETypes_VHTCap_t {
	/** Header */
	MrvlIEtypesHeader_t header;

	/** VHTCap struct */
	VHT_capa_t vht_cap;
} MLAN_PACK_END MrvlIETypes_VHTCap_t;

/** HostCmd_DS_REMAIN_ON_CHANNEL */
typedef MLAN_PACK_START struct _HostCmd_DS_REMAIN_ON_CHANNEL {
	/** Action 0-GET, 1-SET, 4 CLEAR*/
	t_u16 action;
	/** Not used set to zero */
	t_u8 status;
	/** Not used set to zero */
	t_u8 reserved;
	/** Band cfg */
	Band_Config_t bandcfg;
	/** channel */
	t_u8 channel;
	/** remain time: Unit ms*/
	t_u32 remain_period;
} MLAN_PACK_END HostCmd_DS_REMAIN_ON_CHANNEL;

#ifdef WIFI_DIRECT_SUPPORT
/** HostCmd_DS_WIFI_DIRECT_MODE */
typedef MLAN_PACK_START struct _HostCmd_DS_WIFI_DIRECT_MODE {
	/** Action 0-GET, 1-SET*/
	t_u16 action;
	/**0:disable 1:listen 2:GO 3:p2p client 4:find 5:stop find*/
	t_u16 mode;
} MLAN_PACK_END HostCmd_DS_WIFI_DIRECT_MODE;

/** MrvlIEtypes_NoA_setting_t */
typedef MLAN_PACK_START struct _MrvlIEtypes_NoA_setting_t {
	/** Header */
	MrvlIEtypesHeader_t header;
	/** enable/disable */
	t_u8 enable;
	/** index */
	t_u16 index;
	/** NoA count */
	t_u8 noa_count;
	/** NoA duration */
	t_u32 noa_duration;
	/** NoA interval */
	t_u32 noa_interval;
} MLAN_PACK_END MrvlIEtypes_NoA_setting_t;

/** MrvlIEtypes_NoA_setting_t */
typedef MLAN_PACK_START struct _MrvlIEtypes_OPP_PS_setting_t {
	/** Header */
	MrvlIEtypesHeader_t header;
	/** enable/disable && ct_window */
	t_u8 enable;
} MLAN_PACK_END MrvlIEtypes_OPP_PS_setting_t;

/** HostCmd_DS_WIFI_DIRECT_PARAM_CONFIG */
typedef MLAN_PACK_START struct _HostCmd_DS_WIFI_DIRECT_PARAM_CONFIG {
	/** Action 0-GET, 1-SET */
	t_u16 action;
	/** MrvlIEtypes_NoA_setting_t
	 *  MrvlIEtypes_OPP_PS_setting_t
	 */
	t_u8 tlv_buf[];
} MLAN_PACK_END HostCmd_DS_WIFI_DIRECT_PARAM_CONFIG;
#endif

/** MrvlIEtypes_GPIO_TSF_LATCH_CONFIG*/
typedef MLAN_PACK_START struct _MrvlIEtypes_GPIO_TSF_LATCH_CONFIG {
	/** Header */
	MrvlIEtypesHeader_t header;
	/**clock sync Mode */
	t_u8 clock_sync_mode;
	/**clock sync Role */
	t_u8 clock_sync_Role;
	/**clock sync GPIO Pin Number */
	t_u8 clock_sync_gpio_pin_number;
	/**clock sync GPIO Level or Toggle */
	t_u8 clock_sync_gpio_level_toggle;
	/**clock sync GPIO Pulse Width */
	t_u16 clock_sync_gpio_pulse_width;

} MLAN_PACK_END MrvlIEtypes_GPIO_TSF_LATCH_CONFIG;

/** MrvlIEtypes_GPIO_TSF_LATCH_REPORT */
typedef MLAN_PACK_START struct _MrvlIEtypes_GPIO_TSF_LATCH_REPORT {
	/** Header */
	MrvlIEtypesHeader_t header;
	/**get tsf info format */
	t_u16 tsf_format;
	/**tsf info */
	t_u16 tsf_info;
	/**tsf */
	t_u64 tsf;
	/**Positive or negative offset in microsecond from Beacon TSF to GPIO toggle TSF  */
	t_s32 tsf_offset;
} MLAN_PACK_END MrvlIEtypes_GPIO_TSF_LATCH_REPORT;

/** HostCmd_DS_GPIO_TSF_LATCH_PARAM_CONFIG */
typedef MLAN_PACK_START struct _HostCmd_DS_GPIO_TSF_LATCH_PARAM_CONFIG {
	/** Action 0-GET, 1-SET */
	t_u16 action;
	/** MrvlIEtypes_GPIO_TSF_LATCH_CONFIG
	 *  MrvlIEtypes_GPIO_TSF_LATCH_REPORT
	 */
	t_u8 tlv_buf[];
} MLAN_PACK_END HostCmd_DS_GPIO_TSF_LATCH_PARAM_CONFIG;

MLAN_PACK_START struct coalesce_filt_field_param {
	t_u8 operation;
	t_u8 operand_len;
	t_u16 offset;
	t_u8 operand_byte_stream[4];
} MLAN_PACK_END;

MLAN_PACK_START struct coalesce_receive_filt_rule {
	MrvlIEtypesHeader_t header;
	t_u8 num_of_fields;
	t_u8 pkt_type;
	t_u16 max_coalescing_delay;
	struct coalesce_filt_field_param params[1];
} MLAN_PACK_END;

/** HostCmd_DS_COALESCE_CONFIG */
typedef MLAN_PACK_START struct _HostCmd_DS_COALESCE_CONFIG {
	/** Action 0-GET, 1-SET */
	t_u16 action;
	t_u16 num_of_rules;
	struct coalesce_receive_filt_rule rule[1];
} MLAN_PACK_END HostCmd_DS_COALESCE_CONFIG;

/** TLV type : FW support max connection TLV */
#define TLV_TYPE_MAX_CONN (PROPRIETARY_TLV_BASE_ID + 0x117)	/* 0x0217 */
/** MrvlIEtypes_Max_Conn_t */
typedef MLAN_PACK_START struct _MrvlIEtypes_Max_Conn_t {
	/** Header */
	MrvlIEtypesHeader_t header;
	/** FW support max P2P connection */
	t_u8 max_p2p_conn;
	/** FW support max STA connection */
	t_u8 max_sta_conn;
} MLAN_PACK_END MrvlIEtypes_Max_Conn_t;

/** exceed max p2p connection event */
typedef MLAN_PACK_START struct _event_exceed_max_p2p_conn {
	/** Event ID */
	t_u16 event_id;
	/** BSS index number for multiple BSS support */
	t_u8 bss_index;
	/** BSS type */
	t_u8 bss_type;
	/** When exceed max, the mac address who request p2p connect */
	t_u8 peer_mac_addr[MLAN_MAC_ADDR_LENGTH];
} MLAN_PACK_END event_exceed_max_p2p_conn;

#ifdef STA_SUPPORT

/**
 * @brief Structure used internally in the wlan driver to configure a scan.
 *
 * Sent to the command process module to configure the firmware
 *   scan command prepared by wlan_cmd_802_11_scan.
 *
 * @sa wlan_scan_networks
 *
 */
typedef MLAN_PACK_START struct _wlan_scan_cmd_config {
	/**
	 *  BSS Type to be sent in the firmware command
	 *
	 *  Field can be used to restrict the types of networks returned in the
	 *    scan.  Valid settings are:
	 *
	 *   - MLAN_SCAN_MODE_BSS  (infrastructure)
	 *   - MLAN_SCAN_MODE_IBSS (adhoc)
	 *   - MLAN_SCAN_MODE_ANY  (unrestricted, adhoc and infrastructure)
	 */
	t_u8 bss_mode;

	/**
	 *  Specific BSSID used to filter scan results in the firmware
	 */
	t_u8 specific_bssid[MLAN_MAC_ADDR_LENGTH];

	/**
	 *  Length of TLVs sent in command starting at tlvBuffer
	 */
	t_u32 tlv_buf_len;

	/**
	 *  SSID TLV(s) and ChanList TLVs to be sent in the firmware command
	 *
	 *  TLV_TYPE_CHANLIST, MrvlIEtypes_ChanListParamSet_t
	 *  TLV_TYPE_SSID, MrvlIEtypes_SsIdParamSet_t
	 */
	t_u8 tlv_buf[1];	/* SSID TLV(s) and ChanList TLVs are stored here */
} MLAN_PACK_END wlan_scan_cmd_config;

/**
 *  Sructure to retrieve the scan table
 */
typedef MLAN_PACK_START struct {
	/**
	 *  - Zero based scan entry to start retrieval in command request
	 *  - Number of scans entries returned in command response
	 */
	t_u32 scan_number;
	/**
	 * Buffer marker for multiple wlan_ioctl_get_scan_table_entry
	 * structures. Each struct is padded to the nearest 32 bit boundary.
	 */
	t_u8 scan_table_entry_buf[1];
} MLAN_PACK_END wlan_get_scan_table_info;

/** Generic structure defined for parsing WPA/RSN IEs for GTK/PTK OUIs */
typedef MLAN_PACK_START struct {
	/** Group key oui */
	t_u8 GrpKeyOui[4];
	/** Number of PTKs */
	t_u8 PtkCnt[2];
	/** Ptk body starts here */
	t_u8 PtkBody[4];
} MLAN_PACK_END IEBody;
#endif /* STA_SUPPORT */

/*
 * This scan handle Country Information IE(802.11d compliant)
 * Define data structure for HostCmd_CMD_802_11_SCAN
 */
/** HostCmd_DS_802_11_SCAN */
typedef MLAN_PACK_START struct _HostCmd_DS_802_11_SCAN {
	/** BSS mode */
	t_u8 bss_mode;
	/** BSSID */
	t_u8 bssid[MLAN_MAC_ADDR_LENGTH];
	/** TLV buffer */
	t_u8 tlv_buffer[1];
	/** MrvlIEtypes_SsIdParamSet_t      SsIdParamSet;
	 *  MrvlIEtypes_ChanListParamSet_t  ChanListParamSet;
	 *  MrvlIEtypes_RatesParamSet_t     OpRateSet;
	 */
} MLAN_PACK_END HostCmd_DS_802_11_SCAN;

/** fw_cap_info bit to indicate enhance ext scan type */
#define ENHANCE_EXT_SCAN_ENABLE MBIT(19)
/** mlan_event_scan_result data structure */
typedef MLAN_PACK_START struct _mlan_event_scan_result {
	/** Event ID */
	t_u16 event_id;
	/** BSS index number for multiple BSS support */
	t_u8 bss_index;
	/** BSS type */
	t_u8 bss_type;
	/** More event available or not */
	t_u8 more_event;
	/** Reserved */
	t_u8 reserved[3];
	/** Size of the response buffer */
	t_u16 buf_size;
	/** Number of BSS in scan response */
	t_u8 num_of_set;
} MLAN_PACK_END mlan_event_scan_result, *pmlan_event_scan_result;

/** ext scan status report event */
typedef MLAN_PACK_START struct _mlan_event_scan_status {
	/** Event ID */
	t_u16 event_id;
	/** BSS index number for multiple BSS support */
	t_u8 bss_index;
	/** BSS type */
	t_u8 bss_type;
	/** scan status */
	t_u8 scan_status;
	/** result */
	t_u16 buf_len;
	/** event buf */
	t_u8 event_buf[];
} MLAN_PACK_END mlan_event_scan_status, *pmlan_event_scan_status;

/*
 * This scan handle Country Information IE(802.11d compliant)
 * Define data structure for HostCmd_CMD_802_11_SCAN_EXT
 */
/** HostCmd_DS_802_11_SCAN_EXT */
typedef MLAN_PACK_START struct _HostCmd_DS_802_11_SCAN_EXT {
	/** Scan type for ext scan
	 *  0: default type: cmd resp after ext scan report event
	 *  1: enhanced type: cmd resp before ext scan report event
	 *  2: scan cancelled: cancel scan during scan processing
	 */
	t_u8 ext_scan_type;
	/** Reserved */
	t_u8 reserved[3];
	/** TLV buffer */
	t_u8 tlv_buffer[1];
	/** MrvlIEtypes_Bssid_List_t            BssIdList;
	 *  MrvlIEtypes_SsIdParamSet_t          SSIDParamSet;
	 *  MrvlIEtypes_ChanListParamSet_t      ChanListParamSet;
	 *  MrvlIEtypes_RatesParamSet_t         OpRateSet;
	 *  MrvlIEtypes_NumProbes_t             NumProbes;
	 *  MrvlIEtypes_WildCardSsIdParamSet_t  WildCardSSIDParamSet;
	 *  MrvlIEtypes_BssMode_t               BssMode;
	 */
} MLAN_PACK_END HostCmd_DS_802_11_SCAN_EXT;

/** MrvlIEtypes_BssMode */
typedef MLAN_PACK_START struct _MrvlIEtypes_BssMode_t {
	/** Header */
	MrvlIEtypesHeader_t header;
	/* INFRA/IBSS/AUTO */
	t_u8 bss_mode;
} MLAN_PACK_END MrvlIEtypes_BssMode_t;

/** BSS scan Rsp */
typedef MLAN_PACK_START struct _MrvlIEtypes_Bss_Scan_Rsp_t {
	/** Header */
	MrvlIEtypesHeader_t header;
	/** BSSID of the BSS descriptor */
	t_u8 bssid[MLAN_MAC_ADDR_LENGTH];
	/** Beacon/Probe response buffer */
	t_u8 frame_body[1];
} MLAN_PACK_END MrvlIEtypes_Bss_Scan_Rsp_t;

typedef MLAN_PACK_START struct _MrvlIEtypes_Bss_Scan_Info_t {
	/** Header */
	MrvlIEtypesHeader_t header;
	/** RSSI for scan entry */
	t_s16 rssi;
	/** Channel ANPI */
	t_s16 anpi;
	/** Channel load (parts per 255) */
	t_u8 cca_busy_fraction;
	/** Band */
	Band_Config_t bandcfg;
	/** Channel */
	t_u8 channel;
	/** Reserved */
	t_u8 reserved;
	/** TSF data */
	t_u64 tsf;
} MLAN_PACK_END MrvlIEtypes_Bss_Scan_Info_t;

/** HostCmd_DS_RX_MGMT_IND */
typedef MLAN_PACK_START struct _HostCmd_DS_RX_MGMT_IND {
	/** Action */
	t_u16 action;
	/** Mgmt frame subtype mask */
	t_u32 mgmt_subtype_mask;
} MLAN_PACK_END HostCmd_DS_RX_MGMT_IND;

/** HostCmd_DS_802_11_SCAN_RSP */
typedef MLAN_PACK_START struct _HostCmd_DS_802_11_SCAN_RSP {
	/** Size of BSS descriptor */
	t_u16 bss_descript_size;
	/** Numner of sets */
	t_u8 number_of_sets;
	/** BSS descriptor and TLV buffer */
	t_u8 bss_desc_and_tlv_buffer[1];
} MLAN_PACK_END HostCmd_DS_802_11_SCAN_RSP;

/** HostCmd_DS_802_11_BG_SCAN_CONFIG */
typedef MLAN_PACK_START struct _HostCmd_DS_802_11_BG_SCAN_CONFIG {
	/** action */
	t_u16 action;
	/** 0: disable, 1: enable */
	t_u8 enable;
	/** bss type */
	t_u8 bss_type;
	/** num of channel per scan */
	t_u8 chan_per_scan;
	/** reserved field */
	t_u8 reserved;
	/** reserved field */
	t_u16 reserved1;
	/** interval between consecutive scans */
	t_u32 scan_interval;
	/** reserved field */
	t_u32 reserved2;
	/** condition to trigger report to host */
	t_u32 report_condition;
	/** reserved field */
	t_u16 reserved3;
} MLAN_PACK_END HostCmd_DS_802_11_BG_SCAN_CONFIG;

/** HostCmd_DS_802_11_BG_SCAN_QUERY */
typedef MLAN_PACK_START struct _HostCmd_DS_802_11_BG_SCAN_QUERY {
	/** Flush */
	t_u8 flush;
} MLAN_PACK_END HostCmd_DS_802_11_BG_SCAN_QUERY;

/** HostCmd_DS_802_11_BG_SCAN_QUERY_RSP */
typedef MLAN_PACK_START struct _HostCmd_DS_802_11_BG_SCAN_QUERY_RSP {
	/** Report condition */
	t_u32 report_condition;
	/** Scan response */
	HostCmd_DS_802_11_SCAN_RSP scan_resp;
} MLAN_PACK_END HostCmd_DS_802_11_BG_SCAN_QUERY_RSP;

/** MrvlIEtypes_StartLater_t */
typedef MLAN_PACK_START struct _MrvlIEtypes_StartLater_t {
	/** Header */
	MrvlIEtypesHeader_t header;
	/* 0 - BGScan start immediately, 1 - BGScan will start later after "Scan
	 * Interval" */
	t_u16 value;
} MLAN_PACK_END MrvlIEtypes_StartLater_t;

/** MrvlIEtypes_RepeatCount_t */
typedef MLAN_PACK_START struct _MrvlIEtypes_RepeatCount_t {
	/** Header */
	MrvlIEtypesHeader_t header;
	/* Repeat count */
	t_u16 repeat_count;
} MLAN_PACK_END MrvlIEtypes_RepeatCount_t;

/** MrvlIEtypes_DomainParamSet_t */
typedef MLAN_PACK_START struct _MrvlIEtypes_DomainParamSet {
	/** Header */
	MrvlIEtypesHeader_t header;
	/** Country code */
	t_u8 country_code[COUNTRY_CODE_LEN];
	/** Set of subbands */
	IEEEtypes_SubbandSet_t sub_band[1];
} MLAN_PACK_END MrvlIEtypes_DomainParamSet_t;

/** HostCmd_DS_802_11D_DOMAIN_INFO */
typedef MLAN_PACK_START struct _HostCmd_DS_802_11D_DOMAIN_INFO {
	/** Action */
	t_u16 action;
	/** Domain parameter set */
	MrvlIEtypes_DomainParamSet_t domain;
} MLAN_PACK_END HostCmd_DS_802_11D_DOMAIN_INFO;

/** HostCmd_DS_802_11D_DOMAIN_INFO_RSP */
typedef MLAN_PACK_START struct _HostCmd_DS_802_11D_DOMAIN_INFO_RSP {
	/** Action */
	t_u16 action;
	/** Domain parameter set */
	MrvlIEtypes_DomainParamSet_t domain;
} MLAN_PACK_END HostCmd_DS_802_11D_DOMAIN_INFO_RSP;

/** HostCmd_DS_11N_ADDBA_REQ */
typedef MLAN_PACK_START struct _HostCmd_DS_11N_ADDBA_REQ {
	/** Result of the ADDBA Request Operation */
	t_u8 add_req_result;
	/** Peer MAC address */
	t_u8 peer_mac_addr[MLAN_MAC_ADDR_LENGTH];
	/** Dialog Token */
	t_u8 dialog_token;
	/** Block Ack Parameter Set */
	t_u16 block_ack_param_set;
	/** Block Act Timeout Value */
	t_u16 block_ack_tmo;
	/** Starting Sequence Number */
	t_u16 ssn;
} MLAN_PACK_END HostCmd_DS_11N_ADDBA_REQ;

/** HostCmd_DS_11N_ADDBA_RSP */
typedef MLAN_PACK_START struct _HostCmd_DS_11N_ADDBA_RSP {
	/** Result of the ADDBA Response Operation */
	t_u8 add_rsp_result;
	/** Peer MAC address */
	t_u8 peer_mac_addr[MLAN_MAC_ADDR_LENGTH];
	/** Dialog Token */
	t_u8 dialog_token;
	/** Status Code */
	t_u16 status_code;
	/** Block Ack Parameter Set */
	t_u16 block_ack_param_set;
	/** Block Act Timeout Value */
	t_u16 block_ack_tmo;
	/** Starting Sequence Number */
	t_u16 ssn;
} MLAN_PACK_END HostCmd_DS_11N_ADDBA_RSP;

/** HostCmd_DS_11N_DELBA */
typedef MLAN_PACK_START struct _HostCmd_DS_11N_DELBA {
	/** Result of the ADDBA Request Operation */
	t_u8 del_result;
	/** Peer MAC address */
	t_u8 peer_mac_addr[MLAN_MAC_ADDR_LENGTH];
	/** Delete Block Ack Parameter Set */
	t_u16 del_ba_param_set;
	/** Reason Code sent for DELBA */
	t_u16 reason_code;
	/** Reserved */
	t_u8 reserved;
} MLAN_PACK_END HostCmd_DS_11N_DELBA;

/** HostCmd_DS_11N_BATIMEOUT */
typedef MLAN_PACK_START struct _HostCmd_DS_11N_BATIMEOUT {
	/** TID */
	t_u8 tid;
	/** Peer MAC address */
	t_u8 peer_mac_addr[MLAN_MAC_ADDR_LENGTH];
	/** Delete Block Ack Parameter Set */
	t_u8 origninator;
} MLAN_PACK_END HostCmd_DS_11N_BATIMEOUT;

/** HostCmd_DS_11N_CFG */
typedef MLAN_PACK_START struct _HostCmd_DS_11N_CFG {
	/** Action */
	t_u16 action;
	/** HTTxCap */
	t_u16 ht_tx_cap;
	/** HTTxInfo */
	t_u16 ht_tx_info;
	/** Misc configuration */
	t_u16 misc_config;
} MLAN_PACK_END HostCmd_DS_11N_CFG;

/** HostCmd_DS_11N_CFG */
typedef MLAN_PACK_START struct _HostCmd_DS_REJECT_ADDBA_REQ {
	/** Action */
	t_u16 action;
	/** Bit0    : host sleep activated
	 *  Bit1    : auto reconnect enabled
	 *  Others  : reserved
	 */
	t_u32 conditions;
} MLAN_PACK_END HostCmd_DS_REJECT_ADDBA_REQ;

/** HostCmd_DS_TXBUF_CFG*/
typedef MLAN_PACK_START struct _HostCmd_DS_TXBUF_CFG {
	/** Action */
	t_u16 action;
	/** Buffer Size */
	t_u16 buff_size;
	/** End Port_for Multiport */
	t_u16 mp_end_port;
	/** Reserved */
	t_u16 reserved3;
} MLAN_PACK_END HostCmd_DS_TXBUF_CFG;

/** HostCmd_DS_AMSDU_AGGR_CTRL */
typedef MLAN_PACK_START struct _HostCmd_DS_AMSDU_AGGR_CTRL {
	/** Action */
	t_u16 action;
	/** Enable */
	t_u16 enable;
	/** Get the current Buffer Size valid */
	t_u16 curr_buf_size;
} MLAN_PACK_END HostCmd_DS_AMSDU_AGGR_CTRL;

/** HostCmd_DS_11AC_CFG */
typedef MLAN_PACK_START struct _HostCmd_DS_11AC_CFG {
	/** Action */
	t_u16 action;
	/** BandConfig */
	t_u8 band_config;
	/** Misc Configuration */
	t_u8 misc_config;
	/** VHT Capability Info */
	t_u32 vht_cap_info;
	/** VHT Support MCS Set */
	t_u8 vht_supp_mcs_set[VHT_MCS_SET_LEN];
} MLAN_PACK_END HostCmd_DS_11AC_CFG;

/** HostCmd_DS_11ACTXBUF_CFG*/
typedef MLAN_PACK_START struct _HostCmd_DS_11ACTXBUF_CFG {
	/** Action */
	t_u16 action;
	/** Buffer Size */
	t_u16 buff_size;
	/** End Port_for Multiport */
	t_u16 mp_end_port;
	/** Reserved */
	t_u16 reserved3;
} MLAN_PACK_END HostCmd_DS_11ACTXBUF_CFG;

/** HostCmd_DS_11AX_CFG */
typedef MLAN_PACK_START struct _HostCmd_DS_11AX_CFG {
	/** Action */
	t_u16 action;
	/** BandConfig */
	t_u8 band_config;
	/** TLV for HE capability or HE operation */
	t_u8 val[];
} MLAN_PACK_END HostCmd_DS_11AX_CFG;

/** HostCmd_DS_11AX_CMD_CFG */
typedef MLAN_PACK_START struct _HostCmd_DS_11AX_CMD_CFG {
	/** Action */
	t_u16 action;
	/** CMD_SUBID */
	t_u16 sub_id;
	/** TLV or value for cmd */
	t_u8 val[];
} MLAN_PACK_END HostCmd_DS_11AX_CMD_CFG;

/** HostCmd_DS_RANGE_EXT */
typedef MLAN_PACK_START struct _HostCmd_DS_RANGE_EXT {
	/** Action */
	t_u16 action;
	/** Range ext mode */
	t_u8 mode;
} MLAN_PACK_END HostCmd_DS_RANGE_EXT;

/** Type definition of hostcmd_twt_setup */
typedef struct MLAN_PACK_START _hostcmd_twt_setup {
	/** Implicit, 0: TWT session is explicit, 1: Session is implicit */
	t_u8 implicit;
	/** Announced, 0: Unannounced, 1: Announced TWT */
	t_u8 announced;
	/** Trigger Enabled, 0: Non-Trigger enabled, 1: Trigger enabled TWT */
	t_u8 trigger_enabled;
	/** TWT Information Disabled, 0: TWT info enabled, 1: TWT info disabled
	 */
	t_u8 twt_info_disabled;
	/** Negotiation Type, 0: Future Individual TWT SP start time, 1: Next
	 * Wake TBTT time */
	t_u8 negotiation_type;
	/** TWT Wakeup Duration, time after which the TWT requesting STA can
	 * transition to doze state */
	t_u8 twt_wakeup_duration;
	/** Flow Identifier. Range: [0-7]*/
	t_u8 flow_identifier;
	/** Hard Constraint, 0: FW can tweak the TWT setup parameters if it is
	 *rejected by AP.
	 ** 1: Firmware should not tweak any parameters. */
	t_u8 hard_constraint;
	/** TWT Exponent, Range: [0-63] */
	t_u8 twt_exponent;
	/** TWT Mantissa Range: [0-sizeof(UINT16)] */
	t_u16 twt_mantissa;
    /** TWT Request Type, 0: REQUEST_TWT, 1: SUGGEST_TWT*/
	t_u8 twt_request;
	/** TWT Setup State. Set to 0 by driver, filled by FW in response*/
	t_u8 twt_setup_state;
	/** Reserved, set to 0. */
	t_u8 reserved[2];
} MLAN_PACK_END hostcmd_twt_setup, *phostcmd_twt_setup;

/** Type definition of hostcmd_twt_teardown */
typedef struct MLAN_PACK_START _hostcmd_twt_teardown {
	/** TWT Flow Identifier. Range: [0-7] */
	t_u8 flow_identifier;
	/** Negotiation Type. 0: Future Individual TWT SP start time, 1: Next
	 * Wake TBTT time */
	t_u8 negotiation_type;
	/** Tear down all TWT. 1: To teardown all TWT, 0 otherwise */
	t_u8 teardown_all_twt;
	/** TWT Teardown State. Set to 0 by driver, filled by FW in response */
	t_u8 twt_teardown_state;
	/** Reserved, set to 0. */
	t_u8 reserved[3];
} MLAN_PACK_END hostcmd_twt_teardown, *phostcmd_twt_teardown;

/** HostCmd_DS_TWT_CFG */
typedef MLAN_PACK_START struct _HostCmd_DS_TWT_CFG {
	/** Action */
	t_u16 action;
	/** CMD_SUBID */
	t_u16 sub_id;
	/** TWT Setup/Teardown configuration parameters */
	union {
		/** TWT Setup config for Sub ID: MLAN_11AX_TWT_SETUP_SUBID */
		hostcmd_twt_setup twt_setup;
		/** TWT Teardown config for Sub ID: MLAN_11AX_TWT_TEARDOWN_SUBID
		 */
		hostcmd_twt_teardown twt_teardown;
	} param;
} MLAN_PACK_END HostCmd_DS_TWT_CFG;

/** HostCmd_DS_ECL_SYSTEM_CLOCK_CONFIG */
typedef MLAN_PACK_START struct _HostCmd_DS_ECL_SYSTEM_CLOCK_CONFIG {
	/** Action */
	t_u16 action;
	/** Current system clock */
	t_u16 cur_sys_clk;
	/** Clock type */
	t_u16 sys_clk_type;
	/** Length of clocks */
	t_u16 sys_clk_len;
	/** System clocks */
	t_u16 sys_clk[16];
} MLAN_PACK_END HostCmd_DS_ECL_SYSTEM_CLOCK_CONFIG;

/** MrvlIEtypes_WmmParamSet_t */
typedef MLAN_PACK_START struct _MrvlIEtypes_WmmParamSet_t {
	/** Header */
	MrvlIEtypesHeader_t header;
	/** WMM IE */
	t_u8 wmm_ie[1];
} MLAN_PACK_END MrvlIEtypes_WmmParamSet_t;

/** MrvlIEtypes_WmmQueueStatus_t */
typedef MLAN_PACK_START struct {
	/** Header */
	MrvlIEtypesHeader_t header;
	/** Queue index */
	t_u8 queue_index;
	/** Disabled flag */
	t_u8 disabled;
	/** Medium time allocation in 32us units*/
	t_u16 medium_time;
	/** Flow required flag */
	t_u8 flow_required;
	/** Flow created flag */
	t_u8 flow_created;
	/** Reserved */
	t_u32 reserved;
} MLAN_PACK_END MrvlIEtypes_WmmQueueStatus_t;

/** Size of a TSPEC.  Used to allocate necessary buffer space in commands */
#define WMM_TSPEC_SIZE 63

/** Extra IE bytes allocated in messages for appended IEs after a TSPEC */
#define WMM_ADDTS_EXTRA_IE_BYTES 256

/** Extra TLV bytes allocated in messages for configuring WMM Queues */
#define WMM_QUEUE_CONFIG_EXTRA_TLV_BYTES 64

/** Number of bins in the histogram for the HostCmd_DS_WMM_QUEUE_STATS */
#define WMM_STATS_PKTS_HIST_BINS 7

/**
 *  @brief Firmware command structure to retrieve the firmware WMM status.
 *
 *  Used to retrieve the status of each WMM AC Queue in TLV
 *    format (MrvlIEtypes_WmmQueueStatus_t) as well as the current WMM
 *    parameter IE advertised by the AP.
 *
 *  Used in response to a EVENT_WMM_STATUS_CHANGE event signaling
 *    a QOS change on one of the ACs or a change in the WMM Parameter in
 *    the Beacon.
 *
 *  TLV based command, byte arrays used for max sizing purpose. There are no
 *    arguments sent in the command, the TLVs are returned by the firmware.
 */
typedef MLAN_PACK_START struct {
	/** Queue status TLV */
	t_u8 queue_status_tlv[sizeof(MrvlIEtypes_WmmQueueStatus_t) *
			      MAX_AC_QUEUES];
	/** WMM parameter TLV */
	t_u8 wmm_param_tlv[sizeof(IEEEtypes_WmmParameter_t) + 2];
} MLAN_PACK_END HostCmd_DS_WMM_GET_STATUS;

/**
 *  @brief Command structure for the HostCmd_CMD_WMM_ADDTS_REQ firmware command
 */
typedef MLAN_PACK_START struct {
	mlan_cmd_result_e command_result; /**< Command result */
	t_u32 timeout_ms; /**< Timeout value in milliseconds */
	t_u8 dialog_token; /**< Dialog token */
	t_u8 ieee_status_code; /**< IEEE status code */
	t_u8 tspec_data[WMM_TSPEC_SIZE]; /**< TSPEC data */
	t_u8 addts_extra_ie_buf[WMM_ADDTS_EXTRA_IE_BYTES]; /**< Extra IE buffer
							    */
} MLAN_PACK_END HostCmd_DS_WMM_ADDTS_REQ;

/**
 *  @brief Command structure for the HostCmd_CMD_WMM_DELTS_REQ firmware command
 */
typedef MLAN_PACK_START struct {
	mlan_cmd_result_e command_result; /**< Command result */
	t_u8 dialog_token; /**< Dialog token */
	t_u8 ieee_reason_code; /**< IEEE reason code */
	t_u8 tspec_data[WMM_TSPEC_SIZE]; /**< TSPEC data */
} MLAN_PACK_END HostCmd_DS_WMM_DELTS_REQ;

/**
 *  @brief Command structure for the HostCmd_CMD_WMM_QUEUE_CONFIG firmware cmd
 *
 *  Set/Get/Default the Queue parameters for a specific AC in the firmware.
 */
typedef MLAN_PACK_START struct {
	mlan_wmm_queue_config_action_e action; /**< Set, Get, or Default */
	mlan_wmm_ac_e access_category; /**< WMM_AC_BK(0) to WMM_AC_VO(3) */
	/** @brief MSDU lifetime expiry per 802.11e
	 *
	 *   - Ignored if 0 on a set command
	 *   - Set to the 802.11e specified 500 TUs when defaulted
	 */
	t_u16 msdu_lifetime_expiry;
	t_u8 tlv_buffer[WMM_QUEUE_CONFIG_EXTRA_TLV_BYTES]; /**< Not supported */
} MLAN_PACK_END HostCmd_DS_WMM_QUEUE_CONFIG;

/**
 *  @brief Command structure for the HostCmd_CMD_WMM_QUEUE_STATS firmware cmd
 *
 *  Turn statistical collection on/off for a given AC or retrieve the
 *    accumulated stats for an AC and clear them in the firmware.
 */
typedef MLAN_PACK_START struct {
	mlan_wmm_queue_stats_action_e action; /**< Start, Stop, or Get */
#ifdef BIG_ENDIAN_SUPPORT
	t_u8 select_bin:7;   /**< WMM_AC_BK(0) to WMM_AC_VO(3), or TID */
	t_u8 select_is_userpri:1;   /**< Set if select_bin is UP, Clear for AC
				     */
#else
	t_u8 select_is_userpri:1;   /**< Set if select_bin is UP, Clear for AC
				     */
	t_u8 select_bin:7;   /**< WMM_AC_BK(0) to WMM_AC_VO(3), or TID */
#endif
	t_u16 pkt_count; /**< Number of successful packets transmitted */
	t_u16 pkt_loss;	/**< Packets lost; not included in pktCount */
	t_u32 avg_queue_delay; /**< Average Queue delay in microsec */
	t_u32 avg_tx_delay; /**< Average Transmission delay in microsec */
	t_u16 used_time; /**< Calc used time - units of 32 microsec */
	t_u16 policed_time; /**< Calc policed time - units of 32 microsec */
	/** @brief Queue Delay Histogram; number of packets per queue delay
	 * range
	 *
	 *  [0] -  0ms <= delay < 5ms
	 *  [1] -  5ms <= delay < 10ms
	 *  [2] - 10ms <= delay < 20ms
	 *  [3] - 20ms <= delay < 30ms
	 *  [4] - 30ms <= delay < 40ms
	 *  [5] - 40ms <= delay < 50ms
	 *  [6] - 50ms <= delay < msduLifetime (TUs)
	 */
	t_u16 delay_histogram[WMM_STATS_PKTS_HIST_BINS];
	/** Reserved */
	t_u16 reserved_1;
} MLAN_PACK_END HostCmd_DS_WMM_QUEUE_STATS;

/**
 *  @brief Command structure for the HostCmd_CMD_WMM_TS_STATUS firmware cmd
 *
 *  Query the firmware to get the status of the WMM Traffic Streams
 */
typedef MLAN_PACK_START struct {
	/** TSID: Range: 0->7 */
	t_u8 tid;
	/** TSID specified is valid */
	t_u8 valid;
	/** AC TSID is active on */
	t_u8 access_category;
	/** UP specified for the TSID */
	t_u8 user_priority;
	/** Power save mode for TSID: 0 (legacy), 1 (UAPSD) */
	t_u8 psb;
	/** Uplink(1), Downlink(2), Bidirectional(3) */
	t_u8 flow_dir;
	/** Medium time granted for the TSID */
	t_u16 medium_time;
} MLAN_PACK_END HostCmd_DS_WMM_TS_STATUS;

/** Firmware status for a specific AC */
typedef MLAN_PACK_START struct {
	/** Disabled flag */
	t_u8 disabled;
	/** Flow required flag */
	t_u8 flow_required;
	/** Flow created flag */
	t_u8 flow_created;
} MLAN_PACK_END WmmAcStatus_t;

/**  Local Power Capability */
typedef MLAN_PACK_START struct _MrvlIEtypes_PowerCapability_t {
	/** Header */
	MrvlIEtypesHeader_t header;
	/** Minmum power */
	t_s8 min_power;
	/** Maximum power */
	t_s8 max_power;
} MLAN_PACK_END MrvlIEtypes_PowerCapability_t;

/** HT Information element */
typedef MLAN_PACK_START struct _MrvlIETypes_HTInfo_t {
	/** Header */
	MrvlIEtypesHeader_t header;

	/** HTInfo struct */
	HTInfo_t ht_info;
} MLAN_PACK_END MrvlIETypes_HTInfo_t;

/** 20/40 BSS Coexistence element */
typedef MLAN_PACK_START struct _MrvlIETypes_2040BSSCo_t {
	/** Header */
	MrvlIEtypesHeader_t header;

	/** BSSCo2040_t struct */
	BSSCo2040_t bss_co_2040;
} MLAN_PACK_END MrvlIETypes_2040BSSCo_t;

/** Extended Capabilities element */
typedef MLAN_PACK_START struct _MrvlIETypes_ExtCap_t {
	/** Header */
	MrvlIEtypesHeader_t header;

	/** ExtCap_t struct */
	ExtCap_t ext_cap;
} MLAN_PACK_END MrvlIETypes_ExtCap_t;

/** Supported operating classes element */
typedef MLAN_PACK_START struct _MrvlIETypes_SuppOperClass_t {
	/** Header */
	MrvlIEtypesHeader_t header;
	/** Current operationg class **/
	t_u8 current_oper_class;
	/** Operating class list */
	t_u8 oper_class[1];
} MLAN_PACK_END MrvlIETypes_SuppOperClass_t;

/** Oper_class channel bandwidth element */
typedef MLAN_PACK_START struct _MrvlIEtypes_chan_bw_oper_t {
	/** Header */
	MrvlIEtypesHeader_t header;
	/** channel oper bandwidth*/
	mlan_ds_bw_chan_oper ds_chan_bw_oper;
} MLAN_PACK_END MrvlIEtypes_chan_bw_oper_t;

/** Qos Info */
typedef MLAN_PACK_START struct _MrvlIETypes_qosinfo_t {
	/** Header */
	MrvlIEtypesHeader_t header;
	/** qos_info*/
	t_u8 qos_info;
} MLAN_PACK_END MrvlIETypes_qosinfo_t;

/** Overlapping BSS Scan Parameters element */
typedef MLAN_PACK_START struct _MrvlIETypes_OverlapBSSScanParam_t {
	/** Header */
	MrvlIEtypesHeader_t header;

	/** OBSSScanParam_t struct */
	OBSSScanParam_t obss_scan_param;
} MLAN_PACK_END MrvlIETypes_OverlapBSSScanParam_t;

/** Set of MCS values that STA desires to use within the BSS */
typedef MLAN_PACK_START struct _MrvlIETypes_HTOperationalMCSSet_t {
	/** Header */
	MrvlIEtypesHeader_t header;

	/** Bitmap indicating MCSs that STA desires to use within the BSS */
	t_u8 ht_operational_mcs_bitmap[16];
} MLAN_PACK_END MrvlIETypes_HTOperationalMCSSet_t;

/** VHT Operations IE */
typedef MLAN_PACK_START struct _MrvlIETypes_VHTOprat_t {
	/** Header */
	MrvlIEtypesHeader_t header;

	t_u8 chan_width;
	t_u8 chan_center_freq_1;
	t_u8 chan_center_freq_2;
	/** Basic MCS set map, each 2 bits stands for a Nss */
	t_u16 basic_MCS_map;
} MLAN_PACK_END MrvlIETypes_VHTOprat_t;

/** VHT Transmit Power Envelope IE */
typedef MLAN_PACK_START struct _MrvlIETypes_VHTtxpower_t {
	/** Header */
	MrvlIEtypesHeader_t header;

	t_u8 max_tx_power;
	t_u8 chan_center_freq;
	t_u8 chan_width;
} MLAN_PACK_END MrvlIETypes_VHTtxpower_t;

/** Extended Power Constraint IE */
typedef MLAN_PACK_START struct _MrvlIETypes_ExtPwerCons_t {
	/** Header */
	MrvlIEtypesHeader_t header;

	/** channel width */
	t_u8 chan_width;
	/** local power constraint */
	t_u8 local_power_cons;
} MLAN_PACK_END MrvlIETypes_ExtPwerCons_t;

/** Extended BSS Load IE */
typedef MLAN_PACK_START struct _MrvlIETypes_ExtBSSload_t {
	/** Header */
	MrvlIEtypesHeader_t header;

	t_u8 MU_MIMO_capa_count;
	t_u8 stream_underutilization;
	t_u8 VHT40_util;
	t_u8 VHT80_util;
	t_u8 VHT160_util;
} MLAN_PACK_END MrvlIETypes_ExtBSSload_t;

/** Quiet Channel IE */
typedef MLAN_PACK_START struct _MrvlIETypes_QuietChan_t {
	/** Header */
	MrvlIEtypesHeader_t header;

	t_u8 AP_quiet_mode;
	t_u8 quiet_count;
	t_u8 quiet_period;
	t_u16 quiet_dur;
	t_u16 quiet_offset;
} MLAN_PACK_END MrvlIETypes_QuietChan_t;

/** Wide Bandwidth Channel Switch IE */
typedef MLAN_PACK_START struct _MrvlIETypes_BWSwitch_t {
	/** Header */
	MrvlIEtypesHeader_t header;

	t_u8 new_chan_width;
	t_u8 new_chan_center_freq_1;
	t_u8 new_chan_center_freq_2;
} MLAN_PACK_END MrvlIETypes_BWSwitch_t;

/** AID IE */
typedef MLAN_PACK_START struct _MrvlIETypes_AID_t {
	/** Header */
	MrvlIEtypesHeader_t header;

	/** AID number */
	t_u16 AID;
} MLAN_PACK_END MrvlIETypes_AID_t;

/** Operating Mode Notification IE */
typedef MLAN_PACK_START struct _MrvlIETypes_OperModeNtf_t {
	/** Header */
	MrvlIEtypesHeader_t header;

	/** operating mdoe */
	t_u8 oper_mode;
} MLAN_PACK_END MrvlIETypes_OperModeNtf_t;

/** bf global args */
typedef struct MLAN_PACK_START _bf_global_cfg_args {
	/** Global enable/disable bf */
	t_u8 bf_enbl;
	/** Global enable/disable sounding */
	t_u8 sounding_enbl;
	/** FB Type */
	t_u8 fb_type;
	/** SNR Threshold */
	t_u8 snr_threshold;
	/** Sounding interval */
	t_u16 sounding_interval;
	/** BF mode */
	t_u8 bf_mode;
	/** Reserved */
	t_u8 reserved;
} MLAN_PACK_END bf_global_cfg_args;

/** bf_trigger_sound_args_t */
typedef MLAN_PACK_START struct _bf_trigger_sound_args_t {
	/** Peer MAC address */
	t_u8 peer_mac[MLAN_MAC_ADDR_LENGTH];
	/** Status */
	t_u8 status;
} MLAN_PACK_END bf_trigger_sound_args_t;

/** bf periodicity args */
typedef MLAN_PACK_START struct _bf_periodicity_args {
	/** Peer MAC address */
	t_u8 peer_mac[MLAN_MAC_ADDR_LENGTH];
	/** Current Tx BF Interval */
	t_u16 interval;
	/** Status */
	t_u8 status;
} MLAN_PACK_END bf_periodicity_args;

/** bf peer configuration args */
typedef struct MLAN_PACK_START _bf_peer_args {
	/** Peer MAC address */
	t_u8 peer_mac[MLAN_MAC_ADDR_LENGTH];
	/** Reserved */
	t_u16 reserved;
	/** Enable/Disable Beamforming */
	t_u8 bf_enbl;
	/** Enable/Disable sounding */
	t_u8 sounding_enbl;
	/** FB Type */
	t_u8 fb_type;
} MLAN_PACK_END bf_peer_args;

/** bf_snr_thr_t */
typedef MLAN_PACK_START struct _bf_snr_thr_t {
	/** Peer MAC address */
	t_u8 peer_mac[MLAN_MAC_ADDR_LENGTH];
	/** SNR */
	t_u8 snr;
} MLAN_PACK_END bf_snr_thr_t;

/** HostCmd_DS_TX_BF_CFG */
typedef MLAN_PACK_START struct _HostCmd_DS_TX_BF_CFG {
	/* Beamforming action */
	t_u16 bf_action;
	/* action - SET/GET */
	t_u16 action;

	MLAN_PACK_START union {
		bf_global_cfg_args bf_global_cfg;
		bf_trigger_sound_args_t bf_sound_args;
		bf_periodicity_args bf_periodicity;
		bf_peer_args tx_bf_peer;
		bf_snr_thr_t bf_snr;
	} MLAN_PACK_END body;
} MLAN_PACK_END HostCmd_DS_TX_BF_CFG;

#ifdef WIFI_DIRECT_SUPPORT
/** MrvlIEtypes_psk_t */
typedef MLAN_PACK_START struct _MrvlIEtypes_psk_t {
	/** Header */
	MrvlIEtypesHeader_t header;
	/** PSK */
	t_u8 psk[MLAN_MAX_KEY_LENGTH];
} MLAN_PACK_END MrvlIEtypes_psk_t;
#endif /* WIFI_DIRECT_SUPPORT */

/** Data structure for Link ID */
typedef MLAN_PACK_START struct _MrvlIETypes_LinkIDElement_t {
	/** Header */
	MrvlIEtypesHeader_t header;
	/** Bssid */
	t_u8 bssid[MLAN_MAC_ADDR_LENGTH];
	/** initial sta address*/
	t_u8 init_sta[MLAN_MAC_ADDR_LENGTH];
	/** respose sta address */
	t_u8 resp_sta[MLAN_MAC_ADDR_LENGTH];
} MLAN_PACK_END MrvlIETypes_LinkIDElement_t;

/** MrvlIEtypes_PMK_t */
typedef MLAN_PACK_START struct _MrvlIEtypes_PMK_t {
	/** Header */
	MrvlIEtypesHeader_t header;
	/** PMK */
	t_u8 pmk[1];
} MLAN_PACK_END MrvlIEtypes_PMK_t;

/** MrvlIEtypes_Passphrase_t */
typedef MLAN_PACK_START struct _MrvlIEtypes_Passphrase_t {
	/** Header */
	MrvlIEtypesHeader_t header;
	/** Passphrase */
	char passphrase[1];
} MLAN_PACK_END MrvlIEtypes_Passphrase_t;

/** MrvlIEtypes_SAE_Password_t */
typedef MLAN_PACK_START struct _MrvlIEtypes_SAE_Password_t {
	/** Header */
	MrvlIEtypesHeader_t header;
	/** SAE Password */
	char sae_password[1];
} MLAN_PACK_END MrvlIEtypes_SAE_Password_t;

/** MrvlIEtypes_SAE_PWE_Mode_t */
typedef MLAN_PACK_START struct _MrvlIEtypes_SAE_PWE_Mode_t {
	/** Header */
	MrvlIEtypesHeader_t header;
	/** WPA3 SAE mechanism for PWE derivation */
	char pwe[1];
} MLAN_PACK_END MrvlIEtypes_SAE_PWE_Mode_t;

/** SAE H2E capability bit in RSNX */
#define SAE_H2E_BIT 5

/* rsnMode -
 *      Bit 0    : No RSN
 *      Bit 1-2  : RFU
 *      Bit 3    : WPA
 *      Bit 4    : WPA-NONE
 *      Bit 5    : WPA2
 *      Bit 6    : AES CCKM
 *      Bit 7-15 : RFU
 */
/** MrvlIEtypes_EncrProto_t */
typedef MLAN_PACK_START struct _MrvlIEtypes_EncrProto_t {
	/** Header */
	MrvlIEtypesHeader_t header;
	/** EncrProto */
	t_u16 rsn_mode;
} MLAN_PACK_END MrvlIEtypes_EncrProto_t;

/** MrvlIEtypes_Bssid_t */
typedef MLAN_PACK_START struct _MrvlIEtypes_Bssid_t {
	/** Header */
	MrvlIEtypesHeader_t header;
	/** Bssid */
	t_u8 bssid[MLAN_MAC_ADDR_LENGTH];
} MLAN_PACK_END MrvlIEtypes_Bssid_t;

/*
 * This struct will handle GET,SET,CLEAR function for embedded
 * supplicant.
 * Define data structure for HostCmd_CMD_802_11_SUPPLICANT_PMK
 */
/** HostCmd_DS_802_11_SUPPLICANT_PMK */
typedef MLAN_PACK_START struct _HostCmd_DS_802_11_SUPPLICANT_PMK {
	/** CMD Action GET/SET/CLEAR */
	t_u16 action;
	/** CacheResult initialized to 0 */
	t_u16 cache_result;
	/** TLV Buffer */
	t_u8 tlv_buffer[1];
	/** MrvlIEtypes_SsidParamSet_t  SsidParamSet;
	 * MrvlIEtypes_PMK_t           Pmk;
	 * MrvlIEtypes_Passphrase_t    Passphrase;
	 * MrvlIEtypes_Bssid_t         Bssid;
	 **/
} MLAN_PACK_END HostCmd_DS_802_11_SUPPLICANT_PMK;

/*
 * This struct will GET the Supplicant supported bitmaps
 * The GET_CURRENT action will get the network profile used
 * for the current assocation.
 * Define data structure for HostCmd_CMD_802_11_SUPPLICANT_PROFILE
 */
typedef MLAN_PACK_START struct _HostCmd_DS_802_11_SUPPLICANT_PROFILE {
	/** GET/SET/GET_CURRENT */
	t_u16 action;
	/** Reserved */
	t_u16 reserved;
	/** TLVBuffer */
	t_u8 tlv_buf[1];
	/* MrvlIEtypes_EncrProto_t */
} MLAN_PACK_END HostCmd_DS_802_11_SUPPLICANT_PROFILE;

/* unicastCipher -
 *      Bit 0   : RFU
 *      Bit 1   : RFU
 *      Bit 2   : TKIP
 *      Bit 3   : AES CCKM
 *      Bit 2-7 : RFU
 * multicastCipher -
 *      Bit 0   : WEP40
 *      Bit 1   : WEP104
 *      Bit 2   : TKIP
 *      Bit 3   : AES
 *      Bit 4-7 : Reserved for now
 */
/** MrvlIEtypes_Cipher_t */
typedef MLAN_PACK_START struct _MrvlIEtypes_Cipher_t {
	/** Header */
	MrvlIEtypesHeader_t header;
	/** PairCipher */
	t_u8 pair_cipher;
	/** GroupCipher */
	t_u8 group_cipher;
} MLAN_PACK_END MrvlIEtypes_Cipher_t;

/** RFType */
typedef MLAN_PACK_START struct _RFType_t {
	/** band info */
	Band_Config_t bandcfg;
	/** reserved */
	t_u8 reserved;
} MLAN_PACK_END RFType_t;

/** HostCmd_CMD_802_11_RF_CHANNEL */
typedef MLAN_PACK_START struct _HostCmd_DS_802_11_RF_CHANNEL {
	/** Action */
	t_u16 action;
	/** Current channel */
	t_u16 current_channel;
	/** RF type */
	RFType_t rf_type;
	/** Reserved field */
	t_u16 reserved;
#ifdef STA_SUPPORT
	/** Reserved */
	t_u8 reserved_1[32];
#else				/* STA_SUPPORT */
	/** List of channels */
	t_u8 channel_list[32];
#endif				/* !STA_SUPPORT */
} MLAN_PACK_END HostCmd_DS_802_11_RF_CHANNEL;

/** HostCmd_DS_VERSION_EXT */
typedef MLAN_PACK_START struct _HostCmd_DS_VERSION_EXT {
	/** Selected version string */
	t_u8 version_str_sel;
	/** Version string */
	char version_str[128];
} MLAN_PACK_END HostCmd_DS_VERSION_EXT;

#define TLV_TYPE_CHAN_ATTR_CFG (PROPRIETARY_TLV_BASE_ID + 237)
#define TLV_TYPE_REGION_INFO (PROPRIETARY_TLV_BASE_ID + 238)
#define TLV_TYPE_POWER_TABLE (PROPRIETARY_TLV_BASE_ID + 262)
#define TLV_TYPE_POWER_TABLE_ATTR (PROPRIETARY_TLV_BASE_ID + 317)
/** HostCmd_DS_CHAN_REGION_CFG */
typedef MLAN_PACK_START struct _HostCmd_DS_CHAN_REGION_CFG {
	/** Action */
	t_u16 action;
} MLAN_PACK_END HostCmd_DS_CHAN_REGION_CFG;

/** HostCmd_CMD_CW_MODE_CTRL */
typedef MLAN_PACK_START struct _HostCmd_DS_CW_MODE_CTRL {
	/** Action for CW Tone Control */
	t_u16 action;
	/** Mode of Operation 0: Disbale 1: Tx Continuous Packet 2: Tx
	 * Continuous Wave */
	t_u8 mode;
	/** channel */
	t_u8 channel;
	/** channel info*/
	t_u8 chanInfo;
	/** Tx Power level in dBm */
	t_u16 txPower;
	/** Packet Length */
	t_u16 pktLength;
	/** bit rate Info */
	t_u32 rateInfo;
} MLAN_PACK_END HostCmd_DS_CW_MODE_CTRL;

/** HostCmd_CMD_802_11_RF_ANTENNA */
typedef MLAN_PACK_START struct _HostCmd_DS_802_11_RF_ANTENNA {
	/** Action for Tx antenna */
	t_u16 action_tx;
	/** Tx antenna mode Bit0:1, Bit1:2, Bit0-1:1+2, 0xffff: diversity */
	t_u16 tx_antenna_mode;
	/** Action for Rx antenna */
	t_u16 action_rx;
	/** Rx antenna mode Bit0:1, Bit1:2, Bit0-1:1+2, 0xffff: diversity */
	t_u16 rx_antenna_mode;
} MLAN_PACK_END HostCmd_DS_802_11_RF_ANTENNA;

/** HostCmd_DS_802_11_IBSS_STATUS */
typedef MLAN_PACK_START struct _HostCmd_DS_802_11_IBSS_STATUS {
	/** Action */
	t_u16 action;
	/** Enable */
	t_u16 enable;
	/** BSSID */
	t_u8 bssid[MLAN_MAC_ADDR_LENGTH];
	/** Beacon interval */
	t_u16 beacon_interval;
	/** ATIM window interval */
	t_u16 atim_window;
	/** User G rate protection */
	t_u16 use_g_rate_protect;
} MLAN_PACK_END HostCmd_DS_802_11_IBSS_STATUS;

/** HostCmd_DS_MGMT_IE_LIST_CFG */
typedef MLAN_PACK_START struct _HostCmd_DS_MGMT_IE_LIST {
	/** Action */
	t_u16 action;
	/** Get/Set mgmt IE */
	mlan_ds_misc_custom_ie ds_mgmt_ie;
} MLAN_PACK_END HostCmd_DS_MGMT_IE_LIST_CFG;

/** HostCmd_DS_TDLS_CONFIG */
typedef MLAN_PACK_START struct _HostCmd_DS_TDLS_CONFIG {
	/** Set TDLS configuration */
	mlan_ds_misc_tdls_config tdls_info;
} MLAN_PACK_END HostCmd_DS_TDLS_CONFIG;

/**Action ID for TDLS delete link*/
#define TDLS_DELETE 0x00
/**Action ID for TDLS create link*/
#define TDLS_CREATE 0x01
/**Action ID for TDLS config link*/
#define TDLS_CONFIG 0x02
/** HostCmd_DS_TDLS_OPER */
typedef MLAN_PACK_START struct _HostCmd_DS_TDLS_OPER {
	/** Action */
	t_u16 tdls_action;
	/**reason*/
	t_u16 reason;
	/** peer mac */
	t_u8 peer_mac[MLAN_MAC_ADDR_LENGTH];
} MLAN_PACK_END HostCmd_DS_TDLS_OPER;

/** HostCmd_CMD_MAC_REG_ACCESS */
typedef MLAN_PACK_START struct _HostCmd_DS_MAC_REG_ACCESS {
	/** Action */
	t_u16 action;
	/** MAC register offset */
	t_u16 offset;
	/** MAC register value */
	t_u32 value;
} MLAN_PACK_END HostCmd_DS_MAC_REG_ACCESS;

/** HostCmd_CMD_BCA_REG_ACCESS */
typedef MLAN_PACK_START struct _HostCmd_DS_BCA_REG_ACCESS {
	/** Action */
	t_u16 action;
	/** BCA register offset */
	t_u16 offset;
	/** BCA register value */
	t_u32 value;
} MLAN_PACK_END HostCmd_DS_BCA_REG_ACCESS;

/** HostCmd_CMD_BBP_REG_ACCESS */
typedef MLAN_PACK_START struct _HostCmd_DS_BBP_REG_ACCESS {
	/** Acion */
	t_u16 action;
	/** BBP register offset */
	t_u16 offset;
	/** BBP register value */
	t_u8 value;
	/** Reserved field */
	t_u8 reserved[3];
} MLAN_PACK_END HostCmd_DS_BBP_REG_ACCESS;

/**  HostCmd_CMD_RF_REG_ACCESS */
typedef MLAN_PACK_START struct _HostCmd_DS_RF_REG_ACCESS {
	/** Action */
	t_u16 action;
	/** RF register offset */
	t_u16 offset;
	/** RF register value */
	t_u8 value;
	/** Reserved field */
	t_u8 reserved[3];
} MLAN_PACK_END HostCmd_DS_RF_REG_ACCESS;

/** HostCmd_DS_802_11_EEPROM_ACCESS */
typedef MLAN_PACK_START struct _HostCmd_DS_802_11_EEPROM_ACCESS {
	/** Action */
	t_u16 action;

	/** multiple 4 */
	t_u16 offset;
	/** Number of bytes */
	t_u16 byte_count;
	/** Value */
	t_u8 value;
} MLAN_PACK_END HostCmd_DS_802_11_EEPROM_ACCESS;

/** HostCmd_DS_MEM_ACCESS */
typedef MLAN_PACK_START struct _HostCmd_DS_MEM_ACCESS {
	/** Action */
	t_u16 action;
	/** Reserved field */
	t_u16 reserved;
	/** Address */
	t_u32 addr;
	/** Value */
	t_u32 value;
} MLAN_PACK_END HostCmd_DS_MEM_ACCESS;

/** HostCmd_DS_TARGET_ACCESS */
typedef MLAN_PACK_START struct _HostCmd_DS_TARGET_ACCESS {
	/** Action */
	t_u16 action;
	/** CSU Target Device. 1: CSU, 2: PSU */
	t_u16 csu_target;
	/** Target Device Address */
	t_u16 address;
	/** Data */
	t_u8 data;
} MLAN_PACK_END HostCmd_DS_TARGET_ACCESS;

/** HostCmd_DS_SUBSCRIBE_EVENT */
typedef MLAN_PACK_START struct _HostCmd_DS_SUBSCRIBE_EVENT {
	/** Action */
	t_u16 action;
	/** Bitmap of subscribed events */
	t_u16 event_bitmap;
} MLAN_PACK_END HostCmd_DS_SUBSCRIBE_EVENT;

/** HostCmd_DS_OTP_USER_DATA */
typedef MLAN_PACK_START struct _HostCmd_DS_OTP_USER_DATA {
	/** Action */
	t_u16 action;
	/** Reserved field */
	t_u16 reserved;
	/** User data length */
	t_u16 user_data_length;
	/** User data */
	t_u8 user_data[1];
} MLAN_PACK_END HostCmd_DS_OTP_USER_DATA;

/** HostCmd_CMD_HS_WAKEUP_REASON */
typedef MLAN_PACK_START struct _HostCmd_DS_HS_WAKEUP_REASON {
	/** wakeupReason:
	 * 0: unknown
	 * 1: Broadcast data matched
	 * 2: Multicast data matched
	 * 3: Unicast data matched
	 * 4: Maskable event matched
	 * 5. Non-maskable event matched
	 * 6: Non-maskable condition matched (EAPoL rekey)
	 * 7: Magic pattern matched
	 * Others: reserved. (set to 0) */
	t_u16 wakeup_reason;
} MLAN_PACK_END HostCmd_DS_HS_WAKEUP_REASON;

/** MrvlIEtypes_HsWakeHoldoff_t */
typedef MLAN_PACK_START struct _MrvlIEtypes_HsWakeHoldoff_t {
	/** Header */
	MrvlIEtypesHeader_t header;
	/** Minimum delay between HsActive and HostWake (in msec) */
	t_u16 min_wake_holdoff;
} MLAN_PACK_END MrvlIEtypes_HsWakeHoldoff_t;

/** MrvlIEtypes_PsParamsInHs_t */
typedef MLAN_PACK_START struct _MrvlIEtypes_PsParamsInHs_t {
	/** Header */
	MrvlIEtypesHeader_t header;
	/** Host sleep wake interval(in msec) */
	t_u32 hs_wake_interval;
	/** Host sleep inactivity timeout (in msec) */
	t_u32 hs_inactivity_timeout;
} MLAN_PACK_END MrvlIEtypes_PsParamsInHs_t;

/** MrvlIEtypes_WakeupSourceGPIO_t */
typedef MLAN_PACK_START struct _MrvlIEtypes_WakeupSourceGPIO_t {
	/** Header */
	MrvlIEtypesHeader_t header;
	/** GPIO for indication of wakeup source */
	t_u8 ind_gpio;
	/** Level on ind_gpio for normal wakeup source */
	t_u8 level;
} MLAN_PACK_END MrvlIEtypes_WakeupSourceGPIO_t;

/** MrvlIEtypes_RobustcoexSourceGPIO_t */
typedef MLAN_PACK_START struct _MrvlIEtypes_RobustcoexSourceGPIO_t {
	/** Header */
	MrvlIEtypesHeader_t header;
	/** GPIO cfg for external bt request */
	t_u8 enable;
	/** GPIO number */
	t_u8 gpio_num;
	/** GPIO Polarity */
	t_u8 gpio_polarity;
} MLAN_PACK_END MrvlIEtypes_RobustcoexSourceGPIO_t;

#define MAX_NUM_MAC 2

typedef MLAN_PACK_START struct _dmcs_chan_status {
	/** Channel number */
	t_u8 channel;
	/** Number of AP on this channel */
	t_u8 ap_count;
	/** Number of STA on this channel*/
	t_u8 sta_count;
} MLAN_PACK_END dmcs_chan_status;

typedef MLAN_PACK_START struct _dmcs_status_data {
	/** radio ID */
	t_u8 radio_id;
	/** Running mode
	 ** 0 - Idle
	 ** 1 - DBC
	 ** 2 - DRCS
	 */
	t_u8 running_mode;
	/** Channel status of this radio */
	dmcs_chan_status chan_status[2];
} MLAN_PACK_END dmcs_status_data;

/** MrvlIEtypes_DmcsConfig_t */
typedef MLAN_PACK_START struct _MrvlIEtypes_DmcsConfig_t {
	/** Header */
	MrvlIEtypesHeader_t header;
	/** Mapping policy */
	t_u8 mapping_policy;
	/** Radio status of DMCS */
	dmcs_status_data radio_status[MAX_NUM_MAC];
} MLAN_PACK_END MrvlIEtypes_DmcsStatus_t;

#define ANTMODE_FW_DECISION 0xff
/** MrvlIEtypes_HS_Antmode_t */
typedef MLAN_PACK_START struct _MrvlIEtypes_HS_Antmode_t {
	/** Header */
	MrvlIEtypesHeader_t header;
	/** Tx Path antenna mode*/
	t_u8 txpath_antmode;
	/** Rx Path antenna mode */
	t_u8 rxpath_antmode;
} MLAN_PACK_END MrvlIEtypes_HS_Antmode_t;

typedef MLAN_PACK_START struct _MrvlIEtypes_WakeupExtend_t {
	/** Header */
	MrvlIEtypesHeader_t header;
	/** Events that will be forced ignore **/
	t_u32 event_force_ignore;
	/** Events that will use extend gap to inform host*/
	t_u32 event_use_ext_gap;
	/** Extend gap*/
	t_u8 ext_gap;
	/** GPIO wave level*/
	t_u8 gpio_wave;
} MLAN_PACK_END MrvlIEtypes_WakeupExtend_t;

#define EVENT_MANAGEMENT_FRAME_WAKEUP 136
typedef MLAN_PACK_START struct _mgmt_frame_filter {
	/** action - bitmap
	 ** On matching rx'd pkt and filter during NON_HOSTSLEEP mode:
	 **   Action[1]=0  Discard
	 **   Action[1]=1  Allow
	 ** Note that default action on non-match is "Allow".
	 **
	 ** On matching rx'd pkt and filter during HOSTSLEEP mode:
	 **   Action[1:0]=00  Discard and Not Wake host
	 **   Action[1:0]=01  Discard and Wake host
	 **   Action[1:0]=10  Invalid
	 ** Note that default action on non-match is "Discard and Not Wake
	 *host".
	 **/
	t_u8 action;
	/** Frame type(p2p...)
	 ** type=0: invalid
	 ** type=1: p2p
	 ** type=0xff: management frames(assoc req/rsp, probe req/rsp,...)
	 ** type=others: reserved
	 **/
	t_u8 type;
	/** Frame mask according to each type
	 ** When type=1 for p2p, frame-mask have following define:
	 **    Bit      Frame
	 **     0       GO Negotiation Request
	 **     1       GO Negotiation Response
	 **     2       GO Negotiation Confirmation
	 **     3       P2P Invitation Request
	 **     4       P2P Invitation Response
	 **     5       Device Discoverability Request
	 **     6       Device Discoverability Response
	 **     7       Provision Discovery Request
	 **     8       Provision Discovery Response
	 **     9       Notice of Absence
	 **     10      P2P Presence Request
	 **     11      P2P Presence Response
	 **     12      GO Discoverability Request
	 **     13-31   Reserved
	 **
	 ** When type=others, frame-mask is reserved.
	 **/
	t_u32 frame_mask;
} MLAN_PACK_END mgmt_frame_filter;

#define MAX_MGMT_FRAME_FILTER 2
/** MrvlIEtypes_MgmtFrameFilter_t */
typedef MLAN_PACK_START struct _MrvlIEtypes_MgmtFrameFilter_t {
	/** Header */
	MrvlIEtypesHeader_t header;
	/** management frame filters */
	mgmt_frame_filter filter[MAX_MGMT_FRAME_FILTER];
} MLAN_PACK_END MrvlIEtypes_MgmtFrameFilter_t;

/** HostCmd_DS_INACTIVITY_TIMEOUT_EXT */
typedef MLAN_PACK_START struct _HostCmd_DS_INACTIVITY_TIMEOUT_EXT {
	/** ACT_GET/ACT_SET */
	t_u16 action;
	/** uS, 0 means 1000uS(1ms) */
	t_u16 timeout_unit;
	/** Inactivity timeout for unicast data */
	t_u16 unicast_timeout;
	/** Inactivity timeout for multicast data */
	t_u16 mcast_timeout;
	/** Timeout for additional RX traffic after Null PM1 packet exchange */
	t_u16 ps_entry_timeout;
	/** Reserved to further expansion */
	t_u16 reserved;
} MLAN_PACK_END HostCmd_DS_INACTIVITY_TIMEOUT_EXT;

/** HostCmd_DS_INDEPENDENT_RESET_CFG */
typedef MLAN_PACK_START struct _HostCmd_DS_INDEPENDENT_RESET_CFG {
	/** ACT_GET/ACT_SET */
	t_u16 action;
	/** out band independent reset */
	t_u8 ir_mode;
	/** gpio pin */
	t_u8 gpio_pin;
} MLAN_PACK_END HostCmd_DS_INDEPENDENT_RESET_CFG;

/** HostCmd_DS_802_11_PS_INACTIVITY_TIMEOUT */
typedef MLAN_PACK_START struct _HostCmd_DS_802_11_PS_INACTIVITY_TIMEOUT {
	/** ACT_GET/ACT_SET */
	t_u16 action;
	/** ps inactivity timeout value */
	t_u16 inact_tmo;
} MLAN_PACK_END HostCmd_DS_802_11_PS_INACTIVITY_TIMEOUT;

/** TLV type : STA Mac address */
#define TLV_TYPE_STA_MAC_ADDRESS (PROPRIETARY_TLV_BASE_ID + 0x20)	/* 0x0120 */

#define TLV_TYPE_RANDOM_MAC (PROPRIETARY_TLV_BASE_ID + 0xEC)	/*0x01EC */

/** MrvlIEtypes_MacAddr_t */
typedef MLAN_PACK_START struct _MrvlIEtypes_MacAddr_t {
	/** Header */
	MrvlIEtypesHeader_t header;
	/** mac address */
	t_u8 mac[MLAN_MAC_ADDR_LENGTH];
} MLAN_PACK_END MrvlIEtypes_MacAddr_t;

/** Assoc Request */
#define SUBTYPE_ASSOC_REQUEST 0
/** ReAssoc Request */
#define SUBTYPE_REASSOC_REQUEST 2
/** Probe Resp */
#define SUBTYPE_PROBE_RESP 5
/** Disassoc Request */
#define SUBTYPE_DISASSOC 10
/** Auth Request */
#define SUBTYPE_AUTH 11
/** Deauth Request */
#define SUBTYPE_DEAUTH 12
/** Action frame */
#define SUBTYPE_ACTION 13
/** beacon */
#define SUBTYPE_BEACON 8

#ifdef UAP_SUPPORT
/** TLV type : AP Channel band Config */
#define TLV_TYPE_UAP_CHAN_BAND_CONFIG                                          \
	(PROPRIETARY_TLV_BASE_ID + 0x2a)	/* 0x012a */
/** TLV type : AP Mac address */
#define TLV_TYPE_UAP_MAC_ADDRESS (PROPRIETARY_TLV_BASE_ID + 0x2b)	/* 0x012b */
/** TLV type : AP Beacon period */
#define TLV_TYPE_UAP_BEACON_PERIOD (PROPRIETARY_TLV_BASE_ID + 0x2c)	/* 0x012c  \
									 */
/** TLV type : AP DTIM period */
#define TLV_TYPE_UAP_DTIM_PERIOD (PROPRIETARY_TLV_BASE_ID + 0x2d)	/* 0x012d */
/** TLV type : AP Tx power */
#define TLV_TYPE_UAP_TX_POWER (PROPRIETARY_TLV_BASE_ID + 0x2f)	/* 0x012f */
/** TLV type : AP SSID broadcast control */
#define TLV_TYPE_UAP_BCAST_SSID_CTL                                            \
	(PROPRIETARY_TLV_BASE_ID + 0x30)	/* 0x0130 */
/** TLV type : AP Preamble control */
#define TLV_TYPE_UAP_PREAMBLE_CTL (PROPRIETARY_TLV_BASE_ID + 0x31)	/* 0x0131   \
									 */
/** TLV type : AP Antenna control */
#define TLV_TYPE_UAP_ANTENNA_CTL (PROPRIETARY_TLV_BASE_ID + 0x32)	/* 0x0132 */
/** TLV type : AP RTS threshold */
#define TLV_TYPE_UAP_RTS_THRESHOLD (PROPRIETARY_TLV_BASE_ID + 0x33)	/* 0x0133  \
									 */
/** TLV type : AP Tx data rate */
#define TLV_TYPE_UAP_TX_DATA_RATE (PROPRIETARY_TLV_BASE_ID + 0x35)	/* 0x0135   \
									 */
/** TLV type: AP Packet forwarding control */
#define TLV_TYPE_UAP_PKT_FWD_CTL (PROPRIETARY_TLV_BASE_ID + 0x36)	/* 0x0136 */
/** TLV type: STA information */
#define TLV_TYPE_UAP_STA_INFO (PROPRIETARY_TLV_BASE_ID + 0x37)	/* 0x0137 */
/** TLV type: AP STA MAC address filter */
#define TLV_TYPE_UAP_STA_MAC_ADDR_FILTER                                       \
	(PROPRIETARY_TLV_BASE_ID + 0x38)	/* 0x0138 */
/** TLV type: AP STA ageout timer */
#define TLV_TYPE_UAP_STA_AGEOUT_TIMER                                          \
	(PROPRIETARY_TLV_BASE_ID + 0x39)	/* 0x0139 */
/** TLV type: AP WEP keys */
#define TLV_TYPE_UAP_WEP_KEY (PROPRIETARY_TLV_BASE_ID + 0x3b)	/* 0x013b */
/** TLV type: AP WPA passphrase */
#define TLV_TYPE_UAP_WPA_PASSPHRASE                                            \
	(PROPRIETARY_TLV_BASE_ID + 0x3c)	/* 0x013c */
/** TLV type: AP protocol */
#define TLV_TYPE_UAP_ENCRYPT_PROTOCOL                                          \
	(PROPRIETARY_TLV_BASE_ID + 0x40)	/* 0x0140 */
/** TLV type: AP AKMP */
#define TLV_TYPE_UAP_AKMP (PROPRIETARY_TLV_BASE_ID + 0x41)	/* 0x0141 */
/** TLV type: AP Fragment threshold */
#define TLV_TYPE_UAP_FRAG_THRESHOLD                                            \
	(PROPRIETARY_TLV_BASE_ID + 0x46)	/* 0x0146 */
/** TLV type: AP Group rekey timer */
#define TLV_TYPE_UAP_GRP_REKEY_TIME                                            \
	(PROPRIETARY_TLV_BASE_ID + 0x47)	/* 0x0147 */
/**TLV type : AP Max Station number */
#define TLV_TYPE_UAP_MAX_STA_CNT (PROPRIETARY_TLV_BASE_ID + 0x55)	/* 0x0155 */
/**TLV type : AP Max Station number per chip */
#define TLV_TYPE_UAP_MAX_STA_CNT_PER_CHIP                                      \
	(PROPRIETARY_TLV_BASE_ID + 0x140)	/* 0x0240 */
/**TLV type : AP Retry limit */
#define TLV_TYPE_UAP_RETRY_LIMIT (PROPRIETARY_TLV_BASE_ID + 0x5d)	/* 0x015d */
/** TLV type : AP MCBC data rate */
#define TLV_TYPE_UAP_MCBC_DATA_RATE                                            \
	(PROPRIETARY_TLV_BASE_ID + 0x62)	/* 0x0162 */
/**TLV type: AP RSN replay protection */
#define TLV_TYPE_UAP_RSN_REPLAY_PROTECT                                        \
	(PROPRIETARY_TLV_BASE_ID + 0x64)	/* 0x0164 */
/**TLV type: AP mgmt IE passthru mask */
#define TLV_TYPE_UAP_MGMT_IE_PASSTHRU_MASK                                     \
	(PROPRIETARY_TLV_BASE_ID + 0x70)	/* 0x0170 */

/**TLV type: AP pairwise handshake timeout */
#define TLV_TYPE_UAP_EAPOL_PWK_HSK_TIMEOUT                                     \
	(PROPRIETARY_TLV_BASE_ID + 0x75)	/* 0x0175 */
/**TLV type: AP pairwise handshake retries */
#define TLV_TYPE_UAP_EAPOL_PWK_HSK_RETRIES                                     \
	(PROPRIETARY_TLV_BASE_ID + 0x76)	/* 0x0176 */
/**TLV type: AP groupwise handshake timeout */
#define TLV_TYPE_UAP_EAPOL_GWK_HSK_TIMEOUT                                     \
	(PROPRIETARY_TLV_BASE_ID + 0x77)	/* 0x0177 */
/**TLV type: AP groupwise handshake retries */
#define TLV_TYPE_UAP_EAPOL_GWK_HSK_RETRIES                                     \
	(PROPRIETARY_TLV_BASE_ID + 0x78)	/* 0x0178 */
/** TLV type: AP PS STA ageout timer */
#define TLV_TYPE_UAP_PS_STA_AGEOUT_TIMER                                       \
	(PROPRIETARY_TLV_BASE_ID + 0x7b)	/* 0x017b */
/** TLV type : Pairwise Cipher */
#define TLV_TYPE_PWK_CIPHER (PROPRIETARY_TLV_BASE_ID + 0x91)	/* 0x0191 */
/** TLV type : Group Cipher */
#define TLV_TYPE_GWK_CIPHER (PROPRIETARY_TLV_BASE_ID + 0x92)	/* 0x0192 */
/** TLV type : BSS Status */
#define TLV_TYPE_BSS_STATUS (PROPRIETARY_TLV_BASE_ID + 0x93)	/* 0x0193 */
/** TLV type :  AP WMM params */
#define TLV_TYPE_AP_WMM_PARAM (PROPRIETARY_TLV_BASE_ID + 0xd0)	/* 0x01d0 */
/** TLV type : AP Tx beacon rate */
#define TLV_TYPE_UAP_TX_BEACON_RATE (PROPRIETARY_TLV_BASE_ID + 288)	/* 0x0220  \
									 */

/** MrvlIEtypes_beacon_period_t */
typedef MLAN_PACK_START struct _MrvlIEtypes_beacon_period_t {
	/** Header */
	MrvlIEtypesHeader_t header;
	/** beacon period */
	t_u16 beacon_period;
} MLAN_PACK_END MrvlIEtypes_beacon_period_t;

/** MrvlIEtypes_dtim_period_t */
typedef MLAN_PACK_START struct _MrvlIEtypes_dtim_period_t {
	/** Header */
	MrvlIEtypesHeader_t header;
	/** DTIM period */
	t_u8 dtim_period;
} MLAN_PACK_END MrvlIEtypes_dtim_period_t;

/** MrvlIEtypes_tx_rate_t */
typedef MLAN_PACK_START struct _MrvlIEtypes_tx_rate_t {
	/** Header */
	MrvlIEtypesHeader_t header;
	/** tx data rate */
	t_u16 tx_data_rate;
} MLAN_PACK_END MrvlIEtypes_tx_rate_t;

/** MrvlIEtypes_mcbc_rate_t */
typedef MLAN_PACK_START struct _MrvlIEtypes_mcbc_rate_t {
	/** Header */
	MrvlIEtypesHeader_t header;
	/** mcbc data rate */
	t_u16 mcbc_data_rate;
} MLAN_PACK_END MrvlIEtypes_mcbc_rate_t;

/** MrvlIEtypes_tx_power_t */
typedef MLAN_PACK_START struct _MrvlIEtypes_tx_power_t {
	/** Header */
	MrvlIEtypesHeader_t header;
	/** tx power */
	t_u8 tx_power;
} MLAN_PACK_END MrvlIEtypes_tx_power_t;

/** MrvlIEtypes_bcast_ssid_t */
typedef MLAN_PACK_START struct _MrvlIEtypes_bcast_ssid_t {
	/** Header */
	MrvlIEtypesHeader_t header;
	/** bcast ssid control*/
	t_u8 bcast_ssid_ctl;
} MLAN_PACK_END MrvlIEtypes_bcast_ssid_t;

/** MrvlIEtypes_antenna_mode_t */
typedef MLAN_PACK_START struct _MrvlIEtypes_antenna_mode_t {
	/** Header */
	MrvlIEtypesHeader_t header;
	/** which antenna */
	t_u8 which_antenna;
	/** antenna mode*/
	t_u8 antenna_mode;
} MLAN_PACK_END MrvlIEtypes_antenna_mode_t;

/** MrvlIEtypes_pkt_forward_t */
typedef MLAN_PACK_START struct _MrvlIEtypes_pkt_forward_t {
	/** Header */
	MrvlIEtypesHeader_t header;
	/** pkt foward control */
	t_u8 pkt_forward_ctl;
} MLAN_PACK_END MrvlIEtypes_pkt_forward_t;

/** MrvlIEtypes_max_sta_count_t */
typedef MLAN_PACK_START struct _MrvlIEtypes_max_sta_count_t {
	/** Header */
	MrvlIEtypesHeader_t header;
	/** max station count */
	t_u16 max_sta_count;
} MLAN_PACK_END MrvlIEtypes_max_sta_count_t;

/** MrvlIEtypes_uap_max_sta_cnt */
typedef MLAN_PACK_START struct _MrvlIEtypes_uap_max_sta_cnt_t {
	/** Header */
	MrvlIEtypesHeader_t header;
	/** max station count */
	t_u16 uap_max_sta;
} MLAN_PACK_END MrvlIEtypes_uap_max_sta_cnt_t;

#define MRVL_ACTION_CHAN_SWITCH_ANNOUNCE        (PROPRIETARY_TLV_BASE_ID + 0x341)

/** MrvlIEtypes_uap_chan_switch */
typedef MLAN_PACK_START struct _MrvlIEtypes_action_chan_switch_t {
	/** Header */
	MrvlIEtypesHeader_t header;
	/* 0 send broadcast CSA action frame, 1 send unicast CSA action frame */
	t_u32 mode;
    /**ie buf*/
	t_u8 ie_buf[];
} MLAN_PACK_END MrvlIEtypes_action_chan_switch_t;

/** MrvlIEtypes_sta_ageout_t */
typedef MLAN_PACK_START struct _MrvlIEtypes_sta_ageout_t {
	/** Header */
	MrvlIEtypesHeader_t header;
	/** station age out timer */
	t_u32 sta_ageout_timer;
} MLAN_PACK_END MrvlIEtypes_sta_ageout_t;

/** MrvlIEtypes_rts_threshold_t */
typedef MLAN_PACK_START struct _MrvlIEtypes_rts_threshold_t {
	/** Header */
	MrvlIEtypesHeader_t header;
	/** rts threshold */
	t_u16 rts_threshold;
} MLAN_PACK_END MrvlIEtypes_rts_threshold_t;

/** MrvlIEtypes_frag_threshold_t */
typedef MLAN_PACK_START struct _MrvlIEtypes_frag_threshold_t {
	/** Header */
	MrvlIEtypesHeader_t header;
	/** frag threshold */
	t_u16 frag_threshold;
} MLAN_PACK_END MrvlIEtypes_frag_threshold_t;

/** MrvlIEtypes_retry_limit_t */
typedef MLAN_PACK_START struct _MrvlIEtypes_retry_limit_t {
	/** Header */
	MrvlIEtypesHeader_t header;
	/** retry limit */
	t_u8 retry_limit;
} MLAN_PACK_END MrvlIEtypes_retry_limit_t;

/** MrvlIEtypes_eapol_pwk_hsk_timeout_t */
typedef MLAN_PACK_START struct _MrvlIEtypes_eapol_pwk_hsk_timeout_t {
	/** Header */
	MrvlIEtypesHeader_t header;
	/** pairwise update timeout in milliseconds */
	t_u32 pairwise_update_timeout;
} MLAN_PACK_END MrvlIEtypes_eapol_pwk_hsk_timeout_t;

/** MrvlIEtypes_eapol_pwk_hsk_retries_t */
typedef MLAN_PACK_START struct _MrvlIEtypes_eapol_pwk_hsk_retries_t {
	/** Header */
	MrvlIEtypesHeader_t header;
	/** pairwise handshake retries */
	t_u32 pwk_retries;
} MLAN_PACK_END MrvlIEtypes_eapol_pwk_hsk_retries_t;

/** MrvlIEtypes_eapol_gwk_hsk_timeout_t */
typedef MLAN_PACK_START struct _MrvlIEtypes_eapol_gwk_hsk_timeout_t {
	/** Header */
	MrvlIEtypesHeader_t header;
	/** groupwise update timeout in milliseconds */
	t_u32 groupwise_update_timeout;
} MLAN_PACK_END MrvlIEtypes_eapol_gwk_hsk_timeout_t;

/** MrvlIEtypes_eapol_gwk_hsk_retries_t */
typedef MLAN_PACK_START struct _MrvlIEtypes_eapol_gwk_hsk_retries_t {
	/** Header */
	MrvlIEtypesHeader_t header;
	/** groupwise handshake retries */
	t_u32 gwk_retries;
} MLAN_PACK_END MrvlIEtypes_eapol_gwk_hsk_retries_t;

/** MrvlIEtypes_mgmt_ie_passthru_t */
typedef MLAN_PACK_START struct _MrvlIEtypes_mgmt_ie_passthru_t {
	/** Header */
	MrvlIEtypesHeader_t header;
	/** mgmt IE mask value */
	t_u32 mgmt_ie_mask;
} MLAN_PACK_END MrvlIEtypes_mgmt_ie_passthru_t;

/** MrvlIEtypes_mac_filter_t */
typedef MLAN_PACK_START struct _MrvlIEtypes_mac_filter_t {
	/** Header */
	MrvlIEtypesHeader_t header;
	/** Filter mode */
	t_u8 filter_mode;
	/** Number of STA MACs */
	t_u8 count;
	/** STA MAC addresses buffer */
	t_u8 mac_address[1];
} MLAN_PACK_END MrvlIEtypes_mac_filter_t;

/** MrvlIEtypes_auth_type_t */
typedef MLAN_PACK_START struct _MrvlIEtypes_auth_type_t {
	/** Header */
	MrvlIEtypesHeader_t header;
	/** Authentication type */
	t_u8 auth_type;
} MLAN_PACK_END MrvlIEtypes_auth_type_t;

/** MrvlIEtypes_encrypt_protocol_t */
typedef MLAN_PACK_START struct _MrvlIEtypes_encrypt_protocol_t {
	/** Header */
	MrvlIEtypesHeader_t header;
	/** encryption protocol */
	t_u16 protocol;
} MLAN_PACK_END MrvlIEtypes_encrypt_protocol_t;

/** MrvlIEtypes_pwk_cipher_t */
typedef MLAN_PACK_START struct _MrvlIEtypes_pwk_cipher_t {
	/** Header */
	MrvlIEtypesHeader_t header;
	/** protocol */
	t_u16 protocol;
	/** pairwise cipher */
	t_u8 pairwise_cipher;
	/** reserved */
	t_u8 reserved;
} MLAN_PACK_END MrvlIEtypes_pwk_cipher_t;

/** MrvlIEtypes_gwk_cipher_t */
typedef MLAN_PACK_START struct _MrvlIEtypes_gwk_cipher_t {
	/** Header */
	MrvlIEtypesHeader_t header;
	/** group cipher */
	t_u8 group_cipher;
	/** reserved */
	t_u8 reserved;
} MLAN_PACK_END MrvlIEtypes_gwk_cipher_t;

/** MrvlIEtypes_akmp_t */
typedef MLAN_PACK_START struct _MrvlIEtypes_akmp_t {
	/** Header */
	MrvlIEtypesHeader_t header;
	/** key management */
	t_u16 key_mgmt;
	/** key management operation */
	t_u16 key_mgmt_operation;
} MLAN_PACK_END MrvlIEtypes_akmp_t;

/** MrvlIEtypes_passphrase_t */
typedef MLAN_PACK_START struct _MrvlIEtypes_passphrase_t {
	/** Header */
	MrvlIEtypesHeader_t header;
	/** passphrase */
	t_u8 passphrase[1];
} MLAN_PACK_END MrvlIEtypes_passphrase_t;

/** MrvlIEtypes_rsn_replay_prot_t */
typedef MLAN_PACK_START struct _MrvlIEtypes_rsn_replay_prot_t {
	/** Header */
	MrvlIEtypesHeader_t header;
	/** rsn replay proection */
	t_u8 rsn_replay_prot;
} MLAN_PACK_END MrvlIEtypes_rsn_replay_prot_t;

/** MrvlIEtypes_group_rekey_time_t */
typedef MLAN_PACK_START struct _MrvlIEtypes_group_rekey_time_t {
	/** Header */
	MrvlIEtypesHeader_t header;
	/** group key rekey time */
	t_u32 gk_rekey_time;
} MLAN_PACK_END MrvlIEtypes_group_rekey_time_t;

/** MrvlIEtypes_wep_key_t */
typedef MLAN_PACK_START struct _MrvlIEtypes_wep_key_t {
	/** Header */
	MrvlIEtypesHeader_t header;
	/** key index */
	t_u8 key_index;
	/** is default */
	t_u8 is_default;
	/** key data */
	t_u8 key[1];
} MLAN_PACK_END MrvlIEtypes_wep_key_t;

/** MrvlIEtypes_bss_status_t */
typedef MLAN_PACK_START struct _MrvlIEtypes_bss_status_t {
	/** Header */
	MrvlIEtypesHeader_t header;
	/** BSS status, READ only */
	t_u16 bss_status;
} MLAN_PACK_END MrvlIEtypes_bss_status_t;

/** MrvlIEtypes_preamble_t */
typedef MLAN_PACK_START struct _MrvlIEtypes_preamble_t {
	/** Header */
	MrvlIEtypesHeader_t header;
	/** preamble type, READ only */
	t_u8 preamble_type;
} MLAN_PACK_END MrvlIEtypes_preamble_t;

/** MrvlIEtypes_wmm_parameter_t */
typedef MLAN_PACK_START struct _MrvlIEtypes_wmm_parameter_t {
	/** Header */
	MrvlIEtypesHeader_t header;
	/** WMM parameter */
	WmmParameter_t wmm_para;
} MLAN_PACK_END MrvlIEtypes_wmm_parameter_t;

/** MrvlIEtypes_wacp_mode_t */
typedef MLAN_PACK_START struct _MrvlIEtypes_wacp_mode_t {
    /** Header */
	MrvlIEtypesHeader_t header;
    /** wacp_mode */
	t_u8 wacp_mode;
} MLAN_PACK_END MrvlIEtypes_wacp_mode_t;

/** SNMP_MIB_UAP_INDEX */
typedef enum _SNMP_MIB_UAP_INDEX {
	tkip_mic_failures = 0x0b,
	ccmp_decrypt_errors = 0x0c,
	wep_undecryptable_count = 0x0d,
	wep_icv_error_count = 0x0e,
	decrypt_failure_count = 0xf,
	dot11_failed_count = 0x12,
	dot11_retry_count = 0x13,
	dot11_multi_retry_count = 0x14,
	dot11_frame_dup_count = 0x15,
	dot11_rts_success_count = 0x16,
	dot11_rts_failure_count = 0x17,
	dot11_ack_failure_count = 0x18,
	dot11_rx_fragment_count = 0x19,
	dot11_mcast_rx_frame_count = 0x1a,
	dot11_fcs_error_count = 0x1b,
	dot11_tx_frame_count = 0x1c,
	dot11_rsna_tkip_cm_invoked = 0x1d,
	dot11_rsna_4way_hshk_failures = 0x1e,
	dot11_mcast_tx_count = 0x1f,
} SNMP_MIB_UAP_INDEX;

/** MrvlIEtypes_snmp_oid_t */
typedef MLAN_PACK_START struct _MrvlIEtypes_snmp_oid_t {
	/** Header */
	MrvlIEtypesHeader_t header;
	/** data */
	t_u32 data;
} MLAN_PACK_END MrvlIEtypes_snmp_oid_t;

/** HostCmd_SYS_CONFIG */
typedef MLAN_PACK_START struct _HostCmd_DS_SYS_CONFIG {
	/** CMD Action GET/SET*/
	t_u16 action;
	/** Tlv buffer */
	t_u8 tlv_buffer[1];
} MLAN_PACK_END HostCmd_DS_SYS_CONFIG;

/** HostCmd_SYS_CONFIG */
typedef MLAN_PACK_START struct _HostCmd_DS_SYS_INFO {
	/** sys info */
	t_u8 sys_info[64];
} MLAN_PACK_END HostCmd_DS_SYS_INFO;

/** HostCmd_DS_STA_DEAUTH */
typedef MLAN_PACK_START struct _HostCmd_DS_STA_DEAUTH {
	/** mac address */
	t_u8 mac[MLAN_MAC_ADDR_LENGTH];
	/** reason code */
	t_u16 reason;
} MLAN_PACK_END HostCmd_DS_STA_DEAUTH;

/** HostCmd_DS_REPORT_MIC */
typedef MLAN_PACK_START struct _HostCmd_DS_REPORT_MIC {
	/** mac address */
	t_u8 mac[MLAN_MAC_ADDR_LENGTH];
} MLAN_PACK_END HostCmd_DS_REPORT_MIC;

/** HostCmd_UAP_OPER_CTRL */
typedef MLAN_PACK_START struct _HostCmd_DS_UAP_OPER_CTRL {
	/** CMD Action GET/SET*/
	t_u16 action;
	/** control*/
	t_u16 ctrl;
	/**channel operation*/
	t_u16 chan_opt;
	/**channel band tlv*/
	MrvlIEtypes_channel_band_t channel_band;
} MLAN_PACK_END HostCmd_DS_UAP_OPER_CTRL;

/** Host Command id: POWER_MGMT  */
#define HOST_CMD_POWER_MGMT_EXT 0x00ef
/** TLV type: AP Sleep param */
#define TLV_TYPE_AP_SLEEP_PARAM (PROPRIETARY_TLV_BASE_ID + 0x6a)	/* 0x016a */
/** TLV type: AP Inactivity Sleep param */
#define TLV_TYPE_AP_INACT_SLEEP_PARAM                                          \
	(PROPRIETARY_TLV_BASE_ID + 0x6b)	/* 0x016b */

/** MrvlIEtypes_sleep_param_t */
typedef MLAN_PACK_START struct _MrvlIEtypes_sleep_param_t {
	/** Header */
	MrvlIEtypesHeader_t header;
	/** control bitmap */
	t_u32 ctrl_bitmap;
	/** min_sleep */
	t_u32 min_sleep;
	/** max_sleep */
	t_u32 max_sleep;
} MLAN_PACK_END MrvlIEtypes_sleep_param_t;

/** MrvlIEtypes_inact_sleep_param_t */
typedef MLAN_PACK_START struct _MrvlIEtypes_inact_sleep_param_t {
	/** Header */
	MrvlIEtypesHeader_t header;
	/** inactivity timeout */
	t_u32 inactivity_to;

	/** min_awake */
	t_u32 min_awake;
	/** max_awake */
	t_u32 max_awake;
} MLAN_PACK_END MrvlIEtypes_inact_sleep_param_t;

/** HostCmd_DS_POWER_MGMT */
typedef MLAN_PACK_START struct _HostCmd_DS_POWER_MGMT_EXT {
	/** CMD Action Get/Set*/
	t_u16 action;
	/** power mode */
	t_u16 power_mode;
} MLAN_PACK_END HostCmd_DS_POWER_MGMT_EXT;

/** MrvlIEtypes_ps_sta_ageout_t */
typedef MLAN_PACK_START struct _MrvlIEtypes_ps_sta_ageout_t {
	/** Header */
	MrvlIEtypesHeader_t header;
	/** station age out timer */
	t_u32 ps_sta_ageout_timer;
} MLAN_PACK_END MrvlIEtypes_ps_sta_ageout_t;

/** MrvlIEtypes_sta_info_t */
typedef MLAN_PACK_START struct _MrvlIEtypes_sta_info_t {
	/** Header */
	MrvlIEtypesHeader_t header;
	/** STA MAC address */
	t_u8 mac_address[MLAN_MAC_ADDR_LENGTH];
	/** Power Mgmt status */
	t_u8 power_mgmt_status;
	/** RSSI */
	t_s8 rssi;
	/** ie_buf */
	t_u8 ie_buf[];
} MLAN_PACK_END MrvlIEtypes_sta_info_t;

/** HostCmd_DS_STA_LIST */
typedef MLAN_PACK_START struct _HostCmd_DS_STA_LIST {
	/** Number of STAs */
	t_u16 sta_count;
	/* MrvlIEtypes_sta_info_t sta_info[]; */
	t_u8 tlv_buf[];
} MLAN_PACK_END HostCmd_DS_STA_LIST;

/** TLV ID : WAPI Information */
#define TLV_TYPE_AP_WAPI_INFO (PROPRIETARY_TLV_BASE_ID + 0x67)	/* 0x0167 */

/** MrvlIEtypes_sta_info_t */
typedef MLAN_PACK_START struct _MrvlIEtypes_wapi_info_t {
	/** Header */
	MrvlIEtypesHeader_t header;
	/** Multicast PN */
	t_u8 multicast_PN[16];
} MLAN_PACK_END MrvlIEtypes_wapi_info_t;
#endif /* UAP_SUPPORT */

/** HostCmd_DS_TX_RX_HISTOGRAM */
typedef MLAN_PACK_START struct _HostCmd_DS_TX_RX_HISTOGRAM {
	/**  Enable or disable  */
	t_u8 enable;
	/** Choose to get TX, RX or both */
	t_u16 action;
} MLAN_PACK_END HostCmd_DS_TX_RX_HISTOGRAM;

/** TLV buffer : 2040 coex config */
typedef MLAN_PACK_START struct _MrvlIEtypes_2040_coex_enable_t {
	/** Header */
	MrvlIEtypesHeader_t header;
	/** Enable */
	t_u8 enable_2040coex;
} MLAN_PACK_END MrvlIEtypes_2040_coex_enable_t;

/**BT coexit scan time setting*/
typedef MLAN_PACK_START struct _MrvlIEtypes_BtCoexScanTime_t {
	/** Header */
	MrvlIEtypesHeader_t header;
	/**coex scan state  0: disable 1: enable*/
	t_u8 coex_scan;
	/**reserved*/
	t_u8 reserved;
	/**min scan time*/
	t_u16 min_scan_time;
	/**max scan time*/
	t_u16 max_scan_time;
} MLAN_PACK_END MrvlIEtypes_BtCoexScanTime_t;

/**BT coexit aggr win size */
typedef MLAN_PACK_START struct _MrvlIETypes_BtCoexAggrWinSize_t {
	/** Header */
	MrvlIEtypesHeader_t header;
	/**winsize  0: restore default winsize, 1: use below winsize */
	t_u8 coex_win_size;
	/**tx win size*/
	t_u8 tx_win_size;
	/**rx win size*/
	t_u8 rx_win_size;
	/**reserved*/
	t_u8 reserved;
} MLAN_PACK_END MrvlIETypes_BtCoexAggrWinSize_t;

/** MrvlIEtypes_eapol_pkt_t */
typedef MLAN_PACK_START struct _MrvlIEtypes_eapol_pkt_t {
	/** Header */
	MrvlIEtypesHeader_t header;
	/** eapol pkt buf */
	t_u8 pkt_buf[];
} MLAN_PACK_END MrvlIEtypes_eapol_pkt_t;

/** HostCmd_DS_EAPOL_PKT */
typedef MLAN_PACK_START struct _HostCmd_DS_EAPOL_PKT {
	/** Action */
	t_u16 action;
	/** TLV buffer */
	MrvlIEtypes_eapol_pkt_t tlv_eapol;
} MLAN_PACK_END HostCmd_DS_EAPOL_PKT;

/** HostCmd_DS_OXYGEN_MIMO_SWITCH */
typedef MLAN_PACK_START struct _HostCmd_DS_MIMO_SWITCH {
	/** Tx path antanne mode */
	t_u8 txpath_antmode;
	/** Rx path antanne mode */
	t_u8 rxpath_antmode;
} MLAN_PACK_END HostCmd_DS_MIMO_SWITCH;

#ifdef RX_PACKET_COALESCE
typedef MLAN_PACK_START struct _HostCmd_DS_RX_PKT_COAL_CFG {
	/** Action */
	t_u16 action;
	/** Packet threshold */
	t_u32 packet_threshold;
	/** Timeout */
	t_u16 delay;
} MLAN_PACK_END HostCmd_DS_RX_PKT_COAL_CFG;
#endif

/** HostCmd_DS_DYN_BW */
typedef MLAN_PACK_START struct _HostCmd_DS_DYN_BW {
	/** Action */
	t_u16 action;
	/** Dynamic bandwidth */
	t_u16 dyn_bw;
} MLAN_PACK_END HostCmd_DS_DYN_BW;

/** Host Command ID : Packet aggregation CTRL */
#define HostCmd_CMD_PACKET_AGGR_CTRL 0x0251

/** HostCmd_DS_PACKET_AGGR_CTRL */
typedef MLAN_PACK_START struct _HostCmd_DS_PACKET_AGGR_AGGR_CTRL {
	/** ACT_GET/ACT_SET */
	t_u16 action;
	/** enable aggregation, BIT(0) TX, BIT(1)RX */
	t_u16 aggr_enable;
	/** Tx aggregation alignment */
	t_u16 tx_aggr_max_size;
	/** Tx aggregation max packet number */
	t_u16 tx_aggr_max_num;
	/** Tx aggregation alignment */
	t_u16 tx_aggr_align;
} MLAN_PACK_END HostCmd_DS_PACKET_AGGR_CTRL;

#ifdef USB
/** Host Command ID : Packet aggregation over host interface */
#define HostCmd_CMD_PACKET_AGGR_OVER_HOST_INTERFACE 0x0117

/** TLV ID : USB Aggregation parameters */
#define MRVL_USB_AGGR_PARAM_TLV_ID (PROPRIETARY_TLV_BASE_ID + 0xB1)	/* 0x1B1   \
									 */

/** TLV size : USB Aggregation parameters, except header */
#define MRVL_USB_AGGR_PARAM_TLV_LEN (14)

/** VHT Operations IE */
typedef MLAN_PACK_START struct _MrvlIETypes_USBAggrParam_t {
	/** Header */
	MrvlIEtypesHeader_t header;

	/** Enable */
	t_u16 enable;
	/** Rx aggregation mode */
	t_u16 rx_aggr_mode;
	/** Rx aggregation alignment */
	t_u16 rx_aggr_align;
	/** Rx aggregation max packet/size */
	t_u16 rx_aggr_max;
	/** Rx aggrgation timeout, in microseconds */
	t_u16 rx_aggr_tmo;
	/** Tx aggregation mode */
	t_u16 tx_aggr_mode;
	/** Tx aggregation alignment */
	t_u16 tx_aggr_align;
} MLAN_PACK_END MrvlIETypes_USBAggrParam_t;

/** HostCmd_DS_PACKET_AGGR_OVER_HOST_INTERFACE */
typedef MLAN_PACK_START struct _HostCmd_DS_PACKET_AGGR_OVER_HOST_INTERFACE {
	/** ACT_GET/ACT_SET */
	t_u16 action;
	/**
	 *  Host interface aggregation control TLV(s) to be sent in the firmware
	 * command
	 *
	 *  TLV_USB_AGGR_PARAM, MrvlIETypes_USBAggrParam_t
	 */
	t_u8 tlv_buf[1];
} MLAN_PACK_END HostCmd_DS_PACKET_AGGR_OVER_HOST_INTERFACE;
#endif /* USB */

/** HostCmd_CONFIG_LOW_PWR_MODE */
typedef MLAN_PACK_START struct _HostCmd_CONFIG_LOW_PWR_MODE {
	/** Enable LPM */
	t_u8 enable;
} MLAN_PACK_END HostCmd_CONFIG_LOW_PWR_MODE;

/** HostCmd_CMD_GET_TSF */
typedef MLAN_PACK_START struct _HostCmd_DS_TSF {
	/** tsf value*/
	t_u64 tsf;
} MLAN_PACK_END HostCmd_DS_TSF;
/* WLAN_GET_TSF*/

typedef struct _HostCmd_DS_DFS_REPEATER_MODE {
	/** Set or Get */
	t_u16 action;
	/** 1 on or 0 off */
	t_u16 mode;
} HostCmd_DS_DFS_REPEATER_MODE;

/** HostCmd_DS_BOOT_SLEEP */
typedef MLAN_PACK_START struct _HostCmd_DS_BOOT_SLEEP {
	/** Set or Get */
	t_u16 action;
	/** 1 on or 0 off */
	t_u16 enable;
} MLAN_PACK_END HostCmd_DS_BOOT_SLEEP;

/**
 * @brief 802.11h Local Power Constraint NXP extended TLV
 */
typedef MLAN_PACK_START struct {
	MrvlIEtypesHeader_t header; /**< NXP TLV header: ID/Len */
	t_u8 chan; /**< Channel local constraint applies to */

	/** Power constraint included in beacons
	 *  and used by fw to offset 11d info
	 */
	t_u8 constraint;

} MLAN_PACK_END MrvlIEtypes_LocalPowerConstraint_t;

/*
 *
 * Data structures for driver/firmware command processing
 *
 */

/**  TPC Info structure sent in CMD_802_11_TPC_INFO command to firmware */
typedef MLAN_PACK_START struct {
	/**< Local constraint */
	MrvlIEtypes_LocalPowerConstraint_t local_constraint;
	/**< Power Capability */
	MrvlIEtypes_PowerCapability_t power_cap;

} MLAN_PACK_END HostCmd_DS_802_11_TPC_INFO;

/**  TPC Request structure sent in CMD_802_11_TPC_ADAPT_REQ
 *  command to firmware
 */
typedef MLAN_PACK_START struct {
	t_u8 dest_mac[MLAN_MAC_ADDR_LENGTH]; /**< Destination STA address  */
	t_u16 timeout; /**< Response timeout in ms */
	t_u8 rate_index; /**< IEEE Rate index to send request */

} MLAN_PACK_END HostCmd_TpcRequest;

/**  TPC Response structure received from the
 *   CMD_802_11_TPC_ADAPT_REQ command
 */
typedef MLAN_PACK_START struct {
	t_u8 tpc_ret_code; /**< Firmware command result status code */
	t_s8 tx_power; /**< Reported TX Power from the TPC Report element */
	t_s8 link_margin; /**< Reported link margin from the TPC Report element
			   */
	t_s8 rssi; /**< RSSI of the received TPC Report frame */

} MLAN_PACK_END HostCmd_TpcResponse;

/**  CMD_802_11_TPC_ADAPT_REQ substruct.
 *   Union of the TPC request and response
 */
typedef MLAN_PACK_START union {
	HostCmd_TpcRequest req;	/**< Request struct sent to firmware */
	HostCmd_TpcResponse resp; /**< Response struct received from firmware */

} MLAN_PACK_END HostCmd_DS_802_11_TPC_ADAPT_REQ;

/**  CMD_802_11_CHAN_SW_ANN firmware command substructure */
typedef MLAN_PACK_START struct {
	t_u8 switch_mode; /**< Set to 1 for a quiet switch request, no STA tx */
	t_u8 new_chan; /**< Requested new channel */
	t_u8 switch_count; /**< Number of TBTTs until the switch is to occur */
} MLAN_PACK_END HostCmd_DS_802_11_CHAN_SW_ANN;

/**
 * @brief Enumeration of measurement types, including max supported
 *        enum for 11h/11k
 */
typedef MLAN_PACK_START enum _MeasType_t {
	WLAN_MEAS_BASIC = 0, /**< 11h: Basic */
	WLAN_MEAS_NUM_TYPES, /**< Number of enumerated measurements */
	WLAN_MEAS_11H_MAX_TYPE = WLAN_MEAS_BASIC, /**< Max 11h measurement */

} MLAN_PACK_END MeasType_t;

/**
 * @brief Mode octet of the measurement request element (7.3.2.21)
 */
typedef MLAN_PACK_START struct {
#ifdef BIG_ENDIAN_SUPPORT
	/**< Reserved */
	t_u8 rsvd5_7:3;
	/**< 11k: duration spec. for meas. is mandatory */
	t_u8 duration_mandatory:1;
	/**< 11h: en/disable report rcpt. of spec. type */
	t_u8 report:1;
	/**< 11h: en/disable requests of specified type */
	t_u8 request:1;
	/**< 11h: enable report/request bits */
	t_u8 enable:1;
	/**< 11k: series or parallel with previous meas */
	t_u8 parallel:1;
#else
	/**< 11k: series or parallel with previous meas */
	t_u8 parallel:1;
	/**< 11h: enable report/request bits */
	t_u8 enable:1;
	/**< 11h: en/disable requests of specified type */
	t_u8 request:1;
	/**< 11h: en/disable report rcpt. of spec. type */
	t_u8 report:1;
	/**< 11k: duration spec. for meas. is mandatory */
	t_u8 duration_mandatory:1;
	/**< Reserved */
	t_u8 rsvd5_7:3;
#endif				/* BIG_ENDIAN_SUPPORT */

} MLAN_PACK_END MeasReqMode_t;

/**
 * @brief Common measurement request structure (7.3.2.21.1 to 7.3.2.21.3)
 */
typedef MLAN_PACK_START struct {
	t_u8 channel; /**< Channel to measure */
	t_u64 start_time; /**< TSF Start time of measurement (0 for immediate)
			   */
	t_u16 duration;	/**< TU duration of the measurement */

} MLAN_PACK_END MeasReqCommonFormat_t;

/**
 * @brief Basic measurement request structure (7.3.2.21.1)
 */
typedef MeasReqCommonFormat_t MeasReqBasic_t;

/**
 * @brief CCA measurement request structure (7.3.2.21.2)
 */
typedef MeasReqCommonFormat_t MeasReqCCA_t;

/**
 * @brief RPI measurement request structure (7.3.2.21.3)
 */
typedef MeasReqCommonFormat_t MeasReqRPI_t;

/**
 * @brief Union of the availble measurement request types.  Passed in the
 *        driver/firmware interface.
 */
typedef union {
	MeasReqBasic_t basic; /**< Basic measurement request */
	MeasReqCCA_t cca; /**< CCA measurement request */
	MeasReqRPI_t rpi; /**< RPI measurement request */

} MeasRequest_t;

/**
 * @brief Mode octet of the measurement report element (7.3.2.22)
 */
typedef MLAN_PACK_START struct {
#ifdef BIG_ENDIAN_SUPPORT
	t_u8 rsvd3_7:5;	  /**< Reserved */
	t_u8 refused:1;	  /**< Measurement refused */
	t_u8 incapable:1;   /**< Incapable of performing measurement */
	t_u8 late:1;   /**< Start TSF time missed for measurement */
#else
	t_u8 late:1;   /**< Start TSF time missed for measurement */
	t_u8 incapable:1;   /**< Incapable of performing measurement */
	t_u8 refused:1;	  /**< Measurement refused */
	t_u8 rsvd3_7:5;	  /**< Reserved */
#endif				/* BIG_ENDIAN_SUPPORT */

} MLAN_PACK_END MeasRptMode_t;

/**
 * @brief Basic measurement report (7.3.2.22.1)
 */
typedef MLAN_PACK_START struct {
	t_u8 channel; /**< Channel to measured */
	t_u64 start_time; /**< Start time (TSF) of measurement */
	t_u16 duration;	/**< Duration of measurement in TUs */
	MeasRptBasicMap_t map; /**< Basic measurement report */

} MLAN_PACK_END MeasRptBasic_t;

/**
 * @brief CCA measurement report (7.3.2.22.2)
 */
typedef MLAN_PACK_START struct {
	t_u8 channel; /**< Channel to measured */
	t_u64 start_time; /**< Start time (TSF) of measurement */
	t_u16 duration;	/**< Duration of measurement in TUs  */
	t_u8 busy_fraction; /**< Fractional duration CCA indicated chan busy */

} MLAN_PACK_END MeasRptCCA_t;

/**
 * @brief RPI measurement report (7.3.2.22.3)
 */
typedef MLAN_PACK_START struct {
	t_u8 channel; /**< Channel to measured  */
	t_u64 start_time; /**< Start time (TSF) of measurement */
	t_u16 duration;	/**< Duration of measurement in TUs  */
	t_u8 density[8]; /**< RPI Density histogram report */

} MLAN_PACK_END MeasRptRPI_t;

/**
 * @brief Union of the availble measurement report types.  Passed in the
 *        driver/firmware interface.
 */
typedef union {
	MeasRptBasic_t basic; /**< Basic measurement report */
	MeasRptCCA_t cca; /**< CCA measurement report */
	MeasRptRPI_t rpi; /**< RPI measurement report */

} MeasReport_t;

/**
 * @brief Structure passed to firmware to perform a measurement
 */
typedef MLAN_PACK_START struct {
	t_u8 mac_addr[MLAN_MAC_ADDR_LENGTH]; /**< Reporting STA address */
	t_u8 dialog_token; /**< Measurement dialog toke */
	MeasReqMode_t req_mode;	/**< Report mode  */
	MeasType_t meas_type; /**< Measurement type */
	MeasRequest_t req; /**< Measurement request data */

} MLAN_PACK_END HostCmd_DS_MEASUREMENT_REQUEST,
	*pHostCmd_DS_MEASUREMENT_REQUEST;

/**
 * @brief Structure passed back from firmware with a measurement report,
 *        also can be to send a measurement report to another STA
 */
typedef MLAN_PACK_START struct {
	t_u8 mac_addr[MLAN_MAC_ADDR_LENGTH]; /**< Reporting STA address */
	t_u8 dialog_token; /**< Measurement dialog token */
	MeasRptMode_t rpt_mode;	/**< Report mode */
	MeasType_t meas_type; /**< Measurement type */
	MeasReport_t rpt; /**< Measurement report data */

} MLAN_PACK_END HostCmd_DS_MEASUREMENT_REPORT, *pHostCmd_DS_MEASUREMENT_REPORT;

typedef MLAN_PACK_START struct {
	t_u16 startFreq;
	Band_Config_t bandcfg;
	t_u8 chanNum;

} MLAN_PACK_END MrvlChannelDesc_t;

#ifdef OPCHAN
typedef MLAN_PACK_START struct {
	MrvlIEtypesHeader_t header; /**< Header */

	MrvlChannelDesc_t chanDesc;

	t_u16 controlFlags;
	t_u16 reserved;

	t_u8 actPower;
	t_u8 mdMinPower;
	t_u8 mdMaxPower;
	t_u8 mdPower;

} MLAN_PACK_END MrvlIEtypes_ChanControlDesc_t;

typedef MLAN_PACK_START struct {
	MrvlIEtypesHeader_t header; /**< Header */

	t_u16 chanGroupBitmap;
	ChanScanMode_t scanMode;
	t_u8 numChan;

	MrvlChannelDesc_t chanDesc[50];

} MLAN_PACK_END MrvlIEtypes_ChanGroupControl_t;

typedef MLAN_PACK_START struct {
	t_u16 action; /**< CMD Action Get/Set*/

	t_u8 tlv_buffer[1];

} MLAN_PACK_END HostCmd_DS_OPCHAN_CONFIG;

typedef MLAN_PACK_START struct {
	t_u16 action; /**< CMD Action Get/Set*/

	t_u8 tlv_buffer[1];

} MLAN_PACK_END HostCmd_DS_OPCHAN_CHANGROUP_CONFIG;

#define HostCmd_CMD_OPCHAN_CONFIG 0x00f8
#define HostCmd_CMD_OPCHAN_CHANGROUP_CONFIG 0x00f9
#endif

typedef MLAN_PACK_START struct {
	MrvlIEtypesHeader_t Header; /**< Header */

	MeasRptBasicMap_t map; /**< IEEE 802.11h basic meas report */
} MLAN_PACK_END MrvlIEtypes_ChanRpt11hBasic_t;

/* MrvlIEtypes_DfsW53Cfg_t*/
typedef MLAN_PACK_START struct {
	/* header */
	MrvlIEtypesHeader_t Header;
	/** df53cfg vlue*/
	t_u8 dfs53cfg;
} MLAN_PACK_END MrvlIEtypes_DfsW53Cfg_t;

typedef MLAN_PACK_START struct {
	MrvlChannelDesc_t chan_desc; /**< Channel band, number */
	t_u32 millisec_dwell_time; /**< Channel dwell time in milliseconds */
} MLAN_PACK_END HostCmd_DS_CHAN_RPT_REQ;

typedef MLAN_PACK_START struct {
	t_u32 cmd_result; /**< Rpt request command result (0 == SUCCESS) */
	t_u64 start_tsf; /**< TSF Measurement started */
	t_u32 duration;	/**< Duration of measurement in microsecs */
	t_u8 tlv_buffer[1]; /**< TLV Buffer */
} MLAN_PACK_END HostCmd_DS_CHAN_RPT_RSP;

/** statistics threshold */
typedef MLAN_PACK_START struct {
	/** Header */
	MrvlIEtypesHeader_t header;
	/** value */
	t_u8 value;
	/** reporting frequency */
	t_u8 frequency;
} MLAN_PACK_END MrvlIEtypes_BeaconHighRssiThreshold_t,
	MrvlIEtypes_BeaconLowRssiThreshold_t,
	MrvlIEtypes_BeaconHighSnrThreshold_t,
	MrvlIEtypes_BeaconLowSnrThreshold_t, MrvlIEtypes_FailureCount_t,
	MrvlIEtypes_DataLowRssiThreshold_t, MrvlIEtypes_DataHighRssiThreshold_t,
	MrvlIEtypes_DataLowSnrThreshold_t, MrvlIEtypes_DataHighSnrThreshold_t,
	MrvlIETypes_PreBeaconMissed_t, MrvlIEtypes_BeaconsMissed_t;

/** statistics threshold for LinkQuality */
typedef MLAN_PACK_START struct {
	/** Header */
	MrvlIEtypesHeader_t header;
	/** Link SNR threshold (dB) */
	t_u16 link_snr;
	/** Link SNR frequency */
	t_u16 link_snr_freq;
	/* Second minimum rate value as per the rate table below */
	t_u16 link_rate;
	/* Second minimum rate frequency */
	t_u16 link_rate_freq;
	/* Tx latency value (us) */
	t_u16 link_tx_latency;
	/* Tx latency frequency */
	t_u16 link_tx_lantency_freq;
} MLAN_PACK_END MrvlIEtypes_LinkQualityThreshold_t;

#ifdef PCIE
/** PCIE dual descriptor for data/event */
typedef MLAN_PACK_START struct _dual_desc_buf {
	/** buf size */
	t_u16 len;
	/** buffer descriptor flags */
	t_u16 flags;
	/** pkt size */
	t_u16 pkt_size;
	/** reserved */
	t_u16 reserved;
	/** Physical address of the buffer */
	t_u64 paddr;
} MLAN_PACK_END adma_dual_desc_buf, *padma_dual_desc_buf;

#if defined(PCIE8997) || defined(PCIE8897)
/** PCIE ring buffer description for DATA */
typedef MLAN_PACK_START struct _mlan_pcie_data_buf {
	/** Buffer descriptor flags */
	t_u16 flags;
	/** Offset of fragment/pkt to start of ip header */
	t_u16 offset;
	/** Fragment length of the buffer */
	t_u16 frag_len;
	/** Length of the buffer */
	t_u16 len;
	/** Physical address of the buffer */
	t_u64 paddr;
	/** Reserved */
	t_u32 reserved;
} MLAN_PACK_END mlan_pcie_data_buf, *pmlan_pcie_data_buf;

/** PCIE ring buffer description for EVENT */
typedef MLAN_PACK_START struct _mlan_pcie_evt_buf {
	/** Physical address of the buffer */
	t_u64 paddr;
	/** Length of the buffer */
	t_u16 len;
	/** Buffer descriptor flags */
	t_u16 flags;
} MLAN_PACK_END mlan_pcie_evt_buf, *pmlan_pcie_evt_buf;

/** PCIE host buffer configuration */
typedef MLAN_PACK_START struct _HostCmd_DS_PCIE_HOST_BUF_DETAILS {
	/** TX buffer descriptor ring address */
	t_u32 txbd_addr_lo;
	t_u32 txbd_addr_hi;
	/** TX buffer descriptor ring count */
	t_u32 txbd_count;

	/** RX buffer descriptor ring address */
	t_u32 rxbd_addr_lo;
	t_u32 rxbd_addr_hi;
	/** RX buffer descriptor ring count */
	t_u32 rxbd_count;

	/** Event buffer descriptor ring address */
	t_u32 evtbd_addr_lo;
	t_u32 evtbd_addr_hi;
	/** Event buffer descriptor ring count */
	t_u32 evtbd_count;
} HostCmd_DS_PCIE_HOST_BUF_DETAILS;
#endif
#endif

typedef MLAN_PACK_START struct _HostCmd_DS_SENSOR_TEMP {
	t_u32 temperature;
} MLAN_PACK_END HostCmd_DS_SENSOR_TEMP;

#define TLV_TYPE_IPV6_RA_OFFLOAD (PROPRIETARY_TLV_BASE_ID + 0xE6) /** 0x1E6*/
typedef MLAN_PACK_START struct {
	MrvlIEtypesHeader_t Header;
	t_u8 ipv6_addr[16];
} MLAN_PACK_END MrvlIETypes_IPv6AddrParamSet_t;

typedef MLAN_PACK_START struct _HostCmd_DS_IPV6_RA_OFFLOAD {
	/** 0x0000: Get IPv6 RA Offload configuration
	 *  0x0001: Set IPv6 RA Offload configuration
	 */
	t_u16 action;
	/** 0x00: disable IPv6 RA Offload; 0x01: enable IPv6 RA offload */
	t_u8 enable;
	MrvlIETypes_IPv6AddrParamSet_t ipv6_addr_param;
} MLAN_PACK_END HostCmd_DS_IPV6_RA_OFFLOAD;

#ifdef STA_SUPPORT
typedef MLAN_PACK_START struct _HostCmd_DS_STA_CONFIGURE {
	/** Action Set or get */
	t_u16 action;
	/** Tlv buffer */
	t_u8 tlv_buffer[];
	/**MrvlIEtypes_channel_band_t band_channel; */
} MLAN_PACK_END HostCmd_DS_STA_CONFIGURE;
#endif

/** HostCmd_DS_AUTO_TX structure */
typedef MLAN_PACK_START struct _HostCmd_DS_AUTO_TX {
	/** Action Set or get */
	t_u16 action;
	/** Tlv buffer */
	t_u8 tlv_buffer[];
} MLAN_PACK_END HostCmd_DS_AUTO_TX;

#define OID_CLOUD_KEEP_ALIVE 0
#define EVENT_CLOUD_KEEP_ALIVE_RETRY_FAIL 133
/** TLV for cloud keep alive control info */
#define TLV_TYPE_CLOUD_KEEP_ALIVE                                              \
	(PROPRIETARY_TLV_BASE_ID + 0x102)	/* 0x0100 + 258 */
typedef MLAN_PACK_START struct _MrvlIEtypes_Cloud_Keep_Alive_t {
	/** Header */
	MrvlIEtypesHeader_t header;
	/** ID for cloud keep alive */
	t_u8 keep_alive_id;
	/** Enable/disable for this ID */
	t_u8 enable;
	/** TLV buffer */
	t_u8 tlv[];
} MLAN_PACK_END MrvlIEtypes_Cloud_Keep_Alive_t;

/** TLV for cloud keep alive control info */
#define TLV_TYPE_KEEP_ALIVE_CTRL                                               \
	(PROPRIETARY_TLV_BASE_ID + 0x103)	/* 0x0100 + 259 */
typedef MLAN_PACK_START struct _MrvlIEtypes_Keep_Alive_Ctrl_t {
	/** Header */
	MrvlIEtypesHeader_t header;
	/** period to send keep alive packet */
	t_u32 snd_interval;
	/** period to send retry packet */
	t_u16 retry_interval;
	/** count to send retry packet */
	t_u16 retry_count;
} MLAN_PACK_END MrvlIEtypes_Keep_Alive_Ctrl_t;

/** TLV for cloud keep alive packet */
#define TLV_TYPE_KEEP_ALIVE_PKT                                                \
	(PROPRIETARY_TLV_BASE_ID + 0x104)	/* 0x0100 + 260 */
typedef MLAN_PACK_START struct _MrvlIEtypes_Keep_Alive_Pkt_t {
	/** Header */
	MrvlIEtypesHeader_t header;
	/** Ethernet Header */
	Eth803Hdr_t eth_header;
	/** packet buffer*/
	t_u8 ip_packet[];
} MLAN_PACK_END MrvlIEtypes_Keep_Alive_Pkt_t;

/** TLV to indicate firmware only keep probe response while scan */
#define TLV_TYPE_ONLYPROBERESP (PROPRIETARY_TLV_BASE_ID + 0xE9)	/* 0x01E9 */
typedef MLAN_PACK_START struct _MrvlIEtypes_OnlyProberesp_t {
	/** Header */
	MrvlIEtypesHeader_t header;
	/** only keep probe response */
	t_u8 proberesp_only;
} MLAN_PACK_END MrvlIEtypes_OnlyProberesp_t;

#if defined(DRV_EMBEDDED_AUTHENTICATOR) || defined(DRV_EMBEDDED_SUPPLICANT)
#define HostCmd_CMD_CRYPTO 0x025e

#define HostCmd_CMD_CRYPTO_SUBCMD_PRF_HMAC_SHA1 (0x1)
#define HostCmd_CMD_CRYPTO_SUBCMD_HMAC_SHA1 (0x2)
#define HostCmd_CMD_CRYPTO_SUBCMD_HMAC_SHA256 (0x3)
#define HostCmd_CMD_CRYPTO_SUBCMD_SHA256 (0x4)
#define HostCmd_CMD_CRYPTO_SUBCMD_RIJNDAEL (0x5)
#define HostCmd_CMD_CRYPTO_SUBCMD_RC4 (0x6)
#define HostCmd_CMD_CRYPTO_SUBCMD_MD5 (0x7)
#define HostCmd_CMD_CRYPTO_SUBCMD_MRVL_F (0x8)
#define HostCmd_CMD_CRYPTO_SUBCMD_SHA256_KDF (0x9)

#define TLV_TYPE_CRYPTO_KEY (PROPRIETARY_TLV_BASE_ID + 308)
#define TLV_TYPE_CRYPTO_KEY_IV (PROPRIETARY_TLV_BASE_ID + 309)
#define TLV_TYPE_CRYPTO_KEY_PREFIX (PROPRIETARY_TLV_BASE_ID + 310)
#define TLV_TYPE_CRYPTO_KEY_DATA_BLK (PROPRIETARY_TLV_BASE_ID + 311)

/** MrvlIEParamSet_t */
typedef MLAN_PACK_START struct {
	/** Type */
	t_u16 Type;
	/** Length */
	t_u16 Length;
} MLAN_PACK_END MrvlIEParamSet_t;

/** HostCmd_DS_CRYPTO */
typedef MLAN_PACK_START struct _HostCmd_DS_CRYPTO {
	/** action */
	t_u16 action;
	/** subCmdCode */
	t_u8 subCmdCode;
	/** subCmd start */
	t_u8 subCmd[];
} MLAN_PACK_END HostCmd_DS_CRYPTO;

/** subcmd_prf_hmac_sha1 used by prf_hmac_sha1, md5 and sha256_kdf */
typedef MLAN_PACK_START struct _subcmd_prf_hmac_sha1 {
	/** output_len */
	t_u16 output_len;
	/** tlv start */
	t_u8 tlv[];
} MLAN_PACK_END subcmd_prf_hmac_sha1_t, subcmd_md5_t, subcmd_sha256_kdf_t;

/** subcmd_hmac_sha1 used by hmac_sha1, hmac_sha256, sha256 */
typedef MLAN_PACK_START struct _subcmd_hmac_sha1 {
	/** output_len */
	t_u16 output_len;
	/** number of data blocks */
	t_u16 data_blks_nr;
	/** tlv start */
	t_u8 tlv[];
} MLAN_PACK_END subcmd_hmac_sha1_t, subcmd_hmac_sha256_t, subcmd_sha256_t;

/** subcmd_rijndael, used by rijndael */
typedef MLAN_PACK_START struct _subcmd_rijndael {
	/** output_len */
	t_u16 output_len;
	/** sub action code */
	t_u8 sub_action_code;
	/** tlv start */
	t_u8 tlv[];
} MLAN_PACK_END subcmd_rijndael_t;

/** subcmd_rc4, used by rc4 */
typedef MLAN_PACK_START struct _subcmd_rc4 {
	/** output_len */
	t_u16 output_len;
	/** skip bytes */
	t_u16 skip_bytes;
	/** tlv start */
	t_u8 tlv[];
} MLAN_PACK_END subcmd_rc4_t;

/** subcmd_mrvf_f, used by mrvl_f*/
typedef MLAN_PACK_START struct _subcmd_mrvf_f {
	/** output_len */
	t_u16 output_len;
	/** iterations */
	t_u32 iterations;
	/** count */
	t_u32 count;
	/** tlv start */
	t_u8 tlv[];
} MLAN_PACK_END subcmd_mrvl_f_t;

#endif

#ifdef UAP_SUPPORT
/** action add station */
#define HostCmd_ACT_ADD_STA 0x1
/** remove station */
#define HostCmd_ACT_REMOVE_STA 0x0
/** HostCmd_DS_ADD_STATION */
typedef MLAN_PACK_START struct _HostCmd_DS_ADD_STATION {
	/** 1 -add, 0 --delete */
	t_u16 action;
	/** aid */
	t_u16 aid;
	/** peer_mac */
	t_u8 peer_mac[MLAN_MAC_ADDR_LENGTH];
	/** Listen Interval */
	int listen_interval;
	/** Capability Info */
	t_u16 cap_info;
	/** tlv start */
	t_u8 tlv[];
} MLAN_PACK_END HostCmd_DS_ADD_STATION;

/** Host Command ID : Add New Station */
#define HostCmd_CMD_ADD_NEW_STATION 0x025f
/** TLV id: station flag */
#define TLV_TYPE_UAP_STA_FLAGS (PROPRIETARY_TLV_BASE_ID + 313)
/**MrvlIEtypes_Sta_Flag_t */
typedef MLAN_PACK_START struct _MrvlIEtypes_StaFlag_t {
	/** Header */
	MrvlIEtypesHeader_t header;
	/** station flag     */
	t_u32 sta_flags;
} MLAN_PACK_END MrvlIEtypes_StaFlag_t;
#endif

/** Host Command ID : _HostCmd_DS_BAND_STEERING */
typedef MLAN_PACK_START struct _HostCmd_DS_BAND_STEERING {
	/** ACT_GET/ACT_SET */
	t_u8 action;
	/** State */
	t_u8 state;
	/** probe requests to be blocked on 2g */
	t_u8 block_2g_prb_req;
	/** limit the btm request sent to STA at <max_btm_req_allowed>*/
	t_u8 max_btm_req_allowed;
} MLAN_PACK_END HostCmd_DS_BAND_STEERING;

/** HostCmd_CMD_RX_ABORT_CFG */
typedef MLAN_PACK_START struct _HostCmd_DS_CMD_RX_ABORT_CFG {
	/** Action */
	t_u16 action;
	/** Enable/disable rx abort on weak pkt rssi */
	t_u8 enable;
	/** rx weak rssi pkt threshold */
	t_s8 rssi_threshold;
} MLAN_PACK_END HostCmd_DS_CMD_RX_ABORT_CFG;
/** HostCmd_CMD_RX_ABORT_CFG_EXT */
typedef MLAN_PACK_START struct _HostCmd_DS_CMD_RX_ABORT_CFG_EXT {
	/** Action */
	t_u16 action;
	/** Enable/disable dyn rx abort on weak pkt rssi */
	t_u8 enable;
	/** specify rssi margin */
	t_s8 rssi_margin;
	/** specify ceil rssi threshold */
	t_s8 ceil_rssi_threshold;
} MLAN_PACK_END HostCmd_DS_CMD_RX_ABORT_CFG_EXT;

/** HostCmd_CMD_ARB_CONFIG */
typedef MLAN_PACK_START struct _HostCmd_DS_CMD_ARB_CONFIG {
	/** Action */
	t_u16 action;
	/** 0-4 */
	t_u32 arb_mode;
	/** 1: use FW enhancement, 0: use FW default */
	t_u32 reserved;
} MLAN_PACK_END HostCmd_DS_CMD_ARB_CONFIG;

/** HostCmd_DS_CMD_TX_AMPDU_PROT_MODE */
typedef MLAN_PACK_START struct _HostCmd_DS_CMD_TX_AMPDU_PROT_MODE {
	/** Action */
	t_u16 action;
	/** Prot mode */
	t_u16 mode;
} MLAN_PACK_END HostCmd_DS_CMD_TX_AMPDU_PROT_MODE;

/** HostCmd_DS_CMD_DOT11MC_UNASSOC_FTM_CFG */
typedef MLAN_PACK_START struct _HostCmd_DS_CMD_DOT11MC_UNASSOC_FTM_CFG {
	/** Action */
	t_u16 action;
	/** Cfg state */
	t_u16 state;
} MLAN_PACK_END HostCmd_DS_CMD_DOT11MC_UNASSOC_FTM_CFG;

/** HostCmd_CMD_RATE_ADAPT_CFG */
typedef MLAN_PACK_START struct _HostCmd_DS_CMD_RATE_ADAPT_CFG {
	/** Action */
	t_u16 action;
	/** SR Rateadapt*/
	t_u8 sr_rateadapt;
	/** set low threshold */
	t_u8 ra_low_thresh;
	/** set high threshold */
	t_u8 ra_high_thresh;
	/** set interval */
	t_u16 ra_interval;
} MLAN_PACK_END HostCmd_DS_CMD_RATE_ADAPT_CFG;

/** HostCmd_CMD_CCK_DESENSE_CFG */
typedef MLAN_PACK_START struct _HostCmd_DS_CMD_CCK_DESENSE_CFG {
	/** Action */
	t_u16 action;
	/** cck desense mode: 0:disable 1:normal 2:dynamic */
	t_u16 mode;
	/** specify rssi margin */
	t_s8 margin;
	/** specify ceil rssi threshold */
	t_s8 ceil_thresh;
	/** cck desense "on" interval count */
	t_u8 num_on_intervals;
	/** cck desense "off" interval count */
	t_u8 num_off_intervals;
} MLAN_PACK_END HostCmd_DS_CMD_CCK_DESENSE_CFG;

/** HostCmd_DS_COMMAND */
typedef struct MLAN_PACK_START _HostCmd_DS_COMMAND {
	/** Command Header : Command */
	t_u16 command;
	/** Command Header : Size */
	t_u16 size;
	/** Command Header : Sequence number */
	t_u16 seq_num;
	/** Command Header : Result */
	t_u16 result;
	/** Command Body */
	union {
		/** Hardware specifications */
		HostCmd_DS_GET_HW_SPEC hw_spec;
#ifdef SDIO
		HostCmd_DS_SDIO_SP_RX_AGGR_CFG sdio_rx_aggr;
#endif
		/** Cfg data */
		HostCmd_DS_802_11_CFG_DATA cfg_data;
		/** MAC control */
		HostCmd_DS_MAC_CONTROL mac_ctrl;
		/** MAC address */
		HostCmd_DS_802_11_MAC_ADDRESS mac_addr;
		/** MAC muticast address */
		HostCmd_DS_MAC_MULTICAST_ADR mc_addr;
		/** Get log */
		HostCmd_DS_802_11_GET_LOG get_log;
		/** Get link layer statistic */
		HostCmd_DS_802_11_LINK_STATISTIC get_link_statistic;
		/** RSSI information */
		HostCmd_DS_802_11_RSSI_INFO_EXT rssi_info_ext;
		/** RSSI information */
		HostCmd_DS_802_11_RSSI_INFO rssi_info;
		/** RSSI information response */
		HostCmd_DS_802_11_RSSI_INFO_RSP rssi_info_rsp;
		/** SNMP MIB */
		HostCmd_DS_802_11_SNMP_MIB smib;
#ifdef UAP_SUPPORT
		/** UAP SNMP MIB */
		HostCmd_DS_UAP_802_11_SNMP_MIB uap_smib;
#endif
		/** Radio control */
		HostCmd_DS_802_11_RADIO_CONTROL radio;
		/** RF channel */
		HostCmd_DS_802_11_RF_CHANNEL rf_channel;
		/** Tx rate query */
		HostCmd_TX_RATE_QUERY tx_rate;
		/** Tx rate configuration */
		HostCmd_DS_TX_RATE_CFG tx_rate_cfg;
		/** Tx power configuration */
		HostCmd_DS_TXPWR_CFG txp_cfg;
		/** RF Tx power configuration */
		HostCmd_DS_802_11_RF_TX_POWER txp;

		/** RF antenna */
		HostCmd_DS_802_11_RF_ANTENNA antenna;

		/** CW Mode: Tx CW Level control */
		HostCmd_DS_CW_MODE_CTRL cwmode;
		/** Enhanced power save command */
		HostCmd_DS_802_11_PS_MODE_ENH psmode_enh;
		HostCmd_DS_802_11_HS_CFG_ENH opt_hs_cfg;
		/** Scan */
		HostCmd_DS_802_11_SCAN scan;
		/** Extended Scan */
		HostCmd_DS_802_11_SCAN_EXT ext_scan;

		/** Mgmt frame subtype mask */
		HostCmd_DS_RX_MGMT_IND rx_mgmt_ind;
		/** Scan response */
		HostCmd_DS_802_11_SCAN_RSP scan_resp;

		HostCmd_DS_802_11_BG_SCAN_CONFIG bg_scan_config;
		HostCmd_DS_802_11_BG_SCAN_QUERY bg_scan_query;
		HostCmd_DS_802_11_BG_SCAN_QUERY_RSP bg_scan_query_resp;
		HostCmd_DS_SUBSCRIBE_EVENT subscribe_event;
		HostCmd_DS_OTP_USER_DATA otp_user_data;
		/** Associate */
		HostCmd_DS_802_11_ASSOCIATE associate;

		/** Associate response */
		HostCmd_DS_802_11_ASSOCIATE_RSP associate_rsp;
		/** Deauthenticate */
		HostCmd_DS_802_11_DEAUTHENTICATE deauth;
		/** Ad-Hoc start */
		HostCmd_DS_802_11_AD_HOC_START adhoc_start;
		/** Ad-Hoc start result */
		HostCmd_DS_802_11_AD_HOC_START_RESULT adhoc_start_result;
		/** Ad-Hoc join result */
		HostCmd_DS_802_11_AD_HOC_JOIN_RESULT adhoc_join_result;
		/** Ad-Hoc join */
		HostCmd_DS_802_11_AD_HOC_JOIN adhoc_join;
		/** Domain information */
		HostCmd_DS_802_11D_DOMAIN_INFO domain_info;
		/** Domain information response */
		HostCmd_DS_802_11D_DOMAIN_INFO_RSP domain_info_resp;
		HostCmd_DS_802_11_TPC_ADAPT_REQ tpc_req;
		HostCmd_DS_802_11_TPC_INFO tpc_info;
		HostCmd_DS_802_11_CHAN_SW_ANN chan_sw_ann;
		HostCmd_DS_CHAN_RPT_REQ chan_rpt_req;
		HostCmd_DS_MEASUREMENT_REQUEST meas_req;
		HostCmd_DS_MEASUREMENT_REPORT meas_rpt;
		/** Add BA request */
		HostCmd_DS_11N_ADDBA_REQ add_ba_req;
		/** Add BA response */
		HostCmd_DS_11N_ADDBA_RSP add_ba_rsp;
		/** Delete BA entry */
		HostCmd_DS_11N_DELBA del_ba;
		/** Tx buffer configuration */
		HostCmd_DS_TXBUF_CFG tx_buf;
		/** AMSDU Aggr Ctrl configuration */
		HostCmd_DS_AMSDU_AGGR_CTRL amsdu_aggr_ctrl;
		/** 11n configuration */
		HostCmd_DS_11N_CFG htcfg;
		/** reject addba req conditions configuration */
		HostCmd_DS_REJECT_ADDBA_REQ rejectaddbareq;
		/* RANDYTODO need add more */
		/** HostCmd_DS_11AC_CFG */
		HostCmd_DS_11AC_CFG vhtcfg;
		/** HostCmd_DS_11ACTXBUF_CFG*/
		HostCmd_DS_11ACTXBUF_CFG ac_tx_buf;
		/** 11n configuration */
		HostCmd_DS_TX_BF_CFG tx_bf_cfg;
		/** WMM status get */
		HostCmd_DS_WMM_GET_STATUS get_wmm_status;
		/** WMM ADDTS */
		HostCmd_DS_WMM_ADDTS_REQ add_ts;
		/** WMM DELTS */
		HostCmd_DS_WMM_DELTS_REQ del_ts;
		/** WMM set/get queue config */
		HostCmd_DS_WMM_QUEUE_CONFIG queue_config;
		/** WMM param config*/
		HostCmd_DS_WMM_PARAM_CONFIG param_config;
		/** WMM on/of/get queue statistics */
		HostCmd_DS_WMM_QUEUE_STATS queue_stats;
		/** WMM get traffic stream status */
		HostCmd_DS_WMM_TS_STATUS ts_status;
		/** Key material */
		HostCmd_DS_802_11_KEY_MATERIAL key_material;
		/** GTK Rekey parameters */
		HostCmd_DS_GTK_REKEY_PARAMS gtk_rekey;
		/** E-Supplicant PSK */
		HostCmd_DS_802_11_SUPPLICANT_PMK esupplicant_psk;
		/** E-Supplicant profile */
		HostCmd_DS_802_11_SUPPLICANT_PROFILE esupplicant_profile;
		/** Extended version */
		HostCmd_DS_VERSION_EXT verext;
		/** Adhoc Coalescing */
		HostCmd_DS_802_11_IBSS_STATUS ibss_coalescing;
		/** Mgmt IE list configuration */
		HostCmd_DS_MGMT_IE_LIST_CFG mgmt_ie_list;
		/** TDLS configuration command */
		HostCmd_DS_TDLS_CONFIG tdls_config_data;
		/** TDLS operation command */
		HostCmd_DS_TDLS_OPER tdls_oper_data;
		/** System clock configuration */
		HostCmd_DS_ECL_SYSTEM_CLOCK_CONFIG sys_clock_cfg;
		/** MAC register access */
		HostCmd_DS_MAC_REG_ACCESS mac_reg;
		/** BBP register access */
		HostCmd_DS_BBP_REG_ACCESS bbp_reg;
		/** RF register access */
		HostCmd_DS_RF_REG_ACCESS rf_reg;
		/** EEPROM register access */
		HostCmd_DS_802_11_EEPROM_ACCESS eeprom;
		/** Memory access */
		HostCmd_DS_MEM_ACCESS mem;
		/** Target device access */
		HostCmd_DS_TARGET_ACCESS target;
		/** BCA register access */
		HostCmd_DS_BCA_REG_ACCESS bca_reg;
		/** Inactivity timeout extend */
		HostCmd_DS_INACTIVITY_TIMEOUT_EXT inactivity_to;
#ifdef UAP_SUPPORT
		HostCmd_DS_SYS_CONFIG sys_config;
		HostCmd_DS_SYS_INFO sys_info;
		HostCmd_DS_STA_DEAUTH sta_deauth;
		HostCmd_DS_STA_LIST sta_list;
		HostCmd_DS_POWER_MGMT_EXT pm_cfg;
		HostCmd_DS_REPORT_MIC report_mic;
		HostCmd_DS_UAP_OPER_CTRL uap_oper_ctrl;
#endif				/* UAP_SUPPORT */
		HostCmd_DS_TX_RX_HISTOGRAM tx_rx_histogram;

		/** Sleep period command */
		HostCmd_DS_802_11_SLEEP_PERIOD sleep_pd;
		/** Sleep params command */
		HostCmd_DS_802_11_SLEEP_PARAMS sleep_param;

#ifdef SDIO
		/** SDIO GPIO interrupt config command */
		HostCmd_DS_SDIO_GPIO_INT_CONFIG sdio_gpio_int;
		HostCmd_DS_SDIO_PULL_CTRL sdio_pull_ctl;
#endif
		HostCmd_DS_SET_BSS_MODE bss_mode;
		HostCmd_DS_CMD_TX_DATA_PAUSE tx_data_pause;
#if defined(PCIE)
#if defined(PCIE8997) || defined(PCIE8897)
		HostCmd_DS_PCIE_HOST_BUF_DETAILS pcie_host_spec;
#endif
#endif
		HostCmd_DS_REMAIN_ON_CHANNEL remain_on_chan;
#ifdef WIFI_DIRECT_SUPPORT
		HostCmd_DS_WIFI_DIRECT_MODE wifi_direct_mode;
		HostCmd_DS_WIFI_DIRECT_PARAM_CONFIG p2p_params_config;
#endif
		HostCmd_DS_GPIO_TSF_LATCH_PARAM_CONFIG gpio_tsf_latch;
		HostCmd_DS_COALESCE_CONFIG coalesce_config;
		HostCmd_DS_HS_WAKEUP_REASON hs_wakeup_reason;
		HostCmd_DS_PACKET_AGGR_CTRL aggr_ctrl;
#ifdef USB
		HostCmd_DS_PACKET_AGGR_OVER_HOST_INTERFACE packet_aggr;
#endif
		HostCmd_CONFIG_LOW_PWR_MODE low_pwr_mode_cfg;
		HostCmd_DS_TSF tsf;
		HostCmd_DS_DFS_REPEATER_MODE dfs_repeater;
#ifdef RX_PACKET_COALESCE
		HostCmd_DS_RX_PKT_COAL_CFG rx_pkt_coal_cfg;
#endif
		HostCmd_DS_EAPOL_PKT eapol_pkt;
		HostCmd_DS_SENSOR_TEMP temp_sensor;
		HostCMD_DS_APCMD_ACS_SCAN acs_scan;
		HostCmd_DS_MIMO_SWITCH mimo_switch;
		HostCmd_DS_IPV6_RA_OFFLOAD ipv6_ra_offload;
#ifdef STA_SUPPORT
		HostCmd_DS_STA_CONFIGURE sta_cfg;
#endif
		/** GPIO Independent reset configure */
		HostCmd_DS_INDEPENDENT_RESET_CFG ind_rst_cfg;
		HostCmd_DS_802_11_PS_INACTIVITY_TIMEOUT ps_inact_tmo;
		HostCmd_DS_CHAN_REGION_CFG reg_cfg;
		HostCmd_DS_AUTO_TX auto_tx;
		HostCmd_DS_DYN_BW dyn_bw;
		HostCmd_DS_802_11_ROBUSTCOEX robustcoexparams;
		HostCmd_DS_DMCS_CFG dmcs;
#if defined(PCIE)
		HostCmd_DS_SSU_CFG ssu_params;
#endif
		/** boot sleep configure */
		HostCmd_DS_BOOT_SLEEP boot_sleep;
#if defined(DRV_EMBEDDED_AUTHENTICATOR) || defined(DRV_EMBEDDED_SUPPLICANT)
		/** crypto cmd */
		HostCmd_DS_CRYPTO crypto_cmd;
#endif
#ifdef UAP_SUPPORT
		/** Add station cmd */
		HostCmd_DS_ADD_STATION sta_info;
#endif
		/** HostCmd_DS_11AX_CFG */
		HostCmd_DS_11AX_CFG axcfg;
		/** HostCmd_DS_11AX_CMD_CFG */
		HostCmd_DS_11AX_CMD_CFG axcmd;
		HostCmd_DS_RANGE_EXT range_ext;
		/** HostCmd_DS_TWT_CFG */
		HostCmd_DS_TWT_CFG twtcfg;

		HostCmd_DS_CMD_RX_ABORT_CFG rx_abort_cfg;
		HostCmd_DS_CMD_RX_ABORT_CFG_EXT rx_abort_cfg_ext;
		HostCmd_DS_CMD_TX_AMPDU_PROT_MODE tx_ampdu_prot_mode;
		HostCmd_DS_CMD_RATE_ADAPT_CFG rate_adapt_cfg;
		HostCmd_DS_CMD_CCK_DESENSE_CFG cck_desense_cfg;
		/** trpc_config */
		HostCmd_DS_CHANNEL_TRPC_CONFIG ch_trpc_config;
		HostCmd_DS_LOW_POWER_MODE_CFG lpm_cfg;
		HostCmd_DS_BAND_STEERING band_steer_info;
		HostCmd_DS_BEACON_STUCK_CFG beacon_stuck_cfg;
		struct mfg_cmd_generic_cfg mfg_generic_cfg;
		struct mfg_cmd_tx_cont mfg_tx_cont;
		struct mfg_cmd_tx_frame2 mfg_tx_frame2;
		struct mfg_Cmd_HE_TBTx_t mfg_he_power;
		HostCmd_DS_CMD_ARB_CONFIG arb_cfg;
		HostCmd_DS_CMD_DOT11MC_UNASSOC_FTM_CFG dot11mc_unassoc_ftm_cfg;
		HostCmd_DS_HAL_PHY_CFG hal_phy_cfg_params;
	} params;
} MLAN_PACK_END HostCmd_DS_COMMAND, *pHostCmd_DS_COMMAND;

/** PS_CMD_ConfirmSleep */
typedef MLAN_PACK_START struct _OPT_Confirm_Sleep {
	/** Command */
	t_u16 command;
	/** Size */
	t_u16 size;
	/** Sequence number */
	t_u16 seq_num;
	/** Result */
	t_u16 result;
	/** Action */
	t_u16 action;
	/** Sleep comfirm param definition */
	sleep_confirm_param sleep_cfm;
} MLAN_PACK_END OPT_Confirm_Sleep;

typedef struct MLAN_PACK_START _opt_sleep_confirm_buffer {
	/** Header for interface */
	t_u32 hdr;
	/** New power save command used to send
	 *  sleep confirmation to the firmware */
	OPT_Confirm_Sleep ps_cfm_sleep;
} MLAN_PACK_END opt_sleep_confirm_buffer;

/** req host side download vdll block */
#define VDLL_IND_TYPE_REQ 0
/** notify vdll start offset in firmware image */
#define VDLL_IND_TYPE_OFFSET 1
/** notify vdll download error: signature error */
#define VDLL_IND_TYPE_ERR_SIG 2
/** notify vdll download error: ID error */
#define VDLL_IND_TYPE_ERR_ID 3

/** vdll indicate event structure */
typedef MLAN_PACK_START struct _vdll_ind {
	/*VDLL ind type */
	t_u16 type;
	/*reserved */
	t_u16 reserved;
	/*indicate the offset downloaded so far */
	t_u32 offset;
	/*VDLL block size */
	t_u16 block_len;
} MLAN_PACK_END vdll_ind, *pvdll_ind;
#ifdef PRAGMA_PACK
#pragma pack(pop)
#endif

#endif /* !_MLAN_FW_H_ */
