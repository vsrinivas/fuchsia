/** @file mlan_uap_txrx.c
 *
 *  @brief This file contains AP mode transmit and receive functions
 *
 *
 *  Copyright 2009-2021 NXP
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
    02/05/2009: initial version
********************************************************/

#include "mlan.h"
#include "mlan_util.h"
#include "mlan_fw.h"
#ifdef STA_SUPPORT
#include "mlan_join.h"
#endif
#include "mlan_main.h"
#include "mlan_uap.h"
#include "mlan_wmm.h"
#include "mlan_11n_aggr.h"
#include "mlan_11n_rxreorder.h"
#ifdef DRV_EMBEDDED_AUTHENTICATOR
#include "authenticator_api.h"
#endif

/********************************************************
			Local Functions
********************************************************/

/**
 *  @brief This function processes received packet and forwards it
 *          to kernel/upper layer
 *
 *  @param pmadapter A pointer to mlan_adapter
 *  @param pmbuf     A pointer to mlan_buffer which includes the received packet
 *
 *  @return          MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
static mlan_status
wlan_upload_uap_rx_packet(pmlan_adapter pmadapter, pmlan_buffer pmbuf)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
#ifdef DEBUG_LEVEL1
	pmlan_private priv = pmadapter->priv[pmbuf->bss_index];
#endif
	PRxPD prx_pd;
	ENTER();
	prx_pd = (RxPD *)(pmbuf->pbuf + pmbuf->data_offset);

	/* Chop off RxPD */
	pmbuf->data_len -= prx_pd->rx_pkt_offset;
	pmbuf->data_offset += prx_pd->rx_pkt_offset;
	pmbuf->pparent = MNULL;

	DBG_HEXDUMP(MDAT_D, "uAP RxPD", (t_u8 *)prx_pd,
		    MIN(sizeof(RxPD), MAX_DATA_DUMP_LEN));
	DBG_HEXDUMP(MDAT_D, "uAP Rx Payload",
		    ((t_u8 *)prx_pd + prx_pd->rx_pkt_offset),
		    MIN(prx_pd->rx_pkt_length, MAX_DATA_DUMP_LEN));

	pmadapter->callbacks.moal_get_system_time(pmadapter->pmoal_handle,
						  &pmbuf->out_ts_sec,
						  &pmbuf->out_ts_usec);
	PRINTM_NETINTF(MDATA, priv);
	PRINTM(MDATA, "%lu.%06lu : Data => kernel seq_num=%d tid=%d\n",
	       pmbuf->out_ts_sec, pmbuf->out_ts_usec, prx_pd->seq_num,
	       prx_pd->priority);
	ret = pmadapter->callbacks.moal_recv_packet(pmadapter->pmoal_handle,
						    pmbuf);
	if (ret == MLAN_STATUS_FAILURE) {
		PRINTM(MERROR,
		       "uAP Rx Error: moal_recv_packet returned error\n");
		pmbuf->status_code = MLAN_ERROR_PKT_INVALID;
	}

	if (ret != MLAN_STATUS_PENDING)
		pmadapter->ops.data_complete(pmadapter, pmbuf, ret);
#ifdef USB
	else if (IS_USB(pmadapter->card_type))
		pmadapter->callbacks.moal_recv_complete(pmadapter->pmoal_handle,
							MNULL,
							pmadapter->rx_data_ep,
							ret);
#endif
	LEAVE();

	return ret;
}

/**
 *  @brief This function will check if unicast packet need be dropped
 *
 *  @param priv    A pointer to mlan_private
 *  @param mac     mac address to find in station list table
 *
 *  @return	       MLAN_STATUS_FAILURE -- drop packet, otherwise forward to
 * network stack
 */
static mlan_status
wlan_check_unicast_packet(mlan_private *priv, t_u8 *mac)
{
	int j;
	sta_node *sta_ptr = MNULL;
	pmlan_adapter pmadapter = priv->adapter;
	pmlan_private pmpriv = MNULL;
	t_u8 pkt_type = 0;
	mlan_status ret = MLAN_STATUS_SUCCESS;
	ENTER();
	for (j = 0; j < MLAN_MAX_BSS_NUM; ++j) {
		pmpriv = pmadapter->priv[j];
		if (pmpriv) {
			if (GET_BSS_ROLE(pmpriv) == MLAN_BSS_ROLE_STA)
				continue;
			sta_ptr = wlan_get_station_entry(pmpriv, mac);
			if (sta_ptr) {
				if (pmpriv == priv)
					pkt_type = PKT_INTRA_UCAST;
				else
					pkt_type = PKT_INTER_UCAST;
				break;
			}
		}
	}
	if ((pkt_type == PKT_INTRA_UCAST) &&
	    (priv->pkt_fwd & PKT_FWD_INTRA_UCAST)) {
		PRINTM(MDATA, "Drop INTRA_UCAST packet\n");
		ret = MLAN_STATUS_FAILURE;
	} else if ((pkt_type == PKT_INTER_UCAST) &&
		   (priv->pkt_fwd & PKT_FWD_INTER_UCAST)) {
		PRINTM(MDATA, "Drop INTER_UCAST packet\n");
		ret = MLAN_STATUS_FAILURE;
	}
	LEAVE();
	return ret;
}

