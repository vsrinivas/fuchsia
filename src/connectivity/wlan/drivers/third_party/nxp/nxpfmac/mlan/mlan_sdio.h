/** @file mlan_sdio.h
 *
 * @brief This file contains definitions for SDIO interface.
 * driver.
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
/****************************************************
Change log:
****************************************************/

#ifndef _MLAN_SDIO_H
#define _MLAN_SDIO_H

/** Block mode */
#ifndef BLOCK_MODE
#define BLOCK_MODE 1
#endif

/** Fixed address mode */
#ifndef FIXED_ADDRESS
#define FIXED_ADDRESS 0
#endif

/* Host Control Registers */
/** Host Control Registers : Host to Card Event */
#define HOST_TO_CARD_EVENT_REG 0x00
/** Host Control Registers : Host terminates Command 53 */
#define HOST_TERM_CMD53 (0x1U << 2)
/** Host Control Registers : Host without Command 53 finish host */
#define HOST_WO_CMD53_FINISH_HOST (0x1U << 2)
/** Host Control Registers : Host power up */
#define HOST_POWER_UP (0x1U << 1)
/** Host Control Registers : Host power down */
#define HOST_POWER_DOWN (0x1U << 0)

/** Host Control Registers : Upload host interrupt RSR */
#define UP_LD_HOST_INT_RSR (0x1U)
#define HOST_INT_RSR_MASK 0xFF

/** Host Control Registers : Upload command port host interrupt status */
#define UP_LD_CMD_PORT_HOST_INT_STATUS (0x40U)
/** Host Control Registers : Download command port host interrupt status */
#define DN_LD_CMD_PORT_HOST_INT_STATUS (0x80U)

/** Host Control Registers : Upload host interrupt mask */
#define UP_LD_HOST_INT_MASK (0x1U)
/** Host Control Registers : Download host interrupt mask */
#define DN_LD_HOST_INT_MASK (0x2U)
/** Host Control Registers : Cmd port upload interrupt mask */
#define CMD_PORT_UPLD_INT_MASK (0x1U << 6)
/** Host Control Registers : Cmd port download interrupt mask */
#define CMD_PORT_DNLD_INT_MASK (0x1U << 7)
/** Enable Host interrupt mask */
#define HIM_ENABLE                                                             \
	(UP_LD_HOST_INT_MASK | DN_LD_HOST_INT_MASK | CMD_PORT_UPLD_INT_MASK |  \
	 CMD_PORT_DNLD_INT_MASK)
/** Disable Host interrupt mask */
#define HIM_DISABLE 0xff

/** Host Control Registers : Upload host interrupt status */
#define UP_LD_HOST_INT_STATUS (0x1U)
/** Host Control Registers : Download host interrupt status */
#define DN_LD_HOST_INT_STATUS (0x2U)

/** Host Control Registers : Download CRC error */
#define DN_LD_CRC_ERR (0x1U << 2)
/** Host Control Registers : Upload restart */
#define UP_LD_RESTART (0x1U << 1)
/** Host Control Registers : Download restart */
#define DN_LD_RESTART (0x1U << 0)

/** Card Control Registers : Command port upload ready */
#define UP_LD_CP_RDY (0x1U << 6)
/** Card Control Registers : Command port download ready */
#define DN_LD_CP_RDY (0x1U << 7)
/** Card Control Registers : Card I/O ready */
#define CARD_IO_READY (0x1U << 3)
/** Card Control Registers : CIS card ready */
#define CIS_CARD_RDY (0x1U << 2)
/** Card Control Registers : Upload card ready */
#define UP_LD_CARD_RDY (0x1U << 1)
/** Card Control Registers : Download card ready */
#define DN_LD_CARD_RDY (0x1U << 0)

/** Card Control Registers : Host power interrupt mask */
#define HOST_POWER_INT_MASK (0x1U << 3)
/** Card Control Registers : Abort card interrupt mask */
#define ABORT_CARD_INT_MASK (0x1U << 2)
/** Card Control Registers : Upload card interrupt mask */
#define UP_LD_CARD_INT_MASK (0x1U << 1)
/** Card Control Registers : Download card interrupt mask */
#define DN_LD_CARD_INT_MASK (0x1U << 0)

