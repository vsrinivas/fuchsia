/** @file mlan_ioctl.h
 *
 *  @brief This file declares the IOCTL data structures and APIs.
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
    11/07/2008: initial version
******************************************************/

#ifndef _MLAN_IOCTL_H_
#define _MLAN_IOCTL_H_

/** Enumeration for IOCTL request ID */
enum _mlan_ioctl_req_id {
	/* Scan Group */
	MLAN_IOCTL_SCAN = 0x00010000,
	MLAN_OID_SCAN_NORMAL = 0x00010001,
	MLAN_OID_SCAN_SPECIFIC_SSID = 0x00010002,
	MLAN_OID_SCAN_USER_CONFIG = 0x00010003,
	MLAN_OID_SCAN_CONFIG = 0x00010004,
	MLAN_OID_SCAN_GET_CURRENT_BSS = 0x00010005,
	MLAN_OID_SCAN_CANCEL = 0x00010006,
	MLAN_OID_SCAN_TABLE_FLUSH = 0x0001000A,
	MLAN_OID_SCAN_BGSCAN_CONFIG = 0x0001000B,
	/* BSS Configuration Group */
	MLAN_IOCTL_BSS = 0x00020000,
	MLAN_OID_BSS_START = 0x00020001,
	MLAN_OID_BSS_STOP = 0x00020002,
	MLAN_OID_BSS_MODE = 0x00020003,
	MLAN_OID_BSS_CHANNEL = 0x00020004,
	MLAN_OID_BSS_CHANNEL_LIST = 0x00020005,
	MLAN_OID_BSS_MAC_ADDR = 0x00020006,
	MLAN_OID_BSS_MULTICAST_LIST = 0x00020007,
	MLAN_OID_BSS_FIND_BSS = 0x00020008,
	MLAN_OID_IBSS_BCN_INTERVAL = 0x00020009,
	MLAN_OID_IBSS_ATIM_WINDOW = 0x0002000A,
	MLAN_OID_IBSS_CHANNEL = 0x0002000B,
#ifdef UAP_SUPPORT
	MLAN_OID_UAP_BSS_CONFIG = 0x0002000C,
	MLAN_OID_UAP_DEAUTH_STA = 0x0002000D,
	MLAN_OID_UAP_BSS_RESET = 0x0002000E,
#endif
#if defined(STA_SUPPORT) && defined(UAP_SUPPORT)
	MLAN_OID_BSS_ROLE = 0x0002000F,
#endif
#ifdef WIFI_DIRECT_SUPPORT
	MLAN_OID_WIFI_DIRECT_MODE = 0x00020010,
#endif
#ifdef STA_SUPPORT
	MLAN_OID_BSS_LISTEN_INTERVAL = 0x00020011,
#endif
	MLAN_OID_BSS_REMOVE = 0x00020014,
#ifdef UAP_SUPPORT
	MLAN_OID_UAP_CFG_WMM_PARAM = 0x00020015,
#endif
	MLAN_OID_BSS_11D_CHECK_CHANNEL = 0x00020016,
#ifdef UAP_SUPPORT
	MLAN_OID_UAP_ACS_SCAN = 0x00020017,
	MLAN_OID_UAP_SCAN_CHANNELS = 0x00020018,
	MLAN_OID_UAP_CHANNEL = 0x00020019,
	MLAN_OID_UAP_OPER_CTRL = 0x0002001A,
#endif
#ifdef STA_SUPPORT
	MLAN_OID_BSS_CHAN_INFO = 0x0002001B,
#endif
#ifdef UAP_SUPPORT
	MLAN_OID_UAP_ADD_STATION = 0x0002001C,
#endif

	MLAN_OID_BSS_FIND_BSSID = 0x0002001D,
#ifdef UAP_SUPPORT
	MLAN_OID_ACTION_CHAN_SWITCH = 0x0002001E,
#endif

	/* Radio Configuration Group */
	MLAN_IOCTL_RADIO_CFG = 0x00030000,
	MLAN_OID_RADIO_CTRL = 0x00030001,
	MLAN_OID_BAND_CFG = 0x00030002,
	MLAN_OID_ANT_CFG = 0x00030003,
	MLAN_OID_REMAIN_CHAN_CFG = 0x00030004,
	MLAN_OID_MIMO_SWITCH = 0x00030005,

	/* SNMP MIB Group */
	MLAN_IOCTL_SNMP_MIB = 0x00040000,
	MLAN_OID_SNMP_MIB_RTS_THRESHOLD = 0x00040001,
	MLAN_OID_SNMP_MIB_FRAG_THRESHOLD = 0x00040002,
	MLAN_OID_SNMP_MIB_RETRY_COUNT = 0x00040003,
	MLAN_OID_SNMP_MIB_DOT11D = 0x00040004,
#if defined(UAP_SUPPORT)
	MLAN_OID_SNMP_MIB_DOT11H = 0x00040005,
#endif
	MLAN_OID_SNMP_MIB_DTIM_PERIOD = 0x00040006,
	MLAN_OID_SNMP_MIB_SIGNALEXT_ENABLE = 0x00040007,
	MLAN_OID_SNMP_MIB_CTRL_DEAUTH = 0x00040008,

	/* Status Information Group */
	MLAN_IOCTL_GET_INFO = 0x00050000,
	MLAN_OID_GET_STATS = 0x00050001,
	MLAN_OID_GET_SIGNAL = 0x00050002,
	MLAN_OID_GET_FW_INFO = 0x00050003,
	MLAN_OID_GET_VER_EXT = 0x00050004,
	MLAN_OID_GET_BSS_INFO = 0x00050005,
	MLAN_OID_GET_DEBUG_INFO = 0x00050006,
#ifdef UAP_SUPPORT
	MLAN_OID_UAP_STA_LIST = 0x00050007,
#endif
	MLAN_OID_GET_SIGNAL_EXT = 0x00050008,
	MLAN_OID_LINK_STATS = 0x00050009,
	MLAN_OID_GET_UAP_STATS_LOG = 0x0005000A,
	/* Security Configuration Group */
	MLAN_IOCTL_SEC_CFG = 0x00060000,
	MLAN_OID_SEC_CFG_AUTH_MODE = 0x00060001,
	MLAN_OID_SEC_CFG_ENCRYPT_MODE = 0x00060002,
	MLAN_OID_SEC_CFG_WPA_ENABLED = 0x00060003,
	MLAN_OID_SEC_CFG_ENCRYPT_KEY = 0x00060004,
	MLAN_OID_SEC_CFG_PASSPHRASE = 0x00060005,
	MLAN_OID_SEC_CFG_EWPA_ENABLED = 0x00060006,
	MLAN_OID_SEC_CFG_ESUPP_MODE = 0x00060007,
	MLAN_OID_SEC_CFG_WAPI_ENABLED = 0x00060009,
	MLAN_OID_SEC_CFG_PORT_CTRL_ENABLED = 0x0006000A,
#ifdef UAP_SUPPORT
	MLAN_OID_SEC_CFG_REPORT_MIC_ERR = 0x0006000B,
#endif
	MLAN_OID_SEC_QUERY_KEY = 0x0006000C,

	/* Rate Group */
	MLAN_IOCTL_RATE = 0x00070000,
	MLAN_OID_RATE_CFG = 0x00070001,
	MLAN_OID_GET_DATA_RATE = 0x00070002,
	MLAN_OID_SUPPORTED_RATES = 0x00070003,

	/* Power Configuration Group */
	MLAN_IOCTL_POWER_CFG = 0x00080000,
	MLAN_OID_POWER_CFG = 0x00080001,
	MLAN_OID_POWER_CFG_EXT = 0x00080002,
	MLAN_OID_POWER_LOW_POWER_MODE = 0x00080003,

	/* Power Management Configuration Group */
	MLAN_IOCTL_PM_CFG = 0x00090000,
	MLAN_OID_PM_CFG_IEEE_PS = 0x00090001,
	MLAN_OID_PM_CFG_HS_CFG = 0x00090002,
	MLAN_OID_PM_CFG_INACTIVITY_TO = 0x00090003,
	MLAN_OID_PM_CFG_DEEP_SLEEP = 0x00090004,
	MLAN_OID_PM_CFG_SLEEP_PD = 0x00090005,
	MLAN_OID_PM_CFG_PS_CFG = 0x00090006,
	MLAN_OID_PM_CFG_SLEEP_PARAMS = 0x00090008,
#ifdef UAP_SUPPORT
	MLAN_OID_PM_CFG_PS_MODE = 0x00090009,
#endif /* UAP_SUPPORT */
	MLAN_OID_PM_INFO = 0x0009000A,
	MLAN_OID_PM_HS_WAKEUP_REASON = 0x0009000B,
	MLAN_OID_PM_MGMT_FILTER = 0x0009000C,
	MLAN_OID_PM_CFG_BCN_TIMEOUT = 0x0009000D,

	/* WMM Configuration Group */
	MLAN_IOCTL_WMM_CFG = 0x000A0000,
	MLAN_OID_WMM_CFG_ENABLE = 0x000A0001,
	MLAN_OID_WMM_CFG_QOS = 0x000A0002,
	MLAN_OID_WMM_CFG_ADDTS = 0x000A0003,
	MLAN_OID_WMM_CFG_DELTS = 0x000A0004,
	MLAN_OID_WMM_CFG_QUEUE_CONFIG = 0x000A0005,
	MLAN_OID_WMM_CFG_QUEUE_STATS = 0x000A0006,
	MLAN_OID_WMM_CFG_QUEUE_STATUS = 0x000A0007,
	MLAN_OID_WMM_CFG_TS_STATUS = 0x000A0008,

	/* WPS Configuration Group */
	MLAN_IOCTL_WPS_CFG = 0x000B0000,
	MLAN_OID_WPS_CFG_SESSION = 0x000B0001,

	/* 802.11n Configuration Group */
	MLAN_IOCTL_11N_CFG = 0x000C0000,
	MLAN_OID_11N_CFG_TX = 0x000C0001,
	MLAN_OID_11N_HTCAP_CFG = 0x000C0002,
	MLAN_OID_11N_CFG_ADDBA_REJECT = 0x000C0003,
	MLAN_OID_11N_CFG_AGGR_PRIO_TBL = 0x000C0004,
	MLAN_OID_11N_CFG_ADDBA_PARAM = 0x000C0005,
	MLAN_OID_11N_CFG_MAX_TX_BUF_SIZE = 0x000C0006,
	MLAN_OID_11N_CFG_AMSDU_AGGR_CTRL = 0x000C0007,
	MLAN_OID_11N_CFG_SUPPORTED_MCS_SET = 0x000C0008,
	MLAN_OID_11N_CFG_TX_BF_CAP = 0x000C0009,
	MLAN_OID_11N_CFG_TX_BF_CFG = 0x000C000A,
	MLAN_OID_11N_CFG_STREAM_CFG = 0x000C000B,
	MLAN_OID_11N_CFG_DELBA = 0x000C000C,
	MLAN_OID_11N_CFG_REJECT_ADDBA_REQ = 0x000C000D,
	MLAN_OID_11N_CFG_COEX_RX_WINSIZE = 0x000C000E,
	MLAN_OID_11N_CFG_TX_AGGR_CTRL = 0x000C000F,
	MLAN_OID_11N_CFG_IBSS_AMPDU_PARAM = 0x000C0010,
	MLAN_OID_11N_CFG_MIN_BA_THRESHOLD = 0x000C0011,

	/* 802.11d Configuration Group */
	MLAN_IOCTL_11D_CFG = 0x000D0000,
#ifdef STA_SUPPORT
	MLAN_OID_11D_CFG_ENABLE = 0x000D0001,
	MLAN_OID_11D_CLR_CHAN_TABLE = 0x000D0002,
#endif /* STA_SUPPORT */
#ifdef UAP_SUPPORT
	MLAN_OID_11D_DOMAIN_INFO = 0x000D0003,
#endif
	MLAN_OID_11D_DOMAIN_INFO_EXT = 0x000D0004,

	/* Register Memory Access Group */
	MLAN_IOCTL_REG_MEM = 0x000E0000,
	MLAN_OID_REG_RW = 0x000E0001,
	MLAN_OID_EEPROM_RD = 0x000E0002,
	MLAN_OID_MEM_RW = 0x000E0003,

	/* Multi-Radio Configuration Group */
	MLAN_IOCTL_MFR_CFG = 0x00100000,
	/* 802.11h Configuration Group */
	MLAN_IOCTL_11H_CFG = 0x00110000,
	MLAN_OID_11H_CHANNEL_CHECK = 0x00110001,
	MLAN_OID_11H_LOCAL_POWER_CONSTRAINT = 0x00110002,
	MLAN_OID_11H_DFS_TESTING = 0x00110003,
	MLAN_OID_11H_CHAN_REPORT_REQUEST = 0x00110004,
	MLAN_OID_11H_CHAN_SWITCH_COUNT = 0x00110005,
	MLAN_OID_11H_CHAN_NOP_INFO = 0x00110006,
	MLAN_OID_11H_DFS_W53_CFG = 0x00110008,

	/* 802.11n Configuration Group RANDYTODO for value assign */
	MLAN_IOCTL_11AC_CFG = 0x00120000,
	MLAN_OID_11AC_VHT_CFG = 0x00120001,
	MLAN_OID_11AC_CFG_SUPPORTED_MCS_SET = 0x00120002,
	MLAN_OID_11AC_OPERMODE_CFG = 0x00120003,

	/* 802.11ax Configuration Group  */
	MLAN_IOCTL_11AX_CFG = 0x00170000,
	MLAN_OID_11AX_HE_CFG = 0x00170001,
	MLAN_OID_11AX_CMD_CFG = 0x00170002,
	MLAN_OID_11AX_TWT_CFG = 0x00170003,

	/* Miscellaneous Configuration Group */
	MLAN_IOCTL_MISC_CFG = 0x00200000,
	MLAN_OID_MISC_GEN_IE = 0x00200001,
	MLAN_OID_MISC_REGION = 0x00200002,
	MLAN_OID_MISC_WARM_RESET = 0x00200003,
#ifdef SDIO
	MLAN_OID_MISC_SDIO_MPA_CTRL = 0x00200006,
#endif
	MLAN_OID_MISC_HOST_CMD = 0x00200007,
	MLAN_OID_MISC_SYS_CLOCK = 0x00200009,
	MLAN_OID_MISC_SOFT_RESET = 0x0020000A,
	MLAN_OID_MISC_WWS = 0x0020000B,
	MLAN_OID_MISC_ASSOC_RSP = 0x0020000C,
	MLAN_OID_MISC_INIT_SHUTDOWN = 0x0020000D,
	MLAN_OID_MISC_CUSTOM_IE = 0x0020000F,
	MLAN_OID_MISC_TDLS_CONFIG = 0x00200010,
	MLAN_OID_MISC_TX_DATAPAUSE = 0x00200012,
	MLAN_OID_MISC_IP_ADDR = 0x00200013,
	MLAN_OID_MISC_MAC_CONTROL = 0x00200014,
	MLAN_OID_MISC_MEF_CFG = 0x00200015,
	MLAN_OID_MISC_CFP_CODE = 0x00200016,
	MLAN_OID_MISC_COUNTRY_CODE = 0x00200017,
	MLAN_OID_MISC_THERMAL = 0x00200018,
	MLAN_OID_MISC_RX_MGMT_IND = 0x00200019,
	MLAN_OID_MISC_SUBSCRIBE_EVENT = 0x0020001A,
#ifdef DEBUG_LEVEL1
	MLAN_OID_MISC_DRVDBG = 0x0020001B,
#endif
	MLAN_OID_MISC_HOTSPOT_CFG = 0x0020001C,
	MLAN_OID_MISC_OTP_USER_DATA = 0x0020001D,
#ifdef USB
	MLAN_OID_MISC_USB_AGGR_CTRL = 0x0020001F,
#endif
	MLAN_OID_MISC_TXCONTROL = 0x00200020,
#ifdef STA_SUPPORT
	MLAN_OID_MISC_EXT_CAP_CFG = 0x00200021,
#endif
#if defined(STA_SUPPORT)
	MLAN_OID_MISC_PMFCFG = 0x00200022,
#endif
#ifdef WIFI_DIRECT_SUPPORT
	MLAN_OID_MISC_WIFI_DIRECT_CONFIG = 0x00200025,
#endif
	MLAN_OID_MISC_TDLS_OPER = 0x00200026,
	MLAN_OID_MISC_GET_TDLS_IES = 0x00200027,
	MLAN_OID_MISC_LOW_PWR_MODE = 0x00200029,
	MLAN_OID_MISC_MEF_FLT_CFG = 0x0020002A,
	MLAN_OID_MISC_DFS_REAPTER_MODE = 0x0020002B,
#ifdef RX_PACKET_COALESCE
	MLAN_OID_MISC_RX_PACKET_COALESCE = 0x0020002C,
#endif
	MLAN_OID_MISC_TDLS_CS_CHANNEL = 0x0020002D,
	MLAN_OID_MISC_COALESCE_CFG = 0x0020002E,
	MLAN_OID_MISC_TDLS_IDLE_TIME = 0x0020002F,
	MLAN_OID_MISC_GET_SENSOR_TEMP = 0x00200030,
	MLAN_OID_MISC_IPV6_RA_OFFLOAD = 0x00200036,
	MLAN_OID_MISC_GTK_REKEY_OFFLOAD = 0x00200037,
	MLAN_OID_MISC_OPER_CLASS = 0x00200038,
	MLAN_OID_MISC_PMIC_CFG = 0x00200039,
	MLAN_OID_MISC_IND_RST_CFG = 0x00200040,
	MLAN_OID_MISC_GET_TSF = 0x00200045,
	MLAN_OID_MISC_GET_CHAN_REGION_CFG = 0x00200046,
	MLAN_OID_MISC_CLOUD_KEEP_ALIVE = 0x00200048,
	MLAN_OID_MISC_OPER_CLASS_CHECK = 0x00200049,

	MLAN_OID_MISC_CWMODE_CTRL = 0x00200051,
	MLAN_OID_MISC_AGGR_CTRL = 0x00200052,
	MLAN_OID_MISC_DYN_BW = 0x00200053,
	MLAN_OID_MISC_FW_DUMP_EVENT = 0x00200054,
	MLAN_OID_MISC_PER_PKT_CFG = 0x00200055,

	MLAN_OID_MISC_ROBUSTCOEX = 0x00200056,
	MLAN_OID_MISC_GET_TX_RX_HISTOGRAM = 0x00200057,
	MLAN_OID_MISC_CFP_INFO = 0x00200060,
	MLAN_OID_MISC_BOOT_SLEEP = 0x00200061,
#if defined(PCIE)
	MLAN_OID_MISC_SSU = 0x00200062,
#endif
	MLAN_OID_MISC_DMCS_CONFIG = 0x00200065,
	MLAN_OID_MISC_RX_ABORT_CFG = 0x00200066,
	MLAN_OID_MISC_RX_ABORT_CFG_EXT = 0x00200067,
	MLAN_OID_MISC_TX_AMPDU_PROT_MODE = 0x00200068,
	MLAN_OID_MISC_RATE_ADAPT_CFG = 0x00200069,
	MLAN_OID_MISC_CCK_DESENSE_CFG = 0x00200070,
	MLAN_OID_MISC_GET_CHAN_TRPC_CFG = 0x00200072,
	MLAN_OID_MISC_BAND_STEERING = 0x00200073,
	MLAN_OID_MISC_GET_REGIONPWR_CFG = 0x00200074,
	MLAN_OID_MISC_RF_TEST_GENERIC = 0x00200075,
	MLAN_OID_MISC_RF_TEST_TX_CONT = 0x00200076,
	MLAN_OID_MISC_RF_TEST_TX_FRAME = 0x00200077,
	MLAN_OID_MISC_ARB_CONFIG = 0x00200078,
	MLAN_OID_MISC_BEACON_STUCK = 0x00200079,
	MLAN_OID_MISC_CFP_TABLE = 0x0020007A,
	MLAN_OID_MISC_RANGE_EXT = 0x0020007B,
	MLAN_OID_MISC_DOT11MC_UNASSOC_FTM_CFG = 0x0020007C,
	MLAN_OID_MISC_TP_STATE = 0x0020007D,
	MLAN_OID_MISC_HAL_PHY_CFG = 0x0020007E,
	MLAN_OID_MISC_RF_TEST_HE_POWER = 0X0020007F,
#ifdef UAP_SUPPORT
	MLAN_OID_MISC_WACP_MODE = 0x00200081,
#endif
	MLAN_OID_MISC_GPIO_TSF_LATCH = 0x00200082,
	MLAN_OID_MISC_GET_TSF_INFO = 0x00200083,
};

/** Sub command size */
#define MLAN_SUB_COMMAND_SIZE 4

/** Enumeration for the action of IOCTL request */
enum _mlan_act_ioctl {
	MLAN_ACT_SET = 1,
	MLAN_ACT_GET,
	MLAN_ACT_CANCEL,
	MLAN_ACT_CLEAR,
	MLAN_ACT_RESET,
	MLAN_ACT_DEFAULT
};

/** Enumeration for generic enable/disable */
enum _mlan_act_generic { MLAN_ACT_DISABLE = 0, MLAN_ACT_ENABLE = 1 };

/** Enumeration for scan mode */
enum _mlan_scan_mode {
	MLAN_SCAN_MODE_UNCHANGED = 0,
	MLAN_SCAN_MODE_BSS,
	MLAN_SCAN_MODE_IBSS,
	MLAN_SCAN_MODE_ANY
};

/** Enumeration for scan type */
enum _mlan_scan_type {
	MLAN_SCAN_TYPE_UNCHANGED = 0,
	MLAN_SCAN_TYPE_ACTIVE,
	MLAN_SCAN_TYPE_PASSIVE,
	MLAN_SCAN_TYPE_PASSIVE_TO_ACTIVE
};

/** Enumeration for passive to active scan */
enum _mlan_pass_to_act_scan {
	MLAN_PASS_TO_ACT_SCAN_UNCHANGED = 0,
	MLAN_PASS_TO_ACT_SCAN_EN,
	MLAN_PASS_TO_ACT_SCAN_DIS
};

/** Max number of supported rates */
#define MLAN_SUPPORTED_RATES 32

/** Mrvl Proprietary Tlv base */
#define PROPRIETARY_TLV_BASE_ID 0x100

/** RSSI scan */
#define SCAN_RSSI(RSSI) (0x100 - ((t_u8)(RSSI)))

/** Max passive scan time for each channel in milliseconds */
#define MRVDRV_MAX_PASSIVE_SCAN_CHAN_TIME 2000

/** Max active scan time for each channel in milliseconds  */
#define MRVDRV_MAX_ACTIVE_SCAN_CHAN_TIME 500
/** Max gap time between 2 scan in milliseconds  */
#define MRVDRV_MAX_SCAN_CHAN_GAP_TIME 500

/** Maximum number of probes to send on each channel */
#define MAX_PROBES 5

/** Default number of probes to send on each channel */
#define DEFAULT_PROBES 4

/**
 *  @brief Sub-structure passed in wlan_ioctl_get_scan_table_entry for each BSS
 *
 *  Fixed field information returned for the scan response in the IOCTL
 *    response.
 */
typedef struct _wlan_get_scan_table_fixed {
	/** BSSID of this network */
	t_u8 bssid[MLAN_MAC_ADDR_LENGTH];
	/** Channel this beacon/probe response was detected */
	t_u8 channel;
	/** RSSI for the received packet */
	t_u8 rssi;
	/** channel load */
	t_u8 chan_load;
	/** TSF value in microseconds from the firmware at packet reception */
	t_u64 network_tsf;
} wlan_get_scan_table_fixed;

/** mlan_802_11_ssid data structure */
typedef struct _mlan_802_11_ssid {
	/** SSID Length */
	t_u32 ssid_len;
	/** SSID information field */
	t_u8 ssid[MLAN_MAX_SSID_LENGTH];
} mlan_802_11_ssid, *pmlan_802_11_ssid;

typedef MLAN_PACK_START struct _tx_status_event {
	/** packet type */
	t_u8 packet_type;
	/** tx_token_id */
	t_u8 tx_token_id;
	/** 0--success, 1--fail, 2--watchdogtimeout */
	t_u8 status;
} MLAN_PACK_END tx_status_event;

/**
 *  Sructure to retrieve the scan table
 */
typedef struct {
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
} wlan_ioctl_get_scan_table_info;

/**
 *  Structure passed in the wlan_ioctl_get_scan_table_info for each
 *    BSS returned in the WLAN_GET_SCAN_RESP IOCTL
 */
typedef struct _wlan_ioctl_get_scan_table_entry {
	/**
	 *  Fixed field length included in the response.
	 *
	 *  Length value is included so future fixed fields can be added to the
	 *   response without breaking backwards compatibility.  Use the length
	 *   to find the offset for the bssInfoLength field, not a sizeof()
	 * calc.
	 */
	t_u32 fixed_field_length;

	/**
	 *  Length of the BSS Information (probe resp or beacon) that
	 *    follows after the fixed_field_length
	 */
	t_u32 bss_info_length;

	/**
	 *  Always present, fixed length data fields for the BSS
	 */
	wlan_get_scan_table_fixed fixed_fields;

	/*
	 * Probe response or beacon scanned for the BSS.
	 *
	 * Field layout:
	 *  - TSF              8 octets
	 *  - Beacon Interval  2 octets
	 *  - Capability Info  2 octets
	 *
	 *  - IEEE Infomation Elements; variable number & length per 802.11 spec
	 */
	/* t_u8  bss_info_buffer[]; */
} wlan_ioctl_get_scan_table_entry;

/** Type definition of mlan_scan_time_params */
typedef struct _mlan_scan_time_params {
	/** Scan channel time for specific scan in milliseconds */
	t_u32 specific_scan_time;
	/** Scan channel time for active scan in milliseconds */
	t_u32 active_scan_time;
	/** Scan channel time for passive scan in milliseconds */
	t_u32 passive_scan_time;
} mlan_scan_time_params, *pmlan_scan_time_params;

/** Type definition of mlan_user_scan */
typedef struct _mlan_user_scan {
	/** Length of scan_cfg_buf */
	t_u32 scan_cfg_len;
	/** Buffer of scan config */
	t_u8 scan_cfg_buf[1];
} mlan_user_scan, *pmlan_user_scan;

/** Type definition of mlan_scan_req */
typedef struct _mlan_scan_req {
	/** BSS mode for scanning */
	t_u32 scan_mode;
	/** Scan type */
	t_u32 scan_type;
	/** SSID */
	mlan_802_11_ssid scan_ssid;
	/** Scan time parameters */
	mlan_scan_time_params scan_time;
	/** Scan config parameters in user scan */
	mlan_user_scan user_scan;
} mlan_scan_req, *pmlan_scan_req;

/** Type defnition of mlan_scan_resp */
typedef struct _mlan_scan_resp {
	/** Number of scan result */
	t_u32 num_in_scan_table;
	/** Scan table */
	t_u8 *pscan_table;
	/* Age in seconds */
	t_u32 age_in_secs;
	/** channel statstics */
	t_u8 *pchan_stats;
	/** Number of records in the chan_stats */
	t_u32 num_in_chan_stats;
} mlan_scan_resp, *pmlan_scan_resp;

#define EXT_SCAN_TYPE_ENH 2
/** Type definition of mlan_scan_cfg */
typedef struct _mlan_scan_cfg {
	/** Scan type */
	t_u32 scan_type;
	/** BSS mode for scanning */
	t_u32 scan_mode;
	/** Scan probe */
	t_u32 scan_probe;
	/** Scan time parameters */
	mlan_scan_time_params scan_time;
	/** First passive scan then active scan */
	t_u8 passive_to_active_scan;
	/** Ext_scan:  0 disable, 1: enable, 2: enhance scan*/
	t_u32 ext_scan;
	/** scan channel gap */
	t_u32 scan_chan_gap;
} mlan_scan_cfg, *pmlan_scan_cfg;

