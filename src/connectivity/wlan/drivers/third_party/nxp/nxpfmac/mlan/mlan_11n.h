/** @file mlan_11n.h
 *
 *  @brief Interface for the 802.11n mlan_11n module implemented in mlan_11n.c
 *
 *  Driver interface functions and type declarations for the 11n module
 *    implemented in mlan_11n.c.
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
    12/01/2008: initial version
********************************************************/

#ifndef _MLAN_11N_H_
#define _MLAN_11N_H_

#include "mlan_11n_aggr.h"
#include "mlan_11n_rxreorder.h"
#include "mlan_wmm.h"

/** Print the 802.11n device capability */
void wlan_show_dot11ndevcap(pmlan_adapter pmadapter, t_u32 cap);
/** Print the 802.11n device MCS */
void wlan_show_devmcssupport(pmlan_adapter pmadapter, t_u8 support);
/** Handle the command response of a delete block ack request */
mlan_status wlan_ret_11n_delba(mlan_private *priv, HostCmd_DS_COMMAND *resp);
/** Handle the command response of an add block ack request */
mlan_status wlan_ret_11n_addba_req(mlan_private *priv,
				   HostCmd_DS_COMMAND *resp);
/** Handle the command response of 11ncfg command */
mlan_status wlan_ret_11n_cfg(pmlan_private pmpriv,
			     HostCmd_DS_COMMAND *resp,
			     mlan_ioctl_req *pioctl_buf);
/** Prepare 11ncfg command */
mlan_status wlan_cmd_11n_cfg(pmlan_private pmpriv,
			     HostCmd_DS_COMMAND *cmd, t_u16 cmd_action,
			     t_void *pdata_buf);
/** Prepare reject addba requst command */
mlan_status wlan_cmd_reject_addba_req(pmlan_private pmpriv,
				      HostCmd_DS_COMMAND *cmd,
				      t_u16 cmd_action, t_void *pdata_buf);
/** Handle the command response of rejecting addba request */
mlan_status wlan_ret_reject_addba_req(pmlan_private pmpriv,
				      HostCmd_DS_COMMAND *resp,
				      mlan_ioctl_req *pioctl_buf);
/** Prepare TX BF configuration command */
mlan_status wlan_cmd_tx_bf_cfg(pmlan_private pmpriv,
			       HostCmd_DS_COMMAND *cmd, t_u16 cmd_action,
			       t_void *pdata_buf);
/** Handle the command response TX BF configuration */
mlan_status wlan_ret_tx_bf_cfg(pmlan_private pmpriv,
			       HostCmd_DS_COMMAND *resp,
			       mlan_ioctl_req *pioctl_buf);
#ifdef STA_SUPPORT
t_u8 wlan_11n_bandconfig_allowed(mlan_private *pmpriv, t_u8 bss_band);
/** Append the 802_11N tlv */
int wlan_cmd_append_11n_tlv(mlan_private *pmpriv,
			    BSSDescriptor_t *pbss_desc, t_u8 **ppbuffer);
/** wlan fill HT cap tlv */
void wlan_fill_ht_cap_tlv(mlan_private *priv, MrvlIETypes_HTCap_t *pht_cap,
			  t_u16 band, t_u8 fill);
/** wlan fill HT cap IE */
void wlan_fill_ht_cap_ie(mlan_private *priv, IEEEtypes_HTCap_t *pht_cap,
			 t_u16 bands);
#endif /* STA_SUPPORT */
/** Miscellaneous configuration handler */
mlan_status wlan_11n_cfg_ioctl(pmlan_adapter pmadapter,
			       pmlan_ioctl_req pioctl_req);
/** Delete Tx BA stream table entry */
void wlan_11n_delete_txbastream_tbl_entry(mlan_private *priv,
					  TxBAStreamTbl *ptx_tbl);
/** Delete all Tx BA stream table entries */
void wlan_11n_deleteall_txbastream_tbl(mlan_private *priv);
/** Get Tx BA stream table */
TxBAStreamTbl *wlan_11n_get_txbastream_tbl(mlan_private *priv, int tid,
					   t_u8 *ra, int lock);
/** Create Tx BA stream table */
void wlan_11n_create_txbastream_tbl(mlan_private *priv, t_u8 *ra, int tid,
				    baStatus_e ba_status);