/********************************************************
			Global Functions
********************************************************/
/**
 *  @brief This function fill the txpd for tx packet
 *
 *  @param priv	   A pointer to mlan_private structure
 *  @param pmbuf   A pointer to the mlan_buffer for process
 *
 *  @return        headptr or MNULL
 */
t_void *
wlan_ops_uap_process_txpd(t_void *priv, pmlan_buffer pmbuf)
{
	pmlan_private pmpriv = (pmlan_private)priv;
	TxPD *plocal_tx_pd;
	t_u8 *head_ptr = MNULL;
	t_u32 pkt_type;
	t_u32 tx_control;
	t_u8 dst_mac[MLAN_MAC_ADDR_LENGTH];

	ENTER();

	if (!pmbuf->data_len) {
		PRINTM(MERROR, "uAP Tx Error: Invalid packet length: %d\n",
		       pmbuf->data_len);
		pmbuf->status_code = MLAN_ERROR_PKT_SIZE_INVALID;
		goto done;
	}
	if (pmbuf->buf_type == MLAN_BUF_TYPE_RAW_DATA) {
		memcpy_ext(pmpriv->adapter, &pkt_type,
			   pmbuf->pbuf + pmbuf->data_offset, sizeof(pkt_type),
			   sizeof(pkt_type));
		memcpy_ext(pmpriv->adapter, &tx_control,
			   pmbuf->pbuf + pmbuf->data_offset + sizeof(pkt_type),
			   sizeof(tx_control), sizeof(tx_control));
		pmbuf->data_offset += sizeof(pkt_type) + sizeof(tx_control);
		pmbuf->data_len -= sizeof(pkt_type) + sizeof(tx_control);
	}
	if (pmbuf->data_offset <
	    (sizeof(TxPD) + pmpriv->intf_hr_len + DMA_ALIGNMENT)) {
		PRINTM(MERROR,
		       "not enough space for TxPD: headroom=%d pkt_len=%d, required=%d\n",
		       pmbuf->data_offset, pmbuf->data_len,
		       sizeof(TxPD) + pmpriv->intf_hr_len + DMA_ALIGNMENT);
		DBG_HEXDUMP(MDAT_D, "drop pkt",
			    pmbuf->pbuf + pmbuf->data_offset, pmbuf->data_len);
		pmbuf->status_code = MLAN_ERROR_PKT_SIZE_INVALID;
		goto done;
	}

	/* head_ptr should be aligned */
	head_ptr = pmbuf->pbuf + pmbuf->data_offset - sizeof(TxPD) -
		pmpriv->intf_hr_len;
	head_ptr = (t_u8 *)((t_ptr)head_ptr & ~((t_ptr)(DMA_ALIGNMENT - 1)));

	plocal_tx_pd = (TxPD *)(head_ptr + pmpriv->intf_hr_len);
	memset(pmpriv->adapter, plocal_tx_pd, 0, sizeof(TxPD));

	/* Set the BSS number to TxPD */
	plocal_tx_pd->bss_num = GET_BSS_NUM(pmpriv);
	plocal_tx_pd->bss_type = pmpriv->bss_type;

	plocal_tx_pd->tx_pkt_length = (t_u16)pmbuf->data_len;

	plocal_tx_pd->priority = (t_u8)pmbuf->priority;
	plocal_tx_pd->pkt_delay_2ms =
		wlan_wmm_compute_driver_packet_delay(pmpriv, pmbuf);

	if (plocal_tx_pd->priority <
	    NELEMENTS(pmpriv->wmm.user_pri_pkt_tx_ctrl))
		/*
		 * Set the priority specific tx_control field, setting of 0 will
		 *   cause the default value to be used later in this function
		 */
		plocal_tx_pd->tx_control =
			pmpriv->wmm.user_pri_pkt_tx_ctrl[plocal_tx_pd->
							 priority];

	if (pmbuf->flags & MLAN_BUF_FLAG_TX_STATUS) {
		plocal_tx_pd->tx_control_1 |= pmbuf->tx_seq_num << 8;
		plocal_tx_pd->flags |= MRVDRV_TxPD_FLAGS_TX_PACKET_STATUS;
	}

	/* Offset of actual data */
	plocal_tx_pd->tx_pkt_offset = (t_u16)((t_ptr)pmbuf->pbuf +
					      pmbuf->data_offset -
					      (t_ptr)plocal_tx_pd);

	if (!plocal_tx_pd->tx_control) {
		/* TxCtrl set by user or default */
		plocal_tx_pd->tx_control = pmpriv->pkt_tx_ctrl;
	}

	if (pmbuf->buf_type == MLAN_BUF_TYPE_RAW_DATA) {
		plocal_tx_pd->tx_pkt_type = (t_u16)pkt_type;
		plocal_tx_pd->tx_control = tx_control;
	}

	if (pmbuf->flags & MLAN_BUF_FLAG_TX_CTRL) {
		if (pmbuf->u.tx_info.data_rate) {
			memcpy_ext(pmpriv->adapter, dst_mac,
				   pmbuf->pbuf + pmbuf->data_offset,
				   sizeof(dst_mac), sizeof(dst_mac));
			plocal_tx_pd->tx_control |=
				(wlan_ieee_rateid_to_mrvl_rateid
				 (pmpriv, pmbuf->u.tx_info.data_rate, dst_mac)
				 << 16);
			plocal_tx_pd->tx_control |= TXPD_TXRATE_ENABLE;
		}
		plocal_tx_pd->tx_control_1 |= pmbuf->u.tx_info.channel << 21;
		if (pmbuf->u.tx_info.bw) {
			plocal_tx_pd->tx_control_1 |= pmbuf->u.tx_info.bw << 16;
			plocal_tx_pd->tx_control_1 |= TXPD_BW_ENABLE;
		}
		if (pmbuf->u.tx_info.tx_power.tp.hostctl)
			plocal_tx_pd->tx_control |=
				(t_u32)pmbuf->u.tx_info.tx_power.val;
		if (pmbuf->u.tx_info.retry_limit) {
			plocal_tx_pd->tx_control |= pmbuf->u.tx_info.retry_limit
				<< 8;
			plocal_tx_pd->tx_control |= TXPD_RETRY_ENABLE;
		}
	}

	endian_convert_TxPD(plocal_tx_pd);

	/* Adjust the data offset and length to include TxPD in pmbuf */
	pmbuf->data_len += pmbuf->data_offset;
	pmbuf->data_offset = (t_u32)((t_ptr)head_ptr - (t_ptr)pmbuf->pbuf);
	pmbuf->data_len -= pmbuf->data_offset;

done:
	LEAVE();
	return head_ptr;
}

