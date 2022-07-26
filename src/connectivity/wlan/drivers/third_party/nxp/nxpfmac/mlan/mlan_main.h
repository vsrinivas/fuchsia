/** @file mlan_main.h
 *
 *  @brief This file defines the private and adapter data
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
    10/13/2008: initial version
******************************************************/

#ifndef _MLAN_MAIN_H_
#define _MLAN_MAIN_H_

#ifdef DEBUG_LEVEL1
extern t_void (*print_callback) (t_pvoid pmoal_handle, t_u32 level,
				 char *pformat, IN ...);

extern mlan_status (*get_sys_time_callback) (t_void *pmoal_handle,
					     t_u32 *psec, t_u32 *pusec);

extern t_u32 mlan_drvdbg;

#ifdef DEBUG_LEVEL2
#define PRINTM_MINFO(msg...)                                                   \
	do {                                                                   \
		if ((mlan_drvdbg & MINFO) && (print_callback))                 \
			print_callback(MNULL, MINFO, msg);                     \
	} while (0)
#define PRINTM_MWARN(msg...)                                                   \
	do {                                                                   \
		if ((mlan_drvdbg & MWARN) && (print_callback))                 \
			print_callback(MNULL, MWARN, msg);                     \
	} while (0)
#define PRINTM_MENTRY(msg...)                                                  \
	do {                                                                   \
		if ((mlan_drvdbg & MENTRY) && (print_callback))                \
			print_callback(MNULL, MENTRY, msg);                    \
	} while (0)
#define PRINTM_GET_SYS_TIME(level, psec, pusec)                                \
	do {                                                                   \
		if ((level & mlan_drvdbg) && (get_sys_time_callback))          \
			get_sys_time_callback(MNULL, psec, pusec);             \
	} while (0)

/** Hexdump for level-2 debugging */
#define HEXDUMP(x, y, z)                                                       \
	do {                                                                   \
		if ((mlan_drvdbg & (MHEX_DUMP | MINFO)) && (print_callback))   \
			print_callback(MNULL, MHEX_DUMP | MINFO, x, y, z);     \
	} while (0)

#else

#define PRINTM_MINFO(msg...)                                                   \
	do {                                                                   \
	} while (0)
#define PRINTM_MWARN(msg...)                                                   \
	do {                                                                   \
	} while (0)
#define PRINTM_MENTRY(msg...)                                                  \
	do {                                                                   \
	} while (0)

#define PRINTM_GET_SYS_TIME(level, psec, pusec)                                \
	do {                                                                   \
		if ((level & mlan_drvdbg) && (get_sys_time_callback) &&        \
		    (level != MINFO) && (level != MWARN))                      \
			get_sys_time_callback(MNULL, psec, pusec);             \
	} while (0)

/** Hexdump for debugging */
#define HEXDUMP(x, y, z)                                                       \
	do {                                                                   \
	} while (0)

#endif /* DEBUG_LEVEL2 */

#define PRINTM_MFW_D(msg...)                                                   \
	do {                                                                   \
		if ((mlan_drvdbg & MFW_D) && (print_callback))                 \
			print_callback(MNULL, MFW_D, msg);                     \
	} while (0)
#define PRINTM_MCMD_D(msg...)                                                  \
	do {                                                                   \
		if ((mlan_drvdbg & MCMD_D) && (print_callback))                \
			print_callback(MNULL, MCMD_D, msg);                    \
	} while (0)
#define PRINTM_MDAT_D(msg...)                                                  \
	do {                                                                   \
		if ((mlan_drvdbg & MDAT_D) && (print_callback))                \
			print_callback(MNULL, MDAT_D, msg);                    \
	} while (0)
#define PRINTM_MIF_D(msg...)                                                   \
	do {                                                                   \
		if ((mlan_drvdbg & MIF_D) && (print_callback))                 \
			print_callback(MNULL, MIF_D, msg);                     \
	} while (0)

#define PRINTM_MIOCTL(msg...)                                                  \
	do {                                                                   \
		if ((mlan_drvdbg & MIOCTL) && (print_callback))                \
			print_callback(MNULL, MIOCTL, msg);                    \
	} while (0)
#define PRINTM_MINTR(msg...)                                                   \
	do {                                                                   \
		if ((mlan_drvdbg & MINTR) && (print_callback))                 \
			print_callback(MNULL, MINTR, msg);                     \
	} while (0)
#define PRINTM_MEVENT(msg...)                                                  \
	do {                                                                   \
		if ((mlan_drvdbg & MEVENT) && (print_callback))                \
			print_callback(MNULL, MEVENT, msg);                    \
	} while (0)
#define PRINTM_MCMND(msg...)                                                   \
	do {                                                                   \
		if ((mlan_drvdbg & MCMND) && (print_callback))                 \
			print_callback(MNULL, MCMND, msg);                     \
	} while (0)
#define PRINTM_MDATA(msg...)                                                   \
	do {                                                                   \
		if ((mlan_drvdbg & MDATA) && (print_callback))                 \
			print_callback(MNULL, MDATA, msg);                     \
	} while (0)
#define PRINTM_MERROR(msg...)                                                  \
	do {                                                                   \
		if ((mlan_drvdbg & MERROR) && (print_callback))                \
			print_callback(MNULL, MERROR, msg);                    \
	} while (0)
#define PRINTM_MFATAL(msg...)                                                  \
	do {                                                                   \
		if ((mlan_drvdbg & MFATAL) && (print_callback))                \
			print_callback(MNULL, MFATAL, msg);                    \
	} while (0)
#define PRINTM_MMSG(msg...)                                                    \
	do {                                                                   \
		if ((mlan_drvdbg & MMSG) && (print_callback))                  \
			print_callback(MNULL, MMSG, msg);                      \
	} while (0)

#define PRINTM(level, msg...) PRINTM_##level((char *)msg)

/** Log debug message */
#ifdef __GNUC__
#define PRINTM_NETINTF(level, pmpriv)                                          \
	do {                                                                   \
		if ((mlan_drvdbg & level) && pmpriv &&                         \
		    pmpriv->adapter->callbacks.moal_print_netintf)             \
			pmpriv->adapter->callbacks.moal_print_netintf(         \
				pmpriv->adapter->pmoal_handle,                 \
				pmpriv->bss_index, level);                     \
	} while (0)
#endif /* __GNUC__ */

/** Max hex dump data length */
#define MAX_DATA_DUMP_LEN 64

/** Debug hexdump for level-1 debugging */
#define DBG_HEXDUMP(level, x, y, z)                                            \
	do {                                                                   \
		if ((mlan_drvdbg & level) && print_callback)                   \
			print_callback(MNULL, MHEX_DUMP | level, x, y, z);     \
	} while (0)

#else /* DEBUG_LEVEL1 */

#define PRINTM(level, msg...)                                                  \
	do {                                                                   \
	} while (0)

#define PRINTM_NETINTF(level, pmpriv)                                          \
	do {                                                                   \
	} while (0)

/** Debug hexdump for level-1 debugging */
#define DBG_HEXDUMP(level, x, y, z)                                            \
	do {                                                                   \
	} while (0)

/** Hexdump for debugging */
#define HEXDUMP(x, y, z)                                                       \
	do {                                                                   \
	} while (0)

#define PRINTM_GET_SYS_TIME(level, psec, pusec)                                \
	do {                                                                   \
	} while (0)

#endif /* DEBUG_LEVEL1 */

/* Reason Code 3: STA is leaving (or has left) IBSS or ESS */
#define DEF_DEAUTH_REASON_CODE (0x3)

/** Log entry point for debugging */
#define ENTER()                                                                \
	do {                                                                   \
		PRINTM(MENTRY, "Enter: %s\n", __func__);                       \
	} while (0)

/** Log exit point for debugging */
#define LEAVE()                                                                \
	do {                                                                   \
		PRINTM(MENTRY, "Leave: %s\n", __func__);                       \
	} while (0)

/** Find minimum */
#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

/** Find maximum */
#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif

#ifdef memset
#undef memset
#endif
/** Memset routine */
#define memset(adapter, s, c, len)                                             \
	(adapter->callbacks.moal_memset(adapter->pmoal_handle, s, c, len))

#ifdef memmove
#undef memmove
#endif
/** Memmove routine */
#define memmove(adapter, dest, src, len)                                       \
	(adapter->callbacks.moal_memmove(adapter->pmoal_handle, dest, src, len))

#ifdef memcpy
#undef memcpy
#endif
/** Memcpy routine */
#define memcpy(adapter, to, from, len)                                         \
	(adapter->callbacks.moal_memcpy(adapter->pmoal_handle, to, from, len))
/* memcpy_ext rountine */
#define memcpy_ext(adapter, to, from, len, size)                               \
	(adapter->callbacks.moal_memcpy_ext(adapter->pmoal_handle, to, from,   \
					    len, size))

#ifdef memcmp
#undef memcmp
#endif
/** Memcmp routine */
#define memcmp(adapter, s1, s2, len)                                           \
	(adapter->callbacks.moal_memcmp(adapter->pmoal_handle, s1, s2, len))

/** Find number of elements */
#ifndef NELEMENTS
#define NELEMENTS(x) (sizeof(x) / sizeof(x[0]))
#endif

/** SWAP: swap t_u8 */
#define SWAP_U8(a, b)                                                          \
	{                                                                      \
		t_u8 t;                                                        \
		t = a;                                                         \
		a = b;                                                         \
		b = t;                                                         \
	}

/** SWAP: swap t_u8 */
#define SWAP_U16(a, b)                                                         \
	{                                                                      \
		t_u16 t;                                                       \
		t = a;                                                         \
		a = b;                                                         \
		b = t;                                                         \
	}

/** 16 bits byte swap */
#define swap_byte_16(x)                                                        \
	((t_u16)((((t_u16)(x)&0x00ffU) << 8) | (((t_u16)(x)&0xff00U) >> 8)))

/** 32 bits byte swap */
#define swap_byte_32(x)                                                        \
	((t_u32)((((t_u32)(x)&0x000000ffUL) << 24) |                           \
		 (((t_u32)(x)&0x0000ff00UL) << 8) |                            \
		 (((t_u32)(x)&0x00ff0000UL) >> 8) |                            \
		 (((t_u32)(x)&0xff000000UL) >> 24)))

/** 64 bits byte swap */
#define swap_byte_64(x)                                                        \
	((t_u64)((t_u64)(((t_u64)(x)&0x00000000000000ffULL) << 56) |           \
		 (t_u64)(((t_u64)(x)&0x000000000000ff00ULL) << 40) |           \
		 (t_u64)(((t_u64)(x)&0x0000000000ff0000ULL) << 24) |           \
		 (t_u64)(((t_u64)(x)&0x00000000ff000000ULL) << 8) |            \
		 (t_u64)(((t_u64)(x)&0x000000ff00000000ULL) >> 8) |            \
		 (t_u64)(((t_u64)(x)&0x0000ff0000000000ULL) >> 24) |           \
		 (t_u64)(((t_u64)(x)&0x00ff000000000000ULL) >> 40) |           \
		 (t_u64)(((t_u64)(x)&0xff00000000000000ULL) >> 56)))

#ifdef BIG_ENDIAN_SUPPORT
/** Convert ulong n/w to host */
#define mlan_ntohl(x) x
/** Convert host ulong to n/w */
#define mlan_htonl(x) x
/** Convert n/w to host */
#define mlan_ntohs(x) x
/** Convert host to n/w */
#define mlan_htons(x) x
/** Convert from 16 bit little endian format to CPU format */
#define wlan_le16_to_cpu(x) swap_byte_16(x)
/** Convert from 32 bit little endian format to CPU format */
#define wlan_le32_to_cpu(x) swap_byte_32(x)
/** Convert from 64 bit little endian format to CPU format */
#define wlan_le64_to_cpu(x) swap_byte_64(x)
/** Convert to 16 bit little endian format from CPU format */
#define wlan_cpu_to_le16(x) swap_byte_16(x)
/** Convert to 32 bit little endian format from CPU format */
#define wlan_cpu_to_le32(x) swap_byte_32(x)
/** Convert to 64 bit little endian format from CPU format */
#define wlan_cpu_to_le64(x) swap_byte_64(x)

/** Convert TxPD to little endian format from CPU format */
#define endian_convert_TxPD(x)                                                 \
	{                                                                      \
		(x)->tx_pkt_length = wlan_cpu_to_le16((x)->tx_pkt_length);     \
		(x)->tx_pkt_offset = wlan_cpu_to_le16((x)->tx_pkt_offset);     \
		(x)->tx_pkt_type = wlan_cpu_to_le16((x)->tx_pkt_type);         \
		(x)->tx_control = wlan_cpu_to_le32((x)->tx_control);           \
		(x)->tx_control_1 = wlan_cpu_to_le32((x)->tx_control_1);       \
	}
/** Convert RxPD from little endian format to CPU format */
#define endian_convert_RxPD(x)                                                 \
	{                                                                      \
		(x)->rx_pkt_length = wlan_le16_to_cpu((x)->rx_pkt_length);     \
		(x)->rx_pkt_offset = wlan_le16_to_cpu((x)->rx_pkt_offset);     \
		(x)->rx_pkt_type = wlan_le16_to_cpu((x)->rx_pkt_type);         \
		(x)->seq_num = wlan_le16_to_cpu((x)->seq_num);                 \
		(x)->rx_info = wlan_le32_to_cpu((x)->rx_info);                 \

}

#else
/** Convert ulong n/w to host */
#define mlan_ntohl(x) swap_byte_32(x)
/** Convert host ulong to n/w */
#define mlan_htonl(x) swap_byte_32(x)
/** Convert n/w to host */
#define mlan_ntohs(x) swap_byte_16(x)
/** Convert host to n/w */
#define mlan_htons(x) swap_byte_16(x)
/** Do nothing */
#define wlan_le16_to_cpu(x) x
/** Do nothing */
#define wlan_le32_to_cpu(x) x
/** Do nothing */
#define wlan_le64_to_cpu(x) x
/** Do nothing */
#define wlan_cpu_to_le16(x) x
/** Do nothing */
#define wlan_cpu_to_le32(x) x
/** Do nothing */
#define wlan_cpu_to_le64(x) x

/** Convert TxPD to little endian format from CPU format */
#define endian_convert_TxPD(x)                                                 \
	do {                                                                   \
	} while (0)
/** Convert RxPD from little endian format to CPU format */
#define endian_convert_RxPD(x)                                                 \
	do {                                                                   \
	} while (0)
#endif /* BIG_ENDIAN_SUPPORT */

/** Global moal_assert_callback */
extern t_void (*assert_callback) (t_void *pmoal_handle, t_u32 cond);

/** Assertion */
#if 0
#define MASSERT(cond)                                                          \
	do {                                                                   \
		if (!(cond)) {                                                 \
			PRINTM(MFATAL, "ASSERT: %s: %i\n", __func__,           \
			       __LINE__);                                      \
			if (assert_callback) {                                 \
				assert_callback(MNULL, (t_ptr)(cond));         \
			} else {                                               \
				do {                                           \
				} while (1);                                   \
			}                                                      \
		}                                                              \
	} while (0)
#else
#define MASSERT(cond)
#endif

/** Maximum event buffer size */
#define MAX_EVENT_SIZE (3 * 1024)

#ifdef STA_SUPPORT
/** Maximum buffer size for ARP filter */
#define ARP_FILTER_MAX_BUF_SIZE 68
#endif /* STA_SUPPORT */

/** 60 seconds */
#define MRVDRV_TIMER_60S 60000
/** 10 seconds */
#define MRVDRV_TIMER_10S 10000
/** 5 seconds */
#define MRVDRV_TIMER_5S 5000
/** 3 seconds */
#define MRVDRV_TIMER_3S 3000
/** 1 second */
#define MRVDRV_TIMER_1S 1000

/** Maximum size of multicast list */
#define MRVDRV_MAX_MULTICAST_LIST_SIZE 32
/** Maximum size of channel */
#define MRVDRV_MAX_CHANNEL_SIZE 14
/** Maximum length of SSID */
#define MRVDRV_MAX_SSID_LENGTH 32
/** WEP list macros & data structures */
/** Size of key buffer in bytes */
#define MRVL_KEY_BUFFER_SIZE_IN_BYTE 16
/** Maximum length of WPA key */
#define MRVL_MAX_KEY_WPA_KEY_LENGTH 32

/** Default listen interval */
#define MLAN_DEFAULT_LISTEN_INTERVAL 20

/** Maximum number of region codes */
#define MRVDRV_MAX_REGION_CODE 9

/** Maximum number of CFP codes for BG */
#define MRVDRV_MAX_CFP_CODE_BG 0
/** Maximum number of CFP codes for A */
#define MRVDRV_MAX_CFP_CODE_A 5

/** high rx pending packets */
#define HIGH_RX_PENDING 1000
/** low rx pending packets */
#define LOW_RX_PENDING 800

/** Default region code */
#define MRVDRV_DEFAULT_REGION_CODE 0x10
/** Default country code */
#define MRVDRV_DEFAULT_COUNTRY_CODE "US"

/** Japan country code */
#define COUNTRY_CODE_JP_40 0x40
/** Japan special country code */
#define COUNTRY_CODE_JP_FF 0xFF

/** Default factor for calculating beacon average */
#define DEFAULT_BCN_AVG_FACTOR 8
/** Default factor for calculating data average */
#define DEFAULT_DATA_AVG_FACTOR 8

/** The first valid channel for use */
#define FIRST_VALID_CHANNEL 0xff
/** Default Ad-Hoc channel */
#define DEFAULT_AD_HOC_CHANNEL 6
/** Default Ad-Hoc channel A */
#define DEFAULT_AD_HOC_CHANNEL_A 36

/** Number of WEP keys */
#define MRVL_NUM_WEP_KEY (4)

/** Default multiple DTIM */
#define MRVDRV_DEFAULT_MULTIPLE_DTIM 1

/** Default beacon missing timeout */
#define DEFAULT_BCN_MISS_TIMEOUT 10

/** Maximum buffer space for beacons retrieved from scan responses */
#define MAX_SCAN_BEACON_BUFFER 102400
/** Default buffer space for beacons retrieved from scan responses */
#define DEFAULT_SCAN_BEACON_BUFFER 4096

/**
 * @brief Buffer pad space for newly allocated beacons/probe responses
 *
 * Beacons are typically 6 bytes longer than an equivalent probe response.
 *  For each scan response stored, allocate an extra byte pad at the end to
 *  allow easy expansion to store a beacon in the same memory a probe response
 *  previously contained
 */
#define SCAN_BEACON_ENTRY_PAD 6

/** Scan time specified in the channel TLV
 *  for each channel for passive scans
 */
#define MRVDRV_PASSIVE_SCAN_CHAN_TIME 200

/** Scan time specified in the channel TLV
 *  for each channel for active scans
 */
#define MRVDRV_ACTIVE_SCAN_CHAN_TIME 200

/** Scan time specified in the channel TLV
 *  for each channel for specific scans
 */
#define MRVDRV_SPECIFIC_SCAN_CHAN_TIME 110

/**
 * Max total scan time in milliseconds
 * The total scan time should be less than scan command timeout value (20s)
 */
#define MRVDRV_MAX_TOTAL_SCAN_TIME (MRVDRV_TIMER_10S * 2 - MRVDRV_TIMER_1S)

/** Offset for GTK as it has version to skip past for GTK */
#define RSN_GTK_OUI_OFFSET 2

/** If OUI is not found */
#define MLAN_OUI_NOT_PRESENT 0
/** If OUI is found */
#define MLAN_OUI_PRESENT 1

/** Is cmd_resp, event or data packet received? */
#define IS_CARD_RX_RCVD(adapter)                                               \
	(adapter->cmd_resp_received || adapter->event_received ||              \
	 adapter->data_received)
#ifdef USB
/** Type length */
#define MLAN_TYPE_LEN 4
/** Type Command */
#define MLAN_USB_TYPE_CMD 0xF00DFACE
/** Type VDLL */
#define MLAN_USB_TYPE_VDLL 0xF00DC0DE
/** Type Data */
#define MLAN_USB_TYPE_DATA 0xBEADC0DE
/** Type Event */
#define MLAN_USB_TYPE_EVENT 0xBEEFFACE
#endif /* USB */
/** Type command */
#define MLAN_TYPE_CMD 1
/** Type data */
#define MLAN_TYPE_DATA 0
/** Type event */
#define MLAN_TYPE_EVENT 3
/** Type vdll */
#define MLAN_TYPE_VDLL 4
#ifdef SDIO
/** Type single port aggr data */
#define MLAN_TYPE_SPA_DATA 10
/** OFFSET of 512 block number */
#define OFFSET_OF_BLOCK_NUMBER 15
/** OFFSET of SDIO Header */
#define OFFSET_OF_SDIO_HEADER 28
/** sdio max rx size for cmd53, 255 * 256, reserve 1 block for DMA alignment */
#define SDIO_CMD53_MAX_SIZE 65280
#define MAX_SUPPORT_AMSDU_SIZE 4096
/** Maximum numbfer of registers to read for multiple port */
#if defined(SD8887) || defined(SD8997) || defined(SD8977) || defined(SD8987) || defined(SD9098) || defined(SD9097) || defined(SD8978) || defined(SD9177)
#define MAX_MP_REGS 196
#else
/* upto 0xB7 */
#define MAX_MP_REGS 184
#endif
/** Maximum port */
#define MAX_PORT 32
/** Maximum port 16 */
#define MAX_PORT_16	(16)

/** max MP REGS */
#define MAX_MP_REGS_MAX (196)

/** Multi port TX aggregation buffer size */
#define SDIO_MP_TX_AGGR_DEF_BUF_SIZE (65280)	/* 64K - 256 */

/** Multi port RX aggregation buffer size */
#define SDIO_MP_RX_AGGR_DEF_BUF_SIZE (65280)	/* 64K - 256 */

#endif /* SDIO */

/** Minimum BA threshold */
#define MIN_BA_THRESHOLD 16

/** High threshold at which to start drop packets */
#define RX_HIGH_THRESHOLD 1024
/** Low threshold to allow Rx BA */
#define RX_LOW_THRESHOLD 128

