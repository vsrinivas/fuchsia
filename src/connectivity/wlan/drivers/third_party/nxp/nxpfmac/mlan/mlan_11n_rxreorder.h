/** @file mlan_11n_rxreorder.h
 *
 *  @brief This file contains related macros, enum, and struct
 *  of 11n RxReordering functionalities
 *
 *
 *  Copyright 2008-2020 NXP
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

/********************************************************
Change log:
    11/10/2008: initial version
********************************************************/

#ifndef _MLAN_11N_RXREORDER_H_
#define _MLAN_11N_RXREORDER_H_

/** Max value a TID can take = 2^12 = 4096 */
#define MAX_TID_VALUE (2 << 11)
/** 2^11 = 2048 */
#define TWOPOW11 (2 << 10)

/** Tid Mask used for extracting TID from BlockAckParamSet */
#define BLOCKACKPARAM_TID_MASK 0x3C
/** Tid position in BlockAckParamSet */
#define BLOCKACKPARAM_TID_POS 2
/** WinSize Mask used for extracting WinSize from BlockAckParamSet */
#define BLOCKACKPARAM_WINSIZE_MASK 0xffc0
/** WinSize Mask used for extracting WinSize from BlockAckParamSet */
#define BLOCKACKPARAM_AMSDU_SUPP_MASK 0x1
/** WinSize position in BlockAckParamSet */
#define BLOCKACKPARAM_WINSIZE_POS 6
/** Position of TID in DelBA Param set */
#define DELBA_TID_POS 12
/** Position of INITIATOR in DelBA Param set */
#define DELBA_INITIATOR_POS 11
/** Reason code: Requested from peer STA as it does not want to
 * use the mechanism */
#define REASON_CODE_STA_DONT_WANT 37
/** Reason code: Requested from peer STA due to timeout*/
#define REASON_CODE_STA_TIMEOUT 39
/** Type: send delba command */
#define TYPE_DELBA_SENT 1
/** Type: recieve delba command */
#define TYPE_DELBA_RECEIVE 2
/** Set Initiator Bit */
#define DELBA_INITIATOR(paramset) (paramset = (paramset | (1 << 11)))
/** Reset Initiator Bit for recipient */
#define DELBA_RECIPIENT(paramset) (paramset = (paramset & ~(1 << 11)))
/** Immediate block ack */
#define IMMEDIATE_BLOCK_ACK 0x2

/** The request has been declined */
#define ADDBA_RSP_STATUS_DECLINED 37
/** ADDBA response status : Reject */
#define ADDBA_RSP_STATUS_REJECT 1
/** ADDBA response status : Accept */
#define ADDBA_RSP_STATUS_ACCEPT 0

/** DEFAULT SEQ NUM */
#define DEFAULT_SEQ_NUM 0xffff

/** Indicate packet has been dropped in FW */
#define RX_PKT_DROPPED_IN_FW 0xffffffff

mlan_status mlan_11n_rxreorder_pkt(void *priv, t_u16 seqNum, t_u16 tid,
				   t_u8 *ta, t_u8 pkttype, void *payload);
void mlan_11n_delete_bastream_tbl(mlan_private *priv, int tid,
				  t_u8 *PeerMACAddr, t_u8 type, int initiator,
				  t_u16 reason_code);
void wlan_11n_ba_stream_timeout(mlan_private *priv,
				HostCmd_DS_11N_BATIMEOUT *event);
mlan_status wlan_ret_11n_addba_resp(mlan_private *priv,
				    HostCmd_DS_COMMAND *resp);
mlan_status wlan_cmd_11n_delba(mlan_private *priv, HostCmd_DS_COMMAND *cmd,
			       void *pdata_buf);
mlan_status wlan_cmd_11n_addba_rspgen(mlan_private *priv,
				      HostCmd_DS_COMMAND *cmd, void *pdata_buf);
mlan_status wlan_cmd_11n_addba_req(mlan_private *priv, HostCmd_DS_COMMAND *cmd,
				   void *pdata_buf);
void wlan_11n_cleanup_reorder_tbl(mlan_private *priv);
RxReorderTbl *wlan_11n_get_rxreorder_tbl(mlan_private *priv, int tid, t_u8 *ta);
void wlan_11n_rxba_sync_event(mlan_private *priv, t_u8 *event_buf, t_u16 len);
void wlan_update_rxreorder_tbl(pmlan_adapter pmadapter, t_u8 flag);
void wlan_flush_rxreorder_tbl(pmlan_adapter pmadapter);
void wlan_coex_ampdu_rxwinsize(pmlan_adapter pmadapter);

/** clean up reorder_tbl */
void wlan_cleanup_reorder_tbl(mlan_private *priv, t_u8 *ta);
#endif /* _MLAN_11N_RXREORDER_H_ */