/** Card Control Registers : Power up interrupt */
#define POWER_UP_INT (0x1U << 4)
/** Card Control Registers : Power down interrupt */
#define POWER_DOWN_INT (0x1U << 3)

/** Card Control Registers : Power up RSR */
#define POWER_UP_RSR (0x1U << 4)
/** Card Control Registers : Power down RSR */
#define POWER_DOWN_RSR (0x1U << 3)

/** Card Control Registers : SD test BUS 0 */
#define SD_TESTBUS0 (0x1U)
/** Card Control Registers : SD test BUS 1 */
#define SD_TESTBUS1 (0x1U)
/** Card Control Registers : SD test BUS 2 */
#define SD_TESTBUS2 (0x1U)
/** Card Control Registers : SD test BUS 3 */
#define SD_TESTBUS3 (0x1U)

/** Port for registers */
#define REG_PORT 0
/** Port for memory */
#define MEM_PORT 0x10000
/** Ctrl port */
#define CTRL_PORT			0
/** Ctrl port mask */
#define CTRL_PORT_MASK			0x0001
/** Card Control Registers : cmd53 new mode */
#define CMD53_NEW_MODE (0x1U << 0)
/** Card Control Registers : cmd53 tx len format 1 (0x10) */
#define CMD53_TX_LEN_FORMAT_1 (0x1U << 4)
/** Card Control Registers : cmd53 tx len format 2 (0x20)*/
#define CMD53_TX_LEN_FORMAT_2 (0x1U << 5)
/** Card Control Registers : cmd53 rx len format 1 (0x40) */
#define CMD53_RX_LEN_FORMAT_1 (0x1U << 6)
/** Card Control Registers : cmd53 rx len format 2 (0x80)*/
#define CMD53_RX_LEN_FORMAT_2 (0x1U << 7)

#define CMD_PORT_RD_LEN_EN (0x1U << 2)
/* Card Control Registers : cmd port auto enable */
#define CMD_PORT_AUTO_EN (0x1U << 0)

/* Command port */
#define CMD_PORT_SLCT 0x8000

/** Misc. Config Register : Auto Re-enable interrupts */
#define AUTO_RE_ENABLE_INT MBIT(4)

/** Enable GPIO-1 as a duplicated signal of interrupt as appear of SDIO_DAT1*/
#define ENABLE_GPIO_1_INT_MODE 0x88
/** Scratch reg 3 2  :     Configure GPIO-1 INT*/
#define SCRATCH_REG_32 0xEE

/** Event header Len*/
#define MLAN_EVENT_HEADER_LEN 8

/** SDIO byte mode size */
#define MAX_BYTE_MODE_SIZE 512

/** The base address for packet with multiple ports aggregation */
#define SDIO_MPA_ADDR_BASE 0x1000

/** SDIO Tx aggregation in progress ? */
#define MP_TX_AGGR_IN_PROGRESS(a) (a->pcard_sd->mpa_tx.pkt_cnt > 0)

/** SDIO Tx aggregation buffer room for next packet ? */
#define MP_TX_AGGR_BUF_HAS_ROOM(a, mbuf, len)                                  \
	(((a->pcard_sd->mpa_tx.buf_len) + len) <=                              \
	 (a->pcard_sd->mpa_tx.buf_size))