#define MFG_CMD_SET_TEST_MODE   1
#define MFG_CMD_UNSET_TEST_MODE 0
#define MFG_CMD_TX_ANT          0x1004
#define MFG_CMD_RX_ANT          0x1005
#define MFG_CMD_TX_CONT         0x1009
#define MFG_CMD_RF_CHAN         0x100A
#define MFG_CMD_CLR_RX_ERR      0x1010
#define MFG_CMD_TX_FRAME        0x1021
#define MFG_CMD_RF_BAND_AG      0x1034
#define MFG_CMD_RF_CHANNELBW    0x1044
#define MFG_CMD_RADIO_MODE_CFG  0x1211
#define MFG_CMD_CONFIG_MAC_HE_TB_TX 0x110A

/** Debug command number */
#define DBG_CMD_NUM 10

/** Info for debug purpose */
typedef struct _wlan_dbg {
	/** Number of host to card command failures */
	t_u32 num_cmd_host_to_card_failure;
	/** Number of host to card sleep confirm failures */
	t_u32 num_cmd_sleep_cfm_host_to_card_failure;
	/** Number of host to card Tx failures */
	t_u32 num_tx_host_to_card_failure;
	/** Number of card to host command/event failures */
	t_u32 num_cmdevt_card_to_host_failure;
	/** Number of card to host Rx failures */
	t_u32 num_rx_card_to_host_failure;
	/** Number of interrupt read failures */
	t_u32 num_int_read_failure;
	/** Last interrupt status */
	t_u32 last_int_status;
	/** Number of allocate buffer failure */
	t_u32 num_alloc_buffer_failure;
	/** Number of pkt dropped */
	t_u32 num_pkt_dropped;
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
	/** Number of consecutive association command failures */
	t_u32 num_cons_assoc_failure;

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
} wlan_dbg;

/** Hardware status codes */
typedef enum _WLAN_HARDWARE_STATUS {
	WlanHardwareStatusReady,
	WlanHardwareStatusGetHwSpec,
	WlanHardwareStatusGetHwSpecdone,
	WlanHardwareStatusInitializing,
	WlanHardwareStatusInitdone,
	WlanHardwareStatusReset,
	WlanHardwareStatusClosing,
	WlanHardwareStatusNotReady
} WLAN_HARDWARE_STATUS;

/** WLAN_802_11_POWER_MODE */
typedef enum _WLAN_802_11_POWER_MODE {
	Wlan802_11PowerModeCAM,
	Wlan802_11PowerModePSP
} WLAN_802_11_POWER_MODE;

/** tx param */
typedef struct _mlan_tx_param {
	/** next packet length */
	t_u32 next_pkt_len;
} mlan_tx_param;

/** PS_STATE */
typedef enum _PS_STATE {
	PS_STATE_AWAKE,
	PS_STATE_PRE_SLEEP,
	PS_STATE_SLEEP_CFM,
	PS_STATE_SLEEP
} PS_STATE;

/** Minimum flush timer for win size of 1 is 50 ms */
#define MIN_FLUSH_TIMER_MS 50
/** Minimum flush timer for win size of 1 is 15 ms */
#define MIN_FLUSH_TIMER_15_MS 15

/** Tx BA stream table */
typedef struct _TxBAStreamTbl TxBAStreamTbl;

/** Add BA parameter data structure */
typedef struct {
	/** Window size for initiator */
	t_u32 tx_win_size;
	/** Window size for receiver */
	t_u32 rx_win_size;
	/** Block ack timeout */
	t_u32 timeout;
	/** amsdu support for ADDBA request */
	t_u8 tx_amsdu;
	/** amsdu support for ADDBA response */
	t_u8 rx_amsdu;
} add_ba_param_t;

/** Tx aggregation data structure */
typedef struct _txAggr_t {
	/** AMPDU user */
	t_u8 ampdu_user;
	/** AMPDU AP */
	t_u8 ampdu_ap;
	/** AMSDU */
	t_u8 amsdu;
} tx_aggr_t;

/** del ba threshold */
#define DEL_BA_THRESHOLD 10
/** BA stream status */
typedef enum _baStatus_e {
	BA_STREAM_NOT_SETUP = 0,
	BA_STREAM_SETUP_INPROGRESS,
	BA_STREAM_SETUP_COMPLETE
} baStatus_e;

/** RA list table */
typedef struct _raListTbl raListTbl, *praListTbl;

/** RA list table */
struct _raListTbl {
	/** Pointer to previous node */
	raListTbl *pprev;
	/** Pointer to next node */
	raListTbl *pnext;
	/** Buffer list head */
	mlan_list_head buf_head;
	/** RA list buffer */
	t_u8 ra[MLAN_MAC_ADDR_LENGTH];
	/** total packets in RA list */
	t_u16 total_pkts;
	/** packets received */
	t_u16 packet_count;
	/** packet count threshold to setup BA */
	t_u8 ba_packet_threshold;
	/** is 11n enabled */
	t_u8 is_11n_enabled;
	/** max amsdu size */
	t_u16 max_amsdu;
	/** BA stream status */
	baStatus_e ba_status;
	/** del ba count */
	t_u8 del_ba_count;
	/** amsdu in ampdu flag */
	t_u8 amsdu_in_ampdu;
	/** tdls flag */
	t_u8 is_tdls_link;
	/** tx_pause flag */
	t_u8 tx_pause;
};

/** TID table */
typedef struct _tidTbl {
	/** RA list head */
	mlan_list_head ra_list;
	/** Current RA list */
	raListTbl *ra_list_curr;
} tid_tbl_t;

/** Highest priority setting for a packet (uses voice AC) */
#define WMM_HIGHEST_PRIORITY 7
/** Highest priority TID  */
#define HIGH_PRIO_TID 7
/** Lowest priority TID  */
#define LOW_PRIO_TID 0
/** No packet priority (< lowest) */
#define NO_PKT_PRIO_TID -1

/** Max driver packet delay in msec */
#define WMM_DRV_DELAY_MAX 510

/** Struct of WMM DESC */
typedef struct _wmm_desc {
	/** TID table */
	tid_tbl_t tid_tbl_ptr[MAX_NUM_TID];
	/** Packets out */
	t_u32 packets_out[MAX_NUM_TID];
	/** Packets queued */
	t_u32 pkts_queued[MAX_NUM_TID];
	/** Packets paused */
	t_u32 pkts_paused[MAX_NUM_TID];
	/** Spin lock to protect ra_list */
	t_void *ra_list_spinlock;

	/** AC status */
	WmmAcStatus_t ac_status[MAX_AC_QUEUES];
	/** AC downgraded values */
	mlan_wmm_ac_e ac_down_graded_vals[MAX_AC_QUEUES];

	/** Max driver packet delay sent to the firmware for expiry eval */
	t_u32 drv_pkt_delay_max;

	/** WMM queue priority table */
	t_u8 queue_priority[MAX_AC_QUEUES];
	/** User priority packet transmission control */
	t_u32 user_pri_pkt_tx_ctrl[WMM_HIGHEST_PRIORITY + 1];	/* UP: 0 to 7 */

	/** Number of transmit packets queued */
	mlan_scalar tx_pkts_queued;
	/** Tracks highest priority with a packet queued */
	mlan_scalar highest_queued_prio;
} wmm_desc_t;

/** Security structure */
typedef struct _wlan_802_11_security_t {
	/** WPA enabled flag */
	t_u8 wpa_enabled;
	/** E-Supplicant enabled flag */
	t_u8 ewpa_enabled;
	/** WPA2 enabled flag */
	t_u8 wpa2_enabled;
	/** WAPI enabled flag */
	t_u8 wapi_enabled;
	/** WAPI key on flag */
	t_u8 wapi_key_on;
	/** WEP status */
	WLAN_802_11_WEP_STATUS wep_status;
	/** Authentication mode */
	t_u32 authentication_mode;
	/** Encryption mode */
	t_u32 encryption_mode;
	/** Hotspot OSEN enabled */
	t_u8 osen_enabled;
} wlan_802_11_security_t;

/** Current Basic Service Set State Structure */
typedef struct {
	/** BSS descriptor */
	BSSDescriptor_t bss_descriptor;
	/** WMM enable? */
	t_u8 wmm_enabled;
	/** Uapsd enable?*/
	t_u8 wmm_uapsd_enabled;
	/** Band */
	t_u8 band;
	/** Number of rates supported */
	t_u32 num_of_rates;
	/** Supported rates*/
	t_u8 data_rates[WLAN_SUPPORTED_RATES];
	/** Host MLME flag*/
	t_u8 host_mlme;
	mlan_802_11_mac_addr prev_bssid;
	t_u8 use_mfp;
} current_bss_params_t;

/** Sleep_params */
typedef struct _sleep_params_t {
	/** Sleep parameter error */
	t_u16 sp_error;
	/** Sleep parameter offset */
	t_u16 sp_offset;
	/** Sleep parameter stable time */
	t_u16 sp_stable_time;
	/** Sleep parameter calibration control */
	t_u8 sp_cal_control;
	/** Sleep parameter external sleep clock */
	t_u8 sp_ext_sleep_clk;
	/** Sleep parameter reserved */
	t_u16 sp_reserved;
} sleep_params_t;

/** Sleep_period */
typedef struct sleep_period_t {
	/** Sleep period */
	t_u16 period;
	/** Reserved */
	t_u16 reserved;
} sleep_period_t;

/** mrvl_wep_key_t */
typedef struct _mrvl_wep_key_t {
	/** Length */
	t_u32 length;
	/** WEP key index */
	t_u32 key_index;
	/** WEP key length */
	t_u32 key_length;
	/** WEP keys */
	t_u8 key_material[MRVL_KEY_BUFFER_SIZE_IN_BYTE];
} mrvl_wep_key_t;

/** Maximum number of region channel */
#define MAX_REGION_CHANNEL_NUM 2

/** Region-band mapping table */
typedef struct _region_chan_t {
	/** TRUE if this entry is valid */
	t_u8 valid;
	/** Region code for US, Japan ... */
	t_u8 region;
	/** Band B/G/A, used for BAND_CONFIG cmd */
	t_u8 band;
	/** Actual No. of elements in the array below */
	t_u8 num_cfp;
	/** chan-freq-txpower mapping table */
	chan_freq_power_t *pcfp;
} region_chan_t;

/** State of 11d */
typedef enum _state_11d_t {
	DISABLE_11D = 0,
	ENABLE_11D = 1,
} state_11d_t;

#define DEFAULT_11D_STATE DISABLE_11D

/** Domain regulatory information */
typedef struct _wlan_802_11d_domain_reg {
	/** Country Code */
	t_u8 country_code[COUNTRY_CODE_LEN];
	/** band that channels in sub_band belong to */
	t_u8 band;
	/** No. of subband in below */
	t_u8 no_of_sub_band;
	/** Subband data to send/last sent */
	IEEEtypes_SubbandSet_t sub_band[MRVDRV_MAX_SUBBAND_802_11D];
} wlan_802_11d_domain_reg_t;

/** Data for state machine */
typedef struct _wlan_802_11d_state {
	/** True for enabling 11D */
	state_11d_t enable_11d;
	/** True for user enabling 11D */
	state_11d_t user_enable_11d;
} wlan_802_11d_state_t;

/** 802.11h State information kept in the 'mlan_private' driver structure */
typedef struct {
	/** Indicate 11h is enabled from host */
	t_bool is_11h_host;
	/** Indicates whether 11h is enabled in the driver */
	t_bool is_11h_enabled;
	/** Indicates whether 11h is active in the firmware */
	t_bool is_11h_active;
	/** Master device using automatic channel select */
	t_bool adhoc_auto_sel_chan;
	/** Set when driver receives a STOP TX event from fw */
	t_bool tx_disabled;
	/** Channel that ChanSwAnn was received for, non-zero = active */
	t_u8 dfs_slave_csa_chan;
	/** Expiry for above variable, seconds in system time */
	t_u32 dfs_slave_csa_expire_at_sec;
} wlan_11h_interface_state_t;

#if defined(UAP_SUPPORT)
/** UAP get info callback state kept in the 'mlan_private' driver structure */
typedef struct {
	/** UAP internal callback after wlan_uap_get_channel */
	/**  (parameter is really pointer to mlan_private)   */
	mlan_status (*get_chan_callback) (t_void *);
	/** current ioctl_req (to be completed in callback) */
	pmlan_ioctl_req pioctl_req_curr;
	/** band config from MrvlIEtypes_channel_band_t */
	Band_Config_t bandcfg;
	/** channel from MrvlIEtypes_channel_band_t */
	t_u8 channel;
	/** beacon period (in msec) from MrvlIEtypes_beacon_period_t */
	t_u16 beacon_period;
	/** dtim period (no unit) from MrvlIEtypes_dtim_period_t */
	t_u8 dtim_period;
} wlan_uap_get_info_cb_t;
#endif

/** Data structure for WPS information */
typedef struct {
	/** WPS IE */
	IEEEtypes_VendorSpecific_t wps_ie;
	/** Session enable flag */
	t_u8 session_enable;
} wps_t;

/** mlan_operations data structure */
typedef struct _mlan_operations {
	/** cmd init handler */
	mlan_status (*init_cmd) (t_void *priv, t_u8 first_bss);
	/** ioctl handler */
	mlan_status (*ioctl) (t_void *adapter, pmlan_ioctl_req pioctl_req);
	/** cmd handler */
	mlan_status (*prepare_cmd) (t_void *priv, t_u16 cmd_no,
				    t_u16 cmd_action, t_u32 cmd_oid,
				    t_void *pioctl_buf, t_void *pdata_buf,
				    t_void *pcmd_buf);
	/** cmdresp handler */
	mlan_status (*process_cmdresp) (t_void *priv, t_u16 cmdresp_no,
					t_void *pcmd_buf, t_void *pioctl);
	/** rx handler */
	mlan_status (*process_rx_packet) (t_void *adapter, pmlan_buffer pmbuf);
	/** event handler */
	mlan_status (*process_event) (t_void *priv);
	/** txpd handler */
	t_void *(*process_txpd) (t_void *priv, pmlan_buffer pmbuf);
	/** BSS role */
	mlan_bss_role bss_role;
} mlan_operations, *pmlan_operations;

/** Private structure for MLAN */
typedef struct _mlan_private {
	/** Pointer to mlan_adapter */
	struct _mlan_adapter *adapter;
	/** BSS index */
	t_u8 bss_index;
	/** BSS type */
	t_u8 bss_type;
	/** BSS role */
	t_u8 bss_role;
	/** BSS virtual flag */
	t_u8 bss_virtual;
	/** BSS Priority */
	t_u8 bss_priority;
	/** BSS number */
	t_u8 bss_num;
	/** Frame type */
	t_u8 frame_type;
	/** MAC address information */
	t_u8 curr_addr[MLAN_MAC_ADDR_LENGTH];
	/** Media connection status */
	t_bool media_connected;

	/** Current packet filter */
	t_u32 curr_pkt_filter;
	/** Infrastructure mode */
	t_u32 bss_mode;

	/** Tx packet control */
	t_u32 pkt_tx_ctrl;

	/** Tx power level */
	t_s16 tx_power_level;
	/** Maximum Tx power level */
	t_s8 max_tx_power_level;
	/** Minimum Tx power level */
	t_s8 min_tx_power_level;
	/** Tx rate */
	t_u8 tx_rate;
	t_u8 tx_rate_info;
	/*HE tx tone mode and DCM info */
	t_u8 ext_tx_rate_info;
	/*HE rx tone mode and DCM info */
	t_u8 rxpd_rx_info;
	/** rxpd_htinfo */
	t_u8 rxpd_rate_info;
	/** max amsdu size */
	t_u16 max_amsdu;
	/** amsdu disable flag */
	t_u8 amsdu_disable;
	/** 802.11n Device Capabilities for 2.4GHz */
	t_u32 usr_dot_11n_dev_cap_bg;
	/** 802.11n Device Capabilities for 5GHz */
	t_u32 usr_dot_11n_dev_cap_a;
	/** MIMO abstraction of MCSs supported by device */
	t_u8 usr_dev_mcs_support;
#ifdef UAP_SUPPORT
	/** UAP 11n flag */
	t_u8 is_11n_enabled;
#endif				/* UAP_SUPPORT */
	/** UAP 11ac flag */
	t_u8 is_11ac_enabled;
	/** UAP 11ax flag */
	t_u8 is_11ax_enabled;
	/** tx vht_info */
	t_u8 tx_vhtinfo;
	/** rxpd_vhtinfo */
	t_u8 rxpd_vhtinfo;
	/** 802.11ac Device Capabilities for 2.4GHz */
	t_u32 usr_dot_11ac_dev_cap_bg;
	/** 802.11ac Device Capabilities for 5GHz */
	t_u32 usr_dot_11ac_dev_cap_a;
	/** MIMO abstraction of MCSs supported by device */
	t_u32 usr_dot_11ac_mcs_support;
	/** user dot 11ac_BW */
	t_u8 usr_dot_11ac_bw;
	/** user dot 11ac_opermode_BW */
	t_u8 usr_dot_11ac_opermode_bw;
	/** user dot 11ac_opermode_nss */
	t_u8 usr_dot_11ac_opermode_nss;
	/** length of hw he capability */
	t_u8 user_hecap_len;
	/** user configured 802.11ax HE capability */
	t_u8 user_he_cap[54];
	/** length of hw he capability */
	t_u8 user_2g_hecap_len;
	/** user configured 802.11ax HE capability */
	t_u8 user_2g_he_cap[54];
	/**  dropped pkts */
	t_u32 num_drop_pkts;
#ifdef UAP_SUPPORT
	/** packet forward control */
	t_u8 pkt_fwd;
#endif
	/** TX beamforming capability */
	t_u32 tx_bf_cap;
	/** Rx PD rate */
	t_u8 rxpd_rate;
	/** Bitmap rates */
	t_u16 bitmap_rates[MAX_BITMAP_RATES_SIZE];
	/** Data rate */
	t_u32 data_rate;
	/** Automatic data rate flag */
	t_u8 is_data_rate_auto;
	/** Factor for calculating beacon average */
	t_u16 bcn_avg_factor;
	/** Factor for calculating data average */
	t_u16 data_avg_factor;
	/** SNR */
	t_s8 snr;
	/** Noise Floor */
	t_s8 nf;
	/** Last data RSSI */
	t_s16 data_rssi_last;
	/** Last data Noise Floor */
	t_s16 data_nf_last;
	/** Average data RSSI */
	t_s16 data_rssi_avg;
	/** Averag data Noise Floor */
	t_s16 data_nf_avg;
	/** Last beacon RSSI */
	t_s16 bcn_rssi_last;
	/** Last beacon Noise Floor */
	t_s16 bcn_nf_last;
	/** Average beacon RSSI */
	t_s16 bcn_rssi_avg;
	/** Average beacon Noise Floor */
	t_s16 bcn_nf_avg;
	/** Attempted BSS descriptor */
	BSSDescriptor_t *pattempted_bss_desc;

	/** GTK rekey data*/
	mlan_ds_misc_gtk_rekey_data gtk_rekey;

	/** Current SSID/BSSID related parameters*/
	current_bss_params_t curr_bss_params;
	/** current channel flags */
	t_u32 curr_chan_flags;
	/** User selected bands */
	t_u16 config_bands;

	/** Beacon period */
	t_u16 beacon_period;
	/** Listen interval */
	t_u16 listen_interval;
	/** ATIM window */
	t_u16 atim_window;

	/** AdHoc channel */
	t_u8 adhoc_channel;
	/** AdHoc link sensed flag */
	t_u8 adhoc_is_link_sensed;
	/** AdHoc operating state */
	t_u8 adhoc_state;
#if defined(STA_SUPPORT)
	/** AdHoc operating state backup */
	t_u8 adhoc_state_prev;
	/** AdHoc previous ssid used for Start */
	mlan_802_11_ssid adhoc_last_start_ssid;
#endif
	/** FSM variable for 11d support */
	wlan_802_11d_state_t state_11d;
	/** FSM variable for 11h support */
	wlan_11h_interface_state_t intf_state_11h;
#ifdef UAP_SUPPORT
	/** Whether UAP interface has started */
	t_bool uap_bss_started;
	/** Whether UAP interface start from hostapd */
	t_bool uap_host_based;
	/**UAP operating channel*/
	t_u8 uap_channel;
	/** state variable for UAP Get Info callback */
	wlan_uap_get_info_cb_t uap_state_chan_cb;
#endif				/* UAP_SUPPORT */

	/** Security related */
	/** Encryption parameter */
	wlan_802_11_security_t sec_info;
	/** WEP keys */
	mrvl_wep_key_t wep_key[MRVL_NUM_WEP_KEY];
	/** Current WEP key index */
	t_u16 wep_key_curr_index;
	/** EWPA query 0: disable, 1: enable */
	t_u8 ewpa_query;
	/** Encryption Key*/
	t_u8 wpa_ie[256];
	/** WPA IE length */
	t_u8 wpa_ie_len;
	/** GTK set flag */
	t_u8 wpa_is_gtk_set;
	/** AES key material */
	mlan_ds_encrypt_key aes_key;
#if defined(STA_SUPPORT)
	/* Mgmt Frame Protection config */
	mlan_ds_misc_pmfcfg pmfcfg;
#endif
	/** WAPI IE */
	t_u8 wapi_ie[256];
	/** WAPI IE length */
	t_u8 wapi_ie_len;
	/** OSEN IE */
	t_u8 osen_ie[256];
	/** OSEN IE length */
	t_u8 osen_ie_len;
	/** Pointer to the station table */
	mlan_list_head sta_list;
	/** tdls pending queue */
	mlan_list_head tdls_pending_txq;
	t_u16 tdls_idle_time;

	/** MGMT IE */
	custom_ie mgmt_ie[MAX_MGMT_IE_INDEX];
	/** mgmt frame passthru mask */
	t_u32 mgmt_frame_passthru_mask;
	/** WMM required */
	t_u8 wmm_required;
	/** WMM enabled */
	t_u8 wmm_enabled;
	/** WMM qos info */
	t_u8 wmm_qosinfo;
	/** saved WMM qos info */
	t_u8 saved_wmm_qosinfo;
	/**host tdls uapsd support*/
	t_u8 host_tdls_uapsd_support;
	/**host tdls channel switch support*/
	t_u8 host_tdls_cs_support;
	/**supported channel IE len*/
	t_u8 chan_supp_len;
	/**save channel support IE*/
	t_u8 chan_supp[MAX_IE_SIZE];
	/**supported regulatory classl IE len*/
	t_u8 supp_regulatory_class_len;
	/**save support channel regulatory class IE*/
	t_u8 supp_regulatory_class[MAX_IE_SIZE];
	/**tdls cs off channel*/
	t_u8 tdls_cs_channel;
	/** WMM related variable*/
	wmm_desc_t wmm;

	/** Pointer to the Transmit BA stream table*/
	mlan_list_head tx_ba_stream_tbl_ptr;
	/** Pointer to the priorities for AMSDU/AMPDU table*/
	tx_aggr_t aggr_prio_tbl[MAX_NUM_TID];
	/** Pointer to the priorities for AMSDU/AMPDU table*/
	t_u8 addba_reject[MAX_NUM_TID];
	/** Pointer to the priorities for AMSDU/AMPDU table*/
	t_u8 ibss_ampdu[MAX_NUM_TID];
	/** Pointer to the priorities for AMSDU/AMPDU table*/
	t_u8 ibss_addba_reject[MAX_NUM_TID];
	/** Struct to store ADDBA parameters */
	add_ba_param_t add_ba_param;
	/**  user rx_win_size */
	t_u32 user_rxwinsize;
	/** last rx_seq */
	t_u16 rx_seq[MAX_NUM_TID];
	/** Pointer to the Receive Reordering table*/
	mlan_list_head rx_reorder_tbl_ptr;
	/** Lock for Rx packets */
	t_void *rx_pkt_lock;

#ifdef STA_SUPPORT
	/** Buffer to store the association response for application retrieval
	 */
	t_u8 assoc_rsp_buf[MRVDRV_ASSOC_RSP_BUF_SIZE];
	/** Length of the data stored in assoc_rsp_buf */
	t_u32 assoc_rsp_size;

	/** Generic IEEE IEs passed from the application to be inserted into the
	 *    association request to firmware
	 */
	t_u8 gen_ie_buf[MRVDRV_GENIE_BUF_SIZE];
	/** Length of the data stored in gen_ie_buf */
	t_u8 gen_ie_buf_len;

	/** disconnect reason code*/
	t_u16 disconnect_reason_code;
	t_u8 *pcurr_bcn_buf;
	t_u32 curr_bcn_size;
	t_void *curr_bcn_buf_lock;

	/** WPS */
	wps_t wps;
#endif				/* STA_SUPPORT */

	/** function table */
	mlan_operations ops;
	/** tx pause flag */
	t_u8 tx_pause;
	/** Port Control mode */
	t_u8 port_ctrl_mode;

	/** Port open flag */
	t_u8 port_open;

	/** Port open flag state at time of association attempt */
	t_u8 prior_port_status;
	/** Bypass TX queue */
	mlan_list_head bypass_txq;
	/** IP address operation */
	t_u32 op_code;
	/** IP address */
	t_u8 ip_addr[IPADDR_LEN];
	t_u32 hotspot_cfg;
#ifdef STA_SUPPORT
	ExtCap_t ext_cap;
	ExtCap_t def_ext_cap;
#endif
	/** interface header len */
	t_u8 intf_hr_len;
#ifdef USB
	/** USB data port */
	t_u32 port;
#endif
	/** Control TX AMPDU on infra link */
	t_u8 txaggrctrl;
#if defined(DRV_EMBEDDED_AUTHENTICATOR) || defined(DRV_EMBEDDED_SUPPLICANT)
	t_void *psapriv;
#endif
	/** rx per packet info */
	t_u8 rx_pkt_info;
	/** received amsdu count*/
	t_u32 amsdu_rx_cnt;
	/** received msdu count in amsdu*/
	t_u32 msdu_in_rx_amsdu_cnt;
	/** tx amsdu count*/
	t_u32 amsdu_tx_cnt;
	/** tx msdu count in amsdu*/
	t_u32 msdu_in_tx_amsdu_cnt;
} mlan_private, *pmlan_private;