/**
 *  @brief This function processes received packet and forwards it
 *          to kernel/upper layer
 *
 *  @param adapter   A pointer to mlan_adapter
 *  @param pmbuf     A pointer to mlan_buffer which includes the received packet
 *
 *  @return          MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
mlan_status
wlan_ops_uap_process_rx_packet(t_void *adapter, pmlan_buffer pmbuf)
{
	pmlan_adapter pmadapter = (pmlan_adapter)adapter;
	mlan_status ret = MLAN_STATUS_SUCCESS;
	RxPD *prx_pd;
	wlan_mgmt_pkt *puap_pkt_hdr = MNULL;

	RxPacketHdr_t *prx_pkt;
	pmlan_private priv = pmadapter->priv[pmbuf->bss_index];
	t_u8 ta[MLAN_MAC_ADDR_LENGTH];
	t_u16 rx_pkt_type = 0;
	sta_node *sta_ptr = MNULL;
#ifdef DRV_EMBEDDED_AUTHENTICATOR
	t_u8 eapol_type[2] = { 0x88, 0x8e };
#endif
	t_u16 adj_rx_rate = 0;
	t_u8 antenna = 0;

	t_u32 last_rx_sec = 0;
	t_u32 last_rx_usec = 0;
	t_u8 ext_rate_info = 0;

	ENTER();

	prx_pd = (RxPD *)(pmbuf->pbuf + pmbuf->data_offset);
	/* Endian conversion */
	endian_convert_RxPD(prx_pd);

	if (priv->adapter->pcard_info->v14_fw_api) {
		t_u8 rxpd_rate_info_orig = prx_pd->rate_info;
		prx_pd->rate_info =
			wlan_convert_v14_rx_rate_info(priv,
						      rxpd_rate_info_orig);
		PRINTM(MINFO,
		       "UAP RX: v14_fw_api=%d rx_rate =%d rxpd_rate_info=0x%x->0x%x\n",
		       priv->adapter->pcard_info->v14_fw_api, prx_pd->rx_rate,
		       rxpd_rate_info_orig, prx_pd->rate_info);
	}

	if (priv->rx_pkt_info) {
		ext_rate_info = (t_u8)(prx_pd->rx_info >> 16);
		pmbuf->u.rx_info.data_rate =
			wlan_index_to_data_rate(priv->adapter, prx_pd->rx_rate,
						prx_pd->rate_info,
						ext_rate_info);
		pmbuf->u.rx_info.channel =
			(prx_pd->rx_info & RXPD_CHAN_MASK) >> 5;
		pmbuf->u.rx_info.antenna = prx_pd->antenna;
		pmbuf->u.rx_info.rssi = prx_pd->snr - prx_pd->nf;
	}

	rx_pkt_type = prx_pd->rx_pkt_type;
	prx_pkt = (RxPacketHdr_t *)((t_u8 *)prx_pd + prx_pd->rx_pkt_offset);

	PRINTM(MINFO,
	       "RX Data: data_len - prx_pd->rx_pkt_offset = %d - %d = %d\n",
	       pmbuf->data_len, prx_pd->rx_pkt_offset,
	       pmbuf->data_len - prx_pd->rx_pkt_offset);

	if ((prx_pd->rx_pkt_offset + prx_pd->rx_pkt_length) !=
	    (t_u16)pmbuf->data_len) {
		PRINTM(MERROR,
		       "Wrong rx packet: len=%d,rx_pkt_offset=%d,"
		       " rx_pkt_length=%d\n",
		       pmbuf->data_len, prx_pd->rx_pkt_offset,
		       prx_pd->rx_pkt_length);
		pmbuf->status_code = MLAN_ERROR_PKT_SIZE_INVALID;
		ret = MLAN_STATUS_FAILURE;
		pmadapter->ops.data_complete(pmadapter, pmbuf, ret);
		goto done;
	}
	pmbuf->data_len = prx_pd->rx_pkt_offset + prx_pd->rx_pkt_length;

	if (pmadapter->priv[pmbuf->bss_index]->mgmt_frame_passthru_mask &&
	    prx_pd->rx_pkt_type == PKT_TYPE_MGMT_FRAME) {
		/* Check if this is mgmt packet and needs to
		 * forwarded to app as an event
		 */
		puap_pkt_hdr = (wlan_mgmt_pkt *)((t_u8 *)prx_pd +
						 prx_pd->rx_pkt_offset);
		puap_pkt_hdr->frm_len = wlan_le16_to_cpu(puap_pkt_hdr->frm_len);
		if ((puap_pkt_hdr->wlan_header.frm_ctl &
		     IEEE80211_FC_MGMT_FRAME_TYPE_MASK) == 0)
			wlan_process_802dot11_mgmt_pkt(pmadapter->
						       priv[pmbuf->bss_index],
						       (t_u8 *)&puap_pkt_hdr->
						       wlan_header,
						       puap_pkt_hdr->frm_len +
						       sizeof(wlan_mgmt_pkt) -
						       sizeof(puap_pkt_hdr->
							      frm_len),
						       (RxPD *)prx_pd);
		pmadapter->ops.data_complete(pmadapter, pmbuf, ret);
		goto done;
	}
	if (rx_pkt_type != PKT_TYPE_BAR) {
		priv->rxpd_rate = prx_pd->rx_rate;
		priv->rxpd_rate_info = prx_pd->rate_info;
		priv->rxpd_rx_info = (t_u8)(prx_pd->rx_info >> 16);

		if (priv->bss_type == MLAN_BSS_TYPE_UAP) {
			antenna = wlan_adjust_antenna(priv, (RxPD *)prx_pd);
			adj_rx_rate =
				wlan_adjust_data_rate(priv, priv->rxpd_rate,
						      priv->rxpd_rate_info);
			pmadapter->callbacks.moal_hist_data_add(pmadapter->
								pmoal_handle,
								pmbuf->
								bss_index,
								adj_rx_rate,
								prx_pd->snr,
								prx_pd->nf,
								antenna);
		}
	}

	sta_ptr = wlan_get_station_entry(priv, prx_pkt->eth803_hdr.src_addr);
	if (sta_ptr) {
		sta_ptr->snr = prx_pd->snr;
		sta_ptr->nf = prx_pd->nf;
		pmadapter->callbacks.moal_get_system_time(pmadapter->
							  pmoal_handle,
							  &last_rx_sec,
							  &last_rx_usec);
		sta_ptr->stats.last_rx_in_msec =
			(t_u64)last_rx_sec *1000 + (t_u64)last_rx_usec / 1000;
	}