/** Copy current packet (SDIO Tx aggregation buffer) to SDIO buffer */
#define MP_TX_AGGR_BUF_PUT(a, mbuf, port)                                      \
	do {                                                                   \
		pmadapter->callbacks.moal_memmove(                             \
			a->pmoal_handle,                                       \
			&a->pcard_sd->mpa_tx.buf[a->pcard_sd->mpa_tx.buf_len], \
			mbuf->pbuf + mbuf->data_offset, mbuf->data_len);       \
		a->pcard_sd->mpa_tx.buf_len += mbuf->data_len;                 \
		a->pcard_sd->mpa_tx.mp_wr_info[a->pcard_sd->mpa_tx.pkt_cnt] =  \
			*(t_u16 *)(mbuf->pbuf + mbuf->data_offset);            \
		if (!a->pcard_sd->mpa_tx.pkt_cnt) {                            \
			a->pcard_sd->mpa_tx.start_port = port;                 \
		}                                                              \
		a->pcard_sd->mpa_tx.ports |= (1 << port);                      \
		a->pcard_sd->mpa_tx.pkt_cnt++;                                 \
	} while (0)

#define MP_TX_AGGR_BUF_PUT_NONEWMODE(a, mbuf, port) do {                  \
	pmadapter->callbacks.moal_memmove(a->pmoal_handle, \
		&a->pcard_sd->mpa_tx.buf[a->pcard_sd->mpa_tx.buf_len], \
		mbuf->pbuf+mbuf->data_offset, mbuf->data_len);\
	a->pcard_sd->mpa_tx.buf_len += mbuf->data_len;                        \
	a->pcard_sd->mpa_tx.mp_wr_info[a->pcard_sd->mpa_tx.pkt_cnt] = *(t_u16 *)(mbuf->pbuf+mbuf->data_offset); \
	if (!a->pcard_sd->mpa_tx.pkt_cnt) {                                   \
	    a->pcard_sd->mpa_tx.start_port = port;                            \
	}                                                           \
	if (a->pcard_sd->mpa_tx.start_port <= port) {                         \
	    a->pcard_sd->mpa_tx.ports |= (1 << (a->pcard_sd->mpa_tx.pkt_cnt));			\
	} else {                                                    \
	      a->pcard_sd->mpa_tx.ports |= (1 << (a->pcard_sd->mpa_tx.pkt_cnt \
			+ 1 + (a->pcard_sd->max_ports - a->pcard_sd->mp_end_port)));  \
	}                                                           \
	a->pcard_sd->mpa_tx.pkt_cnt++;                                       \
} while (0)
#define MP_TX_AGGR_BUF_PUT_SG(a, mbuf, port)                                   \
	do {                                                                   \
		a->pcard_sd->mpa_tx.buf_len += mbuf->data_len;                 \
		a->pcard_sd->mpa_tx.mp_wr_info[a->pcard_sd->mpa_tx.pkt_cnt] =  \
			*(t_u16 *)(mbuf->pbuf + mbuf->data_offset);            \
		a->pcard_sd->mpa_tx.mbuf_arr[a->pcard_sd->mpa_tx.pkt_cnt] =    \
			mbuf;                                                  \
		if (!a->pcard_sd->mpa_tx.pkt_cnt) {                            \
			a->pcard_sd->mpa_tx.start_port = port;                 \
		}                                                              \
		a->pcard_sd->mpa_tx.ports |= (1 << port);                      \
		a->pcard_sd->mpa_tx.pkt_cnt++;                                 \
	} while (0)
#define MP_TX_AGGR_BUF_PUT_SG_NONEWMODE(a, mbuf, port) do {                  \
	a->pcard_sd->mpa_tx.buf_len += mbuf->data_len;                        \
    a->pcard_sd->mpa_tx.mp_wr_info[a->pcard_sd->mpa_tx.pkt_cnt] = *(t_u16 *)(mbuf->pbuf+mbuf->data_offset); \
    a->pcard_sd->mpa_tx.mbuf_arr[a->pcard_sd->mpa_tx.pkt_cnt] = mbuf;               \
	if (!a->pcard_sd->mpa_tx.pkt_cnt) {                                   \
	    a->pcard_sd->mpa_tx.start_port = port;                            \
	}                                                           \
	if (a->pcard_sd->mpa_tx.start_port <= port) {                         \
	    a->pcard_sd->mpa_tx.ports |= (1 << (a->pcard_sd->mpa_tx.pkt_cnt));			\
	} else {                                                    \
	      a->pcard_sd->mpa_tx.ports |= (1 << (a->pcard_sd->mpa_tx.pkt_cnt \
			+ 1 + (a->pcard_sd->max_ports - a->pcard_sd->mp_end_port)));  \
	}                                                           \
	a->pcard_sd->mpa_tx.pkt_cnt++;                                       \
} while (0)