typedef struct _assoc_logger {
	/** vendor specific */
	t_u8 oui[3];
	t_u8 bssid[MLAN_MAC_ADDR_LENGTH];
	t_u8 ssid[MLAN_MAX_SSID_LENGTH];
	t_s32 rssi;
	t_u32 channel;
} assoc_logger_data;

/** Tx BA stream table */
struct _TxBAStreamTbl {
	/** TxBAStreamTbl previous node */
	TxBAStreamTbl *pprev;
	/** TxBAStreamTbl next node */
	TxBAStreamTbl *pnext;
	/** TID */
	int tid;
	/** RA */
	t_u8 ra[MLAN_MAC_ADDR_LENGTH];
	/** BA stream status */
	baStatus_e ba_status;
	t_u8 amsdu;
};

/** RX reorder table */
typedef struct _RxReorderTbl RxReorderTbl;

typedef struct {
	/** Timer for flushing */
	t_void *timer;
	/** Timer set flag */
	t_u8 timer_is_set;
	/** RxReorderTbl ptr */
	RxReorderTbl *ptr;
	/** Priv pointer */
	mlan_private *priv;
} reorder_tmr_cnxt_t;

/** RX reorder table */
struct _RxReorderTbl {
	/** RxReorderTbl previous node */
	RxReorderTbl *pprev;
	/** RxReorderTbl next node */
	RxReorderTbl *pnext;
	/** TID */
	int tid;
	/** TA */
	t_u8 ta[MLAN_MAC_ADDR_LENGTH];
	/** Start window */
	int start_win;
	/** last_seq */
	int last_seq;
	/** Window size */
	int win_size;
	/** Pointer to pointer to RxReorderTbl */
	t_void **rx_reorder_ptr;
	/** Timer context */
	reorder_tmr_cnxt_t timer_context;
	/** BA stream status */
	baStatus_e ba_status;
	t_u8 amsdu;
	/** no packet drop flag for rx_reorder_tbl */
	t_u8 force_no_drop;
	/** flag for check start win */
	t_u8 check_start_win;
	/** pkt receive after BA setup */
	t_u8 pkt_count;
	/** flush data flag */
	t_u8 flush_data;
};

/** BSS priority node */
typedef struct _mlan_bssprio_node mlan_bssprio_node;

/** BSS priority node */
struct _mlan_bssprio_node {
	/** Pointer to previous node */
	mlan_bssprio_node *pprev;
	/** Pointer to next node */
	mlan_bssprio_node *pnext;
	/** Pointer to priv */
	pmlan_private priv;
};

/** BSS priority table */
typedef struct _mlan_bssprio_tbl mlan_bssprio_tbl;

/** BSS priority table */
struct _mlan_bssprio_tbl {
	/** BSS priority list head */
	mlan_list_head bssprio_head;
	/** Current priority node */
	mlan_bssprio_node *bssprio_cur;
};

/** cmd_ctrl_node */
typedef struct _cmd_ctrl_node cmd_ctrl_node;

/** _cmd_ctrl_node */
struct _cmd_ctrl_node {
	/** Pointer to previous node */
	cmd_ctrl_node *pprev;
	/** Pointer to next node */
	cmd_ctrl_node *pnext;
	/** Pointer to priv */
	pmlan_private priv;
	/** Command number */
	t_u32 cmd_no;
	/** Command flag */
	t_u32 cmd_flag;
	/** Pointer to mlan_buffer */
	mlan_buffer *cmdbuf;
	/** Pointer to mlan_buffer */
	mlan_buffer *respbuf;
	/** Command parameter */
	t_void *pdata_buf;
	/** Pointer to mlan_ioctl_req if command is from IOCTL */
	t_void *pioctl_buf;
#if defined(PCIE) || defined(SDIO)
	/** pre_allocated mlan_buffer for cmd */
	mlan_buffer *pmbuf;
#endif
};

/** default tdls wmm qosinfo */
#define DEFAULT_TDLS_WMM_QOS_INFO 15
/** default tdls sleep period */
#define DEFAULT_TDLS_SLEEP_PERIOD 30

/** TDLS status */
typedef enum _tdlsStatus_e {
	TDLS_NOT_SETUP = 0,
	TDLS_SETUP_INPROGRESS,
	TDLS_SETUP_COMPLETE,
	TDLS_SETUP_FAILURE,
	TDLS_TEAR_DOWN,
	TDLS_SWITCHING_CHANNEL,
	TDLS_IN_BASE_CHANNEL,
	TDLS_IN_OFF_CHANNEL,
} tdlsStatus_e;
/** station node */
typedef struct _sta_node sta_node, *psta_node;

/** station node*/
struct _sta_node {
	/** previous node */
	sta_node *pprev;
	/** next node */
	sta_node *pnext;
	/** station mac address */
	t_u8 mac_addr[MLAN_MAC_ADDR_LENGTH];
	/** wmm flag */
	t_u8 is_wmm_enabled;
	/** 11n flag */
	t_u8 is_11n_enabled;
	/** AMPDU STA */
	t_u8 ampdu_sta[MAX_NUM_TID];
	/** last rx_seq */
	t_u16 rx_seq[MAX_NUM_TID];
	/** max amsdu size */
	t_u16 max_amsdu;
	/** HT cap */
	IEEEtypes_HTCap_t HTcap;
	/** 11ac flag */
	t_u8 is_11ac_enabled;
	/** UAP 11ax flag */
	t_u8 is_11ax_enabled;
	IEEEtypes_HECap_t he_cap;
	/** SNR */
	t_s8 snr;
	/** Noise Floor */
	t_s8 nf;
	/** peer capability */
	t_u16 capability;
	/** tdls status */
	tdlsStatus_e status;
	/** flag for host based tdls */
	t_u8 external_tdls;
	/** peer support rates */
	t_u8 support_rate[32];
	/** rate size */
	t_u8 rate_len;
	/*Qos capability info */
	t_u8 qos_info;
	/** HT info in TDLS setup confirm*/
	IEEEtypes_HTInfo_t HTInfo;
	/** peer BSSCO_20_40*/
	IEEEtypes_2040BSSCo_t BSSCO_20_40;
	/*Extended capability */
	IEEEtypes_ExtCap_t ExtCap;
	/*RSN IE */
	IEEEtypes_Generic_t rsn_ie;
	/**Link ID*/
	IEEEtypes_LinkIDElement_t link_ie;
	/** AID info */
	IEEEtypes_AID_t aid_info;
	/** VHT Capabilities IE */
	IEEEtypes_VHTCap_t vht_cap;
	/** VHT Operations IE */
	IEEEtypes_VHTOprat_t vht_oprat;
	/** wapi key on off flag */
	t_u8 wapi_key_on;
	/** tx pause status */
	t_u8 tx_pause;
	/** station band mode */
	t_u16 bandmode;
#if defined(DRV_EMBEDDED_AUTHENTICATOR) || defined(DRV_EMBEDDED_SUPPLICANT)
	t_void *cm_connectioninfo;
#endif
	sta_stats stats;
};

/** 802.11h State information kept in the 'mlan_adapter' driver structure */
typedef struct {
	/** Min TX Power capability sent to FW for 11h use and fw power control
	 */
	t_s8 min_tx_power_capability;
	/** Max TX Power capability sent to FW for 11h use and fw power control
	 */
	t_s8 max_tx_power_capability;
	/** User provisioned local power constraint sent in association requests
	 */
	t_s8 usr_def_power_constraint;
	/** Received CHANNEL_SWITCH_ANN event */
	t_bool recvd_chanswann_event;
	/** Indicates an interface wants to enable master radar detection */
	t_bool master_radar_det_enable_pending;
	/** Indicates an interface wants to enable slave radar detection */
	t_bool slave_radar_det_enable_pending;
	/** Indicates whether master radar detection active in the firmware */
	t_bool is_master_radar_det_active;
	/** Indicates whether slave radar detection active in the firmware */
	t_bool is_slave_radar_det_active;
	/** Quiet IE */
	IEEEtypes_Quiet_t quiet_ie;
} wlan_11h_device_state_t;

/** Enumeration for DFS Timestamp represents field */
enum _dfs_timestamp_repr_e {
	/** Ignore entry */
	DFS_TS_REPR_NOT_IN_USE = 0,
	/** NOP (Non-Occupancy Period) start time */
	DFS_TS_REPR_NOP_START = 1,
	/** CAC (Channel Availability Check) completion time */
	DFS_TS_REPR_CAC_COMPLETION
};

/** DFS Timestamp type used for marking NOP/CAC events */
typedef struct _wlan_dfs_timestamp_t wlan_dfs_timestamp_t;

/** DFS Timestamp type used for marking NOP/CAC events */
struct _wlan_dfs_timestamp_t {
	/** Pointer to previous node */
	wlan_dfs_timestamp_t *pprev;
	/** Pointer to next node */
	wlan_dfs_timestamp_t *pnext;
	/** WLAN Channel number */
	t_u8 channel;
	/** What this timestamp represents */
	t_u8 represents;
	/** reserved field */
	t_u16 reserved;
	/** timestamp - seconds */
	t_u32 ts_sec;
	/** timestamp - microseconds */
	t_u32 ts_usec;
};

/** DFS State information kept in the 'mlan_adapter' driver structure */
typedef struct {
	/** Indicates whether DFS channel check is occurring in firmware */
	t_bool dfs_check_pending;
	/** Indicates whether DFS channel check found radar */
	t_bool dfs_radar_found;
	/** Channel radar is being checked on.  BAND_A is assumed. */
	t_u8 dfs_check_channel;
	/** point to the priv which start the DFS check */
	t_void *dfs_check_priv;
	/** Timestamp when we got last report,
	 * to determine if data is old or not.
	 */
	t_u32 dfs_report_time_sec;
	/** List for holding dfs_timestamps for NOP/CAC events */
	mlan_list_head dfs_ts_head;
} wlan_dfs_device_state_t;

/** Enumeration for mlan_ds_11h_radar_det_hndlg stages */
enum _mlan_ds_11h_rdh_stages {
	RDH_OFF = 0,
	RDH_CHK_INTFS = 1,
	RDH_STOP_TRAFFIC,
	RDH_GET_INFO_CHANNEL,
	RDH_GET_INFO_BEACON_DTIM,
	RDH_SET_CUSTOM_IE,
	RDH_REM_CUSTOM_IE,
	RDH_STOP_INTFS,
	RDH_SET_NEW_CHANNEL,
	RDH_RESTART_INTFS,
	RDH_RESTART_TRAFFIC
};

/** State info for Radar Detected Handling kept in 'mlan_adapter' */
typedef struct {
	/** Stage (of Operation) */
	t_u8 stage;
	/** Number of interfaces to handle */
	t_u8 priv_list_count;
	/** Index of interface in process (used by some stages) */
	t_u8 priv_curr_idx;
	/** Current Channel (to leave) */
	t_u8 curr_channel;
	/** New Channel (to switch to) */
	t_u8 new_channel;
	/** UAP band_config */
	Band_Config_t uap_band_cfg;
	/** BEACON*DTIM period (in msec; max of STA/UAP) */
	t_u16 max_bcn_dtim_ms;
	/** tx block flag */
	t_u8 tx_block;
	/** List of interfaces to handle */
	mlan_private *priv_list[MLAN_MAX_BSS_NUM];
} wlan_radar_det_hndlg_state_t;

/** DFS/RDH testing exception settings kept in 'mlan_adapter' */
typedef struct {
	/** user-configured CAC period (in msec) */
	t_u32 user_cac_period_msec;
	/** user-configured NOP period (in sec) */
	t_u16 user_nop_period_sec;
	/** user-configured skip channel change on radar */
	t_bool no_channel_change_on_radar;
	/** user-configured new channel to change to on radar */
	t_u8 fixed_new_channel_on_radar;
	/** user-configured cac restart */
	t_u8 cac_restart;
	/**  cac channel */
	t_u8 chan;
	/** band cfg */
	Band_Config_t bandcfg;
	/** cac time */
	t_u32 millisec_dwell_time;
} wlan_dfs_testing_settings_t;

/**
 * @brief Driver measurement state held in 'mlan_adapter' structure
 *
 *  Used to record a measurement request that the driver is pending on
 *    the result (received measurement report).
 */
typedef struct {
	/**
	 * Dialog token of a pending measurement request/report.  Used to
	 *   block execution while waiting for the specific dialog token
	 */
	t_u8 meas_rpt_pend_on;

	/**
	 * Measurement report received from the firmware that we were pending on
	 */
	HostCmd_DS_MEASUREMENT_REPORT meas_rpt_returned;

} wlan_meas_state_t;

#ifdef SDIO
/**
 *  @brief Link buffer into aggregate head buffer
 *
 *  @param pmbuf_aggr	Pointer to aggregation buffer
 *  @param pmbuf		Pointer to buffer to copy
 */
static inline t_void
wlan_link_buf_to_aggr(pmlan_buffer pmbuf_aggr, pmlan_buffer pmbuf)
{
	/* link new buf at end of list */
	pmbuf->pnext = pmbuf_aggr;
	pmbuf->pprev = pmbuf_aggr->pprev;
	pmbuf->pparent = pmbuf_aggr;
	pmbuf_aggr->pprev->pnext = pmbuf;
	pmbuf_aggr->pprev = pmbuf;
	pmbuf_aggr->use_count++;
}

/** data structure for SDIO MPA TX */
typedef struct _sdio_mpa_tx {
	/** allocated buf for tx aggreation */
	t_u8 *head_ptr;
	/** multiport tx aggregation buffer pointer */
	t_u8 *buf;
	/** multiport tx aggregation buffer length */
	t_u32 buf_len;
	/** multiport tx aggregation packet count */
	t_u32 pkt_cnt;
	/** multiport tx aggregation ports */
	t_u32 ports;
	/** multiport tx aggregation starting port */
	t_u16 start_port;
	/** multiport tx aggregation enable/disable flag */
	t_u8 enabled;
	/** multiport tx aggregation buffer size */
	t_u32 buf_size;
	/** multiport tx aggregation pkt aggr limit */
	t_u32 pkt_aggr_limit;
	/** multiport write info */
	t_u16 mp_wr_info[SDIO_MP_AGGR_DEF_PKT_LIMIT_MAX];
	/** multiport rx aggregation mbuf array */
	pmlan_buffer mbuf_arr[SDIO_MP_AGGR_DEF_PKT_LIMIT_MAX];
} sdio_mpa_tx;

/** data structure for SDIO MPA RX */
typedef struct _sdio_mpa_rx {
	/** allocated buf for rx aggreation */
	t_u8 *head_ptr;
	/** multiport rx aggregation buffer pointer */
	t_u8 *buf;
	/** multiport rx aggregation buffer length */
	t_u32 buf_len;
	/** multiport rx aggregation packet count */
	t_u32 pkt_cnt;
	/** multiport rx aggregation ports */
	t_u32 ports;
	/** multiport rx aggregation starting port */
	t_u16 start_port;

	/** multiport rx aggregation mbuf array */
	pmlan_buffer mbuf_arr[SDIO_MP_AGGR_DEF_PKT_LIMIT_MAX];
	/** multiport rx aggregation pkt len array */
	t_u32 len_arr[SDIO_MP_AGGR_DEF_PKT_LIMIT_MAX];

	/** multiport rx aggregation enable/disable flag */
	t_u8 enabled;
	/** multiport rx aggregation buffer size */
	t_u32 buf_size;
	/** multiport rx aggregation pkt aggr limit */
	t_u32 pkt_aggr_limit;
} sdio_mpa_rx;
#endif

#ifdef USB
/** data structure for USB Rx Deaggregation */
typedef struct _usb_rx_deaggr_params {
	/** Rx aggregation control */
	usb_aggr_ctrl_cfg aggr_ctrl;
} usb_rx_deaggr_params;

#define MAX_USB_TX_PORT_NUM 1
/** data structure for USB Tx Aggregation */
typedef struct _usb_tx_aggr_params {
	/** Tx aggregation control */
	usb_aggr_ctrl_cfg aggr_ctrl;
	/** allocated pmbuf for tx aggreation */
	pmlan_buffer pmbuf_aggr;
	/** packet len used in pmbuf_aggr */
	t_u32 aggr_len;
	/** usb_tx_aggr timer */
	t_void *paggr_hold_timer;
	/** usb_tx_aggr timer set flag */
	t_u8 aggr_hold_timer_is_set;
	/** Timeout duration in milliseconds to wait for aggregation */
	t_u32 hold_timeout_msec;
	/** lock for transmission */
	t_void *paggr_lock;
	/** port for data transmission */
	t_u32 port;
	/** pointer to moal_adatper structure */
	t_void *phandle;
} usb_tx_aggr_params, *pusb_tx_aggr_params;
#endif

/** Type definition of mef_entry*/
typedef struct _mef_cfg_data {
	/** criteria*/
	t_u32 criteria;
	/** entry num*/
	t_u16 entry_num;
	/** entry pointer*/
	mef_entry_t *pentry;
} mef_cfg_data;

/** Type definition of mef_entry*/
typedef struct _mef_entry {
	/** Flag for auto arp entry*/
	int enable_autoarp_entry;
	/** Num for wowlan entry*/
	int num_wowlan_entry;
	/** Num for IPv6 neighbor solicitation message offload */
	int num_ipv6_ns_offload;

	/** criteria*/
	t_u32 criteria;
	/** MEF CFG Array to store etted_entry_bitmap;
	 *  Caution: 0-3 is for NVIDIA WHITE/BLACK list entries
	 *  Caution: 4   is for NVIDIA ping entry
	 *  Caution: 5   is for Auto Arp Entry
	 *  Caution: 6   is for wowlan Entry
	 *  Caution: 7   is for IPv6 Neighbor Solicitation offload Entry
	 */
	mef_entry_t entry[MAX_NUM_ENTRIES];
} mef_entry;

/** vdll_dnld_ctrl structure */
typedef struct _vdll_dnld_ctrl {
	/**  pending  VDLL block */
	t_u8 *pending_block;
	/* pending VDLL block len */
	t_u16 pending_block_len;
	/** memory for VDLL fw image */
	t_u8 *vdll_mem;
	/**  VDLL fw image len */
	t_u32 vdll_len;
#if defined(SDIO) || defined(PCIE)
	/** mlan_buffer for VDLL download */
	mlan_buffer *cmd_buf;
#endif
} vdll_dnld_ctrl, *pvdll_dnld_ctrl;