#ifdef DRV_EMBEDDED_AUTHENTICATOR
	/**process eapol packet for uap*/
	if (IsAuthenticatorEnabled(priv->psapriv) &&
	    (!memcmp(pmadapter, &prx_pkt->eth803_hdr.h803_len, eapol_type,
		     sizeof(eapol_type)))) {
		ret = AuthenticatorProcessEapolPacket(priv->psapriv,
						      ((t_u8 *)prx_pd +
						       prx_pd->rx_pkt_offset),
						      prx_pd->rx_pkt_length);
		if (ret == MLAN_STATUS_SUCCESS) {
			pmadapter->ops.data_complete(pmadapter, pmbuf, ret);
			goto done;
		}
	}
#endif

	pmbuf->priority |= prx_pd->priority;
	memcpy_ext(pmadapter, ta, prx_pkt->eth803_hdr.src_addr,
		   MLAN_MAC_ADDR_LENGTH, MLAN_MAC_ADDR_LENGTH);
	if ((rx_pkt_type != PKT_TYPE_BAR) && (prx_pd->priority < MAX_NUM_TID)) {
		sta_ptr = wlan_get_station_entry(priv, ta);
		if (sta_ptr) {
			sta_ptr->rx_seq[prx_pd->priority] = prx_pd->seq_num;
			sta_ptr->snr = prx_pd->snr;
			sta_ptr->nf = prx_pd->nf;
		}
	}
	/* check if UAP enable 11n */
	if (!priv->is_11n_enabled ||
	    (!wlan_11n_get_rxreorder_tbl((mlan_private *)priv, prx_pd->priority,
					 ta)
	     && (prx_pd->rx_pkt_type != PKT_TYPE_AMSDU)
	    )
		) {
		if (priv->pkt_fwd)
			wlan_process_uap_rx_packet(priv, pmbuf);
		else
			wlan_upload_uap_rx_packet(pmadapter, pmbuf);
		goto done;
	}
	/* Reorder and send to OS */
	ret = mlan_11n_rxreorder_pkt(priv, prx_pd->seq_num, prx_pd->priority,
				     ta, (t_u8)prx_pd->rx_pkt_type,
				     (void *)pmbuf);
	if (ret || (rx_pkt_type == PKT_TYPE_BAR)) {
		pmadapter->ops.data_complete(pmadapter, pmbuf, ret);
	}