/** Type defnition of mlan_ds_scan for MLAN_IOCTL_SCAN */
typedef struct _mlan_ds_scan {
	/** Sub-command */
	t_u32 sub_command;
	/** Scan request/response */
	union {
		/** Scan request */
		mlan_scan_req scan_req;
		/** Scan response */
		mlan_scan_resp scan_resp;
		/** Scan config parameters in user scan */
		mlan_user_scan user_scan;
		/** Scan config parameters */
		mlan_scan_cfg scan_cfg;
	} param;
} mlan_ds_scan, *pmlan_ds_scan;

/*-----------------------------------------------------------------*/
/** BSS Configuration Group */
/*-----------------------------------------------------------------*/
/** Enumeration for BSS mode */
enum _mlan_bss_mode {
	MLAN_BSS_MODE_INFRA = 1,
	MLAN_BSS_MODE_IBSS,
	MLAN_BSS_MODE_AUTO
};

/** Maximum key length */
#define MLAN_MAX_KEY_LENGTH 32

/** Maximum atim window in milliseconds */
#define MLAN_MAX_ATIM_WINDOW 50

/** Minimum beacon interval */
#define MLAN_MIN_BEACON_INTERVAL 20
/** Maximum beacon interval */
#define MLAN_MAX_BEACON_INTERVAL 1000
/** Default beacon interval */
#define MLAN_BEACON_INTERVAL 100

/** Receive all packets */
#define MLAN_PROMISC_MODE 1
/** Receive multicast packets in multicast list */
#define MLAN_MULTICAST_MODE 2
/** Receive all multicast packets */
#define MLAN_ALL_MULTI_MODE 4

/** Maximum size of multicast list */
#define MLAN_MAX_MULTICAST_LIST_SIZE 32

/** mlan_multicast_list data structure for MLAN_OID_BSS_MULTICAST_LIST */
typedef struct _mlan_multicast_list {
	/** Multicast mode */
	t_u32 mode;
	/** Number of multicast addresses in the list */
	t_u32 num_multicast_addr;
	/** Multicast address list */
	mlan_802_11_mac_addr mac_list[MLAN_MAX_MULTICAST_LIST_SIZE];
} mlan_multicast_list, *pmlan_multicast_list;

/** Max channel */
#define MLAN_MAX_CHANNEL 165
/** Maximum number of channels in table */
#define MLAN_MAX_CHANNEL_NUM 128

/** Channel/frequence for MLAN_OID_BSS_CHANNEL */
typedef struct _chan_freq {
	/** Channel Number */
	t_u32 channel;
	/** Frequency of this Channel */
	t_u32 freq;
} chan_freq;

/** mlan_chan_list data structure for MLAN_OID_BSS_CHANNEL_LIST */
typedef struct _mlan_chan_list {
	/** Number of channel */
	t_u32 num_of_chan;
	/** Channel-Frequency table */
	chan_freq cf[MLAN_MAX_CHANNEL_NUM];
} mlan_chan_list;

/* This channel is disabled.*/
#define CHAN_FLAGS_DISABLED MBIT(0)
/* do not initiate radiation, this includes sending probe requests or beaconing
 */
#define CHAN_FLAGS_NO_IR MBIT(1)
/* Radar detection is required on this channel */
#define CHAN_FLAGS_RADAR MBIT(3)
/* extension channel above this channel is not permitted */
#define CHAN_FLAGS_NO_HT40PLUS MBIT(4)
/* extension channel below this channel is not permitted */
#define CHAN_FLAGS_NO_HT40MINUS MBIT(5)
/* OFDM is not allowed on this channel */
#define CHAN_FLAGS_NO_OFDM MBIT(6)
/** 80Mhz can not used on this channel */
#define CHAN_FLAGS_NO_80MHZ MBIT(7)
/** 180Mhz can not used on this channel */
#define CHAN_FLAGS_NO_160MHZ MBIT(8)
/* Only indoor use is permitted on this channel */
#define CHAN_FLAGS_INDOOR_ONLY MBIT(9)
/* IR operation is allowed on this channel if it's
 * connected concurrently to a BSS on the same channel on
 * the 2 GHz band or to a channel in the same UNII band (on the 5 GHz
 * band), and IEEE80211_CHAN_RADAR is not set */
#define CHAN_FLAGS_IR_CONCURRENT MBIT(10)
/* 20 MHz operation is not allowed on this channel */
#define CHAN_FLAGS_20MHZ MBIT(11)
/* 10 MHz operation is not allowed on this channel */
#define CHAN_FLAGS_NO_10MHZ MBIT(12)
/** This channel's flag is valid */
#define CHAN_FLAGS_MAX MBIT(31)

/** Maximum response buffer length */
#define ASSOC_RSP_BUF_SIZE 500

/** Type definition of mlan_ds_misc_assoc_rsp for MLAN_OID_MISC_ASSOC_RSP */
typedef struct _mlan_ds_misc_assoc_rsp {
	/** Associate response buffer */
	t_u8 assoc_resp_buf[ASSOC_RSP_BUF_SIZE];
	/** Response buffer length */
	t_u32 assoc_resp_len;
} mlan_ds_misc_assoc_rsp, *pmlan_ds_misc_assoc_rsp;

/** mlan_ssid_bssid  data structure for
 *  MLAN_OID_BSS_START and MLAN_OID_BSS_FIND_BSS
 */
typedef struct _mlan_ssid_bssid {
	/** SSID */
	mlan_802_11_ssid ssid;
	/** BSSID */
	mlan_802_11_mac_addr bssid;
	/** index in BSSID list, start from 1 */
	t_u32 idx;
	/** Receive signal strength in dBm */
	t_s32 rssi;
	/* previous bssid */
	mlan_802_11_mac_addr prev_bssid;
	/**channel*/
	t_u16 channel;
	/**mobility domain value*/
	t_u16 ft_md;
	/**ft capability*/
	t_u8 ft_cap;
	/**band*/
	t_u16 bss_band;
	/** channel flag */
	t_u32 channel_flags;
	/** host mlme flag*/
	t_u8 host_mlme;
	/** assoicate resp frame/ie from firmware */
	mlan_ds_misc_assoc_rsp assoc_rsp;
} mlan_ssid_bssid, *pmlan_ssid_bssid;

/** Data structure of WMM ECW */
typedef struct _wmm_ecw_t {
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
} wmm_ecw_t, *pwmm_ecw_t;

/** Data structure of WMM Aci/Aifsn */
typedef struct _wmm_aci_aifsn_t {
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
} wmm_aci_aifsn_t, *pwmm_aci_aifsn_t;

/** Data structure of WMM AC parameters  */
typedef struct _wmm_ac_parameters_t {
	wmm_aci_aifsn_t aci_aifsn; /**< AciAifSn */
	wmm_ecw_t ecw; /**< Ecw */
	t_u16 tx_op_limit; /**< Tx op limit */
} wmm_ac_parameters_t, *pwmm_ac_parameters_t;

/** mlan_deauth_param */
typedef struct _mlan_deauth_param {
	/** STA mac addr */
	t_u8 mac_addr[MLAN_MAC_ADDR_LENGTH];
	/** deauth reason */
	t_u16 reason_code;
} mlan_deauth_param;

#ifdef UAP_SUPPORT
/** UAP FLAG: Host based */
#define UAP_FLAG_HOST_BASED MBIT(0)
/** UAP FLAG: Host mlme */
#define UAP_FLAG_HOST_MLME MBIT(1)

/** Maximum packet forward control value */
#define MAX_PKT_FWD_CTRL 15
/** Maximum BEACON period */
#define MAX_BEACON_PERIOD 4000
/** Minimum BEACON period */
#define MIN_BEACON_PERIOD 50
/** Maximum DTIM period */
#define MAX_DTIM_PERIOD 100
/** Minimum DTIM period */
#define MIN_DTIM_PERIOD 1
/** Maximum TX Power Limit */
#define MAX_TX_POWER 20
/** Minimum TX Power Limit */
#define MIN_TX_POWER 0
/** MAX station count */
#define MAX_STA_COUNT 64
/** Maximum RTS threshold */
#define MAX_RTS_THRESHOLD 2347
/** Maximum fragmentation threshold */
#define MAX_FRAG_THRESHOLD 2346
/** Minimum fragmentation threshold */
#define MIN_FRAG_THRESHOLD 256
/** data rate 54 M */
#define DATA_RATE_54M 108
/** Maximum value of bcast_ssid_ctl */
#define MAX_BCAST_SSID_CTL 2
/** antenna A */
#define ANTENNA_MODE_A 0
/** antenna B */
#define ANTENNA_MODE_B 1
/** transmit antenna */
#define TX_ANTENNA 1
/** receive antenna */
#define RX_ANTENNA 0
/** Maximum stage out time */
#define MAX_STAGE_OUT_TIME 864000
/** Minimum stage out time */
#define MIN_STAGE_OUT_TIME 50
/** Maximum Retry Limit */
#define MAX_RETRY_LIMIT 14

/** Maximum group key timer in seconds */
#define MAX_GRP_TIMER 86400

/** Maximum value of 4 byte configuration */
#define MAX_VALID_DWORD 0x7FFFFFFF	/*  (1 << 31) - 1 */

/** default UAP BAND 2.4G */
#define DEFAULT_UAP_BAND 0
/** default UAP channel 6 */
#define DEFAULT_UAP_CHANNEL 6

/** Maximum data rates */
#define MAX_DATA_RATES 14

/** auto data rate */
#define DATA_RATE_AUTO 0

/**filter mode: disable */
#define MAC_FILTER_MODE_DISABLE 0
/**filter mode: block mac address */
#define MAC_FILTER_MODE_ALLOW_MAC 1
/**filter mode: block mac address */
#define MAC_FILTER_MODE_BLOCK_MAC 2
/** Maximum mac filter num */
#define MAX_MAC_FILTER_NUM 64

/* Bitmap for protocol to use */
/** No security */
#define PROTOCOL_NO_SECURITY 0x01
/** Static WEP */
#define PROTOCOL_STATIC_WEP 0x02
/** WPA */
#define PROTOCOL_WPA 0x08
/** WPA2 */
#define PROTOCOL_WPA2 0x20
/** WP2 Mixed */
#define PROTOCOL_WPA2_MIXED 0x28
/** EAP */
#define PROTOCOL_EAP 0x40
/** WAPI */
#define PROTOCOL_WAPI 0x80
/** WPA3 SAE */
#define PROTOCOL_WPA3_SAE 0x100

/** Key_mgmt_psk */
#define KEY_MGMT_NONE 0x04
/** Key_mgmt_none */
#define KEY_MGMT_PSK 0x02
/** Key_mgmt_eap  */
#define KEY_MGMT_EAP 0x01
/** Key_mgmt_psk_sha256 */
#define KEY_MGMT_PSK_SHA256 0x100
/** Key_mgmt_sae */
#define KEY_MGMT_SAE 0x400
/** Key_mgmt_owe */
#define KEY_MGMT_OWE 0x200

/** TKIP */
#define CIPHER_TKIP 0x04
/** AES CCMP */
#define CIPHER_AES_CCMP 0x08

/** Valid cipher bitmap */
#define VALID_CIPHER_BITMAP 0x0c

/** Packet forwarding to be done by FW or host */
#define PKT_FWD_FW_BIT 0x01
/** Intra-BSS broadcast packet forwarding allow bit */
#define PKT_FWD_INTRA_BCAST 0x02
/** Intra-BSS unicast packet forwarding allow bit */
#define PKT_FWD_INTRA_UCAST 0x04
/** Inter-BSS unicast packet forwarding allow bit */
#define PKT_FWD_INTER_UCAST 0x08
/** Intra-BSS unicast packet */
#define PKT_INTRA_UCAST 0x01
/** Inter-BSS unicast packet */
#define PKT_INTER_UCAST 0x02
/** Enable Host PKT forwarding */
#define PKT_FWD_ENABLE_BIT 0x01

/** Channel List Entry */
typedef struct _channel_list {
	/** Channel Number */
	t_u8 chan_number;
	/** Band Config */
	Band_Config_t bandcfg;
} scan_chan_list;

/** mac_filter data structure */
typedef struct _mac_filter {
	/** mac filter mode */
	t_u16 filter_mode;
	/** mac adress count */
	t_u16 mac_count;
	/** mac address list */
	mlan_802_11_mac_addr mac_list[MAX_MAC_FILTER_NUM];
} mac_filter;

/** wpa parameter */
typedef struct _wpa_param {
	/** Pairwise cipher WPA */
	t_u8 pairwise_cipher_wpa;
	/** Pairwise cipher WPA2 */
	t_u8 pairwise_cipher_wpa2;
	/** group cipher */
	t_u8 group_cipher;
	/** RSN replay protection */
	t_u8 rsn_protection;
	/** passphrase length */
	t_u32 length;
	/** passphrase */
	t_u8 passphrase[64];
	/**group key rekey time in seconds */
	t_u32 gk_rekey_time;
} wpa_param;

/** wep key */
typedef struct _wep_key {
	/** key index 0-3 */
	t_u8 key_index;
	/** is default */
	t_u8 is_default;
	/** length */
	t_u16 length;
	/** key data */
	t_u8 key[26];
} wep_key;

/** wep param */
typedef struct _wep_param {
	/** key 0 */
	wep_key key0;
	/** key 1 */
	wep_key key1;
	/** key 2 */
	wep_key key2;
	/** key 3 */
	wep_key key3;
} wep_param;

/** Data structure of WMM QoS information */
typedef struct _wmm_qos_info_t {
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
} wmm_qos_info_t, *pwmm_qos_info_t;

/** Data structure of WMM parameter IE  */
typedef struct _wmm_parameter_t {
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
	wmm_ac_parameters_t ac_params[MAX_AC_QUEUES];
} wmm_parameter_t, *pwmm_parameter_t;

/** MAX BG channel */
#define MAX_BG_CHANNEL 14
/** mlan_bss_param
 * Note: For each entry you must enter an invalid value
 * in the MOAL function woal_set_sys_config_invalid_data().
 * Otherwise for a valid data an unwanted TLV will be
 * added to that command.
 */
typedef struct _mlan_uap_bss_param {
	/** AP mac addr */
	mlan_802_11_mac_addr mac_addr;
	/** SSID */
	mlan_802_11_ssid ssid;
	/** Broadcast ssid control */
	t_u8 bcast_ssid_ctl;
	/** Radio control: on/off */
	t_u8 radio_ctl;
	/** dtim period */
	t_u8 dtim_period;
	/** beacon period */
	t_u16 beacon_period;
	/** rates */
	t_u8 rates[MAX_DATA_RATES];
	/** Tx data rate */
	t_u16 tx_data_rate;
	/** Tx beacon rate */
	t_u16 tx_beacon_rate;
	/** multicast/broadcast data rate */
	t_u16 mcbc_data_rate;
	/** Tx power level in dBm */
	t_u8 tx_power_level;
	/** Tx antenna */
	t_u8 tx_antenna;
	/** Rx antenna */
	t_u8 rx_antenna;
	/** packet forward control */
	t_u8 pkt_forward_ctl;
	/** max station count */
	t_u16 max_sta_count;
	/** mac filter */
	mac_filter filter;
	/** station ageout timer in unit of 100ms  */
	t_u32 sta_ageout_timer;
	/** PS station ageout timer in unit of 100ms  */
	t_u32 ps_sta_ageout_timer;
	/** RTS threshold */
	t_u16 rts_threshold;
	/** fragmentation threshold */
	t_u16 frag_threshold;
	/**  retry_limit */
	t_u16 retry_limit;
	/**  pairwise update timeout in milliseconds */
	t_u32 pairwise_update_timeout;
	/** pairwise handshake retries */
	t_u32 pwk_retries;
	/**  groupwise update timeout in milliseconds */
	t_u32 groupwise_update_timeout;
	/** groupwise handshake retries */
	t_u32 gwk_retries;
	/** preamble type */
	t_u8 preamble_type;
	/** band cfg */
	Band_Config_t bandcfg;
	/** channel */
	t_u8 channel;
	/** auth mode */
	t_u16 auth_mode;
	/** encryption protocol */
	t_u16 protocol;
	/** key managment type */
	t_u16 key_mgmt;
	/** wep param */
	wep_param wep_cfg;
	/** wpa param */
	wpa_param wpa_cfg;
	/** Mgmt IE passthru mask */
	t_u32 mgmt_ie_passthru_mask;
	/*
	 * 11n HT Cap  HTCap_t  ht_cap
	 */
	/** HT Capabilities Info field */
	t_u16 ht_cap_info;
	/** A-MPDU Parameters field */
	t_u8 ampdu_param;
	/** Supported MCS Set field */
	t_u8 supported_mcs_set[16];
	/** HT Extended Capabilities field */
	t_u16 ht_ext_cap;
	/** Transmit Beamforming Capabilities field */
	t_u32 tx_bf_cap;
	/** Antenna Selection Capability field */
	t_u8 asel;
	/** Enable 2040 Coex */
	t_u8 enable_2040coex;
	/** key management operation */
	t_u16 key_mgmt_operation;
	/** BSS status */
	t_u16 bss_status;
#ifdef WIFI_DIRECT_SUPPORT
	/* pre shared key */
	t_u8 psk[MLAN_MAX_KEY_LENGTH];
#endif				/* WIFI_DIRECT_SUPPORT */
	/** Number of channels in scan_channel_list */
	t_u32 num_of_chan;
	/** scan channel list in ACS mode */
	scan_chan_list chan_list[MLAN_MAX_CHANNEL];
	/** Wmm parameters */
	wmm_parameter_t wmm_para;

	/** uap host based config */
	t_u32 uap_host_based_config;
} mlan_uap_bss_param, *pmlan_uap_bss_param;

/** mlan_uap_scan_channels */
typedef struct _mlan_uap_scan_channels {
	/** flag for remove nop channel*/
	t_u8 remove_nop_channel;
	/** num of removed channel */
	t_u8 num_remvoed_channel;
	/** Number of channels in scan_channel_list */
	t_u32 num_of_chan;
	/** scan channel list in ACS mode */
	scan_chan_list chan_list[MLAN_MAX_CHANNEL];
} mlan_uap_scan_channels;

/** mlan_chan_switch_param */
typedef struct _mlan_action_chan_switch {
	/** mode*/
	t_u8 mode;
	/** switch mode*/
	t_u8 chan_switch_mode;
	/** oper class*/
	t_u8 new_oper_class;
    /** new channel */
	t_u8 new_channel_num;
    /** chan_switch_count */
	t_u8 chan_switch_count;
} mlan_action_chan_switch;

/** mlan_uap_oper_ctrl */
typedef struct _mlan_uap_oper_ctrl {
	/** control value
	 *  0: do nothing,
	 *  2: uap stops and restarts automaticaly
	 */
	t_u16 ctrl_value;
	/** channel opt
	 *  1: uap restart on default 2.4G/channel 6
	 *  2: uap restart on the band/channel configured by driver previously
	 *  3: uap restart on the band/channel specified by band_cfg and channel
	 */
	t_u16 chan_opt;
	/** band cfg   0
	 *  0: 20Mhz  2: 40 Mhz  3: 80Mhz
	 */
	t_u8 band_cfg;
	/** channel */
	t_u8 channel;
} mlan_uap_oper_ctrl;

/** mlan_uap_acs_scan */
typedef struct _mlan_uap_acs_scan {
	/** band */
	Band_Config_t bandcfg;
	/** channel */
	t_u8 chan;
} mlan_uap_acs_scan;

/** station is authorized (802.1X) */
#define STA_FLAG_AUTHORIZED MBIT(1)
/** Station is capable of receiving frames with short barker preamble */
#define STA_FLAG_SHORT_PREAMBLE MBIT(2)
/** station is WME/QoS capable */
#define STA_FLAG_WME MBIT(3)
/** station uses management frame protection */
#define STA_FLAG_MFP MBIT(4)
/** station is authenticated */
#define STA_FLAG_AUTHENTICATED MBIT(5)
/** station is a TDLS peer */
#define STA_FLAG_TDLS_PEER MBIT(6)
/** station is associated */
#define STA_FLAG_ASSOCIATED MBIT(7)
/** mlan_ds_sta_info */
typedef struct _mlan_ds_sta_info {
	/** aid */
	t_u16 aid;
	/** peer_mac */
	t_u8 peer_mac[MLAN_MAC_ADDR_LENGTH];
	/** Listen Interval */
	int listen_interval;
	/** Capability Info */
	t_u16 cap_info;
	/** station flag */
	t_u32 sta_flags;
	/** tlv len */
	t_u16 tlv_len;
	/** tlv start */
	t_u8 tlv[];
} mlan_ds_sta_info;
#endif

#ifdef WIFI_DIRECT_SUPPORT
/** mode: disable wifi direct */
#define WIFI_DIRECT_MODE_DISABLE 0
/** mode: listen */
#define WIFI_DIRECT_MODE_LISTEN 1
/** mode: GO */
#define WIFI_DIRECT_MODE_GO 2
/** mode: client */
#define WIFI_DIRECT_MODE_CLIENT 3
/** mode: find */
#define WIFI_DIRECT_MODE_FIND 4
/** mode: stop find */
#define WIFI_DIRECT_MODE_STOP_FIND 5
#endif

/** Type definition of mlan_ds_bss for MLAN_IOCTL_BSS */
typedef struct _mlan_ds_bss {
	/** Sub-command */
	t_u32 sub_command;
	/** BSS parameter */
	union {
		/** SSID-BSSID for MLAN_OID_BSS_START */
		mlan_ssid_bssid ssid_bssid;
		/** BSSID for MLAN_OID_BSS_STOP */
		mlan_802_11_mac_addr bssid;
		/** BSS mode for MLAN_OID_BSS_MODE */
		t_u32 bss_mode;
		/** BSS channel/frequency for MLAN_OID_BSS_CHANNEL */
		chan_freq bss_chan;
		/** BSS channel list for MLAN_OID_BSS_CHANNEL_LIST */
		mlan_chan_list chanlist;
		/** MAC address for MLAN_OID_BSS_MAC_ADDR */
		mlan_802_11_mac_addr mac_addr;
		/** Multicast list for MLAN_OID_BSS_MULTICAST_LIST */
		mlan_multicast_list multicast_list;
		/** Beacon interval for MLAN_OID_IBSS_BCN_INTERVAL */
		t_u32 bcn_interval;
		/** ATIM window for MLAN_OID_IBSS_ATIM_WINDOW */
		t_u32 atim_window;
		/** deauth param for MLAN_OID_BSS_STOP & MLAN_OID_UAP_DEAUTH_STA
		 */
		mlan_deauth_param deauth_param;
#ifdef UAP_SUPPORT
		/** host based flag for MLAN_OID_BSS_START */
		t_u8 host_based;
		/** BSS param for AP mode for MLAN_OID_UAP_BSS_CONFIG */
		mlan_uap_bss_param bss_config;
		/** AP Wmm parameters for MLAN_OID_UAP_CFG_WMM_PARAM */
		wmm_parameter_t ap_wmm_para;
		/** ap scan channels for MLAN_OID_UAP_SCAN_CHANNELS*/
		mlan_uap_scan_channels ap_scan_channels;
	/** channel switch for MLAN_OID_UAP_CHAN_SWITCH */
		mlan_action_chan_switch chanswitch;
		/** ap channel for MLAN_OID_UAP_CHANNEL*/
		chan_band_info ap_channel;
		/** ap operation control for MLAN_OID_UAP_OPER_CTRL*/
		mlan_uap_oper_ctrl ap_oper_ctrl;
		/** AP acs scan 	    MLAN_OID_UAP_ACS_SCAN */
		mlan_uap_acs_scan ap_acs_scan;
#endif
#if defined(STA_SUPPORT) && defined(UAP_SUPPORT)
		/** BSS role for MLAN_OID_BSS_ROLE */
		t_u8 bss_role;
#endif
#ifdef WIFI_DIRECT_SUPPORT
		/** wifi direct mode for MLAN_OID_WIFI_DIRECT_MODE */
		t_u16 wfd_mode;
#endif
#ifdef STA_SUPPORT
		/** Listen interval for MLAN_OID_BSS_LISTEN_INTERVAL */
		t_u16 listen_interval;
		/** STA channel info for MLAN_OID_BSS_CHAN_INFO */
		chan_band_info sta_channel;
#endif
#ifdef UAP_SUPPORT
		/** STA info for MLAN_OID_UAP_ADD_STATION */
		mlan_ds_sta_info sta_info;
#endif
	} param;
} mlan_ds_bss, *pmlan_ds_bss;

/* OTP Region info */
typedef MLAN_PACK_START struct _otp_region_info {
	t_u8 country_code[2];
	t_u8 region_code;
	t_u8 environment;
	t_u8 force_reg:1;
	t_u8 reserved:7;
	t_u8 dfs_region;
} MLAN_PACK_END otp_region_info_t;

/** Type definition of mlan_ds_custom_reg_domain */
typedef struct _mlan_ds_custom_reg_domain {
	otp_region_info_t region;
	/** num of 2g channels in custom_reg_domain */
	t_u8 num_bg_chan;
	/** num of 5g channels in custom_reg_domain */
	t_u8 num_a_chan;
	/** cfp table */
	chan_freq_power_t cfp_tbl[];
} mlan_ds_custom_reg_domain;
/*-----------------------------------------------------------------*/
/** Radio Control Group */
/*-----------------------------------------------------------------*/
/** Enumeration for band */
enum _mlan_band_def {
	BAND_B = 1,
	BAND_G = 2,
	BAND_A = 4,
	BAND_GN = 8,
	BAND_AN = 16,
	BAND_GAC = 32,
	BAND_AAC = 64,
	BAND_GAX = 256,
	BAND_AAX = 512,

};

/** Channel bandwidth */
#define CHANNEL_BW_20MHZ 0
#define CHANNEL_BW_40MHZ_ABOVE 1
#define CHANNEL_BW_40MHZ_BELOW 3
/** secondary channel is 80Mhz bandwidth for 11ac */
#define CHANNEL_BW_80MHZ 4
#define CHANNEL_BW_160MHZ 5

/** RF antenna selection */
#define RF_ANTENNA_MASK(n) ((1 << (n)) - 1)
/** RF antenna auto select */
#define RF_ANTENNA_AUTO 0xFFFF

/** Type definition of mlan_ds_band_cfg for MLAN_OID_BAND_CFG */
typedef struct _mlan_ds_band_cfg {
	/** Infra band */
	t_u32 config_bands;
	/** Ad-hoc start band */
	t_u32 adhoc_start_band;
	/** Ad-hoc start channel */
	t_u32 adhoc_channel;
	/** fw supported band */
	t_u32 fw_bands;
} mlan_ds_band_cfg;

/** Type definition of mlan_ds_ant_cfg for MLAN_OID_ANT_CFG */
typedef struct _mlan_ds_ant_cfg {
	/** Tx antenna mode */
	t_u32 tx_antenna;
	/** Rx antenna mode */
	t_u32 rx_antenna;
} mlan_ds_ant_cfg, *pmlan_ds_ant_cfg;
/** Type definition of mlan_ds_mimo_switch for MLAN_OID_MIMO_SWITCH */
typedef struct _mlan_ds_mimo_switch {
	/** Tx antenna mode */
	t_u8 txpath_antmode;
	/** Rx antenna mode */
	t_u8 rxpath_antmode;
} mlan_ds_mimo_switch, *pmlan_ds_mimo_switch;
/** Type definition of mlan_ds_ant_cfg_1x1 for MLAN_OID_ANT_CFG */
typedef struct _mlan_ds_ant_cfg_1x1 {
	/** Antenna mode */
	t_u32 antenna;
	/** Evaluate time */
	t_u16 evaluate_time;
	/** Current antenna */
	t_u16 current_antenna;
} mlan_ds_ant_cfg_1x1, *pmlan_ds_ant_cfg_1x1;