/** mlan_init_para structure */
typedef struct _mlan_init_para {
#ifdef MFG_CMD_SUPPORT
	/** MFG mode */
	t_u32 mfg_mode;
#endif
#ifdef SDIO
	/** SDIO interrupt mode (0: INT_MODE_SDIO, 1: INT_MODE_GPIO) */
	t_u32 int_mode;
	/** GPIO interrupt pin number */
	t_u32 gpio_pin;
	/** SDIO MPA Tx */
	t_u32 mpa_tx_cfg;
	/** SDIO MPA Rx */
	t_u32 mpa_rx_cfg;
#endif
	/** Auto deep sleep */
	t_u32 auto_ds;
	/** IEEE PS mode */
	t_u32 ps_mode;
	/** Max Tx buffer size */
	t_u32 max_tx_buf;
	/** 802.11d configuration */
	t_u32 cfg_11d;
	/** 802.11H DFS Master Radar Detect */
	t_u32 dfs_master_radar_det_en;
	/** 802.11H DFS Slave Radar Detect */
	t_u32 dfs_slave_radar_det_en;
	/** dev cap mask */
	t_u32 dev_cap_mask;
	/** oob independent reset mode */
	t_u32 indrstcfg;
	/** passive to active scan */
	t_u8 passive_to_active_scan;
	/** uap max sta */
	t_u8 uap_max_sta;
	/** dfs w53 cfg */
	t_u8 dfs53cfg;
#ifdef PCIE
    /** adma ring size */
	t_u16 ring_size;
#endif
	t_u8 ext_scan;
} mlan_init_para, *pmlan_init_para;

#ifdef SDIO
typedef struct _mlan_sdio_card_reg {
	t_u8 start_rd_port;
	t_u8 start_wr_port;
	t_u8 base_0_reg;
	t_u8 base_1_reg;
	t_u8 poll_reg;
	t_u8 host_int_enable;
	t_u8 host_int_status;
	t_u8 status_reg_0;
	t_u8 status_reg_1;
	t_u8 sdio_int_mask;
	t_u32 data_port_mask;
	t_u8 max_mp_regs;
	t_u8 rd_bitmap_l;
	t_u8 rd_bitmap_u;
	t_u8 rd_bitmap_1l;
	t_u8 rd_bitmap_1u;
	t_u8 wr_bitmap_l;
	t_u8 wr_bitmap_u;
	t_u8 wr_bitmap_1l;
	t_u8 wr_bitmap_1u;
	t_u8 rd_len_p0_l;
	t_u8 rd_len_p0_u;
	t_u8 card_config_2_1_reg;
	t_u8 cmd_config_0;
	t_u8 cmd_config_1;
	t_u8 cmd_config_2;
	t_u8 cmd_config_3;
	t_u8 cmd_rd_len_0;
	t_u8 cmd_rd_len_1;
	t_u8 cmd_rd_len_2;
	t_u8 cmd_rd_len_3;
	t_u8 io_port_0_reg;
	t_u8 io_port_1_reg;
	t_u8 io_port_2_reg;
	t_u8 host_int_rsr_reg;
	t_u8 host_int_mask_reg;
	t_u8 host_int_status_reg;
	t_u8 host_restart_reg;
	t_u8 card_to_host_event_reg;
	t_u8 host_interrupt_mask_reg;
	t_u8 card_interrupt_status_reg;
	t_u8 card_interrupt_rsr_reg;
	t_u8 card_revision_reg;
	t_u8 card_ocr_0_reg;
	t_u8 card_ocr_1_reg;
	t_u8 card_ocr_3_reg;
	t_u8 card_config_reg;
	t_u8 card_misc_cfg_reg;
	t_u8 debug_0_reg;
	t_u8 debug_1_reg;
	t_u8 debug_2_reg;
	t_u8 debug_3_reg;
	t_u32 fw_reset_reg;
	t_u8 fw_reset_val;
	t_u8 fw_dnld_offset_0_reg;
	t_u8 fw_dnld_offset_1_reg;
	t_u8 fw_dnld_offset_2_reg;
	t_u8 fw_dnld_offset_3_reg;
	t_u8 fw_dnld_status_0_reg;
	t_u8 fw_dnld_status_1_reg;
	t_u8 winner_check_reg;
} mlan_sdio_card_reg, *pmlan_sdio_card_reg;

typedef struct _mlan_sdio_card {
	const mlan_sdio_card_reg *reg;
    /** maximum ports */
	t_u8 max_ports;
    /** mp aggregation packet limit */
	t_u8 mp_aggr_pkt_limit;
    /** sdio new mode support */
	t_bool supports_sdio_new_mode;
    /** max tx aggr buf size */
	t_u32 mp_tx_aggr_buf_size;
    /** max rx aggr buf size */
	t_u32 mp_rx_aggr_buf_size;
    /** IO port */
	t_u32 ioport;
	/** number of interrupt receive */
	t_u32 num_of_irq;
	/** max SDIO single port tx size */
	t_u16 max_sp_tx_size;
	/** max SDIO single port rx size */
	t_u16 max_sp_rx_size;
	/** SDIO multiple port read bitmap */
	t_u32 mp_rd_bitmap;
	/** SDIO multiple port write bitmap */
	t_u32 mp_wr_bitmap;
	/** SDIO end port from txbufcfg */
	t_u16 mp_end_port;
	/** SDIO port mask calculated based on txbufcfg end port */
	t_u32 mp_data_port_mask;
	/** Current available port for read */
	t_u8 curr_rd_port;
	/** Current available port for write */
	t_u8 curr_wr_port;
	/** FW update port number */
	t_u32 mp_update[SDIO_MP_AGGR_DEF_PKT_LIMIT_MAX * 2];
	/** Invalid port update count */
	t_u32 mp_invalid_update;
	/** Array to store values of SDIO multiple port group registers */
	t_u8 *mp_regs;
	/** allocated buf to read SDIO multiple port group registers */
	t_u8 *mp_regs_buf;
	/** buffer to handle receive packet */
	t_u8 *rx_buf;
	/** allocated buf for receive */
	t_u8 *rx_buffer;
	/* see blk_queue_max_segment_size */
	t_u32 max_seg_size;
	/* see blk_queue_max_segments */
	t_u16 max_segs;

	/** data structure for SDIO MPA TX */
	sdio_mpa_tx mpa_tx;
	/** packet number for tx aggr */
	t_u32 mpa_tx_count[SDIO_MP_AGGR_DEF_PKT_LIMIT_MAX];
	/** no more packets count*/
	t_u32 mpa_sent_last_pkt;
	/** no write_ports count */
	t_u32 mpa_sent_no_ports;
	/** last wr_bitmap from FW */
	t_u32 last_recv_wr_bitmap;
	/** last wr_bitmap from FW */
	t_u32 last_recv_rd_bitmap;
	/** last mp_wr_bitmap */
	t_u32 last_mp_wr_bitmap[SDIO_MP_DBG_NUM];
	/** last ports for cmd53 write data */
	t_u32 last_mp_wr_ports[SDIO_MP_DBG_NUM];
	/** last length for cmd53 write data */
	t_u32 last_mp_wr_len[SDIO_MP_DBG_NUM];
	/** length info for cmd53 write data */
	t_u16 last_mp_wr_info[SDIO_MP_DBG_NUM * SDIO_MP_AGGR_DEF_PKT_LIMIT_MAX];
	/** last curr_wr_port */
	t_u8 last_curr_wr_port[SDIO_MP_DBG_NUM];

	/** buffer for mp debug */
	t_u8 *mpa_buf;
	/** length info for mp buf size */
	t_u32 mpa_buf_size;

	/** last mp_index */
	t_u8 last_mp_index;

	/** data structure for SDIO MPA RX */
	sdio_mpa_rx mpa_rx;
	/** packet number for tx aggr */
	t_u32 mpa_rx_count[SDIO_MP_AGGR_DEF_PKT_LIMIT_MAX];

	/** SDIO interrupt mode (0: INT_MODE_SDIO, 1: INT_MODE_GPIO) */
	t_u32 int_mode;
	/** GPIO interrupt pin number */
	t_u32 gpio_pin;

	/** flag for sdio rx aggr */
	t_bool sdio_rx_aggr_enable;
	/** fw rx block size */
	t_u16 sdio_rx_block_size;
} mlan_sdio_card, *pmlan_sdio_card;
#endif

#ifdef PCIE
#define MAX_TXRX_BD         32
#define ADMA_MAX_TXRX_BD    512
#define ADMA_DEF_TXRX_BD    128
/** 128 entry will mapping to 7*/
#define TXRX_DEF_NUM_DESC 7
/** 8 entry will mapping to 3 */
#define EVT_NUM_DESC 3
#define MLAN_MAX_TXRX_BD    MAX(ADMA_MAX_TXRX_BD, MAX_TXRX_BD)
/** 8 Event buffer ring */
#define MLAN_MAX_EVT_BD 0x08
typedef struct _mlan_pcie_card_reg {
	/* TX buffer description rd pointer */
	t_u32 reg_txbd_rdptr;
	/* TX buffer description wr pointer */
	t_u32 reg_txbd_wrptr;
	/* RX buffer description rd pointer */
	t_u32 reg_rxbd_rdptr;
	/* RX buffer description wr pointer */
	t_u32 reg_rxbd_wrptr;
	/** evtbd rdptr register */
	t_u32 reg_evtbd_rdptr;
	/** evtbd wrptr register */
	t_u32 reg_evtbd_wrptr;
	/** host int mask register */
	t_u16 reg_host_int_mask;
	/** host int status mask register*/
	t_u16 reg_host_int_status_mask;
	/** host int status register */
	t_u16 reg_host_int_status;
	/** host int status clr selection */
	t_u32 reg_host_int_clr_sel;
	/** cpu int event register */
	t_u16 reg_cpu_int_event;
	/** ip revision register */
	t_u16 reg_ip_rev;
	/** revision id register */
	t_u32 reg_rev_id;
	/** driver ready register */
	t_u16 reg_drv_ready;
	/** cpu int status register */
	t_u16 reg_cpu_int_status;
	/** scratch 0 register */
	t_u16 reg_scratch_0;
	/** scratch 1 register */
	t_u16 reg_scratch_1;
	/** scratch 2 register */
	t_u16 reg_scratch_2;
	/** scratch 3 register */
	t_u16 reg_scratch_3;
	/** scratch 6 register */
	t_u16 reg_scratch_6;
	/** scratch 7 register */
	t_u16 reg_scratch_7;
	/** host interrupt mask*/
	t_u32 host_intr_mask;
	/** data send interrupt for host*/
	t_u32 host_intr_dnld_done;
	/** Data receive interrupt for host */
	t_u32 host_intr_upld_rdy;
	/** Command sent interrupt for host */
	t_u32 host_intr_cmd_done;
	/** Event ready interrupt for host */
	t_u32 host_intr_event_rdy;
	t_u32 host_intr_cmd_dnld;
	/* TX/RX buffer description mask */
	t_u32 txrx_rw_ptr_mask;
	/* TX/RX buffer description wrap mask */
	t_u32 txrx_rw_ptr_wrap_mask;
	/* TX/RX buffer description indication */
	t_u32 txrx_rw_ptr_rollover_ind;
	/** ADMA feature */
	t_u8 use_adma;
	/** write to clear interrupt status flag */
	t_u8 msi_int_wr_clr;
} mlan_pcie_card_reg, *pmlan_pcie_card_reg;

typedef struct _mlan_pcie_card {
	const mlan_pcie_card_reg *reg;
	/** PCIE interrupt modes 0: Legacy, 1: MSI, 2:MSI-X */
	t_u32 pcie_int_mode;
	/** PCIE function number */
	t_u8 func_num;
	/** pending num of tx ring buffer in firmware */
	t_u8 txbd_pending;
	/** Write pointer for TXBD ring */
	t_u32 txbd_wrptr;
	/** Shadow copy of TXBD read pointer */
	t_u32 txbd_rdptr;
	/** TXBD ring size */
	t_u32 txbd_ring_size;
	/** Lock for protecting the TX ring */
	t_void *tx_ring_lock;
	/** Virtual base address of txbd_ring */
	t_u8 *txbd_ring_vbase;
	/** Physical base address of txbd_ring */
	t_u64 txbd_ring_pbase;
	/** Ring of buffer descriptors for TX */
	t_void *txbd_ring[MLAN_MAX_TXRX_BD];
	/** A list of mlan_buffer objects used for data tx */
	mlan_buffer *tx_buf_list[MLAN_MAX_TXRX_BD];
	/** Flush indicator for txbd_ring */
	t_bool txbd_flush;
	/** txrx data dma ring size */
	t_u16 txrx_bd_size;
    /** txrx num desc */
	t_u16 txrx_num_desc;

	/** Shadow copy of RXBD write pointer */
	t_u32 rxbd_wrptr;
	/** RxBD read pointer */
	t_u32 rxbd_rdptr;
	/** RXBD ring size */
	t_u32 rxbd_ring_size;
	/** A spinlock for rxbd_ring */
	t_void *rx_ring_lock;
	/** Virtual base address of rxbd_ring */
	t_u8 *rxbd_ring_vbase;
	/** Physical base address of rxbd_ring */
	t_u64 rxbd_ring_pbase;
	/** Ring of buffer descriptors for RX */
	t_void *rxbd_ring[MLAN_MAX_TXRX_BD];
	/** A list of mlan_buffer objects used for data rx */
	mlan_buffer *rx_buf_list[MLAN_MAX_TXRX_BD];

	/** Shadow copy of cmdrsp/evt write pointer */
	t_u32 evtbd_wrptr;
	/** Read pointer for cmdrsp/evt ring */
	t_u32 evtbd_rdptr;
	/** Size of the cmdrsp/evt ring */
	t_u32 evtbd_ring_size;
	/** Virtual base address of evtbd_bd_ring */
	t_u8 *evtbd_ring_vbase;
	/** Physical base address of evtbd_bd_ring */
	t_u64 evtbd_ring_pbase;
	/** Ring of buffer descriptors for EVENT */
	t_void *evtbd_ring[MLAN_MAX_EVT_BD];
	/** A list of mlan_buffer objects used for EVENT */
	mlan_buffer *evt_buf_list[MLAN_MAX_EVT_BD];

	/** Command buffer */
	mlan_buffer *cmd_buf;
	/** Command response buffer */
	mlan_buffer *cmdrsp_buf;
	/** Command buffer */
	mlan_buffer *vdll_cmd_buf;
	/** last tx_pkt_size  */
	t_u32 last_tx_pkt_size[MLAN_MAX_TXRX_BD];
} mlan_pcie_card, *pmlan_pcie_card;
#endif

#ifdef USB
typedef struct _mlan_usb_card {
	/** data structure for USB Rx Deaggregation */
	usb_rx_deaggr_params usb_rx_deaggr;
	/** data structure for USB Tx Aggregation */
	usb_tx_aggr_params usb_tx_aggr[MAX_USB_TX_PORT_NUM];
	/** USB sggregation supported by FW */
	t_u8 fw_usb_aggr;

} mlan_usb_card, *pmlan_usb_card;

#endif

typedef struct _mlan_card_info {
	/** Max Tx buffer size */
	t_u32 max_tx_buf_size;
    /** support V14_FW_API */
	t_u8 v14_fw_api;
	/** support V16_FW_API */
	t_u8 v16_fw_api;
	/** support V17_FW_API */
	t_u8 v17_fw_api;
	/** suppress PS handshake */
	t_u8 supp_ps_handshake;
	/** DEFAULT_11N_TX_BF_CAP */
	t_u32 default_11n_tx_bf_cap;
} mlan_card_info, *pmlan_card_info;

typedef struct _mlan_adapter mlan_adapter, *pmlan_adapter;

/**Adapter_operations data structure*/
typedef struct _adapter_operations {
	/**firmware download handler*/
	mlan_status (*dnld_fw) (pmlan_adapter pmadapter, pmlan_fw_image pmfw);
	/**interrupt handler*/
	mlan_status (*interrupt) (t_u16 msg_id, pmlan_adapter pmadapter);
	/**INT process handler*/
	mlan_status (*process_int_status) (pmlan_adapter pmadapter);
	/**host to card handler*/
	mlan_status (*host_to_card) (pmlan_private pmpriv, t_u8 type,
				     mlan_buffer *pmbuf,
				     mlan_tx_param *tx_param);
	/*wakeup card */
	mlan_status (*wakeup_card) (pmlan_adapter pmadapter, t_u8 timeout);
	/*reset the PM setting of card */
	mlan_status (*reset_card) (pmlan_adapter adapter);
	/** Handle event/cmd complete*/
	mlan_status (*event_complete) (mlan_adapter *pmlan_adapter,
				       pmlan_buffer pmbuf, mlan_status status);
	/** Handle complete receiving data */
	mlan_status (*data_complete) (mlan_adapter *pmlan_adapter,
				      pmlan_buffer pmbuf, mlan_status status);
	/** Handle command response complete */
	mlan_status (*cmdrsp_complete) (mlan_adapter *pmlan_adapter,
					pmlan_buffer pmbuf, mlan_status status);
	/** Handle rx packet */
	mlan_status (*handle_rx_packet) (mlan_adapter *pmadapter,
					 pmlan_buffer pmbuf);
	/** handle dump interface specific info */
	mlan_status (*debug_dump) (mlan_adapter *pmadapter);
	/** disable host interrupt */
	mlan_status (*disable_host_int) (mlan_adapter *pmadapter);
	/** enable host interrupt */
	mlan_status (*enable_host_int) (mlan_adapter *pmadapter);
	/**Interface header length*/
	t_u32 intf_header_len;
} mlan_adapter_operations;