done:
	LEAVE();
	return ret;
}

/**
 *  @brief This function processes received packet and forwards it
 *          to kernel/upper layer or send back to firmware
 *
 *  @param priv      A pointer to mlan_private
 *  @param pmbuf     A pointer to mlan_buffer which includes the received packet
 *
 *  @return          MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
mlan_status
wlan_uap_recv_packet(mlan_private *priv, pmlan_buffer pmbuf)
{
	pmlan_adapter pmadapter = priv->adapter;
	mlan_status ret = MLAN_STATUS_SUCCESS;
	RxPacketHdr_t *prx_pkt;
	pmlan_buffer newbuf = MNULL;

	ENTER();

	prx_pkt = (RxPacketHdr_t *)((t_u8 *)pmbuf->pbuf + pmbuf->data_offset);

	DBG_HEXDUMP(MDAT_D, "uap_recv_packet", pmbuf->pbuf + pmbuf->data_offset,
		    MIN(pmbuf->data_len, MAX_DATA_DUMP_LEN));

	PRINTM(MDATA, "AMSDU dest " MACSTR "\n",
	       MAC2STR(prx_pkt->eth803_hdr.dest_addr));

	/* don't do packet forwarding in disconnected state */
	if ((priv->media_connected == MFALSE) ||
	    (pmbuf->data_len > MV_ETH_FRAME_LEN))
		goto upload;

	if (prx_pkt->eth803_hdr.dest_addr[0] & 0x01) {
		if (!(priv->pkt_fwd & PKT_FWD_INTRA_BCAST)) {
			/* Multicast pkt */
			newbuf = wlan_alloc_mlan_buffer(pmadapter,
							MLAN_TX_DATA_BUF_SIZE_2K,
							0, MOAL_MALLOC_BUFFER);
			if (newbuf) {
				newbuf->bss_index = pmbuf->bss_index;
				newbuf->buf_type = pmbuf->buf_type;
				newbuf->priority = pmbuf->priority;
				newbuf->in_ts_sec = pmbuf->in_ts_sec;
				newbuf->in_ts_usec = pmbuf->in_ts_usec;
				newbuf->data_offset =
					(sizeof(TxPD) + priv->intf_hr_len +
					 DMA_ALIGNMENT);
				util_scalar_increment(pmadapter->pmoal_handle,
						      &pmadapter->
						      pending_bridge_pkts,
						      pmadapter->callbacks.
						      moal_spin_lock,
						      pmadapter->callbacks.
						      moal_spin_unlock);

				newbuf->flags |= MLAN_BUF_FLAG_BRIDGE_BUF;

				/* copy the data */
				memcpy_ext(pmadapter,
					   (t_u8 *)newbuf->pbuf +
					   newbuf->data_offset,
					   pmbuf->pbuf + pmbuf->data_offset,
					   pmbuf->data_len,
					   MLAN_TX_DATA_BUF_SIZE_2K);
				newbuf->data_len = pmbuf->data_len;
				wlan_wmm_add_buf_txqueue(pmadapter, newbuf);
				if (util_scalar_read(pmadapter->pmoal_handle,
						     &pmadapter->
						     pending_bridge_pkts,
						     pmadapter->callbacks.
						     moal_spin_lock,
						     pmadapter->callbacks.
						     moal_spin_unlock) >
				    RX_HIGH_THRESHOLD)
					wlan_drop_tx_pkts(priv);
				wlan_recv_event(priv,
						MLAN_EVENT_ID_DRV_DEFER_HANDLING,
						MNULL);
			}
		}
	} else {
		if ((!(priv->pkt_fwd & PKT_FWD_INTRA_UCAST)) &&
		    (wlan_get_station_entry(priv,
					    prx_pkt->eth803_hdr.dest_addr))) {
			/* Intra BSS packet */
			newbuf = wlan_alloc_mlan_buffer(pmadapter,
							MLAN_TX_DATA_BUF_SIZE_2K,
							0, MOAL_MALLOC_BUFFER);
			if (newbuf) {
				newbuf->bss_index = pmbuf->bss_index;
				newbuf->buf_type = pmbuf->buf_type;
				newbuf->priority = pmbuf->priority;
				newbuf->in_ts_sec = pmbuf->in_ts_sec;
				newbuf->in_ts_usec = pmbuf->in_ts_usec;
				newbuf->data_offset =
					(sizeof(TxPD) + priv->intf_hr_len +
					 DMA_ALIGNMENT);
				util_scalar_increment(pmadapter->pmoal_handle,
						      &pmadapter->
						      pending_bridge_pkts,
						      pmadapter->callbacks.
						      moal_spin_lock,
						      pmadapter->callbacks.
						      moal_spin_unlock);
				newbuf->flags |= MLAN_BUF_FLAG_BRIDGE_BUF;

				/* copy the data */
				memcpy_ext(pmadapter,
					   (t_u8 *)newbuf->pbuf +
					   newbuf->data_offset,
					   pmbuf->pbuf + pmbuf->data_offset,
					   pmbuf->data_len,
					   MLAN_TX_DATA_BUF_SIZE_2K);
				newbuf->data_len = pmbuf->data_len;
				wlan_wmm_add_buf_txqueue(pmadapter, newbuf);
				if (util_scalar_read(pmadapter->pmoal_handle,
						     &pmadapter->
						     pending_bridge_pkts,
						     pmadapter->callbacks.
						     moal_spin_lock,
						     pmadapter->callbacks.
						     moal_spin_unlock) >
				    RX_HIGH_THRESHOLD)
					wlan_drop_tx_pkts(priv);
				wlan_recv_event(priv,
						MLAN_EVENT_ID_DRV_DEFER_HANDLING,
						MNULL);
			}
			goto done;
		} else if (MLAN_STATUS_FAILURE ==
			   wlan_check_unicast_packet(priv,
						     prx_pkt->eth803_hdr.
						     dest_addr)) {
			/* drop packet */
			PRINTM(MDATA, "Drop AMSDU dest " MACSTR "\n",
			       MAC2STR(prx_pkt->eth803_hdr.dest_addr));
			goto done;
		}
	}