/** SDIO Tx aggregation limit ? */
#define MP_TX_AGGR_PKT_LIMIT_REACHED(a)                                        \
	((a->pcard_sd->mpa_tx.pkt_cnt) == (a->pcard_sd->mpa_tx.pkt_aggr_limit))

#define MP_TX_AGGR_PORT_LIMIT_REACHED(a) ((a->pcard_sd->curr_wr_port < \
                a->pcard_sd->mpa_tx.start_port) && (((a->pcard_sd->max_ports - \
                a->pcard_sd->mpa_tx.start_port) + a->pcard_sd->curr_wr_port) >= \
                    a->pcard_sd->mp_aggr_pkt_limit))

/** Reset SDIO Tx aggregation buffer parameters */
#define MP_TX_AGGR_BUF_RESET(a)                                                \
	do {                                                                   \
		memset(a, a->pcard_sd->mpa_tx.mp_wr_info, 0,                   \
		       sizeof(a->pcard_sd->mpa_tx.mp_wr_info));                \
		a->pcard_sd->mpa_tx.pkt_cnt = 0;                               \
		a->pcard_sd->mpa_tx.buf_len = 0;                               \
		a->pcard_sd->mpa_tx.ports = 0;                                 \
		a->pcard_sd->mpa_tx.start_port = 0;                            \
	} while (0)

/** SDIO Rx aggregation limit ? */
#define MP_RX_AGGR_PKT_LIMIT_REACHED(a)                                        \
	(a->pcard_sd->mpa_rx.pkt_cnt == a->pcard_sd->mpa_rx.pkt_aggr_limit)

/** SDIO Rx aggregation port limit ? */
/** this is for test only, because port 0 is reserved for control port */
/* #define MP_RX_AGGR_PORT_LIMIT_REACHED(a) (a->curr_rd_port == 1) */

/* receive packets aggregated up to a half of mp_end_port */
/* note: hw rx wraps round only after port (MAX_PORT-1) */
#define MP_RX_AGGR_PORT_LIMIT_REACHED(a)                                       \
	(((a->pcard_sd->curr_rd_port < a->pcard_sd->mpa_rx.start_port) &&      \
	  (((a->pcard_sd->max_ports - a->pcard_sd->mpa_rx.start_port) +                      \
	    a->pcard_sd->curr_rd_port) >= (a->pcard_sd->mp_end_port >> 1))) || \
	 ((a->pcard_sd->curr_rd_port - a->pcard_sd->mpa_rx.start_port) >=      \
	  (a->pcard_sd->mp_end_port >> 1)))

#define MP_RX_AGGR_PORT_LIMIT_REACHED_NONEWMODE(a) ((a->pcard_sd->curr_rd_port < \
                a->pcard_sd->mpa_rx.start_port) && (((a->pcard_sd->max_ports - \
                a->pcard_sd->mpa_rx.start_port) + a->pcard_sd->curr_rd_port) >= \
                a->pcard_sd->mp_aggr_pkt_limit))

/** SDIO Rx aggregation in progress ? */
#define MP_RX_AGGR_IN_PROGRESS(a) (a->pcard_sd->mpa_rx.pkt_cnt > 0)

/** SDIO Rx aggregation buffer room for next packet ? */
#define MP_RX_AGGR_BUF_HAS_ROOM(a, rx_len)                                     \
	((a->pcard_sd->mpa_rx.buf_len + rx_len) <= a->pcard_sd->mpa_rx.buf_size)