/** Adapter data structure for MLAN */
typedef struct _mlan_adapter {
	/** MOAL handle structure */
	t_void *pmoal_handle;
	/** BSS Attributes */
	mlan_bss_attr bss_attr[MLAN_MAX_BSS_NUM];
	/** Private pointer */
	pmlan_private priv[MLAN_MAX_BSS_NUM];
	/** Total number of Priv number */
	t_u8 priv_num;
	/** Priority table for bss */
	mlan_bssprio_tbl bssprio_tbl[MLAN_MAX_BSS_NUM];
	/** Callback table */
	mlan_callbacks callbacks;
	/** Init parameters */
	mlan_init_para init_para;
	/** mlan_lock for init/shutdown */
	t_void *pmlan_lock;
	/** main_proc_lock for main_process */
	t_void *pmain_proc_lock;
	/** mlan_processing */
	t_u32 mlan_processing;
	/** main_process_cnt */
	t_u32 main_process_cnt;
	/** mlan_rx_processing */
	t_u32 mlan_rx_processing;
	/** rx_proc_lock for main_rx_process */
	t_void *prx_proc_lock;
	/** more_rx_task_flag */
	t_u32 more_rx_task_flag;
	/** rx work enable flag */
	t_u8 rx_work_flag;
	/* number of rx pkts queued */
	t_u16 rx_pkts_queued;
	/** more task flag */
	t_u32 more_task_flag;
	/** delay task flag */
	t_u32 delay_task_flag;
	/** Max tx buf size */
	t_u16 max_tx_buf_size;
	/** Tx buf size */
	t_u16 tx_buf_size;
	/** current tx buf size in fw */
	t_u16 curr_tx_buf_size;
	/** flush data flag */
	t_u8 flush_data;
	/** STATUS variables */
	WLAN_HARDWARE_STATUS hw_status;
	/** PnP SUPPORT */
	t_u8 surprise_removed;
	/** FW hang report */
	t_u8 fw_hang_report;

	/** ECSA support */
	t_u8 ecsa_enable;

	/** Get log support */
	t_u8 getlog_enable;

	/** Radio on flag */
	t_u16 radio_on;

	/** Firmware release number */
	t_u32 fw_release_number;
	/** firmware version */
	t_u8 fw_ver;
	/** firmware minor version */
	t_u8 fw_min_ver;
	/** firmare hotfix version */
	t_u8 fw_hotfix_ver;
	/** uap firmware version */
	t_u8 uap_fw_ver;
	/** mac address retrun from get_hw_spec */
	t_u8 permanent_addr[MLAN_MAC_ADDR_LENGTH];
	/** Number of antenna used */
	t_u16 number_of_antenna;
	/** antenna info */
	t_u8 antinfo;
	/** Firmware capability information */
	t_u32 fw_cap_info;
	/** Extended firmware capability information */
	t_u32 fw_cap_ext;
#if defined(PCIE9098) || defined(SD9098) || defined(USB9098) || defined(PCIE9097) || defined(SD9097) || defined(USB9097)
	/** High byte for 5G, low byte for 2G, like 0x2211 0x22 for 5G, 0x11 for
	 * 2G */
	t_u16 user_htstream;
#endif
	/** vdll ctrl */
	vdll_dnld_ctrl vdll_ctrl;
#if defined(SDIO) || defined(PCIE)
	/** pint_lock for interrupt handling */
	t_void *pint_lock;
	/** Interrupt status */
	t_u32 ireg;
#endif
	/** card type */
	t_u16 card_type;
	/** card rev */
	t_u8 card_rev;
	const mlan_card_info *pcard_info;
#ifdef SDIO
	pmlan_sdio_card pcard_sd;
#endif
#ifdef PCIE
	pmlan_pcie_card pcard_pcie;
#endif
#ifdef USB
	pmlan_usb_card pcard_usb;
#endif

	/** Event cause */
	t_u32 event_cause;
	/** Event buffer */
	pmlan_buffer pmlan_buffer_event;
	/** Upload length */
	t_u32 upld_len;
	/** Upload buffer*/
	t_u8 upld_buf[WLAN_UPLD_SIZE];
	/** Data sent:
	 *       TRUE - Data is sent to fw, no Tx Done received
	 *       FALSE - Tx done received for previous Tx
	 */
	t_u8 data_sent;
	/** Data sent cnt */
	t_u32 data_sent_cnt;
	/** CMD sent:
	 *       TRUE - CMD is sent to fw, no CMD Done received
	 *       FALSE - CMD done received for previous CMD
	 */
	t_u8 cmd_sent;
	/** CMD Response received:
	 *       TRUE - CMD is response is received from fw, and yet to process
	 *       FALSE - No cmd response to process
	 */
	t_u8 cmd_resp_received;
	/** Event received:
	 *       TRUE - Event received from fw, and yet to process
	 *       FALSE - No events to process
	 */
	t_u8 event_received;

	/** Data received:
	 *       TRUE - Data received from fw
	 *       FALSE - No Data received
	 */
	t_u8 data_received;

	/** Command-related variables */
	/** Command sequence number */
	t_u16 seq_num;
	/** Command controller nodes */
	cmd_ctrl_node *cmd_pool;
	/** Current Command */
	cmd_ctrl_node *curr_cmd;
	/** mlan_lock for command */
	t_void *pmlan_cmd_lock;
	/** Number of command timeouts */
	t_u32 num_cmd_timeout;
	/** Last init fw command id */
	t_u16 last_init_cmd;
	/** Command timer */
	t_void *pmlan_cmd_timer;
	/** Command timer set flag */
	t_u8 cmd_timer_is_set;
	/** time stamp for command dnld */
	t_u32 dnld_cmd_in_secs;

	/** Command Queues */
	/** Free command buffers */
	mlan_list_head cmd_free_q;
	/** Pending command buffers */
	mlan_list_head cmd_pending_q;
	/** Command queue for scanning */
	mlan_list_head scan_pending_q;
	/** ioctl pending queue */
	mlan_list_head ioctl_pending_q;
	/** pending_ioctl flag */
	t_u8 pending_ioctl;
	pmlan_private pending_disconnect_priv;
	/** mlan_processing */
	t_u32 scan_processing;
	/** ext_scan enh support flag */
	t_u8 ext_scan_enh;
	/** scan type: 0 legacy, 1: enhance scan*/
	t_u8 ext_scan_type;
	/** ext scan timeout */
	t_u8 ext_scan_timeout;
	/** coex scan flag */
	t_u8 coex_scan;
	/** coex min scan time */
	t_u8 coex_min_scan_time;
	/** coex max scan time */
	t_u8 coex_max_scan_time;
	/** coex win size flag */
	t_u8 coex_win_size;
	/** coex amdpdu tx win size */
	t_u8 coex_tx_win_size;
	/** coex ampdu rx win size */
	t_u8 coex_rx_win_size;
	/** Region code */
	t_u16 region_code;
	/** Region Channel data */
	region_chan_t region_channel[MAX_REGION_CHANNEL_NUM];
	/** CFP table code for 2.4GHz */
	t_u8 cfp_code_bg;
	/** CFP table code for 5GHz */
	t_u8 cfp_code_a;
	wmm_ac_parameters_t ac_params[MAX_AC_QUEUES];
	/** Minimum BA Threshold */
	t_u8 min_ba_threshold;
#ifdef STA_SUPPORT
	/** Universal Channel data */
	region_chan_t universal_channel[MAX_REGION_CHANNEL_NUM];
	/** Parsed region channel */
	parsed_region_chan_11d_t parsed_region_chan;
#endif				/* STA_SUPPORT */
	/** 11D and Domain Regulatory Data */
	wlan_802_11d_domain_reg_t domain_reg;
	/** Country Code */
	t_u8 country_code[COUNTRY_CODE_LEN];
	/** FSM variable for 11h support */
	wlan_11h_device_state_t state_11h;
	/** FSM variable for DFS support */
	wlan_dfs_device_state_t state_dfs;
	/** FSM variable for RDH support */
	wlan_radar_det_hndlg_state_t state_rdh;
	/** variable to configure dfs channel switch count */
	t_s8 dfs_cs_count;
	/** User configured settings for DFS testing */
	wlan_dfs_testing_settings_t dfs_test_params;
	/**  dfs w53 cfg */
	t_u8 dfs53cfg;
	/** FSM variable for MEAS support */
	wlan_meas_state_t state_meas;
	/** Scan table */
	BSSDescriptor_t *pscan_table;
	/** scan age in secs */
	t_u32 age_in_secs;
	/** Active scan for hidden ssid triggered */
	t_u8 active_scan_triggered;
	/** channel statstics */
	ChanStatistics_t *pchan_stats;
	/** Number of records in the chan_stats */
	t_u32 num_in_chan_stats;
	/** index of chan stats */
	t_u32 idx_chan_stats;
	t_u8 bgscan_reported;

	/** Number of records in the scan table */
	t_u32 num_in_scan_table;
	/** Scan probes */
	t_u16 scan_probes;

	/** Scan type */
	t_u8 scan_type;
	/** Scan mode */
	t_u32 scan_mode;
	/** Specific scan time */
	t_u16 specific_scan_time;
	/** Active scan time */
	t_u16 active_scan_time;
	/** Passive scan time */
	t_u16 passive_scan_time;
	/** Passive scan to active scan */
	t_u8 passive_to_active_scan;
	/** scan channel gap time */
	t_u16 scan_chan_gap;
	/** Scan block flag */
	t_u8 scan_block;
	/** Extended scan or legacy scan */
	t_u8 ext_scan;
	t_u32 bcn_buf_size;
	/** Beacon buffer */
	t_u8 *bcn_buf;
	/** Pointer to valid beacon buffer end */
	t_u8 *pbcn_buf_end;
	/** allocate fixed scan beacon buffer size*/
	t_u32 fixed_beacon_buffer;

	/** F/W supported bands */
	t_u16 fw_bands;
	/** User selected band to start adhoc network */
	t_u16 adhoc_start_band;
	/** User selected bands */
	t_u16 config_bands;
	/** Pointer to channel list last sent to the firmware for scanning */
	ChanScanParamSet_t *pscan_channels;

	/** Tx lock flag */
	t_u8 tx_lock_flag;
	/** Rx lock flag */
	t_u8 rx_lock_flag;
	/** main lock flag */
	t_u8 main_lock_flag;
#ifdef USB
	/** Tx CMD endpoint address */
	t_u8 tx_cmd_ep;
	/** Rx CMD/EVT endpoint address */
	t_u8 rx_cmd_ep;
	/** Rx data endpoint address */
	t_u8 rx_data_ep;
	/** Tx data endpoint address */
	t_u8 tx_data_ep;
#endif

	/** sleep_params_t */
	sleep_params_t sleep_params;
	/** sleep_period_t (Enhanced Power Save) */
	sleep_period_t sleep_period;
	/** saved sleep_period_t (Enhanced Power Save) */
	sleep_period_t saved_sleep_period;

	/** Power Save mode */
	/**
	 * Wlan802_11PowerModeCAM = disable
	 * Wlan802_11PowerModePSP = enable
	 */
	t_u16 ps_mode;
	/** Power Save state */
	t_u32 ps_state;
	/** Need to wakeup flag */
	t_u8 need_to_wakeup;
	/** keep_wakeup */
	t_u8 keep_wakeup;

	/** Multiple DTIM */
	t_u16 multiple_dtim;
	/** Local listen interval */
	t_u16 local_listen_interval;
	/** Null packet interval */
	t_u16 null_pkt_interval;

	/** IEEE ps inactivity timout value */
	t_u16 inact_tmo;
	/** Power save confirm sleep command buffer */
	pmlan_buffer psleep_cfm;
	/** Beacon miss timeout */
	t_u16 bcn_miss_time_out;

	/** Deep Sleep flag */
	t_u8 is_deep_sleep;
	/** Idle time */
	t_u16 idle_time;
	/** Auto Deep Sleep enabled at init time */
	t_u8 init_auto_ds;

	/** delay null pkt flag */
	t_u8 delay_null_pkt;
	/** Delay to PS in milliseconds */
	t_u16 delay_to_ps;
	/** Enhanced PS mode */
	t_u16 enhanced_ps_mode;
	/** Device wakeup required flag */
	t_u8 pm_wakeup_card_req;

	/** Gen NULL pkg */
	t_u16 gen_null_pkt;

	/** PPS/UAPSD mode flag */
	t_u16 pps_uapsd_mode;
	/** Number of wakeup tries */
	t_u32 pm_wakeup_fw_try;
	/** time stamp when host try to wake up firmware */
	t_u32 pm_wakeup_in_secs;
	/** Card wakeup timer */
	t_void *pwakeup_fw_timer;
	/** Card wakeup timer */
	t_u8 wakeup_fw_timer_is_set;
	/** Number of wake up timeouts */
	t_u32 pm_wakeup_timeout;

	/** Host Sleep configured flag */
	t_u8 is_hs_configured;
	/** Host Sleep configuration */
	hs_config_param hs_cfg;
	/** Host Sleep activated flag */
	t_u8 hs_activated;
	/** mef_flt_cfg_mef configuration */
	mef_entry entry_cfg;
	/** Event body */
	t_u8 event_body[MAX_EVENT_SIZE];
	/** 802.11n device capabilities */
	t_u32 hw_dot_11n_dev_cap;
	/** Device support for MIMO abstraction of MCSs */
	t_u8 hw_dev_mcs_support;
#ifdef STA_SUPPORT
	/** Adhoc Secondary Channel Bandwidth */
	t_u8 chan_bandwidth;
#endif				/* STA_SUPPORT */

	/** 802.11ac device capabilities */
	t_u32 hw_dot_11ac_dev_cap;
	/** 802.11ac device support for MIMO abstraction of MCSs */
	t_u32 hw_dot_11ac_mcs_support;
	/** length of hw he capability */
	t_u8 hw_hecap_len;
	/** 802.11ax HE capability */
	t_u8 hw_he_cap[54];
	/** length of hw 2.4G he capability */
	t_u8 hw_2g_hecap_len;
	/** 802.11ax 2.4G HE capability */
	t_u8 hw_2g_he_cap[54];
	/** max mgmt IE index in device */
	t_u16 max_mgmt_ie_index;
	/** Head of Rx data queue */
	mlan_list_head rx_data_queue;
#ifdef MFG_CMD_SUPPORT
	t_u32 mfg_mode;
#endif
	/** Debug */
	wlan_dbg dbg;

	/** RX pending for forwarding packets */
	mlan_scalar pending_bridge_pkts;

#ifdef STA_SUPPORT
	/** ARP filter buffer */
	t_u8 arp_filter[ARP_FILTER_MAX_BUF_SIZE];
	/** ARP filter buffer size */
	t_u32 arp_filter_size;
#endif				/* STA_SUPPORT */
	/** Minimum delay between HsActive and HostWake (in msec) */
	t_u16 min_wake_holdoff;
	/** Host sleep wake interval(in msec) */
	t_u32 hs_wake_interval;
	/** Host sleep inactivity timeout (in msec) */
	t_u32 hs_inactivity_timeout;
	/** Parameter type for indication gpio*/
	t_u8 param_type_ind;
	/** GPIO pin for indication wakeup source */
	t_u32 ind_gpio;
	/** Level on ind_gpio pin for indication normal wakeup source */
	t_u32 level;
	/** Parameter type for extend hscfg*/
	t_u8 param_type_ext;
	/** Events that will be forced ignore */
	t_u32 event_force_ignore;
	/** Events that will use extend gap to inform host*/
	t_u32 event_use_ext_gap;
	/** Extend gap*/
	t_u8 ext_gap;
	/** GPIO wave level for extend hscfg */
	t_u8 gpio_wave;
	/** Dynamic MIMO-SISO switch for hscfg*/
	t_u8 hs_mimo_switch;
	/** management frame wakeup filter config */
	mlan_mgmt_frame_wakeup mgmt_filter[MAX_MGMT_FRAME_FILTER];
	/** Bypass TX queue pkt count  */
	t_u32 bypass_pkt_count;
#ifdef STA_SUPPORT
	/** warm-reset IOCTL request buffer pointer */
	pmlan_ioctl_req pwarm_reset_ioctl_req;
#endif
	/** SCAN IOCTL request buffer pointer */
	pmlan_ioctl_req pscan_ioctl_req;
	/** DPD data pointer */
	t_u8 *pdpd_data;
	/** DPD data length  */
	t_u32 dpd_data_len;
	/** region txpowerlimit cfg data buf pointer */
	t_u8 *ptxpwr_data;
	/** region txpowerlimit cfg data len */
	t_u32 txpwr_data_len;
	/** Cal data pointer */
	t_u8 *pcal_data;
	/** Cal data length  */
	t_u32 cal_data_len;
	/** tdls status */
	/* TDLS_NOT_SETUP|TDLS_SWITCHING_CHANNEL|TDLS_IN_BASE_CHANNEL|TDLS_IN_SWITCH_CHANNEL */
	tdlsStatus_e tdls_status;
	/** Feature control bitmask */
	t_u32 feature_control;

	/** Control coex RX window size configuration */
	t_u8 coex_rx_winsize;
	t_bool dfs_repeater;
	t_u32 dfsr_channel;
	t_u8 chanrpt_param_bandcfg;
#if defined(PCIE)
	mlan_buffer *ssu_buf;
#endif
	/** maximum sta connection */
	t_u8 max_sta_conn;
	otp_region_info_t *otp_region;
	chan_freq_power_t *cfp_otp_bg;
	t_u8 *tx_power_table_bg;
	t_u32 tx_power_table_bg_size;
	t_u8 tx_power_table_bg_rows;
	t_u8 tx_power_table_bg_cols;
	chan_freq_power_t *cfp_otp_a;
	t_u8 *tx_power_table_a;
	t_u32 tx_power_table_a_size;
	t_u8 tx_power_table_a_rows;
	t_u8 tx_power_table_a_cols;
	/**mlan adapter operations*/
	mlan_adapter_operations ops;
#ifdef DRV_EMBEDDED_AUTHENTICATOR
	/** authenticator_priv */
	pmlan_private authenticator_priv;
#endif
	/** TP accounting mode 1-enable 0-disable */
	t_u32 tp_state_on;
	/** Packet drop point */
	t_u32 tp_state_drop_point;
} mlan_adapter, *pmlan_adapter;

/** Check if stream 2X2 enabled */
#define IS_STREAM_2X2(x) ((x)&FEATURE_CTRL_STREAM_2X2)
/** Check if DFS support enabled */
#define IS_DFS_SUPPORT(x) ((x)&FEATURE_CTRL_DFS_SUPPORT)
#ifdef USB
/** Check if winner check & not wait for FW ready event */
#define IS_USB_NEW_INIT(x) ((x)&FEATURE_CTRL_USB_NEW_INIT)
#endif

/** Ethernet packet type for EAPOL */
#define MLAN_ETHER_PKT_TYPE_EAPOL (0x888E)
#define MLAN_ETHER_PKT_TYPE_ARP (0x0806)
/** Ethernet packet type for WAPI */
#define MLAN_ETHER_PKT_TYPE_WAPI (0x88B4)
/** Ethernet packet type offset */
#define MLAN_ETHER_PKT_TYPE_OFFSET (12)

mlan_status wlan_init_lock_list(pmlan_adapter pmadapter);
mlan_status wlan_init_priv_lock_list(pmlan_adapter pmadapter, t_u8 start_index);
t_void wlan_free_lock_list(pmlan_adapter pmadapter);
mlan_status wlan_init_timer(pmlan_adapter pmadapter);
t_void wlan_free_timer(pmlan_adapter pmadapter);

/* Function prototype */
/** Initialize firmware */
mlan_status wlan_init_fw(pmlan_adapter pmadapter);

/** get hw spec complete */
mlan_status wlan_get_hw_spec_complete(pmlan_adapter pmadapter);

/** Initialize firmware complete */
mlan_status wlan_init_fw_complete(pmlan_adapter pmadapter);

/** Shutdown firmware complete */
mlan_status wlan_shutdown_fw_complete(pmlan_adapter pmadapter);

/** Receive event */
mlan_status wlan_recv_event(pmlan_private priv, mlan_event_id event_id,
			    t_void *pmevent);

/** Initialize mlan_adapter structure */
t_void wlan_init_adapter(pmlan_adapter pmadapter);

/** Initialize mlan_private structure */
mlan_status wlan_init_priv(pmlan_private priv);
#ifdef USB
/** get pcie device from card type */
mlan_status wlan_get_usb_device(pmlan_adapter pmadapter);
#endif
mlan_status wlan_download_vdll_block(mlan_adapter *pmadapter, t_u8 *block,
				     t_u16 block_len);
mlan_status wlan_process_vdll_event(pmlan_private pmpriv, pmlan_buffer pevent);
/** Process event */
mlan_status wlan_process_event(pmlan_adapter pmadapter);

/** Prepare command */
mlan_status wlan_prepare_cmd(pmlan_private priv, t_u16 cmd_no,
			     t_u16 cmd_action, t_u32 cmd_oid,
			     t_void *pioctl_buf, t_void *pdata_buf);

/** cmd timeout handler */
t_void wlan_cmd_timeout_func(t_void *function_context);

/**
 *  @brief check if Tx pending
 *
 *  @param pmadapter	Pointer to mlan_adapter
 *  @return  MTRUE/MFALSE;
 */
static inline t_u8
wlan_is_tx_pending(mlan_adapter *pmadapter)
{
#ifdef PCIE
	if (IS_PCIE(pmadapter->card_type) &&
	    pmadapter->pcard_pcie->txbd_pending)
		return MTRUE;
#endif
	return MFALSE;
}

/** process host cmd */
mlan_status wlan_misc_ioctl_host_cmd(pmlan_adapter pmadapter,
				     pmlan_ioctl_req pioctl_req);
/** process init/shutdown cmd*/
mlan_status wlan_misc_ioctl_init_shutdown(pmlan_adapter pmadapter,
					  pmlan_ioctl_req pioctl_req);
/** process debug info */
mlan_status wlan_get_info_debug_info(pmlan_adapter pmadapter,
				     pmlan_ioctl_req pioctl_req);

#if defined(STA_SUPPORT) && defined(UAP_SUPPORT)
extern pmlan_operations mlan_ops[];
/** Set/Get BSS role */
mlan_status wlan_bss_ioctl_bss_role(pmlan_adapter pmadapter,
				    pmlan_ioctl_req pioctl_req);
#endif

#if defined(PCIE)
mlan_status wlan_misc_ssu(pmlan_adapter pmadapter, pmlan_ioctl_req pioctl_req);
#endif

mlan_status wlan_misc_hal_phy_cfg(pmlan_adapter pmadapter,
				  pmlan_ioctl_req pioctl_req);

mlan_status wlan_set_ewpa_mode(mlan_private *priv, pmlan_ds_passphrase psec_pp);
mlan_status wlan_find_bss(mlan_private *pmpriv, pmlan_ioctl_req pioctl_req);

/* block main process */
void mlan_block_main_process(mlan_adapter *pmadapter, t_u8 block);
/* block rx process */
void mlan_block_rx_process(mlan_adapter *pmadapter, t_u8 block);
/** check pending command */
int wlan_check_pending_cmd(mlan_adapter *pmadapter);
/** Allocate memory for adapter structure members */
mlan_status wlan_allocate_adapter(pmlan_adapter pmadapter);
/** Free adapter */
t_void wlan_free_adapter(pmlan_adapter pmadapter);
/** Free priv */
t_void wlan_free_priv(mlan_private *pmpriv);
/** Allocate command buffer */
mlan_status wlan_alloc_cmd_buffer(mlan_adapter *pmadapter);
/** Free command buffer */
mlan_status wlan_free_cmd_buffer(mlan_adapter *pmadapter);
/** Request command lock */
t_void wlan_request_cmd_lock(mlan_adapter *pmadapter);
/** Release command lock */
t_void wlan_release_cmd_lock(mlan_adapter *pmadapter);
#ifdef STA_SUPPORT
/** Flush the scan pending queue */
t_void wlan_flush_scan_queue(pmlan_adapter pmadapter);
t_void wlan_move_cmd_to_cmd_pending_q(pmlan_adapter pmadapter);
mlan_status wlan_cancel_pending_scan_cmd(pmlan_adapter pmadapter,
					 pmlan_ioctl_req pioctl_req);
#endif
/**Cancel pending command */
t_void wlan_cancel_all_pending_cmd(pmlan_adapter pmadapter, t_u8 flag);
/**Cancel pending ioctl */
t_void wlan_cancel_pending_ioctl(pmlan_adapter pmadapter,
				 pmlan_ioctl_req pioctl_req);
/**Cancel bss pending ioctl */
t_void wlan_cancel_bss_pending_cmd(pmlan_adapter pmadapter, t_u32 bss_index);

/** Insert command to free queue */
t_void wlan_insert_cmd_to_free_q(mlan_adapter *pmadapter,
				 cmd_ctrl_node *pcmd_node);

/** Insert command to pending queue */
t_void wlan_insert_cmd_to_pending_q(mlan_adapter *pmadapter,
				    cmd_ctrl_node *pcmd_node, t_u32 addtail);

/** Execute next command */
mlan_status wlan_exec_next_cmd(mlan_adapter *pmadapter);
/** Proecess command response */
mlan_status wlan_process_cmdresp(mlan_adapter *pmadapter);
/** Handle received packet, has extra handling for aggregate packets */
mlan_status wlan_handle_rx_packet(pmlan_adapter pmadapter, pmlan_buffer pmbuf);
/** Process transmission */
mlan_status wlan_process_tx(pmlan_private priv, pmlan_buffer pmbuf,
			    mlan_tx_param *tx_param);
/** Transmit a null data packet */
mlan_status wlan_send_null_packet(pmlan_private priv, t_u8 flags);

#ifdef SDIO
mlan_status wlan_alloc_sdio_mpa_buffers(mlan_adapter *pmadapter,
					t_u32 mpa_tx_buf_size,
					t_u32 mpa_rx_buf_size);

mlan_status wlan_free_sdio_mpa_buffers(mlan_adapter *pmadapter);
#endif

/** Process write data complete */
mlan_status wlan_write_data_complete(pmlan_adapter pmlan_adapter,
				     pmlan_buffer pmbuf, mlan_status status);

#ifdef USB
mlan_status wlan_usb_deaggr_rx_pkt(pmlan_adapter pmadapter, pmlan_buffer pmbuf);

/**
 *  @brief  This function resets USB Tx Aggregation buffers
 *
 *  @param pmadapter	A pointer to mlan_adapter
 *
 *  @return 	N/A
 */