/** Type definition of mlan_ds_remain_chan for MLAN_OID_REMAIN_CHAN_CFG */
typedef struct _mlan_ds_remain_chan {
	/** remove flag */
	t_u16 remove;
	/** status */
	t_u8 status;
	/** Band cfg */
	Band_Config_t bandcfg;
	/** channel */
	t_u8 channel;
	/** remain time: Unit ms*/
	t_u32 remain_period;
} mlan_ds_remain_chan, *pmlan_ds_remain_chan;

/** Type definition of mlan_ds_radio_cfg for MLAN_IOCTL_RADIO_CFG */
typedef struct _mlan_ds_radio_cfg {
	/** Sub-command */
	t_u32 sub_command;
	/** Radio control parameter */
	union {
		/** Radio on/off for MLAN_OID_RADIO_CTRL */
		t_u32 radio_on_off;
		/** Band info for MLAN_OID_BAND_CFG */
		mlan_ds_band_cfg band_cfg;
		/** Antenna info for MLAN_OID_ANT_CFG */
		mlan_ds_ant_cfg ant_cfg;
		/** Antenna mode for MLAN_OID_MIMO_SWITCH */
		mlan_ds_mimo_switch mimo_switch_cfg;
		/** Antenna info for MLAN_OID_ANT_CFG */
		mlan_ds_ant_cfg_1x1 ant_cfg_1x1;
		/** remain on channel for MLAN_OID_REMAIN_CHAN_CFG */
		mlan_ds_remain_chan remain_chan;
	} param;
} mlan_ds_radio_cfg, *pmlan_ds_radio_cfg;

enum COALESCE_OPERATION {
	RECV_FILTER_MATCH_TYPE_EQ = 0x80,
	RECV_FILTER_MATCH_TYPE_NE,
};

enum COALESCE_PACKET_TYPE {
	PACKET_TYPE_UNICAST = 1,
	PACKET_TYPE_MULTICAST = 2,
	PACKET_TYPE_BROADCAST = 3
};

#define COALESCE_MAX_RULES 8
#define COALESCE_MAX_BYTESEQ 4	/* non-adjustable */
#define COALESCE_MAX_FILTERS 4
#define MAX_COALESCING_DELAY 100	/* in msecs */
#define MAX_PATTERN_LEN 20
#define MAX_OFFSET_LEN 100

struct filt_field_param {
	t_u8 operation;
	t_u8 operand_len;
	t_u16 offset;
	t_u8 operand_byte_stream[COALESCE_MAX_BYTESEQ];
};

struct coalesce_rule {
	t_u16 max_coalescing_delay;
	t_u8 num_of_fields;
	t_u8 pkt_type;
	struct filt_field_param params[COALESCE_MAX_FILTERS];
};

typedef struct _mlan_ds_coalesce_cfg {
	t_u16 num_of_rules;
	struct coalesce_rule rule[COALESCE_MAX_RULES];
} mlan_ds_coalesce_cfg;

/*-----------------------------------------------------------------*/
/** SNMP MIB Group */
/*-----------------------------------------------------------------*/
/** Type definition of mlan_ds_snmp_mib for MLAN_IOCTL_SNMP_MIB */
typedef struct _mlan_ds_snmp_mib {
	/** Sub-command */
	t_u32 sub_command;
	/** SNMP MIB parameter */
	union {
		/** RTS threshold for MLAN_OID_SNMP_MIB_RTS_THRESHOLD */
		t_u32 rts_threshold;
		/** Fragment threshold for MLAN_OID_SNMP_MIB_FRAG_THRESHOLD */
		t_u32 frag_threshold;
		/** Retry count for MLAN_OID_SNMP_MIB_RETRY_COUNT */
		t_u32 retry_count;
		/** OID value for MLAN_OID_SNMP_MIB_DOT11D/H */
		t_u32 oid_value;
		/** DTIM period for MLAN_OID_SNMP_MIB_DTIM_PERIOD */
		t_u32 dtim_period;
		/** Singal_ext Enable for MLAN_OID_SNMP_MIB_SIGNALEXT_ENABLE */
		t_u8 signalext_enable;
		/** Control deauth when uap switch channel */
		t_u8 deauthctrl;
	} param;
} mlan_ds_snmp_mib, *pmlan_ds_snmp_mib;

/*-----------------------------------------------------------------*/
/** Status Information Group */
/*-----------------------------------------------------------------*/
/** Enumeration for ad-hoc status */
enum _mlan_adhoc_status {
	ADHOC_IDLE,
	ADHOC_STARTED,
	ADHOC_JOINED,
	ADHOC_COALESCED,
	ADHOC_STARTING
};

typedef struct _mlan_ds_get_stats_org {
	/** Statistics counter */
	/** Multicast transmitted frame count */
	t_u32 mcast_tx_frame;
	/** Failure count */
	t_u32 failed;
	/** Retry count */
	t_u32 retry;
	/** Multi entry count */
	t_u32 multi_retry;
	/** Duplicate frame count */
	t_u32 frame_dup;
	/** RTS success count */
	t_u32 rts_success;
	/** RTS failure count */
	t_u32 rts_failure;
	/** Ack failure count */
	t_u32 ack_failure;
	/** Rx fragmentation count */
	t_u32 rx_frag;
	/** Multicast Tx frame count */
	t_u32 mcast_rx_frame;
	/** FCS error count */
	t_u32 fcs_error;
	/** Tx frame count */
	t_u32 tx_frame;
	/** WEP ICV error count */
	t_u32 wep_icv_error[4];
	/** beacon recv count */
	t_u32 bcn_rcv_cnt;
	/** beacon miss count */
	t_u32 bcn_miss_cnt;
	/** received amsdu count*/
	t_u32 amsdu_rx_cnt;
	/** received msdu count in amsdu*/
	t_u32 msdu_in_rx_amsdu_cnt;
	/** tx amsdu count*/
	t_u32 amsdu_tx_cnt;
	/** tx msdu count in amsdu*/
	t_u32 msdu_in_tx_amsdu_cnt;
} mlan_ds_get_stats_org;

/** Type definition of mlan_ds_get_stats for MLAN_OID_GET_STATS */
typedef struct _mlan_ds_get_stats {
	/** Statistics counter */
	/** Multicast transmitted frame count */
	t_u32 mcast_tx_frame;
	/** Failure count */
	t_u32 failed;
	/** Retry count */
	t_u32 retry;
	/** Multi entry count */
	t_u32 multi_retry;
	/** Duplicate frame count */
	t_u32 frame_dup;
	/** RTS success count */
	t_u32 rts_success;
	/** RTS failure count */
	t_u32 rts_failure;
	/** Ack failure count */
	t_u32 ack_failure;
	/** Rx fragmentation count */
	t_u32 rx_frag;
	/** Multicast Tx frame count */
	t_u32 mcast_rx_frame;
	/** FCS error count */
	t_u32 fcs_error;
	/** Tx frame count */
	t_u32 tx_frame;
	/** WEP ICV error count */
	t_u32 wep_icv_error[4];
	/** beacon recv count */
	t_u32 bcn_rcv_cnt;
	/** beacon miss count */
	t_u32 bcn_miss_cnt;
	/** received amsdu count*/
	t_u32 amsdu_rx_cnt;
	/** received msdu count in amsdu*/
	t_u32 msdu_in_rx_amsdu_cnt;
	/** tx amsdu count*/
	t_u32 amsdu_tx_cnt;
	/** tx msdu count in amsdu*/
	t_u32 msdu_in_tx_amsdu_cnt;

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
} mlan_ds_get_stats, *pmlan_ds_get_stats;

/** Type definition of mlan_ds_uap_stats for MLAN_OID_GET_STATS */
typedef struct _mlan_ds_uap_stats {
	/** tkip mic failures */
	t_u32 tkip_mic_failures;
	/** ccmp decrypt errors */
	t_u32 ccmp_decrypt_errors;
	/** wep undecryptable count */
	t_u32 wep_undecryptable_count;
	/** wep icv error count */
	t_u32 wep_icv_error_count;
	/** decrypt failure count */
	t_u32 decrypt_failure_count;
	/** dot11 multicast tx count */
	t_u32 mcast_tx_count;
	/** dot11 failed count */
	t_u32 failed_count;
	/** dot11 retry count */
	t_u32 retry_count;
	/** dot11 multi retry count */
	t_u32 multi_retry_count;
	/** dot11 frame duplicate count */
	t_u32 frame_dup_count;
	/** dot11 rts success count */
	t_u32 rts_success_count;
	/** dot11 rts failure count */
	t_u32 rts_failure_count;
	/** dot11 ack failure count */
	t_u32 ack_failure_count;
	/** dot11 rx ragment count */
	t_u32 rx_fragment_count;
	/** dot11 mcast rx frame count */
	t_u32 mcast_rx_frame_count;
	/** dot11 fcs error count */
	t_u32 fcs_error_count;
	/** dot11 tx frame count */
	t_u32 tx_frame_count;
	/** dot11 rsna tkip cm invoked */
	t_u32 rsna_tkip_cm_invoked;
	/** dot11 rsna 4way handshake failures */
	t_u32 rsna_4way_hshk_failures;
} mlan_ds_uap_stats, *pmlan_ds_uap_stats;

/** Mask of last beacon RSSI */
#define BCN_RSSI_LAST_MASK 0x00000001
/** Mask of average beacon RSSI */
#define BCN_RSSI_AVG_MASK 0x00000002
/** Mask of last data RSSI */
#define DATA_RSSI_LAST_MASK 0x00000004
/** Mask of average data RSSI */
#define DATA_RSSI_AVG_MASK 0x00000008
/** Mask of last beacon SNR */
#define BCN_SNR_LAST_MASK 0x00000010
/** Mask of average beacon SNR */
#define BCN_SNR_AVG_MASK 0x00000020
/** Mask of last data SNR */
#define DATA_SNR_LAST_MASK 0x00000040
/** Mask of average data SNR */
#define DATA_SNR_AVG_MASK 0x00000080
/** Mask of last beacon NF */
#define BCN_NF_LAST_MASK 0x00000100
/** Mask of average beacon NF */
#define BCN_NF_AVG_MASK 0x00000200
/** Mask of last data NF */
#define DATA_NF_LAST_MASK 0x00000400
/** Mask of average data NF */
#define DATA_NF_AVG_MASK 0x00000800
/** Mask of all RSSI_INFO */
#define ALL_RSSI_INFO_MASK 0x00000fff
#define MAX_PATH_NUM 3
/** path A */
#define PATH_A 0x01
/** path B */
#define PATH_B 0x02
/** path AB */
#define PATH_AB 0x03
/** ALL the path */
#define PATH_ALL 0
/** Type definition of mlan_ds_get_signal for MLAN_OID_GET_SIGNAL */
typedef struct _mlan_ds_get_signal {
	/** Selector of get operation */
	/*
	 * Bit0:  Last Beacon RSSI,  Bit1:  Average Beacon RSSI,
	 * Bit2:  Last Data RSSI,    Bit3:  Average Data RSSI,
	 * Bit4:  Last Beacon SNR,   Bit5:  Average Beacon SNR,
	 * Bit6:  Last Data SNR,     Bit7:  Average Data SNR,
	 * Bit8:  Last Beacon NF,    Bit9:  Average Beacon NF,
	 * Bit10: Last Data NF,      Bit11: Average Data NF
	 *
	 * Bit0: PATH A
	 * Bit1: PATH B
	 */
	t_u16 selector;

	/** RSSI */
	/** RSSI of last beacon */
	t_s16 bcn_rssi_last;
	/** RSSI of beacon average */
	t_s16 bcn_rssi_avg;
	/** RSSI of last data packet */
	t_s16 data_rssi_last;
	/** RSSI of data packet average */
	t_s16 data_rssi_avg;

	/** SNR */
	/** SNR of last beacon */
	t_s16 bcn_snr_last;
	/** SNR of beacon average */
	t_s16 bcn_snr_avg;
	/** SNR of last data packet */
	t_s16 data_snr_last;
	/** SNR of data packet average */
	t_s16 data_snr_avg;

	/** NF */
	/** NF of last beacon */
	t_s16 bcn_nf_last;
	/** NF of beacon average */
	t_s16 bcn_nf_avg;
	/** NF of last data packet */
	t_s16 data_nf_last;
	/** NF of data packet average */
	t_s16 data_nf_avg;
} mlan_ds_get_signal, *pmlan_ds_get_signal;

/** bit for 2.4 G antenna diversity */
#define ANT_DIVERSITY_2G MBIT(3)
/** bit for 5 G antenna diversity */
#define ANT_DIVERSITY_5G MBIT(7)

/** mlan_fw_info data structure for MLAN_OID_GET_FW_INFO */
typedef struct _mlan_fw_info {
	/** Firmware version */
	t_u32 fw_ver;
	/** Firmware Hotfix version */
	t_u8 hotfix_version;
	/** MAC address */
	mlan_802_11_mac_addr mac_addr;
	/** 802.11n device capabilities */
	t_u32 hw_dot_11n_dev_cap;
	/** Device support for MIMO abstraction of MCSs */
	t_u8 hw_dev_mcs_support;
	/** user's MCS setting */
	t_u8 usr_dev_mcs_support;
	/** 802.11ac device capabilities */
	t_u32 hw_dot_11ac_dev_cap;
	/** 802.11ac device Capabilities for 2.4GHz */
	t_u32 usr_dot_11ac_dev_cap_bg;
	/** 802.11ac device Capabilities for 5GHz */
	t_u32 usr_dot_11ac_dev_cap_a;
	/** length of hw he capability */
	t_u8 hw_hecap_len;
	/** 802.11ax HE capability */
	t_u8 hw_he_cap[54];
	/** length of hw 2.4G he capability */
	t_u8 hw_2g_hecap_len;
	/** 802.11ax 2.4G HE capability */
	t_u8 hw_2g_he_cap[54];
	/** 802.11ac device support for MIMO abstraction of MCSs */
	t_u32 hw_dot_11ac_mcs_support;
	/** User conf 802.11ac device support for MIMO abstraction of MCSs */
	t_u32 usr_dot_11ac_mcs_support;
	/** fw supported band */
	t_u16 fw_bands;
	/** region code */
	t_u16 region_code;
	/** force_reg */
	t_u8 force_reg;
	/** ECSA support */
	t_u8 ecsa_enable;
	/** Get log support */
	t_u8 getlog_enable;
	/** FW support for embedded supplicant */
	t_u8 fw_supplicant_support;
	/** ant info */
	t_u8 antinfo;
	/** max AP associated sta count supported by fw */
	t_u8 max_ap_assoc_sta;
	/** Bandwidth not support 80Mhz */
	t_u8 prohibit_80mhz;
	/** FW support beacon protection */
	t_u8 fw_beacon_prot;
} mlan_fw_info, *pmlan_fw_info;

/** Version string buffer length */
#define MLAN_MAX_VER_STR_LEN 128

/** mlan_ver_ext data structure for MLAN_OID_GET_VER_EXT */
typedef struct _mlan_ver_ext {
	/** Selected version string */
	t_u32 version_str_sel;
	/** Version string */
	char version_str[MLAN_MAX_VER_STR_LEN];
} mlan_ver_ext, *pmlan_ver_ext;

#ifdef BIG_ENDIAN_SUPPORT
/** Extended Capabilities Data */
typedef struct MLAN_PACK_START _ExtCap_t {
	/** Extended Capabilities value */
	t_u8 rsvdBit87:1;	/* bit 87 */
	t_u8 rsvdBit86:1;	/* bit 86 */
	t_u8 rsvdBit85:1;	/* bit 85 */
	t_u8 beacon_prot:1;	/* bit 84 */
	t_u8 rsvdBit83:1;	/* bit 83 */
	t_u8 rsvdBit82:1;	/* bit 82 */
	t_u8 rsvdBit81:1;	/* bit 81 */
	t_u8 rsvdBit80:1;	/* bit 80 */
	t_u8 rsvdBit79:1;	/* bit 79 */
	t_u8 TWTResp:1;		/* bit 78 */
	t_u8 TWTReq:1;		/* bit 77 */
	t_u8 rsvdBit76:1;	/* bit 76 */
	t_u8 rsvdBit75:1;	/* bit 75 */
	t_u8 rsvdBit74:1;	/* bit 74 */
	t_u8 rsvdBit73:1;	/* bit 73 */
	t_u8 FILS:1;		/* bit 72 */
	t_u8 FTMI:1;		/* bit 71 */
	t_u8 FTMR:1;		/* bit 70 */
	t_u8 CAQ:1;		/* bit 69 */
	t_u8 rsvdBit68:1;	/* bit 68 */
	t_u8 NCC:1;		/* bit 67 */
	t_u8 rsvdBit66:1;	/* bit 66 */
	t_u8 chanSchedMgnt:1;	/* bit 65 */
	t_u8 MaxAMSDU1:1;	/* bit 64 */
	t_u8 MaxAMSDU0:1;	/* bit 63 */
	t_u8 OperModeNtf:1;	/* bit 62 */
	t_u8 TDLSWildBandwidth:1;	/* bit 61 */
	t_u8 rsvdBit60:1;	/* bit 60 */
	t_u8 rsvdBit59:1;	/* bit 59 */
	t_u8 rsvdBit58:1;	/* bit 58 */
	t_u8 rsvdBit57:1;	/* bit 57 */
	t_u8 rsvdBit56:1;	/* bit 56 */
	t_u8 rsvdBit55:1;	/* bit 55 */
	t_u8 rsvdBit54:1;	/* bit 54 */
	t_u8 rsvdBit53:1;	/* bit 53 */
	t_u8 rsvdBit52:1;	/* bit 52 */
	t_u8 rsvdBit51:1;	/* bit 51 */
	t_u8 rsvdBit50:1;	/* bit 50 */
	t_u8 rsvdBit49:1;	/* bit 49 */
	t_u8 rsvdBit48:1;	/* bit 48 */
	t_u8 rsvdBit47:1;	/* bit 47 */
	t_u8 rsvdBit46:1;	/* bit 46 */
	t_u8 rsvdBit45:1;	/* bit 45 */
	t_u8 rsvdBit44:1;	/* bit 44 */
	t_u8 rsvdBit43:1;	/* bit 43 */
	t_u8 rsvdBit42:1;	/* bit 42 */
	t_u8 rsvdBit41:1;	/* bit 41 */
	t_u8 rsvdBit40:1;	/* bit 40 */
	t_u8 TDLSChlSwitchProhib:1;	/* bit 39 */
	t_u8 TDLSProhibited:1;	/* bit 38 */
	t_u8 TDLSSupport:1;	/* bit 37 */
	t_u8 MSGCF_Capa:1;	/* bit 36 */
	t_u8 Reserved35:1;	/* bit 35 */
	t_u8 SSPN_Interface:1;	/* bit 34 */
	t_u8 EBR:1;		/* bit 33 */
	t_u8 Qos_Map:1;		/* bit 32 */
	t_u8 Interworking:1;	/* bit 31 */
	t_u8 TDLSChannelSwitching:1;	/* bit 30 */
	t_u8 TDLSPeerPSMSupport:1;	/* bit 29 */
	t_u8 TDLSPeerUAPSDSupport:1;	/* bit 28 */
	t_u8 UTC:1;		/* bit 27 */
	t_u8 DMS:1;		/* bit 26 */
	t_u8 SSID_List:1;	/* bit 25 */
	t_u8 ChannelUsage:1;	/* bit 24 */
	t_u8 TimingMeasurement:1;	/* bit 23 */
	t_u8 MultipleBSSID:1;	/* bit 22 */
	t_u8 AC_StationCount:1;	/* bit 21 */
	t_u8 QoSTrafficCap:1;	/* bit 20 */
	t_u8 BSS_Transition:1;	/* bit 19 */
	t_u8 TIM_Broadcast:1;	/* bit 18 */
	t_u8 WNM_Sleep:1;	/* bit 17 */
	t_u8 TFS:1;		/* bit 16 */
	t_u8 GeospatialLocation:1;	/* bit 15 */
	t_u8 CivicLocation:1;	/* bit 14 */
	t_u8 CollocatedIntf:1;	/* bit 13 */
	t_u8 ProxyARPService:1;	/* bit 12 */
	t_u8 FMS:1;		/* bit 11 */
	t_u8 LocationTracking:1;	/* bit 10 */
	t_u8 MulticastDiagnostics:1;	/* bit 9  */
	t_u8 Diagnostics:1;	/* bit 8  */
	t_u8 Event:1;		/* bit 7  */
	t_u8 SPSMP_Support:1;	/* bit 6 */
	t_u8 Reserved5:1;	/* bit 5 */
	t_u8 PSMP_Capable:1;	/* bit 4 */
	t_u8 RejectUnadmFrame:1;	/* bit 3 */
	t_u8 ExtChanSwitching:1;	/* bit 2 */
	t_u8 Reserved1:1;	/* bit 1 */
	t_u8 BSS_CoexistSupport:1;	/* bit 0 */
} MLAN_PACK_END ExtCap_t, *pExtCap_t;
#else
/** Extended Capabilities Data */
typedef struct MLAN_PACK_START _ExtCap_t {
	/** Extended Capabilities value */
	t_u8 BSS_CoexistSupport:1;	/* bit 0 */
	t_u8 Reserved1:1;	/* bit 1 */
	t_u8 ExtChanSwitching:1;	/* bit 2 */
	t_u8 RejectUnadmFrame:1;	/* bit 3 */
	t_u8 PSMP_Capable:1;	/* bit 4 */
	t_u8 Reserved5:1;	/* bit 5 */
	t_u8 SPSMP_Support:1;	/* bit 6 */
	t_u8 Event:1;		/* bit 7  */
	t_u8 Diagnostics:1;	/* bit 8  */
	t_u8 MulticastDiagnostics:1;	/* bit 9  */
	t_u8 LocationTracking:1;	/* bit 10 */
	t_u8 FMS:1;		/* bit 11 */
	t_u8 ProxyARPService:1;	/* bit 12 */
	t_u8 CollocatedIntf:1;	/* bit 13 */
	t_u8 CivicLocation:1;	/* bit 14 */
	t_u8 GeospatialLocation:1;	/* bit 15 */
	t_u8 TFS:1;		/* bit 16 */
	t_u8 WNM_Sleep:1;	/* bit 17 */
	t_u8 TIM_Broadcast:1;	/* bit 18 */
	t_u8 BSS_Transition:1;	/* bit 19 */
	t_u8 QoSTrafficCap:1;	/* bit 20 */
	t_u8 AC_StationCount:1;	/* bit 21 */
	t_u8 MultipleBSSID:1;	/* bit 22 */
	t_u8 TimingMeasurement:1;	/* bit 23 */
	t_u8 ChannelUsage:1;	/* bit 24 */
	t_u8 SSID_List:1;	/* bit 25 */
	t_u8 DMS:1;		/* bit 26 */
	t_u8 UTC:1;		/* bit 27 */
	t_u8 TDLSPeerUAPSDSupport:1;	/* bit 28 */
	t_u8 TDLSPeerPSMSupport:1;	/* bit 29 */
	t_u8 TDLSChannelSwitching:1;	/* bit 30 */
	t_u8 Interworking:1;	/* bit 31 */
	t_u8 Qos_Map:1;		/* bit 32 */
	t_u8 EBR:1;		/* bit 33 */
	t_u8 SSPN_Interface:1;	/* bit 34 */
	t_u8 Reserved35:1;	/* bit 35 */
	t_u8 MSGCF_Capa:1;	/* bit 36 */
	t_u8 TDLSSupport:1;	/* bit 37 */
	t_u8 TDLSProhibited:1;	/* bit 38 */
	t_u8 TDLSChlSwitchProhib:1;	/* bit 39 */
	t_u8 rsvdBit40:1;	/* bit 40 */
	t_u8 rsvdBit41:1;	/* bit 41 */
	t_u8 rsvdBit42:1;	/* bit 42 */
	t_u8 rsvdBit43:1;	/* bit 43 */
	t_u8 rsvdBit44:1;	/* bit 44 */
	t_u8 rsvdBit45:1;	/* bit 45 */
	t_u8 rsvdBit46:1;	/* bit 46 */
	t_u8 rsvdBit47:1;	/* bit 47 */
	t_u8 rsvdBit48:1;	/* bit 48 */
	t_u8 rsvdBit49:1;	/* bit 49 */
	t_u8 rsvdBit50:1;	/* bit 50 */
	t_u8 rsvdBit51:1;	/* bit 51 */
	t_u8 rsvdBit52:1;	/* bit 52 */
	t_u8 rsvdBit53:1;	/* bit 53 */
	t_u8 rsvdBit54:1;	/* bit 54 */
	t_u8 rsvdBit55:1;	/* bit 55 */
	t_u8 rsvdBit56:1;	/* bit 56 */
	t_u8 rsvdBit57:1;	/* bit 57 */
	t_u8 rsvdBit58:1;	/* bit 58 */
	t_u8 rsvdBit59:1;	/* bit 59 */
	t_u8 rsvdBit60:1;	/* bit 60 */
	t_u8 TDLSWildBandwidth:1;	/* bit 61 */
	t_u8 OperModeNtf:1;	/* bit 62 */
	t_u8 MaxAMSDU0:1;	/* bit 63 */
	t_u8 MaxAMSDU1:1;	/* bit 64 */
	t_u8 chanSchedMgnt:1;	/* bit 65 */
	t_u8 rsvdBit66:1;	/* bit 66 */
	t_u8 NCC:1;		/* bit 67 */
	t_u8 rsvdBit68:1;	/* bit 68 */
	t_u8 CAQ:1;		/* bit 69 */
	t_u8 FTMR:1;		/* bit 70 */
	t_u8 FTMI:1;		/* bit 71 */
	t_u8 FILS:1;		/* bit 72 */
	t_u8 rsvdBit73:1;	/* bit 73 */
	t_u8 rsvdBit74:1;	/* bit 74 */
	t_u8 rsvdBit75:1;	/* bit 75 */
	t_u8 rsvdBit76:1;	/* bit 76 */
	t_u8 TWTReq:1;		/* bit 77 */
	t_u8 TWTResp:1;		/* bit 78 */
	t_u8 rsvdBit79:1;	/* bit 79 */
	t_u8 rsvdBit80:1;	/* bit 80 */
	t_u8 rsvdBit81:1;	/* bit 81 */
	t_u8 rsvdBit82:1;	/* bit 82 */
	t_u8 rsvdBit83:1;	/* bit 83 */
	t_u8 beacon_prot:1;	/* bit 84 */
	t_u8 rsvdBit85:1;	/* bit 85 */
	t_u8 rsvdBit86:1;	/* bit 86 */
	t_u8 rsvdBit87:1;	/* bit 87 */
} MLAN_PACK_END ExtCap_t, *pExtCap_t;
#endif

/** ExtCap : TDLS prohibited */
#define IS_EXTCAP_TDLS_PROHIBITED(ext_cap) (ext_cap.TDLSProhibited)
/** ExtCap : TDLS channel switch prohibited */
#define IS_EXTCAP_TDLS_CHLSWITCHPROHIB(ext_cap) (ext_cap.TDLSChlSwitchProhib)