/** Prepare to copy current packet from card to SDIO Rx aggregation buffer */
#define MP_RX_AGGR_SETUP(a, mbuf, port, rx_len)                                \
	do {                                                                   \
		a->pcard_sd->mpa_rx.buf_len += rx_len;                         \
		if (!a->pcard_sd->mpa_rx.pkt_cnt) {                            \
			a->pcard_sd->mpa_rx.start_port = port;                 \
		}                                                              \
		a->pcard_sd->mpa_rx.ports |= (1 << port);                      \
		a->pcard_sd->mpa_rx.mbuf_arr[a->pcard_sd->mpa_rx.pkt_cnt] =    \
			mbuf;                                                  \
		a->pcard_sd->mpa_rx.len_arr[a->pcard_sd->mpa_rx.pkt_cnt] =     \
			rx_len;                                                \
		a->pcard_sd->mpa_rx.pkt_cnt++;                                 \
	} while (0)

#define MP_RX_AGGR_SETUP_NONEWMODE(a, mbuf, port, rx_len) do {   \
	a->pcard_sd->mpa_rx.buf_len += rx_len;                       \
	if (!a->pcard_sd->mpa_rx.pkt_cnt) {                          \
	    a->pcard_sd->mpa_rx.start_port = port;                   \
	}                                                  \
	if (a->pcard_sd->mpa_rx.start_port <= port) {                  \
	    a->pcard_sd->mpa_rx.ports |= (1 << (a->pcard_sd->mpa_rx.pkt_cnt)); \
	} else {                                           \
	    a->pcard_sd->mpa_rx.ports |= (1 << (a->pcard_sd->mpa_rx.pkt_cnt + 1)); \
	}                                                  \
	a->pcard_sd->mpa_rx.mbuf_arr[a->pcard_sd->mpa_rx.pkt_cnt] = mbuf;      \
	a->pcard_sd->mpa_rx.len_arr[a->pcard_sd->mpa_rx.pkt_cnt] = rx_len;     \
	a->pcard_sd->mpa_rx.pkt_cnt++;                               \
} while (0);

/** Reset SDIO Rx aggregation buffer parameters */
#define MP_RX_AGGR_BUF_RESET(a)                                                \
	do {                                                                   \
		a->pcard_sd->mpa_rx.pkt_cnt = 0;                               \
		a->pcard_sd->mpa_rx.buf_len = 0;                               \
		a->pcard_sd->mpa_rx.ports = 0;                                 \
		a->pcard_sd->mpa_rx.start_port = 0;                            \
	} while (0)

/** aggr buf size 32k  */
#define SDIO_MP_AGGR_BUF_SIZE_32K (32768)
/** max aggr buf size 64k-256 */
#define SDIO_MP_AGGR_BUF_SIZE_MAX (65280)

extern mlan_adapter_operations mlan_sdio_ops;

/** Probe and initialization function */
mlan_status wlan_sdio_probe(pmlan_adapter pmadapter);
mlan_status wlan_get_sdio_device(pmlan_adapter pmadapter);

mlan_status wlan_send_mp_aggr_buf(mlan_adapter *pmadapter);

mlan_status wlan_re_alloc_sdio_rx_mpa_buffer(mlan_adapter *pmadapter);

void wlan_decode_spa_buffer(mlan_adapter *pmadapter, t_u8 *buf, t_u32 len);
t_void wlan_sdio_deaggr_rx_pkt(pmlan_adapter pmadapter, mlan_buffer *pmbuf);
/** Transfer data to card */
mlan_status wlan_sdio_host_to_card(mlan_adapter *pmadapter, t_u8 type,
				   mlan_buffer *mbuf, mlan_tx_param *tx_param);
mlan_status wlan_set_sdio_gpio_int(pmlan_private priv);
mlan_status wlan_cmd_sdio_gpio_int(pmlan_private pmpriv,
				   HostCmd_DS_COMMAND *cmd,
				   t_u16 cmd_action, t_void *pdata_buf);
mlan_status wlan_reset_fw(pmlan_adapter pmadapter);

#endif /* _MLAN_SDIO_H */