static INLINE t_void
wlan_reset_usb_tx_aggr(pmlan_adapter pmadapter)
{
	t_s32 i = 0;
	pmlan_callbacks pcb = &pmadapter->callbacks;
	ENTER();
	for (i = 0; i < MAX_USB_TX_PORT_NUM; i++) {
		pcb->moal_spin_lock(pmadapter->pmoal_handle,
				    pmadapter->pcard_usb->usb_tx_aggr[i].
				    paggr_lock);
		if (pmadapter->pcard_usb->usb_tx_aggr[i].aggr_hold_timer_is_set) {
			pcb->moal_stop_timer(pmadapter->pmoal_handle,
					     pmadapter->pcard_usb->
					     usb_tx_aggr[i]
					     .paggr_hold_timer);
			pmadapter->pcard_usb->usb_tx_aggr[i]
				.aggr_hold_timer_is_set = MFALSE;
		}
		if (pmadapter->pcard_usb->usb_tx_aggr[i].aggr_ctrl.enable &&
		    pmadapter->pcard_usb->usb_tx_aggr[i].pmbuf_aggr != MNULL) {
			wlan_write_data_complete(pmadapter, pmadapter->pcard_usb->usb_tx_aggr[i].pmbuf_aggr, MLAN_STATUS_FAILURE);	/* did not get sent */
			pmadapter->pcard_usb->usb_tx_aggr[i].pmbuf_aggr = MNULL;
			pmadapter->pcard_usb->usb_tx_aggr[i].aggr_len = 0;
		}
		pcb->moal_spin_unlock(pmadapter->pmoal_handle,
				      pmadapter->pcard_usb->usb_tx_aggr[i].
				      paggr_lock);
	}
	LEAVE();
}

/**
 *  @brief  This function get usb_tx_aggr_params
 *
 *  @param pmadapter	A pointer to mlan_adapter
 *  @param port		    port for TX
 *
 *  @return	            A pointer to usb_tx_aggr_params
 */
static INLINE usb_tx_aggr_params *
wlan_get_usb_tx_aggr_params(pmlan_adapter pmadapter, t_u32 port)
{
	int i;
	ENTER();
	for (i = 0; i < MAX_USB_TX_PORT_NUM; i++) {
		if (pmadapter->pcard_usb->usb_tx_aggr[i].aggr_ctrl.enable &&
		    pmadapter->pcard_usb->usb_tx_aggr[i].port == port)
			return &pmadapter->pcard_usb->usb_tx_aggr[i];
	}
	LEAVE();
	return MNULL;
}

t_void wlan_usb_tx_aggr_timeout_func(t_void *function_context);
mlan_status wlan_usb_host_to_card_aggr(pmlan_adapter pmadapter,
				       pmlan_buffer pmbuf,
				       mlan_tx_param *tx_param,
				       usb_tx_aggr_params *aggr_params);
#endif

/** Process receive packet complete */
mlan_status wlan_recv_packet_complete(pmlan_adapter pmadapter,
				      pmlan_buffer pmbuf, mlan_status status);
/** Clean Tx Rx queues */
t_void wlan_clean_txrx(pmlan_private priv);

t_void wlan_add_buf_bypass_txqueue(mlan_adapter *pmadapter, pmlan_buffer pmbuf);
t_void wlan_process_bypass_tx(mlan_adapter *pmadapter);
t_void wlan_cleanup_bypass_txq(pmlan_private priv);
t_u8 wlan_bypass_tx_list_empty(mlan_adapter *pmadapter);

/** Check if this is the last packet */
t_u8 wlan_check_last_packet_indication(pmlan_private priv);

#define MOAL_ALLOC_MLAN_BUFFER (0)
#define MOAL_MALLOC_BUFFER (1)

#ifdef PCIE
/* This defines the direction arg to the DMA mapping routines. */
#define PCI_DMA_BIDIRECTIONAL 0
#define PCI_DMA_TODEVICE 1
#define PCI_DMA_FROMDEVICE 2
#define PCI_DMA_NONE 3
#endif

/** function to allocate a mlan_buffer */
pmlan_buffer wlan_alloc_mlan_buffer(mlan_adapter *pmadapter, t_u32 data_len,
				    t_u32 head_room, t_u32 malloc_flag);
/** function to free a mlan_buffer */
t_void wlan_free_mlan_buffer(mlan_adapter *pmadapter, pmlan_buffer pmbuf);

/** command resp handler for version ext */
mlan_status wlan_ret_ver_ext(pmlan_private pmpriv, HostCmd_DS_COMMAND *resp,
			     mlan_ioctl_req *pioctl_buf);

/** command resp handler for rx mgmt forward registration */
mlan_status wlan_ret_rx_mgmt_ind(pmlan_private pmpriv, HostCmd_DS_COMMAND *resp,
				 mlan_ioctl_req *pioctl_buf);

/** Check Power Save condition */
t_void wlan_check_ps_cond(mlan_adapter *pmadapter);

/** handle command for enhanced power save mode */
mlan_status wlan_cmd_enh_power_mode(pmlan_private pmpriv,
				    HostCmd_DS_COMMAND *cmd,
				    t_u16 cmd_action, t_u16 ps_bitmap,
				    t_void *pdata_buf);
/** handle command resp for enhanced power save mode */
mlan_status wlan_ret_enh_power_mode(pmlan_private pmpriv,
				    HostCmd_DS_COMMAND *resp,
				    mlan_ioctl_req *pioctl_buf);

/** handle commnand for cfg data */
mlan_status wlan_cmd_cfg_data(pmlan_private pmpriv,
			      HostCmd_DS_COMMAND *pcmd, t_u16 cmd_action,
			      t_u32 cmd_oid, t_void *pdata_buf);
/** handle command resp for cfg data */
mlan_status wlan_ret_cfg_data(pmlan_private pmpriv,
			      HostCmd_DS_COMMAND *resp, t_void *pioctl_buf);

/** Process sleep confirm command response */
void wlan_process_sleep_confirm_resp(pmlan_adapter pmadapter, t_u8 *pbuf,
				     t_u32 len);

/** Perform hs related activities on receving the power up interrupt */
void wlan_process_hs_config(pmlan_adapter pmadapter);

t_void wlan_wakeup_card_timeout_func(void *function_context);

mlan_status wlan_process_802dot11_mgmt_pkt(mlan_private *priv, t_u8 *payload,
					   t_u32 payload_len, RxPD *prx_pd);

mlan_status wlan_pm_ioctl_hscfg(pmlan_adapter pmadapter,
				pmlan_ioctl_req pioctl_req);

mlan_status wlan_radio_ioctl_remain_chan_cfg(pmlan_adapter pmadapter,
					     pmlan_ioctl_req pioctl_req);
mlan_status wlan_cmd_remain_on_channel(pmlan_private pmpriv,
				       HostCmd_DS_COMMAND *cmd,
				       t_u16 cmd_action, t_void *pdata_buf);
mlan_status wlan_ret_remain_on_channel(pmlan_private pmpriv,
				       HostCmd_DS_COMMAND *resp,
				       mlan_ioctl_req *pioctl_buf);

#ifdef WIFI_DIRECT_SUPPORT
mlan_status wlan_bss_ioctl_wifi_direct_mode(pmlan_adapter pmadapter,
					    pmlan_ioctl_req pioctl_req);

mlan_status wlan_cmd_wifi_direct_mode(pmlan_private pmpriv,
				      HostCmd_DS_COMMAND *cmd,
				      t_u16 cmd_action, t_void *pdata_buf);
mlan_status wlan_ret_wifi_direct_mode(pmlan_private pmpriv,
				      HostCmd_DS_COMMAND *resp,
				      mlan_ioctl_req *pioctl_buf);
mlan_status wlan_cmd_p2p_params_config(pmlan_private pmpriv,
				       HostCmd_DS_COMMAND *cmd,
				       t_u16 cmd_action, t_void *pdata_buf);
mlan_status wlan_ret_p2p_params_config(pmlan_private pmpriv,
				       HostCmd_DS_COMMAND *resp,
				       mlan_ioctl_req *pioctl_buf);
mlan_status wlan_misc_p2p_config(pmlan_adapter pmadapter,
				 pmlan_ioctl_req pioctl_req);
#endif
mlan_status wlan_cmd_gpio_tsf_latch(pmlan_private pmpriv,
				    HostCmd_DS_COMMAND *cmd,
				    t_u16 cmd_action,
				    mlan_ioctl_req *pioctl_buf,
				    t_void *pdata_buf);
mlan_status wlan_ret_gpio_tsf_latch(pmlan_private pmpriv,
				    HostCmd_DS_COMMAND *resp,
				    mlan_ioctl_req *pioctl_buf);
mlan_status wlan_misc_gpio_tsf_latch_config(pmlan_adapter pmadapter,
					    pmlan_ioctl_req pioctl_req);
mlan_status wlan_misc_get_tsf_info(pmlan_adapter pmadapter,
				   pmlan_ioctl_req pioctl_req);
/** get ralist info */
int wlan_get_ralist_info(mlan_private *priv, pralist_info buf);
/** dump ralist */
void wlan_dump_ralist(mlan_private *priv);

/** get pm info */
mlan_status wlan_get_pm_info(pmlan_adapter pmadapter,
			     pmlan_ioctl_req pioctl_req);

mlan_status wlan_bss_ioctl_bss_remove(pmlan_adapter pmadapter,
				      pmlan_ioctl_req pioctl_req);

mlan_status wlan_radio_ioctl_mimo_switch_cfg(pmlan_adapter pmadapter,
					     pmlan_ioctl_req pioctl_req);
mlan_status wlan_cmd_802_11_mimo_switch(pmlan_private pmpriv,
					HostCmd_DS_COMMAND *cmd,
					t_void *pdata_buf);

mlan_status wlan_misc_per_pkt_cfg(pmlan_adapter pmadapter,
				  pmlan_ioctl_req pioctl_req);

mlan_status wlan_config_mgmt_filter(pmlan_adapter pmadapter,
				    pmlan_ioctl_req pioctl_req);
mlan_status wlan_get_hs_wakeup_reason(pmlan_adapter pmadapter,
				      pmlan_ioctl_req pioctl_req);

mlan_status wlan_cmd_hs_wakeup_reason(pmlan_private pmpriv,
				      HostCmd_DS_COMMAND *cmd,
				      t_void *pdata_buf);

mlan_status wlan_ret_hs_wakeup_reason(pmlan_private pmpriv,
				      HostCmd_DS_COMMAND *resp,
				      mlan_ioctl_req *pioctl_buf);

mlan_status wlan_get_tx_rx_histogram(pmlan_adapter pmadapter,
				     pmlan_ioctl_req pioctl_req);
mlan_status wlan_cmd_tx_rx_pkt_stats(pmlan_private pmpriv,
				     HostCmd_DS_COMMAND *cmd,
				     pmlan_ioctl_req pioctl_buf,
				     t_void *pdata_buf);
mlan_status wlan_ret_tx_rx_pkt_stats(pmlan_private pmpriv,
				     HostCmd_DS_COMMAND *resp,
				     mlan_ioctl_req *pioctl_buf);

mlan_status wlan_radio_ioctl_radio_ctl(pmlan_adapter pmadapter,
				       pmlan_ioctl_req pioctl_req);

mlan_status wlan_radio_ioctl_ant_cfg(pmlan_adapter pmadapter,
				     pmlan_ioctl_req pioctl_req);

mlan_status wlan_cmd_tx_rate_cfg(pmlan_private pmpriv,
				 HostCmd_DS_COMMAND *cmd,
				 t_u16 cmd_action, t_void *pdata_buf,
				 mlan_ioctl_req *pioctl_buf);
mlan_status wlan_ret_tx_rate_cfg(pmlan_private pmpriv,
				 HostCmd_DS_COMMAND *resp,
				 mlan_ioctl_req *pioctl_buf);

mlan_status wlan_rate_ioctl_cfg(pmlan_adapter pmadapter,
				pmlan_ioctl_req pioctl_req);
mlan_status wlan_ret_802_11_tx_rate_query(pmlan_private pmpriv,
					  HostCmd_DS_COMMAND *resp,
					  mlan_ioctl_req *pioctl_buf);

mlan_status wlan_rate_ioctl_get_data_rate(pmlan_adapter pmadapter,
					  pmlan_ioctl_req pioctl_req);

t_void wlan_host_sleep_activated_event(pmlan_private priv, t_u8 activated);
/** Handles the command response of hs_cfg */
mlan_status wlan_ret_802_11_hs_cfg(pmlan_private pmpriv,
				   HostCmd_DS_COMMAND *resp,
				   mlan_ioctl_req *pioctl_buf);
/** Sends HS_WAKEUP event to applications */
t_void wlan_host_sleep_wakeup_event(pmlan_private priv);

/** Prepares command of robustcoex */
mlan_status wlan_cmd_robustcoex(pmlan_private pmpriv,
				HostCmd_DS_COMMAND *cmd, t_u16 cmd_action,
				t_u16 *pdata_buf);
/** Set Robustcoex gpiocfg */
mlan_status wlan_misc_robustcoex(pmlan_adapter pmadapter,
				 pmlan_ioctl_req pioctl_req);

/** Set mapping policy/get DMCS status */
mlan_status wlan_misc_dmcs_config(pmlan_adapter pmadapter,
				  pmlan_ioctl_req pioctl_req);
/** Prepares command of DMCS config */
mlan_status wlan_cmd_dmcs_config(pmlan_private pmpriv,
				 HostCmd_DS_COMMAND *cmd,
				 t_u16 cmd_action, t_void *pdata_buf);
/** Handles command response of DMCS config */
mlan_status wlan_ret_dmcs_config(pmlan_private pmpriv,
				 HostCmd_DS_COMMAND *resp,
				 mlan_ioctl_req *pioctl_buf);

#if defined(PCIE)
mlan_status wlan_cmd_ssu(pmlan_private pmpriv, HostCmd_DS_COMMAND *cmd,
			 t_u16 cmd_action, t_u16 *pdata_buf);
#endif

/** send get hw spec command to firmware */
mlan_status wlan_adapter_get_hw_spec(pmlan_adapter pmadapter);
/** send adapter specific init cmd to firmware */
mlan_status wlan_adapter_init_cmd(pmlan_adapter pmadapter);
/** get/set bandcfg */
mlan_status wlan_radio_ioctl_band_cfg(pmlan_adapter pmadapter,
				      pmlan_ioctl_req pioctl_req);

#ifdef RX_PACKET_COALESCE
mlan_status wlan_cmd_rx_pkt_coalesce_cfg(pmlan_private pmpriv,
					 HostCmd_DS_COMMAND *cmd,
					 t_u16 cmd_action, t_void *pdata_buf);
mlan_status wlan_ret_rx_pkt_coalesce_cfg(pmlan_private pmpriv,
					 const HostCmd_DS_COMMAND *resp,
					 mlan_ioctl_req *pioctl_buf);
#endif

#ifdef STA_SUPPORT
/** warm reset */
mlan_status wlan_misc_ioctl_warm_reset(pmlan_adapter pmadapter,
				       pmlan_ioctl_req pioctl_req);
/** Process received packet */
mlan_status wlan_process_rx_packet(pmlan_adapter pmadapter, pmlan_buffer pmbuf);
/** ioctl handler for station mode */
mlan_status wlan_ops_sta_ioctl(t_void *adapter, pmlan_ioctl_req pioctl_req);

/** cmd handler for station mode */
mlan_status wlan_ops_sta_prepare_cmd(t_void *priv, t_u16 cmd_no,
				     t_u16 cmd_action, t_u32 cmd_oid,
				     t_void *pioctl_buf,
				     t_void *pdata_buf, t_void *pcmd_buf);

/** cmdresp handler for station mode */
mlan_status wlan_ops_sta_process_cmdresp(t_void *priv, t_u16 cmdresp_no,
					 t_void *pcmd_buf, t_void *pioctl);

/** rx handler for station mode */
mlan_status wlan_ops_sta_process_rx_packet(t_void *adapter, pmlan_buffer pmbuf);

/** event handler for station mode */
mlan_status wlan_ops_sta_process_event(t_void *priv);

/** fill txpd for station mode */
t_void *wlan_ops_sta_process_txpd(t_void *priv, pmlan_buffer pmbuf);

/** send init cmd to firmware for station mode */
mlan_status wlan_ops_sta_init_cmd(t_void *priv, t_u8 first_bss);

/** Flush the scan table */
mlan_status wlan_flush_scan_table(pmlan_adapter pmadapter);

/** Scan for networks */
mlan_status wlan_scan_networks(mlan_private *pmpriv, t_void *pioctl_buf,
			       wlan_user_scan_cfg *puser_scan_in);

/** Scan for specific SSID */
mlan_status wlan_scan_specific_ssid(mlan_private *pmpriv,
				    t_void *pioctl_buf,
				    mlan_802_11_ssid *preq_ssid);

/** Scan command handler */
mlan_status wlan_cmd_802_11_scan(pmlan_private pmpriv,
				 HostCmd_DS_COMMAND *pcmd, t_void *pdata_buf);

/** Handler for scan command response */
mlan_status wlan_ret_802_11_scan(pmlan_private pmpriv,
				 HostCmd_DS_COMMAND *resp, t_void *pioctl_buf);

/** Extended scan command handler */
mlan_status wlan_cmd_802_11_scan_ext(pmlan_private pmpriv,
				     HostCmd_DS_COMMAND *pcmd,
				     t_void *pdata_buf);
/** Handler for extended scan command response */
mlan_status wlan_ret_802_11_scan_ext(pmlan_private pmpriv,
				     HostCmd_DS_COMMAND *resp,
				     t_void *pioctl_buf);
/** Handler event for extended scan report */
mlan_status wlan_handle_event_ext_scan_report(mlan_private *pmpriv,
					      mlan_buffer *pmbuf);
mlan_status wlan_handle_event_ext_scan_status(mlan_private *pmpriv,
					      mlan_buffer *pmbuf);

/** check network compatibility */
t_s32 wlan_is_network_compatible(mlan_private *pmpriv, t_u32 index, t_u32 mode);

/** Find an SSID in a list */
t_s32 wlan_find_ssid_in_list(pmlan_private pmpriv, mlan_802_11_ssid *ssid,
			     t_u8 *bssid, t_u32 mode);

/** Find a BSSID in a list */
t_s32 wlan_find_bssid_in_list(mlan_private *pmpriv, t_u8 *bssid, t_u32 mode);

/** Find best network */
mlan_status wlan_find_best_network(mlan_private *pmpriv,
				   mlan_ssid_bssid *preq_ssid_bssid);

/** Compare two SSIDs */
t_s32 wlan_ssid_cmp(pmlan_adapter pmadapter, mlan_802_11_ssid *ssid1,
		    mlan_802_11_ssid *ssid2);

/** Associate */
mlan_status wlan_associate(mlan_private *pmpriv, IN t_void *pioctl_buf,
			   IN BSSDescriptor_t *pBSSDesc);

/** Associate command handler */
mlan_status wlan_cmd_802_11_associate(mlan_private *pmpriv,
				      HostCmd_DS_COMMAND *cmd,
				      t_void *pdata_buf);

/** Handler for association command response */
mlan_status wlan_ret_802_11_associate(mlan_private *pmpriv,
				      HostCmd_DS_COMMAND *resp,
				      t_void *pioctl_buf);

/** Reset connected state */
t_void wlan_reset_connect_state(pmlan_private priv, t_u8 drv_disconnect);

t_void wlan_2040_coex_event(pmlan_private pmpriv);

/** convert band to radio type */
t_u8 wlan_band_to_radio_type(t_u8 band);
/** convert radio_type to band */
t_u8 radio_type_to_band(t_u8 chanBand);

/** Disconnect */
mlan_status wlan_disconnect(mlan_private *pmpriv,
			    mlan_ioctl_req *pioctl_req,
			    mlan_deauth_param *deauth_param);

/** Ad-Hoc start */
mlan_status wlan_adhoc_start(mlan_private *pmpriv, t_void *pioctl_buf,
			     mlan_802_11_ssid *padhoc_ssid);

/** Ad-Hoc join */
mlan_status wlan_adhoc_join(mlan_private *pmpriv, t_void *pioctl_buf,
			    BSSDescriptor_t *pBSSDesc);

/** Ad-Hoc start command handler */
mlan_status wlan_cmd_802_11_ad_hoc_start(mlan_private *pmpriv,
					 HostCmd_DS_COMMAND *cmd,
					 t_void *pdata_buf);

/** Ad-Hoc command handler */
mlan_status wlan_cmd_802_11_ad_hoc_join(mlan_private *pmpriv,
					HostCmd_DS_COMMAND *cmd,
					t_void *pdata_buf);

/** Handler for Ad-Hoc commands */
mlan_status wlan_ret_802_11_ad_hoc(mlan_private *pmpriv,
				   HostCmd_DS_COMMAND *resp,
				   t_void *pioctl_buf);

/** Handler for bgscan query commands */
mlan_status wlan_cmd_802_11_bg_scan_query(mlan_private *pmpriv,
					  HostCmd_DS_COMMAND *pcmd,
					  t_void *pdata_buf);
/** Handler for bgscan config command */
mlan_status wlan_cmd_bgscan_config(mlan_private *pmpriv,
				   HostCmd_DS_COMMAND *pcmd, t_void *pdata_buf);
/** Hander for bgscan config command response */
mlan_status wlan_ret_bgscan_config(mlan_private *pmpriv,
				   HostCmd_DS_COMMAND *resp,
				   mlan_ioctl_req *pioctl_buf);
mlan_status wlan_ret_802_11_bgscan_query(mlan_private *pmpriv,
					 HostCmd_DS_COMMAND *resp,
					 mlan_ioctl_req *pioctl_buf);

/** Get Channel-Frequency-Power by band and channel */
chan_freq_power_t *wlan_get_cfp_by_band_and_channel(pmlan_adapter pmadapter,
						    t_u8 band, t_u16 channel,
						    region_chan_t
						    *region_channel);
/** Find Channel-Frequency-Power by band and channel */
chan_freq_power_t *wlan_find_cfp_by_band_and_channel(mlan_adapter *pmadapter,
						     t_u8 band, t_u16 channel);