/** mlan_bss_info data structure for MLAN_OID_GET_BSS_INFO */
typedef struct _mlan_bss_info {
	/** BSS mode */
	t_u32 bss_mode;
	/** SSID */
	mlan_802_11_ssid ssid;
	/** Table index */
	t_u32 scan_table_idx;
	/** Channel */
	t_u32 bss_chan;
	/** Band */
	t_u8 bss_band;
	/** Region code */
	t_u32 region_code;
	/** Connection status */
	t_u32 media_connected;
	/** Radio on */
	t_u32 radio_on;
	/** Max power level in dBm */
	t_s32 max_power_level;
	/** Min power level in dBm */
	t_s32 min_power_level;
	/** Adhoc state */
	t_u32 adhoc_state;
	/** NF of last beacon */
	t_s32 bcn_nf_last;
	/** wep status */
	t_u32 wep_status;
	/** scan block status */
	t_u8 scan_block;
	/** Host Sleep configured flag */
	t_u32 is_hs_configured;
	/** Deep Sleep flag */
	t_u32 is_deep_sleep;
	/** BSSID */
	mlan_802_11_mac_addr bssid;
#ifdef STA_SUPPORT
	/** Capability Info */
	t_u16 capability_info;
	/** Beacon Interval */
	t_u16 beacon_interval;
	/** Listen Interval */
	t_u16 listen_interval;
	/** Association Id  */
	t_u16 assoc_id;
	/** AP/Peer supported rates */
	t_u8 peer_supp_rates[MLAN_SUPPORTED_RATES];
	/** extend capability for AP */
	ExtCap_t ext_cap;
#endif				/* STA_SUPPORT */
	/** Mobility Domain ID */
	t_u16 mdid;
	/** FT Capability policy */
	t_u8 ft_cap;
	/** 11h active */
	t_bool is_11h_active;
	/** dfs check channel */
	t_u8 dfs_check_channel;
} mlan_bss_info, *pmlan_bss_info;

/** MAXIMUM number of TID */
#define MAX_NUM_TID 8

/** Max RX Win size */
#define MAX_RX_WINSIZE 64

/** rx_reorder_tbl */
typedef struct {
	/** TID */
	t_u16 tid;
	/** TA */
	t_u8 ta[MLAN_MAC_ADDR_LENGTH];
	/** Start window */
	t_u32 start_win;
	/** Window size */
	t_u32 win_size;
	/** amsdu flag */
	t_u8 amsdu;
	/** buffer status */
	t_u32 buffer[MAX_RX_WINSIZE];
} rx_reorder_tbl;

/** tx_ba_stream_tbl */
typedef struct {
	/** TID */
	t_u16 tid;
	/** RA */
	t_u8 ra[MLAN_MAC_ADDR_LENGTH];
	/** amsdu flag */
	t_u8 amsdu;
} tx_ba_stream_tbl;

/** Debug command number */
#define DBG_CMD_NUM 10

#ifdef SDIO
/** sdio mp debug number */
#define SDIO_MP_DBG_NUM 10
#endif

/** Maximum size of IEEE Information Elements */
#define IEEE_MAX_IE_SIZE 256

/** support up to 8 TDLS peer */
#define MLAN_MAX_TDLS_PEER_SUPPORTED 8
/** TDLS peer info */
typedef struct _tdls_peer_info {
	/** station mac address */
	t_u8 mac_addr[MLAN_MAC_ADDR_LENGTH];
	/** SNR */
	t_s8 snr;
	/** Noise Floor */
	t_s8 nf;
	/** Extended Capabilities IE */
	t_u8 ext_cap[IEEE_MAX_IE_SIZE];
	/** HT Capabilities IE */
	t_u8 ht_cap[IEEE_MAX_IE_SIZE];
	/** VHT Capabilities IE */
	t_u8 vht_cap[IEEE_MAX_IE_SIZE];
} tdls_peer_info;

/** max ralist num */
#define MLAN_MAX_RALIST_NUM 8
/** ralist info */
typedef struct _ralist_info {
	/** RA list buffer */
	t_u8 ra[MLAN_MAC_ADDR_LENGTH];
	/** total packets in RA list */
	t_u16 total_pkts;
	/** tid num */
	t_u8 tid;
	/** tx_pause flag */
	t_u8 tx_pause;
} ralist_info, *pralist_info;

/** mlan_debug_info data structure for MLAN_OID_GET_DEBUG_INFO */
typedef struct _mlan_debug_info {
	/* WMM AC_BK count */
	t_u32 wmm_ac_bk;
	/* WMM AC_BE count */
	t_u32 wmm_ac_be;
	/* WMM AC_VI count */
	t_u32 wmm_ac_vi;
	/* WMM AC_VO count */
	t_u32 wmm_ac_vo;
	/** Corresponds to max_tx_buf_size member of mlan_adapter*/
	t_u32 max_tx_buf_size;
	/** Corresponds to tx_buf_size member of mlan_adapter*/
	t_u32 tx_buf_size;
	/** Corresponds to curr_tx_buf_size member of mlan_adapter*/
	t_u32 curr_tx_buf_size;
	/** Tx table num */
	t_u32 tx_tbl_num;
	/** Tx ba stream table */
	tx_ba_stream_tbl tx_tbl[MLAN_MAX_TX_BASTREAM_SUPPORTED];
	/** Rx table num */
	t_u32 rx_tbl_num;
	/** Rx reorder table*/
	rx_reorder_tbl rx_tbl[MLAN_MAX_RX_BASTREAM_SUPPORTED];
	/** TDLS peer number */
	t_u32 tdls_peer_num;
	/** TDLS peer list*/
	tdls_peer_info tdls_peer_list[MLAN_MAX_TDLS_PEER_SUPPORTED];
	/** ralist num */
	t_u32 ralist_num;
	/** ralist info */
	ralist_info ralist[MLAN_MAX_RALIST_NUM];
	/** Corresponds to ps_mode member of mlan_adapter */
	t_u16 ps_mode;
	/** Corresponds to ps_state member of mlan_adapter */
	t_u32 ps_state;
#ifdef STA_SUPPORT
	/** Corresponds to is_deep_sleep member of mlan_adapter */
	t_u8 is_deep_sleep;
#endif /** STA_SUPPORT */
	/** Corresponds to pm_wakeup_card_req member of mlan_adapter */
	t_u8 pm_wakeup_card_req;
	/** Corresponds to pm_wakeup_fw_try member of mlan_adapter */
	t_u32 pm_wakeup_fw_try;
	/** time stamp when host try to wake up firmware */
	t_u32 pm_wakeup_in_secs;
	/** wake up timeout happened */
	t_u32 pm_wakeup_timeout;
	/** Corresponds to is_hs_configured member of mlan_adapter */
	t_u8 is_hs_configured;
	/** Corresponds to hs_activated member of mlan_adapter */
	t_u8 hs_activated;
	/** Corresponds to pps_uapsd_mode member of mlan_adapter */
	t_u16 pps_uapsd_mode;
	/** Corresponds to sleep_period.period member of mlan_adapter */
	t_u16 sleep_pd;
	/** Corresponds to wmm_qosinfo member of mlan_private */
	t_u8 qos_cfg;
	/** Corresponds to tx_lock_flag member of mlan_adapter */
	t_u8 tx_lock_flag;
	/** Corresponds to port_open member of mlan_private */
	t_u8 port_open;
	/** bypass pkt count */
	t_u32 bypass_pkt_count;
	/** Corresponds to scan_processing member of mlan_adapter */
	t_u32 scan_processing;
	/** Corresponds to mlan_processing member of mlan_adapter */
	t_u32 mlan_processing;
	/** Corresponds to main_lock_flag member of mlan_adapter */
	t_u32 main_lock_flag;
	/** Corresponds to main_process_cnt member of mlan_adapter */
	t_u32 main_process_cnt;
	/** Corresponds to delay_task_flag member of mlan_adapter */
	t_u32 delay_task_flag;
	/** mlan_rx_processing */
	t_u32 mlan_rx_processing;
	/** rx pkts queued */
	t_u32 rx_pkts_queued;
	/** Number of host to card command failures */
	t_u32 num_cmd_host_to_card_failure;
	/** Number of host to card sleep confirm failures */
	t_u32 num_cmd_sleep_cfm_host_to_card_failure;
	/** Number of host to card Tx failures */
	t_u32 num_tx_host_to_card_failure;
	/** Number of allocate buffer failure */
	t_u32 num_alloc_buffer_failure;
	/** Number of pkt dropped */
	t_u32 num_pkt_dropped;
#ifdef SDIO
	/** Number of card to host command/event failures */
	t_u32 num_cmdevt_card_to_host_failure;
	/** Number of card to host Rx failures */
	t_u32 num_rx_card_to_host_failure;
	/** Number of interrupt read failures */
	t_u32 num_int_read_failure;
	/** Last interrupt status */
	t_u32 last_int_status;
	/** number of interrupt receive */
	t_u32 num_of_irq;
	/** flag for sdio rx aggr */
	t_u8 sdio_rx_aggr;
	/** FW update port number */
	t_u32 mp_update[SDIO_MP_AGGR_DEF_PKT_LIMIT_MAX * 2];
	/** Invalid port update count */
	t_u32 mp_invalid_update;
	/** Number of packets tx aggr */
	t_u32 mpa_tx_count[SDIO_MP_AGGR_DEF_PKT_LIMIT_MAX];
	/** no more packets count*/
	t_u32 mpa_sent_last_pkt;
	/** no write_ports count */
	t_u32 mpa_sent_no_ports;
	/** last recv wr_bitmap */
	t_u32 last_recv_wr_bitmap;
    /** last recv rd_bitmap */
	t_u32 last_recv_rd_bitmap;
    /** mp_data_port_mask */
	t_u32 mp_data_port_mask;
	/** last mp_wr_bitmap */
	t_u32 last_mp_wr_bitmap[SDIO_MP_DBG_NUM];
	/** last ports for cmd53 write data */
	t_u32 last_mp_wr_ports[SDIO_MP_DBG_NUM];
	/** last len for cmd53 write data */
	t_u32 last_mp_wr_len[SDIO_MP_DBG_NUM];
	/** last curr_wr_port */
	t_u8 last_curr_wr_port[SDIO_MP_DBG_NUM];
	/** length info for cmd53 write data */
	t_u16 last_mp_wr_info[SDIO_MP_DBG_NUM * SDIO_MP_AGGR_DEF_PKT_LIMIT_MAX];
	/** last mp_index */
	t_u8 last_mp_index;
	/** buffer for mp debug */
	t_u8 *mpa_buf;
	/** length info for mp buf size */
	t_u32 mpa_buf_size;
	/** Number of packets rx aggr */
	t_u32 mpa_rx_count[SDIO_MP_AGGR_DEF_PKT_LIMIT_MAX];
	/** mp aggr_pkt limit */
	t_u8 mp_aggr_pkt_limit;
#endif
	/** Number of deauthentication events */
	t_u32 num_event_deauth;
	/** Number of disassosiation events */
	t_u32 num_event_disassoc;
	/** Number of link lost events */
	t_u32 num_event_link_lost;
	/** Number of deauthentication commands */
	t_u32 num_cmd_deauth;
	/** Number of association comamnd successes */
	t_u32 num_cmd_assoc_success;
	/** Number of association command failures */
	t_u32 num_cmd_assoc_failure;
	/** Number of consecutive association failures */
	t_u32 num_cons_assoc_failure;

	/** Number of command timeouts */
	t_u32 num_cmd_timeout;
	/** Timeout command ID */
	t_u16 timeout_cmd_id;
	/** Timeout command action */
	t_u16 timeout_cmd_act;
	/** List of last command IDs */
	t_u16 last_cmd_id[DBG_CMD_NUM];
	/** List of last command actions */
	t_u16 last_cmd_act[DBG_CMD_NUM];
	/** Last command index */
	t_u16 last_cmd_index;
	/** List of last command response IDs */
	t_u16 last_cmd_resp_id[DBG_CMD_NUM];
	/** Last command response index */
	t_u16 last_cmd_resp_index;
	/** List of last events */
	t_u16 last_event[DBG_CMD_NUM];
	/** Last event index */
	t_u16 last_event_index;
	/** Number of no free command node */
	t_u16 num_no_cmd_node;
	/** pending command id */
	t_u16 pending_cmd;
	/** time stamp for dnld last cmd */
	t_u32 dnld_cmd_in_secs;
	/** Corresponds to data_sent member of mlan_adapter */
	t_u8 data_sent;
	/** Corresponds to data_sent_cnt member of mlan_adapter */
	t_u32 data_sent_cnt;
	/** Corresponds to cmd_sent member of mlan_adapter */
	t_u8 cmd_sent;
	/** SDIO multiple port read bitmap */
	t_u32 mp_rd_bitmap;
	/** SDIO multiple port write bitmap */
	t_u32 mp_wr_bitmap;
	/** Current available port for read */
	t_u8 curr_rd_port;
	/** Current available port for write */
	t_u8 curr_wr_port;
#ifdef PCIE
	/** PCIE txbd read pointer */
	t_u32 txbd_rdptr;
	/** PCIE txbd write pointer */
	t_u32 txbd_wrptr;
	/** PCIE rxbd read pointer */
	t_u32 rxbd_rdptr;
	/** PCIE rxbd write pointer */
	t_u32 rxbd_wrptr;
	/** PCIE eventbd read pointer */
	t_u32 eventbd_rdptr;
	/** PCIE eventbd write pointer */
	t_u32 eventbd_wrptr;
	/** txrx_bd_size */
	t_u16 txrx_bd_size;
	/** txbd ring vbase */
	t_u8 *txbd_ring_vbase;
	/** txbd ring size */
	t_u32 txbd_ring_size;
	/** rxbd ring vbase */
	t_u8 *rxbd_ring_vbase;
	/** rxbd ring size */
	t_u32 rxbd_ring_size;
	/** evtbd ring vbase */
	t_u8 *evtbd_ring_vbase;
	/** evtbd ring size */
	t_u32 evtbd_ring_size;
#endif
	/** Corresponds to cmdresp_received member of mlan_adapter */
	t_u8 cmd_resp_received;
	/** Corresponds to event_received member of mlan_adapter */
	t_u8 event_received;
	/**  pendig tx pkts */
	t_u32 tx_pkts_queued;
#ifdef UAP_SUPPORT
	/**  pending bridge pkts */
	t_u16 num_bridge_pkts;
	/**  dropped pkts */
	t_u32 num_drop_pkts;
#endif
	/** FW hang report */
	t_u8 fw_hang_report;
	/** mlan_adapter pointer */
	t_void *mlan_adapter;
	/** mlan_adapter_size */
	t_u32 mlan_adapter_size;
	/** mlan_priv vector */
	t_void *mlan_priv[MLAN_MAX_BSS_NUM];
	/** mlan_priv_size */
	t_u32 mlan_priv_size[MLAN_MAX_BSS_NUM];
	/** mlan_priv_num */
	t_u8 mlan_priv_num;
} mlan_debug_info, *pmlan_debug_info;

#ifdef UAP_SUPPORT
/** Maximum number of clients supported by AP */
#define MAX_NUM_CLIENTS MAX_STA_COUNT

/** station info */
typedef struct _sta_info_data {
	/** STA MAC address */
	t_u8 mac_address[MLAN_MAC_ADDR_LENGTH];
	/** Power mgmt status */
	t_u8 power_mgmt_status;
	/** RSSI */
	t_s8 rssi;
	/** station bandmode */
	t_u16 bandmode;
	/** station stats */
	sta_stats stats;
	/** ie length */
	t_u16 ie_len;
} sta_info_data;

/** mlan_ds_sta_list structure for MLAN_OID_UAP_STA_LIST */
typedef struct _mlan_ds_sta_list {
	/** station count */
	t_u16 sta_count;
	/** station list */
	sta_info_data info[MAX_NUM_CLIENTS];
	/* ie_buf will be append at the end */
} mlan_ds_sta_list, *pmlan_ds_sta_list;
#endif

/** Type definition of mlan_ds_get_info for MLAN_IOCTL_GET_INFO */
typedef struct _mlan_ds_get_info {
	/** Sub-command */
	t_u32 sub_command;

	/** Status information parameter */
	union {
		/** Signal information for MLAN_OID_GET_SIGNAL */
		mlan_ds_get_signal signal;
		/** Signal path id for MLAN_OID_GET_SIGNAL_EXT */
		t_u16 path_id;
		/** Signal information for MLAN_OID_GET_SIGNAL_EXT */
		mlan_ds_get_signal signal_ext[MAX_PATH_NUM];
		/** Statistics information for MLAN_OID_GET_STATS */
		mlan_ds_get_stats stats;
		/** Statistics information for MLAN_OID_LINK_STATS*/
		t_u8 link_statistic[1];
		/** Firmware information for MLAN_OID_GET_FW_INFO */
		mlan_fw_info fw_info;
		/** Extended version information for MLAN_OID_GET_VER_EXT */
		mlan_ver_ext ver_ext;
		/** BSS information for MLAN_OID_GET_BSS_INFO */
		mlan_bss_info bss_info;
		/** Debug information for MLAN_OID_GET_DEBUG_INFO */
		t_u8 debug_info[1];
#ifdef UAP_SUPPORT
		/** UAP Statistics information for MLAN_OID_GET_STATS */
		mlan_ds_uap_stats ustats;
		/** UAP station list for MLAN_OID_UAP_STA_LIST */
		mlan_ds_sta_list sta_list;
#endif
	} param;
} mlan_ds_get_info, *pmlan_ds_get_info;

/*-----------------------------------------------------------------*/
/** Security Configuration Group */
/*-----------------------------------------------------------------*/
/** Enumeration for authentication mode */
enum _mlan_auth_mode {
	MLAN_AUTH_MODE_OPEN = 0x00,
	MLAN_AUTH_MODE_SHARED = 0x01,
	MLAN_AUTH_MODE_FT = 0x02,
	MLAN_AUTH_MODE_SAE = 0x03,
	MLAN_AUTH_MODE_NETWORKEAP = 0x80,
	MLAN_AUTH_MODE_AUTO = 0xFF,
};

/**Enumeration for AssocAgent authentication mode, sync from FW.*/
typedef enum {
	AssocAgentAuth_Open,
	AssocAgentAuth_Shared,
	AssocAgentAuth_FastBss,
	AssocAgentAuth_FastBss_Skip,
	AssocAgentAuth_Network_EAP,
	AssocAgentAuth_Wpa3Sae = 6,
	AssocAgentAuth_Auto,
} AssocAgentAuthType_e;

/** Enumeration for encryption mode */
enum _mlan_encryption_mode {
	MLAN_ENCRYPTION_MODE_NONE = 0,
	MLAN_ENCRYPTION_MODE_WEP40 = 1,
	MLAN_ENCRYPTION_MODE_TKIP = 2,
	MLAN_ENCRYPTION_MODE_CCMP = 3,
	MLAN_ENCRYPTION_MODE_WEP104 = 4,
	MLAN_ENCRYPTION_MODE_GCMP = 5,
	MLAN_ENCRYPTION_MODE_GCMP_256 = 6,
	MLAN_ENCRYPTION_MODE_CCMP_256 = 7,
};

/** Enumeration for PSK */
enum _mlan_psk_type {
	MLAN_PSK_PASSPHRASE = 1,
	MLAN_PSK_PMK,
	MLAN_PSK_CLEAR,
	MLAN_PSK_QUERY,
	MLAN_PSK_SAE_PASSWORD,
};

/** The bit to indicate the key is for unicast */
#define MLAN_KEY_INDEX_UNICAST 0x40000000
/** The key index to indicate default key */
#define MLAN_KEY_INDEX_DEFAULT 0x000000ff
/** Maximum key length */
/* #define MLAN_MAX_KEY_LENGTH        32 */
/** Minimum passphrase length */
#define MLAN_MIN_PASSPHRASE_LENGTH 8
/** Maximum passphrase length */
#define MLAN_MAX_PASSPHRASE_LENGTH 63
/** Minimum sae_password length */
#define MLAN_MIN_SAE_PASSWORD_LENGTH 8
/** Maximum sae_password length */
#define MLAN_MAX_SAE_PASSWORD_LENGTH 255
/** PMK length */
#define MLAN_PMK_HEXSTR_LENGTH 64
/* A few details needed for WEP (Wireless Equivalent Privacy) */
/** 104 bits */
#define MAX_WEP_KEY_SIZE 13
/** 40 bits RC4 - WEP */
#define MIN_WEP_KEY_SIZE 5
/** packet number size */
#define PN_SIZE 16
/** max seq size of wpa/wpa2 key */
#define SEQ_MAX_SIZE 8

/** key flag for tx_seq */
#define KEY_FLAG_TX_SEQ_VALID 0x00000001
/** key flag for rx_seq */
#define KEY_FLAG_RX_SEQ_VALID 0x00000002
/** key flag for group key */
#define KEY_FLAG_GROUP_KEY 0x00000004
/** key flag for tx */
#define KEY_FLAG_SET_TX_KEY 0x00000008
/** key flag for mcast IGTK */
#define KEY_FLAG_AES_MCAST_IGTK 0x00000010
/** key flag for remove key */
#define KEY_FLAG_REMOVE_KEY 0x80000000
/** key flag for GCMP */
#define KEY_FLAG_GCMP 0x00000020
/** key flag for GCMP_256 */
#define KEY_FLAG_GCMP_256 0x00000040
/** key flag for ccmp 256 */
#define KEY_FLAG_CCMP_256 0x00000080
/** key flag for GMAC_128 */
#define KEY_FLAG_GMAC_128 0x00000100
/** key flag for GMAC_256 */
#define KEY_FLAG_GMAC_256 0x00000200

/** Type definition of mlan_ds_encrypt_key for MLAN_OID_SEC_CFG_ENCRYPT_KEY */
typedef struct _mlan_ds_encrypt_key {
	/** Key disabled, all other fields will be
	 *  ignore when this flag set to MTRUE
	 */
	t_u32 key_disable;
	/** key removed flag, when this flag is set
	 *  to MTRUE, only key_index will be check
	 */
	t_u32 key_remove;
	/** Key index, used as current tx key index
	 *  when is_current_wep_key is set to MTRUE
	 */
	t_u32 key_index;
	/** Current Tx key flag */
	t_u32 is_current_wep_key;
	/** Key length */
	t_u32 key_len;
	/** Key */
	t_u8 key_material[MLAN_MAX_KEY_LENGTH];
	/** mac address */
	t_u8 mac_addr[MLAN_MAC_ADDR_LENGTH];
	/** wapi key flag */
	t_u32 is_wapi_key;
	/** Initial packet number */
	t_u8 pn[PN_SIZE];
	/** key flags */
	t_u32 key_flags;
} mlan_ds_encrypt_key, *pmlan_ds_encrypt_key;

/** Type definition of mlan_passphrase_t */
typedef struct _mlan_passphrase_t {
	/** Length of passphrase */
	t_u32 passphrase_len;
	/** Passphrase */
	t_u8 passphrase[MLAN_MAX_PASSPHRASE_LENGTH];
} mlan_passphrase_t;

/** Type definition of mlan_sae_password_t */
typedef struct _mlan_sae_password_t {
	/** Length of SAE Password */
	t_u32 sae_password_len;
	/** SAE Password */
	t_u8 sae_password[MLAN_MAX_SAE_PASSWORD_LENGTH];
} mlan_sae_password_t;

/** Type defnition of mlan_pmk_t */
typedef struct _mlan_pmk_t {
	/** PMK */
	t_u8 pmk[MLAN_MAX_KEY_LENGTH];
} mlan_pmk_t;

/** Embedded supplicant RSN type: No RSN */
#define RSN_TYPE_NO_RSN MBIT(0)
/** Embedded supplicant RSN type: WPA */
#define RSN_TYPE_WPA MBIT(3)
/** Embedded supplicant RSN type: WPA-NONE */
#define RSN_TYPE_WPANONE MBIT(4)
/** Embedded supplicant RSN type: WPA2 */
#define RSN_TYPE_WPA2 MBIT(5)
/** Embedded supplicant RSN type: RFU */
#define RSN_TYPE_VALID_BITS                                                    \
	(RSN_TYPE_NO_RSN | RSN_TYPE_WPA | RSN_TYPE_WPANONE | RSN_TYPE_WPA2)

/** Embedded supplicant cipher type: TKIP */
#define EMBED_CIPHER_TKIP MBIT(2)
/** Embedded supplicant cipher type: AES */
#define EMBED_CIPHER_AES MBIT(3)
/** Embedded supplicant cipher type: RFU */
#define EMBED_CIPHER_VALID_BITS (EMBED_CIPHER_TKIP | EMBED_CIPHER_AES)

/** Type definition of mlan_ds_passphrase for MLAN_OID_SEC_CFG_PASSPHRASE */
typedef struct _mlan_ds_passphrase {
	/** SSID may be used */
	mlan_802_11_ssid ssid;
	/** BSSID may be used */
	mlan_802_11_mac_addr bssid;
	/** Flag for passphrase or pmk used */
	t_u16 psk_type;
	/** Passphrase or PMK */
	union {
		/** Passphrase */
		mlan_passphrase_t passphrase;
		/** SAE Password */
		mlan_sae_password_t sae_password;
		/** PMK */
		mlan_pmk_t pmk;
	} psk;
} mlan_ds_passphrase, *pmlan_ds_passphrase;

/** Type definition of mlan_ds_esupp_mode for MLAN_OID_SEC_CFG_ESUPP_MODE */
typedef struct _mlan_ds_ewpa_mode {
	/** RSN mode */
	t_u32 rsn_mode;
	/** Active pairwise cipher */
	t_u32 act_paircipher;
	/** Active pairwise cipher */
	t_u32 act_groupcipher;
} mlan_ds_esupp_mode, *pmlan_ds_esupp_mode;

/** Type definition of mlan_ds_sec_cfg for MLAN_IOCTL_SEC_CFG */
typedef struct _mlan_ds_sec_cfg {
	/** Sub-command */
	t_u32 sub_command;
	/** Security configuration parameter */
	union {
		/** Authentication mode for MLAN_OID_SEC_CFG_AUTH_MODE */
		t_u32 auth_mode;
		/** Encryption mode for MLAN_OID_SEC_CFG_ENCRYPT_MODE */
		t_u32 encrypt_mode;
		/** WPA enabled flag for MLAN_OID_SEC_CFG_WPA_ENABLED */
		t_u32 wpa_enabled;
		/** WAPI enabled flag for MLAN_OID_SEC_CFG_WAPI_ENABLED */
		t_u32 wapi_enabled;
		/** Port Control enabled flag for MLAN_OID_SEC_CFG_PORT_CTRL */
		t_u32 port_ctrl_enabled;
		/** Encryption key for MLAN_OID_SEC_CFG_ENCRYPT_KEY */
		mlan_ds_encrypt_key encrypt_key;
		/** Passphrase for MLAN_OID_SEC_CFG_PASSPHRASE */
		mlan_ds_passphrase passphrase;
		/** Embedded supplicant WPA enabled flag for
		 *  MLAN_OID_SEC_CFG_EWPA_ENABLED
		 */
		t_u32 ewpa_enabled;
		/** Embedded supplicant mode for MLAN_OID_SEC_CFG_ESUPP_MODE */
		mlan_ds_esupp_mode esupp_mode;
#ifdef UAP_SUPPORT
		t_u8 sta_mac[MLAN_MAC_ADDR_LENGTH];
#endif
	} param;
} mlan_ds_sec_cfg, *pmlan_ds_sec_cfg;

#if defined(DRV_EMBEDDED_AUTHENTICATOR) || defined(DRV_EMBEDDED_SUPPLICANT)
#define BIT_TLV_TYPE_CRYPTO_KEY (1 << 0)
#define BIT_TLV_TYPE_CRYPTO_KEY_IV (1 << 1)
#define BIT_TLV_TYPE_CRYPTO_KEY_PREFIX (1 << 2)
#define BIT_TLV_TYPE_CRYPTO_KEY_DATA_BLK (1 << 3)