upload:
	/** send packet to moal */
	ret = pmadapter->callbacks.moal_recv_packet(pmadapter->pmoal_handle,
						    pmbuf);
done:
	LEAVE();
	return ret;
}

/**
 *  @brief This function processes received packet and forwards it
 *          to kernel/upper layer or send back to firmware
 *
 *  @param priv      A pointer to mlan_private
 *  @param pmbuf     A pointer to mlan_buffer which includes the received packet
 *
 *  @return          MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
mlan_status
wlan_process_uap_rx_packet(mlan_private *priv, pmlan_buffer pmbuf)
{
	pmlan_adapter pmadapter = priv->adapter;
	mlan_status ret = MLAN_STATUS_SUCCESS;
	RxPD *prx_pd;
	RxPacketHdr_t *prx_pkt;
	pmlan_buffer newbuf = MNULL;

	ENTER();

	prx_pd = (RxPD *)(pmbuf->pbuf + pmbuf->data_offset);
	prx_pkt = (RxPacketHdr_t *)((t_u8 *)prx_pd + prx_pd->rx_pkt_offset);

	DBG_HEXDUMP(MDAT_D, "uAP RxPD", prx_pd,
		    MIN(sizeof(RxPD), MAX_DATA_DUMP_LEN));
	DBG_HEXDUMP(MDAT_D, "uAP Rx Payload",
		    ((t_u8 *)prx_pd + prx_pd->rx_pkt_offset),
		    MIN(prx_pd->rx_pkt_length, MAX_DATA_DUMP_LEN));

	PRINTM(MINFO,
	       "RX Data: data_len - prx_pd->rx_pkt_offset = %d - %d = %d\n",
	       pmbuf->data_len, prx_pd->rx_pkt_offset,
	       pmbuf->data_len - prx_pd->rx_pkt_offset);
	PRINTM(MDATA, "Rx dest " MACSTR "\n",
	       MAC2STR(prx_pkt->eth803_hdr.dest_addr));

	/* don't do packet forwarding in disconnected state */
	/* don't do packet forwarding when packet > 1514 */
	if ((priv->media_connected == MFALSE) ||
	    ((pmbuf->data_len - prx_pd->rx_pkt_offset) > MV_ETH_FRAME_LEN))
		goto upload;

	if (prx_pkt->eth803_hdr.dest_addr[0] & 0x01) {
		if (!(priv->pkt_fwd & PKT_FWD_INTRA_BCAST)) {
			/* Multicast pkt */
			newbuf = wlan_alloc_mlan_buffer(pmadapter,
							MLAN_TX_DATA_BUF_SIZE_2K,
							0, MOAL_MALLOC_BUFFER);
			if (newbuf) {
				newbuf->bss_index = pmbuf->bss_index;
				newbuf->buf_type = pmbuf->buf_type;
				newbuf->priority = pmbuf->priority;
				newbuf->in_ts_sec = pmbuf->in_ts_sec;
				newbuf->in_ts_usec = pmbuf->in_ts_usec;
				newbuf->data_offset =
					(sizeof(TxPD) + priv->intf_hr_len +
					 DMA_ALIGNMENT);
				util_scalar_increment(pmadapter->pmoal_handle,
						      &pmadapter->
						      pending_bridge_pkts,
						      pmadapter->callbacks.
						      moal_spin_lock,
						      pmadapter->callbacks.
						      moal_spin_unlock);
				newbuf->flags |= MLAN_BUF_FLAG_BRIDGE_BUF;

				/* copy the data, skip rxpd */
				memcpy_ext(pmadapter,
					   (t_u8 *)newbuf->pbuf +
					   newbuf->data_offset,
					   pmbuf->pbuf + pmbuf->data_offset +
					   prx_pd->rx_pkt_offset,
					   pmbuf->data_len -
					   prx_pd->rx_pkt_offset,
					   MLAN_TX_DATA_BUF_SIZE_2K);
				newbuf->data_len =
					pmbuf->data_len - prx_pd->rx_pkt_offset;
				wlan_wmm_add_buf_txqueue(pmadapter, newbuf);
				if (util_scalar_read(pmadapter->pmoal_handle,
						     &pmadapter->
						     pending_bridge_pkts,
						     pmadapter->callbacks.
						     moal_spin_lock,
						     pmadapter->callbacks.
						     moal_spin_unlock) >
				    RX_HIGH_THRESHOLD)
					wlan_drop_tx_pkts(priv);
				wlan_recv_event(priv,
						MLAN_EVENT_ID_DRV_DEFER_HANDLING,
						MNULL);
			}
		}
	} else {
		if ((!(priv->pkt_fwd & PKT_FWD_INTRA_UCAST)) &&
		    (wlan_get_station_entry(priv,
					    prx_pkt->eth803_hdr.dest_addr))) {
			/* Forwarding Intra-BSS packet */
#ifdef USB
			if (IS_USB(pmadapter->card_type)) {
				if (pmbuf->flags & MLAN_BUF_FLAG_RX_DEAGGR) {
					newbuf = wlan_alloc_mlan_buffer
						(pmadapter,
						 MLAN_TX_DATA_BUF_SIZE_2K, 0,
						 MOAL_MALLOC_BUFFER);
					if (newbuf) {
						newbuf->bss_index =
							pmbuf->bss_index;
						newbuf->buf_type =
							pmbuf->buf_type;
						newbuf->priority =
							pmbuf->priority;
						newbuf->in_ts_sec =
							pmbuf->in_ts_sec;
						newbuf->in_ts_usec =
							pmbuf->in_ts_usec;
						newbuf->data_offset =
							(sizeof(TxPD) +
							 priv->intf_hr_len +
							 DMA_ALIGNMENT);
						util_scalar_increment
							(pmadapter->
							 pmoal_handle,
							 &pmadapter->
							 pending_bridge_pkts,
							 pmadapter->callbacks.
							 moal_spin_lock,
							 pmadapter->callbacks.
							 moal_spin_unlock);
						newbuf->flags |=
							MLAN_BUF_FLAG_BRIDGE_BUF;

						/* copy the data, skip rxpd */
						memcpy_ext(pmadapter,
							   (t_u8 *)newbuf->
							   pbuf +
							   newbuf->data_offset,
							   pmbuf->pbuf +
							   pmbuf->data_offset +
							   prx_pd->
							   rx_pkt_offset,
							   pmbuf->data_len -
							   prx_pd->
							   rx_pkt_offset,
							   pmbuf->data_len -
							   prx_pd->
							   rx_pkt_offset);
						newbuf->data_len =
							pmbuf->data_len -
							prx_pd->rx_pkt_offset;
						wlan_wmm_add_buf_txqueue
							(pmadapter, newbuf);
						if (util_scalar_read
						    (pmadapter->pmoal_handle,
						     &pmadapter->
						     pending_bridge_pkts,
						     pmadapter->callbacks.
						     moal_spin_lock,
						     pmadapter->callbacks.
						     moal_spin_unlock) >
						    RX_HIGH_THRESHOLD)
							wlan_drop_tx_pkts(priv);
						wlan_recv_event(priv,
								MLAN_EVENT_ID_DRV_DEFER_HANDLING,
								MNULL);
					}
					pmadapter->callbacks.
						moal_recv_complete(pmadapter->
								   pmoal_handle,
								   pmbuf,
								   pmadapter->
								   rx_data_ep,
								   ret);
					goto done;
				}
			}
#endif
			pmbuf->data_len -= prx_pd->rx_pkt_offset;
			pmbuf->data_offset += prx_pd->rx_pkt_offset;
			pmbuf->flags |= MLAN_BUF_FLAG_BRIDGE_BUF;
			util_scalar_increment(pmadapter->pmoal_handle,
					      &pmadapter->pending_bridge_pkts,
					      pmadapter->callbacks.
					      moal_spin_lock,
					      pmadapter->callbacks.
					      moal_spin_unlock);
			wlan_wmm_add_buf_txqueue(pmadapter, pmbuf);
			if (util_scalar_read(pmadapter->pmoal_handle,
					     &pmadapter->pending_bridge_pkts,
					     pmadapter->callbacks.
					     moal_spin_lock,
					     pmadapter->callbacks.
					     moal_spin_unlock) >
			    RX_HIGH_THRESHOLD)
				wlan_drop_tx_pkts(priv);
			wlan_recv_event(priv, MLAN_EVENT_ID_DRV_DEFER_HANDLING,
					MNULL);
			goto done;
		} else if (MLAN_STATUS_FAILURE ==
			   wlan_check_unicast_packet(priv,
						     prx_pkt->eth803_hdr.
						     dest_addr)) {
			PRINTM(MDATA, "Drop Pkts: Rx dest " MACSTR "\n",
			       MAC2STR(prx_pkt->eth803_hdr.dest_addr));
			pmbuf->status_code = MLAN_ERROR_PKT_INVALID;
			pmadapter->ops.data_complete(pmadapter, pmbuf, ret);
			goto done;
		}
	}