/** Find Channel-Frequency-Power by band and frequency */
chan_freq_power_t *wlan_find_cfp_by_band_and_freq(mlan_adapter *pmadapter,
						  t_u8 band, t_u32 freq);
/** Get Tx power of channel from Channel-Frequency-Power */
t_u8 wlan_get_txpwr_of_chan_from_cfp(mlan_private *pmpriv, t_u8 channel);
/** find frequency from band and channel */
t_u32 wlan_find_freq_from_band_chan(t_u8, t_u8);

/*  Save a beacon buffer of the current bss descriptor */
t_void wlan_save_curr_bcn(mlan_private *pmpriv);
/*  Free a beacon buffer of the current bss descriptor */
t_void wlan_free_curr_bcn(mlan_private *pmpriv);

#endif /* STA_SUPPORT */

/* Rate related functions */
t_u8 wlan_convert_v14_tx_rate_info(pmlan_private pmpriv, t_u8 v14_rate_info);
t_u8 wlan_convert_v14_rx_rate_info(pmlan_private pmpriv, t_u8 v14_rate_info);
/** Convert index into data rate */
t_u32 wlan_index_to_data_rate(pmlan_adapter pmadapter, t_u8 index,
			      t_u8 rate_info, t_u8 ext_rate_info);
/** Get active data rates */
t_u32 wlan_get_active_data_rates(mlan_private *pmpriv, t_u32 bss_mode,
				 t_u16 config_bands, WLAN_802_11_RATES rates);
/** Get supported data rates */
t_u32 wlan_get_supported_rates(mlan_private *pmpriv, t_u32 bss_mode,
			       t_u16 config_bands, WLAN_802_11_RATES rates);
/** Convert data rate to index */
t_u8 wlan_data_rate_to_index(pmlan_adapter pmadapter, t_u32 rate);
/** Check if rate is auto */
t_u8 wlan_is_rate_auto(mlan_private *pmpriv);
/** Get rate index */
int wlan_get_rate_index(pmlan_adapter pmadapter, t_u16 *rateBitmap, int size);
mlan_status wlan_cmd_rxabortcfg(pmlan_private pmpriv,
				HostCmd_DS_COMMAND *cmd, t_u16 cmd_action,
				t_void *pdata_buf);
mlan_status wlan_ret_rxabortcfg(pmlan_private pmpriv,
				HostCmd_DS_COMMAND *resp,
				mlan_ioctl_req *pioctl_buf);
mlan_status wlan_cmd_rxabortcfg_ext(pmlan_private pmpriv,
				    HostCmd_DS_COMMAND *cmd,
				    t_u16 cmd_action, t_void *pdata_buf);
mlan_status wlan_ret_rxabortcfg_ext(pmlan_private pmpriv,
				    HostCmd_DS_COMMAND *resp,
				    mlan_ioctl_req *pioctl_buf);
mlan_status wlan_cmd_tx_ampdu_prot_mode(pmlan_private pmpriv,
					HostCmd_DS_COMMAND *cmd,
					t_u16 cmd_action, t_void *pdata_buf);
mlan_status wlan_ret_tx_ampdu_prot_mode(pmlan_private pmpriv,
					HostCmd_DS_COMMAND *resp,
					mlan_ioctl_req *pioctl_buf);
mlan_status wlan_cmd_dot11mc_unassoc_ftm_cfg(pmlan_private pmpriv,
					     HostCmd_DS_COMMAND *cmd,
					     t_u16 cmd_action,
					     t_void *pdata_buf);
mlan_status wlan_ret_dot11mc_unassoc_ftm_cfg(pmlan_private pmpriv,
					     HostCmd_DS_COMMAND *resp,
					     mlan_ioctl_req *pioctl_buf);
mlan_status wlan_cmd_hal_phy_cfg(pmlan_private pmpriv,
				 HostCmd_DS_COMMAND *cmd,
				 t_u16 cmd_action, t_u16 *pdata_buf);
mlan_status wlan_ret_hal_phy_cfg(pmlan_private pmpriv,
				 HostCmd_DS_COMMAND *resp,
				 mlan_ioctl_req *pioctl_buf);

mlan_status wlan_cmd_rate_adapt_cfg(pmlan_private pmpriv,
				    HostCmd_DS_COMMAND *cmd,
				    t_u16 cmd_action, t_void *pdata_buf);
mlan_status wlan_ret_rate_adapt_cfg(pmlan_private pmpriv,
				    HostCmd_DS_COMMAND *resp,
				    mlan_ioctl_req *pioctl_buf);
mlan_status wlan_cmd_cck_desense_cfg(pmlan_private pmpriv,
				     HostCmd_DS_COMMAND *cmd,
				     t_u16 cmd_action, t_void *pdata_buf);

mlan_status wlan_ret_cck_desense_cfg(pmlan_private pmpriv,
				     HostCmd_DS_COMMAND *resp,
				     mlan_ioctl_req *pioctl_buf);

mlan_status wlan_cmd_arb_cfg(pmlan_private pmpriv,
			     HostCmd_DS_COMMAND *cmd, t_u16 cmd_action,
			     t_void *pdata_buf);
mlan_status wlan_ret_arb_cfg(pmlan_private pmpriv,
			     HostCmd_DS_COMMAND *resp,
			     mlan_ioctl_req *pioctl_buf);

mlan_status wlan_misc_ioctl_rxabortcfg(pmlan_adapter pmadapter,
				       pmlan_ioctl_req pioctl_req);
mlan_status wlan_misc_ioctl_rxabortcfg_ext(pmlan_adapter pmadapter,
					   pmlan_ioctl_req pioctl_req);
mlan_status wlan_misc_ioctl_tx_ampdu_prot_mode(pmlan_adapter pmadapter,
					       pmlan_ioctl_req pioctl_req);
mlan_status wlan_misc_ioctl_dot11mc_unassoc_ftm_cfg(pmlan_adapter pmadapter,
						    pmlan_ioctl_req pioctl_req);
mlan_status wlan_misc_ioctl_rate_adapt_cfg(pmlan_adapter pmadapter,
					   pmlan_ioctl_req pioctl_req);
mlan_status wlan_misc_ioctl_cck_desense_cfg(pmlan_adapter pmadapter,
					    pmlan_ioctl_req pioctl_req);
mlan_status wlan_cmd_mfg(pmlan_private pmpriv, pHostCmd_DS_COMMAND cmd,
			 t_u16 cmd_action, t_pvoid pdata_buf);
mlan_status wlan_ret_mfg(pmlan_private pmpriv, HostCmd_DS_COMMAND *resp,
			 mlan_ioctl_req *pioctl_buf);
mlan_status wlan_misc_ioctl_rf_test_cfg(pmlan_adapter pmadapter,
					pmlan_ioctl_req pioctl_req);
mlan_status wlan_misc_ioctl_range_ext(pmlan_adapter pmadapter,
				      pmlan_ioctl_req pioctl_req);
mlan_status wlan_misc_ioctl_arb_cfg(pmlan_adapter pmadapter,
				    pmlan_ioctl_req pioctl_req);
mlan_status wlan_misc_ioctl_tp_state(pmlan_adapter pmadapter,
				     pmlan_ioctl_req pioctl_req);
/* CFP related functions */
/** Region code index table */
extern t_u16 region_code_index[MRVDRV_MAX_REGION_CODE];
/** The table to keep CFP code for BG */
extern t_u16 cfp_code_index_bg[MRVDRV_MAX_CFP_CODE_BG];
/** The table to keep CFP code for A */
extern t_u16 cfp_code_index_a[MRVDRV_MAX_CFP_CODE_A];

/** Set region table */
mlan_status wlan_set_regiontable(mlan_private *pmpriv, t_u8 region, t_u8 band);
/** Get radar detection requirements*/
t_bool wlan_get_cfp_radar_detect(mlan_private *priv, t_u8 chnl);
/** check if scan type is passive for b/g band*/
t_bool wlan_bg_scan_type_is_passive(mlan_private *priv, t_u8 chnl);
/** check if channel is NO_IR (passive) */
t_bool wlan_is_chan_passive(mlan_private *priv, t_u8 band, t_u8 chan);
/** check if channel is disabled */
t_bool wlan_is_chan_disabled(mlan_private *priv, t_u8 band, t_u8 chan);
/** check if channel is blacklisted */
t_bool wlan_is_chan_blacklisted(mlan_private *priv, t_u8 band, t_u8 chan);
/** set blacklist setting for a channel */
t_bool wlan_set_chan_blacklist(mlan_private *priv, t_u8 band, t_u8 chan,
			       t_bool bl);

/* 802.11D related functions */
/** Initialize 11D */
t_void wlan_11d_priv_init(mlan_private *pmpriv);
/** Initialize 11D */
t_void wlan_11d_init(mlan_adapter *pmadapter);
/** Enable 11D */
mlan_status wlan_11d_enable(mlan_private *pmpriv, t_void *pioctl_buf,
			    state_11d_t flag);
/** Get if 11D is enabled */
t_bool wlan_11d_is_enabled(mlan_private *pmpriv);
/** Get if FW 11D is enabled */
t_bool wlan_fw_11d_is_enabled(mlan_private *pmpriv);
/** Get if priv is station */
t_bool wlan_is_station(mlan_private *pmpriv);
/** Command handler for 11D country info */
mlan_status wlan_cmd_802_11d_domain_info(mlan_private *pmpriv,
					 HostCmd_DS_COMMAND *pcmd,
					 t_u16 cmd_action);
/** Handler for 11D country info command response */
mlan_status wlan_ret_802_11d_domain_info(mlan_private *pmpriv,
					 HostCmd_DS_COMMAND *resp);
/** Convert channel to frequency */
t_u32 wlan_11d_chan_2_freq(pmlan_adapter pmadapter, t_u8 chan, t_u8 band);
#ifdef STA_SUPPORT
/** Set 11D universal table */
mlan_status wlan_11d_set_universaltable(mlan_private *pmpriv, t_u8 band);
/** Clear 11D region table */
mlan_status wlan_11d_clear_parsedtable(mlan_private *pmpriv);
/** Create 11D country information for downloading */
mlan_status wlan_11d_create_dnld_countryinfo(mlan_private *pmpriv, t_u8 band);
/** Get scan type from 11D info */
t_u8 wlan_11d_get_scan_type(pmlan_adapter pmadapter, t_u8 band, t_u8 chan,
			    parsed_region_chan_11d_t *parsed_region_chan);
/** Parse 11D country info */
mlan_status wlan_11d_parse_dnld_countryinfo(mlan_private *pmpriv,
					    BSSDescriptor_t *pBSSDesc);
/** Prepare 11D domain information for download */
mlan_status wlan_11d_prepare_dnld_domain_info_cmd(mlan_private *pmpriv);
/** Parse 11D country information into domain info */
mlan_status wlan_11d_parse_domain_info(pmlan_adapter pmadapter,
				       IEEEtypes_CountryInfoFullSet_t
				       *country_info, t_u8 band,
				       parsed_region_chan_11d_t
				       *parsed_region_chan);
#endif /* STA_SUPPORT */
#ifdef UAP_SUPPORT
/** Handle 11D domain information from UAP */
mlan_status wlan_11d_handle_uap_domain_info(mlan_private *pmpriv, t_u8 band,
					    t_u8 *domain_tlv,
					    t_void *pioctl_buf);
#endif
/** Configure 11D domain info command */
mlan_status wlan_11d_cfg_domain_info(pmlan_adapter pmadapter,
				     mlan_ioctl_req *pioctl_req);

/** This function converts region string to CFP table code */
mlan_status wlan_misc_country_2_cfp_table_code(pmlan_adapter pmadapter,
					       t_u8 *country_code,
					       t_u8 *cfp_bg, t_u8 *cfp_a);

/** This function finds if given country code is in EU table */
t_bool wlan_is_etsi_country(pmlan_adapter pmadapter, t_u8 *country_code);

/** check if station list is empty */
t_u8 wlan_is_station_list_empty(mlan_private *priv);
/** get station node */
sta_node *wlan_get_station_entry(mlan_private *priv, t_u8 *mac);
/** delete station list */
t_void wlan_delete_station_list(pmlan_private priv);
/** delete station entry */
t_void wlan_delete_station_entry(mlan_private *priv, t_u8 *mac);
/** add station entry */
sta_node *wlan_add_station_entry(mlan_private *priv, t_u8 *mac);
/** process uap rx packet */

void wlan_check_sta_capability(pmlan_private priv, pmlan_buffer pevent,
			       sta_node *sta_ptr);
/** find specific ie */
t_u8 *wlan_get_specific_ie(pmlan_private priv, t_u8 *ie_buf, t_u8 ie_len,
			   IEEEtypes_ElementId_e id, t_u8 ext_id);
t_u8 wlan_is_wmm_ie_present(pmlan_adapter pmadapter, t_u8 *pbuf, t_u16 buf_len);

/**
 *  @brief This function checks whether a station TDLS link is enabled or not
 *
 *  @param priv     A pointer to mlan_private
 *  @param mac      station mac address
 *  @return
 * TDLS_NOT_SETUP/TDLS_SETUP_INPROGRESS/TDLS_SETUP_COMPLETE/TDLS_SETUP_FAILURE/TDLS_TEAR_DOWN
 */
static INLINE tdlsStatus_e
wlan_get_tdls_link_status(mlan_private *priv, t_u8 *mac)
{
	sta_node *sta_ptr = MNULL;
	sta_ptr = wlan_get_station_entry(priv, mac);
	if (sta_ptr)
		return sta_ptr->status;
	return TDLS_NOT_SETUP;
}

/**
 *  @brief This function checks if TDLS link is in channel switching
 *
 *  @param status     tdls link status
 *  @return         MTRUE/MFALSE
 */
static INLINE int
wlan_is_tdls_link_chan_switching(tdlsStatus_e status)
{
	return (status == TDLS_SWITCHING_CHANNEL) ? MTRUE : MFALSE;
}

/**
 *  @brief This function checks if send command to firmware is allowed
 *
 *  @param status     tdls link status
 *  @return         MTRUE/MFALSE
 */
static INLINE int
wlan_is_send_cmd_allowed(tdlsStatus_e status)
{
	int ret = MTRUE;
	switch (status) {
	case TDLS_SWITCHING_CHANNEL:
	case TDLS_IN_OFF_CHANNEL:
		ret = MFALSE;
		break;
	default:
		break;
	}
	return ret;
}

/**
 *  @brief This function checks if TDLS link is setup
 *
 *  @param status     tdls link status
 *  @return         MTRUE/MFALSE
 */
static INLINE int
wlan_is_tdls_link_setup(tdlsStatus_e status)
{
	int ret = MFALSE;
	switch (status) {
	case TDLS_SWITCHING_CHANNEL:
	case TDLS_IN_OFF_CHANNEL:
	case TDLS_IN_BASE_CHANNEL:
	case TDLS_SETUP_COMPLETE:
		ret = MTRUE;
		break;
	default:
		break;
	}
	return ret;
}

/**
 *  @brief This function checks tx_pause flag for peer
 *
 *  @param priv     A pointer to mlan_private
 *  @param ra       Address of the receiver STA
 *
 *  @return         MTRUE or MFALSE
 */
static INLINE int
wlan_is_tx_pause(mlan_private *priv, t_u8 *ra)
{
	sta_node *sta_ptr = MNULL;
	sta_ptr = wlan_get_station_entry(priv, ra);
	if (sta_ptr)
		return sta_ptr->tx_pause;
	return MFALSE;
}

t_u16 wlan_update_ralist_tx_pause(pmlan_private priv, t_u8 *mac, t_u8 tx_pause);

#ifdef UAP_SUPPORT
mlan_status wlan_process_uap_rx_packet(mlan_private *priv, pmlan_buffer pmbuf);
t_void wlan_drop_tx_pkts(pmlan_private priv);
#endif /* UAP_SUPPORT */

#ifdef UAP_SUPPORT
/* process the recevied packet and bridge the packet */
mlan_status wlan_uap_recv_packet(mlan_private *priv, pmlan_buffer pmbuf);
#endif /* UAP_SUPPORT */

mlan_status wlan_misc_ioctl_custom_ie_list(pmlan_adapter pmadapter,
					   pmlan_ioctl_req pioctl_req,
					   t_bool send_ioctl);

mlan_status wlan_cmd_get_hw_spec(pmlan_private pmpriv,
				 HostCmd_DS_COMMAND *pcmd);
mlan_status wlan_ret_get_hw_spec(pmlan_private pmpriv,
				 HostCmd_DS_COMMAND *resp, t_void *pioctl_buf);
#ifdef SDIO
mlan_status wlan_cmd_sdio_rx_aggr_cfg(HostCmd_DS_COMMAND *pcmd,
				      t_u16 cmd_action, t_void *pdata_buf);
mlan_status wlan_ret_sdio_rx_aggr_cfg(pmlan_private pmpriv,
				      HostCmd_DS_COMMAND *resp);
#endif

mlan_status wlan_misc_ioctl_mac_control(pmlan_adapter pmadapter,
					pmlan_ioctl_req pioctl_req);
mlan_status wlan_cmd_mac_control(pmlan_private pmpriv,
				 HostCmd_DS_COMMAND *pcmd,
				 t_u16 cmd_action, t_void *pdata_buf);
mlan_status wlan_ret_mac_control(pmlan_private pmpriv,
				 HostCmd_DS_COMMAND *resp,
				 mlan_ioctl_req *pioctl_buf);

mlan_status wlan_cmd_cw_mode_ctrl(pmlan_private pmpriv,
				  HostCmd_DS_COMMAND *cmd,
				  t_u16 cmd_action, t_void *pdata_buf);
mlan_status wlan_ret_cw_mode_ctrl(pmlan_private pmpriv,
				  HostCmd_DS_COMMAND *resp,
				  mlan_ioctl_req *pioctl_buf);

mlan_status wlan_cmd_802_11_radio_control(pmlan_private pmpriv,
					  HostCmd_DS_COMMAND *cmd,
					  t_u16 cmd_action, t_void *pdata_buf);
mlan_status wlan_ret_802_11_radio_control(pmlan_private pmpriv,
					  HostCmd_DS_COMMAND *resp,
					  mlan_ioctl_req *pioctl_buf);

mlan_status wlan_cmd_802_11_rf_antenna(pmlan_private pmpriv,
				       HostCmd_DS_COMMAND *cmd,
				       t_u16 cmd_action, t_void *pdata_buf);
mlan_status wlan_ret_802_11_rf_antenna(pmlan_private pmpriv,
				       HostCmd_DS_COMMAND *resp,
				       mlan_ioctl_req *pioctl_buf);

mlan_status wlan_ret_reg_access(mlan_adapter *pmadapter, t_u16 type,
				HostCmd_DS_COMMAND *resp,
				mlan_ioctl_req *pioctl_buf);
mlan_status wlan_ret_mem_access(pmlan_private pmpriv,
				HostCmd_DS_COMMAND *resp,
				mlan_ioctl_req *pioctl_buf);

mlan_status wlan_reg_mem_ioctl_reg_rw(pmlan_adapter pmadapter,
				      pmlan_ioctl_req pioctl_req);
mlan_status wlan_reg_mem_ioctl_read_eeprom(pmlan_adapter pmadapter,
					   pmlan_ioctl_req pioctl_req);
mlan_status wlan_reg_mem_ioctl_mem_rw(pmlan_adapter pmadapter,
				      pmlan_ioctl_req pioctl_req);
mlan_status wlan_cmd_reg_access(pmlan_private pmpriv,
				HostCmd_DS_COMMAND *cmd, t_u16 cmd_action,
				t_void *pdata_buf);
mlan_status wlan_cmd_mem_access(HostCmd_DS_COMMAND *cmd, t_u16 cmd_action,
				t_void *pdata_buf);
mlan_status wlan_cmd_802_11_mac_address(pmlan_private pmpriv,
					HostCmd_DS_COMMAND *cmd,
					t_u16 cmd_action);
mlan_status wlan_ret_802_11_mac_address(pmlan_private pmpriv,
					HostCmd_DS_COMMAND *resp,
					mlan_ioctl_req *pioctl_buf);

int wlan_get_tdls_list(mlan_private *priv, tdls_peer_info *buf);
t_void wlan_hold_tdls_packets(pmlan_private priv, t_u8 *mac);
t_void wlan_restore_tdls_packets(pmlan_private priv, t_u8 *mac,
				 tdlsStatus_e status);
t_void wlan_update_non_tdls_ralist(mlan_private *priv, t_u8 *mac,
				   t_u8 tx_pause);
mlan_status wlan_misc_ioctl_tdls_config(pmlan_adapter pmadapter,
					pmlan_ioctl_req pioctl_req);
void wlan_11n_send_delba_to_peer(mlan_private *priv, t_u8 *ra);
void wlan_process_tdls_action_frame(pmlan_private priv, t_u8 *pbuf, t_u32 len);
mlan_status wlan_misc_ioctl_tdls_oper(pmlan_adapter pmadapter,
				      pmlan_ioctl_req pioctl_req);

mlan_status wlan_misc_ioctl_tdls_get_ies(pmlan_adapter pmadapter,
					 pmlan_ioctl_req pioctl_req);
mlan_status wlan_misc_ioctl_tdls_idle_time(pmlan_adapter pmadapter,
					   pmlan_ioctl_req pioctl_req);

t_void wlan_tdls_config(pmlan_private pmpriv, t_u8 enable);
mlan_status wlan_misc_ioctl_tdls_cs_channel(pmlan_adapter pmadapter,
					    pmlan_ioctl_req pioctl_req);

mlan_status wlan_get_info_ver_ext(pmlan_adapter pmadapter,
				  pmlan_ioctl_req pioctl_req);

mlan_status wlan_ioctl_link_statistic(mlan_private *pmpriv,
				      pmlan_ioctl_req pioctl_req);

mlan_status wlan_cmd_802_11_link_statistic(pmlan_private pmpriv,
					   HostCmd_DS_COMMAND *cmd,
					   t_u16 cmd_action,
					   mlan_ioctl_req *pioctl_buf);

mlan_status wlan_ret_get_link_statistic(pmlan_private pmpriv,
					HostCmd_DS_COMMAND *resp,
					mlan_ioctl_req *pioctl_buf);