/** Type definition of mlan_ds_sup_cfg */
typedef struct _mlan_ds_sup_cfg {
	/** Sub-command */
	t_u8 sub_command;
	/** output length */
	t_u16 output_len;
	/** number of data blks */
	t_u16 data_blks_nr;
	/** sub action code */
	t_u8 sub_action_code;
	/** skip bytes */
	t_u16 skip_bytes;
	/** iteration */
	t_u32 iteration;
	/** count */
	t_u32 count;
	/** pointer to output */
	t_u8 *output;
	/** key length  */
	t_u16 key_len;
	/** pointer to key */
	t_u8 *key;
	/** key iv length  */
	t_u16 key_iv_len;
	/** pointer to key iv */
	t_u8 *key_iv;
	/** key prefix length */
	t_u16 key_prefix_len;
	/** pointer to key prefix */
	t_u8 *key_prefix;
	/** pointer to data blk length array */
	t_u32 *key_data_blk_len;
	/** pointer to key data blk pointer array */
	t_u8 **key_data_blk;
	/** callback */
	t_u8 call_back;
} mlan_ds_sup_cfg, *pmlan_ds_sup_cfg;
#endif

/*-----------------------------------------------------------------*/
/** Rate Configuration Group */
/*-----------------------------------------------------------------*/
/** Enumeration for rate type */
enum _mlan_rate_type { MLAN_RATE_INDEX, MLAN_RATE_VALUE, MLAN_RATE_BITMAP };

/** Enumeration for rate format */
enum _mlan_rate_format {
	MLAN_RATE_FORMAT_LG = 0,
	MLAN_RATE_FORMAT_HT,
	MLAN_RATE_FORMAT_VHT,
	MLAN_RATE_FORMAT_HE,
	MLAN_RATE_FORMAT_AUTO = 0xFF,
};

/** Max bitmap rates size */
#define MAX_BITMAP_RATES_SIZE 26

/** Type definition of mlan_rate_cfg_t for MLAN_OID_RATE_CFG */
typedef struct _mlan_rate_cfg_t {
	/** Fixed rate: 0, auto rate: 1 */
	t_u32 is_rate_auto;
	/** Rate type. 0: index; 1: value; 2: bitmap */
	t_u32 rate_type;
	/** Rate/MCS index or rate value if fixed rate */
	t_u32 rate;
	/** Rate Bitmap */
	t_u16 bitmap_rates[MAX_BITMAP_RATES_SIZE];
	/** NSS */
	t_u32 nss;
	/* LG rate: 0, HT rate: 1, VHT rate: 2 */
	t_u32 rate_format;
	/** Rate Setting */
	t_u16 rate_setting;
} mlan_rate_cfg_t;

/** HT channel bandwidth */
typedef enum _mlan_ht_bw {
	MLAN_HT_BW20,
	MLAN_HT_BW40,
	/** VHT channel bandwidth */
	MLAN_VHT_BW80,
	MLAN_VHT_BW160,
} mlan_ht_bw;

/** HT guard interval */
typedef enum _mlan_ht_gi {
	MLAN_HT_LGI,
	MLAN_HT_SGI,
} mlan_ht_gi;

typedef enum _mlan_vht_stbc {
	MLAN_VHT_STBC,
	MLAN_VHT_NO_STBC,
} mlan_vht_stbc;

typedef enum _mlan_vht_ldpc {
	MLAN_VHT_LDPC,
	MLAN_VHT_NO_LDPC,
} mlan_vht_ldpc;

/** Band and BSS mode */
typedef struct _mlan_band_data_rate {
	/** Band configuration */
	t_u8 config_bands;
	/** BSS mode (Infra or IBSS) */
	t_u8 bss_mode;
} mlan_band_data_rate;

/** Type definition of mlan_data_rate for MLAN_OID_GET_DATA_RATE */
typedef struct _mlan_data_rate {
	/** Tx data rate */
	t_u32 tx_data_rate;
	/** Rx data rate */
	t_u32 rx_data_rate;

	/** Tx channel bandwidth */
	t_u32 tx_ht_bw;
	/** Tx guard interval */
	t_u32 tx_ht_gi;
	/** Rx channel bandwidth */
	t_u32 rx_ht_bw;
	/** Rx guard interval */
	t_u32 rx_ht_gi;
	/** MCS index */
	t_u32 tx_mcs_index;
	t_u32 rx_mcs_index;
	/** NSS */
	t_u32 tx_nss;
	t_u32 rx_nss;
	/* LG rate: 0, HT rate: 1, VHT rate: 2 */
	t_u32 tx_rate_format;
	t_u32 rx_rate_format;
} mlan_data_rate;

/** Type definition of mlan_ds_rate for MLAN_IOCTL_RATE */
typedef struct _mlan_ds_rate {
	/** Sub-command */
	t_u32 sub_command;
	/** Rate configuration parameter */
	union {
		/** Rate configuration for MLAN_OID_RATE_CFG */
		mlan_rate_cfg_t rate_cfg;
		/** Data rate for MLAN_OID_GET_DATA_RATE */
		mlan_data_rate data_rate;
		/** Supported rates for MLAN_OID_SUPPORTED_RATES */
		t_u8 rates[MLAN_SUPPORTED_RATES];
		/** Band/BSS mode for getting supported rates */
		mlan_band_data_rate rate_band_cfg;
	} param;
} mlan_ds_rate, *pmlan_ds_rate;

/*-----------------------------------------------------------------*/
/** Power Configuration Group */
/*-----------------------------------------------------------------*/

/** Type definition of mlan_power_cfg_t for MLAN_OID_POWER_CFG */
typedef struct _mlan_power_cfg_t {
	/** Is power auto */
	t_u32 is_power_auto;
	/** Power level in dBm */
	t_s32 power_level;
} mlan_power_cfg_t;

/** max power table size */
#define MAX_POWER_TABLE_SIZE 128
#define TX_PWR_CFG_AUTO_CTRL_OFF 0xFF
#define MAX_POWER_GROUP 64
/** Type definition of mlan_power group info */
typedef struct mlan_power_group {
	/** rate format (LG: 0, HT: 1, VHT: 2, no auto ctrl: 0xFF) */
	t_u32 rate_format;
	/** bandwidth (LG: 20 MHz, HT: 20/40 MHz, VHT: 80/160/80+80 MHz) */
	t_u8 bandwidth;
	/** NSS */
	t_u32 nss;
	/** LG: first rate index, HT/VHT: first MCS */
	t_u8 first_rate_ind;
	/** LG: last rate index, HT/VHT: last MCS */
	t_u8 last_rate_ind;
	/** minmum tx power (dBm) */
	t_s8 power_min;
	/** maximum tx power (dBm) */
	t_s8 power_max;
	/** tx power step (dB) */
	t_s8 power_step;
} mlan_power_group;

/** Type definition of mlan_power_cfg_ext for MLAN_OID_POWER_CFG_EXT */
typedef struct _mlan_power_cfg_ext {
	/** number of power_groups */
	t_u32 num_pwr_grp;
	/** array of power groups */
	mlan_power_group power_group[MAX_POWER_GROUP];
} mlan_power_cfg_ext;

/** Type definition of mlan_ds_power_cfg for MLAN_IOCTL_POWER_CFG */
typedef struct _mlan_ds_power_cfg {
	/** Sub-command */
	t_u32 sub_command;
	/** Power configuration parameter */
	union {
		/** Power configuration for MLAN_OID_POWER_CFG */
		mlan_power_cfg_t power_cfg;
		/** Extended power configuration for MLAN_OID_POWER_CFG_EXT */
		mlan_power_cfg_ext power_ext;
		/** Low power mode for MLAN_OID_POWER_LOW_POWER_MODE */
		t_u16 lpm;
	} param;
} mlan_ds_power_cfg, *pmlan_ds_power_cfg;

/** Type definition of mlan_ds_band_steer_cfg for MLAN_IOCTL_POWER_CFG */
typedef struct _mlan_ds_band_steer_cfg {
	/** Set/Get */
	t_u8 action;
	/** enable/disable band steering*/
	t_u8 state;
	/** Probe Response will be blocked to 2G channel for first
	 * block_2g_prb_req probe requests*/
	t_u8 block_2g_prb_req;
	/** When band steering is enabled, limit the btm request sent to STA at
	 * <max_btm_req_allowed>*/
	t_u8 max_btm_req_allowed;
} mlan_ds_band_steer_cfg, *pmlan_ds_band_steer_cfg;

/** Type definition of mlan_ds_beacon_stuck_param_cfg for MLAN_IOCTL_POWER_CFG */
typedef struct _mlan_ds_beacon_stuck_param_cfg {
    /** subcmd */
	t_u32 subcmd;
    /** Set/Get */
	t_u8 action;
    /** No of beacon interval after which firmware will check if beacon Tx is going fine */
	t_u8 beacon_stuck_detect_count;
    /** Upon performing MAC reset, no of beacon interval after which firmware will check if recovery was successful */
	t_u8 recovery_confirm_count;
} mlan_ds_beacon_stuck_param_cfg, *pmlan_ds_beacon_stuck_param_cfg;

/*-----------------------------------------------------------------*/
/** Power Management Configuration Group */
/*-----------------------------------------------------------------*/
/** Host sleep config conditions : Cancel */
#define HOST_SLEEP_CFG_CANCEL 0xffffffff

/** Host sleep config condition: broadcast data */
#define HOST_SLEEP_COND_BROADCAST_DATA MBIT(0)
/** Host sleep config condition: unicast data */
#define HOST_SLEEP_COND_UNICAST_DATA MBIT(1)
/** Host sleep config condition: mac event */
#define HOST_SLEEP_COND_MAC_EVENT MBIT(2)
/** Host sleep config condition: multicast data */
#define HOST_SLEEP_COND_MULTICAST_DATA MBIT(3)
/** Host sleep config condition: IPV6 packet */
#define HOST_SLEEP_COND_IPV6_PACKET MBIT(31)

/** Host sleep config conditions: Default */
#define HOST_SLEEP_DEF_COND                                                    \
	(HOST_SLEEP_COND_BROADCAST_DATA | HOST_SLEEP_COND_UNICAST_DATA |       \
	 HOST_SLEEP_COND_MAC_EVENT)

/** Host sleep config GPIO : Default */
#define HOST_SLEEP_DEF_GPIO 0xff
/** Host sleep config gap : Default */
#define HOST_SLEEP_DEF_GAP 200
/** Host sleep config min wake holdoff */
#define HOST_SLEEP_DEF_WAKE_HOLDOFF 0;
/** Host sleep config inactivity timeout */
#define HOST_SLEEP_DEF_INACTIVITY_TIMEOUT 10;

/** Type definition of mlan_ds_hs_cfg for MLAN_OID_PM_CFG_HS_CFG */
typedef struct _mlan_ds_hs_cfg {
	/** MTRUE to invoke the HostCmd, MFALSE otherwise */
	t_u32 is_invoke_hostcmd;
	/** Host sleep config condition */
	/** Bit0: broadcast data
	 *  Bit1: unicast data
	 *  Bit2: mac event
	 *  Bit3: multicast data
	 */
	t_u32 conditions;
	/** GPIO pin or 0xff for interface */
	t_u32 gpio;
	/** Gap in milliseconds or or 0xff for special
	 *  setting when GPIO is used to wakeup host
	 */
	t_u32 gap;
	/** Host sleep wake interval */
	t_u32 hs_wake_interval;
	/** Parameter type for indication gpio*/
	t_u8 param_type_ind;
	/** GPIO pin for indication wakeup source */
	t_u32 ind_gpio;
	/** Level on ind_gpio pin for indication normal wakeup source */
	t_u32 level;
	/** Parameter type for extend hscfg*/
	t_u8 param_type_ext;
	/** Events that will be forced ignore*/
	t_u32 event_force_ignore;
	/** Events that will use extend gap to inform host*/
	t_u32 event_use_ext_gap;
	/** Ext gap*/
	t_u8 ext_gap;
	/** GPIO wave level for extend hscfg*/
	t_u8 gpio_wave;
} mlan_ds_hs_cfg, *pmlan_ds_hs_cfg;

#define MAX_MGMT_FRAME_FILTER 2
typedef struct _mlan_mgmt_frame_wakeup {
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
	t_u32 action;
	/** Frame type(p2p, tdls...)
	 ** type=0: invalid
	 ** type=1: p2p
	 ** type=others: reserved
	 **/
	t_u32 type;
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
} mlan_mgmt_frame_wakeup, *pmlan_mgmt_frame_wakeup;

/** Enable deep sleep mode */
#define DEEP_SLEEP_ON 1
/** Disable deep sleep mode */
#define DEEP_SLEEP_OFF 0

/** Default idle time in milliseconds for auto deep sleep */
#define DEEP_SLEEP_IDLE_TIME 100

typedef struct _mlan_ds_auto_ds {
	/** auto ds mode, 0 - disable, 1 - enable */
	t_u16 auto_ds;
	/** auto ds idle time in milliseconds */
	t_u16 idletime;
} mlan_ds_auto_ds;

/** Type definition of mlan_ds_inactivity_to
 *  for MLAN_OID_PM_CFG_INACTIVITY_TO
 */
typedef struct _mlan_ds_inactivity_to {
	/** Timeout unit in microsecond, 0 means 1000us (1ms) */
	t_u32 timeout_unit;
	/** Inactivity timeout for unicast data */
	t_u32 unicast_timeout;
	/** Inactivity timeout for multicast data */
	t_u32 mcast_timeout;
	/** Timeout for additional Rx traffic after Null PM1 packet exchange */
	t_u32 ps_entry_timeout;
} mlan_ds_inactivity_to, *pmlan_ds_inactivity_to;

/** Minimum sleep period in milliseconds */
#define MIN_SLEEP_PERIOD 10
/** Maximum sleep period in milliseconds */
#define MAX_SLEEP_PERIOD 60
/** Special setting for UPSD certification tests */
#define SLEEP_PERIOD_RESERVED_FF 0xFF

/** PS null interval disable */
#define PS_NULL_DISABLE (-1)

/** Local listen interval disable */
#define MRVDRV_LISTEN_INTERVAL_DISABLE (-1)
/** Minimum listen interval */
#define MRVDRV_MIN_LISTEN_INTERVAL 0

/** Minimum multiple DTIM */
#define MRVDRV_MIN_MULTIPLE_DTIM 0
/** Maximum multiple DTIM */
#define MRVDRV_MAX_MULTIPLE_DTIM 5
/** Ignore multiple DTIM */
#define MRVDRV_IGNORE_MULTIPLE_DTIM 0xfffe
/** Match listen interval to closest DTIM */
#define MRVDRV_MATCH_CLOSEST_DTIM 0xfffd

/** Minimum beacon miss timeout in milliseconds */
#define MIN_BCN_MISS_TO 0
/** Maximum beacon miss timeout in milliseconds */
#define MAX_BCN_MISS_TO 50
/** Disable beacon miss timeout */
#define DISABLE_BCN_MISS_TO 65535

/** Minimum delay to PS in milliseconds */
#define MIN_DELAY_TO_PS 0
/** Maximum delay to PS in milliseconds */
#define MAX_DELAY_TO_PS 65535
/** Delay to PS unchanged */
#define DELAY_TO_PS_UNCHANGED (-1)
/** Default delay to PS in milliseconds */
#define DELAY_TO_PS_DEFAULT 1000

/** PS mode: Unchanged */
#define PS_MODE_UNCHANGED 0
/** PS mode: Auto */
#define PS_MODE_AUTO 1
/** PS mode: Poll */
#define PS_MODE_POLL 2
/** PS mode: Null */
#define PS_MODE_NULL 3

/** Type definition of mlan_ds_ps_cfg for MLAN_OID_PM_CFG_PS_CFG */
typedef struct _mlan_ds_ps_cfg {
	/** PS null interval in seconds */
	t_u32 ps_null_interval;
	/** Multiple DTIM interval */
	t_u32 multiple_dtim_interval;
	/** Listen interval */
	t_u32 listen_interval;
	/** Beacon miss timeout in milliseconds */
	t_u32 bcn_miss_timeout;
	/** Delay to PS in milliseconds */
	t_s32 delay_to_ps;
	/** PS mode */
	t_u32 ps_mode;
} mlan_ds_ps_cfg, *pmlan_ds_ps_cfg;

/** Type definition of mlan_ds_sleep_params for MLAN_OID_PM_CFG_SLEEP_PARAMS */
typedef struct _mlan_ds_sleep_params {
	/** Error */
	t_u32 error;
	/** Offset in microseconds */
	t_u32 offset;
	/** Stable time in microseconds */
	t_u32 stable_time;
	/** Calibration control */
	t_u32 cal_control;
	/** External sleep clock */
	t_u32 ext_sleep_clk;
	/** Reserved */
	t_u32 reserved;
} mlan_ds_sleep_params, *pmlan_ds_sleep_params;

/** sleep_param */
typedef struct _ps_sleep_param {
	/** control bitmap */
	t_u32 ctrl_bitmap;
	/** minimum sleep period (micro second) */
	t_u32 min_sleep;
	/** maximum sleep period (micro second) */
	t_u32 max_sleep;
} ps_sleep_param;

/** inactivity sleep_param */
typedef struct _inact_sleep_param {
	/** inactivity timeout (micro second) */
	t_u32 inactivity_to;
	/** miniumu awake period (micro second) */
	t_u32 min_awake;
	/** maximum awake period (micro second) */
	t_u32 max_awake;
} inact_sleep_param;

/** flag for ps mode */
#define PS_FLAG_PS_MODE 1
/** flag for sleep param */
#define PS_FLAG_SLEEP_PARAM 2
/** flag for inactivity sleep param */
#define PS_FLAG_INACT_SLEEP_PARAM 4

/** Enable Robust Coex mode */
#define ROBUSTCOEX_GPIOCFG_ENABLE 1
/** Disable Robust Coex mode */
#define ROBUSTCOEX_GPIOCFG_DISABLE 0

/** Disable power mode */
#define PS_MODE_DISABLE 0
/** Enable periodic dtim ps */
#define PS_MODE_PERIODIC_DTIM 1
/** Enable inactivity ps */
#define PS_MODE_INACTIVITY 2
/** FW wake up method interface */
#define FW_WAKEUP_METHOD_INTERFACE 1
/** FW wake up method gpio */
#define FW_WAKEUP_METHOD_GPIO 2
/** mlan_ds_ps_mgmt */
typedef struct _mlan_ds_ps_mgmt {
	/** flags for valid field */
	t_u16 flags;
	/** power mode */
	t_u16 ps_mode;
	/** sleep param */
	ps_sleep_param sleep_param;
	/** inactivity sleep param */
	inact_sleep_param inact_param;
} mlan_ds_ps_mgmt;

/** mlan_ds_ps_info */
typedef struct _mlan_ds_ps_info {
	/** suspend allowed flag */
	t_u32 is_suspend_allowed;
} mlan_ds_ps_info;

/** Type definition of mlan_ds_wakeup_reason for MLAN_OID_PM_HS_WAKEUP_REASON */
typedef struct _mlan_ds_hs_wakeup_reason {
	t_u16 hs_wakeup_reason;
} mlan_ds_hs_wakeup_reason;

/** Type definition of mlan_ds_ps_cfg for MLAN_OID_PM_CFG_PS_CFG */
typedef struct _mlan_ds_bcn_timeout {
	/** Beacon miss timeout period window */
	t_u16 bcn_miss_tmo_window;
	/** Beacon miss timeout period */
	t_u16 bcn_miss_tmo_period;
	/** Beacon reacquire timeout period window */
	t_u16 bcn_rq_tmo_window;
	/** Beacon reacquire timeout period */
	t_u16 bcn_rq_tmo_period;
} mlan_ds_bcn_timeout, *pmlan_ds_bcn_timeout;

/** Type definition of mlan_ds_pm_cfg for MLAN_IOCTL_PM_CFG */
typedef struct _mlan_ds_pm_cfg {
	/** Sub-command */
	t_u32 sub_command;
	/** Power management parameter */
	union {
		/** Power saving mode for MLAN_OID_PM_CFG_IEEE_PS */
		t_u32 ps_mode;
		/** Host Sleep configuration for MLAN_OID_PM_CFG_HS_CFG */
		mlan_ds_hs_cfg hs_cfg;
		/** Deep sleep mode for MLAN_OID_PM_CFG_DEEP_SLEEP */
		mlan_ds_auto_ds auto_deep_sleep;
		/** Inactivity timeout for MLAN_OID_PM_CFG_INACTIVITY_TO */
		mlan_ds_inactivity_to inactivity_to;
		/** Sleep period for MLAN_OID_PM_CFG_SLEEP_PD */
		t_u32 sleep_period;
		/** PS configuration parameters for MLAN_OID_PM_CFG_PS_CFG */
		mlan_ds_ps_cfg ps_cfg;
		/** PS configuration parameters for MLAN_OID_PM_CFG_SLEEP_PARAMS
		 */
		mlan_ds_sleep_params sleep_params;
		/** PS configuration parameters for MLAN_OID_PM_CFG_PS_MODE */
		mlan_ds_ps_mgmt ps_mgmt;
		/** power info for MLAN_OID_PM_INFO */
		mlan_ds_ps_info ps_info;
		/** hs wakeup reason for MLAN_OID_PM_HS_WAKEUP_REASON */
		mlan_ds_hs_wakeup_reason wakeup_reason;
		/** config manamgement frame for hs wakeup */
		mlan_mgmt_frame_wakeup mgmt_filter[MAX_MGMT_FRAME_FILTER];
		/** Beacon timout parameters for MLAN_OID_PM_CFG_BCN_TIMEOUT */
		mlan_ds_bcn_timeout bcn_timeout;
	} param;
} mlan_ds_pm_cfg, *pmlan_ds_pm_cfg;

#ifdef RX_PACKET_COALESCE
typedef struct {
	mlan_cmd_result_e cmd_result; /**< Firmware execution result */

	t_u32 pkt_threshold; /** Packet threshold */
	t_u16 delay; /** Timeout value in milliseconds */
} wlan_ioctl_rx_pkt_coalesce_config_t;
#endif

/*-----------------------------------------------------------------*/
/** WMM Configuration Group */
/*-----------------------------------------------------------------*/

/** WMM TSpec size */
#define MLAN_WMM_TSPEC_SIZE 63
/** WMM Add TS extra IE bytes */
#define MLAN_WMM_ADDTS_EXTRA_IE_BYTES 256
/** WMM statistics for packets hist bins */
#define MLAN_WMM_STATS_PKTS_HIST_BINS 7
/** Maximum number of AC QOS queues available */
#define MLAN_WMM_MAX_AC_QUEUES 4

/**
 *  @brief IOCTL structure to send an ADDTS request and retrieve the response.
 *
 *  IOCTL structure from the application layer relayed to firmware to
 *    instigate an ADDTS management frame with an appropriate TSPEC IE as well
 *    as any additional IEs appended in the ADDTS Action frame.
 *
 *  @sa woal_wmm_addts_req_ioctl
 */
typedef struct {
	mlan_cmd_result_e cmd_result; /**< Firmware execution result */

	t_u32 timeout_ms; /**< Timeout value in milliseconds */
	t_u8 ieee_status_code; /**< IEEE status code */

	t_u32 ie_data_len; /**< Length of ie block in ie_data */
	t_u8 ie_data[MLAN_WMM_TSPEC_SIZE /**< TSPEC to send in the ADDTS */
		     + MLAN_WMM_ADDTS_EXTRA_IE_BYTES]; /**< Extra IE buf*/
} wlan_ioctl_wmm_addts_req_t;

/**
 *  @brief IOCTL structure to send a DELTS request.
 *
 *  IOCTL structure from the application layer relayed to firmware to
 *    instigate an DELTS management frame with an appropriate TSPEC IE.
 *
 *  @sa woal_wmm_delts_req_ioctl
 */
typedef struct {
	mlan_cmd_result_e cmd_result; /**< Firmware execution result */
	t_u8 ieee_reason_code; /**< IEEE reason code sent, unused for WMM */
	t_u32 ie_data_len; /**< Length of ie block in ie_data */
	t_u8 ie_data[MLAN_WMM_TSPEC_SIZE]; /**< TSPEC to send in the DELTS */
} wlan_ioctl_wmm_delts_req_t;

/**
 *  @brief IOCTL structure to configure a specific AC Queue's parameters
 *
 *  IOCTL structure from the application layer relayed to firmware to
 *    get, set, or default the WMM AC queue parameters.
 *
 *  - msdu_lifetime_expiry is ignored if set to 0 on a set command
 *
 *  @sa woal_wmm_queue_config_ioctl
 */
typedef struct {
	mlan_wmm_queue_config_action_e action; /**< Set, Get, or Default */
	mlan_wmm_ac_e access_category; /**< WMM_AC_BK(0) to WMM_AC_VO(3) */
	t_u16 msdu_lifetime_expiry; /**< lifetime expiry in TUs */
	t_u8 supported_rates[10]; /**< Not supported yet */
} wlan_ioctl_wmm_queue_config_t;

/**
 *  @brief IOCTL structure to start, stop, and get statistics for a WMM AC
 *
 *  IOCTL structure from the application layer relayed to firmware to
 *    start or stop statistical collection for a given AC.  Also used to
 *    retrieve and clear the collected stats on a given AC.
 *
 *  @sa woal_wmm_queue_stats_ioctl
 */
typedef struct {
	/** Action of Queue Config : Start, Stop, or Get */
	mlan_wmm_queue_stats_action_e action;
	/** User Priority */
	t_u8 user_priority;
	/** Number of successful packets transmitted */
	t_u16 pkt_count;
	/** Packets lost; not included in pkt_count */
	t_u16 pkt_loss;
	/** Average Queue delay in microseconds */
	t_u32 avg_queue_delay;
	/** Average Transmission delay in microseconds */
	t_u32 avg_tx_delay;
	/** Calculated used time in units of 32 microseconds */
	t_u16 used_time;
	/** Calculated policed time in units of 32 microseconds */
	t_u16 policed_time;
	/** Queue Delay Histogram; number of packets per queue delay range
	 *
	 *  [0] -  0ms <= delay < 5ms
	 *  [1] -  5ms <= delay < 10ms
	 *  [2] - 10ms <= delay < 20ms
	 *  [3] - 20ms <= delay < 30ms
	 *  [4] - 30ms <= delay < 40ms
	 *  [5] - 40ms <= delay < 50ms
	 *  [6] - 50ms <= delay < msduLifetime (TUs)
	 */
	t_u16 delay_histogram[MLAN_WMM_STATS_PKTS_HIST_BINS];
} wlan_ioctl_wmm_queue_stats_t,
	/** Type definition of mlan_ds_wmm_queue_stats
	 *  for MLAN_OID_WMM_CFG_QUEUE_STATS
	 */
mlan_ds_wmm_queue_stats, *pmlan_ds_wmm_queue_stats;

/**
 *  @brief IOCTL sub structure for a specific WMM AC Status
 */
typedef struct {
	/** WMM Acm */
	t_u8 wmm_acm;
	/** Flow required flag */
	t_u8 flow_required;
	/** Flow created flag */
	t_u8 flow_created;
	/** Disabled flag */
	t_u8 disabled;
} wlan_ioctl_wmm_queue_status_ac_t;

/**
 *  @brief IOCTL structure to retrieve the WMM AC Queue status
 *
 *  IOCTL structure from the application layer to retrieve:
 *     - ACM bit setting for the AC
 *     - Firmware status (flow required, flow created, flow disabled)
 *
 *  @sa woal_wmm_queue_status_ioctl
 */