/** Send ADD BA request */
int wlan_send_addba(mlan_private *priv, int tid, t_u8 *peer_mac);
/** Send DEL BA request */
int wlan_send_delba(mlan_private *priv, pmlan_ioctl_req pioctl_req, int tid,
		    t_u8 *peer_mac, int initiator);
/** This function handles the command response of delete a block ack request*/
void wlan_11n_delete_bastream(mlan_private *priv, t_u8 *del_ba);
/** get rx reorder table */
int wlan_get_rxreorder_tbl(mlan_private *priv, rx_reorder_tbl *buf);
/** get tx ba stream table */
int wlan_get_txbastream_tbl(mlan_private *priv, tx_ba_stream_tbl *buf);
/** send delba */
void wlan_11n_delba(mlan_private *priv, int tid);
/** update amdpdu tx win size */
void wlan_update_ampdu_txwinsize(pmlan_adapter pmadapter);
/** Minimum number of AMSDU */
#define MIN_NUM_AMSDU 2
/** AMSDU Aggr control cmd resp */
mlan_status wlan_ret_amsdu_aggr_ctrl(pmlan_private pmpriv,
				     HostCmd_DS_COMMAND *resp,
				     mlan_ioctl_req *pioctl_buf);
void wlan_set_tx_pause_flag(mlan_private *priv, t_u8 flag);
/** reconfigure tx buf size */
mlan_status wlan_cmd_recfg_tx_buf(mlan_private *priv, HostCmd_DS_COMMAND *cmd,
				  int cmd_action, void *pdata_buf);
/** AMSDU aggr control cmd */
mlan_status wlan_cmd_amsdu_aggr_ctrl(mlan_private *priv,
				     HostCmd_DS_COMMAND *cmd, int cmd_action,
				     void *pdata_buf);

t_u8 wlan_validate_chan_offset(mlan_private *pmpriv, t_u16 band,
			       t_u32 chan, t_u8 chan_bw);
/** get channel offset */
t_u8 wlan_get_second_channel_offset(mlan_private *priv, int chan);

void wlan_update_11n_cap(mlan_private *pmpriv);

/** clean up txbastream_tbl */
void wlan_11n_cleanup_txbastream_tbl(mlan_private *priv, t_u8 *ra);
/**
 *  @brief This function checks whether a station has 11N enabled or not
 *
 *  @param priv     A pointer to mlan_private
 *  @param mac      station mac address
 *  @return         MTRUE or MFALSE
 */
static INLINE t_u8
is_station_11n_enabled(mlan_private *priv, t_u8 *mac)
{
	sta_node *sta_ptr = MNULL;
	sta_ptr = wlan_get_station_entry(priv, mac);
	if (sta_ptr)
		return (sta_ptr->is_11n_enabled) ? MTRUE : MFALSE;
	return MFALSE;
}

/**
 *  @brief This function get station max amsdu size
 *
 *  @param priv     A pointer to mlan_private
 *  @param mac      station mac address
 *  @return         max amsdu size statio supported
 */
static INLINE t_u16
get_station_max_amsdu_size(mlan_private *priv, t_u8 *mac)
{
	sta_node *sta_ptr = MNULL;
	sta_ptr = wlan_get_station_entry(priv, mac);
	if (sta_ptr)
		return sta_ptr->max_amsdu;
	return 0;
}

/**
 *  @brief This function checks whether a station allows AMPDU or not
 *
 *  @param priv     A pointer to mlan_private
 *  @param ptr      A pointer to RA list table
 *  @param tid      TID value for ptr
 *  @return         MTRUE or MFALSE
 */
static INLINE t_u8
is_station_ampdu_allowed(mlan_private *priv, raListTbl *ptr, int tid)
{
	sta_node *sta_ptr = MNULL;
	sta_ptr = wlan_get_station_entry(priv, ptr->ra);
	if (sta_ptr) {
		if (GET_BSS_ROLE(priv) == MLAN_BSS_ROLE_UAP) {
			if (priv->sec_info.wapi_enabled &&
			    !sta_ptr->wapi_key_on)
				return MFALSE;
		}
		return (sta_ptr->ampdu_sta[tid] != BA_STREAM_NOT_ALLOWED) ?
			MTRUE : MFALSE;
	}
	return MFALSE;
}

/**
 *  @brief This function disable station ampdu for specific tid
 *
 *  @param priv     A pointer to mlan_private
 *  @param tid     tid index
 *  @param ra      station mac address
 *  @return        N/A
 */