mlan_status wlan_reg_rx_mgmt_ind(pmlan_adapter pmadapter,
				 pmlan_ioctl_req pioctl_req);

#ifdef DEBUG_LEVEL1
mlan_status wlan_set_drvdbg(pmlan_adapter pmadapter,
			    pmlan_ioctl_req pioctl_req);
#endif

mlan_status wlan_misc_hotspot_cfg(pmlan_adapter pmadapter,
				  pmlan_ioctl_req pioctl_req);

#ifdef STA_SUPPORT
mlan_status wlan_misc_ext_capa_cfg(pmlan_adapter pmadapter,
				   pmlan_ioctl_req pioctl_req);

t_u32 wlan_is_ext_capa_support(mlan_private *pmpriv);
#endif

#ifdef STA_SUPPORT
void wlan_add_ext_capa_info_ie(mlan_private *pmpriv,
			       BSSDescriptor_t *pbss_desc, t_u8 **pptlv_out);
#endif

mlan_status wlan_cmd_boot_sleep(pmlan_private pmpriv,
				HostCmd_DS_COMMAND *cmd, t_u16 cmd_action,
				t_void *pdata_buf);

mlan_status wlan_ret_boot_sleep(pmlan_private pmpriv,
				HostCmd_DS_COMMAND *resp,
				mlan_ioctl_req *pioctl_buf);

#if defined(DRV_EMBEDDED_AUTHENTICATOR) || defined(DRV_EMBEDDED_SUPPLICANT)
mlan_status wlan_cmd_crypto(pmlan_private pmpriv, HostCmd_DS_COMMAND *cmd,
			    t_u16 cmd_action, t_u16 *pdata_buf);

mlan_status wlan_ret_crypto(pmlan_private pmpriv,
			    HostCmd_DS_COMMAND *resp,
			    mlan_ioctl_req *pioctl_buf);
#endif

#define BW_20MHZ 0
#define BW_40MHZ 1
#define BW_80MHZ 2
#define BW_160MHZ 3
int wlan_add_supported_oper_class_ie(mlan_private *pmpriv,
				     t_u8 **pptlv_out, t_u8 curr_oper_class);
mlan_status wlan_get_curr_oper_class(mlan_private *pmpriv, t_u8 channel,
				     t_u8 bw, t_u8 *oper_class);
mlan_status wlan_check_operclass_validation(mlan_private *pmpriv, t_u8 channel,
					    t_u8 oper_class);
mlan_status wlan_misc_ioctl_operclass_validation(pmlan_adapter pmadapter,
						 mlan_ioctl_req *pioctl_req);
mlan_status wlan_misc_ioctl_oper_class(pmlan_adapter pmadapter,
				       mlan_ioctl_req *pioctl_req);

t_u16 wlan_adjust_data_rate(mlan_private *priv, t_u8 rx_rate, t_u8 rate_info);
t_u8 wlan_adjust_antenna(pmlan_private priv, RxPD *prx_pd);

mlan_status wlan_misc_otp_user_data(pmlan_adapter pmadapter,
				    pmlan_ioctl_req pioctl_req);

#ifdef USB
extern mlan_adapter_operations mlan_usb_ops;

mlan_status wlan_misc_ioctl_usb_aggr_ctrl(pmlan_adapter pmadapter,
					  pmlan_ioctl_req pioctl_req);
#endif

mlan_status wlan_misc_ioctl_aggr_ctrl(pmlan_adapter pmadapter,
				      pmlan_ioctl_req pioctl_req);
mlan_status wlan_cmd_packet_aggr_ctrl(pmlan_private pmpriv,
				      HostCmd_DS_COMMAND *cmd,
				      t_u16 cmd_action, t_void *pdata_buf);
mlan_status wlan_ret_packet_aggr_ctrl(pmlan_private pmpriv,
				      HostCmd_DS_COMMAND *resp,
				      mlan_ioctl_req *pioctl_buf);
mlan_status wlan_misc_ioctl_txcontrol(pmlan_adapter pmadapter,
				      pmlan_ioctl_req pioctl_req);

mlan_status wlan_misc_ioctl_region(pmlan_adapter pmadapter,
				   pmlan_ioctl_req pioctl_req);

#ifdef RX_PACKET_COALESCE
mlan_status

wlan_misc_ioctl_rx_pkt_coalesce_config(pmlan_adapter pmadapter,
				       pmlan_ioctl_req pioctl_req);
#endif

void wlan_bt_coex_wlan_param_update_event(pmlan_private priv,
					  pmlan_buffer pevent);

mlan_status wlan_misc_ioctl_dfs_repeater_cfg(pmlan_adapter pmadapter,
					     pmlan_ioctl_req pioctl_req);

t_bool wlan_check_interface_active(mlan_adapter *pmadapter);

mlan_status wlan_misc_ioctl_coalesce_cfg(pmlan_adapter pmadapter,
					 pmlan_ioctl_req pioctl_req);

mlan_status wlan_misc_ioctl_low_pwr_mode(pmlan_adapter pmadapter,
					 pmlan_ioctl_req pioctl_req);

mlan_status wlan_misc_ioctl_pmic_configure(pmlan_adapter pmadapter,
					   pmlan_ioctl_req pioctl_req);

mlan_status wlan_misc_ioctl_cwmode_ctrl(pmlan_adapter pmadapter,
					pmlan_ioctl_req pioctl_req);

mlan_status wlan_set_mef_entry(mlan_private *pmpriv,
			       pmlan_adapter pmadapter, mef_cfg_data * pmef);
mlan_status wlan_process_mef_cfg_cmd(mlan_private *pmpriv,
				     pmlan_adapter pmadapter);
mlan_status wlan_misc_ioctl_mef_flt_cfg(pmlan_adapter pmadapter,
					pmlan_ioctl_req pioctl_req);

mlan_status wlan_misc_ioctl_ind_rst_cfg(pmlan_adapter pmadapter,
					pmlan_ioctl_req pioctl_req);
mlan_status wlan_cmd_ind_rst_cfg(HostCmd_DS_COMMAND *cmd,
				 t_u16 cmd_action, t_void *pdata_buf);
mlan_status wlan_ret_ind_rst_cfg(pmlan_private pmpriv,
				 HostCmd_DS_COMMAND *resp,
				 mlan_ioctl_req *pioctl_buf);

mlan_status wlan_cmd_802_11_supplicant_pmk(pmlan_private pmpriv,
					   HostCmd_DS_COMMAND *cmd,
					   t_u16 cmd_action, t_void *pdata_buf);

mlan_status wlan_ret_802_11_supplicant_pmk(pmlan_private pmpriv,
					   HostCmd_DS_COMMAND *resp,
					   mlan_ioctl_req *pioctl_buf);

mlan_status wlan_sec_ioctl_passphrase(pmlan_adapter pmadapter,
				      pmlan_ioctl_req pioctl_req);

mlan_status wlan_misc_ioctl_get_tsf(pmlan_adapter pmadapter,
				    pmlan_ioctl_req pioctl_req);
void wlan_add_fw_cfp_tables(pmlan_private pmpriv, t_u8 *buf, t_u16 buf_left);

void wlan_free_fw_cfp_tables(mlan_adapter *pmadapter);

mlan_status wlan_misc_chan_reg_cfg(pmlan_adapter pmadapter,
				   pmlan_ioctl_req pioctl_req);

mlan_status wlan_get_cfp_table(pmlan_adapter pmadapter,
			       pmlan_ioctl_req pioctl_req);
mlan_status wlan_get_cfpinfo(pmlan_adapter pmadapter,
			     pmlan_ioctl_req pioctl_req);

mlan_status wlan_cmd_get_tsf(pmlan_private pmpriv, HostCmd_DS_COMMAND *cmd,
			     t_u16 cmd_action);
mlan_status wlan_ret_get_tsf(pmlan_private pmpriv,
			     HostCmd_DS_COMMAND *resp,
			     mlan_ioctl_req *pioctl_buf);

t_u8 wlan_ft_akm_is_used(mlan_private *pmpriv, t_u8 *rsn_ie);

mlan_status wlan_get_rgchnpwr_cfg(pmlan_adapter pmadapter,
				  mlan_ioctl_req *pioctl_req);
mlan_status wlan_get_chan_trpc_cfg(pmlan_adapter pmadapter,
				   mlan_ioctl_req *pioctl_req);
mlan_status wlan_cmd_get_chan_trpc_config(pmlan_private pmpriv,
					  HostCmd_DS_COMMAND *cmd,
					  t_u16 cmd_action, t_void *pdata_buf);
mlan_status wlan_ret_get_chan_trpc_config(pmlan_private pmpriv,
					  HostCmd_DS_COMMAND *resp,
					  mlan_ioctl_req *pioctl_buf);

mlan_status wlan_cmd_ps_inactivity_timeout(pmlan_private pmpriv,
					   HostCmd_DS_COMMAND *cmd,
					   t_u16 cmd_action, t_void *pdata_buf);

t_u8 wlan_ieee_rateid_to_mrvl_rateid(mlan_private *priv,
				     t_u16 IeeeMacRate, t_u8 *dst_mac);
t_u8 wlan_mrvl_rateid_to_ieee_rateid(t_u8 rate);

t_u8 wlan_get_center_freq_idx(mlan_private *pmpriv, t_u16 band,
			      t_u32 pri_chan, t_u8 chan_bw);

mlan_status wlan_ret_chan_region_cfg(pmlan_private pmpriv,
				     HostCmd_DS_COMMAND *resp,
				     mlan_ioctl_req *pioctl_buf);

mlan_status wlan_misc_ioctl_fw_dump_event(pmlan_adapter pmadapter,
					  mlan_ioctl_req *pioctl_req);
mlan_status wlan_cmd_fw_dump_event(pmlan_private pmpriv,
				   HostCmd_DS_COMMAND *cmd,
				   t_u16 cmd_action, t_void *pdata_buf);

mlan_status wlan_misc_bootsleep(pmlan_adapter pmadapter,
				pmlan_ioctl_req pioctl_req);

mlan_status wlan_misc_ioctl_dyn_bw(pmlan_adapter pmadapter,
				   mlan_ioctl_req *pioctl_req);
mlan_status wlan_cmd_config_dyn_bw(pmlan_private pmpriv,
				   HostCmd_DS_COMMAND *cmd,
				   t_u16 cmd_action, t_void *pdata_buf);
mlan_status wlan_ret_dyn_bw(pmlan_private pmpriv,
			    HostCmd_DS_COMMAND *resp,
			    mlan_ioctl_req *pioctl_buf);

#ifdef UAP_SUPPORT
/** wacp mode misc ioctl */
mlan_status wlan_misc_ioctl_wacp_mode(pmlan_adapter pmadapter,
				      mlan_ioctl_req *pioctl_req);
#endif

mlan_status wlan_power_ioctl_set_get_lpm(pmlan_adapter pmadapter,
					 pmlan_ioctl_req pioctl_req);
mlan_status wlan_cmd_set_get_low_power_mode_cfg(pmlan_private pmpriv,
						HostCmd_DS_COMMAND *cmd,
						t_u16 cmd_action,
						t_void *pdata_buf);
mlan_status wlan_ret_set_get_low_power_mode_cfg(pmlan_private pmpriv,
						HostCmd_DS_COMMAND *resp,
						mlan_ioctl_req *pioctl_buf);

mlan_status wlan_cmd_range_ext(pmlan_private pmpriv,
			       HostCmd_DS_COMMAND *cmd,
			       t_u16 cmd_action, t_void *pdata_buf);
mlan_status wlan_ret_range_ext(pmlan_private pmpriv,
			       HostCmd_DS_COMMAND *resp,
			       mlan_ioctl_req *pioctl_buf);

/**
 *  @brief RA based queueing
 *
 *  @param priv             A pointer to mlan_private structure
 *
 *  @return                 MTRUE or MFALSE
 */
static INLINE t_u8
queuing_ra_based(pmlan_private priv)
{
	/*
	 * Currently we assume if we are in Infra, then DA=RA. This might not be
	 * true in the future
	 */
	if ((priv->bss_mode == MLAN_BSS_MODE_INFRA) &&
	    (GET_BSS_ROLE(priv) == MLAN_BSS_ROLE_STA))
		return MFALSE;

	return MTRUE;
}

/**
 *  @brief Copy Rates
 *
 *  @param dest             A pointer to Dest Buf
 *  @param pos		        The position for copy
 *  @param src		        A pointer to Src Buf
 *  @param len              The len of Src Buf
 *
 *  @return                 Number of Rates copied
 */
static INLINE t_u32
wlan_copy_rates(t_u8 *dest, t_u32 pos, t_u8 *src, int len)
{
	int i;

	for (i = 0; i < len && src[i]; i++, pos++) {
		if (pos >= sizeof(WLAN_802_11_RATES))
			break;
		dest[pos] = src[i];
	}

	return pos;
}

/**
 *  @brief strlen
 *
 *  @param str		        A pointer to string
 *
 *  @return                 Length of string
 */
static INLINE t_u32
wlan_strlen(const char *str)
{
	t_u32 i;

	for (i = 0; str[i] != 0; i++) ;

	return i;
}

/**
 *  @brief iscdigit
 *
 *  @param chr                  A char
 *
 *  @return                     Non zero if chr is a hex, else 0
 */
static INLINE t_u32
wlan_isxdigit(t_u8 chr)
{
	return (chr <= 'f' && chr >= 'a') || (chr <= 'F' && chr >= 'A') ||
		(chr <= '9' && chr >= '0');
}

/**
 *  @brief isspace
 *
 *  @param                       A chr
 *
 *  @return                      Non zero if chr is space etc, else 0
 */
static INLINE t_u32
wlan_isspace(t_u8 chr)
{
	return chr <= ' ' && (chr == ' ' || (chr <= 13 && chr >= 9));
}

/** delay unit */
typedef enum _delay_unit {
	USEC,
	MSEC,
	SEC,
} t_delay_unit;

/** delay function */
t_void wlan_delay_func(mlan_adapter *pmadapter, t_u32 delay, t_delay_unit u);

/** delay function wrapper */
#define wlan_delay(p, n) wlan_delay_func(p, n, SEC)
/** delay function wrapper */
#define wlan_mdelay(p, n) wlan_delay_func(p, n, MSEC)
/** delay function wrapper */
#define wlan_udelay(p, n) wlan_delay_func(p, n, USEC)

/**
 *  @brief This function check if there are pending cmd
 *         in cmd pending Q
 *
 *  @param pmadapter  A pointer to mlan_adapter structure
 *
 *  @return  MTRUE -- cmd pending
 *           MFALSE -- no pending cmd
 */
static INLINE int
wlan_is_cmd_pending(mlan_adapter *pmadapter)
{
	int ret;
	cmd_ctrl_node *pcmd_node = MNULL;
	wlan_request_cmd_lock(pmadapter);
	pcmd_node = (cmd_ctrl_node *)util_peek_list(pmadapter->pmoal_handle,
						    &pmadapter->cmd_pending_q,
						    MNULL, MNULL);
	if (pcmd_node)
		ret = MTRUE;
	else
		ret = MFALSE;
	wlan_release_cmd_lock(pmadapter);
	return ret;
}

/** Get BSS number from priv */
#define GET_BSS_NUM(priv) ((priv)->bss_num)

/**
 *  @brief This function returns priv based on the BSS num and BSS type
 *
 *  @param pmadapter A pointer to mlan_adapter
 *  @param bss_num   BSS number
 *  @param bss_type  BSS type
 *
 *  @return          Pointer to mlan_private
 */
static INLINE mlan_private *
wlan_get_priv_by_id(mlan_adapter *pmadapter, t_u32 bss_num, t_u32 bss_type)
{
	int i;

	for (i = 0; i < MIN(pmadapter->priv_num, MLAN_MAX_BSS_NUM); i++) {
		if (pmadapter->priv[i]) {
			if ((pmadapter->priv[i]->bss_num == bss_num) &&
			    (pmadapter->priv[i]->bss_type == bss_type))
				return pmadapter->priv[i];
		}
	}
	return MNULL;
}

/**
 *  @brief This function returns first available priv
 *  based on the BSS role
 *
 *  @param pmadapter A pointer to mlan_adapter
 *  @param bss_role  BSS role or MLAN_BSS_ROLE_ANY
 *
 *  @return          Pointer to mlan_private
 */
static INLINE mlan_private *
wlan_get_priv(mlan_adapter *pmadapter, mlan_bss_role bss_role)
{
	int i;

	for (i = 0; i < MIN(pmadapter->priv_num, MLAN_MAX_BSS_NUM); i++) {
		if (pmadapter->priv[i]) {
			if (bss_role == MLAN_BSS_ROLE_ANY ||
			    GET_BSS_ROLE(pmadapter->priv[i]) == bss_role)
				return pmadapter->priv[i];
		}
	}
	return MNULL;
}

/**
 *  @brief This function counts the number of occurences for a certain
 *  condition among privs.  Which privs are checked can be configured
 *  via a second condition.
 *
 *  @param pmadapter  A pointer to mlan_adapter
 *  @param count_cond Function pointer to condition to count on privs
 *  @param check_cond Function pointer to condition to decide whether priv
 *                    should be counted or not.  Use MNULL to check all privs.
 *
 *  @return          Count of privs where count_cond returned MTRUE.
 */
static INLINE int
wlan_count_priv_cond(mlan_adapter *pmadapter,
		     t_bool (*count_cond) (pmlan_private pmpriv),
		     t_bool (*check_cond) (pmlan_private pmpriv))
{
	pmlan_private pmpriv;
	int count = 0;
	int i;

	if (pmadapter == MNULL || count_cond == MNULL)
		return 0;

	for (i = 0; i < pmadapter->priv_num; i++) {
		pmpriv = pmadapter->priv[i];
		if (pmpriv) {
			if ((check_cond == MNULL) ||
			    (check_cond && check_cond(pmpriv))) {
				if (count_cond(pmpriv))
					count++;
			}
		}
	}

	return count;
}

/**
 *  @brief This function runs a procedure on each priv.
 *  Which privs it is run on can be configured via a condition.
 *
 *  @param pmadapter  A pointer to mlan_adapter
 *  @param operation  Function pointer to produedure to operate on priv
 *  @param check_cond Function pointer to condition to decide whether priv
 *                    operated on or not.  Use MNULL to run on all privs.
 *
 *  @return           Number of privs that operation was run on.
 */
static INLINE int
wlan_do_task_on_privs(mlan_adapter *pmadapter,
		      t_void (*operation) (pmlan_private pmpriv),
		      t_bool (*check_cond) (pmlan_private pmpriv))
{
	pmlan_private pmpriv;
	int count = 0;
	int i;

	if (pmadapter == MNULL || operation == MNULL)
		return 0;

	for (i = 0; i < pmadapter->priv_num; i++) {
		pmpriv = pmadapter->priv[i];
		if (pmpriv) {
			if ((check_cond == MNULL) ||
			    (check_cond && check_cond(pmpriv))) {
				operation(pmpriv);
				count++;
			}
		}
	}

	return count;
}

/**
 *  @brief This function builds a list of privs that test for a condition
 *  This is useful if you need to do a number of operations on the same set
 *  of privs.  For one-off tasks, the above two functions might be better.
 *
 *  @param pmadapter  A pointer to mlan_adapter
 *  @param check_cond Function pointer to condition to decide whether priv
 *                    should be placed in the list.
 *  @param ppriv_list Output param.  Externally supplied array of mlan_private*
 *                    to hold priv's that test positive with check_cond.
 *                    Array size should be at least pmadapter->priv_num.
 *
 *  @return          Number of privs in ppriv_list
 *
 *  @sa              wlan_count_priv_cond
 */
static INLINE int
wlan_get_privs_by_cond(mlan_adapter *pmadapter,
		       t_bool (*check_cond) (pmlan_private pmpriv),
		       mlan_private **ppriv_list)
{
	pmlan_private pmpriv;
	int count = 0;
	int i;

	if (pmadapter == MNULL || check_cond == MNULL || ppriv_list == MNULL)
		return 0;

	for (i = 0; i < pmadapter->priv_num; i++) {
		pmpriv = pmadapter->priv[i];
		if (pmpriv) {
			if (check_cond(pmpriv))
				ppriv_list[count++] = pmpriv;
		}
	}

	return count;
}

/**
 *  @brief This function builds a list of privs that test against two conditions
 *  This is useful if you need to do a number of operations on the same set
 *  of privs.  Can choose whether both conditions (AND) or either condition (OR)
 *  is required.
 *
 *  @param pmadapter      A pointer to mlan_adapter
 *  @param check_cond     Function pointer to condition to decide whether priv
 *                        should be placed in the list.
 *  @param check_cond_2   Function pointer to second condition to check.
 *  @param and_conditions If MTRUE, both conditions must be met (AND),
 *                        else either condition can be met (OR).
 *  @param ppriv_list     Output param.  Externally supplied array of
 * mlan_private* to hold priv's that test positive with check_cond. Array size
 * should be at least pmadapter->priv_num.
 *
 *  @return          Number of privs in ppriv_list
 *
 *  @sa              wlan_count_priv_cond, wlan_get_privs_by_cond
 */
static INLINE int
wlan_get_privs_by_two_cond(mlan_adapter *pmadapter,
			   t_bool (*check_cond) (pmlan_private pmpriv),
			   t_bool (*check_cond_2) (pmlan_private pmpriv),
			   t_bool and_conditions, mlan_private **ppriv_list)
{
	pmlan_private pmpriv;
	int count = 0;
	int i;

	if (pmadapter == MNULL || check_cond == MNULL ||
	    check_cond_2 == MNULL || ppriv_list == MNULL)
		return 0;

	for (i = 0; i < pmadapter->priv_num; i++) {
		pmpriv = pmadapter->priv[i];
		if (pmpriv) {
			if (and_conditions) {
				if (check_cond(pmpriv) && check_cond_2(pmpriv))
					ppriv_list[count++] = pmpriv;
			} else {
				if (check_cond(pmpriv) || check_cond_2(pmpriv))
					ppriv_list[count++] = pmpriv;
			}
		}
	}

	return count;
}
#endif /* !_MLAN_MAIN_H_ */