typedef struct {
	/** WMM AC queue status */
	wlan_ioctl_wmm_queue_status_ac_t ac_status[MLAN_WMM_MAX_AC_QUEUES];
} wlan_ioctl_wmm_queue_status_t,
	/** Type definition of mlan_ds_wmm_queue_status
	 *  for MLAN_OID_WMM_CFG_QUEUE_STATUS
	 */
mlan_ds_wmm_queue_status, *pmlan_ds_wmm_queue_status;

/** Type definition of mlan_ds_wmm_addts for MLAN_OID_WMM_CFG_ADDTS */
typedef struct _mlan_ds_wmm_addts {
	/** Result of ADDTS request */
	mlan_cmd_result_e result;
	/** Timeout value in milliseconds */
	t_u32 timeout;
	/** IEEE status code */
	t_u32 status_code;
	/** Dialog token */
	t_u8 dialog_tok;
	/** TSPEC data length */
	t_u32 ie_data_len;
	/** TSPEC to send in the ADDTS + buffering for any extra IEs */
	t_u8 ie_data[MLAN_WMM_TSPEC_SIZE + MLAN_WMM_ADDTS_EXTRA_IE_BYTES];
} mlan_ds_wmm_addts, *pmlan_ds_wmm_addts;

/** Type definition of mlan_ds_wmm_delts for MLAN_OID_WMM_CFG_DELTS */
typedef struct _mlan_ds_wmm_delts {
	/** Result of DELTS request */
	mlan_cmd_result_e result;
	/** IEEE status code */
	t_u32 status_code;
	/** TSPEC data length */
	t_u8 ie_data_len;
	/** TSPEC to send in the DELTS */
	t_u8 ie_data[MLAN_WMM_TSPEC_SIZE];
} mlan_ds_wmm_delts, *pmlan_ds_wmm_delts;

/** Type definition of mlan_ds_wmm_queue_config
 *  for MLAN_OID_WMM_CFG_QUEUE_CONFIG
 */
typedef struct _mlan_ds_wmm_queue_config {
	/** Action of Queue Config : Set, Get, or Default */
	mlan_wmm_queue_config_action_e action;
	/** WMM Access Category: WMM_AC_BK(0) to WMM_AC_VO(3) */
	mlan_wmm_ac_e access_category;
	/** Lifetime expiry in TUs */
	t_u16 msdu_lifetime_expiry;
	/** Reserve for future use */
	t_u8 reserved[10];
} mlan_ds_wmm_queue_config, *pmlan_ds_wmm_queue_config;

/** Type definition of mlan_ds_wmm_cfg for MLAN_IOCTL_WMM_CFG */
typedef struct _mlan_ds_wmm_cfg {
	/** Sub-command */
	t_u32 sub_command;
	/** WMM configuration parameter */
	union {
		/** WMM enable for MLAN_OID_WMM_CFG_ENABLE */
		t_u32 wmm_enable;
		/** QoS configuration for MLAN_OID_WMM_CFG_QOS */
		t_u8 qos_cfg;
		/** WMM add TS for MLAN_OID_WMM_CFG_ADDTS */
		mlan_ds_wmm_addts addts;
		/** WMM delete TS for MLAN_OID_WMM_CFG_DELTS */
		mlan_ds_wmm_delts delts;
		/** WMM queue configuration for MLAN_OID_WMM_CFG_QUEUE_CONFIG */
		mlan_ds_wmm_queue_config q_cfg;
		/** AC Parameters Record WMM_AC_BE, WMM_AC_BK, WMM_AC_VI,
		 * WMM_AC_VO */
		wmm_ac_parameters_t ac_params[MAX_AC_QUEUES];
		/** WMM queue status for MLAN_OID_WMM_CFG_QUEUE_STATS */
		mlan_ds_wmm_queue_stats q_stats;
		/** WMM queue status for MLAN_OID_WMM_CFG_QUEUE_STATUS */
		mlan_ds_wmm_queue_status q_status;
		/** WMM TS status for MLAN_OID_WMM_CFG_TS_STATUS */
		mlan_ds_wmm_ts_status ts_status;
	} param;
} mlan_ds_wmm_cfg, *pmlan_ds_wmm_cfg;

/*-----------------------------------------------------------------*/
/** WPS Configuration Group */
/*-----------------------------------------------------------------*/
/** Enumeration for WPS session */
enum _mlan_wps_status {
	MLAN_WPS_CFG_SESSION_START = 1,
	MLAN_WPS_CFG_SESSION_END = 0
};

/** Type definition of mlan_ds_wps_cfg for MLAN_IOCTL_WPS_CFG */
typedef struct _mlan_ds_wps_cfg {
	/** Sub-command */
	t_u32 sub_command;
	/** WPS configuration parameter */
	union {
		/** WPS session for MLAN_OID_WPS_CFG_SESSION */
		t_u32 wps_session;
	} param;
} mlan_ds_wps_cfg, *pmlan_ds_wps_cfg;

/*-----------------------------------------------------------------*/
/** 802.11n Configuration Group */
/*-----------------------------------------------------------------*/
/** Maximum MCS */
#define NUM_MCS_FIELD 16

/** Supported stream modes */
#define HT_STREAM_MODE_1X1 0x11
#define HT_STREAM_MODE_2X2 0x22

/* Both 2.4G and 5G band selected */
#define BAND_SELECT_BOTH 0
/* Band 2.4G selected */
#define BAND_SELECT_BG 1
/* Band 5G selected */
#define BAND_SELECT_A 2

/** Type definition of mlan_ds_11n_htcap_cfg for MLAN_OID_11N_HTCAP_CFG */
typedef struct _mlan_ds_11n_htcap_cfg {
	/** HT Capability information */
	t_u32 htcap;
	/** Band selection */
	t_u32 misc_cfg;
	/** Hardware HT cap information required */
	t_u32 hw_cap_req;
} mlan_ds_11n_htcap_cfg, *pmlan_ds_11n_htcap_cfg;

/** Type definition of mlan_ds_11n_addba_param
 * for MLAN_OID_11N_CFG_ADDBA_PARAM
 */
typedef struct _mlan_ds_11n_addba_param {
	/** Timeout */
	t_u32 timeout;
	/** Buffer size for ADDBA request */
	t_u32 txwinsize;
	/** Buffer size for ADDBA response */
	t_u32 rxwinsize;
	/** amsdu for ADDBA request */
	t_u8 txamsdu;
	/** amsdu for ADDBA response */
	t_u8 rxamsdu;
} mlan_ds_11n_addba_param, *pmlan_ds_11n_addba_param;

/** Type definition of mlan_ds_11n_tx_cfg for MLAN_OID_11N_CFG_TX */
typedef struct _mlan_ds_11n_tx_cfg {
	/** HTTxCap */
	t_u16 httxcap;
	/** HTTxInfo */
	t_u16 httxinfo;
	/** Band selection */
	t_u32 misc_cfg;
} mlan_ds_11n_tx_cfg, *pmlan_ds_11n_tx_cfg;

/** BF Global Configuration */
#define BF_GLOBAL_CONFIGURATION 0x00
/** Performs NDP sounding for PEER specified */
#define TRIGGER_SOUNDING_FOR_PEER 0x01
/** TX BF interval for channel sounding */
#define SET_GET_BF_PERIODICITY 0x02
/** Tell FW not to perform any sounding for peer */
#define TX_BF_FOR_PEER_ENBL 0x03
/** TX BF SNR threshold for peer */
#define SET_SNR_THR_PEER 0x04
/** TX Sounding*/
#define TX_SOUNDING_CFG 0x05

/* Maximum number of peer MAC and status/SNR tuples */
#define MAX_PEER_MAC_TUPLES 10

/** Any new subcommand structure should be declare here */

/** bf global cfg args */
typedef struct _mlan_bf_global_cfg_args {
	/** Global enable/disable bf */
	t_u8 bf_enbl;
	/** Global enable/disable sounding */
	t_u8 sounding_enbl;
	/** FB Type */
	t_u8 fb_type;
	/** SNR Threshold */
	t_u8 snr_threshold;
	/** Sounding interval in milliseconds */
	t_u16 sounding_interval;
	/** BF mode */
	t_u8 bf_mode;
	/** Reserved */
	t_u8 reserved;
} mlan_bf_global_cfg_args;

/** trigger sounding args */
typedef struct _mlan_trigger_sound_args {
	/** Peer MAC address */
	t_u8 peer_mac[MLAN_MAC_ADDR_LENGTH];
	/** Status */
	t_u8 status;
} mlan_trigger_sound_args;

/** bf periodicity args */
typedef struct _mlan_bf_periodicity_args {
	/** Peer MAC address */
	t_u8 peer_mac[MLAN_MAC_ADDR_LENGTH];
	/** Current Tx BF Interval in milliseconds */
	t_u16 interval;
	/** Status */
	t_u8 status;
} mlan_bf_periodicity_args;

/** tx bf peer args */
typedef struct _mlan_tx_bf_peer_args {
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
} mlan_tx_bf_peer_args;

/** SNR threshold args */
typedef struct _mlan_snr_thr_args {
	/** Peer MAC address */
	t_u8 peer_mac[MLAN_MAC_ADDR_LENGTH];
	/** SNR for peer */
	t_u8 snr;
} mlan_snr_thr_args;

/** Type definition of mlan_ds_11n_tx_bf_cfg for MLAN_OID_11N_CFG_TX_BF_CFG */
typedef struct _mlan_ds_11n_tx_bf_cfg {
	/** BF Action */
	t_u16 bf_action;
	/** Action */
	t_u16 action;
	/** Number of peers */
	t_u32 no_of_peers;
	union {
		mlan_bf_global_cfg_args bf_global_cfg;
		mlan_trigger_sound_args bf_sound[MAX_PEER_MAC_TUPLES];
		mlan_bf_periodicity_args bf_periodicity[MAX_PEER_MAC_TUPLES];
		mlan_tx_bf_peer_args tx_bf_peer[MAX_PEER_MAC_TUPLES];
		mlan_snr_thr_args bf_snr[MAX_PEER_MAC_TUPLES];
	} body;
} mlan_ds_11n_tx_bf_cfg, *pmlan_ds_11n_tx_bf_cfg;

/** Type definition of mlan_ds_11n_amsdu_aggr_ctrl for
 * MLAN_OID_11N_AMSDU_AGGR_CTRL*/
typedef struct _mlan_ds_11n_amsdu_aggr_ctrl {
	/** Enable/Disable */
	t_u16 enable;
	/** Current AMSDU size valid */
	t_u16 curr_buf_size;
} mlan_ds_11n_amsdu_aggr_ctrl, *pmlan_ds_11n_amsdu_aggr_ctrl;

/** Type definition of mlan_ds_11n_aggr_prio_tbl
 *  for MLAN_OID_11N_CFG_AGGR_PRIO_TBL
 */
typedef struct _mlan_ds_11n_aggr_prio_tbl {
	/** ampdu priority table */
	t_u8 ampdu[MAX_NUM_TID];
	/** amsdu priority table */
	t_u8 amsdu[MAX_NUM_TID];
} mlan_ds_11n_aggr_prio_tbl, *pmlan_ds_11n_aggr_prio_tbl;

/** DelBA All TIDs */
#define DELBA_ALL_TIDS 0xff
/** DelBA Tx */
#define DELBA_TX MBIT(0)
/** DelBA Rx */
#define DELBA_RX MBIT(1)

/** Type definition of mlan_ds_11n_delba for MLAN_OID_11N_CFG_DELBA */
typedef struct _mlan_ds_11n_delba {
	/** TID */
	t_u8 tid;
	/** Peer MAC address */
	t_u8 peer_mac_addr[MLAN_MAC_ADDR_LENGTH];
	/** Direction (Tx: bit 0, Rx: bit 1) */
	t_u8 direction;
} mlan_ds_11n_delba, *pmlan_ds_11n_delba;

/** Type definition of mlan_ds_delba for MLAN_OID_11N_CFG_REJECT_ADDBA_REQ */
typedef struct _mlan_ds_reject_addba_req {
	/** Bit0    : host sleep activated
	 *  Bit1    : auto reconnect enabled
	 *  Others  : reserved
	 */
	t_u32 conditions;
} mlan_ds_reject_addba_req, *pmlan_ds_reject_addba_req;

/** Type definition of mlan_ds_ibss_ampdu_param */
typedef struct _mlan_ds_ibss_ampdu_param {
	/** ampdu priority table */
	t_u8 ampdu[MAX_NUM_TID];
	/** rx amdpdu setting */
	t_u8 addba_reject[MAX_NUM_TID];
} mlan_ds_ibss_ampdu_param, *pmlan_ds_ibss_ampdu_param;

/** Type definition of mlan_ds_11n_cfg for MLAN_IOCTL_11N_CFG */
typedef struct _mlan_ds_11n_cfg {
	/** Sub-command */
	t_u32 sub_command;
	/** 802.11n configuration parameter */
	union {
		/** Tx param for 11n for MLAN_OID_11N_CFG_TX */
		mlan_ds_11n_tx_cfg tx_cfg;
		/** Aggr priority table for MLAN_OID_11N_CFG_AGGR_PRIO_TBL */
		mlan_ds_11n_aggr_prio_tbl aggr_prio_tbl;
		/** Add BA param for MLAN_OID_11N_CFG_ADDBA_PARAM */
		mlan_ds_11n_addba_param addba_param;
		/** Add BA Reject paramters for MLAN_OID_11N_CFG_ADDBA_REJECT */
		t_u8 addba_reject[MAX_NUM_TID];
		/** Tx buf size for MLAN_OID_11N_CFG_MAX_TX_BUF_SIZE */
		t_u32 tx_buf_size;
		/** HT cap info configuration for MLAN_OID_11N_HTCAP_CFG */
		mlan_ds_11n_htcap_cfg htcap_cfg;
		/** Tx param for 11n for MLAN_OID_11N_AMSDU_AGGR_CTRL */
		mlan_ds_11n_amsdu_aggr_ctrl amsdu_aggr_ctrl;
		/** Supported MCS Set field */
		t_u8 supported_mcs_set[NUM_MCS_FIELD];
		/** Transmit Beamforming Capabilities field */
		t_u32 tx_bf_cap;
		/** Transmit Beamforming configuration */
		mlan_ds_11n_tx_bf_cfg tx_bf;
		/** HT stream configuration */
		t_u32 stream_cfg;
		/** DelBA for MLAN_OID_11N_CFG_DELBA */
		mlan_ds_11n_delba del_ba;
		/** Reject Addba Req for MLAN_OID_11N_CFG_REJECT_ADDBA_REQ */
		mlan_ds_reject_addba_req reject_addba_req;
		/** Control coex RX window size configuration */
		t_u32 coex_rx_winsize;
		/** Control TX AMPDU configuration */
		t_u32 txaggrctrl;
		/** aggrprirotity table for MLAN_OID_11N_CFG_IBSS_AMPDU_PARAM */
		mlan_ds_ibss_ampdu_param ibss_ampdu;
		/** Minimum BA Threshold for MLAN_OID_11N_CFG_MIN_BA_THRESHOLD
		 */
		t_u8 min_ba_threshold;
	} param;
} mlan_ds_11n_cfg, *pmlan_ds_11n_cfg;

#define NUM_MCS_SUPP 20
#define VHT_MCS_SET_LEN 8

/** Type definition of mlan_ds_11ac_vhtcap_cfg for MLAN_OID_11AC_VHTCAP_CFG */
typedef struct _mlan_ds_11ac_vhtcap_cfg {
	/** HT Capability information */
	t_u32 vhtcap;
	/** Band selection */
	t_u32 misc_cfg;
	/** Hardware HT cap information required */
	t_u32 hw_cap_req;
} mlan_ds_11ac_vhtcap_cfg, *pmlan_ds_11ac_vhtcap_cfg;

/** Type definition of mlan_ds_11ac_tx_cfg for MLAN_OID_11AC_CFG_TX */
typedef struct _mlan_ds_11ac_tx_cfg {
	/** Band selection */
	t_u8 band_cfg;
	/** misc configuration */
	t_u8 misc_cfg;
	/** HTTxCap */
	t_u16 vhttxcap;
	/** HTTxInfo */
	t_u16 vhttxinfo;
} mlan_ds_11ac_tx_cfg, *pmlan_ds_11ac_tx_cfg;

/** Tx */
#define MLAN_RADIO_TX MBIT(0)
/** Rx */
#define MLAN_RADIO_RX MBIT(1)
/** Tx & Rx */
#define MLAN_RADIO_TXRX (MLAN_RADIO_TX | MLAN_RADIO_RX)

/** Type definition of mlan_ds_11ac_tx_cfg for MLAN_OID_11AC_CFG */
typedef struct _mlan_ds_11ac_vht_cfg {
	/** Band selection (1: 2.4G, 2: 5 G, 3: both 2.4G and 5G) */
	t_u32 band;
	/** TxRx (1: Tx, 2: Rx, 3: both Tx and Rx) */
	t_u32 txrx;
	/** BW CFG (0: 11N CFG, 1: vhtcap) */
	t_u32 bwcfg;
	/** VHT capabilities. */
	t_u32 vht_cap_info;
	/** VHT Tx mcs */
	t_u32 vht_tx_mcs;
	/** VHT Rx mcs */
	t_u32 vht_rx_mcs;
	/** VHT rx max rate */
	t_u16 vht_rx_max_rate;
	/** VHT max tx rate */
	t_u16 vht_tx_max_rate;
	/** Skip usr 11ac mcs cfg */
	t_bool skip_usr_11ac_mcs_cfg;
} mlan_ds_11ac_vht_cfg, *pmlan_ds_11ac_vht_cfg;

/** Type definition of mlan_ds_11ac_tx_cfg for MLAN_OID_11AC_CFG */
typedef struct _mlan_ds_11ac_opermode_cfg {
	/** channel width: 1-20MHz, 2-40MHz, 3-80MHz, 4-160MHz or 80+80MHz */
	t_u8 bw;
	/** Rx NSS */
	t_u8 nss;
} mlan_ds_11ac_opermode_cfg, *pmlan_ds_11ac_opermode_cfg;

/** Type definition of mlan_ds_11ac_cfg for MLAN_IOCTL_11AC_CFG */
typedef struct _mlan_ds_11ac_cfg {
	/** Sub-command */
	t_u32 sub_command;
	/** 802.11n configuration parameter */
	union {
		/** VHT configuration for MLAN_OID_11AC_VHT_CFG */
		mlan_ds_11ac_vht_cfg vht_cfg;
		/** Supported MCS Set field */
		t_u8 supported_mcs_set[NUM_MCS_SUPP];
		/** Oper mode configuration for MLAN_OID_11AC_OPERMODE_CFG */
		mlan_ds_11ac_opermode_cfg opermode_cfg;
	} param;
} mlan_ds_11ac_cfg, *pmlan_ds_11ac_cfg;

/** Type definition of mlan_ds_11ax_he_capa for MLAN_OID_11AX_HE_CFG */
typedef MLAN_PACK_START struct _mlan_ds_11ax_he_capa {
	/** tlv id of he capability */
	t_u16 id;
	/** length of the payload */
	t_u16 len;
	/** extension id */
	t_u8 ext_id;
	/** he mac capability info */
	t_u8 he_mac_cap[6];
	/** he phy capability info */
	t_u8 he_phy_cap[11];
	/** he txrx mcs support for 80MHz */
	t_u8 he_txrx_mcs_support[4];
	/** val for txrx mcs 160Mhz or 80+80, and PPE thresholds */
	t_u8 val[28];
} MLAN_PACK_END mlan_ds_11ax_he_capa, *pmlan_ds_11ax_he_capa;

/** Type definition of mlan_ds_11ax_he_cfg for MLAN_OID_11AX_HE_CFG */
typedef struct _mlan_ds_11ax_he_cfg {
	/** band, BIT0:2.4G, BIT1:5G*/
	t_u8 band;
	/** mlan_ds_11ax_he_capa */
	mlan_ds_11ax_he_capa he_cap;
} mlan_ds_11ax_he_cfg, *pmlan_ds_11ax_he_cfg;
/** Type definition of mlan_ds_11as_cfg for MLAN_IOCTL_11AX_CFG */
typedef struct _mlan_ds_11ax_cfg {
	/** Sub-command */
	t_u32 sub_command;
	/** 802.11n configuration parameter */
	union {
		/** HE configuration for MLAN_OID_11AX_HE_CFG */
		mlan_ds_11ax_he_cfg he_cfg;
	} param;
} mlan_ds_11ax_cfg, *pmlan_ds_11ax_cfg;

#define MLAN_11AXCMD_CFG_ID_SR_OBSS_PD_OFFSET 1
#define MLAN_11AXCMD_CFG_ID_SR_ENABLE 2
#define MLAN_11AXCMD_CFG_ID_BEAM_CHANGE 3
#define MLAN_11AXCMD_CFG_ID_HTC_ENABLE 4
#define MLAN_11AXCMD_CFG_ID_TXOP_RTS 5
#define MLAN_11AXCMD_CFG_ID_TX_OMI 6
#define MLAN_11AXCMD_CFG_ID_OBSSNBRU_TOLTIME 7

#define MLAN_11AXCMD_SR_SUBID 0x102
#define MLAN_11AXCMD_BEAM_SUBID 0x103
#define MLAN_11AXCMD_HTC_SUBID 0x104
#define MLAN_11AXCMD_TXOMI_SUBID 0x105
#define MLAN_11AXCMD_OBSS_TOLTIME_SUBID 0x106
#define MLAN_11AXCMD_TXOPRTS_SUBID 0x108

#define MLAN_11AX_TWT_SETUP_SUBID 0x114
#define MLAN_11AX_TWT_TEARDOWN_SUBID 0x115

#define MRVL_DOT11AX_ENABLE_SR_TLV_ID (PROPRIETARY_TLV_BASE_ID + 322)
#define MRVL_DOT11AX_OBSS_PD_OFFSET_TLV_ID (PROPRIETARY_TLV_BASE_ID + 323)

/** Type definition of mlan_11axcmdcfg_obss_pd_offset for MLAN_OID_11AX_CMD_CFG
 */
typedef struct MLAN_PACK_START _mlan_11axcmdcfg_obss_pd_offset {
	/** <NON_SRG_OffSET, SRG_OFFSET> */
	t_u8 offset[2];
} MLAN_PACK_END mlan_11axcmdcfg_obss_pd_offset;

/** Type definition of mlan_11axcmdcfg_sr_control for MLAN_OID_11AX_CMD_CFG */
typedef struct MLAN_PACK_START _mlan_11axcmdcfg_sr_control {
	/** 1 enable, 0 disable */
	t_u8 control;
} MLAN_PACK_END mlan_11axcmdcfg_sr_control;

/** Type definition of mlan_ds_11ax_sr_cmd for MLAN_OID_11AX_CMD_CFG */
typedef struct MLAN_PACK_START _mlan_ds_11ax_sr_cmd {
	/** type*/
	t_u16 type;
	/** length of TLV */
	t_u16 len;
	/** value */
	union {
		mlan_11axcmdcfg_obss_pd_offset obss_pd_offset;
		mlan_11axcmdcfg_sr_control sr_control;
	} param;
} MLAN_PACK_END mlan_ds_11ax_sr_cmd, *pmlan_ds_11ax_sr_cmd;

/** Type definition of mlan_ds_11ax_beam_cmd for MLAN_OID_11AX_CMD_CFG */
typedef struct _mlan_ds_11ax_beam_cmd {
	/** command value: 1 is disable, 0 is enable*/
	t_u8 value;
} mlan_ds_11ax_beam_cmd, *pmlan_ds_11ax_beam_cmd;

/** Type definition of mlan_ds_11ax_htc_cmd for MLAN_OID_11AX_CMD_CFG */
typedef struct _mlan_ds_11ax_htc_cmd {
	/** command value: 1 is enable, 0 is disable*/
	t_u8 value;
} mlan_ds_11ax_htc_cmd, *pmlan_ds_11ax_htc_cmd;

/** Type definition of mlan_ds_11ax_htc_cmd for MLAN_OID_11AX_CMD_CFG */
typedef struct _mlan_ds_11ax_txop_cmd {
	/** Two byte rts threshold value of which only 10 bits, bit 0 to bit 9
	 * are valid */
	t_u16 rts_thres;
} mlan_ds_11ax_txop_cmd, *pmlan_ds_11ax_txop_cmd;

/** Type definition of mlan_ds_11ax_htc_cmd for MLAN_OID_11AX_CMD_CFG */
typedef struct _mlan_ds_11ax_txomi_cmd {
	/* 11ax spec 9.2.4.6a.2 OM Control 12 bits. Bit 0 to bit 11 */
	t_u16 omi;
} mlan_ds_11ax_txomi_cmd, *pmlan_ds_11ax_txomi_cmd;

/** Type definition of mlan_ds_11ax_toltime_cmd for MLAN_OID_11AX_CMD_CFG */
typedef struct _mlan_ds_11ax_toltime_cmd {
	/* OBSS Narrow Bandwidth RU Tolerance Time */
	t_u32 tol_time;
} mlan_ds_11ax_toltime_cmd, *pmlan_ds_11ax_toltime_cmd;

/** Type definition of mlan_ds_11ax_cmd_cfg for MLAN_OID_11AX_CMD_CFG */
typedef struct _mlan_ds_11ax_cmd_cfg {
	/** Sub-command */
	t_u32 sub_command;
	/** Sub-id */
	t_u32 sub_id;
	/** 802.11n configuration parameter */
	union {
		/** SR configuration for MLAN_11AXCMD_SR_SUBID */
		mlan_ds_11ax_sr_cmd sr_cfg;
		/** Beam configuration for MLAN_11AXCMD_BEAM_SUBID */
		mlan_ds_11ax_beam_cmd beam_cfg;
		/** HTC configuration for MLAN_11AXCMD_HTC_SUBID */
		mlan_ds_11ax_htc_cmd htc_cfg;
		/** txop RTS configuration for MLAN_11AXCMD_TXOPRTS_SUBID */
		mlan_ds_11ax_txop_cmd txop_cfg;
		/** tx omi configuration for MLAN_11AXCMD_TXOMI_SUBID */
		mlan_ds_11ax_txomi_cmd txomi_cfg;
		/** OBSS tolerance time configuration for
		 * MLAN_11AXCMD_TOLTIME_SUBID */
		mlan_ds_11ax_toltime_cmd toltime_cfg;
	} param;
} mlan_ds_11ax_cmd_cfg, *pmlan_ds_11ax_cmd_cfg;

/** Type definition of mlan_ds_twt_setup for MLAN_OID_11AX_TWT_CFG */
typedef struct MLAN_PACK_START _mlan_ds_twt_setup {
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
} MLAN_PACK_END mlan_ds_twt_setup, *pmlan_ds_twt_setup;

/** Type definition of mlan_ds_twt_teardown for MLAN_OID_11AX_TWT_CFG */
typedef struct MLAN_PACK_START _mlan_ds_twt_teardown {
	/** TWT Flow Identifier. Range: [0-7] */
	t_u8 flow_identifier;
	/** Negotiation Type. 0: Future Individual TWT SP start time, 1: Next
	 * Wake TBTT time */
	t_u8 negotiation_type;
	/** Tear down all TWT. 1: To teardown all TWT, 0 otherwise */
	t_u8 teardown_all_twt;
} MLAN_PACK_END mlan_ds_twt_teardown, *pmlan_ds_twt_teardown;