static INLINE void
disable_station_ampdu(mlan_private *priv, t_u8 tid, t_u8 *ra)
{
	sta_node *sta_ptr = MNULL;
	sta_ptr = wlan_get_station_entry(priv, ra);
	if (sta_ptr)
		sta_ptr->ampdu_sta[tid] = BA_STREAM_NOT_ALLOWED;
	return;
}

/**
 *  @brief This function reset station ampdu for specific id to user setting.
 *
 *  @param priv     A pointer to mlan_private
 *  @param tid     tid index
 *  @param ra      station mac address
 *  @return        N/A
 */
static INLINE void
reset_station_ampdu(mlan_private *priv, t_u8 tid, t_u8 *ra)
{
	sta_node *sta_ptr = MNULL;
	sta_ptr = wlan_get_station_entry(priv, ra);
	if (sta_ptr)
		sta_ptr->ampdu_sta[tid] = priv->aggr_prio_tbl[tid].ampdu_user;
	return;
}

#define IS_BG_RATE (priv->bitmap_rates[0] || priv->bitmap_rates[1])
/**
 *  @brief This function checks whether AMPDU is allowed or not
 *
 *  @param priv     A pointer to mlan_private
 *  @param ptr      A pointer to RA list table
 *  @param tid      TID value for ptr
 *
 *  @return         MTRUE or MFALSE
 */
static INLINE t_u8
wlan_is_ampdu_allowed(mlan_private *priv, raListTbl *ptr, int tid)
{
	if (ptr->is_tdls_link)
		return is_station_ampdu_allowed(priv, ptr, tid);
	if (priv->adapter->tdls_status != TDLS_NOT_SETUP && !priv->txaggrctrl)
		return MFALSE;

	if ((!priv->is_data_rate_auto) && IS_BG_RATE)
		return MFALSE;
#ifdef UAP_SUPPORT
	if (GET_BSS_ROLE(priv) == MLAN_BSS_ROLE_UAP)
		return is_station_ampdu_allowed(priv, ptr, tid);
#endif /* UAP_SUPPORT */
	if (priv->sec_info.wapi_enabled && !priv->sec_info.wapi_key_on)
		return MFALSE;
	return (priv->aggr_prio_tbl[tid].ampdu_ap != BA_STREAM_NOT_ALLOWED)?
		MTRUE : MFALSE;
}

#define BA_RSSI_HIGH_THRESHOLD -70

static INLINE void
wlan_update_station_del_ba_count(mlan_private *priv, raListTbl *ptr)
{
	sta_node *sta_ptr = MNULL;
	t_s8 rssi;
	sta_ptr = wlan_get_station_entry(priv, ptr->ra);
	if (sta_ptr) {
		rssi = sta_ptr->snr - sta_ptr->nf;
		if (rssi > BA_RSSI_HIGH_THRESHOLD)
			ptr->del_ba_count = 0;
	}
	return;
}

static INLINE void
wlan_update_del_ba_count(mlan_private *priv, raListTbl *ptr)
{
	t_s8 rssi;
#ifdef UAP_802_11N
#ifdef UAP_SUPPORT
	if (GET_BSS_ROLE(priv) == MLAN_BSS_ROLE_UAP)
		return wlan_update_station_del_ba_count(priv, ptr);
#endif /* UAP_SUPPORT */
#endif /* UAP_802_11N */
	if (ptr->is_tdls_link)
		return wlan_update_station_del_ba_count(priv, ptr);
	rssi = priv->snr - priv->nf;
	if (rssi > BA_RSSI_HIGH_THRESHOLD)
		ptr->del_ba_count = 0;
}

/**
 *  @brief This function checks whether AMSDU is allowed or not
 *
 *  @param priv     A pointer to mlan_private
 *  @param ptr      A pointer to RA list table
 *  @param tid      TID value for ptr
 *
 *  @return         MTRUE or MFALSE
 */