upload:
	/* Chop off RxPD */
	pmbuf->data_len -= prx_pd->rx_pkt_offset;
	pmbuf->data_offset += prx_pd->rx_pkt_offset;
	pmbuf->pparent = MNULL;

	pmadapter->callbacks.moal_get_system_time(pmadapter->pmoal_handle,
						  &pmbuf->out_ts_sec,
						  &pmbuf->out_ts_usec);
	PRINTM_NETINTF(MDATA, priv);
	PRINTM(MDATA, "%lu.%06lu : Data => kernel seq_num=%d tid=%d\n",
	       pmbuf->out_ts_sec, pmbuf->out_ts_usec, prx_pd->seq_num,
	       prx_pd->priority);

	ret = pmadapter->callbacks.moal_recv_packet(pmadapter->pmoal_handle,
						    pmbuf);
	if (ret == MLAN_STATUS_FAILURE) {
		PRINTM(MERROR,
		       "uAP Rx Error: moal_recv_packet returned error\n");
		pmbuf->status_code = MLAN_ERROR_PKT_INVALID;
	}

	if (ret != MLAN_STATUS_PENDING)
		pmadapter->ops.data_complete(pmadapter, pmbuf, ret);
#ifdef USB
	else if (IS_USB(pmadapter->card_type))
		pmadapter->callbacks.moal_recv_complete(pmadapter->pmoal_handle,
							MNULL,
							pmadapter->rx_data_ep,
							ret);
#endif
done:
	LEAVE();
	return ret;
}