/** Type definition of mlan_ds_twtcfg for MLAN_OID_11AX_TWT_CFG */
typedef struct MLAN_PACK_START _mlan_ds_twtcfg {
	/** Sub-command */
	t_u32 sub_command;
	/** Sub-id */
	t_u32 sub_id;
	/** TWT Setup/Teardown configuration parameter */
	union {
		/** TWT Setup config for Sub ID: MLAN_11AX_TWT_SETUP_SUBID */
		mlan_ds_twt_setup twt_setup;
		/** TWT Teardown config for Sub ID: MLAN_11AX_TWT_TEARDOWN_SUBID
		 */
		mlan_ds_twt_teardown twt_teardown;
	} param;
} MLAN_PACK_END mlan_ds_twtcfg, *pmlan_ds_twtcfg;

/** Country code length */
#define COUNTRY_CODE_LEN 3

/*-----------------------------------------------------------------*/
/** 802.11d Configuration Group */
/*-----------------------------------------------------------------*/
/** Maximum subbands for 11d */
#define MRVDRV_MAX_SUBBAND_802_11D 83

/** Data structure for subband set */
typedef struct _mlan_ds_subband_set_t {
	/** First channel */
	t_u8 first_chan;
	/** Number of channels */
	t_u8 no_of_chan;
	/** Maximum Tx power in dBm */
	t_u8 max_tx_pwr;
} mlan_ds_subband_set_t;

/** Domain regulatory information */
typedef struct _mlan_ds_11d_domain_info {
	/** Country Code */
	t_u8 country_code[COUNTRY_CODE_LEN];
	/** Band that channels in sub_band belong to */
	t_u8 band;
	/** No. of subband in below */
	t_u8 no_of_sub_band;
	/** Subband data to send/last sent */
	mlan_ds_subband_set_t sub_band[MRVDRV_MAX_SUBBAND_802_11D];
} mlan_ds_11d_domain_info;

/** Type definition of mlan_ds_11d_cfg for MLAN_IOCTL_11D_CFG */
typedef struct _mlan_ds_11d_cfg {
	/** Sub-command */
	t_u32 sub_command;
	/** 802.11d configuration parameter */
	union {
#ifdef STA_SUPPORT
		/** Enable for MLAN_OID_11D_CFG_ENABLE */
		t_u32 enable_11d;
#endif				/* STA_SUPPORT */
		/** Domain info for MLAN_OID_11D_DOMAIN_INFO_EXT */
		mlan_ds_11d_domain_info domain_info;
#ifdef UAP_SUPPORT
		/** tlv data for MLAN_OID_11D_DOMAIN_INFO */
		t_u8 domain_tlv[MAX_IE_SIZE];
#endif				/* UAP_SUPPORT */
	} param;
} mlan_ds_11d_cfg, *pmlan_ds_11d_cfg;

/*-----------------------------------------------------------------*/
/** Register Memory Access Group */
/*-----------------------------------------------------------------*/
/** Enumeration for CSU target device type */
enum _mlan_csu_target_type {
	MLAN_CSU_TARGET_CAU = 1,
	MLAN_CSU_TARGET_PSU,
};

/** Enumeration for register type */
enum _mlan_reg_type {
	MLAN_REG_MAC = 1,
	MLAN_REG_BBP,
	MLAN_REG_RF,
	MLAN_REG_CAU = 5,
	MLAN_REG_PSU = 6,
	MLAN_REG_BCA = 7,
#if defined(PCIE9098) || defined(SD9098) || defined(USB9098) || defined(PCIE9097) || defined(USB9097) || defined(SD9097)
	MLAN_REG_MAC2 = 0x81,
	MLAN_REG_BBP2 = 0x82,
	MLAN_REG_RF2 = 0x83,
	MLAN_REG_BCA2 = 0x87
#endif
};

/** Type definition of mlan_ds_reg_rw for MLAN_OID_REG_RW */
typedef struct _mlan_ds_reg_rw {
	/** Register type */
	t_u32 type;
	/** Offset */
	t_u32 offset;
	/** Value */
	t_u32 value;
} mlan_ds_reg_rw;

/** Maximum EEPROM data */
#define MAX_EEPROM_DATA 256

/** Type definition of mlan_ds_read_eeprom for MLAN_OID_EEPROM_RD */
typedef struct _mlan_ds_read_eeprom {
	/** Multiples of 4 */
	t_u16 offset;
	/** Number of bytes */
	t_u16 byte_count;
	/** Value */
	t_u8 value[MAX_EEPROM_DATA];
} mlan_ds_read_eeprom;

/** Type definition of mlan_ds_mem_rw for MLAN_OID_MEM_RW */
typedef struct _mlan_ds_mem_rw {
	/** Address */
	t_u32 addr;
	/** Value */
	t_u32 value;
} mlan_ds_mem_rw;

/** Type definition of mlan_ds_reg_mem for MLAN_IOCTL_REG_MEM */
typedef struct _mlan_ds_reg_mem {
	/** Sub-command */
	t_u32 sub_command;
	/** Register memory access parameter */
	union {
		/** Register access for MLAN_OID_REG_RW */
		mlan_ds_reg_rw reg_rw;
		/** EEPROM access for MLAN_OID_EEPROM_RD */
		mlan_ds_read_eeprom rd_eeprom;
		/** Memory access for MLAN_OID_MEM_RW */
		mlan_ds_mem_rw mem_rw;
	} param;
} mlan_ds_reg_mem, *pmlan_ds_reg_mem;

/*-----------------------------------------------------------------*/
/** Multi-Radio Configuration Group */
/*-----------------------------------------------------------------*/
/*-----------------------------------------------------------------*/
/** 802.11h Configuration Group */
/*-----------------------------------------------------------------*/
/** Type definition of mlan_ds_11h_dfs_testing for MLAN_OID_11H_DFS_TESTING */
typedef struct _mlan_ds_11h_dfs_testing {
	/** User-configured CAC period in milliseconds, 0 to use default */
	t_u32 usr_cac_period_msec;
	/** User-configured NOP period in seconds, 0 to use default */
	t_u16 usr_nop_period_sec;
	/** User-configured skip channel change, 0 to disable */
	t_u8 usr_no_chan_change;
	/** User-configured fixed channel to change to, 0 to use random channel
	 */
	t_u8 usr_fixed_new_chan;
	/** User-configured cac restart */
	t_u8 usr_cac_restart;
} mlan_ds_11h_dfs_testing, *pmlan_ds_11h_dfs_testing;

/** Type definition of mlan_ds_11h_dfs_testing for MLAN_OID_11H_CHAN_NOP_INFO */
typedef struct _mlan_ds_11h_chan_nop_info {
	/** current channel */
	t_u8 curr_chan;
	/** channel_width */
	t_u8 chan_width;
	/** flag for chan under nop */
	t_bool chan_under_nop;
	/** chan_ban_info for new channel */
	chan_band_info new_chan;
} mlan_ds_11h_chan_nop_info, *pmlan_ds_11h_chan_nop_info;

typedef struct _mlan_ds_11h_chan_rep_req {
	t_u16 startFreq;
	Band_Config_t bandcfg;
	t_u8 chanNum;
	t_u32 millisec_dwell_time; /**< Channel dwell time in milliseconds */
	t_u8 host_based;
} mlan_ds_11h_chan_rep_req;

typedef struct _mlan_ds_11h_dfs_w53_cfg {
	/** dfs w53 cfg */
	t_u8 dfs53cfg;
} mlan_ds_11h_dfs_w53_cfg;

/** Type definition of mlan_ds_11h_cfg for MLAN_IOCTL_11H_CFG */
typedef struct _mlan_ds_11h_cfg {
	/** Sub-command */
	t_u32 sub_command;
	union {
		/** Local power constraint for
		 * MLAN_OID_11H_LOCAL_POWER_CONSTRAINT */
		t_s8 usr_local_power_constraint;
		/** User-configuation for MLAN_OID_11H_DFS_TESTING */
		mlan_ds_11h_dfs_testing dfs_testing;
		/** channel NOP information for MLAN_OID_11H_CHAN_NOP_INFO */
		mlan_ds_11h_chan_nop_info ch_nop_info;
		/** channel report req for MLAN_OID_11H_CHAN_REPORT_REQUEST */
		mlan_ds_11h_chan_rep_req chan_rpt_req;
		/** channel switch count for MLAN_OID_11H_CHAN_SWITCH_COUNT*/
		t_s8 cs_count;
		mlan_ds_11h_dfs_w53_cfg dfs_w53_cfg;
	} param;
} mlan_ds_11h_cfg, *pmlan_ds_11h_cfg;

/*-----------------------------------------------------------------*/
/** Miscellaneous Configuration Group */
/*-----------------------------------------------------------------*/

/** CMD buffer size */
#define MLAN_SIZE_OF_CMD_BUFFER (3 * 1024)

/** LDO Internal */
#define LDO_INTERNAL 0
/** LDO External */
#define LDO_EXTERNAL 1

/** Enumeration for IE type */
enum _mlan_ie_type {
	MLAN_IE_TYPE_GEN_IE = 0,
#ifdef STA_SUPPORT
	MLAN_IE_TYPE_ARP_FILTER,
#endif /* STA_SUPPORT */
};

/** Type definition of mlan_ds_misc_gen_ie for MLAN_OID_MISC_GEN_IE */
typedef struct _mlan_ds_misc_gen_ie {
	/** IE type */
	t_u32 type;
	/** IE length */
	t_u32 len;
	/** IE buffer */
	t_u8 ie_data[MAX_IE_SIZE];
} mlan_ds_misc_gen_ie;

#ifdef SDIO
/** Type definition of mlan_ds_misc_sdio_mpa_ctrl
 *  for MLAN_OID_MISC_SDIO_MPA_CTRL
 */
typedef struct _mlan_ds_misc_sdio_mpa_ctrl {
	/** SDIO MP-A TX enable/disable */
	t_u16 tx_enable;
	/** SDIO MP-A RX enable/disable */
	t_u16 rx_enable;
	/** SDIO MP-A TX buf size */
	t_u16 tx_buf_size;
	/** SDIO MP-A RX buf size */
	t_u16 rx_buf_size;
	/** SDIO MP-A TX Max Ports */
	t_u16 tx_max_ports;
	/** SDIO MP-A RX Max Ports */
	t_u16 rx_max_ports;
} mlan_ds_misc_sdio_mpa_ctrl;
#endif

/** Type definition of mlan_ds_misc_cmd for MLAN_OID_MISC_HOST_CMD */
typedef struct _mlan_ds_misc_cmd {
	/** Command length */
	t_u32 len;
	/** Command buffer */
	t_u8 cmd[MRVDRV_SIZE_OF_CMD_BUFFER];
} mlan_ds_misc_cmd;

/** Maximum number of system clocks */
#define MLAN_MAX_CLK_NUM 16

/** Clock type : Configurable */
#define MLAN_CLK_CONFIGURABLE 0
/** Clock type : Supported */
#define MLAN_CLK_SUPPORTED 1

/** Type definition of mlan_ds_misc_sys_clock for MLAN_OID_MISC_SYS_CLOCK */
typedef struct _mlan_ds_misc_sys_clock {
	/** Current system clock */
	t_u16 cur_sys_clk;
	/** Clock type */
	t_u16 sys_clk_type;
	/** Number of clocks */
	t_u16 sys_clk_num;
	/** System clocks */
	t_u16 sys_clk[MLAN_MAX_CLK_NUM];
} mlan_ds_misc_sys_clock;

/** Enumeration for function init/shutdown */
enum _mlan_func_cmd {
	MLAN_FUNC_INIT = 1,
	MLAN_FUNC_SHUTDOWN,
};

/** Type definition of mlan_ds_misc_tx_datapause
 * for MLAN_OID_MISC_TX_DATAPAUSE
 */
typedef struct _mlan_ds_misc_tx_datapause {
	/** Tx data pause flag */
	t_u16 tx_pause;
	/** Max number of Tx buffers for all PS clients */
	t_u16 tx_buf_cnt;
} mlan_ds_misc_tx_datapause;

/** Type definition of mlan_ds_misc_rx_abort_cfg
 * for MLAN_OID_MISC_RX_ABORT_CFG
 */
typedef struct _mlan_ds_misc_rx_abort_cfg {
	/** enable/disable rx abort */
	t_u8 enable;
	/** Rx weak RSSI pkt threshold */
	t_s8 rssi_threshold;
} mlan_ds_misc_rx_abort_cfg;

/** Type definition of mlan_ds_misc_rx_abort_cfg_ext
 * for MLAN_OID_MISC_RX_ABORT_CFG_EXT
 */
typedef struct _mlan_ds_misc_rx_abort_cfg_ext {
	/** enable/disable dynamic rx abort */
	t_u8 enable;
	/** rssi margin */
	t_s8 rssi_margin;
	/** specify ceil rssi threshold */
	t_s8 ceil_rssi_threshold;
} mlan_ds_misc_rx_abort_cfg_ext;

/** Type definition of mlan_ds_misc_rx_abort_cfg_ext
 * for MLAN_OID_MISC_TX_AMDPU_PROT_MODE
 */
typedef struct _mlan_ds_misc_tx_ampdu_prot_mode {
	/** set prot mode */
	t_u16 mode;
} mlan_ds_misc_tx_ampdu_prot_mode;

/** Type definition of mlan_ds_misc_dot11mc_unassoc_ftm_cfg
 * for MLAN_OID_MISC_DOT11MC_UNASSOC_FTM_CFG
 */
typedef struct _mlan_ds_misc_dot11mc_unassoc_ftm_cfg {
	/** set the state */
	t_u16 state;
} mlan_ds_misc_dot11mc_unassoc_ftm_cfg;

#define RATEADAPT_ALGO_LEGACY 0
#define RATEADAPT_ALGO_SR 1

/** Type definition of mlan_ds_misc_rate_adapt_cfg
 * for MLAN_OID_MISC_RATE_ADAPT_CFG
 */
typedef struct _mlan_ds_misc_rate_adapt_cfg {
	/** SR Rateadapt */
	t_u8 sr_rateadapt;
	/** set low threshold */
	t_u8 ra_low_thresh;
	/** set high threshold */
	t_u8 ra_high_thresh;
	/** set interval */
	t_u16 ra_interval;
} mlan_ds_misc_rate_adapt_cfg;

/** Type definition of mlan_ds_misc_cck_desense_cfg
 * for MLAN_OID_MISC_CCK_DESENSE_CFG
 */
typedef struct _mlan_ds_misc_cck_desense_cfg {
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
} mlan_ds_misc_cck_desense_cfg;

/** IP address length */
#define IPADDR_LEN (16)
/** Max number of ip */
#define MAX_IPADDR (4)
/** IP address type - NONE*/
#define IPADDR_TYPE_NONE (0)
/** IP address type - IPv4*/
#define IPADDR_TYPE_IPV4 (1)
/** IP operation remove */
#define MLAN_IPADDR_OP_IP_REMOVE (0)
/** IP operation ARP filter */
#define MLAN_IPADDR_OP_ARP_FILTER MBIT(0)
/** IP operation ARP response */
#define MLAN_IPADDR_OP_AUTO_ARP_RESP MBIT(1)

/** Type definition of mlan_ds_misc_ipaddr_cfg for MLAN_OID_MISC_IP_ADDR */
typedef struct _mlan_ds_misc_ipaddr_cfg {
	/** Operation code */
	t_u32 op_code;
	/** IP address type */
	t_u32 ip_addr_type;
	/** Number of IP */
	t_u32 ip_addr_num;
	/** IP address */
	t_u8 ip_addr[MAX_IPADDR][IPADDR_LEN];
} mlan_ds_misc_ipaddr_cfg;

/** Type definnition of mlan_ds_misc_ipv6_ra_offload for
 * MLAN_OID_MISC_IPV6_RA_OFFLOAD*/
typedef struct _mlan_ds_misc_ipv6_ra_offload {
	/** 0: disable; 1: enable*/
	t_u8 enable;
	t_u8 ipv6_addr[16];
} mlan_ds_misc_ipv6_ra_offload;

/* MEF configuration disable */
#define MEF_CFG_DISABLE 0
/* MEF configuration Rx filter enable */
#define MEF_CFG_RX_FILTER_ENABLE 1
/* MEF configuration auto ARP response */
#define MEF_CFG_AUTO_ARP_RESP 2
/* MEF configuration host command */
#define MEF_CFG_HOSTCMD 0xFFFF

/** Type definition of mlan_ds_misc_mef_cfg for MLAN_OID_MISC_MEF_CFG */
typedef struct _mlan_ds_misc_mef_cfg {
	/** Sub-ID for operation */
	t_u32 sub_id;
	/** Parameter according to sub-ID */
	union {
		/** MEF command buffer for MEF_CFG_HOSTCMD */
		mlan_ds_misc_cmd cmd_buf;
	} param;
} mlan_ds_misc_mef_cfg;

/** Type definition of mlan_ds_misc_cfp_code for MLAN_OID_MISC_CFP_CODE */
typedef struct _mlan_ds_misc_cfp_code {
	/** CFP table code for 2.4GHz */
	t_u32 cfp_code_bg;
	/** CFP table code for 5GHz */
	t_u32 cfp_code_a;
} mlan_ds_misc_cfp_code;

/** Type definition of mlan_ds_misc_arb_cfg
 * for MLAN_OID_MISC_ARB_CFG
 */
typedef struct _mlan_ds_misc_arb_cfg {
	/** arb mode 0-4 */
	t_u32 arb_mode;
} mlan_ds_misc_arb_cfg;

/** Type definition of mlan_ds_misc_tp_state
 *  for MLAN_OID_MISC_TP_STATE
 */
typedef struct _mlan_ds_misc_tp_state {
	/** TP account mode 0-disable 1-enable */
	t_u32 on;
	/** Packet drop point */
	t_u32 drop_point;
} mlan_ds_misc_tp_state;

/** Type definition of mlan_ds_misc_country_code
 *  for MLAN_OID_MISC_COUNTRY_CODE
 */
typedef struct _mlan_ds_misc_country_code {
	/** Country Code */
	t_u8 country_code[COUNTRY_CODE_LEN];
} mlan_ds_misc_country_code;

/** action for set */
#define SUBSCRIBE_EVT_ACT_BITWISE_SET 0x0002
/** action for clear */
#define SUBSCRIBE_EVT_ACT_BITWISE_CLR 0x0003
/** BITMAP for subscribe event rssi low */
#define SUBSCRIBE_EVT_RSSI_LOW MBIT(0)
/** BITMAP for subscribe event snr low */
#define SUBSCRIBE_EVT_SNR_LOW MBIT(1)
/** BITMAP for subscribe event max fail */
#define SUBSCRIBE_EVT_MAX_FAIL MBIT(2)
/** BITMAP for subscribe event beacon missed */
#define SUBSCRIBE_EVT_BEACON_MISSED MBIT(3)
/** BITMAP for subscribe event rssi high */
#define SUBSCRIBE_EVT_RSSI_HIGH MBIT(4)
/** BITMAP for subscribe event snr high */
#define SUBSCRIBE_EVT_SNR_HIGH MBIT(5)
/** BITMAP for subscribe event data rssi low */
#define SUBSCRIBE_EVT_DATA_RSSI_LOW MBIT(6)
/** BITMAP for subscribe event data snr low */
#define SUBSCRIBE_EVT_DATA_SNR_LOW MBIT(7)
/** BITMAP for subscribe event data rssi high */
#define SUBSCRIBE_EVT_DATA_RSSI_HIGH MBIT(8)
/** BITMAP for subscribe event data snr high */
#define SUBSCRIBE_EVT_DATA_SNR_HIGH MBIT(9)
/** BITMAP for subscribe event link quality */
#define SUBSCRIBE_EVT_LINK_QUALITY MBIT(10)
/** BITMAP for subscribe event pre_beacon_lost */
#define SUBSCRIBE_EVT_PRE_BEACON_LOST MBIT(11)
/** default PRE_BEACON_MISS_COUNT */
#define DEFAULT_PRE_BEACON_MISS 30

/** Type definition of mlan_ds_subscribe_evt for MLAN_OID_MISC_CFP_CODE */
typedef struct _mlan_ds_subscribe_evt {
	/** evt action */
	t_u16 evt_action;
	/** bitmap for subscribe event */
	t_u16 evt_bitmap;
	/** Absolute value of RSSI threshold value (dBm) */
	t_u8 low_rssi;
	/** 0--report once, 1--report everytime happen,
	 * N -- report only happend > N consecutive times
	 */
	t_u8 low_rssi_freq;
	/** SNR threshold value (dB) */
	t_u8 low_snr;
	/** 0--report once, 1--report everytime happen,
	 *  N -- report only happend > N consecutive times
	 */
	t_u8 low_snr_freq;
	/** Failure count threshold */
	t_u8 failure_count;
	/** 0--report once, 1--report everytime happen,
	 *  N -- report only happend > N consecutive times
	 */
	t_u8 failure_count_freq;
	/** num of missed beacons */
	t_u8 beacon_miss;
	/** 0--report once, 1--report everytime happen,
	 *  N -- report only happend > N consecutive times
	 */
	t_u8 beacon_miss_freq;
	/** Absolute value of RSSI threshold value (dBm) */
	t_u8 high_rssi;
	/** 0--report once, 1--report everytime happen,
	 *  N -- report only happend > N consecutive times
	 */
	t_u8 high_rssi_freq;
	/** SNR threshold value (dB) */
	t_u8 high_snr;
	/** 0--report once, 1--report everytime happen,
	 *  N -- report only happend > N consecutive times
	 */
	t_u8 high_snr_freq;
	/** Absolute value of data RSSI threshold value (dBm) */
	t_u8 data_low_rssi;
	/** 0--report once, 1--report everytime happen,
	 *  N -- report only happend > N consecutive times
	 */
	t_u8 data_low_rssi_freq;
	/** Absolute value of data SNR threshold value (dBm) */
	t_u8 data_low_snr;
	/** 0--report once, 1--report everytime happen,
	 *  N -- report only happend > N consecutive times
	 */
	t_u8 data_low_snr_freq;
	/** Absolute value of data RSSI threshold value (dBm) */
	t_u8 data_high_rssi;
	/** 0--report once, 1--report everytime happen,
	 *  N -- report only happend > N consecutive times
	 */
	t_u8 data_high_rssi_freq;
	/** Absolute value of data SNR threshold value (dBm) */
	t_u8 data_high_snr;
	/** 0--report once, 1--report everytime happen,
	 *  N -- report only happend > N consecutive times
	 */
	t_u8 data_high_snr_freq;
	/* Link SNR threshold (dB) */
	t_u16 link_snr;
	/* Link SNR frequency */
	t_u16 link_snr_freq;
	/* Second minimum rate value as per the rate table below */
	t_u16 link_rate;
	/* Second minimum rate frequency */
	t_u16 link_rate_freq;
	/* Tx latency value (us) */
	t_u16 link_tx_latency;
	/* Tx latency frequency */
	t_u16 link_tx_lantency_freq;
	/* Number of pre missed beacons */
	t_u8 pre_beacon_miss;
} mlan_ds_subscribe_evt;

/** Max OTP user data length */
#define MAX_OTP_USER_DATA_LEN 252

/** Type definition of mlan_ds_misc_otp_user_data
 * for MLAN_OID_MISC_OTP_USER_DATA
 */
typedef struct _mlan_ds_misc_otp_user_data {
	/** Reserved */
	t_u16 reserved;
	/** OTP user data length */
	t_u16 user_data_length;
	/** User data buffer */
	t_u8 user_data[MAX_OTP_USER_DATA_LEN];
} mlan_ds_misc_otp_user_data;

typedef struct _aggr_ctrl_cfg {
	/** Enable */
	t_u16 enable;
	/** Aggregation alignment */
	t_u16 aggr_align;
	/** Aggregation max size */
	t_u16 aggr_max_size;
	/** Aggregation max packet number */
	t_u16 aggr_max_num;
	/** Aggrgation timeout, in microseconds */
	t_u16 aggr_tmo;
} aggr_ctrl_cfg;

/** Type definition of mlan_ds_misc_aggr_ctrl
 *  for MLAN_OID_MISC_AGGR_CTRL
 */
typedef struct _mlan_ds_misc_aggr_ctrl {
	/** Tx aggregation control */
	aggr_ctrl_cfg tx;
} mlan_ds_misc_aggr_ctrl;

#ifdef USB
typedef struct _usb_aggr_ctrl_cfg {
	/** Enable */
	t_u16 enable;
	/** Aggregation mode */
	t_u16 aggr_mode;
	/** Aggregation alignment */
	t_u16 aggr_align;
	/** Aggregation max packet/size */
	t_u16 aggr_max;
	/** Aggrgation timeout, in microseconds */
	t_u16 aggr_tmo;
} usb_aggr_ctrl_cfg;

/** Type definition of mlan_ds_misc_usb_aggr_ctrl
 *  for MLAN_OID_MISC_USB_AGGR_CTRL
 */
typedef struct _mlan_ds_misc_usb_aggr_ctrl {
	/** Tx aggregation control */
	usb_aggr_ctrl_cfg tx_aggr_ctrl;
	/** Rx deaggregation control */
	usb_aggr_ctrl_cfg rx_deaggr_ctrl;
} mlan_ds_misc_usb_aggr_ctrl;
#endif

#ifdef WIFI_DIRECT_SUPPORT
/** flag for NOA */
#define WIFI_DIRECT_NOA 1
/** flag for OPP_PS */
#define WIFI_DIRECT_OPP_PS 2
/** Type definition of mlan_ds_wifi_direct_config
 *  for MLAN_OID_MISC_WIFI_DIRECT_CONFIG
 */
typedef struct _mlan_ds_wifi_direct_config {
	/** flags for NOA/OPP_PS */
	t_u8 flags;
	/** NoA enable/disable */
	t_u8 noa_enable;
	/** index */
	t_u16 index;
	/** NoA count */
	t_u8 noa_count;
	/** NoA duration */
	t_u32 noa_duration;
	/** NoA interval */
	t_u32 noa_interval;
	/** opp ps enable/disable */
	t_u8 opp_ps_enable;
	/** CT window value */
	t_u8 ct_window;
} mlan_ds_wifi_direct_config;
#endif

/** Type definition of mlan_ds_gpio_tsf_latch */
typedef struct _mlan_ds_gpio_tsf_latch {
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
} mlan_ds_gpio_tsf_latch;

/** Type definition of mlan_ds_tsf_info */
typedef struct _mlan_ds_tsf_info {
	/**get tsf info format */
	t_u16 tsf_format;
	/**tsf info */
	t_u16 tsf_info;
	/**tsf */
	t_u64 tsf;
	/**Positive or negative offset in microsecond from Beacon TSF to GPIO toggle TSF  */
	t_s32 tsf_offset;
} mlan_ds_tsf_info;

#if defined(STA_SUPPORT)
typedef struct _mlan_ds_misc_pmfcfg {
	/** Management Frame Protection Capable */
	t_u8 mfpc;
	/** Management Frame Protection Required */
	t_u8 mfpr;
} mlan_ds_misc_pmfcfg;
#endif

#define MAX_SSID_NUM 16
#define MAX_AP_LIST 8

/**Action ID for TDLS disable link*/
#define WLAN_TDLS_DISABLE_LINK 0x00
/**Action ID for TDLS enable link*/
#define WLAN_TDLS_ENABLE_LINK 0x01
/**Action ID for TDLS create link*/
#define WLAN_TDLS_CREATE_LINK 0x02
/**Action ID for TDLS config link*/
#define WLAN_TDLS_CONFIG_LINK 0x03
/*reason code*/
#define MLAN_REASON_TDLS_TEARDOWN_UNSPECIFIED 26
/** TDLS operation buffer */
typedef struct _mlan_ds_misc_tdls_oper {
	/** TDLS Action */
	t_u16 tdls_action;
	/** TDLS peer address */
	t_u8 peer_mac[MLAN_MAC_ADDR_LENGTH];
	/** peer capability */
	t_u16 capability;
	/** peer qos info */
	t_u8 qos_info;
	/** peer extend capability */
	t_u8 *ext_capab;
	/** extend capability len */
	t_u8 ext_capab_len;
	/** support rates */
	t_u8 *supported_rates;
	/** supported rates len */
	t_u8 supported_rates_len;
	/** peer ht_cap */
	t_u8 *ht_capa;
	/** peer vht capability */
	t_u8 *vht_cap;
} mlan_ds_misc_tdls_oper;