static INLINE t_u8
wlan_is_amsdu_allowed(mlan_private *priv, raListTbl *ptr, int tid)
{
#ifdef UAP_SUPPORT
	sta_node *sta_ptr = MNULL;
#endif
	if (priv->amsdu_disable)
		return MFALSE;
#ifdef UAP_SUPPORT
	if (GET_BSS_ROLE(priv) == MLAN_BSS_ROLE_UAP) {
		sta_ptr = wlan_get_station_entry(priv, ptr->ra);
		if (sta_ptr) {
			if (priv->sec_info.wapi_enabled &&
			    !sta_ptr->wapi_key_on)
				return MFALSE;
		}
	}
#endif /* UAP_SUPPORT */
	if (ptr->is_tdls_link)
		return (priv->aggr_prio_tbl[tid].amsdu !=
			BA_STREAM_NOT_ALLOWED)? MTRUE : MFALSE;
#define TXRATE_BITMAP_INDEX_MCS0_7 2
	return ((priv->aggr_prio_tbl[tid].amsdu != BA_STREAM_NOT_ALLOWED)&&
		((priv->is_data_rate_auto) ||
		 !(((priv->bitmap_rates[TXRATE_BITMAP_INDEX_MCS0_7]) & 0x03) ||
		   IS_BG_RATE))) ? MTRUE : MFALSE;
}

/**
 *  @brief This function checks whether a BA stream is available or not
 *
 *  @param priv     A pointer to mlan_private
 *
 *  @return         MTRUE or MFALSE
 */
static INLINE t_u8
wlan_is_bastream_avail(mlan_private *priv)
{
	mlan_private *pmpriv = MNULL;
	t_u8 i = 0;
	t_u32 bastream_num = 0;
	t_u32 bastream_max = 0;
	for (i = 0; i < priv->adapter->priv_num; i++) {
		pmpriv = priv->adapter->priv[i];
		if (pmpriv)
			bastream_num += wlan_wmm_list_len((pmlan_list_head)
							  &pmpriv->
							  tx_ba_stream_tbl_ptr);
	}
	bastream_max = ISSUPP_GETTXBASTREAM(priv->adapter->hw_dot_11n_dev_cap);
	if (bastream_max == 0)
		bastream_max = MLAN_MAX_TX_BASTREAM_DEFAULT;
	return (bastream_num < bastream_max) ? MTRUE : MFALSE;
}

/**
 *  @brief This function finds the stream to delete
 *
 *  @param priv     A pointer to mlan_private
 *  @param ptr      A pointer to RA list table
 *  @param ptr_tid  TID value of ptr
 *  @param ptid     A pointer to TID of stream to delete, if return MTRUE
 *  @param ra       RA of stream to delete, if return MTRUE
 *
 *  @return         MTRUE or MFALSE
 */
static INLINE t_u8
wlan_find_stream_to_delete(mlan_private *priv,
			   raListTbl *ptr, int ptr_tid, int *ptid, t_u8 *ra)
{
	int tid;
	t_u8 ret = MFALSE;
	TxBAStreamTbl *ptx_tbl;

	ENTER();

	ptx_tbl = (TxBAStreamTbl *)util_peek_list(priv->adapter->pmoal_handle,
						  &priv->tx_ba_stream_tbl_ptr,
						  MNULL, MNULL);
	if (!ptx_tbl) {
		LEAVE();
		return ret;
	}

	tid = priv->aggr_prio_tbl[ptr_tid].ampdu_user;

	while (ptx_tbl != (TxBAStreamTbl *)&priv->tx_ba_stream_tbl_ptr) {
		if (tid > priv->aggr_prio_tbl[ptx_tbl->tid].ampdu_user) {
			tid = priv->aggr_prio_tbl[ptx_tbl->tid].ampdu_user;
			*ptid = ptx_tbl->tid;
			memcpy_ext(priv->adapter, ra, ptx_tbl->ra,
				   MLAN_MAC_ADDR_LENGTH, MLAN_MAC_ADDR_LENGTH);
			ret = MTRUE;
		}

		ptx_tbl = ptx_tbl->pnext;
	}
	LEAVE();
	return ret;
}

/**
 *  @brief This function checks whether 11n is supported
 *
 *  @param priv     A pointer to mlan_private
 *  @param ra       Address of the receiver STA
 *
 *  @return         MTRUE or MFALSE
 */
static INLINE int
wlan_is_11n_enabled(mlan_private *priv, t_u8 *ra)
{
	int ret = MFALSE;
	ENTER();
#ifdef UAP_SUPPORT
	if (GET_BSS_ROLE(priv) == MLAN_BSS_ROLE_UAP) {
		if ((!(ra[0] & 0x01)) && (priv->is_11n_enabled))
			ret = is_station_11n_enabled(priv, ra);
	}
#endif /* UAP_SUPPORT */
	LEAVE();
	return ret;
}
#endif /* !_MLAN_11N_H_ */