/** flag for TDLS extcap */
#define TDLS_IE_FLAGS_EXTCAP 0x0001
/** flag for TDLS HTCAP */
#define TDLS_IE_FLAGS_HTCAP 0x0002
/** flag for TDLS HTINFO */
#define TDLS_IE_FLAGS_HTINFO 0x0004
/** flag for TDLS VHTCAP */
#define TDLS_IE_FLAGS_VHTCAP 0x0008
/** flag for TDLS VHTOPRAT */
#define TDLS_IE_FLAGS_VHTOPRAT 0x0010
/** flag for TDLS AID inof */
#define TDLS_IE_FLAGS_AID 0x0020
/** flag for TDLS Supported channels and regulatory class IE*/
#define TDLS_IE_FLAGS_SUPP_CS_IE 0x0040
/** flag for TDLS Qos info */
#define TDLS_IE_FLAGS_QOS_INFO 0x0080
/** flag for TDLS SETUP */
#define TDLS_IE_FLAGS_SETUP 0x0100

/** TDLS ie buffer */
typedef struct _mlan_ds_misc_tdls_ies {
	/** TDLS peer address */
	t_u8 peer_mac[MLAN_MAC_ADDR_LENGTH];
	/** flags for request IEs */
	t_u16 flags;
	/** Qos info */
	t_u8 QosInfo;
	/** Extended Capabilities IE */
	t_u8 ext_cap[IEEE_MAX_IE_SIZE];
	/** HT Capabilities IE */
	t_u8 ht_cap[IEEE_MAX_IE_SIZE];
	/** HT Information IE */
	t_u8 ht_info[IEEE_MAX_IE_SIZE];
	/** VHT Capabilities IE */
	t_u8 vht_cap[IEEE_MAX_IE_SIZE];
	/** VHT Operations IE */
	t_u8 vht_oprat[IEEE_MAX_IE_SIZE];
	/** aid Info */
	t_u8 aid_info[IEEE_MAX_IE_SIZE];
	/** supported channels */
	t_u8 supp_chan[IEEE_MAX_IE_SIZE];
	/** supported regulatory class */
	t_u8 regulatory_class[IEEE_MAX_IE_SIZE];
} mlan_ds_misc_tdls_ies;

#ifdef RX_PACKET_COALESCE
typedef struct _mlan_ds_misc_rx_packet_coalesce {
	/** packet threshold */
	t_u32 packet_threshold;
	/** timeout value */
	t_u16 delay;
} mlan_ds_misc_rx_packet_coalesce;
#endif

typedef struct _mlan_ds_misc_dfs_repeater {
	/** Set or Get */
	t_u16 action;
	/** 1 on or 0 off */
	t_u16 mode;
} mlan_ds_misc_dfs_repeater;

#define WOWLAN_MAX_PATTERN_LEN 20
#define WOWLAN_MAX_OFFSET_LEN 50
#define MAX_NUM_FILTERS 10
#define MEF_MODE_HOST_SLEEP (1 << 0)
#define MEF_MODE_NON_HOST_SLEEP (1 << 1)
#define MEF_ACTION_WAKE (1 << 0)
#define MEF_ACTION_ALLOW (1 << 1)
#define MEF_ACTION_ALLOW_AND_WAKEUP_HOST 3
#define MEF_AUTO_ARP 0x10
#define MEF_AUTO_PING 0x20
#define MEF_NS_RESP 0x40
#define MEF_MAGIC_PKT 0x80
#define CRITERIA_BROADCAST BIT(0)
#define CRITERIA_UNICAST BIT(1)
#define CRITERIA_MULTICAST BIT(3)

#define MAX_NUM_ENTRIES 8
#define MAX_NUM_BYTE_SEQ 6
#define MAX_NUM_MASK_SEQ 6

#define OPERAND_DNUM 1
#define OPERAND_BYTE_SEQ 2

#define MAX_OPERAND 0x40
#define TYPE_BYTE_EQ (MAX_OPERAND + 1)
#define TYPE_DNUM_EQ (MAX_OPERAND + 2)
#define TYPE_BIT_EQ (MAX_OPERAND + 3)

#define RPN_TYPE_AND (MAX_OPERAND + 4)
#define RPN_TYPE_OR (MAX_OPERAND + 5)

#define ICMP_OF_IP_PROTOCOL 0x01
#define TCP_OF_IP_PROTOCOL 0x06
#define UDP_OF_IP_PROTOCOL 0x11

#define IPV4_PKT_OFFSET 20
#define IP_PROTOCOL_OFFSET 31
#define PORT_PROTOCOL_OFFSET 44

#define FILLING_TYPE MBIT(0)
#define FILLING_PATTERN MBIT(1)
#define FILLING_OFFSET MBIT(2)
#define FILLING_NUM_BYTES MBIT(3)
#define FILLING_REPEAT MBIT(4)
#define FILLING_BYTE_SEQ MBIT(5)
#define FILLING_MASK_SEQ MBIT(6)

/** Type definition of filter_item
 *  Support three match methods:
 *  <1>Byte comparison type=0x41
 *  <2>Decimal comparison type=0x42
 *  <3>Bit comparison type=0x43
 */
typedef struct _mef_filter_t {
	/** flag*/
	t_u32 fill_flag;
	/** BYTE 0X41; Decimal 0X42; Bit 0x43*/
	t_u16 type;
	/** value*/
	t_u32 pattern;
	/** offset*/
	t_u16 offset;
	/** number of bytes*/
	t_u16 num_bytes;
	/** repeat*/
	t_u16 repeat;
	/** byte number*/
	t_u8 num_byte_seq;
	/** array*/
	t_u8 byte_seq[MAX_NUM_BYTE_SEQ];
	/** mask numbers*/
	t_u8 num_mask_seq;
	/** array*/
	t_u8 mask_seq[MAX_NUM_MASK_SEQ];
} mef_filter_t;

typedef struct _mef_entry_t {
	/** mode: bit0--hostsleep mode; bit1--non hostsleep mode */
	t_u8 mode;
	/** action: 0--discard and not wake host;
		    1--discard and wake host;
		    3--allow and wake host;*/
	t_u8 action;
	/** filter number */
	t_u8 filter_num;
	/** filter array*/
	mef_filter_t filter_item[MAX_NUM_FILTERS];
	/** rpn array*/
	t_u8 rpn[MAX_NUM_FILTERS];
} mef_entry_t;

/** Type definition of mlan_ds_nvflt_mef_entry
 *for MLAN_OID_MISC_MEF_FLT_CFG
 */
typedef struct _mlan_ds_misc_mef_flt_cfg {
	/** Type of action*/
	int mef_act_type;
	/** Operation code*/
	t_u32 op_code;
	/** NV Filter Criteria*/
	t_u32 criteria;
	/** NV MEF entry*/
	mef_entry_t mef_entry;
} mlan_ds_misc_mef_flt_cfg;

/** Enumeration for action type*/
enum _mlan_act_mef_act_type {
	MEF_ACT_ADD = 1,
	MEF_ACT_ENABLE,
	MEF_ACT_DISABLE,
	MEF_ACT_CANCEL,
	MEF_ACT_AUTOARP,
	MEF_ACT_WOWLAN,
	MEF_ACT_IPV6_NS,
};

typedef struct _mlan_ds_sensor_temp {
	t_u32 temperature;
} mlan_ds_sensor_temp;

#define MLAN_KCK_LEN 16
#define MLAN_KEK_LEN 16
#define MLAN_REPLAY_CTR_LEN 8
/** mlan_ds_misc_gtk_rekey_data */
typedef struct _mlan_ds_misc_gtk_rekey_data {
	/** key encryption key */
	t_u8 kek[MLAN_KEK_LEN];
	/** key confirmation key */
	t_u8 kck[MLAN_KCK_LEN];
	/** replay counter */
	t_u8 replay_ctr[MLAN_REPLAY_CTR_LEN];
} mlan_ds_misc_gtk_rekey_data;
typedef struct _mlan_ds_bw_chan_oper {
	/* bandwidth 20:20M 40:40M 80:80M */
	t_u8 bandwidth;
	/* channel number */
	t_u8 channel;
	/* Non-global operating class */
	t_u8 oper_class;
} mlan_ds_bw_chan_oper;

typedef struct _mlan_ds_ind_rst_cfg {
	/** Set or Get */
	t_u16 action;
	/** oob mode enable/ disable */
	t_u8 ir_mode;
	/** gpio pin */
	t_u8 gpio_pin;
} mlan_ds_ind_rst_cfg;

#define MKEEP_ALIVE_IP_PKT_MAX 256
typedef struct _mlan_ds_misc_keep_alive {
	t_u8 mkeep_alive_id;
	t_u8 enable;
	/** enable/disable tcp reset*/
	t_u8 reset;
	/**True means saved in driver, false means not saved or download*/
	t_u8 cached;
	t_u32 send_interval;
	t_u16 retry_interval;
	t_u16 retry_count;
	t_u8 dst_mac[MLAN_MAC_ADDR_LENGTH];
	t_u8 src_mac[MLAN_MAC_ADDR_LENGTH];
	t_u16 pkt_len;
	t_u8 packet[MKEEP_ALIVE_IP_PKT_MAX];
	/** Ethernet type */
	t_u16 ether_type;
} mlan_ds_misc_keep_alive, *pmlan_ds_misc_keep_alive;

/** TX and RX histogram statistic parameters*/
typedef MLAN_PACK_START struct _mlan_ds_misc_tx_rx_histogram {
	/** Enable or disable get tx/rx histogram statistic */
	t_u8 enable;
	/** Choose to get TX, RX or both histogram statistic */
	t_u16 action;
	/** Size of Tx/Rx info */
	t_u16 size;
	/** Store Tx/Rx info */
	t_u8 value[1];
} MLAN_PACK_END mlan_ds_misc_tx_rx_histogram;

typedef MLAN_PACK_START struct _mlan_ds_cw_mode_ctrl {
	/** Mode of Operation 0: Disable 1: Tx Continuous Packet 2: Tx
	 * Continuous Wave */
	t_u8 mode;
	/*channel */
	t_u8 channel;
	/* channel info */
	t_u8 chanInfo;
	/** Tx Power level in dBm */
	t_u16 txPower;
	/** Packet Length */
	t_u16 pktLength;
	/** bit rate Info */
	t_u32 rateInfo;
} MLAN_PACK_END mlan_ds_cw_mode_ctrl;

#define RX_PKT_INFO MBIT(1)
/** Struct for per-packet configuration */
typedef struct _mlan_per_pkt_cfg {
	/** Type ID*/
	t_u16 type;
	/** Length of payload*/
	t_u16 len;
	/**  Tx/Rx per-packet control */
	t_u8 tx_rx_control;
	/** Number of ethernet types in ether_type array */
	t_u8 proto_type_num;
	/** Array of ether_type for per-packet control */
	t_u16 ether_type[];
} mlan_per_pkt_cfg;

/** Type definition of mlan_ds_misc_robustcoex_params for MLAN_IOCTL_MISC_CFG */
typedef struct _mlan_ds_misc_robustcoex_params {
	t_u16 method;
	/** enable/disable robustcoex gpio cfg */
	t_u8 enable;
	/** Number of GPIO */
	t_u8 gpio_num;
	/** Polarity of GPIO */
	t_u8 gpio_polarity;
} mlan_ds_misc_robustcoex_params;

#if defined(PCIE)
typedef struct _mlan_ds_ssu_params {
	t_u32 nskip;
	t_u32 nsel;
	t_u32 adcdownsample;
	t_u32 mask_adc_pkt;
	t_u32 out_16bits;
	t_u32 spec_pwr_enable;
	t_u32 rate_deduction;
	t_u32 n_pkt_avg;
} mlan_ds_ssu_params;
#endif

typedef MLAN_PACK_START struct _mlan_ds_hal_phy_cfg_params {
	/** 11b pwr spectral density mask enable/disable */
	t_u8 dot11b_psd_mask_cfg;
	/** reserved fields for future hal/phy cfg use */
	t_u8 reserved[7];
} MLAN_PACK_END mlan_ds_hal_phy_cfg_params;

#define MAX_NUM_MAC 2
/** Type definition of mlan_ds_misc_mapping_policy */
typedef struct _mlan_ds_misc_mapping_policy {
	/** Enable/disable dynamic mapping */
	t_u16 subcmd;
	/** Mapping policy */
	t_u8 mapping_policy;
} mlan_ds_misc_mapping_policy, *pmlan_ds_misc_mapping_policy;

typedef struct _dmcsChanStatus_t {
	/** Channel number */
	t_u8 channel;
	/** Number of ap on this channel */
	t_u8 ap_count;
	/** Number of sta on this channel */
	t_u8 sta_count;
} dmcsChanStatus_t, *pdmcsChanStatus_t;

typedef struct _dmcsStatus_t {
	/** Radio ID */
	t_u8 radio_id;
	/** Running mode
	** 0 - Idle
	** 1 - DBC
	** 2 - DRCS
	*/
	t_u8 running_mode;
	/** Current channel status */
	dmcsChanStatus_t chan_status[2];
} dmcsStatus_t, *pdmcsStatus_t;

/** Type definition of mlan_ds_misc_dmcs_status */
typedef struct _mlan_ds_misc_dmcs_status {
	t_u8 mapping_policy;
	dmcsStatus_t radio_status[MAX_NUM_MAC];
} mlan_ds_misc_dmcs_status, *pmlan_ds_misc_dmcs_status;

/** Type definition of mlan_ds_misc_chan_trpc_cfg for
 * MLAN_OID_MISC_GET_CHAN_TRPC_CFG */
typedef struct _mlan_ds_misc_chan_trpc_cfg {
	/** sub_band */
	t_u16 sub_band;
	/** length */
	t_u16 length;
	/** buf */
	t_u8 trpc_buf[2048];
} mlan_ds_misc_chan_trpc_cfg;

#define MFG_CMD_SET_TEST_MODE   1
#define MFG_CMD_UNSET_TEST_MODE 0
#define MFG_CMD_TX_ANT          0x1004
#define MFG_CMD_RX_ANT          0x1005
#define MFG_CMD_TX_CONT         0x1009
#define MFG_CMD_RF_CHAN         0x100A
#define MFG_CMD_CLR_RX_ERR      0x1010
#define MFG_CMD_TX_FRAME        0x1021
#define MFG_CMD_RFPWR           0x1033
#define MFG_CMD_RF_BAND_AG      0x1034
#define MFG_CMD_RF_CHANNELBW    0x1044
#define MFG_CMD_RADIO_MODE_CFG  0x1211
#define MFG_CMD_CONFIG_MAC_HE_TB_TX 0x110A
/** MFG CMD generic cfg */
struct MLAN_PACK_START mfg_cmd_generic_cfg {
	/** MFG command code */
	t_u32 mfg_cmd;
	/** Action */
	t_u16 action;
	/** Device ID */
	t_u16 device_id;
	/** MFG Error code */
	t_u32 error;
	/** value 1 */
	t_u32 data1;
	/** value 2 */
	t_u32 data2;
	/** value 3 */
	t_u32 data3;
} MLAN_PACK_END;

/** MFG CMD Tx Frame 2 */
struct MLAN_PACK_START mfg_cmd_tx_frame2 {
	/** MFG command code */
	t_u32 mfg_cmd;
	/** Action */
	t_u16 action;
	/** Device ID */
	t_u16 device_id;
	/** MFG Error code */
	t_u32 error;
	/** enable */
	t_u32 enable;
	/** data_rate */
	t_u32 data_rate;
	/** frame pattern */
	t_u32 frame_pattern;
	/** frame length */
	t_u32 frame_length;
	/** BSSID */
	t_u8 bssid[MLAN_MAC_ADDR_LENGTH];
	/** Adjust burst sifs */
	t_u16 adjust_burst_sifs;
	/** Burst sifs in us*/
	t_u32 burst_sifs_in_us;
	/** short preamble */
	t_u32 short_preamble;
	/** active sub channel */
	t_u32 act_sub_ch;
	/** short GI */
	t_u32 short_gi;
	/** Adv coding */
	t_u32 adv_coding;
	/** Tx beamforming */
	t_u32 tx_bf;
	/** HT Greenfield Mode*/
	t_u32 gf_mode;
	/** STBC */
	t_u32 stbc;
	/** power id */
	t_u32 rsvd[2];
    /** NumPkt */
	t_u32 NumPkt;
    /** MaxPE */
	t_u32 MaxPE;
    /** BeamChange */
	t_u32 BeamChange;
    /** Dcm */
	t_u32 Dcm;
    /** Doppler */
	t_u32 Doppler;
    /** MidP */
	t_u32 MidP;
    /** QNum */
	t_u32 QNum;

} MLAN_PACK_END;

/* MFG CMD Tx Continuous */
struct MLAN_PACK_START mfg_cmd_tx_cont {
	/** MFG command code */
	t_u32 mfg_cmd;
	/** Action */
	t_u16 action;
	/** Device ID */
	t_u16 device_id;
	/** MFG Error code */
	t_u32 error;
	/** enable Tx*/
	t_u32 enable_tx;
	/** Continuous Wave mode */
	t_u32 cw_mode;
	/** payload pattern */
	t_u32 payload_pattern;
	/** CS Mode */
	t_u32 cs_mode;
	/** active sub channel */
	t_u32 act_sub_ch;
	/** Tx rate */
	t_u32 tx_rate;
	/** power id */
	t_u32 rsvd;
} MLAN_PACK_END;

struct MLAN_PACK_START mfg_Cmd_HE_TBTx_t {
    /** MFG command code */
	t_u32 mfg_cmd;
    /** Action */
	t_u16 action;
    /** Device ID */
	t_u16 device_id;
    /** MFG Error code */
	t_u32 error;
    /** Enable Tx */
	t_u16 enable;
    /** Q num */
	t_u16 qnum;
    /** AID */
	t_u16 aid;
    /** AXQ Mu Timer */
	t_u16 axq_mu_timer;
    /** Tx Power */
	t_u16 tx_power;
} MLAN_PACK_END;

typedef struct _mlan_ds_misc_chnrgpwr_cfg {
	/** length */
	t_u16 length;
	/** chnrgpwr buf */
	t_u8 chnrgpwr_buf[2048];
} mlan_ds_misc_chnrgpwr_cfg;

/** dfs chan list for MLAN_OID_MISC_CFP_TABLE */
typedef struct _mlan_ds_misc_cfp_tbl {
	/** band */
	t_u8 band;
	/** num chan */
	t_u8 num_chan;
	/** cfp table */
	chan_freq_power_t cfp_tbl[];
} mlan_ds_misc_cfp_tbl;

/** Type definition of mlan_ds_misc_cfg for MLAN_IOCTL_MISC_CFG */
typedef struct _mlan_ds_misc_cfg {
	/** Sub-command */
	t_u32 sub_command;
	/** Miscellaneous configuration parameter */
	union {
		/** Generic IE for MLAN_OID_MISC_GEN_IE */
		mlan_ds_misc_gen_ie gen_ie;
		/** Region code for MLAN_OID_MISC_REGION */
		t_u32 region_code;
#ifdef SDIO
		/** SDIO MP-A Ctrl command for MLAN_OID_MISC_SDIO_MPA_CTRL */
		mlan_ds_misc_sdio_mpa_ctrl mpa_ctrl;
#endif
		/** Hostcmd for MLAN_OID_MISC_HOST_CMD */
		mlan_ds_misc_cmd hostcmd;
		/** System clock for MLAN_OID_MISC_SYS_CLOCK */
		mlan_ds_misc_sys_clock sys_clock;
		/** WWS set/get for MLAN_OID_MISC_WWS */
		t_u32 wws_cfg;
		/** Get associate response for MLAN_OID_MISC_ASSOC_RSP */
		mlan_ds_misc_assoc_rsp assoc_resp;
		/** Function init/shutdown for MLAN_OID_MISC_INIT_SHUTDOWN */
		t_u32 func_init_shutdown;
		/** Custom IE for MLAN_OID_MISC_CUSTOM_IE */
		mlan_ds_misc_custom_ie cust_ie;
		t_u16 tdls_idle_time;
		/** Config dynamic bandwidth*/
		t_u16 dyn_bw;
		/** TDLS configuration for MLAN_OID_MISC_TDLS_CONFIG */
		mlan_ds_misc_tdls_config tdls_config;
		/** TDLS operation for MLAN_OID_MISC_TDLS_OPER */
		mlan_ds_misc_tdls_oper tdls_oper;
		/** TDLS ies for  MLAN_OID_MISC_GET_TDLS_IES */
		mlan_ds_misc_tdls_ies tdls_ies;
		/**tdls cs off channel*/
		t_u8 tdls_cs_channel;
		/** Tx data pause for MLAN_OID_MISC_TX_DATAPAUSE */
		mlan_ds_misc_tx_datapause tx_datapause;
		/** IP address configuration */
		mlan_ds_misc_ipaddr_cfg ipaddr_cfg;
		/** IPv6 Router Advertisement offload configuration */
		mlan_ds_misc_ipv6_ra_offload ipv6_ra_offload;
		/** MAC control for MLAN_OID_MISC_MAC_CONTROL */
		t_u32 mac_ctrl;
		/** MEF configuration for MLAN_OID_MISC_MEF_CFG */
		mlan_ds_misc_mef_cfg mef_cfg;
		/** CFP code for MLAN_OID_MISC_CFP_CODE */
		mlan_ds_misc_cfp_code cfp_code;
		/** Country code for MLAN_OID_MISC_COUNTRY_CODE */
		mlan_ds_misc_country_code country_code;
		/** Thermal reading for MLAN_OID_MISC_THERMAL */
		t_u32 thermal;
		/** Mgmt subtype mask for MLAN_OID_MISC_RX_MGMT_IND */
		t_u32 mgmt_subtype_mask;
		/** subscribe event for MLAN_OID_MISC_SUBSCRIBE_EVENT */
		mlan_ds_subscribe_evt subscribe_event;
#ifdef DEBUG_LEVEL1
		/** Driver debug bit masks */
		t_u32 drvdbg;
#endif
		/** Hotspot config param set */
		t_u32 hotspot_cfg;
#ifdef STA_SUPPORT
		ExtCap_t ext_cap;
#endif
		mlan_ds_misc_otp_user_data otp_user_data;
#ifdef USB
		/** USB aggregation parameters for MLAN_OID_MISC_USB_AGGR_CTRL
		 */
		mlan_ds_misc_usb_aggr_ctrl usb_aggr_params;
#endif
		mlan_ds_misc_aggr_ctrl aggr_params;
		/** Tx control */
		t_u32 tx_control;
#if defined(STA_SUPPORT)
		mlan_ds_misc_pmfcfg pmfcfg;
#endif
#ifdef WIFI_DIRECT_SUPPORT
		mlan_ds_wifi_direct_config p2p_config;
#endif
		mlan_ds_gpio_tsf_latch gpio_tsf_latch_config;
		mlan_ds_tsf_info tsf_info;
		mlan_ds_coalesce_cfg coalesce_cfg;
		t_u8 low_pwr_mode;
		/** MEF-FLT-CONFIG for MLAN_OID_MISC_NV_FLT_CFG */
		mlan_ds_misc_mef_flt_cfg mef_flt_cfg;
		mlan_ds_misc_dfs_repeater dfs_repeater;
#ifdef RX_PACKET_COALESCE
		mlan_ds_misc_rx_packet_coalesce rx_coalesce;
#endif
		/** FW reload flag */
		t_u8 fw_reload;
		mlan_ds_sensor_temp sensor_temp;
		/** GTK rekey data */
		mlan_ds_misc_gtk_rekey_data gtk_rekey;
		mlan_ds_bw_chan_oper bw_chan_oper;
		mlan_ds_ind_rst_cfg ind_rst_cfg;
		t_u64 misc_tsf;
		mlan_ds_custom_reg_domain custom_reg_domain;
		mlan_ds_misc_keep_alive keep_alive;
		mlan_ds_misc_tx_rx_histogram tx_rx_histogram;
		mlan_ds_cw_mode_ctrl cwmode;
		/**  Tx/Rx per-packet control */
		t_u8 txrx_pkt_ctrl;
		mlan_ds_misc_robustcoex_params robustcoexparams;
#if defined(PCIE)
		mlan_ds_ssu_params ssu_params;
#endif
		/** boot sleep enable or disable */
		t_u16 boot_sleep;
		/** Mapping Policy */
		mlan_ds_misc_mapping_policy dmcs_policy;
		mlan_ds_misc_dmcs_status dmcs_status;
		mlan_ds_misc_rx_abort_cfg rx_abort_cfg;
		mlan_ds_misc_rx_abort_cfg_ext rx_abort_cfg_ext;
		mlan_ds_misc_tx_ampdu_prot_mode tx_ampdu_prot_mode;
		mlan_ds_misc_rate_adapt_cfg rate_adapt_cfg;
		mlan_ds_misc_cck_desense_cfg cck_desense_cfg;
		mlan_ds_misc_chan_trpc_cfg trpc_cfg;
		mlan_ds_misc_chnrgpwr_cfg rgchnpwr_cfg;

		mlan_ds_band_steer_cfg band_steer_cfg;
		mlan_ds_beacon_stuck_param_cfg beacon_stuck_cfg;
		struct mfg_cmd_generic_cfg mfg_generic_cfg;
		struct mfg_cmd_tx_cont mfg_tx_cont;
		struct mfg_cmd_tx_frame2 mfg_tx_frame2;
		struct mfg_Cmd_HE_TBTx_t mfg_he_power;
		mlan_ds_misc_arb_cfg arb_cfg;
		mlan_ds_misc_cfp_tbl cfp;
		t_u8 range_ext_mode;
		mlan_ds_misc_dot11mc_unassoc_ftm_cfg dot11mc_unassoc_ftm_cfg;
		mlan_ds_misc_tp_state tp_state;
		mlan_ds_hal_phy_cfg_params hal_phy_cfg_params;
#ifdef UAP_SUPPORT
		t_u8 wacp_mode;
#endif
	} param;
} mlan_ds_misc_cfg, *pmlan_ds_misc_cfg;

/** Hotspot status enable */
#define HOTSPOT_ENABLED MBIT(0)
/** Hotspot status disable */
#define HOTSPOT_DISABLED MFALSE
/** Keep Hotspot2.0 compatible in mwu and wpa_supplicant */
#define HOTSPOT_BY_SUPPLICANT MBIT(1)

/** Reason codes */
#define MLAN_REASON_UNSPECIFIED 1
#define MLAN_REASON_PREV_AUTH_NOT_VALID 2
#define MLAN_REASON_DEAUTH_LEAVING 3
#define MLAN_REASON_DISASSOC_DUE_TO_INACTIVITY 4
#define MLAN_REASON_DISASSOC_AP_BUSY 5
#define MLAN_REASON_CLASS2_FRAME_FROM_NOAUTH_STA 6
#define MLAN_REASON_CLASS3_FRAME_FROM_NOASSOC_STA 7
#define MLAN_REASON_DISASSOC_STA_HAS_LEFT 8
#define MLAN_REASON_STA_REQ_ASSOC_WITHOUT_AUTH 9
#endif /* !_MLAN_IOCTL_H_ */
