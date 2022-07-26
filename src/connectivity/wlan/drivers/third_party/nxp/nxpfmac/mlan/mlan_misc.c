/**
 * @file mlan_misc.c
 *
 *  @brief This file include miscellaneous functions for MLAN module
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

/*************************************************************
Change Log:
    05/11/2009: initial version
************************************************************/
#include "mlan.h"
#ifdef STA_SUPPORT
#include "mlan_join.h"
#endif /* STA_SUPPORT */
#include "mlan_util.h"
#include "mlan_fw.h"
#include "mlan_main.h"
#include "mlan_wmm.h"
#include "mlan_11n.h"
#include "mlan_11ac.h"
#include "mlan_11ax.h"
#ifdef UAP_SUPPORT
#include "mlan_uap.h"
#endif
#ifdef DRV_EMBEDDED_AUTHENTICATOR
#include "authenticator_api.h"
#endif
/********************************************************
			Local Variables
********************************************************/

/********************************************************
			Global Variables
********************************************************/

/********************************************************
			Local Functions
********************************************************/
#if defined(PCIE) || defined(SDIO)
/**
 *  @brief Check pending irq
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *
 *  @return        MTRUE/MFALSE;
 */
static t_u8
wlan_pending_interrupt(pmlan_adapter pmadapter)
{
	if (!IS_USB(pmadapter->card_type) && pmadapter->ireg)
		return MTRUE;
	return MFALSE;
}
#endif

/** Custom IE auto index and mask */
#define MLAN_CUSTOM_IE_AUTO_IDX_MASK 0xffff
/** Custom IE mask for delete operation */
#define MLAN_CUSTOM_IE_DELETE_MASK 0
/** Custom IE mask for create new index */
#define MLAN_CUSTOM_IE_NEW_MASK 0x8000
/** Custom IE header size */
#define MLAN_CUSTOM_IE_HDR_SIZE (sizeof(custom_ie) - MAX_IE_SIZE)

/**
 *  @brief Check if current custom IE index is used on other interfaces.
 *
 *  @param pmpriv   A pointer to mlan_private structure
 *  @param idx		index to check for in use
 *
 *  @return		MLAN_STATUS_SUCCESS --unused, otherwise used.
 */
static mlan_status
wlan_is_custom_ie_index_unused(pmlan_private pmpriv, t_u16 idx)
{
	t_u8 i = 0;
	pmlan_adapter pmadapter = pmpriv->adapter;
	pmlan_private priv;
	ENTER();

	for (i = 0; i < pmadapter->priv_num; i++) {
		priv = pmadapter->priv[i];
		/* Check for other interfaces only */
		if (priv && priv->bss_index != pmpriv->bss_index) {
			if (priv->mgmt_ie[idx].mgmt_subtype_mask &&
			    priv->mgmt_ie[idx].ie_length) {
				/* used entry found */
				LEAVE();
				return MLAN_STATUS_FAILURE;
			}
		}
	}
	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief Get the custom IE index
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *  @param mask	mask value for which the index to be returned
 *  @param ie_data	a pointer to custom_ie structure
 *  @param idx		will hold the computed index
 *
 *  @return		MLAN_STATUS_SUCCESS --success, otherwise fail
 */
static mlan_status
wlan_custom_ioctl_get_autoidx(pmlan_private pmpriv,
			      pmlan_ioctl_req pioctl_req,
			      t_u16 mask, custom_ie *ie_data, t_u16 *idx)
{
	t_u16 index = 0, insert = MFALSE;
	mlan_status ret = MLAN_STATUS_SUCCESS;

	ENTER();
	/* Determine the index where the IE needs to be inserted */
	while (!insert) {
		while (index < MIN(pmpriv->adapter->max_mgmt_ie_index,
				   MAX_MGMT_IE_INDEX)) {
			if (pmpriv->mgmt_ie[index].mgmt_subtype_mask ==
			    MLAN_CUSTOM_IE_AUTO_IDX_MASK) {
				index++;
				continue;
			}
			if (pmpriv->mgmt_ie[index].mgmt_subtype_mask == mask) {
				/* Duplicate IE should be avoided */
				if (pmpriv->mgmt_ie[index].ie_length) {
					if (!memcmp(pmpriv->adapter,
						    pmpriv->mgmt_ie[index]
						    .ie_buffer,
						    ie_data->ie_buffer,
						    pmpriv->mgmt_ie[index]
						    .ie_length)) {
						PRINTM(MINFO,
						       "IE with the same mask exists at index %d mask=0x%x\n",
						       index, mask);
						*idx = MLAN_CUSTOM_IE_AUTO_IDX_MASK;
						goto done;
					}
				}
				/* Check if enough space is available */
				if (pmpriv->mgmt_ie[index].ie_length +
				    ie_data->ie_length > MAX_IE_SIZE) {
					index++;
					continue;
				}
				insert = MTRUE;
				break;
			}
			index++;
		}
		if (!insert) {
			for (index = 0;
			     index < MIN(pmpriv->adapter->max_mgmt_ie_index,
					 MAX_MGMT_IE_INDEX); index++) {
				if (pmpriv->mgmt_ie[index].ie_length == 0) {
					/*
					 * Check if this index is in use
					 * by other interface If yes,
					 * move ahead to next index
					 */
					if (MLAN_STATUS_SUCCESS ==
					    wlan_is_custom_ie_index_unused
					    (pmpriv, index)) {
						insert = MTRUE;
						break;
					} else {
						PRINTM(MINFO,
						       "Skipping IE index %d in use.\n",
						       index);
					}
				}
			}
		}
		if (index == pmpriv->adapter->max_mgmt_ie_index && !insert) {
			PRINTM(MERROR, "Failed to Set the IE buffer\n");
			if (pioctl_req)
				pioctl_req->status_code = MLAN_ERROR_IOCTL_FAIL;
			ret = MLAN_STATUS_FAILURE;
			goto done;
		}
	}

	*idx = index;
done:
	LEAVE();
	return ret;
}

/**
 *  @brief Delete custom IE
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *  @param ie_data	a pointer to custom_ie structure
 *  @param idx		index supplied
 *
 *  @return		MLAN_STATUS_SUCCESS --success, otherwise fail
 */

static mlan_status
wlan_custom_ioctl_auto_delete(pmlan_private pmpriv,
			      pmlan_ioctl_req pioctl_req,
			      custom_ie *ie_data, t_u16 idx)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	pmlan_adapter pmadapter = pmpriv->adapter;
	t_u16 index = 0, insert = MFALSE, del_len;
	t_u8 del_ie[MAX_IE_SIZE], ie[MAX_IE_SIZE];
	t_s32 cnt, tmp_len = 0;
	t_u8 *tmp_ie;

	ENTER();
	memset(pmpriv->adapter, del_ie, 0, MAX_IE_SIZE);
	memcpy_ext(pmpriv->adapter, del_ie, ie_data->ie_buffer,
		   ie_data->ie_length, MAX_IE_SIZE);
	del_len = MIN(MAX_IE_SIZE - 1, ie_data->ie_length);

	if (MLAN_CUSTOM_IE_AUTO_IDX_MASK == idx)
		ie_data->ie_index = 0;

	for (index = 0;
	     index < MIN(pmadapter->max_mgmt_ie_index, MAX_MGMT_IE_INDEX);
	     index++) {
		if (MLAN_CUSTOM_IE_AUTO_IDX_MASK != idx &&
		    idx < MAX_MGMT_IE_INDEX)
			index = idx;
		tmp_ie = pmpriv->mgmt_ie[index].ie_buffer;
		tmp_len = pmpriv->mgmt_ie[index].ie_length;
		cnt = 0;
		while (tmp_len) {
			if (!memcmp(pmpriv->adapter, tmp_ie, del_ie, del_len)) {
				memcpy_ext(pmpriv->adapter, ie,
					   pmpriv->mgmt_ie[index].ie_buffer,
					   cnt, MAX_IE_SIZE);
				if (pmpriv->mgmt_ie[index].ie_length >
				    (cnt + del_len))
					memcpy_ext(pmpriv->adapter, &ie[cnt],
						   &pmpriv->mgmt_ie[index].
						   ie_buffer[MIN
							     ((MAX_IE_SIZE - 1),
							      (cnt + del_len))],
						   (pmpriv->mgmt_ie[index]
						    .ie_length - (cnt +
								  del_len)),
						   MAX_IE_SIZE - cnt);
				memset(pmpriv->adapter,
				       &pmpriv->mgmt_ie[index].ie_buffer, 0,
				       sizeof(pmpriv->mgmt_ie[index].
					      ie_buffer));
				memcpy_ext(pmpriv->adapter,
					   &pmpriv->mgmt_ie[index].ie_buffer,
					   ie,
					   pmpriv->mgmt_ie[index].ie_length -
					   del_len, MAX_IE_SIZE);
				pmpriv->mgmt_ie[index].ie_length -= del_len;
				if (MLAN_CUSTOM_IE_AUTO_IDX_MASK == idx)
					/* set a bit to indicate caller about
					 * update */
					ie_data->ie_index |=
						(((t_u16)1) << index);
				insert = MTRUE;
				tmp_ie = pmpriv->mgmt_ie[index].ie_buffer;
				tmp_len = pmpriv->mgmt_ie[index].ie_length;
				cnt = 0;
				continue;
			}
			tmp_ie++;
			tmp_len--;
			cnt++;
		}
		if (MLAN_CUSTOM_IE_AUTO_IDX_MASK != idx)
			break;
	}
	if (index == pmadapter->max_mgmt_ie_index && !insert) {
		PRINTM(MERROR, "Failed to Clear IE buffer\n");
		if (pioctl_req)
			pioctl_req->status_code = MLAN_ERROR_IOCTL_FAIL;
		ret = MLAN_STATUS_FAILURE;
	}
	LEAVE();
	return ret;
}

/********************************************************
			Global Functions
********************************************************/

/**
 *  @brief send host cmd
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_PENDING --success, otherwise fail
 */
mlan_status
wlan_misc_ioctl_host_cmd(pmlan_adapter pmadapter, pmlan_ioctl_req pioctl_req)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_ds_misc_cfg *misc = MNULL;

	ENTER();

	misc = (mlan_ds_misc_cfg *)pioctl_req->pbuf;

	/* Send request to firmware */
	ret = wlan_prepare_cmd(pmpriv, 0, 0, 0, (t_void *)pioctl_req,
			       (t_void *)&misc->param.hostcmd);
	if (ret == MLAN_STATUS_SUCCESS)
		ret = MLAN_STATUS_PENDING;

	LEAVE();
	return ret;
}

/**
 *  @brief Send function init/shutdown command to firmware
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_PENDING --success, otherwise fail
 */
mlan_status
wlan_misc_ioctl_init_shutdown(pmlan_adapter pmadapter,
			      pmlan_ioctl_req pioctl_req)
{
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_status ret = MLAN_STATUS_SUCCESS;
	mlan_ds_misc_cfg *misc_cfg = MNULL;
	t_u16 cmd;

	ENTER();

	misc_cfg = (mlan_ds_misc_cfg *)pioctl_req->pbuf;
	if (misc_cfg->param.func_init_shutdown == MLAN_FUNC_INIT)
		cmd = HostCmd_CMD_FUNC_INIT;
	else if (misc_cfg->param.func_init_shutdown == MLAN_FUNC_SHUTDOWN)
		cmd = HostCmd_CMD_FUNC_SHUTDOWN;
	else {
		PRINTM(MERROR, "Unsupported parameter\n");
		pioctl_req->status_code = MLAN_ERROR_INVALID_PARAMETER;
		ret = MLAN_STATUS_FAILURE;
		goto exit;
	}

	/* Send command to firmware */
	ret = wlan_prepare_cmd(pmpriv, cmd, HostCmd_ACT_GEN_SET, 0,
			       (t_void *)pioctl_req, MNULL);

	if (ret == MLAN_STATUS_SUCCESS)
		ret = MLAN_STATUS_PENDING;

exit:
	LEAVE();
	return ret;
}

/**
 *  @brief Get debug information
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_SUCCESS --success
 */
mlan_status
wlan_get_info_debug_info(pmlan_adapter pmadapter, pmlan_ioctl_req pioctl_req)
{
	pmlan_private pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_status ret = MLAN_STATUS_SUCCESS;
	mlan_ds_get_info *info;
	mlan_debug_info *debug_info = MNULL;
	t_u32 i;
	t_u8 *ptid;

	ENTER();

	info = (mlan_ds_get_info *)pioctl_req->pbuf;
	debug_info = (mlan_debug_info *)info->param.debug_info;

	if (pioctl_req->action == MLAN_ACT_GET) {
		ptid = ac_to_tid[WMM_AC_BK];
		debug_info->wmm_ac_bk = pmpriv->wmm.packets_out[ptid[0]] +
			pmpriv->wmm.packets_out[ptid[1]];
		ptid = ac_to_tid[WMM_AC_BE];
		debug_info->wmm_ac_be = pmpriv->wmm.packets_out[ptid[0]] +
			pmpriv->wmm.packets_out[ptid[1]];
		ptid = ac_to_tid[WMM_AC_VI];
		debug_info->wmm_ac_vi = pmpriv->wmm.packets_out[ptid[0]] +
			pmpriv->wmm.packets_out[ptid[1]];
		ptid = ac_to_tid[WMM_AC_VO];
		debug_info->wmm_ac_vo = pmpriv->wmm.packets_out[ptid[0]] +
			pmpriv->wmm.packets_out[ptid[1]];
		debug_info->max_tx_buf_size = (t_u32)pmadapter->max_tx_buf_size;
		debug_info->tx_buf_size = (t_u32)pmadapter->tx_buf_size;
		debug_info->curr_tx_buf_size =
			(t_u32)pmadapter->curr_tx_buf_size;
		debug_info->rx_tbl_num =
			wlan_get_rxreorder_tbl(pmpriv, debug_info->rx_tbl);
		debug_info->tx_tbl_num =
			wlan_get_txbastream_tbl(pmpriv, debug_info->tx_tbl);
		debug_info->ralist_num =
			wlan_get_ralist_info(pmpriv, debug_info->ralist);
		debug_info->tdls_peer_num =
			wlan_get_tdls_list(pmpriv, debug_info->tdls_peer_list);
		debug_info->ps_mode = pmadapter->ps_mode;
		debug_info->ps_state = pmadapter->ps_state;
#ifdef STA_SUPPORT
		debug_info->is_deep_sleep = pmadapter->is_deep_sleep;
#endif /* STA_SUPPORT */
		debug_info->pm_wakeup_card_req = pmadapter->pm_wakeup_card_req;
		debug_info->pm_wakeup_fw_try = pmadapter->pm_wakeup_fw_try;
		debug_info->pm_wakeup_in_secs = pmadapter->pm_wakeup_in_secs;
		debug_info->pm_wakeup_timeout = pmadapter->pm_wakeup_timeout;
		debug_info->is_hs_configured = pmadapter->is_hs_configured;
		debug_info->hs_activated = pmadapter->hs_activated;
		debug_info->pps_uapsd_mode = pmadapter->pps_uapsd_mode;
		debug_info->sleep_pd = pmadapter->sleep_period.period;
		debug_info->qos_cfg = pmpriv->wmm_qosinfo;
		debug_info->tx_lock_flag = pmadapter->tx_lock_flag;
		debug_info->port_open = pmpriv->port_open;
		debug_info->bypass_pkt_count = pmadapter->bypass_pkt_count;
		debug_info->scan_processing = pmadapter->scan_processing;
		debug_info->mlan_processing = pmadapter->mlan_processing;
		debug_info->main_lock_flag = pmadapter->main_lock_flag;
		debug_info->main_process_cnt = pmadapter->main_process_cnt;
		debug_info->delay_task_flag = pmadapter->delay_task_flag;
		debug_info->num_cmd_host_to_card_failure =
			pmadapter->dbg.num_cmd_host_to_card_failure;
		debug_info->num_cmd_sleep_cfm_host_to_card_failure =
			pmadapter->dbg.num_cmd_sleep_cfm_host_to_card_failure;
		debug_info->num_tx_host_to_card_failure =
			pmadapter->dbg.num_tx_host_to_card_failure;
		debug_info->num_alloc_buffer_failure =
			pmadapter->dbg.num_alloc_buffer_failure;
		debug_info->num_pkt_dropped = pmadapter->dbg.num_pkt_dropped;

		debug_info->num_event_deauth = pmadapter->dbg.num_event_deauth;
		debug_info->num_event_disassoc =
			pmadapter->dbg.num_event_disassoc;
		debug_info->num_event_link_lost =
			pmadapter->dbg.num_event_link_lost;
		debug_info->num_cmd_deauth = pmadapter->dbg.num_cmd_deauth;
		debug_info->num_cmd_assoc_success =
			pmadapter->dbg.num_cmd_assoc_success;
		debug_info->num_cmd_assoc_failure =
			pmadapter->dbg.num_cmd_assoc_failure;
		debug_info->num_cmd_timeout = pmadapter->num_cmd_timeout;
		debug_info->timeout_cmd_id = pmadapter->dbg.timeout_cmd_id;
		debug_info->timeout_cmd_act = pmadapter->dbg.timeout_cmd_act;
		memcpy_ext(pmadapter, debug_info->last_cmd_id,
			   pmadapter->dbg.last_cmd_id,
			   sizeof(pmadapter->dbg.last_cmd_id),
			   sizeof(debug_info->last_cmd_id));
		memcpy_ext(pmadapter, debug_info->last_cmd_act,
			   pmadapter->dbg.last_cmd_act,
			   sizeof(pmadapter->dbg.last_cmd_act),
			   sizeof(debug_info->last_cmd_act));
		debug_info->last_cmd_index = pmadapter->dbg.last_cmd_index;
		memcpy_ext(pmadapter, debug_info->last_cmd_resp_id,
			   pmadapter->dbg.last_cmd_resp_id,
			   sizeof(pmadapter->dbg.last_cmd_resp_id),
			   sizeof(debug_info->last_cmd_resp_id));
		debug_info->last_cmd_resp_index =
			pmadapter->dbg.last_cmd_resp_index;
		memcpy_ext(pmadapter, debug_info->last_event,
			   pmadapter->dbg.last_event,
			   sizeof(pmadapter->dbg.last_event),
			   sizeof(debug_info->last_event));
		debug_info->last_event_index = pmadapter->dbg.last_event_index;
		debug_info->num_no_cmd_node = pmadapter->dbg.num_no_cmd_node;
		debug_info->pending_cmd =
			(pmadapter->curr_cmd) ?
			pmadapter->dbg.last_cmd_id
			[pmadapter->dbg.last_cmd_index] : 0;
		debug_info->dnld_cmd_in_secs = pmadapter->dnld_cmd_in_secs;
#ifdef SDIO
		if (IS_SD(pmadapter->card_type)) {
			debug_info->num_cmdevt_card_to_host_failure =
				pmadapter->dbg.num_cmdevt_card_to_host_failure;
			debug_info->num_rx_card_to_host_failure =
				pmadapter->dbg.num_rx_card_to_host_failure;
			debug_info->num_int_read_failure =
				pmadapter->dbg.num_int_read_failure;
			debug_info->last_int_status =
				pmadapter->dbg.last_int_status;
			debug_info->mp_rd_bitmap =
				pmadapter->pcard_sd->mp_rd_bitmap;
			debug_info->mp_wr_bitmap =
				pmadapter->pcard_sd->mp_wr_bitmap;
			debug_info->curr_rd_port =
				pmadapter->pcard_sd->curr_rd_port;
			debug_info->curr_wr_port =
				pmadapter->pcard_sd->curr_wr_port;
			debug_info->mp_invalid_update =
				pmadapter->pcard_sd->mp_invalid_update;
			debug_info->num_of_irq =
				pmadapter->pcard_sd->num_of_irq;
			memcpy_ext(pmadapter, debug_info->mp_update,
				   pmadapter->pcard_sd->mp_update,
				   sizeof(pmadapter->pcard_sd->mp_update),
				   sizeof(debug_info->mp_update));
			memcpy_ext(pmadapter, debug_info->mpa_tx_count,
				   pmadapter->pcard_sd->mpa_tx_count,
				   sizeof(pmadapter->pcard_sd->mpa_tx_count),
				   sizeof(debug_info->mpa_tx_count));
			debug_info->mpa_sent_last_pkt =
				pmadapter->pcard_sd->mpa_sent_last_pkt;
			debug_info->mpa_sent_no_ports =
				pmadapter->pcard_sd->mpa_sent_no_ports;
			debug_info->last_recv_wr_bitmap =
				pmadapter->pcard_sd->last_recv_wr_bitmap;
			debug_info->last_recv_rd_bitmap =
				pmadapter->pcard_sd->last_recv_rd_bitmap;
			debug_info->mp_data_port_mask =
				pmadapter->pcard_sd->mp_data_port_mask;
			debug_info->last_mp_index =
				pmadapter->pcard_sd->last_mp_index;
			memcpy_ext(pmadapter, debug_info->last_mp_wr_bitmap,
				   pmadapter->pcard_sd->last_mp_wr_bitmap,
				   sizeof(pmadapter->pcard_sd->
					  last_mp_wr_bitmap),
				   sizeof(debug_info->last_mp_wr_bitmap));
			memcpy_ext(pmadapter, debug_info->last_mp_wr_ports,
				   pmadapter->pcard_sd->last_mp_wr_ports,
				   sizeof(pmadapter->pcard_sd->
					  last_mp_wr_ports),
				   sizeof(debug_info->last_mp_wr_ports));
			memcpy_ext(pmadapter, debug_info->last_mp_wr_len,
				   pmadapter->pcard_sd->last_mp_wr_len,
				   sizeof(pmadapter->pcard_sd->last_mp_wr_len),
				   sizeof(debug_info->last_mp_wr_len));
			memcpy_ext(pmadapter, debug_info->last_mp_wr_info,
				   pmadapter->pcard_sd->last_mp_wr_info,
				   sizeof(pmadapter->pcard_sd->last_mp_wr_info),
				   sizeof(debug_info->last_mp_wr_info));
			memcpy_ext(pmadapter, debug_info->last_curr_wr_port,
				   pmadapter->pcard_sd->last_curr_wr_port,
				   sizeof(pmadapter->pcard_sd->
					  last_curr_wr_port),
				   sizeof(debug_info->last_curr_wr_port));
			debug_info->mpa_buf = pmadapter->pcard_sd->mpa_buf;
			debug_info->mpa_buf_size =
				pmadapter->pcard_sd->mpa_buf_size;
			debug_info->sdio_rx_aggr =
				pmadapter->pcard_sd->sdio_rx_aggr_enable;
			memcpy_ext(pmadapter, debug_info->mpa_rx_count,
				   pmadapter->pcard_sd->mpa_rx_count,
				   sizeof(pmadapter->pcard_sd->mpa_rx_count),
				   sizeof(debug_info->mpa_rx_count));
			debug_info->mp_aggr_pkt_limit =
				pmadapter->pcard_sd->mp_aggr_pkt_limit;
		}
#endif
#ifdef PCIE
		if (IS_PCIE(pmadapter->card_type)) {
			debug_info->txbd_rdptr =
				pmadapter->pcard_pcie->txbd_rdptr;
			debug_info->txbd_wrptr =
				pmadapter->pcard_pcie->txbd_wrptr;
			debug_info->rxbd_rdptr =
				pmadapter->pcard_pcie->rxbd_rdptr;
			debug_info->rxbd_wrptr =
				pmadapter->pcard_pcie->rxbd_wrptr;
			debug_info->eventbd_rdptr =
				pmadapter->pcard_pcie->evtbd_rdptr;
			debug_info->eventbd_wrptr =
				pmadapter->pcard_pcie->evtbd_wrptr;
			debug_info->txbd_ring_vbase =
				pmadapter->pcard_pcie->txbd_ring_vbase;
			debug_info->txbd_ring_size =
				pmadapter->pcard_pcie->txbd_ring_size;
			debug_info->rxbd_ring_vbase =
				pmadapter->pcard_pcie->rxbd_ring_vbase;
			debug_info->rxbd_ring_size =
				pmadapter->pcard_pcie->rxbd_ring_size;
			debug_info->evtbd_ring_vbase =
				pmadapter->pcard_pcie->evtbd_ring_vbase;
			debug_info->evtbd_ring_size =
				pmadapter->pcard_pcie->evtbd_ring_size;
			debug_info->txrx_bd_size =
				pmadapter->pcard_pcie->txrx_bd_size;
		}
#endif
		debug_info->data_sent = pmadapter->data_sent;
		debug_info->data_sent_cnt = pmadapter->data_sent_cnt;
		debug_info->cmd_sent = pmadapter->cmd_sent;
		debug_info->cmd_resp_received = pmadapter->cmd_resp_received;
		debug_info->tx_pkts_queued =
			util_scalar_read(pmadapter->pmoal_handle,
					 &pmpriv->wmm.tx_pkts_queued, MNULL,
					 MNULL);
#ifdef UAP_SUPPORT
		debug_info->num_bridge_pkts =
			util_scalar_read(pmadapter->pmoal_handle,
					 &pmadapter->pending_bridge_pkts,
					 pmadapter->callbacks.moal_spin_lock,
					 pmadapter->callbacks.moal_spin_unlock);
		debug_info->num_drop_pkts = pmpriv->num_drop_pkts;
#endif
		debug_info->fw_hang_report = pmadapter->fw_hang_report;
		debug_info->mlan_processing = pmadapter->mlan_processing;
		debug_info->mlan_rx_processing = pmadapter->mlan_rx_processing;
		debug_info->rx_pkts_queued = pmadapter->rx_pkts_queued;
		debug_info->mlan_adapter = pmadapter;
		debug_info->mlan_adapter_size = sizeof(mlan_adapter);
		debug_info->mlan_priv_num = pmadapter->priv_num;
		for (i = 0; i < pmadapter->priv_num; i++) {
			debug_info->mlan_priv[i] = pmadapter->priv[i];
			debug_info->mlan_priv_size[i] = sizeof(mlan_private);
		}
	}

	pioctl_req->data_read_written =
		sizeof(mlan_debug_info) + MLAN_SUB_COMMAND_SIZE;

	LEAVE();
	return ret;
}

/**
 *  @brief Set/Get the MAC control configuration.
 *
 *  @param pmadapter    A pointer to mlan_adapter structure
 *  @param pioctl_req   A pointer to ioctl request buffer
 *
 *  @return             MLAN_STATUS_PENDING -- success, otherwise fail
 */
mlan_status
wlan_misc_ioctl_mac_control(pmlan_adapter pmadapter, pmlan_ioctl_req pioctl_req)
{
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_ds_misc_cfg *misc = (mlan_ds_misc_cfg *)pioctl_req->pbuf;
	mlan_status ret = MLAN_STATUS_SUCCESS;
	t_u16 cmd_action = 0;

	ENTER();

	misc = (mlan_ds_misc_cfg *)pioctl_req->pbuf;

	if (pioctl_req->action == MLAN_ACT_GET) {
		misc->param.mac_ctrl = pmpriv->curr_pkt_filter;
	} else {
		pmpriv->curr_pkt_filter = misc->param.mac_ctrl;
		cmd_action = HostCmd_ACT_GEN_SET;

		/* Send command to firmware */
		ret = wlan_prepare_cmd(pmpriv, HostCmd_CMD_MAC_CONTROL,
				       cmd_action, 0, (t_void *)pioctl_req,
				       &misc->param.mac_ctrl);

		if (ret == MLAN_STATUS_SUCCESS)
			ret = MLAN_STATUS_PENDING;
	}

	LEAVE();
	return ret;
}

/**
 *  @brief This timer function handles wakeup card timeout.
 *
 *  @param function_context   A pointer to function_context
 *  @return        N/A
 */
t_void
wlan_wakeup_card_timeout_func(void *function_context)
{
	pmlan_adapter pmadapter = (pmlan_adapter)function_context;
	mlan_private *pmpriv = wlan_get_priv(pmadapter, MLAN_BSS_ROLE_ANY);

	ENTER();

	PRINTM(MERROR, "%s: ps_state=%d\n", __FUNCTION__, pmadapter->ps_state);
	if (pmadapter->ps_state != PS_STATE_AWAKE) {
		PRINTM_NETINTF(MERROR, pmpriv);
		PRINTM(MERROR, "Wakeup card timeout!\n");
		pmadapter->pm_wakeup_timeout++;
		wlan_recv_event(pmpriv, MLAN_EVENT_ID_DRV_DBG_DUMP, MNULL);
	}
	pmadapter->wakeup_fw_timer_is_set = MFALSE;

	LEAVE();
}

/**
 *  @brief Set/Get HS configuration
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_SUCCESS/MLAN_STATUS_PENDING --success,
 * otherwise fail
 */
mlan_status
wlan_pm_ioctl_hscfg(pmlan_adapter pmadapter, pmlan_ioctl_req pioctl_req)
{
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_ds_pm_cfg *pm = MNULL;
	mlan_status status = MLAN_STATUS_SUCCESS;
	t_u32 prev_cond = 0;

	ENTER();

	pm = (mlan_ds_pm_cfg *)pioctl_req->pbuf;

	switch (pioctl_req->action) {
	case MLAN_ACT_SET:
#ifdef STA_SUPPORT
		if (pmadapter->pps_uapsd_mode) {
			PRINTM(MINFO,
			       "Host Sleep IOCTL is blocked in UAPSD/PPS mode\n");
			pioctl_req->status_code = MLAN_ERROR_IOCTL_INVALID;
			status = MLAN_STATUS_FAILURE;
			break;
		}
#endif /* STA_SUPPORT */
		if (pm->param.hs_cfg.is_invoke_hostcmd == MTRUE) {
			if (pm->param.hs_cfg.conditions ==
			    HOST_SLEEP_CFG_CANCEL) {
				if (pmadapter->is_hs_configured == MFALSE) {
					/* Already cancelled */
					break;
				}
				/* Save previous condition */
				prev_cond = pmadapter->hs_cfg.conditions;
				pmadapter->hs_cfg.conditions =
					pm->param.hs_cfg.conditions;
			} else if (pmadapter->hs_cfg.conditions ==
				   HOST_SLEEP_CFG_CANCEL) {
				/* Return failure if no parameters for HS enable
				 */
				pioctl_req->status_code =
					MLAN_ERROR_INVALID_PARAMETER;
				status = MLAN_STATUS_FAILURE;
				break;
			}
			status = wlan_prepare_cmd(pmpriv,
						  HostCmd_CMD_802_11_HS_CFG_ENH,
						  HostCmd_ACT_GEN_SET, 0,
						  (t_void *)pioctl_req,
						  (t_void *)(&pmadapter->
							     hs_cfg));
			if (status == MLAN_STATUS_SUCCESS)
				status = MLAN_STATUS_PENDING;
			if (pm->param.hs_cfg.conditions ==
			    HOST_SLEEP_CFG_CANCEL) {
				/* Restore previous condition */
				pmadapter->hs_cfg.conditions = prev_cond;
			}
		} else {
			pmadapter->hs_cfg.conditions =
				pm->param.hs_cfg.conditions;
			pmadapter->hs_cfg.gpio = (t_u8)pm->param.hs_cfg.gpio;
			pmadapter->hs_cfg.gap = (t_u8)pm->param.hs_cfg.gap;
			pmadapter->param_type_ind =
				(t_u8)pm->param.hs_cfg.param_type_ind;
			pmadapter->ind_gpio = (t_u8)pm->param.hs_cfg.ind_gpio;
			pmadapter->level = (t_u8)pm->param.hs_cfg.level;
			pmadapter->param_type_ext =
				(t_u8)pm->param.hs_cfg.param_type_ext;
			pmadapter->event_force_ignore =
				pm->param.hs_cfg.event_force_ignore;
			pmadapter->event_use_ext_gap =
				pm->param.hs_cfg.event_use_ext_gap;
			pmadapter->ext_gap = pm->param.hs_cfg.ext_gap;
			pmadapter->gpio_wave = pm->param.hs_cfg.gpio_wave;
			pmadapter->hs_wake_interval =
				pm->param.hs_cfg.hs_wake_interval;
		}
		break;
	case MLAN_ACT_GET:
		pm->param.hs_cfg.conditions = pmadapter->hs_cfg.conditions;
		pm->param.hs_cfg.gpio = pmadapter->hs_cfg.gpio;
		pm->param.hs_cfg.gap = pmadapter->hs_cfg.gap;
		pm->param.hs_cfg.param_type_ind = pmadapter->param_type_ind;
		pm->param.hs_cfg.ind_gpio = pmadapter->ind_gpio;
		pm->param.hs_cfg.level = pmadapter->level;
		pm->param.hs_cfg.param_type_ext = pmadapter->param_type_ext;
		pm->param.hs_cfg.event_force_ignore =
			pmadapter->event_force_ignore;
		pm->param.hs_cfg.event_use_ext_gap =
			pmadapter->event_use_ext_gap;
		pm->param.hs_cfg.ext_gap = pmadapter->ext_gap;
		pm->param.hs_cfg.gpio_wave = pmadapter->gpio_wave;
		pm->param.hs_cfg.hs_wake_interval = pmadapter->hs_wake_interval;
		break;
	default:
		pioctl_req->status_code = MLAN_ERROR_IOCTL_INVALID;
		status = MLAN_STATUS_FAILURE;
		break;
	}

	LEAVE();
	return status;
}

/**
 *  @brief Set Robustcoex gpiocfg
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_SUCCESS/MLAN_STATUS_PENDING --success,
 * otherwise fail
 */
mlan_status
wlan_misc_robustcoex(pmlan_adapter pmadapter, pmlan_ioctl_req pioctl_req)
{
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_status ret = MLAN_STATUS_SUCCESS;
	t_u16 cmd_action;
	mlan_ds_misc_cfg *robust_coex_cfg =
		(mlan_ds_misc_cfg *)pioctl_req->pbuf;

	ENTER();

	if (pioctl_req->action == MLAN_ACT_SET)
		cmd_action = HostCmd_ACT_GEN_SET;
	else
		cmd_action = HostCmd_ACT_GEN_GET;

	ret = wlan_prepare_cmd(pmpriv, HostCmd_CMD_802_11_ROBUSTCOEX,
			       cmd_action, 0, (t_void *)pioctl_req,
			       &robust_coex_cfg->param.robustcoexparams);

	if (ret == MLAN_STATUS_SUCCESS)
		ret = MLAN_STATUS_PENDING;
	LEAVE();
	return ret;
}

/**
 *  @brief Set/get DMCS config
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return MLAN_STATUS_SUCCESS/MLAN_STATUS_PENDING --success, otherwise fail
 */
mlan_status
wlan_misc_dmcs_config(pmlan_adapter pmadapter, pmlan_ioctl_req pioctl_req)
{
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_status ret = MLAN_STATUS_SUCCESS;
	t_u16 cmd_action;
	mlan_ds_misc_cfg *dmcs_cfg = (mlan_ds_misc_cfg *)pioctl_req->pbuf;

	ENTER();

	if (pioctl_req->action == MLAN_ACT_SET)
		cmd_action = HostCmd_ACT_GEN_SET;
	else
		cmd_action = HostCmd_ACT_GEN_GET;

	ret = wlan_prepare_cmd(pmpriv, HostCmd_CMD_DMCS_CONFIG, cmd_action, 0,
			       (t_void *)pioctl_req,
			       &dmcs_cfg->param.dmcs_policy);

	if (ret == MLAN_STATUS_SUCCESS)
		ret = MLAN_STATUS_PENDING;
	LEAVE();
	return ret;
}

#if defined(PCIE)
/**
 *  @brief Enable SSU support
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_SUCCESS/MLAN_STATUS_PENDING --success,
 * otherwise fail
 */
mlan_status
wlan_misc_ssu(pmlan_adapter pmadapter, pmlan_ioctl_req pioctl_req)
{
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_status ret = MLAN_STATUS_SUCCESS;
	t_u16 cmd_action = HostCmd_ACT_GEN_GET;
	mlan_ds_misc_cfg *ssu_cfg = (mlan_ds_misc_cfg *)pioctl_req->pbuf;

	ENTER();

	if (pioctl_req->action == MLAN_ACT_SET)
		cmd_action = HostCmd_ACT_GEN_SET;
	else if (pioctl_req->action == MLAN_ACT_DEFAULT)
		cmd_action = HostCmd_ACT_GEN_SET_DEFAULT;
	else if (pioctl_req->action == MLAN_ACT_GET)
		cmd_action = HostCmd_ACT_GEN_GET;

	ret = wlan_prepare_cmd(pmpriv, HostCmd_CMD_SSU, cmd_action, 0,
			       (t_void *)pioctl_req,
			       &ssu_cfg->param.ssu_params);

	if (ret == MLAN_STATUS_SUCCESS)
		ret = MLAN_STATUS_PENDING;
	LEAVE();
	return ret;
}
#endif

/**
 *  @brief Set the hal/phy cfg params.
 *
 *  @param pmadapter    A pointer to mlan_adapter structure
 *  @param pioctl_req   A pointer to ioctl request buffer
 *
 *  @return             MLAN_STATUS_SUCCESS/MLAN_STATUS_PENDING --success,
 * otherwise fail
 */
mlan_status
wlan_misc_hal_phy_cfg(pmlan_adapter pmadapter, pmlan_ioctl_req pioctl_req)
{
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_status ret = MLAN_STATUS_SUCCESS;
	mlan_ds_misc_cfg *hal_phy_cfg = (mlan_ds_misc_cfg *)pioctl_req->pbuf;
	t_u16 cmd_act;

	ENTER();

	cmd_act = HostCmd_ACT_GEN_SET;
	ret = wlan_prepare_cmd(pmpriv, HostCmd_CMD_HAL_PHY_CFG, cmd_act, 0,
			       (t_void *)pioctl_req,
			       &hal_phy_cfg->param.hal_phy_cfg_params);

	if (ret == MLAN_STATUS_SUCCESS)
		ret = MLAN_STATUS_PENDING;

	LEAVE();
	return ret;
}

/**
 *  @brief This function allocates a mlan_buffer.
 *
 *  @param pmadapter Pointer to mlan_adapter
 *  @param data_len   Data length
 *  @param head_room  head_room reserved in mlan_buffer
 *  @param malloc_flag  flag to user moal_malloc
 *  @return           mlan_buffer pointer or MNULL
 */
pmlan_buffer
wlan_alloc_mlan_buffer(mlan_adapter *pmadapter, t_u32 data_len,
		       t_u32 head_room, t_u32 malloc_flag)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	pmlan_buffer pmbuf = MNULL;
	t_u32 buf_size = 0;
	t_u8 *tmp_buf = MNULL;
	pmlan_callbacks pcb = &pmadapter->callbacks;

	ENTER();

#ifdef SDIO
	/* make sure that the data length is at least SDIO block size */
	if (IS_SD(pmadapter->card_type))
		data_len = (data_len + MLAN_SDIO_BLOCK_SIZE - 1) /
			MLAN_SDIO_BLOCK_SIZE * MLAN_SDIO_BLOCK_SIZE;
#endif

	/* head_room is not implemented for malloc mlan buffer */

	switch (malloc_flag) {
	case MOAL_MALLOC_BUFFER:
		buf_size = sizeof(mlan_buffer) + data_len + DMA_ALIGNMENT;
		ret = pcb->moal_malloc(pmadapter->pmoal_handle, buf_size,
				       MLAN_MEM_DEF | MLAN_MEM_DMA,
				       (t_u8 **)&pmbuf);
		if ((ret != MLAN_STATUS_SUCCESS) || !pmbuf) {
			pmbuf = MNULL;
			goto exit;
		}
		memset(pmadapter, pmbuf, 0, sizeof(mlan_buffer));

		pmbuf->pdesc = MNULL;
		/* Align address */
		pmbuf->pbuf = (t_u8 *)ALIGN_ADDR((t_u8 *)pmbuf +
						 sizeof(mlan_buffer),
						 DMA_ALIGNMENT);
		pmbuf->data_offset = 0;
		pmbuf->data_len = data_len;
		pmbuf->flags |= MLAN_BUF_FLAG_MALLOC_BUF;
		break;

	case MOAL_ALLOC_MLAN_BUFFER:
		/* use moal_alloc_mlan_buffer, head_room supported */
		ret = pcb->moal_alloc_mlan_buffer(pmadapter->pmoal_handle,
						  data_len + DMA_ALIGNMENT +
						  head_room, &pmbuf);
		if ((ret != MLAN_STATUS_SUCCESS) || !pmbuf) {
			PRINTM(MERROR, "Failed to allocate 'mlan_buffer'\n");
			goto exit;
		}
		pmbuf->data_offset = head_room;
		tmp_buf = (t_u8 *)ALIGN_ADDR(pmbuf->pbuf + pmbuf->data_offset,
					     DMA_ALIGNMENT);
		pmbuf->data_offset +=
			(t_u32)(tmp_buf - (pmbuf->pbuf + pmbuf->data_offset));
		pmbuf->data_len = data_len;
		pmbuf->flags = 0;
		break;
	}

exit:
	LEAVE();
	return pmbuf;
}

/**
 *  @brief This function frees a mlan_buffer.
 *
 *  @param pmadapter  Pointer to mlan_adapter
 *  @param pmbuf      Pointer to mlan_buffer
 *
 *  @return           N/A
 */
t_void
wlan_free_mlan_buffer(mlan_adapter *pmadapter, pmlan_buffer pmbuf)
{
	pmlan_callbacks pcb = &pmadapter->callbacks;
	ENTER();

	if (pcb && pmbuf) {
		if (pmbuf->flags & MLAN_BUF_FLAG_BRIDGE_BUF)
			util_scalar_decrement(pmadapter->pmoal_handle,
					      &pmadapter->pending_bridge_pkts,
					      pmadapter->callbacks.
					      moal_spin_lock,
					      pmadapter->callbacks.
					      moal_spin_unlock);
		if (pmbuf->flags & MLAN_BUF_FLAG_MALLOC_BUF)
			pcb->moal_mfree(pmadapter->pmoal_handle, (t_u8 *)pmbuf);
		else
			pcb->moal_free_mlan_buffer(pmadapter->pmoal_handle,
						   pmbuf);
	}

	LEAVE();
	return;
}

/**
 *  @brief Delay function implementation
 *
 *  @param pmadapter        A pointer to mlan_adapter structure
 *  @param delay            Delay value
 *  @param u                Units of delay (sec, msec or usec)
 *
 *  @return                 N/A
 */
t_void
wlan_delay_func(mlan_adapter *pmadapter, t_u32 delay, t_delay_unit u)
{
	t_u32 now_tv_sec, now_tv_usec;
	t_u32 upto_tv_sec, upto_tv_usec;
	pmlan_callbacks pcb = &pmadapter->callbacks;

	ENTER();

	if (pcb->moal_udelay) {
		if (u == SEC)
			delay *= 1000000;
		else if (u == MSEC)
			delay *= 1000;
		pcb->moal_udelay(pmadapter->pmoal_handle, delay);
	} else {
		pcb->moal_get_system_time(pmadapter->pmoal_handle, &upto_tv_sec,
					  &upto_tv_usec);

		switch (u) {
		case SEC:
			upto_tv_sec += delay;
			break;
		case MSEC:
			delay *= 1000;
			/* fall through */
		case USEC:
			upto_tv_sec += (delay / 1000000);
			upto_tv_usec += (delay % 1000000);
			break;
		}

		do {
			pcb->moal_get_system_time(pmadapter->pmoal_handle,
						  &now_tv_sec, &now_tv_usec);
			if (now_tv_sec > upto_tv_sec) {
				LEAVE();
				return;
			}

			if ((now_tv_sec == upto_tv_sec) &&
			    (now_tv_usec >= upto_tv_usec)) {
				LEAVE();
				return;
			}
		} while (MTRUE);
	}

	LEAVE();
	return;
}

/**
 *  @brief BSS remove
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_PENDING --success, MLAN_STATUS_FAILURE
 */
mlan_status
wlan_bss_ioctl_bss_remove(pmlan_adapter pmadapter, pmlan_ioctl_req pioctl_req)
{
	ENTER();
	wlan_cancel_bss_pending_cmd(pmadapter, pioctl_req->bss_index);
	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

#if defined(STA_SUPPORT) && defined(UAP_SUPPORT)
/**
 *  @brief Set/Get BSS role
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_PENDING --success, MLAN_STATUS_FAILURE
 */
mlan_status
wlan_bss_ioctl_bss_role(pmlan_adapter pmadapter, pmlan_ioctl_req pioctl_req)
{
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_ds_bss *bss = MNULL;
	mlan_status ret = MLAN_STATUS_SUCCESS;
	HostCmd_DS_VERSION_EXT dummy;
#ifdef USB
	pmlan_callbacks pcb = &pmadapter->callbacks;
	pmlan_buffer pmbuf;
#endif
#if defined(WIFI_DIRECT_SUPPORT)
	t_u8 bss_mode;
#endif
	t_u8 i, global_band = 0;
	int j;

	ENTER();

	bss = (mlan_ds_bss *)pioctl_req->pbuf;
	if (pioctl_req->action == MLAN_ACT_GET) {
		bss->param.bss_role = GET_BSS_ROLE(pmpriv);
	} else {
		if (GET_BSS_ROLE(pmpriv) == bss->param.bss_role) {
			PRINTM(MIOCTL, "BSS ie already in the desired role!\n");
			goto done;
		}
		mlan_block_rx_process(pmadapter, MTRUE);
		/** Switch BSS role */
		wlan_free_priv(pmpriv);

#ifdef USB
		if (IS_USB(pmadapter->card_type)) {
			while ((pmbuf =
				(pmlan_buffer)util_dequeue_list(pmadapter->
								pmoal_handle,
								&pmadapter->
								rx_data_queue,
								pcb->
								moal_spin_lock,
								pcb->
								moal_spin_unlock)))
			{
				pcb->moal_recv_complete(pmadapter->pmoal_handle,
							pmbuf,
							pmadapter->rx_data_ep,
							MLAN_STATUS_FAILURE);
			}
		}
#endif
		pmpriv->bss_role = bss->param.bss_role;
		if (pmpriv->bss_type == MLAN_BSS_TYPE_UAP)
			pmpriv->bss_type = MLAN_BSS_TYPE_STA;
		else if (pmpriv->bss_type == MLAN_BSS_TYPE_STA)
			pmpriv->bss_type = MLAN_BSS_TYPE_UAP;
		/* Initialize private structures */
		wlan_init_priv(pmpriv);
		mlan_block_rx_process(pmadapter, MFALSE);
		/* Initialize function table */
		for (j = 0; mlan_ops[j]; j++) {
			if (mlan_ops[j]->bss_role == GET_BSS_ROLE(pmpriv)) {
				memcpy_ext(pmadapter, &pmpriv->ops, mlan_ops[j],
					   sizeof(mlan_operations),
					   sizeof(mlan_operations));
			}
		}

		for (i = 0; i < pmadapter->priv_num; i++) {
			if (pmadapter->priv[i] &&
			    GET_BSS_ROLE(pmadapter->priv[i]) ==
			    MLAN_BSS_ROLE_STA)
				global_band |= pmadapter->priv[i]->config_bands;
		}

		if (global_band != pmadapter->config_bands) {
			if (wlan_set_regiontable
			    (pmpriv, (t_u8)pmadapter->region_code,
			     global_band | pmadapter->adhoc_start_band)) {
				pioctl_req->status_code = MLAN_ERROR_IOCTL_FAIL;
				LEAVE();
				return MLAN_STATUS_FAILURE;
			}

			if (wlan_11d_set_universaltable(pmpriv,
							global_band |
							pmadapter->
							adhoc_start_band)) {
				pioctl_req->status_code = MLAN_ERROR_IOCTL_FAIL;
				LEAVE();
				return MLAN_STATUS_FAILURE;
			}
			pmadapter->config_bands = global_band;
		}

		/* Issue commands to initialize firmware */
#if defined(WIFI_DIRECT_SUPPORT)
		if (GET_BSS_ROLE(pmpriv) == MLAN_BSS_ROLE_STA)
			bss_mode = BSS_MODE_WIFIDIRECT_CLIENT;
		else
			bss_mode = BSS_MODE_WIFIDIRECT_GO;
		ret = wlan_prepare_cmd(pmpriv, HostCmd_CMD_SET_BSS_MODE,
				       HostCmd_ACT_GEN_SET, 0, MNULL,
				       &bss_mode);
		if (ret)
			goto done;
#endif
		ret = pmpriv->ops.init_cmd(pmpriv, MFALSE);
		if (ret == MLAN_STATUS_FAILURE)
			goto done;

		/* Issue dummy Get command to complete the ioctl */
		memset(pmadapter, &dummy, 0, sizeof(HostCmd_DS_VERSION_EXT));
		ret = wlan_prepare_cmd(pmpriv, HostCmd_CMD_VERSION_EXT,
				       HostCmd_ACT_GEN_GET, 0,
				       (t_void *)pioctl_req, (t_void *)&dummy);
		if (ret == MLAN_STATUS_SUCCESS)
			ret = MLAN_STATUS_PENDING;
	}

done:
	LEAVE();
	return ret;
}
#endif

/**
 *  @brief Set the custom IE
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *  @param send_ioctl	Flag to indicate if ioctl should be sent with cmd
 *                      (MTRUE if from moal/user, MFALSE if internal)
 *
 *  @return		MLAN_STATUS_PENDING --success, otherwise fail
 */
mlan_status
wlan_misc_ioctl_custom_ie_list(pmlan_adapter pmadapter,
			       pmlan_ioctl_req pioctl_req, t_bool send_ioctl)
{
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_status ret = MLAN_STATUS_SUCCESS;
	mlan_ds_misc_cfg *misc = (mlan_ds_misc_cfg *)pioctl_req->pbuf;
	custom_ie *ie_data = MNULL;
	t_u16 cmd_action = 0, index, mask, i, len, app_data_len;
	t_s32 ioctl_len;
	t_u8 *tmp_ie;

	ENTER();

	if ((misc->param.cust_ie.len == 0) ||
	    (misc->param.cust_ie.len == sizeof(t_u16))) {
		pioctl_req->action = MLAN_ACT_GET;
		/* Get the IE */
		cmd_action = HostCmd_ACT_GEN_GET;
	} else {
		/* ioctl_len : ioctl length from application, start with
		 * misc->param.cust_ie.len and reach upto 0 */
		ioctl_len = misc->param.cust_ie.len;

		/* app_data_len : length from application, start with 0
		 * and reach upto ioctl_len */
		app_data_len = sizeof(MrvlIEtypesHeader_t);
		misc->param.cust_ie.len = 0;

		while (ioctl_len > 0) {
			ie_data = (custom_ie *)(((t_u8 *)&misc->param.cust_ie) +
						app_data_len);
			ioctl_len -=
				(ie_data->ie_length + MLAN_CUSTOM_IE_HDR_SIZE);
			app_data_len +=
				(ie_data->ie_length + MLAN_CUSTOM_IE_HDR_SIZE);

			index = ie_data->ie_index;
			mask = ie_data->mgmt_subtype_mask;

			/* Need to be Autohandled */
			if (MLAN_CUSTOM_IE_AUTO_IDX_MASK == index) {
				/* Automatic Deletion */
				if (mask == MLAN_CUSTOM_IE_DELETE_MASK) {
					ret = wlan_custom_ioctl_auto_delete
						(pmpriv, pioctl_req, ie_data,
						 index);
					/* if IE to delete is not found, return
					 * error */
					if (ret == MLAN_STATUS_FAILURE)
						goto done;
					index = ie_data->ie_index;
					memset(pmadapter, ie_data, 0,
					       sizeof(custom_ie) *
					       MAX_MGMT_IE_INDEX_TO_FW);
					len = 0;
					for (i = 0;
					     i < pmadapter->max_mgmt_ie_index;
					     i++) {
						/* Check if index is updated
						 * before sending to FW */
						if (index & ((t_u16)1) << i) {
							memcpy_ext(pmadapter,
								   (t_u8 *)
								   ie_data +
								   len, &i,
								   sizeof
								   (ie_data->
								    ie_index),
								   sizeof
								   (ie_data->
								    ie_index));
							len += sizeof(ie_data->
								      ie_index);
							memcpy_ext(pmadapter,
								   (t_u8 *)
								   ie_data +
								   len,
								   &pmpriv->
								   mgmt_ie[i]
								   .
								   mgmt_subtype_mask,
								   sizeof
								   (ie_data->
								    mgmt_subtype_mask),
								   sizeof
								   (ie_data->
								    mgmt_subtype_mask));
							len += sizeof(ie_data->
								      mgmt_subtype_mask);
							memcpy_ext(pmadapter,
								   (t_u8 *)
								   ie_data +
								   len,
								   &pmpriv->
								   mgmt_ie[i]
								   .ie_length,
								   sizeof
								   (ie_data->
								    ie_length),
								   sizeof
								   (ie_data->
								    ie_length));
							len += sizeof(ie_data->
								      ie_length);
							if (pmpriv->mgmt_ie[i]
							    .ie_length) {
								memcpy_ext
									(pmadapter,
									 (t_u8
									  *)
									 ie_data
									 + len,
									 &pmpriv->
									 mgmt_ie
									 [i]
									 .
									 ie_buffer,
									 pmpriv->
									 mgmt_ie
									 [i]
									 .
									 ie_length,
									 pmpriv->
									 mgmt_ie
									 [i]
									 .
									 ie_length);
								len += pmpriv->
									mgmt_ie
									[i]
									.
									ie_length;
							}
						}
					}
					misc->param.cust_ie.len += len;
					pioctl_req->action = MLAN_ACT_SET;
					cmd_action = HostCmd_ACT_GEN_SET;
				} else {	/* Automatic Addition */
					if (MLAN_STATUS_FAILURE ==
					    wlan_custom_ioctl_get_autoidx
					    (pmpriv, pioctl_req, mask, ie_data,
					     &index)) {
						PRINTM(MERROR,
						       "Failed to Set the IE buffer\n");
						ret = MLAN_STATUS_FAILURE;
						goto done;
					}
					mask &= ~MLAN_CUSTOM_IE_NEW_MASK;
					if (MLAN_CUSTOM_IE_AUTO_IDX_MASK ==
					    index ||
					    index >= MAX_MGMT_IE_INDEX) {
						ret = MLAN_STATUS_SUCCESS;
						goto done;
					}
					tmp_ie = (t_u8 *)&pmpriv->mgmt_ie[index]
						.ie_buffer;
					memcpy_ext(pmadapter,
						   tmp_ie +
						   pmpriv->mgmt_ie[index]
						   .ie_length,
						   &ie_data->ie_buffer,
						   ie_data->ie_length,
						   ie_data->ie_length);
					pmpriv->mgmt_ie[index].ie_length +=
						ie_data->ie_length;
					pmpriv->mgmt_ie[index].ie_index = index;
					pmpriv->mgmt_ie[index]
						.mgmt_subtype_mask = mask;

					pioctl_req->action = MLAN_ACT_SET;
					cmd_action = HostCmd_ACT_GEN_SET;
					ie_data->ie_index = index;
					ie_data->ie_length =
						pmpriv->mgmt_ie[index].
						ie_length;
					memcpy_ext(pmadapter,
						   &ie_data->ie_buffer,
						   &pmpriv->mgmt_ie[index]
						   .ie_buffer,
						   pmpriv->mgmt_ie[index].
						   ie_length, MAX_IE_SIZE);
					misc->param.cust_ie.len +=
						pmpriv->mgmt_ie[index]
						.ie_length +
						MLAN_CUSTOM_IE_HDR_SIZE;
				}
			} else {
				if (index >= pmadapter->max_mgmt_ie_index ||
				    index >= MAX_MGMT_IE_INDEX) {
					PRINTM(MERROR,
					       "Invalid custom IE index %d\n",
					       index);
					ret = MLAN_STATUS_FAILURE;
					goto done;
				}
				/* Set/Clear the IE and save it */
				if (ie_data->mgmt_subtype_mask ==
				    MLAN_CUSTOM_IE_DELETE_MASK &&
				    ie_data->ie_length) {
					PRINTM(MINFO, "Clear the IE buffer\n");
					ret = wlan_custom_ioctl_auto_delete
						(pmpriv, pioctl_req, ie_data,
						 index);
					/* if IE to delete is not found, return
					 * error */
					if (ret == MLAN_STATUS_FAILURE)
						goto done;
					memset(pmadapter, ie_data, 0,
					       sizeof(custom_ie) *
					       MAX_MGMT_IE_INDEX_TO_FW);
					memcpy_ext(pmadapter, (t_u8 *)ie_data,
						   &pmpriv->mgmt_ie[index],
						   pmpriv->mgmt_ie[index].
						   ie_length +
						   MLAN_CUSTOM_IE_HDR_SIZE,
						   pmpriv->mgmt_ie[index].
						   ie_length +
						   MLAN_CUSTOM_IE_HDR_SIZE);
				} else {
					/*
					 * Check if this index is being used on
					 * any other interfaces. If yes, then
					 * the request needs to be rejected.
					 */
					ret = wlan_is_custom_ie_index_unused
						(pmpriv, index);
					if (ret == MLAN_STATUS_FAILURE) {
						PRINTM(MERROR,
						       "IE index is used by other interface.\n");
						PRINTM(MERROR,
						       "Set or delete on index %d is not allowed.\n",
						       index);
						pioctl_req->status_code =
							MLAN_ERROR_IOCTL_FAIL;
						goto done;
					}
					PRINTM(MINFO, "Set the IE buffer\n");
					if (ie_data->mgmt_subtype_mask ==
					    MLAN_CUSTOM_IE_DELETE_MASK)
						ie_data->ie_length = 0;
					else {
						if ((pmpriv->mgmt_ie[index]
						     .mgmt_subtype_mask ==
						     ie_data->mgmt_subtype_mask)
						    && (pmpriv->mgmt_ie[index]
							.ie_length ==
							ie_data->ie_length) &&
						    !memcmp(pmpriv->adapter,
							    pmpriv->
							    mgmt_ie[index]
							    .ie_buffer,
							    ie_data->ie_buffer,
							    ie_data->
							    ie_length)) {
							PRINTM(MIOCTL,
							       "same custom ie already configured!\n");
							if (ioctl_len <= 0 &&
							    misc->param.cust_ie.
							    len == 0) {
								goto done;
							} else {
								/* remove
								 * matching IE
								 * from app
								 * buffer */
								app_data_len -=
									ie_data->
									ie_length
									+
									MLAN_CUSTOM_IE_HDR_SIZE;
								memmove(pmadapter, (t_u8 *)ie_data, ie_data->ie_buffer + ie_data->ie_length, ioctl_len);
								continue;
							}
						}
					}
					memset(pmadapter,
					       &pmpriv->mgmt_ie[index], 0,
					       sizeof(custom_ie));
					memcpy_ext(pmadapter,
						   &pmpriv->mgmt_ie[index],
						   ie_data, sizeof(custom_ie),
						   sizeof(custom_ie));
				}

				misc->param.cust_ie.len +=
					pmpriv->mgmt_ie[index].ie_length +
					MLAN_CUSTOM_IE_HDR_SIZE;
				pioctl_req->action = MLAN_ACT_SET;
				cmd_action = HostCmd_ACT_GEN_SET;
			}
		}
	}

	/* Send command to firmware */
	if (GET_BSS_ROLE(pmpriv) == MLAN_BSS_ROLE_STA) {
		ret = wlan_prepare_cmd(pmpriv, HostCmd_CMD_MGMT_IE_LIST,
				       cmd_action, 0,
				       (send_ioctl) ? (t_void *)pioctl_req :
				       MNULL, &misc->param.cust_ie);
	}
#ifdef UAP_SUPPORT
	else if (GET_BSS_ROLE(pmpriv) == MLAN_BSS_ROLE_UAP) {
		ret = wlan_prepare_cmd(pmpriv, HOST_CMD_APCMD_SYS_CONFIGURE,
				       cmd_action, 0,
				       (send_ioctl) ? (t_void *)pioctl_req :
				       MNULL,
				       (send_ioctl) ? MNULL : &misc->param.
				       cust_ie);
	}
#endif
	if (ret == MLAN_STATUS_SUCCESS)
		ret = MLAN_STATUS_PENDING;
done:
	LEAVE();
	return ret;
}

/**
 *  @brief Read/write adapter register
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_PENDING --success, otherwise fail
 */
mlan_status
wlan_reg_mem_ioctl_reg_rw(pmlan_adapter pmadapter, pmlan_ioctl_req pioctl_req)
{
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_ds_reg_mem *reg_mem = MNULL;
	mlan_status ret = MLAN_STATUS_SUCCESS;
	t_u16 cmd_action = 0, cmd_no;

	ENTER();

	reg_mem = (mlan_ds_reg_mem *)pioctl_req->pbuf;
	if (pioctl_req->action == MLAN_ACT_GET)
		cmd_action = HostCmd_ACT_GEN_GET;
	else
		cmd_action = HostCmd_ACT_GEN_SET;

	switch (reg_mem->param.reg_rw.type) {
	case MLAN_REG_MAC:
#if defined(PCIE9098) || defined(SD9098) || defined(USB9098) || defined(PCIE9097) || defined(USB9097) || defined(SD9097)
	case MLAN_REG_MAC2:
#endif
		cmd_no = HostCmd_CMD_MAC_REG_ACCESS;
		break;
	case MLAN_REG_BBP:
#if defined(PCIE9098) || defined(SD9098) || defined(USB9098) || defined(PCIE9097) || defined(USB9097) || defined(SD9097)
	case MLAN_REG_BBP2:
#endif
		cmd_no = HostCmd_CMD_BBP_REG_ACCESS;
		break;
	case MLAN_REG_RF:
#if defined(PCIE9098) || defined(SD9098) || defined(USB9098) || defined(PCIE9097) || defined(USB9097) || defined(SD9097)
	case MLAN_REG_RF2:
#endif
		cmd_no = HostCmd_CMD_RF_REG_ACCESS;
		break;
	case MLAN_REG_CAU:
		cmd_no = HostCmd_CMD_CAU_REG_ACCESS;
		break;
	case MLAN_REG_PSU:
		cmd_no = HostCmd_CMD_TARGET_ACCESS;
		break;
	case MLAN_REG_BCA:
#if defined(PCIE9098) || defined(SD9098) || defined(USB9098) || defined(PCIE9097) || defined(USB9097) || defined(SD9097)
	case MLAN_REG_BCA2:
#endif
		cmd_no = HostCmd_CMD_BCA_REG_ACCESS;
		break;
	default:
		pioctl_req->status_code = MLAN_ERROR_IOCTL_INVALID;
		ret = MLAN_STATUS_FAILURE;
		goto exit;
	}

	/* Send request to firmware */
	ret = wlan_prepare_cmd(pmpriv, cmd_no, cmd_action, 0,
			       (t_void *)pioctl_req,
			       (t_void *)&reg_mem->param.reg_rw);

	if (ret == MLAN_STATUS_SUCCESS)
		ret = MLAN_STATUS_PENDING;

exit:
	LEAVE();
	return ret;
}

/**
 *  @brief Read the EEPROM contents of the card
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_PENDING --success, otherwise fail
 */
mlan_status
wlan_reg_mem_ioctl_read_eeprom(pmlan_adapter pmadapter,
			       pmlan_ioctl_req pioctl_req)
{
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_ds_reg_mem *reg_mem = MNULL;
	mlan_status ret = MLAN_STATUS_SUCCESS;
	t_u16 cmd_action = 0;

	ENTER();

	reg_mem = (mlan_ds_reg_mem *)pioctl_req->pbuf;
	if (pioctl_req->action == MLAN_ACT_GET)
		cmd_action = HostCmd_ACT_GEN_GET;

	/* Send request to firmware */
	ret = wlan_prepare_cmd(pmpriv, HostCmd_CMD_802_11_EEPROM_ACCESS,
			       cmd_action, 0, (t_void *)pioctl_req,
			       (t_void *)&reg_mem->param.rd_eeprom);

	if (ret == MLAN_STATUS_SUCCESS)
		ret = MLAN_STATUS_PENDING;

	LEAVE();
	return ret;
}

/**
 *  @brief Read/write memory of device
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_PENDING --success, otherwise fail
 */
mlan_status
wlan_reg_mem_ioctl_mem_rw(pmlan_adapter pmadapter, pmlan_ioctl_req pioctl_req)
{
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_ds_reg_mem *reg_mem = MNULL;
	mlan_status ret = MLAN_STATUS_SUCCESS;
	t_u16 cmd_action = 0;

	ENTER();

	reg_mem = (mlan_ds_reg_mem *)pioctl_req->pbuf;
	if (pioctl_req->action == MLAN_ACT_GET)
		cmd_action = HostCmd_ACT_GEN_GET;
	else
		cmd_action = HostCmd_ACT_GEN_SET;

	/* Send request to firmware */
	ret = wlan_prepare_cmd(pmpriv, HostCmd_CMD_MEM_ACCESS, cmd_action, 0,
			       (t_void *)pioctl_req,
			       (t_void *)&reg_mem->param.mem_rw);

	if (ret == MLAN_STATUS_SUCCESS)
		ret = MLAN_STATUS_PENDING;

	LEAVE();
	return ret;
}

/**
 *  @brief This function will check if station list is empty
 *
 *  @param priv    A pointer to mlan_private
 *
 *  @return	   MFALSE/MTRUE
 */
t_u8
wlan_is_station_list_empty(mlan_private *priv)
{
	ENTER();
	if (!(util_peek_list(priv->adapter->pmoal_handle, &priv->sta_list,
			     priv->adapter->callbacks.moal_spin_lock,
			     priv->adapter->callbacks.moal_spin_unlock))) {
		LEAVE();
		return MTRUE;
	}
	LEAVE();
	return MFALSE;
}

/**
 *  @brief This function will return the pointer to station entry in station
 * list table which matches the give mac address
 *
 *  @param priv    A pointer to mlan_private
 *  @param mac     mac address to find in station list table
 *
 *  @return	   A pointer to structure sta_node
 */
sta_node *
wlan_get_station_entry(mlan_private *priv, t_u8 *mac)
{
	sta_node *sta_ptr;

	ENTER();

	if (!mac) {
		LEAVE();
		return MNULL;
	}
	sta_ptr =
		(sta_node *)util_peek_list(priv->adapter->pmoal_handle,
					   &priv->sta_list, MNULL, MNULL);

	while (sta_ptr && (sta_ptr != (sta_node *)&priv->sta_list)) {
		if (!memcmp(priv->adapter, sta_ptr->mac_addr, mac,
			    MLAN_MAC_ADDR_LENGTH)) {
			LEAVE();
			return sta_ptr;
		}
		sta_ptr = sta_ptr->pnext;
	}
	LEAVE();
	return MNULL;
}

/**
 *  @brief This function will add a pointer to station entry in station list
 *          table with the give mac address, if it does not exist already
 *
 *  @param priv    A pointer to mlan_private
 *  @param mac     mac address to find in station list table
 *
 *  @return	   A pointer to structure sta_node
 */
sta_node *
wlan_add_station_entry(mlan_private *priv, t_u8 *mac)
{
	sta_node *sta_ptr = MNULL;

	ENTER();

	sta_ptr = wlan_get_station_entry(priv, mac);
	if (sta_ptr)
		goto done;
	if (priv->adapter->callbacks.moal_malloc(priv->adapter->pmoal_handle,
						 sizeof(sta_node), MLAN_MEM_DEF,
						 (t_u8 **)&sta_ptr)) {
		PRINTM(MERROR, "Failed to allocate memory for station node\n");
		LEAVE();
		return MNULL;
	}
	memset(priv->adapter, sta_ptr, 0, sizeof(sta_node));
	memcpy_ext(priv->adapter, sta_ptr->mac_addr, mac, MLAN_MAC_ADDR_LENGTH,
		   MLAN_MAC_ADDR_LENGTH);
	util_enqueue_list_tail(priv->adapter->pmoal_handle, &priv->sta_list,
			       (pmlan_linked_list)sta_ptr,
			       priv->adapter->callbacks.moal_spin_lock,
			       priv->adapter->callbacks.moal_spin_unlock);
#ifdef DRV_EMBEDDED_AUTHENTICATOR
	if ((GET_BSS_ROLE(priv) == MLAN_BSS_ROLE_UAP) &&
	    IsAuthenticatorEnabled(priv->psapriv))
		authenticator_init_client(priv->psapriv,
					  &sta_ptr->cm_connectioninfo, mac);
#endif
done:
	LEAVE();
	return sta_ptr;
}

/**
 *  @brief This function will delete a station entry from station list
 *
 *
 *  @param priv    A pointer to mlan_private
 *  @param mac     station's mac address
 *
 *  @return	   N/A
 */
t_void
wlan_delete_station_entry(mlan_private *priv, t_u8 *mac)
{
	sta_node *sta_ptr = MNULL;
	ENTER();
	sta_ptr = wlan_get_station_entry(priv, mac);
	if (sta_ptr) {
#ifdef DRV_EMBEDDED_AUTHENTICATOR
		if ((GET_BSS_ROLE(priv) == MLAN_BSS_ROLE_UAP) &&
		    IsAuthenticatorEnabled(priv->psapriv))
			authenticator_free_client(priv->psapriv,
						  sta_ptr->cm_connectioninfo);
#endif
		util_unlink_list(priv->adapter->pmoal_handle, &priv->sta_list,
				 (pmlan_linked_list)sta_ptr,
				 priv->adapter->callbacks.moal_spin_lock,
				 priv->adapter->callbacks.moal_spin_unlock);
		priv->adapter->callbacks.moal_mfree(priv->adapter->pmoal_handle,
						    (t_u8 *)sta_ptr);
	}

	LEAVE();
	return;
}

/**
 *  @brief Clean up wapi station list
 *
 *  @param priv  Pointer to the mlan_private driver data struct
 *
 *  @return      N/A
 */
t_void
wlan_delete_station_list(pmlan_private priv)
{
	sta_node *sta_ptr;

	ENTER();
	while ((sta_ptr =
		(sta_node *)util_dequeue_list(priv->adapter->pmoal_handle,
					      &priv->sta_list,
					      priv->adapter->callbacks.
					      moal_spin_lock,
					      priv->adapter->callbacks.
					      moal_spin_unlock))) {
#ifdef DRV_EMBEDDED_AUTHENTICATOR
		if ((GET_BSS_ROLE(priv) == MLAN_BSS_ROLE_UAP) &&
		    IsAuthenticatorEnabled(priv->psapriv))
			authenticator_free_client(priv->psapriv,
						  sta_ptr->cm_connectioninfo);
#endif
		priv->adapter->callbacks.moal_mfree(priv->adapter->pmoal_handle,
						    (t_u8 *)sta_ptr);
	}
	LEAVE();
	return;
}

/**
 *  @brief Get tdls peer list
 *
 *  @param priv         A pointer to mlan_private structure
 *  @param buf          A pointer to tdls_peer_info buf
 *  @return             number of tdls peer
 */
int
wlan_get_tdls_list(mlan_private *priv, tdls_peer_info *buf)
{
	tdls_peer_info *peer_info = buf;
	sta_node *sta_ptr = MNULL;
	int count = 0;
	ENTER();
	if (priv->bss_type != MLAN_BSS_TYPE_STA) {
		LEAVE();
		return count;
	}
	sta_ptr =
		(sta_node *)util_peek_list(priv->adapter->pmoal_handle,
					   &priv->sta_list,
					   priv->adapter->callbacks.
					   moal_spin_lock,
					   priv->adapter->callbacks.
					   moal_spin_unlock);
	if (!sta_ptr) {
		LEAVE();
		return count;
	}
	while (sta_ptr != (sta_node *)&priv->sta_list) {
		if (sta_ptr->status == TDLS_SETUP_COMPLETE) {
			peer_info->snr = sta_ptr->snr;
			peer_info->nf = sta_ptr->nf;
			memcpy_ext(priv->adapter, peer_info->mac_addr,
				   sta_ptr->mac_addr, MLAN_MAC_ADDR_LENGTH,
				   MLAN_MAC_ADDR_LENGTH);
			memcpy_ext(priv->adapter, peer_info->ht_cap,
				   &sta_ptr->HTcap, sizeof(IEEEtypes_HTCap_t),
				   sizeof(peer_info->ht_cap));
			memcpy_ext(priv->adapter, peer_info->ext_cap,
				   &sta_ptr->ExtCap, sizeof(IEEEtypes_ExtCap_t),
				   sizeof(peer_info->ext_cap));
			memcpy_ext(priv->adapter, peer_info->vht_cap,
				   &sta_ptr->vht_cap,
				   sizeof(IEEEtypes_VHTCap_t),
				   sizeof(peer_info->vht_cap));
			peer_info++;
			count++;
		}
		sta_ptr = sta_ptr->pnext;
		if (count >= MLAN_MAX_TDLS_PEER_SUPPORTED)
			break;
	}
	LEAVE();
	return count;
}

/**
 *  @brief Set the TDLS configuration to FW.
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_PENDING --success, otherwise fail
 */
mlan_status
wlan_misc_ioctl_tdls_config(pmlan_adapter pmadapter, pmlan_ioctl_req pioctl_req)
{
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_status ret = MLAN_STATUS_SUCCESS;
	mlan_ds_misc_cfg *misc = (mlan_ds_misc_cfg *)pioctl_req->pbuf;
	tdls_all_config *tdls_all_cfg =
		(tdls_all_config *)misc->param.tdls_config.tdls_data;
	t_u8 event_buf[100];
	mlan_event *pevent = (mlan_event *)event_buf;
	tdls_tear_down_event *tdls_evt =
		(tdls_tear_down_event *)pevent->event_buf;
	sta_node *sta_ptr = MNULL;
	MrvlIEtypes_Data_t *pMrvlTlv = MNULL;
	t_u8 *pos = MNULL;
	t_u16 remain_len = 0;

	ENTER();

	if (misc->param.tdls_config.tdls_action == WLAN_TDLS_TEAR_DOWN_REQ) {
		sta_ptr =
			wlan_get_station_entry(pmpriv,
					       tdls_all_cfg->u.tdls_tear_down.
					       peer_mac_addr);
		if (sta_ptr && sta_ptr->external_tdls) {
			pevent->bss_index = pmpriv->bss_index;
			pevent->event_id = MLAN_EVENT_ID_DRV_TDLS_TEARDOWN_REQ;
			pevent->event_len = sizeof(tdls_tear_down_event);
			memcpy_ext(pmpriv->adapter,
				   (t_u8 *)tdls_evt->peer_mac_addr,
				   tdls_all_cfg->u.tdls_tear_down.peer_mac_addr,
				   MLAN_MAC_ADDR_LENGTH, MLAN_MAC_ADDR_LENGTH);
			tdls_evt->reason_code =
				tdls_all_cfg->u.tdls_tear_down.reason_code;
			wlan_recv_event(pmpriv,
					MLAN_EVENT_ID_DRV_TDLS_TEARDOWN_REQ,
					pevent);
			LEAVE();
			return ret;
		}
	}
	if (misc->param.tdls_config.tdls_action == WLAN_HOST_TDLS_CONFIG) {
		pmpriv->host_tdls_uapsd_support =
			tdls_all_cfg->u.host_tdls_cfg.uapsd_support;
		pmpriv->host_tdls_cs_support =
			tdls_all_cfg->u.host_tdls_cfg.cs_support;
		pos = tdls_all_cfg->u.host_tdls_cfg.tlv_buffer;
		remain_len = tdls_all_cfg->u.host_tdls_cfg.tlv_len;
		while (remain_len > sizeof(MrvlIEtypesHeader_t)) {
			remain_len -= sizeof(MrvlIEtypesHeader_t);
			pMrvlTlv = (MrvlIEtypes_Data_t *)pos;
			switch (pMrvlTlv->header.type) {
			case SUPPORTED_CHANNELS:
				pmpriv->chan_supp_len =
					(t_u8)MIN(pMrvlTlv->header.len,
						  MAX_IE_SIZE);
				memset(pmadapter, pmpriv->chan_supp, 0,
				       sizeof(pmpriv->chan_supp));
				memcpy_ext(pmadapter, pmpriv->chan_supp,
					   pMrvlTlv->data, pMrvlTlv->header.len,
					   MAX_IE_SIZE);
				DBG_HEXDUMP(MCMD_D, "TDLS supported channel",
					    pmpriv->chan_supp,
					    pmpriv->chan_supp_len);
				break;
			case REGULATORY_CLASS:
				pmpriv->supp_regulatory_class_len =
					(t_u8)MIN(pMrvlTlv->header.len,
						  MAX_IE_SIZE);
				memset(pmadapter, pmpriv->supp_regulatory_class,
				       0,
				       sizeof(pmpriv->supp_regulatory_class));
				memcpy_ext(pmadapter,
					   pmpriv->supp_regulatory_class,
					   pMrvlTlv->data, pMrvlTlv->header.len,
					   MAX_IE_SIZE);
				DBG_HEXDUMP(MCMD_D,
					    "TDLS supported regulatory class",
					    pmpriv->supp_regulatory_class,
					    pmpriv->supp_regulatory_class_len);
				break;
			default:
				break;
			}
			remain_len -= pMrvlTlv->header.len;
			pos += sizeof(MrvlIEtypesHeader_t) +
				pMrvlTlv->header.len;
		}
		LEAVE();
		return ret;
	}
	pioctl_req->action = MLAN_ACT_SET;

	/* Send command to firmware */
	ret = wlan_prepare_cmd(pmpriv, HostCmd_CMD_TDLS_CONFIG,
			       HostCmd_ACT_GEN_SET, 0, (t_void *)pioctl_req,
			       &misc->param.tdls_config);

	if (ret == MLAN_STATUS_SUCCESS)
		ret = MLAN_STATUS_PENDING;
	LEAVE();
	return ret;
}

/**
 *  @brief enable tdls config for cs and uapsd.
 *
 *  @param pmpriv	A pointer to mlan_private structure
 *  @param enable   	MTRUE/MFALSE
 *
 *  @return
 */
t_void
wlan_tdls_config(pmlan_private pmpriv, t_u8 enable)
{
	mlan_adapter *pmadapter = pmpriv->adapter;
	mlan_callbacks *pcb = (mlan_callbacks *)&pmadapter->callbacks;
	mlan_ds_misc_tdls_config *tdls_config = MNULL;
	tdls_all_config *tdls_all_cfg = MNULL;
	mlan_status ret = MLAN_STATUS_SUCCESS;

	ENTER();

	ret = pcb->moal_malloc(pmadapter->pmoal_handle,
			       sizeof(mlan_ds_misc_tdls_config), MLAN_MEM_DEF,
			       (t_u8 **)&tdls_config);
	if (ret != MLAN_STATUS_SUCCESS || !tdls_config) {
		PRINTM(MERROR, "Memory allocation for tdls_config failed!\n");
		LEAVE();
		return;
	}
	memset(pmadapter, (t_u8 *)tdls_config, 0,
	       sizeof(mlan_ds_misc_tdls_config));
	tdls_all_cfg = (tdls_all_config *)tdls_config->tdls_data;
	tdls_all_cfg->u.tdls_config.enable = enable;
	tdls_config->tdls_action = WLAN_TDLS_CONFIG;
	/* Send command to firmware */
	wlan_prepare_cmd(pmpriv, HostCmd_CMD_TDLS_CONFIG, HostCmd_ACT_GEN_SET,
			 0, MNULL, tdls_config);
	PRINTM(MCMND, "tdls_config: enable=%d\n", enable);

	if (tdls_config)
		pcb->moal_mfree(pmadapter->pmoal_handle, (t_u8 *)tdls_config);

	LEAVE();
}

/**
 *  @brief set tdls channel switch parameters.
 *
 *  @param pmpriv	A pointer to mlan_private structure
 *
 *  @return
 */
static t_void
wlan_tdls_cs_param_config(pmlan_private pmpriv)
{
	mlan_adapter *pmadapter = pmpriv->adapter;
	mlan_callbacks *pcb = (mlan_callbacks *)&pmadapter->callbacks;
	mlan_ds_misc_tdls_config *tdls_config = MNULL;
	tdls_all_config *tdls_all_cfg = MNULL;
	mlan_status ret = MLAN_STATUS_SUCCESS;

	ENTER();

	ret = pcb->moal_malloc(pmadapter->pmoal_handle,
			       sizeof(mlan_ds_misc_tdls_config), MLAN_MEM_DEF,
			       (t_u8 **)&tdls_config);
	if (ret != MLAN_STATUS_SUCCESS || !tdls_config) {
		PRINTM(MERROR, "Memory allocation for tdls_config failed!\n");
		LEAVE();
		return;
	}
	memset(pmadapter, (t_u8 *)tdls_config, 0,
	       sizeof(mlan_ds_misc_tdls_config));

	tdls_all_cfg = (tdls_all_config *)tdls_config->tdls_data;
	tdls_config->tdls_action = WLAN_TDLS_CS_PARAMS;
	tdls_all_cfg->u.tdls_cs_params.unit_time = 2;
	tdls_all_cfg->u.tdls_cs_params.threshold_otherlink = 10;
	tdls_all_cfg->u.tdls_cs_params.threshold_directlink = 0;

	/* Send command to firmware */
	wlan_prepare_cmd(pmpriv, HostCmd_CMD_TDLS_CONFIG, HostCmd_ACT_GEN_SET,
			 0, MNULL, tdls_config);

	if (tdls_config)
		pcb->moal_mfree(pmadapter->pmoal_handle, (t_u8 *)tdls_config);

	LEAVE();
}

/**
 *  @brief start tdls channel switch
 *
 *  @param pmpriv	A pointer to mlan_private structure
 *  @param peer_mac_addr 	A pointer to peer mac address
 *  @param pioctl_buf   A pointer to ioctl request buffer
 *
 *  @return
 */
static t_void
wlan_tdls_cs_start(pmlan_private pmpriv, t_u8 *peer_mac_addr,
		   pmlan_ioctl_req pioctl_buf)
{
	mlan_adapter *pmadapter = pmpriv->adapter;
	mlan_callbacks *pcb = (mlan_callbacks *)&pmadapter->callbacks;
	mlan_ds_misc_tdls_config *tdls_config = MNULL;
	tdls_all_config *tdls_all_cfg = MNULL;
	mlan_ds_misc_cfg *misc = MNULL;
	mlan_status ret = MLAN_STATUS_SUCCESS;

	ENTER();

	ret = pcb->moal_malloc(pmadapter->pmoal_handle,
			       sizeof(mlan_ds_misc_tdls_config), MLAN_MEM_DEF,
			       (t_u8 **)&tdls_config);
	if (ret != MLAN_STATUS_SUCCESS || !tdls_config) {
		PRINTM(MERROR, "Memory allocation for tdls_config failed!\n");
		LEAVE();
		return;
	}
	memset(pmadapter, (t_u8 *)tdls_config, 0,
	       sizeof(mlan_ds_misc_tdls_config));

	if (pioctl_buf) {
		misc = (mlan_ds_misc_cfg *)pioctl_buf->pbuf;
		memcpy_ext(pmpriv->adapter, tdls_config,
			   &misc->param.tdls_config,
			   sizeof(mlan_ds_misc_tdls_config),
			   sizeof(mlan_ds_misc_tdls_config));
		tdls_all_cfg = (tdls_all_config *)tdls_config->tdls_data;
		if (tdls_all_cfg->u.tdls_chan_switch.primary_channel > 14) {
			tdls_all_cfg->u.tdls_chan_switch.
				secondary_channel_offset =
				wlan_get_second_channel_offset(pmpriv,
							       tdls_all_cfg->u.
							       tdls_chan_switch.
							       primary_channel);
		}
		PRINTM(MCMND, "Start TDLS CS: channel=%d\n",
		       tdls_all_cfg->u.tdls_chan_switch.primary_channel);
	} else {
		tdls_all_cfg = (tdls_all_config *)tdls_config->tdls_data;
		tdls_config->tdls_action = WLAN_TDLS_INIT_CHAN_SWITCH;
		memcpy_ext(pmpriv->adapter,
			   tdls_all_cfg->u.tdls_chan_switch.peer_mac_addr,
			   peer_mac_addr, MLAN_MAC_ADDR_LENGTH,
			   MLAN_MAC_ADDR_LENGTH);
		tdls_all_cfg->u.tdls_chan_switch.primary_channel =
			pmpriv->tdls_cs_channel;
		if (pmpriv->tdls_cs_channel > 14) {
			tdls_all_cfg->u.tdls_chan_switch.band = BAND_5GHZ;
			tdls_all_cfg->u.tdls_chan_switch.
				secondary_channel_offset =
				wlan_get_second_channel_offset(pmpriv,
							       pmpriv->
							       tdls_cs_channel);
		} else {
			tdls_all_cfg->u.tdls_chan_switch.band = BAND_2GHZ;
		}
		PRINTM(MCMND, "Start TDLS CS: channel=%d\n",
		       pmpriv->tdls_cs_channel);
	}
	tdls_all_cfg->u.tdls_chan_switch.switch_time = 10;
	tdls_all_cfg->u.tdls_chan_switch.switch_timeout = 16;
	tdls_all_cfg->u.tdls_chan_switch.regulatory_class = 12;
	tdls_all_cfg->u.tdls_chan_switch.periodicity = 1;

	/* Send command to firmware */
	wlan_prepare_cmd(pmpriv, HostCmd_CMD_TDLS_CONFIG, HostCmd_ACT_GEN_SET,
			 0, MNULL, tdls_config);

	if (tdls_config)
		pcb->moal_mfree(pmadapter->pmoal_handle, (t_u8 *)tdls_config);

	LEAVE();
}

#if 0
/**
 *  @brief stop tdls channel switch
 *
 *  @param pmpriv	A pointer to mlan_private structure
 *  @param peer_mac_addr 	A pointer to peer mac address
 *  @param pioctl_buf   A pointer to command buffer
 *  @return
 */
static t_void
wlan_tdls_cs_stop(pmlan_private pmpriv, t_u8 *peer_mac_addr)
{
	mlan_adapter *pmadapter = pmpriv->adapter;
	mlan_callbacks *pcb = (mlan_callbacks *)&pmadapter->callbacks;
	mlan_ds_misc_tdls_config *tdls_config = MNULL;
	tdls_all_config *tdls_all_cfg = MNULL;
	mlan_status ret = MLAN_STATUS_SUCCESS;

	ENTER();

	ret = pcb->moal_malloc(pmadapter->pmoal_handle,
			       sizeof(mlan_ds_misc_tdls_config), MLAN_MEM_DEF,
			       (t_u8 **)&tdls_config);
	if (ret != MLAN_STATUS_SUCCESS || !tdls_config) {
		PRINTM(MERROR, "Memory allocation for tdls_config failed!\n");
		LEAVE();
		return;
	}
	memset(pmadapter, (t_u8 *)tdls_config, 0,
	       sizeof(mlan_ds_misc_tdls_config));

	tdls_all_cfg = (tdls_all_config *)tdls_config->tdls_data;
	tdls_config->tdls_action = WLAN_TDLS_STOP_CHAN_SWITCH;

	memcpy_ext(pmpriv->adapter,
		   tdls_all_cfg->u.tdls_stop_chan_switch.peer_mac_addr,
		   peer_mac_addr, MLAN_MAC_ADDR_LENGTH, MLAN_MAC_ADDR_LENGTH);
	PRINTM(MCMND, "Stop TDLS CS\n");
	/* Send command to firmware */
	wlan_prepare_cmd(pmpriv, HostCmd_CMD_TDLS_CONFIG, HostCmd_ACT_GEN_SET,
			 0, MNULL, tdls_config);

	if (tdls_config)
		pcb->moal_mfree(pmadapter->pmoal_handle, (t_u8 *)tdls_config);

	LEAVE();
}
#endif

/**
 *  @brief Set/Get the TDLS off channel.
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_PENDING --success, otherwise fail
 */
mlan_status
wlan_misc_ioctl_tdls_cs_channel(pmlan_adapter pmadapter,
				pmlan_ioctl_req pioctl_req)
{
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_status ret = MLAN_STATUS_SUCCESS;
	mlan_ds_misc_cfg *misc = (mlan_ds_misc_cfg *)pioctl_req->pbuf;

	ENTER();

	if (MLAN_ACT_GET == pioctl_req->action)
		misc->param.tdls_cs_channel = pmpriv->tdls_cs_channel;
	else if (MLAN_ACT_SET == pioctl_req->action) {
		pmpriv->tdls_cs_channel = misc->param.tdls_cs_channel;
	}
	LEAVE();
	return ret;
}

/**
 *  @brief Set/Get the TDLS idle time.
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_PENDING --success, otherwise fail
 */
mlan_status
wlan_misc_ioctl_tdls_idle_time(pmlan_adapter pmadapter,
			       pmlan_ioctl_req pioctl_req)
{
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_status ret = MLAN_STATUS_SUCCESS;
	mlan_ds_misc_cfg *misc = (mlan_ds_misc_cfg *)pioctl_req->pbuf;

	ENTER();

	if (MLAN_ACT_GET == pioctl_req->action) {
		misc->param.tdls_idle_time = pmpriv->tdls_idle_time;
	} else if (MLAN_ACT_SET == pioctl_req->action) {
		pmpriv->tdls_idle_time = misc->param.tdls_idle_time;
	}
	LEAVE();
	return ret;
}

/**
 *  @brief Set the TDLS operation to FW.
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_PENDING --success, otherwise fail
 */
mlan_status
wlan_misc_ioctl_tdls_oper(pmlan_adapter pmadapter, pmlan_ioctl_req pioctl_req)
{
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_status ret = MLAN_STATUS_SUCCESS;
	mlan_ds_misc_cfg *misc = (mlan_ds_misc_cfg *)pioctl_req->pbuf;
	mlan_ds_misc_tdls_oper *ptdls_oper = &misc->param.tdls_oper;
	t_u8 event_buf[100];
	mlan_event *ptdls_event = (mlan_event *)event_buf;
	tdls_tear_down_event *tdls_evt =
		(tdls_tear_down_event *)ptdls_event->event_buf;
	sta_node *sta_ptr = MNULL;
	t_u8 i = 0;

	ENTER();
	sta_ptr = wlan_get_station_entry(pmpriv, ptdls_oper->peer_mac);
	switch (ptdls_oper->tdls_action) {
	case WLAN_TDLS_ENABLE_LINK:
		if (sta_ptr && (sta_ptr->status != TDLS_SETUP_FAILURE)) {
			PRINTM(MMSG, "TDLS: Enable link " MACSTR " success\n",
			       MAC2STR(ptdls_oper->peer_mac));
			sta_ptr->status = TDLS_SETUP_COMPLETE;
			pmadapter->tdls_status = TDLS_IN_BASE_CHANNEL;
			if (!pmpriv->txaggrctrl)
				wlan_11n_send_delba_to_peer(pmpriv,
							    pmpriv->
							    curr_bss_params.
							    bss_descriptor.
							    mac_address);
			if (sta_ptr->HTcap.ieee_hdr.element_id == HT_CAPABILITY) {
				sta_ptr->is_11n_enabled = MTRUE;
				if (GETHT_MAXAMSDU
				    (sta_ptr->HTcap.ht_cap.ht_cap_info))
					sta_ptr->max_amsdu =
						MLAN_TX_DATA_BUF_SIZE_8K;
				else
					sta_ptr->max_amsdu =
						MLAN_TX_DATA_BUF_SIZE_4K;
				for (i = 0; i < MAX_NUM_TID; i++) {
					if (sta_ptr->is_11n_enabled)
						sta_ptr->ampdu_sta[i] =
							pmpriv->aggr_prio_tbl[i]
						.ampdu_user;
					else
						sta_ptr->ampdu_sta[i] =
							BA_STREAM_NOT_ALLOWED;
				}
				memset(pmpriv->adapter, sta_ptr->rx_seq, 0xff,
				       sizeof(sta_ptr->rx_seq));
			}
			wlan_restore_tdls_packets(pmpriv, ptdls_oper->peer_mac,
						  TDLS_SETUP_COMPLETE);
			if (ISSUPP_EXTCAP_TDLS_CHAN_SWITCH
			    (sta_ptr->ExtCap.ext_cap)) {
				wlan_tdls_config(pmpriv, MTRUE);
				wlan_tdls_cs_param_config(pmpriv);
				/**tdls cs start*/
				if (pmpriv->tdls_cs_channel &&
				    pmpriv->tdls_cs_channel !=
				    pmpriv->curr_bss_params.bss_descriptor.
				    channel)
					wlan_tdls_cs_start(pmpriv,
							   ptdls_oper->peer_mac,
							   MNULL);
			}
		} else {
			PRINTM(MMSG, "TDLS: Enable link " MACSTR " fail\n",
			       MAC2STR(ptdls_oper->peer_mac));
			/*for supplicant 2.0, we need send event to request
			 *teardown, *for latest supplicant, we only need return
			 *fail, and supplicant will send teardown packet and
			 *disable tdls link*/
			if (sta_ptr) {
				ptdls_event->bss_index = pmpriv->bss_index;
				ptdls_event->event_id =
					MLAN_EVENT_ID_DRV_TDLS_TEARDOWN_REQ;
				ptdls_event->event_len =
					sizeof(tdls_tear_down_event);
				memcpy_ext(pmpriv->adapter,
					   (t_u8 *)tdls_evt->peer_mac_addr,
					   ptdls_oper->peer_mac,
					   MLAN_MAC_ADDR_LENGTH,
					   MLAN_MAC_ADDR_LENGTH);
				tdls_evt->reason_code =
					MLAN_REASON_TDLS_TEARDOWN_UNSPECIFIED;
				wlan_recv_event(pmpriv,
						MLAN_EVENT_ID_DRV_TDLS_TEARDOWN_REQ,
						ptdls_event);
				wlan_restore_tdls_packets(pmpriv,
							  ptdls_oper->peer_mac,
							  TDLS_TEAR_DOWN);
				if (sta_ptr->is_11n_enabled) {
					wlan_cleanup_reorder_tbl(pmpriv,
								 ptdls_oper->
								 peer_mac);
					wlan_11n_cleanup_txbastream_tbl(pmpriv,
									ptdls_oper->
									peer_mac);
				}
				wlan_delete_station_entry(pmpriv,
							  ptdls_oper->peer_mac);
				if (MTRUE == wlan_is_station_list_empty(pmpriv))
					pmadapter->tdls_status = TDLS_NOT_SETUP;
				else
					pmadapter->tdls_status =
						TDLS_IN_BASE_CHANNEL;
			}
			ret = MLAN_STATUS_FAILURE;
		}
		wlan_recv_event(pmpriv, MLAN_EVENT_ID_DRV_DEFER_HANDLING,
				MNULL);
		break;
	case WLAN_TDLS_DISABLE_LINK:
		/* Send command to firmware to delete tdls link */
		ret = wlan_prepare_cmd(pmpriv, HostCmd_CMD_TDLS_OPERATION,
				       HostCmd_ACT_GEN_SET, 0,
				       (t_void *)pioctl_req, ptdls_oper);
		if (ret == MLAN_STATUS_SUCCESS)
			ret = MLAN_STATUS_PENDING;
		break;
	case WLAN_TDLS_CREATE_LINK:
		PRINTM(MIOCTL, "CREATE TDLS LINK\n");
		if (sta_ptr && sta_ptr->status == TDLS_SETUP_INPROGRESS) {
			PRINTM(MIOCTL, "We already create the link\n");
			break;
		}
		if (!sta_ptr)
			sta_ptr =
				wlan_add_station_entry(pmpriv,
						       misc->param.tdls_oper.
						       peer_mac);
		if (sta_ptr) {
			sta_ptr->status = TDLS_SETUP_INPROGRESS;
			sta_ptr->external_tdls = MTRUE;
			wlan_hold_tdls_packets(pmpriv,
					       misc->param.tdls_oper.peer_mac);
		}
		ret = wlan_prepare_cmd(pmpriv, HostCmd_CMD_TDLS_OPERATION,
				       HostCmd_ACT_GEN_SET, 0,
				       (t_void *)pioctl_req, ptdls_oper);
		if (ret == MLAN_STATUS_SUCCESS)
			ret = MLAN_STATUS_PENDING;
		break;
	case WLAN_TDLS_CONFIG_LINK:
		if (!sta_ptr || sta_ptr->status == TDLS_SETUP_FAILURE) {
			PRINTM(MERROR, "Can not CONFIG TDLS Link\n");
			ret = MLAN_STATUS_FAILURE;
			break;
		}
		ret = wlan_prepare_cmd(pmpriv, HostCmd_CMD_TDLS_OPERATION,
				       HostCmd_ACT_GEN_SET, 0,
				       (t_void *)pioctl_req, ptdls_oper);
		if (ret == MLAN_STATUS_SUCCESS)
			ret = MLAN_STATUS_PENDING;
		break;
	case WLAN_TDLS_INIT_CHAN_SWITCH:
		if (sta_ptr &&
		    ISSUPP_EXTCAP_TDLS_CHAN_SWITCH(sta_ptr->ExtCap.ext_cap)) {
			wlan_tdls_config(pmpriv, MTRUE);
			wlan_tdls_cs_param_config(pmpriv);
			/**tdls cs start*/
			wlan_tdls_cs_start(pmpriv, ptdls_oper->peer_mac,
					   pioctl_req);
		}
		wlan_recv_event(pmpriv, MLAN_EVENT_ID_DRV_DEFER_HANDLING,
				MNULL);
		break;
	default:
		break;
	}
	LEAVE();
	return ret;
}

/**
 *  @brief Get AP's ext capability
 *
 *  @param pmpriv	A pointer to mlan_adapter structure
 *  @param ext_cap  A pointer to ExtCap_t structure
 *
 *  @return		MLAN_STATUS_PENDING --success, otherwise fail
 */
static void
wlan_get_ap_ext_cap(mlan_private *pmpriv, ExtCap_t *ext_cap)
{
	pmlan_adapter pmadapter = pmpriv->adapter;
	BSSDescriptor_t *pbss_desc;
	pbss_desc = &pmpriv->curr_bss_params.bss_descriptor;
	memset(pmadapter, ext_cap, 0, sizeof(ExtCap_t));
	if (pbss_desc->pext_cap) {
		memcpy_ext(pmadapter, (t_u8 *)ext_cap,
			   (t_u8 *)pbss_desc->pext_cap +
			   sizeof(IEEEtypes_Header_t),
			   pbss_desc->pext_cap->ieee_hdr.len, sizeof(ExtCap_t));
	}
	return;
}

/**
 *  @brief Set the TDLS operation to FW.
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_PENDING --success, otherwise fail
 */
mlan_status
wlan_misc_ioctl_tdls_get_ies(pmlan_adapter pmadapter,
			     pmlan_ioctl_req pioctl_req)
{
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_ds_misc_cfg *misc = (mlan_ds_misc_cfg *)pioctl_req->pbuf;
	mlan_ds_misc_tdls_ies *tdls_ies = &misc->param.tdls_ies;
	mlan_status ret = MLAN_STATUS_SUCCESS;
	BSSDescriptor_t *pbss_desc;
	t_u32 usr_dot_11n_dev_cap;
	IEEEtypes_ExtCap_t *ext_cap = MNULL;
	ExtCap_t ap_ext_cap;
	IEEEtypes_HTCap_t *ht_cap = MNULL;
	IEEEtypes_HTInfo_t *ht_info = MNULL;
	IEEEtypes_VHTCap_t *vht_cap = MNULL;
	IEEEtypes_VHTOprat_t *vht_oprat = MNULL;
	IEEEtypes_AssocRsp_t *passoc_rsp = MNULL;
	IEEEtypes_AID_t *aid_info = MNULL;
	t_u8 supp_chan[] = { 1, 11 };
	t_u8 regulatory_class[] = { 1, /**current class*/
		1, 2, 3, 4, 12, 22, 23, 24,
		25, 27, 28, 29, 30, 32, 33
	};							  /**list
								     regulatory
								     class*/
	IEEEtypes_Generic_t *pSupp_chan = MNULL, *pRegulatory_class = MNULL;
	sta_node *sta_ptr = MNULL;
	ENTER();

	/* We don't need peer information for TDLS setup */
	if (!(tdls_ies->flags & TDLS_IE_FLAGS_SETUP))
		sta_ptr = wlan_get_station_entry(pmpriv, tdls_ies->peer_mac);
	pbss_desc = &pmpriv->curr_bss_params.bss_descriptor;
	wlan_get_ap_ext_cap(pmpriv, &ap_ext_cap);
	if (pbss_desc->bss_band & BAND_A)
		usr_dot_11n_dev_cap = pmpriv->usr_dot_11n_dev_cap_a;
	else
		usr_dot_11n_dev_cap = pmpriv->usr_dot_11n_dev_cap_bg;

	/** fill the extcap */
	if (tdls_ies->flags & TDLS_IE_FLAGS_EXTCAP) {
		ext_cap = (IEEEtypes_ExtCap_t *)tdls_ies->ext_cap;
		ext_cap->ieee_hdr.element_id = EXT_CAPABILITY;
		ext_cap->ieee_hdr.len = sizeof(ExtCap_t);
		SET_EXTCAP_TDLS(ext_cap->ext_cap);
		RESET_EXTCAP_TDLS_UAPSD(ext_cap->ext_cap);
		RESET_EXTCAP_TDLS_CHAN_SWITCH(ext_cap->ext_cap);

		if (pmpriv->host_tdls_uapsd_support) {
			/* uapsd in tdls confirm frame */
			if (tdls_ies->flags & TDLS_IE_FLAGS_HTINFO) {
				if (sta_ptr &&
				    ISSUPP_EXTCAP_TDLS_UAPSD(sta_ptr->ExtCap.
							     ext_cap))
					SET_EXTCAP_TDLS_UAPSD(ext_cap->ext_cap);
			} else {
				SET_EXTCAP_TDLS_UAPSD(ext_cap->ext_cap);
			}
		}
		/*  channel switch support */
		if (pmpriv->host_tdls_cs_support &&
		    !IS_EXTCAP_TDLS_CHLSWITCHPROHIB(ap_ext_cap)) {
			/* channel switch in tdls confirm frame */
			if (tdls_ies->flags & TDLS_IE_FLAGS_HTINFO) {
				if (sta_ptr &&
				    ISSUPP_EXTCAP_TDLS_CHAN_SWITCH(sta_ptr->
								   ExtCap.
								   ext_cap))
					SET_EXTCAP_TDLS_CHAN_SWITCH(ext_cap->
								    ext_cap);
			} else {
				SET_EXTCAP_TDLS_CHAN_SWITCH(ext_cap->ext_cap);
			}
		}

		RESET_EXTCAP_TDLS_WIDER_BANDWIDTH(ext_cap->ext_cap);
		if ((pmadapter->fw_bands & BAND_AAC) &&
		    (MFALSE == wlan_is_ap_in_11ac_mode(pmpriv)))
			SET_EXTCAP_TDLS_WIDER_BANDWIDTH(ext_cap->ext_cap);
		/* if peer does not support wider bandwidth, don't set wider
		 * bandwidth*/
		if (sta_ptr && sta_ptr->rate_len &&
		    !ISSUPP_EXTCAP_TDLS_WIDER_BANDWIDTH(sta_ptr->ExtCap.
							ext_cap))
			RESET_EXTCAP_TDLS_WIDER_BANDWIDTH(ext_cap->ext_cap);
		DBG_HEXDUMP(MCMD_D, "TDLS extcap", tdls_ies->ext_cap,
			    sizeof(IEEEtypes_ExtCap_t));
	}

	/** default qos info is 0xf, compare with peer device qos info for tdls
	 * confirm */
	if (tdls_ies->flags & TDLS_IE_FLAGS_QOS_INFO) {
		if (sta_ptr && sta_ptr->rate_len)
			tdls_ies->QosInfo = sta_ptr->qos_info & 0xf;
		PRINTM(MCMND, "TDLS Qos info=0x%x\n", tdls_ies->QosInfo);
	}

	/** fill the htcap based on hwspec */
	if (tdls_ies->flags & TDLS_IE_FLAGS_HTCAP) {
		ht_cap = (IEEEtypes_HTCap_t *)tdls_ies->ht_cap;
		memset(pmadapter, ht_cap, 0, sizeof(IEEEtypes_HTCap_t));
		if ((sta_ptr &&
		     !ISSUPP_EXTCAP_TDLS_CHAN_SWITCH(sta_ptr->ExtCap.ext_cap))
		    || IS_EXTCAP_TDLS_CHLSWITCHPROHIB(ap_ext_cap))
			wlan_fill_ht_cap_ie(pmpriv, ht_cap,
					    pbss_desc->bss_band);
		else if (pmpriv->host_tdls_cs_support &&
			 (pmadapter->fw_bands & BAND_A))
			wlan_fill_ht_cap_ie(pmpriv, ht_cap, BAND_A);
		else
			wlan_fill_ht_cap_ie(pmpriv, ht_cap,
					    pbss_desc->bss_band);
		DBG_HEXDUMP(MCMD_D, "TDLS htcap", tdls_ies->ht_cap,
			    sizeof(IEEEtypes_HTCap_t));
	}
	/** if peer did not support 11AC, do not add vht related ie */
	if (sta_ptr && sta_ptr->rate_len &&
	    (sta_ptr->vht_cap.ieee_hdr.element_id != VHT_CAPABILITY))
		tdls_ies->flags &=
			~(TDLS_IE_FLAGS_VHTCAP | TDLS_IE_FLAGS_VHTOPRAT |
			  TDLS_IE_FLAGS_AID);
	/** fill the vhtcap based on hwspec */
	if (tdls_ies->flags & TDLS_IE_FLAGS_VHTCAP) {
		vht_cap = (IEEEtypes_VHTCap_t *)tdls_ies->vht_cap;
		memset(pmadapter, vht_cap, 0, sizeof(IEEEtypes_VHTCap_t));
		wlan_fill_vht_cap_ie(pmpriv, vht_cap, pbss_desc->bss_band);
		if (ht_cap)
			SETHT_SUPPCHANWIDTH(ht_cap->ht_cap.ht_cap_info);
		DBG_HEXDUMP(MCMD_D, "TDLS vhtcap", tdls_ies->vht_cap,
			    sizeof(IEEEtypes_VHTCap_t));
	}
	/** fill the vhtoperation based on hwspec */
	if (tdls_ies->flags & TDLS_IE_FLAGS_VHTOPRAT) {
		vht_oprat = (IEEEtypes_VHTOprat_t *)tdls_ies->vht_oprat;
		memset(pmadapter, vht_oprat, 0, sizeof(IEEEtypes_VHTOprat_t));
		if (sta_ptr &&
		    (sta_ptr->vht_cap.ieee_hdr.element_id == VHT_CAPABILITY) &&
		    (pbss_desc->bss_band & BAND_A)) {
			wlan_fill_tdls_vht_oprat_ie(pmpriv, vht_oprat, sta_ptr);
		}
		if (sta_ptr)
			memcpy_ext(pmadapter, &sta_ptr->vht_oprat,
				   tdls_ies->vht_oprat,
				   sizeof(IEEEtypes_VHTOprat_t),
				   sizeof(IEEEtypes_VHTOprat_t));
		DBG_HEXDUMP(MCMD_D, "TDLS vht_oprat", tdls_ies->vht_oprat,
			    sizeof(IEEEtypes_VHTOprat_t));
	}
	/** fill the AID info */
	if (tdls_ies->flags & TDLS_IE_FLAGS_AID) {
		if (pmpriv->curr_bss_params.host_mlme)
			passoc_rsp =
				(IEEEtypes_AssocRsp_t *)(pmpriv->assoc_rsp_buf +
							 sizeof
							 (IEEEtypes_MgmtHdr_t));
		else
			passoc_rsp =
				(IEEEtypes_AssocRsp_t *)pmpriv->assoc_rsp_buf;
		aid_info = (IEEEtypes_AID_t *)tdls_ies->aid_info;
		memset(pmadapter, aid_info, 0, sizeof(IEEEtypes_AID_t));
		aid_info->ieee_hdr.element_id = AID_INFO;
		aid_info->ieee_hdr.len = sizeof(t_u16);
		aid_info->AID = wlan_le16_to_cpu(passoc_rsp->a_id);
		PRINTM(MCMND, "TDLS AID=0x%x\n", aid_info->AID);
	}
	/** fill the htinfo */
	if (tdls_ies->flags & TDLS_IE_FLAGS_HTINFO) {
		ht_info = (IEEEtypes_HTInfo_t *)tdls_ies->ht_info;
		pbss_desc = &pmpriv->curr_bss_params.bss_descriptor;
		ht_info->ieee_hdr.element_id = HT_OPERATION;
		ht_info->ieee_hdr.len = sizeof(HTInfo_t);
		ht_info->ht_info.pri_chan = pbss_desc->channel;
		/* follow AP's channel bandwidth */
		if (ISSUPP_CHANWIDTH40(usr_dot_11n_dev_cap) &&
		    pbss_desc->pht_info &&
		    ISALLOWED_CHANWIDTH40(pbss_desc->pht_info->ht_info.
					  field2)) {
			ht_info->ht_info.field2 =
				pbss_desc->pht_info->ht_info.field2;
		} else {
			ht_info->ht_info.field2 =
				wlan_get_second_channel_offset(pmpriv,
							       pbss_desc->
							       channel);
		}
		if (vht_oprat &&
		    vht_oprat->ieee_hdr.element_id == VHT_OPERATION) {
			ht_info->ht_info.field2 =
				wlan_get_second_channel_offset(pmpriv,
							       pbss_desc->
							       channel);
			ht_info->ht_info.field2 |= MBIT(2);
		}
		if (sta_ptr)
			memcpy_ext(pmadapter, &sta_ptr->HTInfo,
				   tdls_ies->ht_info,
				   sizeof(IEEEtypes_HTInfo_t),
				   sizeof(IEEEtypes_HTInfo_t));
		DBG_HEXDUMP(MCMD_D, "TDLS htinfo", tdls_ies->ht_info,
			    sizeof(IEEEtypes_HTInfo_t));
	}

	/** supported channels andl regulatory IE*/
	if (pmpriv->host_tdls_cs_support &&
	    (tdls_ies->flags & TDLS_IE_FLAGS_SUPP_CS_IE) &&
	    !IS_EXTCAP_TDLS_CHLSWITCHPROHIB(ap_ext_cap)) {
		/** supported channels IE*/
		pSupp_chan = (IEEEtypes_Generic_t *)tdls_ies->supp_chan;
		pSupp_chan->ieee_hdr.element_id = SUPPORTED_CHANNELS;
		if (pmpriv->chan_supp_len) {
			pSupp_chan->ieee_hdr.len = pmpriv->chan_supp_len;
			memcpy_ext(pmadapter, pSupp_chan->data,
				   pmpriv->chan_supp, pmpriv->chan_supp_len,
				   sizeof(pSupp_chan->data));
		} else {
			pSupp_chan->ieee_hdr.len = sizeof(supp_chan);
			memcpy_ext(pmadapter, pSupp_chan->data, supp_chan,
				   sizeof(supp_chan), sizeof(pSupp_chan->data));
		}
		DBG_HEXDUMP(MCMD_D, "TDLS supported channel",
			    tdls_ies->supp_chan,
			    pSupp_chan->ieee_hdr.len +
			    sizeof(IEEEtypes_Header_t));

		/**fill supported Regulatory Class IE*/
		pRegulatory_class =
			(IEEEtypes_Generic_t *)tdls_ies->regulatory_class;
		pRegulatory_class->ieee_hdr.element_id = REGULATORY_CLASS;
		if (pmpriv->supp_regulatory_class_len) {
			pRegulatory_class->ieee_hdr.len =
				pmpriv->supp_regulatory_class_len;
			memcpy_ext(pmadapter, pRegulatory_class->data,
				   pmpriv->supp_regulatory_class,
				   pmpriv->supp_regulatory_class_len,
				   sizeof(pRegulatory_class->data));
		} else {
			pRegulatory_class->ieee_hdr.len =
				sizeof(regulatory_class);
			memcpy_ext(pmadapter, pRegulatory_class->data,
				   regulatory_class, sizeof(regulatory_class),
				   sizeof(pRegulatory_class->data));
		}
		DBG_HEXDUMP(MCMD_D, "TDLS supported regulatory class",
			    tdls_ies->regulatory_class,
			    pRegulatory_class->ieee_hdr.len +
			    sizeof(IEEEtypes_Header_t));
	}
	LEAVE();
	return ret;
}

/**
 *  @brief Set mimo switch configuration
 *
 *  @param pmadapter    A pointer to mlan_adapter structure
 *  @param pioctl_req   A pointer to ioctl request buffer
 *
 *  @return             MLAN_STATUS_PENDING --success, otherwise fail
 */
mlan_status
wlan_radio_ioctl_mimo_switch_cfg(pmlan_adapter pmadapter,
				 pmlan_ioctl_req pioctl_req)
{
	pmlan_private pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_ds_radio_cfg *radio_cfg = (mlan_ds_radio_cfg *)pioctl_req->pbuf;
	mlan_status ret = MLAN_STATUS_SUCCESS;

	ENTER();

	/* Send request to firmware */
	ret = wlan_prepare_cmd(pmpriv, HostCmd_CMD_802_11_MIMO_SWITCH, 0, 0,
			       (t_void *)pioctl_req,
			       &(radio_cfg->param.mimo_switch_cfg));

	if (ret == MLAN_STATUS_SUCCESS)
		ret = MLAN_STATUS_PENDING;

	LEAVE();
	return ret;
}

/**
 *  @brief Get extended version information
 *
 *  @param pmadapter    A pointer to mlan_adapter structure
 *  @param pioctl_req   A pointer to ioctl request buffer
 *
 *  @return             MLAN_STATUS_PENDING --success, otherwise fail
 */
mlan_status
wlan_get_info_ver_ext(pmlan_adapter pmadapter, pmlan_ioctl_req pioctl_req)
{
	pmlan_private pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_ds_get_info *pinfo = (mlan_ds_get_info *)pioctl_req->pbuf;
	mlan_status ret = MLAN_STATUS_SUCCESS;

	ENTER();

	/* Send request to firmware */
	ret = wlan_prepare_cmd(pmpriv, HostCmd_CMD_VERSION_EXT,
			       HostCmd_ACT_GEN_GET, 0, (t_void *)pioctl_req,
			       &pinfo->param.ver_ext.version_str_sel);

	if (ret == MLAN_STATUS_SUCCESS)
		ret = MLAN_STATUS_PENDING;

	LEAVE();
	return ret;
}

/**
 *  @brief Set/Get link layer statistics
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_PENDING --success, otherwise fail
 */
mlan_status
wlan_ioctl_link_statistic(mlan_private *pmpriv, pmlan_ioctl_req pioctl_req)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	t_u16 cmd_action = 0;

	ENTER();

	/* Check buffer length of MLAN IOCTL */
	if (pioctl_req->buf_len < sizeof(mlan_ds_get_stats)) {
		PRINTM(MWARN,
		       "MLAN IOCTL information buffer length is too short.\n");
		pioctl_req->data_read_written = 0;
		pioctl_req->buf_len_needed = sizeof(mlan_ds_get_stats);
		pioctl_req->status_code = MLAN_ERROR_INVALID_PARAMETER;
		ret = MLAN_STATUS_RESOURCE;
		goto exit;
	}

	switch (pioctl_req->action) {
	case MLAN_ACT_GET:
		cmd_action = HostCmd_ACT_GEN_GET;
		break;
	case MLAN_ACT_SET:
		cmd_action = HostCmd_ACT_GEN_SET;
		break;
	case MLAN_ACT_CLEAR:
		cmd_action = HostCmd_ACT_GEN_REMOVE;
		break;
	default:
		ret = MLAN_STATUS_FAILURE;
		goto exit;
	}

	/* Send request to firmware */
	ret = wlan_prepare_cmd(pmpriv, HostCmd_CMD_802_11_LINK_STATS,
			       cmd_action, 0, (t_void *)pioctl_req, MNULL);

	if (ret == MLAN_STATUS_SUCCESS)
		ret = MLAN_STATUS_PENDING;

exit:
	LEAVE();
	return ret;
}

/**
 *  @brief Get TX/RX histogram statistic
 *
 *  @param pmadapter    A pointer to mlan_adapter structure
 *  @param pioctl_req   A pointer to ioctl request buffer
 *
 *  @return             MLAN_STATUS_PENDING --success, otherwise fail
 */
mlan_status
wlan_get_tx_rx_histogram(pmlan_adapter pmadapter, pmlan_ioctl_req pioctl_req)
{
	pmlan_private pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_ds_misc_cfg *pmisc = (mlan_ds_misc_cfg *)pioctl_req->pbuf;
	mlan_status ret = MLAN_STATUS_SUCCESS;

	ENTER();

	/* Send request to firmware */
	ret = wlan_prepare_cmd(pmpriv, HOST_CMD_TX_RX_PKT_STATS,
			       HostCmd_ACT_GEN_GET, 0, (t_void *)pioctl_req,
			       &(pmisc->param.tx_rx_histogram));

	if (ret == MLAN_STATUS_SUCCESS)
		ret = MLAN_STATUS_PENDING;

	LEAVE();
	return ret;
}

#ifdef DEBUG_LEVEL1
/**
 *  @brief Set driver debug bit masks in order to enhance performance
 *
 *  @param pmadapter    A pointer to mlan_adapter structure
 *  @param pioctl_req   A pointer to ioctl request buffer
 *
 *  @return             MLAN_STATUS_PENDING --success, otherwise fail
 */
mlan_status
wlan_set_drvdbg(pmlan_adapter pmadapter, pmlan_ioctl_req pioctl_req)
{
	mlan_ds_misc_cfg *misc = (mlan_ds_misc_cfg *)pioctl_req->pbuf;
	mlan_status ret = MLAN_STATUS_SUCCESS;

	ENTER();

	/* Set driver debug bit masks */
	mlan_drvdbg = misc->param.drvdbg;

	LEAVE();
	return ret;
}
#endif

/**
 *  @brief Rx mgmt frame forward register
 *
 *  @param pmadapter    A pointer to mlan_adapter structure
 *  @param pioctl_req   A pointer to ioctl request buffer
 *
 *  @return             MLAN_STATUS_PENDING --success, otherwise fail
 */
mlan_status
wlan_reg_rx_mgmt_ind(pmlan_adapter pmadapter, pmlan_ioctl_req pioctl_req)
{
	pmlan_private pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_ds_misc_cfg *misc = (mlan_ds_misc_cfg *)pioctl_req->pbuf;
	mlan_status ret = MLAN_STATUS_SUCCESS;

	ENTER();

	/* Set passthru mask for mgmt frame */
	pmpriv->mgmt_frame_passthru_mask = misc->param.mgmt_subtype_mask;

	/* Send request to firmware */
	ret = wlan_prepare_cmd(pmpriv, HostCmd_CMD_RX_MGMT_IND,
			       pioctl_req->action, 0, (t_void *)pioctl_req,
			       &misc->param.mgmt_subtype_mask);

	if (ret == MLAN_STATUS_SUCCESS)
		ret = MLAN_STATUS_PENDING;

	LEAVE();
	return ret;
}

/**
 *   @brief This function processes the 802.11 mgmt Frame
 *
 *   @param priv            A pointer to mlan_private
 *
 *   @param payload         A pointer to the received buffer
 *   @param payload_len     Length of the received buffer
 *   @param prx_pd          A pointer to RxPD
 *
 *   @return                MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
mlan_status
wlan_process_802dot11_mgmt_pkt(mlan_private *priv,
			       t_u8 *payload, t_u32 payload_len, RxPD *prx_pd)
{
	pmlan_adapter pmadapter = priv->adapter;
	pmlan_callbacks pcb = &pmadapter->callbacks;
	mlan_status ret = MLAN_STATUS_SUCCESS;
	wlan_802_11_header *pieee_pkt_hdr = MNULL;
	t_u16 sub_type = 0;
	t_u8 *event_buf = MNULL;
	mlan_event *pevent = MNULL;
	t_u8 unicast = 0;
	t_u8 broadcast[] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
	IEEE80211_MGMT *mgmt = MNULL;
	t_u8 category = 0;
	t_u8 action_code = 0;
#ifdef UAP_SUPPORT
	sta_node *sta_ptr = MNULL;
	MrvlIETypes_MgmtFrameSet_t *tlv;
	pmlan_buffer pmbuf;
#endif

	ENTER();
	if (payload_len > (MAX_EVENT_SIZE - sizeof(mlan_event))) {
		PRINTM(MERROR, "Dropping large mgmt frame,len =%d\n",
		       payload_len);
		LEAVE();
		return ret;
	}
	/* Check  packet type-subtype and compare with mgmt_passthru_mask
	 * If event is needed to host, just eventify it */
	pieee_pkt_hdr = (wlan_802_11_header *)payload;
	sub_type = IEEE80211_GET_FC_MGMT_FRAME_SUBTYPE(pieee_pkt_hdr->frm_ctl);
	if (((1 << sub_type) & priv->mgmt_frame_passthru_mask) == 0) {
		PRINTM(MINFO, "Dropping mgmt frame for subtype %d snr=%d.\n",
		       sub_type, prx_pd->snr);
		LEAVE();
		return ret;
	}
	switch (sub_type) {
	case SUBTYPE_ASSOC_REQUEST:
	case SUBTYPE_REASSOC_REQUEST:
#ifdef UAP_SUPPORT
		if (priv->uap_host_based & UAP_FLAG_HOST_MLME) {
			PRINTM_NETINTF(MMSG, priv);
			if (!memcmp(pmadapter, pieee_pkt_hdr->addr3,
				    priv->curr_addr, MLAN_MAC_ADDR_LENGTH)) {
				PRINTM(MMSG,
				       "wlan: HostMlme MICRO_AP_STA_ASSOC "
				       MACSTR "\n",
				       MAC2STR(pieee_pkt_hdr->addr2));
				mgmt = (IEEE80211_MGMT *)payload;
				sta_ptr =
					wlan_add_station_entry(priv,
							       pieee_pkt_hdr->
							       addr2);
				if (sta_ptr) {
					sta_ptr->capability =
						wlan_le16_to_cpu(mgmt->u.
								 assoc_req.
								 capab_info);
					pmbuf = wlan_alloc_mlan_buffer
						(pmadapter, payload_len, 0,
						 MOAL_MALLOC_BUFFER);
					if (pmbuf) {
						PRINTM(MCMND,
						       "check sta capability\n");
						pmbuf->data_len =
							ASSOC_EVENT_FIX_SIZE;
						tlv = (MrvlIETypes_MgmtFrameSet_t *)(pmbuf->pbuf + pmbuf->data_offset + pmbuf->data_len);
						tlv->type =
							wlan_cpu_to_le16
							(TLV_TYPE_MGMT_FRAME);
						tlv->len =
							sizeof
							(IEEEtypes_FrameCtl_t);
						memcpy_ext(pmadapter,
							   (t_u8 *)&tlv->
							   frame_control,
							   &pieee_pkt_hdr->
							   frm_ctl,
							   sizeof
							   (IEEEtypes_FrameCtl_t),
							   sizeof
							   (IEEEtypes_FrameCtl_t));
						pmbuf->data_len +=
							sizeof
							(MrvlIETypes_MgmtFrameSet_t);
						memcpy_ext(pmadapter,
							   pmbuf->pbuf +
							   pmbuf->data_offset +
							   pmbuf->data_len,
							   payload +
							   sizeof
							   (wlan_802_11_header),
							   payload_len -
							   sizeof
							   (wlan_802_11_header),
							   payload_len -
							   sizeof
							   (wlan_802_11_header));
						pmbuf->data_len +=
							payload_len -
							sizeof
							(wlan_802_11_header);
						tlv->len +=
							payload_len -
							sizeof
							(wlan_802_11_header);
						tlv->len =
							wlan_cpu_to_le16(tlv->
									 len);
						DBG_HEXDUMP(MCMD_D, "assoc_req",
							    pmbuf->pbuf +
							    pmbuf->data_offset,
							    pmbuf->data_len);
						wlan_check_sta_capability(priv,
									  pmbuf,
									  sta_ptr);
						wlan_free_mlan_buffer(pmadapter,
								      pmbuf);
					}
				}
			} else {
				PRINTM(MMSG,
				       "wlan: Drop MICRO_AP_STA_ASSOC " MACSTR
				       " from unknown BSSID " MACSTR "\n",
				       MAC2STR(pieee_pkt_hdr->addr2),
				       MAC2STR(pieee_pkt_hdr->addr3));
			}
		}
		unicast = MTRUE;
		break;
#endif
	case SUBTYPE_AUTH:
		unicast = MTRUE;
		PRINTM_NETINTF(MMSG, priv);
		PRINTM(MMSG, "wlan: HostMlme Auth received from " MACSTR "\n",
		       MAC2STR(pieee_pkt_hdr->addr2));
		break;
	case SUBTYPE_PROBE_RESP:
		unicast = MTRUE;
		break;
	case SUBTYPE_DISASSOC:
	case SUBTYPE_DEAUTH:
		if (memcmp(pmadapter, pieee_pkt_hdr->addr1, broadcast,
			   MLAN_MAC_ADDR_LENGTH))
			unicast = MTRUE;
#ifdef UAP_SUPPORT
		if (priv->uap_host_based & UAP_FLAG_HOST_MLME) {
			if (!memcmp(pmadapter, pieee_pkt_hdr->addr3,
				    priv->curr_addr, MLAN_MAC_ADDR_LENGTH)) {
				PRINTM_NETINTF(MMSG, priv);
				PRINTM(MMSG,
				       "wlan: HostMlme Deauth Receive from "
				       MACSTR "\n",
				       MAC2STR(pieee_pkt_hdr->addr2));
			}
		}
#endif
		if (priv->bss_role == MLAN_BSS_ROLE_STA) {
			if (priv->curr_bss_params.host_mlme) {
				if (memcmp(pmadapter, pieee_pkt_hdr->addr3,
					   (t_u8 *)priv->curr_bss_params.
					   bss_descriptor.mac_address,
					   MLAN_MAC_ADDR_LENGTH)) {
					PRINTM(MCMND,
					       "Dropping Deauth frame from other bssid: type=%d "
					       MACSTR "\n", sub_type,
					       MAC2STR(pieee_pkt_hdr->addr3));
					LEAVE();
					return ret;
				}
				PRINTM_NETINTF(MMSG, priv);
				PRINTM(MMSG,
				       "wlan: HostMlme Disconnected: sub_type=%d\n",
				       sub_type);
				pmadapter->pending_disconnect_priv = priv;
				wlan_recv_event(priv,
						MLAN_EVENT_ID_DRV_DEFER_HANDLING,
						MNULL);
			}
		}
		break;
	case SUBTYPE_ACTION:
		category = *(payload + sizeof(wlan_802_11_header));
		action_code = *(payload + sizeof(wlan_802_11_header) + 1);
		if (category == IEEE_MGMT_ACTION_CATEGORY_BLOCK_ACK) {
			PRINTM(MINFO,
			       "Drop BLOCK ACK action frame: action_code=%d\n",
			       action_code);
			LEAVE();
			return ret;
		}
		if ((category == IEEE_MGMT_ACTION_CATEGORY_PUBLIC) &&
		    (action_code == BSS_20_40_COEX)) {
			PRINTM(MINFO,
			       "Drop 20/40 BSS Coexistence Management frame\n");
			LEAVE();
			return ret;
		}
		if ((category == CATEGORY_PUBLIC) &&
		    (action_code == TDLS_DISCOVERY_RESPONSE)) {
			pcb->moal_updata_peer_signal(pmadapter->pmoal_handle,
						     priv->bss_index,
						     pieee_pkt_hdr->addr2,
						     prx_pd->snr, prx_pd->nf);
			PRINTM(MINFO,
			       "Rx: TDLS discovery response, nf=%d, snr=%d\n",
			       prx_pd->nf, prx_pd->snr);
		}
		if (memcmp(pmadapter, pieee_pkt_hdr->addr1, broadcast,
			   MLAN_MAC_ADDR_LENGTH))
			unicast = MTRUE;
		break;
	default:
		break;
	}
	if (unicast == MTRUE) {
		if (memcmp(pmadapter, pieee_pkt_hdr->addr1, priv->curr_addr,
			   MLAN_MAC_ADDR_LENGTH)) {
			PRINTM(MINFO,
			       "Dropping mgmt frame for others: type=%d " MACSTR
			       "\n", sub_type, MAC2STR(pieee_pkt_hdr->addr1));
			LEAVE();
			return ret;
		}
	}
	/* Allocate memory for event buffer */
	ret = pcb->moal_malloc(pmadapter->pmoal_handle, MAX_EVENT_SIZE,
			       MLAN_MEM_DEF, &event_buf);
	if ((ret != MLAN_STATUS_SUCCESS) || !event_buf) {
		PRINTM(MERROR, "Could not allocate buffer for event buf\n");
		LEAVE();
		return MLAN_STATUS_FAILURE;
	}
	pevent = (pmlan_event)event_buf;
	pevent->bss_index = priv->bss_index;
	mgmt = (IEEE80211_MGMT *)payload;
	if (!priv->curr_bss_params.host_mlme &&
	    sub_type == SUBTYPE_ACTION &&
	    mgmt->u.ft_resp.category == FT_CATEGORY &&
	    mgmt->u.ft_resp.action == FT_ACTION_RESPONSE &&
	    mgmt->u.ft_resp.status_code == 0) {
		PRINTM(MCMND, "FT Action response received\n");
#define FT_ACTION_HEAD_LEN (24 + 6 + 16)
		pevent->event_id = MLAN_EVENT_ID_DRV_FT_RESPONSE;
		pevent->event_len =
			payload_len + MLAN_MAC_ADDR_LENGTH - FT_ACTION_HEAD_LEN;
		memcpy_ext(pmadapter, (t_u8 *)pevent->event_buf,
			   &mgmt->u.ft_resp.target_ap_addr,
			   MLAN_MAC_ADDR_LENGTH, MLAN_MAC_ADDR_LENGTH);
		memcpy_ext(pmadapter,
			   (t_u8 *)(pevent->event_buf + MLAN_MAC_ADDR_LENGTH),
			   payload + FT_ACTION_HEAD_LEN,
			   payload_len - FT_ACTION_HEAD_LEN,
			   pevent->event_len - MLAN_MAC_ADDR_LENGTH);
	} else if (!priv->curr_bss_params.host_mlme &&
		   sub_type == SUBTYPE_AUTH &&
		   mgmt->u.auth.auth_alg == MLAN_AUTH_MODE_FT &&
		   mgmt->u.auth.auth_transaction == 2 &&
		   mgmt->u.auth.status_code == 0) {
		PRINTM(MCMND, "FT auth response received \n");
#define AUTH_PACKET_LEN (24 + 6 + 6)
		pevent->event_id = MLAN_EVENT_ID_DRV_FT_RESPONSE;
		pevent->event_len =
			payload_len + MLAN_MAC_ADDR_LENGTH - AUTH_PACKET_LEN;
		memcpy_ext(pmadapter, (t_u8 *)pevent->event_buf, mgmt->sa,
			   MLAN_MAC_ADDR_LENGTH, MLAN_MAC_ADDR_LENGTH);
		memcpy_ext(pmadapter,
			   (t_u8 *)(pevent->event_buf + MLAN_MAC_ADDR_LENGTH),
			   payload + AUTH_PACKET_LEN,
			   payload_len - AUTH_PACKET_LEN,
			   pevent->event_len - MLAN_MAC_ADDR_LENGTH);
	} else {
		pevent->event_id = MLAN_EVENT_ID_DRV_MGMT_FRAME;
		pevent->event_len = payload_len + sizeof(pevent->event_id);
		memcpy_ext(pmadapter, (t_u8 *)pevent->event_buf,
			   (t_u8 *)&pevent->event_id, sizeof(pevent->event_id),
			   pevent->event_len);
		memcpy_ext(pmadapter,
			   (t_u8 *)(pevent->event_buf +
				    sizeof(pevent->event_id)), payload,
			   payload_len, payload_len);
	}
	wlan_recv_event(priv, pevent->event_id, pevent);
	if (event_buf)
		pcb->moal_mfree(pmadapter->pmoal_handle, event_buf);
	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

#ifdef STA_SUPPORT
/**
 *  @brief Extended capabilities configuration
 *
 *  @param pmadapter    A pointer to mlan_adapter structure
 *  @param pioctl_req   A pointer to ioctl request buffer
 *
 *  @return             MLAN_STATUS_PENDING --success, otherwise fail
 */
mlan_status
wlan_misc_ext_capa_cfg(pmlan_adapter pmadapter, pmlan_ioctl_req pioctl_req)
{
	pmlan_private pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_ds_misc_cfg *misc = (mlan_ds_misc_cfg *)pioctl_req->pbuf;
	mlan_status ret = MLAN_STATUS_SUCCESS;

	ENTER();

	if (MLAN_ACT_GET == pioctl_req->action)
		memcpy_ext(pmpriv->adapter, &misc->param.ext_cap,
			   &pmpriv->def_ext_cap, sizeof(misc->param.ext_cap),
			   sizeof(misc->param.ext_cap));
	else if (MLAN_ACT_SET == pioctl_req->action) {
		memcpy_ext(pmpriv->adapter, &pmpriv->ext_cap,
			   &misc->param.ext_cap, sizeof(misc->param.ext_cap),
			   sizeof(pmpriv->ext_cap));
		/* Save default Extended Capability */
		memcpy_ext(pmpriv->adapter, &pmpriv->def_ext_cap,
			   &pmpriv->ext_cap, sizeof(pmpriv->ext_cap),
			   sizeof(pmpriv->def_ext_cap));
		if (pmpriv->config_bands & BAND_AAC)
			SET_EXTCAP_OPERMODENTF(pmpriv->ext_cap);
	}

	LEAVE();
	return ret;
}

/**
 *  @brief Check whether Extended Capabilities IE support
 *
 *  @param pmpriv             A pointer to mlan_private structure
 *
 *  @return                   MTRUE or MFALSE;
 */
t_u32
wlan_is_ext_capa_support(mlan_private *pmpriv)
{
	ENTER();

	if (ISSUPP_EXTCAP_TDLS(pmpriv->ext_cap) ||
	    ISSUPP_EXTCAP_INTERWORKING(pmpriv->ext_cap) ||
	    ISSUPP_EXTCAP_BSS_TRANSITION(pmpriv->ext_cap)
	    || ISSUPP_EXTCAP_QOS_MAP(pmpriv->ext_cap)
	    || ISSUPP_EXTCAP_OPERMODENTF(pmpriv->ext_cap)
		) {
		LEAVE();
		return MTRUE;
	} else {
		LEAVE();
		return MFALSE;
	}
}
#endif

/**
 *  @brief Set hotspot enable/disable
 *
 *  @param pmadapter    A pointer to mlan_adapter structure
 *  @param pioctl_req   A pointer to ioctl request buffer
 *
 *  @return             MLAN_STATUS_PENDING --success, otherwise fail
 */
mlan_status
wlan_misc_hotspot_cfg(pmlan_adapter pmadapter, pmlan_ioctl_req pioctl_req)
{
	pmlan_private pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_ds_misc_cfg *misc = (mlan_ds_misc_cfg *)pioctl_req->pbuf;
	mlan_status ret = MLAN_STATUS_SUCCESS;

	ENTER();

	if (MLAN_ACT_GET == pioctl_req->action)
		misc->param.hotspot_cfg = pmpriv->hotspot_cfg;
	else if (MLAN_ACT_SET == pioctl_req->action)
		pmpriv->hotspot_cfg = misc->param.hotspot_cfg;

	LEAVE();
	return ret;
}

#ifdef STA_SUPPORT
/**
 *  @brief Add Extended Capabilities IE
 *
 *  @param pmpriv             A pointer to mlan_private structure
 *  @param pbss_desc          A pointer to BSSDescriptor_t structure
 *  @param pptlv_out          A pointer to TLV to fill in
 *
 *  @return                   N/A
 */
void
wlan_add_ext_capa_info_ie(mlan_private *pmpriv,
			  BSSDescriptor_t *pbss_desc, t_u8 **pptlv_out)
{
	MrvlIETypes_ExtCap_t *pext_cap = MNULL;

	ENTER();

	pext_cap = (MrvlIETypes_ExtCap_t *)*pptlv_out;
	memset(pmpriv->adapter, pext_cap, 0, sizeof(MrvlIETypes_ExtCap_t));
	pext_cap->header.type = wlan_cpu_to_le16(EXT_CAPABILITY);
	pext_cap->header.len = wlan_cpu_to_le16(sizeof(ExtCap_t));
	if (pmpriv->adapter->ecsa_enable)
		SET_EXTCAP_EXT_CHANNEL_SWITCH(pmpriv->ext_cap);
	else
		RESET_EXTCAP_EXT_CHANNEL_SWITCH(pmpriv->ext_cap);
	if (pbss_desc && pbss_desc->multi_bssid_ap)
		SET_EXTCAP_MULTI_BSSID(pmpriv->ext_cap);
	if (wlan_check_11ax_twt_supported(pmpriv, pbss_desc))
		SET_EXTCAP_TWT_REQ(pmpriv->ext_cap);
	memcpy_ext(pmpriv->adapter, &pext_cap->ext_cap, &pmpriv->ext_cap,
		   sizeof(pmpriv->ext_cap), sizeof(pext_cap->ext_cap));
	*pptlv_out += sizeof(MrvlIETypes_ExtCap_t);

	LEAVE();
}
#endif

/**
 *  @brief Get OTP user data
 *
 *  @param pmadapter    A pointer to mlan_adapter structure
 *  @param pioctl_req   A pointer to ioctl request buffer
 *
 *  @return             MLAN_STATUS_PENDING --success, otherwise fail
 */
mlan_status
wlan_misc_otp_user_data(pmlan_adapter pmadapter, pmlan_ioctl_req pioctl_req)
{
	pmlan_private pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_ds_misc_cfg *misc = (mlan_ds_misc_cfg *)pioctl_req->pbuf;
	mlan_status ret = MLAN_STATUS_FAILURE;

	ENTER();

	if (misc->param.otp_user_data.user_data_length > MAX_OTP_USER_DATA_LEN) {
		PRINTM(MERROR, "Invalid OTP user data length\n");
		pioctl_req->status_code = MLAN_ERROR_INVALID_PARAMETER;
		LEAVE();
		return ret;
	}

	ret = wlan_prepare_cmd(pmpriv, HostCmd_CMD_OTP_READ_USER_DATA,
			       HostCmd_ACT_GEN_GET, 0, (t_void *)pioctl_req,
			       &misc->param.otp_user_data);

	if (ret == MLAN_STATUS_SUCCESS)
		ret = MLAN_STATUS_PENDING;

	LEAVE();
	return ret;
}

/**
 *  @brief This function will search for the specific ie
 *
 *  @param priv    A pointer to mlan_private
 *  @param pevent  A pointer to event buf
 *  @param sta_ptr A pointer to sta_node
 *
 *  @return	       N/A
 */
void
wlan_check_sta_capability(pmlan_private priv, pmlan_buffer pevent,
			  sta_node *sta_ptr)
{
	t_u16 tlv_type, tlv_len;
	t_u16 frame_control, frame_sub_type = 0;
	t_u8 *assoc_req_ie = MNULL;
	t_u8 ie_len = 0, assoc_ie_len = 0;
	IEEEtypes_HTCap_t *pht_cap = MNULL;
	IEEEtypes_VHTCap_t *pvht_cap = MNULL;
	IEEEtypes_Extension_t *phe_cap = MNULL;
#ifdef UAP_SUPPORT
	t_u8 *ext_rate = MNULL, *erp = MNULL;
#endif

	int tlv_buf_left = pevent->data_len - ASSOC_EVENT_FIX_SIZE;
	MrvlIEtypesHeader_t *tlv =
		(MrvlIEtypesHeader_t *)(pevent->pbuf + pevent->data_offset +
					ASSOC_EVENT_FIX_SIZE);
	MrvlIETypes_MgmtFrameSet_t *mgmt_tlv = MNULL;

	ENTER();
	while (tlv_buf_left >= (int)sizeof(MrvlIEtypesHeader_t)) {
		tlv_type = wlan_le16_to_cpu(tlv->type);
		tlv_len = wlan_le16_to_cpu(tlv->len);
		if ((sizeof(MrvlIEtypesHeader_t) + tlv_len) >
		    (unsigned int)tlv_buf_left) {
			PRINTM(MERROR, "wrong tlv: tlvLen=%d, tlvBufLeft=%d\n",
			       tlv_len, tlv_buf_left);
			break;
		}
		if (tlv_type == TLV_TYPE_MGMT_FRAME) {
			mgmt_tlv = (MrvlIETypes_MgmtFrameSet_t *)tlv;
			memcpy_ext(priv->adapter, &frame_control,
				   (t_u8 *)&(mgmt_tlv->frame_control),
				   sizeof(frame_control),
				   sizeof(frame_control));
			frame_sub_type =
				IEEE80211_GET_FC_MGMT_FRAME_SUBTYPE
				(frame_control);
			if ((mgmt_tlv->frame_control.type == 0) &&
			    ((frame_sub_type == SUBTYPE_BEACON)
#ifdef UAP_SUPPORT
			     || (frame_sub_type == SUBTYPE_ASSOC_REQUEST) ||
			     (frame_sub_type == SUBTYPE_REASSOC_REQUEST)
#endif
			    )) {
				if (frame_sub_type == SUBTYPE_BEACON)
					assoc_ie_len =
						sizeof(IEEEtypes_Beacon_t);
#ifdef UAP_SUPPORT
				else if (frame_sub_type ==
					 SUBTYPE_ASSOC_REQUEST)
					assoc_ie_len =
						sizeof(IEEEtypes_AssocRqst_t);
				else if (frame_sub_type ==
					 SUBTYPE_REASSOC_REQUEST)
					assoc_ie_len =
						sizeof(IEEEtypes_ReAssocRqst_t);
#endif
				ie_len = tlv_len -
					sizeof(IEEEtypes_FrameCtl_t) -
					assoc_ie_len;
				assoc_req_ie =
					(t_u8 *)tlv +
					sizeof(MrvlIETypes_MgmtFrameSet_t) +
					assoc_ie_len;
				sta_ptr->is_wmm_enabled =
					wlan_is_wmm_ie_present(priv->adapter,
							       assoc_req_ie,
							       ie_len);
				PRINTM(MCMND, "STA: is_wmm_enabled=%d\n",
				       sta_ptr->is_wmm_enabled);
				pht_cap = (IEEEtypes_HTCap_t *)
					wlan_get_specific_ie(priv, assoc_req_ie,
							     ie_len,
							     HT_CAPABILITY, 0);
				if (pht_cap) {
					PRINTM(MCMND, "STA supports 11n\n");
					sta_ptr->is_11n_enabled = MTRUE;
					memcpy_ext(priv->adapter,
						   (t_u8 *)&sta_ptr->HTcap,
						   pht_cap,
						   sizeof(IEEEtypes_HTCap_t),
						   sizeof(IEEEtypes_HTCap_t));
					if (GETHT_MAXAMSDU
					    (wlan_le16_to_cpu
					     (pht_cap->ht_cap.ht_cap_info)))
						sta_ptr->max_amsdu =
							MLAN_TX_DATA_BUF_SIZE_8K;
					else
						sta_ptr->max_amsdu =
							MLAN_TX_DATA_BUF_SIZE_4K;
				} else {
					PRINTM(MCMND,
					       "STA doesn't support 11n\n");
				}
				pvht_cap = (IEEEtypes_VHTCap_t *)
					wlan_get_specific_ie(priv, assoc_req_ie,
							     ie_len,
							     VHT_CAPABILITY, 0);
				if (pvht_cap &&
				    (priv->is_11ac_enabled == MTRUE)) {
					PRINTM(MCMND, "STA supports 11ac\n");
					sta_ptr->is_11ac_enabled = MTRUE;
					if (GET_VHTCAP_MAXMPDULEN
					    (wlan_le32_to_cpu
					     (pvht_cap->vht_cap.
					      vht_cap_info)) == 2)
						sta_ptr->max_amsdu =
							MLAN_TX_DATA_BUF_SIZE_12K;
					else if (GET_VHTCAP_MAXMPDULEN
						 (wlan_le32_to_cpu
						  (pvht_cap->vht_cap.
						   vht_cap_info)) == 1)
						sta_ptr->max_amsdu =
							MLAN_TX_DATA_BUF_SIZE_8K;
					else
						sta_ptr->max_amsdu =
							MLAN_TX_DATA_BUF_SIZE_4K;
				} else {
					PRINTM(MCMND,
					       "STA doesn't support 11ac\n");
				}
				phe_cap = (IEEEtypes_Extension_t *)
					wlan_get_specific_ie(priv, assoc_req_ie,
							     ie_len, EXTENSION,
							     HE_CAPABILITY);
				if (phe_cap && (priv->is_11ax_enabled == MTRUE)) {
					PRINTM(MCMND, "STA supports 11ax\n");
					sta_ptr->is_11ax_enabled = MTRUE;
					memcpy_ext(priv->adapter,
						   (t_u8 *)&sta_ptr->he_cap,
						   phe_cap,
						   phe_cap->ieee_hdr.len +
						   sizeof(IEEEtypes_Header_t),
						   sizeof(IEEEtypes_HECap_t));
					sta_ptr->he_cap.ieee_hdr.len =
						MIN(phe_cap->ieee_hdr.len,
						    sizeof(IEEEtypes_HECap_t) -
						    sizeof(IEEEtypes_Header_t));
				} else {
					PRINTM(MCMND,
					       "STA doesn't support 11ax\n");
				}
#ifdef UAP_SUPPORT
				/* Note: iphone6 does not have ERP_INFO */
				ext_rate =
					wlan_get_specific_ie(priv, assoc_req_ie,
							     ie_len,
							     EXTENDED_SUPPORTED_RATES,
							     0);
				erp = wlan_get_specific_ie(priv, assoc_req_ie,
							   ie_len, ERP_INFO, 0);
				if (!ext_rate)
					PRINTM(MCMND,
					       "STA doesn't support EXTENDED_SUPPORTED_RATES\n");
				if (!erp)
					PRINTM(MCMND,
					       "STA doesn't support ERP_INFO\n");
				if (sta_ptr->is_11ax_enabled) {
					if (priv->uap_channel <= 14)
						sta_ptr->bandmode = BAND_GAX;
					else
						sta_ptr->bandmode = BAND_AAX;
				} else if (sta_ptr->is_11ac_enabled) {
					if (priv->uap_channel <= 14)
						sta_ptr->bandmode = BAND_GAC;
					else
						sta_ptr->bandmode = BAND_AAC;
				} else if (sta_ptr->is_11n_enabled) {
					if (priv->uap_channel <= 14)
						sta_ptr->bandmode = BAND_GN;
					else
						sta_ptr->bandmode = BAND_AN;
				} else if (ext_rate || erp) {
					if (priv->uap_channel <= 14)
						sta_ptr->bandmode = BAND_G;
					else
						sta_ptr->bandmode = BAND_A;
				} else
					sta_ptr->bandmode = BAND_B;
#endif
#ifdef DRV_EMBEDDED_AUTHENTICATOR
				if (IsAuthenticatorEnabled(priv->psapriv))
					authenticator_get_sta_security_info
						(priv->psapriv,
						 sta_ptr->cm_connectioninfo,
						 assoc_req_ie, ie_len);
#endif
				break;
			}
		}
		tlv_buf_left -= (sizeof(MrvlIEtypesHeader_t) + tlv_len);
		tlv = (MrvlIEtypesHeader_t *)((t_u8 *)tlv + tlv_len +
					      sizeof(MrvlIEtypesHeader_t));
	}
	LEAVE();

	return;
}

/**
 *  @brief check if WMM ie present.
 *
 *  @param pmadapter A pointer to mlan_adapter structure
 *  @param pbuf     A pointer to IE buffer
 *  @param buf_len  IE buffer len
 *
 *  @return         MTRUE/MFALSE
 */
t_u8
wlan_is_wmm_ie_present(pmlan_adapter pmadapter, t_u8 *pbuf, t_u16 buf_len)
{
	t_u16 bytes_left = buf_len;
	IEEEtypes_ElementId_e element_id;
	t_u8 *pcurrent_ptr = pbuf;
	t_u8 element_len;
	t_u16 total_ie_len;
	IEEEtypes_VendorSpecific_t *pvendor_ie;
	const t_u8 wmm_oui[4] = { 0x00, 0x50, 0xf2, 0x02 };
	t_u8 find_wmm_ie = MFALSE;

	ENTER();

	/* Process variable IE */
	while (bytes_left >= 2) {
		element_id = (IEEEtypes_ElementId_e)(*((t_u8 *)pcurrent_ptr));
		element_len = *((t_u8 *)pcurrent_ptr + 1);
		total_ie_len = element_len + sizeof(IEEEtypes_Header_t);

		if (bytes_left < total_ie_len) {
			PRINTM(MERROR, "InterpretIE: Error in processing IE, "
			       "bytes left < IE length\n");
			bytes_left = 0;
			continue;
		}
		switch (element_id) {
		case VENDOR_SPECIFIC_221:
			pvendor_ie = (IEEEtypes_VendorSpecific_t *)pcurrent_ptr;
			if (!memcmp(pmadapter, pvendor_ie->vend_hdr.oui,
				    wmm_oui, sizeof(wmm_oui))) {
				find_wmm_ie = MTRUE;
				PRINTM(MINFO, "find WMM IE\n");
			}
			break;
		default:
			break;
		}
		pcurrent_ptr += element_len + 2;
		/* Need to account for IE ID and IE Len */
		bytes_left -= (element_len + 2);
		if (find_wmm_ie)
			break;
	}

	LEAVE();
	return find_wmm_ie;
}

/**
 *  @brief This function will search for the specific ie
 *
 *
 *  @param priv    A pointer to mlan_private
 *  @param ie_buf  A pointer to ie_buf
 *  @param ie_len  total ie length
 *  @param id      ie's id
 *  @param ext_id  ie's extension id
 *
 *  @return	       ie's poiner or MNULL
 */
t_u8 *
wlan_get_specific_ie(pmlan_private priv, t_u8 *ie_buf, t_u8 ie_len,
		     IEEEtypes_ElementId_e id, t_u8 ext_id)
{
	t_u32 bytes_left = ie_len;
	t_u8 *pcurrent_ptr = ie_buf;
	t_u16 total_ie_len;
	t_u8 *ie_ptr = MNULL;
	IEEEtypes_ElementId_e element_id;
	t_u8 element_len;
	t_u8 element_eid;

	ENTER();

	DBG_HEXDUMP(MDAT_D, "ie", ie_buf, ie_len);
	while (bytes_left >= 2) {
		element_id = (IEEEtypes_ElementId_e)(*((t_u8 *)pcurrent_ptr));
		element_len = *((t_u8 *)pcurrent_ptr + 1);
		element_eid = *((t_u8 *)pcurrent_ptr + 2);
		total_ie_len = element_len + sizeof(IEEEtypes_Header_t);
		if (bytes_left < total_ie_len) {
			PRINTM(MERROR, "InterpretIE: Error in processing IE, "
			       "bytes left < IE length\n");
			break;
		}
		if ((!ext_id && element_id == id) ||
		    (id == EXTENSION && element_id == id &&
		     ext_id == element_eid)) {
			PRINTM(MCMND, "Find IE: id=%d ext_id=%d\n", id, ext_id);
			DBG_HEXDUMP(MCMD_D, "IE", pcurrent_ptr, total_ie_len);
			ie_ptr = pcurrent_ptr;
			break;
		}
		pcurrent_ptr += element_len + 2;
		/* Need to account for IE ID and IE Len */
		bytes_left -= (element_len + 2);
	}

	LEAVE();

	return ie_ptr;
}

/**
 *  @brief Get pm info
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		        MLAN_STATUS_SUCCESS --success
 */
mlan_status
wlan_get_pm_info(pmlan_adapter pmadapter, pmlan_ioctl_req pioctl_req)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	mlan_ds_pm_cfg *pm_cfg = MNULL;

	ENTER();

	pm_cfg = (mlan_ds_pm_cfg *)pioctl_req->pbuf;
	pm_cfg->param.ps_info.is_suspend_allowed = MTRUE;
	wlan_request_cmd_lock(pmadapter);
	if (util_peek_list(pmadapter->pmoal_handle, &pmadapter->cmd_pending_q,
			   MNULL, MNULL) ||
	    pmadapter->curr_cmd || !wlan_bypass_tx_list_empty(pmadapter) ||
	    !wlan_wmm_lists_empty(pmadapter)
#if defined(SDIO) || defined(PCIE)
	    || wlan_pending_interrupt(pmadapter)
#endif
		) {
		pm_cfg->param.ps_info.is_suspend_allowed = MFALSE;
#if defined(SDIO) || defined(PCIE)
		PRINTM(MIOCTL,
		       "PM: cmd_pending_q=%p,curr_cmd=%p,wmm_list_empty=%d, by_pass=%d irq_pending=%d\n",
		       util_peek_list(pmadapter->pmoal_handle,
				      &pmadapter->cmd_pending_q, MNULL, MNULL),
		       pmadapter->curr_cmd, wlan_wmm_lists_empty(pmadapter),
		       wlan_bypass_tx_list_empty(pmadapter),
		       wlan_pending_interrupt(pmadapter));
#else
		PRINTM(MIOCTL,
		       "PM: cmd_pending_q=%p,curr_cmd=%p,wmm_list_empty=%d, by_pass=%d\n",
		       util_peek_list(pmadapter->pmoal_handle,
				      &pmadapter->cmd_pending_q, MNULL, MNULL),
		       pmadapter->curr_cmd, wlan_wmm_lists_empty(pmadapter),
		       wlan_bypass_tx_list_empty(pmadapter));
#endif
	}
	wlan_release_cmd_lock(pmadapter);
	LEAVE();
	return ret;
}

/**
 *  @brief Get hs wakeup reason
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		        MLAN_STATUS_SUCCESS --success
 */
mlan_status
wlan_get_hs_wakeup_reason(pmlan_adapter pmadapter, pmlan_ioctl_req pioctl_req)
{
	pmlan_private pmpriv = pmadapter->priv[pioctl_req->bss_index];
	pmlan_ds_pm_cfg pm_cfg = MNULL;
	mlan_status ret = MLAN_STATUS_FAILURE;

	ENTER();

	pm_cfg = (mlan_ds_pm_cfg *)pioctl_req->pbuf;

	/* Send command to firmware */
	ret = wlan_prepare_cmd(pmpriv, HostCmd_CMD_HS_WAKEUP_REASON,
			       HostCmd_ACT_GEN_GET, 0, (t_void *)pioctl_req,
			       &pm_cfg->param.wakeup_reason);

	if (ret == MLAN_STATUS_SUCCESS)
		ret = MLAN_STATUS_PENDING;

	LEAVE();
	return ret;
}

/**
 *  @brief Set/Get radio status
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_SUCCESS --success, otherwise fail
 */
mlan_status
wlan_radio_ioctl_radio_ctl(pmlan_adapter pmadapter, pmlan_ioctl_req pioctl_req)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	mlan_ds_radio_cfg *radio_cfg = MNULL;
	t_u16 cmd_action = 0;
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];

	ENTER();

	radio_cfg = (mlan_ds_radio_cfg *)pioctl_req->pbuf;
	if (pioctl_req->action == MLAN_ACT_SET) {
		if (pmadapter->radio_on == radio_cfg->param.radio_on_off) {
			ret = MLAN_STATUS_SUCCESS;
			goto exit;
		} else {
			if (pmpriv->media_connected == MTRUE) {
				ret = MLAN_STATUS_FAILURE;
				goto exit;
			}
			cmd_action = HostCmd_ACT_GEN_SET;
		}
	} else
		cmd_action = HostCmd_ACT_GEN_GET;

	/* Send request to firmware */
	ret = wlan_prepare_cmd(pmpriv, HostCmd_CMD_802_11_RADIO_CONTROL,
			       cmd_action, 0, (t_void *)pioctl_req,
			       &radio_cfg->param.radio_on_off);

	if (ret == MLAN_STATUS_SUCCESS)
		ret = MLAN_STATUS_PENDING;

exit:
	LEAVE();
	return ret;
}

/**
 *  @brief Set/Get antenna configuration
 *
 *  @param pmadapter    A pointer to mlan_adapter structure
 *  @param pioctl_req   A pointer to ioctl request buffer
 *
 *  @return     MLAN_STATUS_PENDING --success, otherwise fail
 */
mlan_status
wlan_radio_ioctl_ant_cfg(pmlan_adapter pmadapter, pmlan_ioctl_req pioctl_req)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_ds_radio_cfg *radio_cfg = MNULL;
	t_u16 cmd_action = 0;
	mlan_ds_ant_cfg *ant_cfg = MNULL;
	mlan_ds_ant_cfg_1x1 *ant_cfg_1x1 = MNULL;

	ENTER();

	radio_cfg = (mlan_ds_radio_cfg *)pioctl_req->pbuf;
	if (IS_STREAM_2X2(pmadapter->feature_control))
		ant_cfg = &radio_cfg->param.ant_cfg;

	if (pioctl_req->action == MLAN_ACT_SET) {
		/* User input validation */
		if (IS_STREAM_2X2(pmadapter->feature_control)) {
#if defined(PCIE9098) || defined(SD9098) || defined(USB9098) || defined(PCIE9097) || defined(SD9097) || defined(USB9097)
			if (IS_CARD9098(pmadapter->card_type) ||
			    IS_CARD9097(pmadapter->card_type)) {
				ant_cfg->tx_antenna &= 0x0303;
				ant_cfg->rx_antenna &= 0x0303;
				/** 2G antcfg TX */
				if (ant_cfg->tx_antenna & 0x00FF) {
					pmadapter->user_htstream &= ~0xF0;
					pmadapter->user_htstream |=
						(bitcount(ant_cfg->tx_antenna &
							  0x00FF)
						 << 4);
				}
				/* 5G antcfg tx */
				if (ant_cfg->tx_antenna & 0xFF00) {
					pmadapter->user_htstream &= ~0xF000;
					pmadapter->user_htstream |=
						(bitcount(ant_cfg->tx_antenna &
							  0xFF00)
						 << 12);
				}
				/* 2G antcfg RX */
				if (ant_cfg->rx_antenna & 0x00FF) {
					pmadapter->user_htstream &= ~0xF;
					pmadapter->user_htstream |=
						bitcount(ant_cfg->
							 rx_antenna & 0x00FF);
				}
				/* 5G antcfg RX */
				if (ant_cfg->rx_antenna & 0xFF00) {
					pmadapter->user_htstream &= ~0xF00;
					pmadapter->user_htstream |=
						(bitcount(ant_cfg->rx_antenna &
							  0xFF00)
						 << 8);
				}
				PRINTM(MCMND,
				       "user_htstream=0x%x, tx_antenna=0x%x >rx_antenna=0x%x\n",
				       pmadapter->user_htstream,
				       ant_cfg->tx_antenna,
				       ant_cfg->rx_antenna);
			} else {
#endif

				ant_cfg->tx_antenna &= 0x0003;
				ant_cfg->rx_antenna &= 0x0003;
#if defined(PCIE9098) || defined(SD9098) || defined(USB9098) || defined(PCIE9097) || defined(SD9097) || defined(USB9097)
			}
#endif
			if (!ant_cfg->tx_antenna ||
			    bitcount(ant_cfg->tx_antenna & 0x00FF) >
			    pmadapter->number_of_antenna ||
			    bitcount(ant_cfg->tx_antenna & 0xFF00) >
			    pmadapter->number_of_antenna) {
				PRINTM(MERROR,
				       "Invalid TX antenna setting: 0x%x\n",
				       ant_cfg->tx_antenna);
				pioctl_req->status_code =
					MLAN_ERROR_INVALID_PARAMETER;
				ret = MLAN_STATUS_FAILURE;
				goto exit;
			}
			if (ant_cfg->rx_antenna) {
				if (bitcount(ant_cfg->rx_antenna & 0x00FF) >
				    pmadapter->number_of_antenna ||
				    bitcount(ant_cfg->rx_antenna & 0xFF00) >
				    pmadapter->number_of_antenna) {
					PRINTM(MERROR,
					       "Invalid RX antenna setting: 0x%x\n",
					       ant_cfg->rx_antenna);
					pioctl_req->status_code =
						MLAN_ERROR_INVALID_PARAMETER;
					ret = MLAN_STATUS_FAILURE;
					goto exit;
				}
			} else
				ant_cfg->rx_antenna = ant_cfg->tx_antenna;
		} else if (!radio_cfg->param.ant_cfg_1x1.antenna ||
			   ((radio_cfg->param.ant_cfg_1x1.antenna !=
			     RF_ANTENNA_AUTO) &&
			    (radio_cfg->param.ant_cfg_1x1.antenna & 0xFFFC))) {
			PRINTM(MERROR, "Invalid antenna setting\n");
			pioctl_req->status_code = MLAN_ERROR_INVALID_PARAMETER;
			ret = MLAN_STATUS_FAILURE;
			goto exit;
		}
		cmd_action = HostCmd_ACT_GEN_SET;
	} else
		cmd_action = HostCmd_ACT_GEN_GET;

	/* Cast it to t_u16, antenna mode for command
	 * HostCmd_CMD_802_11_RF_ANTENNA requires 2 bytes */
	if (!IS_STREAM_2X2(pmadapter->feature_control))
		ant_cfg_1x1 = &radio_cfg->param.ant_cfg_1x1;

	/* Send request to firmware */
	ret = wlan_prepare_cmd(pmpriv, HostCmd_CMD_802_11_RF_ANTENNA,
			       cmd_action, 0, (t_void *)pioctl_req,
			       (IS_STREAM_2X2(pmadapter->feature_control)) ?
			       (t_void *)ant_cfg : (t_void *)ant_cfg_1x1);

	if (ret == MLAN_STATUS_SUCCESS)
		ret = MLAN_STATUS_PENDING;

exit:
	LEAVE();
	return ret;
}

/**
 *  @brief Get rate bitmap
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_PENDING --success, otherwise fail
 */
static mlan_status
wlan_rate_ioctl_get_rate_bitmap(pmlan_adapter pmadapter,
				pmlan_ioctl_req pioctl_req)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];

	ENTER();

	/* Send request to firmware */
	ret = wlan_prepare_cmd(pmpriv, HostCmd_CMD_TX_RATE_CFG,
			       HostCmd_ACT_GEN_GET, 0, (t_void *)pioctl_req,
			       MNULL);
	if (ret == MLAN_STATUS_SUCCESS)
		ret = MLAN_STATUS_PENDING;

	LEAVE();
	return ret;
}

/**
 *  @brief Set rate bitmap
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_PENDING --success, otherwise fail
 */
static mlan_status
wlan_rate_ioctl_set_rate_bitmap(pmlan_adapter pmadapter,
				pmlan_ioctl_req pioctl_req)
{
	mlan_ds_rate *ds_rate = MNULL;
	mlan_status ret = MLAN_STATUS_FAILURE;
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];
	t_u16 *bitmap_rates = MNULL;

	ENTER();

	ds_rate = (mlan_ds_rate *)pioctl_req->pbuf;
	bitmap_rates = ds_rate->param.rate_cfg.bitmap_rates;

	PRINTM(MINFO,
	       "RateBitmap=%04x%04x%04x%04x%04x%04x%04x%04x"
	       "%04x%04x%04x%04x%04x%04x%04x%04x%04x%04x, "
	       "IsRateAuto=%d, DataRate=%d\n",
	       bitmap_rates[17], bitmap_rates[16], bitmap_rates[15],
	       bitmap_rates[14], bitmap_rates[13], bitmap_rates[12],
	       bitmap_rates[11], bitmap_rates[10], bitmap_rates[9],
	       bitmap_rates[8], bitmap_rates[7], bitmap_rates[6],
	       bitmap_rates[5], bitmap_rates[4], bitmap_rates[3],
	       bitmap_rates[2], bitmap_rates[1], bitmap_rates[0],
	       pmpriv->is_data_rate_auto, pmpriv->data_rate);

	/* Send request to firmware */
	ret = wlan_prepare_cmd(pmpriv, HostCmd_CMD_TX_RATE_CFG,
			       HostCmd_ACT_GEN_SET, 0, (t_void *)pioctl_req,
			       (t_void *)bitmap_rates);
	if (ret == MLAN_STATUS_SUCCESS)
		ret = MLAN_STATUS_PENDING;

	LEAVE();
	return ret;
}

/**
 *  @brief Get rate value
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_SUCCESS/MLAN_STATUS_PENDING --success,
 * otherwise fail
 */
static mlan_status
wlan_rate_ioctl_get_rate_value(pmlan_adapter pmadapter,
			       pmlan_ioctl_req pioctl_req)
{
	mlan_ds_rate *rate = MNULL;
	mlan_status ret = MLAN_STATUS_SUCCESS;
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];

	ENTER();

	rate = (mlan_ds_rate *)pioctl_req->pbuf;
	rate->param.rate_cfg.is_rate_auto = pmpriv->is_data_rate_auto;
	pioctl_req->data_read_written =
		sizeof(mlan_rate_cfg_t) + MLAN_SUB_COMMAND_SIZE;

	/* If not connected, set rate to the lowest in each band */
	if (pmpriv->media_connected != MTRUE) {
		if (pmpriv->config_bands & (BAND_B | BAND_G)) {
			/* Return the lowest supported rate for BG band */
			rate->param.rate_cfg.rate = SupportedRates_BG[0] & 0x7f;
		} else if (pmpriv->config_bands & (BAND_A | BAND_B)) {
			/* Return the lowest supported rate for A band */
			rate->param.rate_cfg.rate = SupportedRates_BG[0] & 0x7f;
		} else if (pmpriv->config_bands & BAND_A) {
			/* Return the lowest supported rate for A band */
			rate->param.rate_cfg.rate = SupportedRates_A[0] & 0x7f;
		} else if (pmpriv->config_bands & BAND_G) {
			/* Return the lowest supported rate for G band */
			rate->param.rate_cfg.rate = SupportedRates_G[0] & 0x7f;
		} else if (pmpriv->config_bands & BAND_B) {
			/* Return the lowest supported rate for B band */
			rate->param.rate_cfg.rate = SupportedRates_B[0] & 0x7f;
		} else if (pmpriv->config_bands & BAND_GN) {
			/* Return the lowest supported rate for N band */
			rate->param.rate_cfg.rate = SupportedRates_N[0] & 0x7f;
		} else {
			PRINTM(MMSG, "Invalid Band 0x%x\n",
			       pmpriv->config_bands);
		}

	} else {
		/* Send request to firmware */
		ret = wlan_prepare_cmd(pmpriv, HostCmd_CMD_802_11_TX_RATE_QUERY,
				       HostCmd_ACT_GEN_GET, 0,
				       (t_void *)pioctl_req, MNULL);
		if (ret == MLAN_STATUS_SUCCESS)
			ret = MLAN_STATUS_PENDING;
	}

	LEAVE();
	return ret;
}

/**
 *  @brief Set rate value
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_PENDING --success, otherwise fail
 */
static mlan_status
wlan_rate_ioctl_set_rate_value(pmlan_adapter pmadapter,
			       pmlan_ioctl_req pioctl_req)
{
	mlan_ds_rate *ds_rate = MNULL;
	WLAN_802_11_RATES rates;
	t_u8 *rate = MNULL;
	int rate_index = 0;
	t_u16 bitmap_rates[MAX_BITMAP_RATES_SIZE];
	t_u32 i = 0;
	mlan_status ret = MLAN_STATUS_SUCCESS;
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];

	ENTER();

	ds_rate = (mlan_ds_rate *)pioctl_req->pbuf;

	if (ds_rate->param.rate_cfg.is_rate_auto) {
		memset(pmadapter, bitmap_rates, 0, sizeof(bitmap_rates));
		/* Support all HR/DSSS rates */
		bitmap_rates[0] = 0x000F;
		/* Support all OFDM rates */
		bitmap_rates[1] = 0x00FF;
		/* Rates talbe [0] HR/DSSS,[1] OFDM,[2..9] HT,[10..17] VHT */
		/* Support all HT-MCSs rate */
		for (i = 0; i < NELEMENTS(pmpriv->bitmap_rates) - 3 - 8; i++)
			bitmap_rates[i + 2] = 0xFFFF;
		bitmap_rates[9] = 0x3FFF;
		/* Support all VHT-MCSs rate */
		for (i = 0; i < NELEMENTS(pmpriv->bitmap_rates) - 10; i++)
			bitmap_rates[i + 10] = 0x03FF;	/* 10 Bits valid */
	} else {
		memset(pmadapter, rates, 0, sizeof(rates));
		wlan_get_active_data_rates(pmpriv, pmpriv->bss_mode,
					   (pmpriv->bss_mode ==
					    MLAN_BSS_MODE_INFRA) ?
					   pmpriv->config_bands :
					   pmadapter->adhoc_start_band, rates);
		rate = rates;
		for (i = 0; (rate[i] && i < WLAN_SUPPORTED_RATES); i++) {
			PRINTM(MINFO, "Rate=0x%X  Wanted=0x%X\n", rate[i],
			       ds_rate->param.rate_cfg.rate);
			if ((rate[i] & 0x7f) ==
			    (ds_rate->param.rate_cfg.rate & 0x7f))
				break;
		}
		if ((i < WLAN_SUPPORTED_RATES && !rate[i]) ||
		    (i == WLAN_SUPPORTED_RATES)) {
			PRINTM(MERROR,
			       "The fixed data rate 0x%X is out "
			       "of range\n", ds_rate->param.rate_cfg.rate);
			pioctl_req->status_code = MLAN_ERROR_INVALID_PARAMETER;
			ret = MLAN_STATUS_FAILURE;
			goto exit;
		}
		memset(pmadapter, bitmap_rates, 0, sizeof(bitmap_rates));
		rate_index =
			wlan_data_rate_to_index(pmadapter,
						ds_rate->param.rate_cfg.rate);
		/* Only allow b/g rates to be set */
		if (rate_index >= MLAN_RATE_INDEX_HRDSSS0 &&
		    rate_index <= MLAN_RATE_INDEX_HRDSSS3)
			bitmap_rates[0] = 1 << rate_index;
		else {
			rate_index -= 1;	/* There is a 0x00 in the table */
			if (rate_index >= MLAN_RATE_INDEX_OFDM0 &&
			    rate_index <= MLAN_RATE_INDEX_OFDM7)
				bitmap_rates[1] = 1 << (rate_index -
							MLAN_RATE_INDEX_OFDM0);
		}
	}

	/* Send request to firmware */
	ret = wlan_prepare_cmd(pmpriv, HostCmd_CMD_TX_RATE_CFG,
			       HostCmd_ACT_GEN_SET, 0, (t_void *)pioctl_req,
			       bitmap_rates);
	if (ret == MLAN_STATUS_SUCCESS)
		ret = MLAN_STATUS_PENDING;

exit:
	LEAVE();
	return ret;
}

/**
 *  @brief Get rate index
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_PENDING --success, otherwise fail
 */
static mlan_status
wlan_rate_ioctl_get_rate_index(pmlan_adapter pmadapter,
			       pmlan_ioctl_req pioctl_req)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];

	ENTER();

	/* Send request to firmware */
	ret = wlan_prepare_cmd(pmpriv, HostCmd_CMD_TX_RATE_CFG,
			       HostCmd_ACT_GEN_GET, 0, (t_void *)pioctl_req,
			       MNULL);
	if (ret == MLAN_STATUS_SUCCESS)
		ret = MLAN_STATUS_PENDING;

	LEAVE();
	return ret;
}

/**
 *  @brief Set rate index
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_PENDING --success, otherwise fail
 */
static mlan_status
wlan_rate_ioctl_set_rate_index(pmlan_adapter pmadapter,
			       pmlan_ioctl_req pioctl_req)
{
	t_u32 rate_index;
	t_u32 rate_format;
	t_u32 nss;
	t_u32 i;
	mlan_ds_rate *ds_rate = MNULL;
	mlan_status ret = MLAN_STATUS_FAILURE;
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];
	t_u16 bitmap_rates[MAX_BITMAP_RATES_SIZE];
	int tx_mcs_supp = GET_TXMCSSUPP(pmpriv->usr_dev_mcs_support);

	ENTER();

	ds_rate = (mlan_ds_rate *)pioctl_req->pbuf;
	rate_format = ds_rate->param.rate_cfg.rate_format;
	nss = ds_rate->param.rate_cfg.nss;
	rate_index = ds_rate->param.rate_cfg.rate;

	if (ds_rate->param.rate_cfg.is_rate_auto) {
		memset(pmadapter, bitmap_rates, 0, sizeof(bitmap_rates));
		/* Rates talbe [0]: HR/DSSS;[1]: OFDM; [2..9] HT; */
		/* Support all HR/DSSS rates */
		bitmap_rates[0] = 0x000F;
		/* Support all OFDM rates */
		bitmap_rates[1] = 0x00FF;
		/* Support all HT-MCSs rate */
		for (i = 2; i < 9; i++)
			bitmap_rates[i] = 0xFFFF;
		bitmap_rates[9] = 0x3FFF;
		/* [10..17] VHT */
		/* Support all VHT-MCSs rate for NSS 1 and 2 */
		for (i = 10; i < 12; i++)
			bitmap_rates[i] = 0x03FF;	/* 10 Bits valid */
		/* Set to 0 as default value for all other NSSs */
		for (i = 12; i < 17; i++)
			bitmap_rates[i] = 0x0;
		/* [18..25] HE */
		/* Support all HE-MCSs rate for NSS1 and 2 */
		for (i = 18; i < 20; i++)
			bitmap_rates[i] = 0x0FFF;
		for (i = 20; i < NELEMENTS(bitmap_rates); i++)
			bitmap_rates[i] = 0x0;
	} else {
		PRINTM(MINFO, "Rate index is %d\n", rate_index);
		if ((rate_format == MLAN_RATE_FORMAT_HT) &&
		    (rate_index > MLAN_RATE_INDEX_MCS7 &&
		     rate_index <= MLAN_RATE_INDEX_MCS15) &&
		    (tx_mcs_supp < 2)) {
			PRINTM(MERROR,
			       "HW don't support 2x2, rate_index=%d hw_mcs_supp=0x%x\n",
			       rate_index, pmpriv->usr_dev_mcs_support);
			pioctl_req->status_code = MLAN_ERROR_INVALID_PARAMETER;
			LEAVE();
			return MLAN_STATUS_FAILURE;
		}
		memset(pmadapter, bitmap_rates, 0, sizeof(bitmap_rates));
		if (rate_format == MLAN_RATE_FORMAT_LG) {
			/* Bitmap of HR/DSSS rates */
			if (rate_index <= MLAN_RATE_INDEX_HRDSSS3) {
				bitmap_rates[0] = 1 << rate_index;
				ret = MLAN_STATUS_SUCCESS;
				/* Bitmap of OFDM rates */
			} else if ((rate_index >= MLAN_RATE_INDEX_OFDM0) &&
				   (rate_index <= MLAN_RATE_INDEX_OFDM7)) {
				bitmap_rates[1] = 1 << (rate_index -
							MLAN_RATE_INDEX_OFDM0);
				ret = MLAN_STATUS_SUCCESS;
			}
		} else if (rate_format == MLAN_RATE_FORMAT_HT) {
			if (rate_index <= MLAN_RATE_INDEX_MCS32) {
				bitmap_rates[2 + (rate_index / 16)] =
					1 << (rate_index % 16);
				ret = MLAN_STATUS_SUCCESS;
			}
		}
		if (rate_format == MLAN_RATE_FORMAT_VHT) {
			if ((rate_index <= MLAN_RATE_INDEX_MCS9) &&
			    (MLAN_RATE_NSS1 <= nss) &&
			    (nss <= MLAN_RATE_NSS2)) {
				bitmap_rates[10 + nss - MLAN_RATE_NSS1] =
					(1 << rate_index);
				ret = MLAN_STATUS_SUCCESS;
			}
		}
		if (rate_format == MLAN_RATE_FORMAT_HE) {
			if (IS_FW_SUPPORT_11AX(pmadapter)) {
				if ((rate_index <= MLAN_RATE_INDEX_MCS11) &&
				    (MLAN_RATE_NSS1 <= nss) &&
				    (nss <= MLAN_RATE_NSS2)) {
					bitmap_rates[18 + nss -
						     MLAN_RATE_NSS1] =
						(1 << rate_index);
					ret = MLAN_STATUS_SUCCESS;
				}
			} else {
				PRINTM(MERROR,
				       "Error! Fw doesn't support 11AX\n");
				LEAVE();
				return MLAN_STATUS_FAILURE;
			}
		}

		if (ret == MLAN_STATUS_FAILURE) {
			PRINTM(MERROR, "Invalid MCS index=%d. \n", rate_index);
			pioctl_req->status_code = MLAN_ERROR_INVALID_PARAMETER;
			LEAVE();
			return MLAN_STATUS_FAILURE;
		}
	}

	PRINTM(MINFO,
	       "RateBitmap=%04x%04x%04x%04x%04x%04x%04x%04x"
	       "%04x%04x%04x%04x%04x%04x%04x%04x%04x%04x, "
	       "IsRateAuto=%d, DataRate=%d\n",
	       bitmap_rates[17], bitmap_rates[16], bitmap_rates[15],
	       bitmap_rates[14], bitmap_rates[13], bitmap_rates[12],
	       bitmap_rates[11], bitmap_rates[10], bitmap_rates[9],
	       bitmap_rates[8], bitmap_rates[7], bitmap_rates[6],
	       bitmap_rates[5], bitmap_rates[4], bitmap_rates[3],
	       bitmap_rates[2], bitmap_rates[1], bitmap_rates[0],
	       pmpriv->is_data_rate_auto, pmpriv->data_rate);

	/* Send request to firmware */
	ret = wlan_prepare_cmd(pmpriv, HostCmd_CMD_TX_RATE_CFG,
			       HostCmd_ACT_GEN_SET, 0, (t_void *)pioctl_req,
			       (t_void *)bitmap_rates);
	if (ret == MLAN_STATUS_SUCCESS)
		ret = MLAN_STATUS_PENDING;

	LEAVE();
	return ret;
}

/**
 *  @brief Rate configuration command handler
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_SUCCESS/MLAN_STATUS_PENDING --success,
 * otherwise fail
 */
mlan_status
wlan_rate_ioctl_cfg(pmlan_adapter pmadapter, pmlan_ioctl_req pioctl_req)
{
	mlan_ds_rate *rate = MNULL;
	mlan_status status = MLAN_STATUS_SUCCESS;

	ENTER();

	rate = (mlan_ds_rate *)pioctl_req->pbuf;
	if (rate->param.rate_cfg.rate_type == MLAN_RATE_BITMAP) {
		if (pioctl_req->action == MLAN_ACT_GET)
			status = wlan_rate_ioctl_get_rate_bitmap(pmadapter,
								 pioctl_req);
		else
			status = wlan_rate_ioctl_set_rate_bitmap(pmadapter,
								 pioctl_req);
	} else if (rate->param.rate_cfg.rate_type == MLAN_RATE_VALUE) {
		if (pioctl_req->action == MLAN_ACT_GET)
			status = wlan_rate_ioctl_get_rate_value(pmadapter,
								pioctl_req);
		else
			status = wlan_rate_ioctl_set_rate_value(pmadapter,
								pioctl_req);
	} else {
		if (pioctl_req->action == MLAN_ACT_GET)
			status = wlan_rate_ioctl_get_rate_index(pmadapter,
								pioctl_req);
		else
			status = wlan_rate_ioctl_set_rate_index(pmadapter,
								pioctl_req);
	}

	LEAVE();
	return status;
}

/**
 *  @brief Get data rates
 *
 *  @param pmadapter    A pointer to mlan_adapter structure
 *  @param pioctl_req   A pointer to ioctl request buffer
 *
 *  @return             MLAN_STATUS_PENDING --success, otherwise fail
 */
mlan_status
wlan_rate_ioctl_get_data_rate(pmlan_adapter pmadapter,
			      pmlan_ioctl_req pioctl_req)
{
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_status ret = MLAN_STATUS_SUCCESS;

	ENTER();

	if (pioctl_req->action != MLAN_ACT_GET) {
		ret = MLAN_STATUS_FAILURE;
		goto exit;
	}

	/* Send request to firmware */
	ret = wlan_prepare_cmd(pmpriv, HostCmd_CMD_802_11_TX_RATE_QUERY,
			       HostCmd_ACT_GEN_GET, 0, (t_void *)pioctl_req,
			       MNULL);
	if (ret == MLAN_STATUS_SUCCESS)
		ret = MLAN_STATUS_PENDING;

exit:
	LEAVE();
	return ret;
}

/**
 *  @brief Set/Get remain on channel setting
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_SUCCESS --success, otherwise fail
 */
mlan_status
wlan_radio_ioctl_remain_chan_cfg(pmlan_adapter pmadapter,
				 pmlan_ioctl_req pioctl_req)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	mlan_ds_radio_cfg *radio_cfg = MNULL;
	t_u16 cmd_action = 0;
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];

	ENTER();

	radio_cfg = (mlan_ds_radio_cfg *)pioctl_req->pbuf;
	if (pioctl_req->action == MLAN_ACT_SET)
		cmd_action = HostCmd_ACT_GEN_SET;
	else
		cmd_action = HostCmd_ACT_GEN_GET;

	/* Send request to firmware */
	ret = wlan_prepare_cmd(pmpriv, HostCmd_CMD_802_11_REMAIN_ON_CHANNEL,
			       cmd_action, 0, (t_void *)pioctl_req,
			       &radio_cfg->param.remain_chan);

	if (ret == MLAN_STATUS_SUCCESS)
		ret = MLAN_STATUS_PENDING;

	LEAVE();
	return ret;
}

#ifdef WIFI_DIRECT_SUPPORT
/**
 *  @brief Set/Get wifi_direct_mode
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_SUCCESS --success, otherwise fail
 */
mlan_status
wlan_bss_ioctl_wifi_direct_mode(pmlan_adapter pmadapter,
				pmlan_ioctl_req pioctl_req)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	mlan_ds_bss *bss = MNULL;

	t_u16 cmd_action = 0;
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];

	ENTER();

	bss = (mlan_ds_bss *)pioctl_req->pbuf;

	if (pioctl_req->action == MLAN_ACT_SET)
		cmd_action = HostCmd_ACT_GEN_SET;
	else
		cmd_action = HostCmd_ACT_GEN_GET;

	/* Send request to firmware */
	ret = wlan_prepare_cmd(pmpriv, HOST_CMD_WIFI_DIRECT_MODE_CONFIG,
			       cmd_action, 0, (t_void *)pioctl_req,
			       &bss->param.wfd_mode);

	if (ret == MLAN_STATUS_SUCCESS)
		ret = MLAN_STATUS_PENDING;

	LEAVE();
	return ret;
}

/**
 *  @brief Set/Get p2p config
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_SUCCESS --success, otherwise fail
 */
mlan_status
wlan_misc_p2p_config(pmlan_adapter pmadapter, pmlan_ioctl_req pioctl_req)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	mlan_ds_misc_cfg *misc_cfg = MNULL;
	t_u16 cmd_action = 0;
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];

	ENTER();

	misc_cfg = (mlan_ds_misc_cfg *)pioctl_req->pbuf;
	if (pioctl_req->action == MLAN_ACT_SET)
		cmd_action = HostCmd_ACT_GEN_SET;
	else
		cmd_action = HostCmd_ACT_GEN_GET;

	/* Send request to firmware */
	ret = wlan_prepare_cmd(pmpriv, HOST_CMD_P2P_PARAMS_CONFIG, cmd_action,
			       0, (t_void *)pioctl_req,
			       &misc_cfg->param.p2p_config);

	if (ret == MLAN_STATUS_SUCCESS)
		ret = MLAN_STATUS_PENDING;

	LEAVE();
	return ret;
}
#endif

/**
 *  @brief Set/Get GPIO TSF Latch config
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_SUCCESS --success, otherwise fail
 */
mlan_status
wlan_misc_gpio_tsf_latch_config(pmlan_adapter pmadapter,
				pmlan_ioctl_req pioctl_req)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	mlan_ds_misc_cfg *misc_cfg = MNULL;
	t_u16 cmd_action = 0;
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];

	ENTER();

	misc_cfg = (mlan_ds_misc_cfg *)pioctl_req->pbuf;
	if (pioctl_req->action == MLAN_ACT_SET)
		cmd_action = HostCmd_ACT_GEN_SET;
	else
		cmd_action = HostCmd_ACT_GEN_GET;

	/* Send request to firmware */
	ret = wlan_prepare_cmd(pmpriv, HOST_CMD_GPIO_TSF_LATCH_PARAM_CONFIG,
			       cmd_action, 0, (t_void *)pioctl_req,
			       &misc_cfg->param.gpio_tsf_latch_config);

	if (ret == MLAN_STATUS_SUCCESS)
		ret = MLAN_STATUS_PENDING;

	LEAVE();
	return ret;
}

/**
 *  @brief Get TSF info
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_SUCCESS --success, otherwise fail
 */
mlan_status
wlan_misc_get_tsf_info(pmlan_adapter pmadapter, pmlan_ioctl_req pioctl_req)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	mlan_ds_misc_cfg *misc_cfg = MNULL;
	t_u16 cmd_action = 0;
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];

	ENTER();

	misc_cfg = (mlan_ds_misc_cfg *)pioctl_req->pbuf;
	cmd_action = HostCmd_ACT_GEN_GET;

	/* Send request to firmware */
	ret = wlan_prepare_cmd(pmpriv, HOST_CMD_GPIO_TSF_LATCH_PARAM_CONFIG,
			       cmd_action, 0, (t_void *)pioctl_req,
			       &misc_cfg->param.tsf_info);

	if (ret == MLAN_STATUS_SUCCESS)
		ret = MLAN_STATUS_PENDING;

	LEAVE();
	return ret;
}

/**
 *  @brief Set coalesce config
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_SUCCESS --success, otherwise fail
 */
mlan_status
wlan_misc_ioctl_coalesce_cfg(pmlan_adapter pmadapter,
			     pmlan_ioctl_req pioctl_req)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	mlan_ds_misc_cfg *misc_cfg = MNULL;
	t_u16 cmd_action = 0;
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];

	ENTER();

	misc_cfg = (mlan_ds_misc_cfg *)pioctl_req->pbuf;
	if (pioctl_req->action == MLAN_ACT_SET)
		cmd_action = HostCmd_ACT_GEN_SET;
	else
		cmd_action = HostCmd_ACT_GEN_GET;

	/* Send request to firmware */
	ret = wlan_prepare_cmd(pmpriv, HostCmd_CMD_COALESCE_CFG, cmd_action, 0,
			       (t_void *)pioctl_req,
			       &misc_cfg->param.coalesce_cfg);

	if (ret == MLAN_STATUS_SUCCESS)
		ret = MLAN_STATUS_PENDING;

	LEAVE();
	return ret;
}

/**
 *  @brief Get/Set USB packet aggregation parameters
 *
 *  @param pmadapter    A pointer to mlan_adapter structure
 *  @param pioctl_req   A pointer to ioctl request buffer
 *
 *  @return             MLAN_STATUS_PENDING --success, otherwise fail
 */
mlan_status
wlan_misc_ioctl_aggr_ctrl(pmlan_adapter pmadapter, pmlan_ioctl_req pioctl_req)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	mlan_ds_misc_cfg *misc = MNULL;
	t_u16 cmd_action = 0;
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];

	ENTER();

	misc = (mlan_ds_misc_cfg *)pioctl_req->pbuf;
	if (pioctl_req->action == MLAN_ACT_SET)
		cmd_action = HostCmd_ACT_GEN_SET;
	else
		cmd_action = HostCmd_ACT_GEN_GET;

	/* Send request to firmware */
	ret = wlan_prepare_cmd(pmpriv, HostCmd_CMD_PACKET_AGGR_CTRL, cmd_action,
			       0, (t_void *)pioctl_req,
			       &misc->param.aggr_params);

	if (ret == MLAN_STATUS_SUCCESS) {
		ret = MLAN_STATUS_PENDING;
	}

	LEAVE();
	return ret;
}

#ifdef USB
/**
 *  @brief Get/Set USB packet aggregation parameters
 *
 *  @param pmadapter    A pointer to mlan_adapter structure
 *  @param pioctl_req   A pointer to ioctl request buffer
 *
 *  @return             MLAN_STATUS_PENDING --success, otherwise fail
 */
mlan_status
wlan_misc_ioctl_usb_aggr_ctrl(pmlan_adapter pmadapter,
			      pmlan_ioctl_req pioctl_req)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	mlan_ds_misc_cfg *misc = MNULL;
	t_u16 cmd_action = 0;
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];

	ENTER();

	if (pmadapter->pcard_usb->fw_usb_aggr == MFALSE) {
		PRINTM(MERROR, "USB aggregation not supported by FW\n");
		pioctl_req->status_code = MLAN_ERROR_CMD_INVALID;
		LEAVE();
		return MLAN_STATUS_FAILURE;
	}

	misc = (mlan_ds_misc_cfg *)pioctl_req->pbuf;
	if (pioctl_req->action == MLAN_ACT_SET)
		cmd_action = HostCmd_ACT_GEN_SET;
	else
		cmd_action = HostCmd_ACT_GEN_GET;

	/* Send request to firmware */
	ret = wlan_prepare_cmd(pmpriv,
			       HostCmd_CMD_PACKET_AGGR_OVER_HOST_INTERFACE,
			       cmd_action, 0, (t_void *)pioctl_req,
			       &misc->param.usb_aggr_params);

	if (ret == MLAN_STATUS_SUCCESS) {
		ret = MLAN_STATUS_PENDING;
	}

	LEAVE();
	return ret;
}
#endif

/**
 *  @brief Get/Set Tx control configuration
 *
 *  @param pmadapter    A pointer to mlan_adapter structure
 *  @param pioctl_req   A pointer to ioctl request buffer
 *
 *  @return             MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_misc_ioctl_txcontrol(pmlan_adapter pmadapter, pmlan_ioctl_req pioctl_req)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	mlan_ds_misc_cfg *misc = MNULL;
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];

	ENTER();

	misc = (mlan_ds_misc_cfg *)pioctl_req->pbuf;
	if (pioctl_req->action == MLAN_ACT_SET)
		pmpriv->pkt_tx_ctrl = misc->param.tx_control;
	else
		misc->param.tx_control = pmpriv->pkt_tx_ctrl;

	LEAVE();
	return ret;
}

#ifdef RX_PACKET_COALESCE
/**
 *  @brief Get/Set RX packet coalescing configuration
 *
 *  @param pmadapter    A pointer to mlan_adapter structure
 *  @param pioctl_req   A pointer to ioctl request buffer
 *
 *  @return             MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_misc_ioctl_rx_pkt_coalesce_config(pmlan_adapter pmadapter,
				       pmlan_ioctl_req pioctl_req)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	mlan_ds_misc_cfg *misc = MNULL;
	t_u16 cmd_action = 0;
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];

	ENTER();

	misc = (mlan_ds_misc_cfg *)pioctl_req->pbuf;

	if (pioctl_req->action == MLAN_ACT_SET)
		cmd_action = HostCmd_ACT_GEN_SET;
	else
		cmd_action = HostCmd_ACT_GEN_GET;

	/* Send request to firmware */
	ret = wlan_prepare_cmd(pmpriv, HostCmd_CMD_RX_PKT_COALESCE_CFG,
			       cmd_action, 0, (t_void *)pioctl_req,
			       &misc->param.rx_coalesce);

	if (ret == MLAN_STATUS_SUCCESS)
		ret = MLAN_STATUS_PENDING;

	LEAVE();
	return ret;
}
#endif

/**
 *  @brief Is any uAP started or STA connected?
 *
 *  @param pmadapter    A pointer to mlan_adapter structure
 *
 *  @return             MTRUE/MFALSE
 */
t_bool
wlan_check_interface_active(mlan_adapter *pmadapter)
{
	t_bool ret = MFALSE;
	pmlan_private pmpriv;
	int i;

	if (pmadapter == MNULL)
		return MFALSE;

	for (i = 0; i < pmadapter->priv_num; i++) {
		pmpriv = pmadapter->priv[i];
		if (pmpriv) {
#ifdef UAP_SUPPORT
			if (GET_BSS_ROLE(pmpriv) == MLAN_BSS_ROLE_UAP)
				ret = pmpriv->uap_bss_started;
			else
#endif
			if (GET_BSS_ROLE(pmpriv) == MLAN_BSS_ROLE_STA)
				ret = pmpriv->media_connected;
		}
		if (ret)
			return MTRUE;
	}

	return MFALSE;
}

/**
 *  @brief Get/Set DFS REPEATER mode
 *
 *  @param pmadapter    A pointer to mlan_adapter structure
 *  @param pioctl_req   A pointer to ioctl request buffer
 *
 *  @return             MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_misc_ioctl_dfs_repeater_cfg(pmlan_adapter pmadapter,
				 pmlan_ioctl_req pioctl_req)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	mlan_ds_misc_cfg *misc = MNULL;
	t_u16 cmd_action = 0;
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];

	ENTER();

	misc = (mlan_ds_misc_cfg *)pioctl_req->pbuf;

	if (pioctl_req->action == MLAN_ACT_SET) {
		/* Make sure no interface is active
		 * before setting the dfs repeater mode
		 */
		if (wlan_check_interface_active(pmadapter)) {
			PRINTM(MMSG, "DFS-Repeater active priv found,"
			       " skip enabling the mode.\n");
			ret = MLAN_STATUS_FAILURE;
			goto done;
		}

		cmd_action = HostCmd_ACT_GEN_SET;
	} else {
		cmd_action = HostCmd_ACT_GEN_GET;
	}

	/* Send request to firmware */
	ret = wlan_prepare_cmd(pmpriv, HostCmd_DFS_REPEATER_MODE, cmd_action, 0,
			       (t_void *)pioctl_req, &misc->param.dfs_repeater);

done:
	if (ret == MLAN_STATUS_SUCCESS)
		ret = MLAN_STATUS_PENDING;

	LEAVE();
	return ret;
}

/**
 *  @brief Set/Get Low Power Mode
 *
 *  @param pmadapter    A pointer to mlan_adapter structure
 *  @param pioctl_req   A pointer to ioctl request buffer
 *
 *  @return             MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_misc_ioctl_low_pwr_mode(pmlan_adapter pmadapter,
			     pmlan_ioctl_req pioctl_req)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	mlan_ds_misc_cfg *misc = MNULL;
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];

	ENTER();

	misc = (mlan_ds_misc_cfg *)pioctl_req->pbuf;

	/* Send request to firmware */
	ret = wlan_prepare_cmd(pmpriv, HostCMD_CONFIG_LOW_POWER_MODE,
			       HostCmd_ACT_GEN_SET, 0, (t_void *)pioctl_req,
			       &misc->param.low_pwr_mode);

	if (ret == MLAN_STATUS_SUCCESS)
		ret = MLAN_STATUS_PENDING;

	LEAVE();
	return ret;
}

/**
 *  @brief Configure PMIC in Firmware
 *
 *  @param pmadapter    A pointer to mlan_adapter structure
 *  @param pioctl_req   A pointer to ioctl request buffer
 *
 *  @return             MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_misc_ioctl_pmic_configure(pmlan_adapter pmadapter,
			       pmlan_ioctl_req pioctl_req)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];

	ENTER();

	/* Send request to firmware */
	ret = wlan_prepare_cmd(pmpriv, HOST_CMD_PMIC_CONFIGURE,
			       HostCmd_ACT_GEN_SET, 0, (t_void *)pioctl_req,
			       MNULL);

	if (ret == MLAN_STATUS_SUCCESS)
		ret = MLAN_STATUS_PENDING;

	LEAVE();
	return ret;
}

/*  @brief Set/Get CW Mode Level control
 *
 *  @param pmadapter    A pointer to mlan_adapter structure
 *  @param pioctl_req   A pointer to ioctl request buffer
 *
 *  @return             MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_misc_ioctl_cwmode_ctrl(pmlan_adapter pmadapter, pmlan_ioctl_req pioctl_req)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	mlan_ds_misc_cfg *misc = MNULL;
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];
	t_u16 cmd_action = 0;

	ENTER();

	misc = (mlan_ds_misc_cfg *)pioctl_req->pbuf;

	if (pioctl_req->action == MLAN_ACT_SET)
		cmd_action = HostCmd_ACT_GEN_SET;
	else
		cmd_action = HostCmd_ACT_GEN_GET;

	/* Send request to firmware */
	ret = wlan_prepare_cmd(pmpriv, HostCmd_CMD_CW_MODE_CTRL, cmd_action, 0,
			       (t_void *)pioctl_req, &misc->param.cwmode);

	if (ret == MLAN_STATUS_SUCCESS)
		ret = MLAN_STATUS_PENDING;

	LEAVE();
	return ret;
}

/**
 *  @brief push value to stack
 *
 *  @param pmadapter    A pointer to mlan_adapter structure
 *  @param s        A pointer to mef_stack
 *  @param len          Length of value
 *  @param val          A pointer to value
 *
 *  @return             MLAN_STATUS_SUCCESS or FAIL
 */
inline mlan_status
push_n(pmlan_adapter pmadapter, mef_stack *s, t_u8 len, t_u8 *val)
{
	if ((s->sp + len) <= MAX_NUM_STACK_BYTES) {
		memcpy_ext(pmadapter, s->byte + s->sp, val, len,
			   MAX_NUM_STACK_BYTES - s->sp);
		s->sp += len;
		return MLAN_STATUS_SUCCESS;
	} else {
		PRINTM(MERROR, "Stack is full\n");
		return MLAN_STATUS_FAILURE;
	}
}

/**
 *  @brief push value to stack accoring to operand type
 *
 *  @param pmadapter    A pointer to mlan_adapter structure
 *  @param s        A pointer to mef_stack
 *  @param op           A pointer to mef_op
 *
 *  @return             MLAN_STATUS_SUCCESS or FAIL
 */
inline mlan_status
mef_push(pmlan_adapter pmadapter, mef_stack *s, mef_op *op)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	t_u8 nbytes;
	switch (op->operand_type) {
	case OPERAND_DNUM:
		ret = push_n(pmadapter, s, 4, op->val);
		if (ret == MLAN_STATUS_SUCCESS)
			ret = push_n(pmadapter, s, 1, &op->operand_type);
		else
			ret = MLAN_STATUS_FAILURE;
		break;
	case OPERAND_BYTE_SEQ:
		nbytes = op->val[0];
		if (MLAN_STATUS_SUCCESS ==
		    push_n(pmadapter, s, nbytes, op->val + 1) &&
		    MLAN_STATUS_SUCCESS == push_n(pmadapter, s, 1, op->val) &&
		    MLAN_STATUS_SUCCESS ==
		    push_n(pmadapter, s, 1, &op->operand_type))
			ret = MLAN_STATUS_SUCCESS;
		else
			ret = MLAN_STATUS_FAILURE;
		break;
	default:
		ret = push_n(pmadapter, s, 1, &op->operand_type);
		break;
	}
	return ret;
}

/**
 *  @brief push dnum filter to stack
 *
 *  @param pmadapter    A pointer to mlan_adapter structure
 *  @param s        A pointer to mef_stack
 *  @param filter       A pointer to filter item
 *
 *  @return             MLAN_STATUS_SUCCESS or FAIL
 */
static mlan_status
push_filter_dnum_eq(pmlan_adapter pmadapter, mef_stack *s, mef_filter_t *filter)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	t_u32 dnum;
	mef_op op;

	ENTER();

	if (!filter) {
		ret = MLAN_STATUS_FAILURE;
		goto done;
	}
	if (filter->fill_flag != (FILLING_TYPE | FILLING_PATTERN |
				  FILLING_OFFSET | FILLING_NUM_BYTES)) {
		PRINTM(MERROR, "Filter item fill error\n");
		ret = MLAN_STATUS_FAILURE;
		goto done;
	}

	/* Format of decimal num:
	 * |   5 bytes  |    5 bytes    |    5 bytes    |        1 byte | |
	 * pattern  |     offset    |  num of bytes |  type (TYPE_DNUM_EQ)  |
	 */

	/* push pattern */
	memset(pmadapter, &op, 0, sizeof(op));
	op.operand_type = OPERAND_DNUM;
	dnum = filter->pattern;
	memcpy_ext(pmadapter, op.val, &dnum, sizeof(dnum), sizeof(op.val));
	ret = mef_push(pmadapter, s, &op);
	if (ret != MLAN_STATUS_SUCCESS)
		goto done;

	/* push offset */
	memset(pmadapter, &op, 0, sizeof(op));
	op.operand_type = OPERAND_DNUM;
	dnum = filter->offset;
	memcpy_ext(pmadapter, op.val, &dnum, sizeof(dnum), sizeof(op.val));
	ret = mef_push(pmadapter, s, &op);
	if (ret != MLAN_STATUS_SUCCESS)
		goto done;

	/* push num of bytes */
	memset(pmadapter, &op, 0, sizeof(op));
	op.operand_type = OPERAND_DNUM;
	dnum = filter->num_bytes;
	memcpy_ext(pmadapter, op.val, &dnum, sizeof(dnum), sizeof(op.val));
	ret = mef_push(pmadapter, s, &op);
	if (ret != MLAN_STATUS_SUCCESS)
		goto done;

	/* push type */
	memset(pmadapter, &op, 0, sizeof(op));
	op.operand_type = TYPE_DNUM_EQ;
	ret = mef_push(pmadapter, s, &op);

done:
	LEAVE();
	return ret;
}

/**
 *  @brief push byte_eq filter to stack
 *
 *  @param pmadapter    A pointer to mlan_adapter structure
 *  @param s            A pointer to mef_stack
 *  @param filter       A pointer to filter item
 *
 *  @return             MLAN_STATUS_SUCCESS or FAIL
 */
static mlan_status
push_filter_byte_eq(pmlan_adapter pmadapter, mef_stack *s, mef_filter_t *filter)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	t_u32 dnum;
	mef_op op;

	ENTER();

	if (!filter) {
		ret = MLAN_STATUS_FAILURE;
		goto done;
	}
	if (filter->fill_flag != (FILLING_TYPE | FILLING_REPEAT |
				  FILLING_BYTE_SEQ | FILLING_OFFSET)) {
		PRINTM(MERROR, "Filter item fill error\n");
		ret = MLAN_STATUS_FAILURE;
		goto done;
	}

	/* Format of decimal num:
	 * |   5 bytes  |      val      |    5 bytes    |        1 byte | |
	 * repeat   |   bytes seq   |    offset     |  type (TYPE_BYTE_EQ)  |
	 */

	/* push repeat */
	memset(pmadapter, &op, 0, sizeof(op));
	op.operand_type = OPERAND_DNUM;
	dnum = filter->repeat;
	memcpy_ext(pmadapter, op.val, &dnum, sizeof(dnum), sizeof(op.val));
	ret = mef_push(pmadapter, s, &op);
	if (ret != MLAN_STATUS_SUCCESS)
		goto done;

	/* push bytes seq */
	memset(pmadapter, &op, 0, sizeof(op));
	op.operand_type = OPERAND_BYTE_SEQ;
	op.val[0] = filter->num_byte_seq;
	memcpy_ext(pmadapter, &op.val[1], filter->byte_seq,
		   filter->num_byte_seq, MAX_NUM_BYTE_SEQ);
	ret = mef_push(pmadapter, s, &op);
	if (ret != MLAN_STATUS_SUCCESS)
		goto done;

	/* push offset */
	memset(pmadapter, &op, 0, sizeof(op));
	op.operand_type = OPERAND_DNUM;
	dnum = filter->offset;
	memcpy_ext(pmadapter, op.val, &dnum, sizeof(dnum), sizeof(op.val));
	ret = mef_push(pmadapter, s, &op);
	if (ret != MLAN_STATUS_SUCCESS)
		goto done;

	/* push type */
	memset(pmadapter, &op, 0, sizeof(op));
	op.operand_type = TYPE_BYTE_EQ;
	ret = mef_push(pmadapter, s, &op);

done:
	LEAVE();
	return ret;
}

/**
 *  @brief push bite_eq filter to stack
 *
 *  @param pmadapter    A pointer to mlan_adapter structure
 *  @param s            A pointer to mef_stack
 *  @param filter       A pointer to filter item
 *
 *  @return             MLAN_STATUS_SUCCESS or FAIL
 */
static mlan_status
push_filter_bit_eq(pmlan_adapter pmadapter, mef_stack *s, mef_filter_t *filter)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	t_u32 dnum;
	mef_op op;

	ENTER();

	if (!filter) {
		ret = MLAN_STATUS_FAILURE;
		goto done;
	}
	if (filter->fill_flag != (FILLING_TYPE | FILLING_REPEAT |
				  FILLING_BYTE_SEQ | FILLING_OFFSET)) {
		PRINTM(MERROR, "Filter item fill error\n");
		ret = MLAN_STATUS_FAILURE;
		goto done;
	}

	/* Format of decimal num:
	 * |   val      |    5 bytes    |      val      |        1 byte | |
	 * bytes seq  |    offset     |    mask seq   |  type (TYPE_BIT_EQ)   |
	 */

	/* push bytes seq */
	memset(pmadapter, &op, 0, sizeof(op));
	op.operand_type = OPERAND_BYTE_SEQ;
	op.val[0] = filter->num_byte_seq;
	memcpy_ext(pmadapter, &op.val[1], filter->byte_seq,
		   filter->num_byte_seq, MAX_NUM_BYTE_SEQ);
	ret = mef_push(pmadapter, s, &op);
	if (ret != MLAN_STATUS_SUCCESS)
		goto done;

	/* push offset */
	memset(pmadapter, &op, 0, sizeof(op));
	op.operand_type = OPERAND_DNUM;
	dnum = filter->offset;
	memcpy_ext(pmadapter, op.val, &dnum, sizeof(dnum), sizeof(op.val));
	ret = mef_push(pmadapter, s, &op);
	if (ret != MLAN_STATUS_SUCCESS)
		goto done;

	/* push mask seq */
	memset(pmadapter, &op, 0, sizeof(op));
	op.operand_type = OPERAND_BYTE_SEQ;
	op.val[0] = filter->num_mask_seq;
	memcpy_ext(pmadapter, &op.val[1], filter->mask_seq,
		   filter->num_mask_seq, MAX_NUM_BYTE_SEQ);
	ret = mef_push(pmadapter, s, &op);
	if (ret != MLAN_STATUS_SUCCESS)
		goto done;

	/* push type */
	memset(pmadapter, &op, 0, sizeof(op));
	op.operand_type = TYPE_BIT_EQ;
	ret = mef_push(pmadapter, s, &op);

done:
	LEAVE();
	return ret;
}

/**
 *  @brief push filter to stack
 *
 *  @param pmadapter    A pointer to mlan_adapter structure
 *  @param s        A pointer to mef_stack
 *  @param filter       A pointer to filter item
 *
 *  @return             MLAN_STATUS_SUCCESS or FAIL
 */
static mlan_status
wlan_push_filter(pmlan_adapter pmadapter, mef_stack *s, mef_filter_t *filter)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;

	switch (filter->type) {
	case TYPE_DNUM_EQ:
		ret = push_filter_dnum_eq(pmadapter, s, filter);
		break;
	case TYPE_BYTE_EQ:
		ret = push_filter_byte_eq(pmadapter, s, filter);
		break;
	case TYPE_BIT_EQ:
		ret = push_filter_bit_eq(pmadapter, s, filter);
		break;
	default:
		PRINTM(MERROR, "Invalid filter type\n");
		ret = MLAN_STATUS_FAILURE;
		break;
	}
	return ret;
}

/**
 *  @brief generate mef data
 *
 *  @param pmadapter    A pointer to mlan_adapter structure
 *  @param s        A pointer to mef_stack
 *  @param entry        A pointer to mef_entry_t
 *
 *  @return             MLAN_STATUS_SUCCESS or FAIL
 */
static mlan_status
wlan_generate_mef_filter_stack(pmlan_adapter pmadapter,
			       mef_stack *s, mef_entry_t *entry)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	mef_op op;
	int i;

	ENTER();

	for (i = 0; i < entry->filter_num; i++) {
		ret = wlan_push_filter(pmadapter, s, &entry->filter_item[i]);
		if (ret != MLAN_STATUS_SUCCESS) {
			PRINTM(MERROR, "push filter to stack error\n");
			goto done;
		}
		if (i != 0) {
			memset(pmadapter, &op, 0, sizeof(op));
			op.operand_type = entry->rpn[i];
			ret = mef_push(pmadapter, s, &op);
			if (ret != MLAN_STATUS_SUCCESS) {
				PRINTM(MERROR, "push filter rpn error\n");
				goto done;
			}
		}
	}
done:
	LEAVE();
	return ret;
}

/**
 *  @brief Set the mef entries to firmware
 *
 *  @param pmpriv        A pointer to mlan_private structure
 *  @param pmadapter    A pointer to mlan_adapter structure
 *  @param pmef      A pointer to mef_cfg structure
 *
 *  @return             MLAN_STATUS_SUCCESS or FAIL
 */
mlan_status
wlan_set_mef_entry(mlan_private *pmpriv,
		   pmlan_adapter pmadapter, mef_cfg_data * pmef)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	mlan_ds_misc_cmd *hostcmd;
	HostCmd_DS_GEN *hostcmd_hdr;
	HostCmd_DS_MEF_CFG *mef_hdr;
	mef_entry_header *entry_hdr;
	mef_stack *stack;
	mef_entry_t *pentry;
	t_u8 *buf;
	t_u32 i, buf_len;
	pmlan_callbacks pcb;

	ENTER();

	if (pmef->entry_num > MAX_NUM_ENTRIES) {
		PRINTM(MERROR, "Too many entries\n");
		ret = MLAN_STATUS_FAILURE;
		goto done;
	}
	pcb = &pmadapter->callbacks;
	ret = pcb->moal_malloc(pmadapter->pmoal_handle,
			       sizeof(mlan_ds_misc_cmd), MLAN_MEM_DEF,
			       (t_u8 **)&hostcmd);
	if (ret != MLAN_STATUS_SUCCESS || hostcmd == MNULL) {
		PRINTM(MERROR, "Failed to allocate cmd data buffer\n");
		ret = MLAN_STATUS_FAILURE;
		goto err_handle;
	}

	/** Fill the cmd header data*/
	memset(pmadapter, hostcmd, 0, sizeof(mlan_ds_misc_cmd));
	buf = hostcmd->cmd;
	hostcmd_hdr = (HostCmd_DS_GEN *)buf;
	hostcmd_hdr->command = wlan_cpu_to_le16(HostCmd_CMD_MEF_CFG);
	buf_len = S_DS_GEN;

	/** Fill HostCmd_DS_MEF_CFG*/
	mef_hdr = (HostCmd_DS_MEF_CFG *)(buf + buf_len);
	mef_hdr->criteria = wlan_cpu_to_le32(pmef->criteria);
	mef_hdr->nentries = wlan_cpu_to_le16(pmef->entry_num);
	buf_len += sizeof(HostCmd_DS_MEF_CFG);

	/** generate mef entry data*/
	for (i = 0, pentry = pmef->pentry; i < pmef->entry_num; i++, pentry++) {
		/** Fill entry header data*/
		entry_hdr = (mef_entry_header *)(buf + buf_len);
		entry_hdr->mode = pentry->mode;
		entry_hdr->action = pentry->action;
		buf_len += sizeof(mef_entry_header);

		/** Fill Stack data*/
		stack = (mef_stack *)(buf + buf_len);
		ret = wlan_generate_mef_filter_stack(pmadapter, stack, pentry);
		if (ret != MLAN_STATUS_SUCCESS) {
			PRINTM(MERROR, "Generate mef data error\n");
			goto err_handle;
		}
		buf_len += (stack->sp + sizeof(stack->sp));
	}
	hostcmd_hdr->size = wlan_cpu_to_le16(buf_len);
	hostcmd->len = wlan_cpu_to_le32(buf_len);

	DBG_HEXDUMP(MCMD_D, "MEF DATA", (t_u8 *)hostcmd, buf_len + 4);

	/** Send command to firmware*/
	ret = wlan_prepare_cmd(pmpriv, 0, 0, 0, (t_void *)MNULL,
			       (t_void *)hostcmd);

err_handle:
	if (hostcmd)
		pcb->moal_mfree(pmadapter->pmoal_handle, (t_u8 *)hostcmd);
done:
	LEAVE();
	return ret;
}

/*
 *  @brief generate Host_CMD_MEF_CFG cmd data to firmware
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param pmadapter    A pointer to mlan_adapter structure
 *
 *  @return             MLAN_STATUS_SUCCESS or FAIL
 */
mlan_status
wlan_process_mef_cfg_cmd(mlan_private *pmpriv, pmlan_adapter pmadapter)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	pmlan_callbacks pcb;
	mef_cfg_data mef;
	mef_entry_t *pentry;
	mef_entry *pmef;
	t_u16 entry_num = 0;

	ENTER();

	pcb = &pmadapter->callbacks;

	/** check how many entries in adapter*/
	pmef = &pmadapter->entry_cfg;
	entry_num += pmef->enable_autoarp_entry;
	entry_num += pmef->num_wowlan_entry;
	entry_num += pmef->num_ipv6_ns_offload;
	if (!entry_num) {
		PRINTM(MIOCTL, "No filter entries\n");
		goto done;
	}

	ret = pcb->moal_malloc(pmadapter->pmoal_handle,
			       sizeof(mef_entry_t) * entry_num, MLAN_MEM_DEF,
			       (t_u8 **)&mef.pentry);
	if (ret != MLAN_STATUS_SUCCESS || mef.pentry == MNULL) {
		PRINTM(MERROR, "Failed to allocate cmd data buffer\n");
		ret = MLAN_STATUS_FAILURE;
		goto err_handle;
	}
	/** Fill mef_cfg structure*/
	mef.criteria = pmef->criteria;
	mef.entry_num = entry_num;
	memset(pmadapter, mef.pentry, 0, sizeof(mef_entry_t) * entry_num);
	pentry = mef.pentry;
	/** Fill mef_entry_t structure*/
	/** Copy Auto arp response entry*/
	if (pmef->enable_autoarp_entry) {
		memcpy_ext(pmadapter, pentry, &pmef->entry[5],
			   sizeof(mef_entry_t), sizeof(mef_entry_t));
		pentry += pmef->enable_autoarp_entry;
	}
	/** Copy wowlan entry*/
	if (pmef->num_wowlan_entry) {
		memcpy_ext(pmadapter, pentry, &pmef->entry[6],
			   sizeof(mef_entry_t), sizeof(mef_entry_t));
		pentry += pmef->num_wowlan_entry;
	}
	/** Copy IPv6 NS message offload entry */
	if (pmef->num_ipv6_ns_offload)
		memcpy_ext(pmadapter, pentry, &pmef->entry[7],
			   sizeof(mef_entry_t), sizeof(mef_entry_t));

	/** Set Entries to firmware*/
	ret = wlan_set_mef_entry(pmpriv, pmadapter, &mef);
	if (ret != MLAN_STATUS_SUCCESS)
		PRINTM(MERROR, "Set MEF entries error\n");

err_handle:
	if (mef.pentry)
		pcb->moal_mfree(pmadapter->pmoal_handle, (t_u8 *)mef.pentry);
done:
	LEAVE();
	return ret;
}

/*  @brief Get/Set NV-FLT-CONFIG parameters
 *
 *  @param pmadapter    A pointer to mlan_adapter structure
 *  @param pioctl_req   A pointer to ioctl request buffer
 *
 *  @return             MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_misc_ioctl_mef_flt_cfg(pmlan_adapter pmadapter, pmlan_ioctl_req pioctl_req)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	mlan_ds_misc_cfg *misc_cfg = MNULL;
	mlan_ds_misc_mef_flt_cfg *mef_cfg = MNULL;
	mef_entry *pmef = MNULL;

	ENTER();

	misc_cfg = (mlan_ds_misc_cfg *)pioctl_req->pbuf;
	mef_cfg = &misc_cfg->param.mef_flt_cfg;
	pmef = &pmadapter->entry_cfg;
	switch (pioctl_req->action) {
	case MLAN_ACT_SET:
		if (mef_cfg->mef_act_type == MEF_ACT_WOWLAN) {
			pmef->num_wowlan_entry = 1;
			pmef->criteria |= mef_cfg->criteria;
			memcpy_ext(pmadapter, &pmef->entry[6],
				   &mef_cfg->mef_entry, sizeof(mef_entry_t),
				   sizeof(mef_entry_t));
		}
		if (mef_cfg->mef_act_type == MEF_ACT_IPV6_NS) {
			pmef->num_ipv6_ns_offload = 1;
			pmef->criteria |= mef_cfg->criteria;
			memcpy_ext(pmadapter, &pmef->entry[7],
				   &mef_cfg->mef_entry, sizeof(mef_entry_t),
				   sizeof(mef_entry_t));
		}
		/** Set AUTO ARP Entry to adapter*/
		if (mef_cfg->mef_act_type == MEF_ACT_AUTOARP) {
			if (mef_cfg->op_code & MLAN_IPADDR_OP_AUTO_ARP_RESP) {
				pmef->enable_autoarp_entry = 1;
				pmef->criteria |= mef_cfg->criteria;
				memcpy_ext(pmadapter, &pmef->entry[5],
					   &mef_cfg->mef_entry,
					   sizeof(mef_entry_t),
					   sizeof(mef_entry_t));
				if (MLAN_STATUS_SUCCESS !=
				    wlan_process_mef_cfg_cmd(pmadapter->
							     priv[pioctl_req->
								  bss_index],
							     pmadapter))
					PRINTM(MERROR,
					       "Set MEF Entries Error\n");
			} else if (mef_cfg->op_code & MLAN_IPADDR_OP_IP_REMOVE) {
				pmef->enable_autoarp_entry = 0;
				memset(pmadapter, &pmef->entry[5], 0,
				       sizeof(mef_entry_t));
			}
		}
		break;
	case MLAN_ACT_GET:
		if (mef_cfg->mef_act_type == MEF_ACT_WOWLAN)
			memcpy_ext(pmadapter, &mef_cfg->mef_entry,
				   &pmef->entry[6], sizeof(mef_entry_t),
				   sizeof(mef_entry_t));
		break;
	default:
		pioctl_req->status_code = MLAN_ERROR_IOCTL_INVALID;
		ret = MLAN_STATUS_FAILURE;
		break;
	}

	LEAVE();
	return ret;
}

/**
 *  @brief Set/Get WPA passphrase for esupplicant
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_PENDING --success, otherwise fail
 */
mlan_status
wlan_sec_ioctl_passphrase(pmlan_adapter pmadapter, pmlan_ioctl_req pioctl_req)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_ds_sec_cfg *sec = MNULL;
	t_u16 cmd_action = 0;
#ifdef STA_SUPPORT
	BSSDescriptor_t *pbss_desc;
	int i = 0;
#endif
	ENTER();

	sec = (mlan_ds_sec_cfg *)pioctl_req->pbuf;
#ifdef DRV_EMBEDDED_SUPPLICANT
	if (GET_BSS_ROLE(pmpriv) == MLAN_BSS_ROLE_STA &&
	    !IS_FW_SUPPORT_SUPPLICANT(pmpriv->adapter)) {
		if (sec->param.passphrase.psk_type == MLAN_PSK_QUERY)
			SupplicantQueryPassphrase(pmpriv->psapriv,
						  (void *)&sec->param.
						  passphrase);
		else if (sec->param.passphrase.psk_type == MLAN_PSK_CLEAR)
			SupplicantClearPMK(pmpriv->psapriv,
					   (void *)&sec->param.passphrase);
		else
			SupplicantSetPassphrase(pmpriv->psapriv,
						(void *)&sec->param.passphrase);

		LEAVE();
		return ret;
	}
#endif

	if (!IS_FW_SUPPORT_SUPPLICANT(pmpriv->adapter)) {
		LEAVE();
		return ret;
	}

	if (pioctl_req->action == MLAN_ACT_SET) {
		if (sec->param.passphrase.psk_type == MLAN_PSK_CLEAR)
			cmd_action = HostCmd_ACT_GEN_REMOVE;
		else
			cmd_action = HostCmd_ACT_GEN_SET;
	} else if (pioctl_req->action == MLAN_ACT_CLEAR) {
		cmd_action = HostCmd_ACT_GEN_REMOVE;
	} else {
		if (sec->param.passphrase.psk_type == MLAN_PSK_QUERY) {
#ifdef STA_SUPPORT
			if (GET_BSS_ROLE(pmpriv) == MLAN_BSS_ROLE_STA &&
			    sec->param.passphrase.ssid.ssid_len == 0) {
				i = wlan_find_bssid_in_list(pmpriv,
							    (t_u8 *)&sec->param.
							    passphrase.bssid,
							    MLAN_BSS_MODE_AUTO);
				if (i >= 0) {
					pbss_desc = &pmadapter->pscan_table[i];
					memcpy_ext(pmadapter,
						   &sec->param.passphrase.ssid,
						   &pbss_desc->ssid,
						   sizeof(mlan_802_11_ssid),
						   sizeof(mlan_802_11_ssid));
					memset(pmadapter,
					       &sec->param.passphrase.bssid, 0,
					       MLAN_MAC_ADDR_LENGTH);
					PRINTM(MINFO,
					       "PSK_QUERY: found ssid=%s\n",
					       sec->param.passphrase.ssid.ssid);
				}
			} else
#endif
				memset(pmadapter, &sec->param.passphrase.bssid,
				       0, MLAN_MAC_ADDR_LENGTH);
		}
		cmd_action = HostCmd_ACT_GEN_GET;
	}

	/* Send request to firmware */
	ret = wlan_prepare_cmd(pmpriv, HostCmd_CMD_SUPPLICANT_PMK, cmd_action,
			       0, (t_void *)pioctl_req, (t_void *)sec);
	if (ret == MLAN_STATUS_SUCCESS)
		ret = MLAN_STATUS_PENDING;

	LEAVE();
	return ret;
}

/**
 *  @brief Set per packet Txctl and Rxinfo configuration
 *
 *  @param pmadapter    A pointer to mlan_adapter structure
 *  @param pioctl_req   A pointer to ioctl request buffer
 *
 *  @return             MLAN_STATUS_SUCCESS --success, otherwise fail
 */
mlan_status
wlan_misc_per_pkt_cfg(pmlan_adapter pmadapter, pmlan_ioctl_req pioctl_req)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_ds_misc_cfg *misc = MNULL;

	ENTER();

	misc = (mlan_ds_misc_cfg *)pioctl_req->pbuf;
	pmpriv->rx_pkt_info = MFALSE;
	if (misc->param.txrx_pkt_ctrl & RX_PKT_INFO)
		pmpriv->rx_pkt_info = MTRUE;

	LEAVE();
	return ret;
}

/**
 *  @brief Set/Get region code
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_SUCCESS --success, otherwise fail
 */
mlan_status
wlan_misc_ioctl_region(pmlan_adapter pmadapter, pmlan_ioctl_req pioctl_req)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_ds_misc_cfg *misc = MNULL;
	int i;

	ENTER();

	misc = (mlan_ds_misc_cfg *)pioctl_req->pbuf;
	if (pioctl_req->action == MLAN_ACT_GET) {
		misc->param.region_code = pmadapter->region_code;
	} else {
		if (pmadapter->otp_region && pmadapter->otp_region->force_reg) {
			PRINTM(MERROR,
			       "ForceRegionRule is set in the on-chip OTP"
			       " memory\n");
			LEAVE();
			return MLAN_STATUS_FAILURE;
		}
		for (i = 0; i < MRVDRV_MAX_REGION_CODE; i++) {
			/* Use the region code to search for the index */
			if (misc->param.region_code == region_code_index[i]) {
				pmadapter->region_code =
					(t_u16)misc->param.region_code;
				break;
			}
		}
		/* It's unidentified region code */
		if (i >= MRVDRV_MAX_REGION_CODE) {
			PRINTM(MERROR, "Region Code not identified\n");
			pioctl_req->status_code = MLAN_ERROR_INVALID_PARAMETER;
			LEAVE();
			return MLAN_STATUS_FAILURE;
		}
		pmadapter->cfp_code_bg = misc->param.region_code;
		pmadapter->cfp_code_a = misc->param.region_code;
		if (wlan_set_regiontable(pmpriv, (t_u8)pmadapter->region_code,
					 pmadapter->config_bands |
					 pmadapter->adhoc_start_band)) {
			pioctl_req->status_code = MLAN_ERROR_IOCTL_FAIL;
			ret = MLAN_STATUS_FAILURE;
		}
	}
	pioctl_req->data_read_written = sizeof(t_u32) + MLAN_SUB_COMMAND_SIZE;

	LEAVE();
	return ret;
}

/**
 *  @brief Configure GPIO independent reset
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_PENDING --success, otherwise fail
 */
mlan_status
wlan_misc_ioctl_ind_rst_cfg(pmlan_adapter pmadapter, pmlan_ioctl_req pioctl_req)
{
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_ds_misc_cfg *misc = (mlan_ds_misc_cfg *)pioctl_req->pbuf;
	mlan_status ret = MLAN_STATUS_SUCCESS;
	t_u16 cmd_action = 0;

	ENTER();

	if (pioctl_req->action == MLAN_ACT_GET)
		cmd_action = HostCmd_ACT_GEN_GET;
	else
		cmd_action = HostCmd_ACT_GEN_SET;

	/* Send request to firmware */
	ret = wlan_prepare_cmd(pmpriv, HostCmd_CMD_INDEPENDENT_RESET_CFG,
			       cmd_action, 0, (t_void *)pioctl_req,
			       (t_void *)&misc->param.ind_rst_cfg);

	if (ret == MLAN_STATUS_SUCCESS)
		ret = MLAN_STATUS_PENDING;

	LEAVE();
	return ret;
}

/**
 *  @brief  Get timestamp from firmware
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_PENDING --success, otherwise fail
 */
mlan_status
wlan_misc_ioctl_get_tsf(pmlan_adapter pmadapter, pmlan_ioctl_req pioctl_req)
{
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_status ret = MLAN_STATUS_SUCCESS;
	t_u16 cmd_action = 0;

	ENTER();

	if (pioctl_req->action == MLAN_ACT_GET)
		cmd_action = HostCmd_ACT_GEN_GET;
	else {
		PRINTM(MERROR, "No support set tsf!");
		return MLAN_STATUS_FAILURE;
	}

	/* Send request to firmware */
	ret = wlan_prepare_cmd(pmpriv, HostCmd_CMD_GET_TSF, cmd_action, 0,
			       (t_void *)pioctl_req, MNULL);

	if (ret == MLAN_STATUS_SUCCESS)
		ret = MLAN_STATUS_PENDING;

	LEAVE();
	return ret;
}

/**
 *  @brief  Create custom regulatory cfg
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_PENDING --success, otherwise fail
 */
mlan_status
wlan_misc_chan_reg_cfg(pmlan_adapter pmadapter, pmlan_ioctl_req pioctl_req)
{
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_status ret = MLAN_STATUS_SUCCESS;
	t_u16 cmd_action = 0;

	ENTER();

	if (pioctl_req->action == MLAN_ACT_GET)
		cmd_action = HostCmd_ACT_GEN_GET;
	else {
		PRINTM(MERROR, "No support set channel region cfg!");
		return MLAN_STATUS_FAILURE;
	}

	/* Send request to firmware */
	ret = wlan_prepare_cmd(pmpriv, HostCmd_CMD_CHAN_REGION_CFG, cmd_action,
			       0, (t_void *)pioctl_req, MNULL);

	if (ret == MLAN_STATUS_SUCCESS)
		ret = MLAN_STATUS_PENDING;

	LEAVE();
	return ret;
}

/**
 *  @brief Check operating class validation
 *
 *  @param pmadapter    A pointer to mlan_adapter structure
 *  @param pioctl_req   Pointer to the IOCTL request buffer
 *
 *  @return             MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
mlan_status
wlan_misc_ioctl_operclass_validation(pmlan_adapter pmadapter,
				     mlan_ioctl_req *pioctl_req)
{
	pmlan_private pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_ds_misc_cfg *misc = MNULL;
	t_u8 channel, oper_class;
	mlan_status ret = MLAN_STATUS_SUCCESS;

	ENTER();

	misc = (mlan_ds_misc_cfg *)pioctl_req->pbuf;
	channel = misc->param.bw_chan_oper.channel;
	oper_class = misc->param.bw_chan_oper.oper_class;
	if (pioctl_req->action == MLAN_ACT_GET) {
		ret = wlan_check_operclass_validation(pmpriv, channel,
						      oper_class);
	} else {
		PRINTM(MERROR, "Unsupported cmd_action\n");
		LEAVE();
		return MLAN_STATUS_FAILURE;
	}

	LEAVE();
	return ret;
}

/**
 *  @brief  Get Region channel power setting
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_PENDING --success, otherwise fail
 */
mlan_status
wlan_get_rgchnpwr_cfg(pmlan_adapter pmadapter, mlan_ioctl_req *pioctl_req)
{
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_status ret = MLAN_STATUS_SUCCESS;
	t_u16 cmd_action = 0;

	ENTER();

	cmd_action = HostCmd_ACT_GEN_GET;

	/* Send request to firmware */
	ret = wlan_prepare_cmd(pmpriv, HostCmd_CMD_CHAN_REGION_CFG, cmd_action,
			       0, (t_void *)pioctl_req, MNULL);

	if (ret == MLAN_STATUS_SUCCESS)
		ret = MLAN_STATUS_PENDING;

	LEAVE();
	return ret;
}

/**
 *  @brief  Get CHAN_TPRC setting
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_PENDING --success, otherwise fail
 */
mlan_status
wlan_get_chan_trpc_cfg(pmlan_adapter pmadapter, mlan_ioctl_req *pioctl_req)
{
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_status ret = MLAN_STATUS_SUCCESS;
	t_u16 cmd_action = 0;
	mlan_ds_misc_cfg *misc = MNULL;

	ENTER();

	misc = (mlan_ds_misc_cfg *)pioctl_req->pbuf;
	cmd_action = HostCmd_ACT_GEN_GET;

	/* Send request to firmware */
	ret = wlan_prepare_cmd(pmpriv, HostCmd_CHANNEL_TRPC_CONFIG, cmd_action,
			       0, (t_void *)pioctl_req,
			       (t_void *)&misc->param.trpc_cfg);

	if (ret == MLAN_STATUS_SUCCESS)
		ret = MLAN_STATUS_PENDING;

	LEAVE();
	return ret;
}

/**
 *  @brief Get non-global operating class
 *
 *  @param pmadapter    A pointer to mlan_adapter structure
 *  @param pioctl_req   Pointer to the IOCTL request buffer
 *
 *  @return             MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
mlan_status
wlan_misc_ioctl_oper_class(pmlan_adapter pmadapter, mlan_ioctl_req *pioctl_req)
{
	pmlan_private pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_ds_misc_cfg *misc = MNULL;
	t_u8 channel, bandwidth, oper_class = 0;
	mlan_status ret = MLAN_STATUS_SUCCESS;

	ENTER();

	misc = (mlan_ds_misc_cfg *)pioctl_req->pbuf;
	channel = misc->param.bw_chan_oper.channel;
	switch (misc->param.bw_chan_oper.bandwidth) {
	case 20:
		bandwidth = BW_20MHZ;
		break;
	case 40:
		bandwidth = BW_40MHZ;
		break;
	case 80:
		bandwidth = BW_80MHZ;
		break;
	default:
		bandwidth = BW_20MHZ;
		break;
	}

	if (pioctl_req->action == MLAN_ACT_GET) {
		ret = wlan_get_curr_oper_class(pmpriv, channel, bandwidth,
					       &oper_class);
		misc->param.bw_chan_oper.oper_class = oper_class;
	} else {
		PRINTM(MERROR, "Unsupported cmd_action\n");
		LEAVE();
		return MLAN_STATUS_FAILURE;
	}

	LEAVE();
	return ret;
}

/**
 *  @brief config dynamic bandwidth
 *
 *  @param pmadapter    A pointer to mlan_adapter structure
 *  @param pioctl_req   Pointer to the IOCTL request buffer
 *
 *  @return             MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
mlan_status
wlan_misc_ioctl_fw_dump_event(pmlan_adapter pmadapter,
			      mlan_ioctl_req *pioctl_req)
{
	pmlan_private pmpriv = pmadapter->priv[pioctl_req->bss_index];
	t_u16 cmd_action = 0;
	mlan_status ret = MLAN_STATUS_SUCCESS;

	ENTER();

	if (pioctl_req->action == MLAN_ACT_SET)
		cmd_action = HostCmd_ACT_GEN_SET;
	else if (pioctl_req->action == MLAN_ACT_GET)
		cmd_action = HostCmd_ACT_GEN_GET;
	else {
		PRINTM(MERROR, "Unsupported cmd_action\n");
		LEAVE();
		return MLAN_STATUS_FAILURE;
	}

	/* Send request to firmware */
	ret = wlan_prepare_cmd(pmpriv, HostCmd_CMD_FW_DUMP_EVENT, cmd_action, 0,
			       (t_void *)pioctl_req, MNULL);

	if (ret == MLAN_STATUS_SUCCESS)
		ret = MLAN_STATUS_PENDING;

	LEAVE();
	return ret;
}

/**
 *  @brief config boot sleep
 *
 *  @param pmadapter    A pointer to mlan_adapter structure
 *  @param pioctl_req   Pointer to the IOCTL request buffer
 *
 *  @return             MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
mlan_status
wlan_misc_bootsleep(pmlan_adapter pmadapter, pmlan_ioctl_req pioctl_req)
{
	pmlan_private pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_ds_misc_cfg *misc = MNULL;
	t_u16 cmd_action = 0;
	mlan_status ret = MLAN_STATUS_SUCCESS;

	ENTER();

	misc = (mlan_ds_misc_cfg *)pioctl_req->pbuf;

	if (pioctl_req->action == MLAN_ACT_SET)
		cmd_action = HostCmd_ACT_GEN_SET;
	else if (pioctl_req->action == MLAN_ACT_GET)
		cmd_action = HostCmd_ACT_GEN_GET;
	else {
		PRINTM(MERROR, "Unsupported cmd_action 0x%x\n",
		       pioctl_req->action);
		LEAVE();
		return MLAN_STATUS_FAILURE;
	}

	/* Send request to firmware */
	ret = wlan_prepare_cmd(pmpriv, HostCmd_CMD_BOOT_SLEEP, cmd_action, 0,
			       (t_void *)pioctl_req, &misc->param.boot_sleep);

	if (ret == MLAN_STATUS_SUCCESS)
		ret = MLAN_STATUS_PENDING;

	LEAVE();
	return ret;
}

/**
 *  @brief Set/Get Infra/Ad-hoc band configuration
 *
 *  @param pmadapter    A pointer to mlan_adapter structure
 *  @param pioctl_req   A pointer to ioctl request buffer
 *
 *  @return     MLAN_STATUS_SUCCESS --success, otherwise fail
 */
mlan_status
wlan_radio_ioctl_band_cfg(pmlan_adapter pmadapter, pmlan_ioctl_req pioctl_req)
{
	t_u32 i, global_band = 0;
	t_u32 infra_band = 0;
	t_u32 adhoc_band = 0;
	t_u32 adhoc_channel = 0;
	mlan_ds_radio_cfg *radio_cfg = MNULL;
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];

	ENTER();

	radio_cfg = (mlan_ds_radio_cfg *)pioctl_req->pbuf;
	if (pioctl_req->action == MLAN_ACT_SET) {
		infra_band = radio_cfg->param.band_cfg.config_bands;
		adhoc_band = radio_cfg->param.band_cfg.adhoc_start_band;
		adhoc_channel = radio_cfg->param.band_cfg.adhoc_channel;

		/* SET Infra band */
		if ((infra_band | pmadapter->fw_bands) & ~pmadapter->fw_bands) {
			pioctl_req->status_code = MLAN_ERROR_INVALID_PARAMETER;
			LEAVE();
			return MLAN_STATUS_FAILURE;
		}

		/* SET Ad-hoc Band */
		if ((adhoc_band | pmadapter->fw_bands) & ~pmadapter->fw_bands) {
			pioctl_req->status_code = MLAN_ERROR_INVALID_PARAMETER;
			LEAVE();
			return MLAN_STATUS_FAILURE;
		}
		if (!adhoc_band)
			adhoc_band = pmadapter->adhoc_start_band;

		for (i = 0; i < pmadapter->priv_num; i++) {
			if (pmadapter->priv[i] &&
			    pmadapter->priv[i] != pmpriv &&
			    GET_BSS_ROLE(pmadapter->priv[i]) ==
			    MLAN_BSS_ROLE_STA)
				global_band |=
					(t_u32)pmadapter->priv[i]->config_bands;
		}
		global_band |= infra_band;

		if (wlan_set_regiontable(pmpriv, (t_u8)pmadapter->region_code,
					 global_band | adhoc_band)) {
			pioctl_req->status_code = MLAN_ERROR_IOCTL_FAIL;
			LEAVE();
			return MLAN_STATUS_FAILURE;
		}
#ifdef STA_SUPPORT
		if (wlan_11d_set_universaltable(pmpriv,
						global_band | adhoc_band)) {
			pioctl_req->status_code = MLAN_ERROR_IOCTL_FAIL;
			LEAVE();
			return MLAN_STATUS_FAILURE;
		}
#endif
		pmpriv->config_bands = infra_band;
		pmadapter->config_bands = global_band;

		pmadapter->adhoc_start_band = adhoc_band;
		pmpriv->intf_state_11h.adhoc_auto_sel_chan = MFALSE;

#ifdef STA_SUPPORT
		/*
		 * If no adhoc_channel is supplied verify if the existing
		 * adhoc channel compiles with new adhoc_band
		 */
		if (!adhoc_channel) {
			if (!wlan_find_cfp_by_band_and_channel
			    (pmadapter, pmadapter->adhoc_start_band,
			     pmpriv->adhoc_channel)) {
				/* Pass back the default channel */
				radio_cfg->param.band_cfg.adhoc_channel =
					DEFAULT_AD_HOC_CHANNEL;
				if ((pmadapter->adhoc_start_band & BAND_A)
					) {
					radio_cfg->param.band_cfg.
						adhoc_channel =
						DEFAULT_AD_HOC_CHANNEL_A;
				}
			}
		} else {
			/* Return error if adhoc_band and adhoc_channel
			 * combination is invalid
			 */
			if (!wlan_find_cfp_by_band_and_channel
			    (pmadapter, pmadapter->adhoc_start_band,
			     (t_u16)adhoc_channel)) {
				pioctl_req->status_code =
					MLAN_ERROR_INVALID_PARAMETER;
				LEAVE();
				return MLAN_STATUS_FAILURE;
			}
			pmpriv->adhoc_channel = (t_u8)adhoc_channel;
		}

#endif

	} else {
		/* Infra Bands   */
		radio_cfg->param.band_cfg.config_bands = pmpriv->config_bands;
		/* Adhoc Band    */
		radio_cfg->param.band_cfg.adhoc_start_band =
			pmadapter->adhoc_start_band;
		/* Adhoc Channel */
		radio_cfg->param.band_cfg.adhoc_channel = pmpriv->adhoc_channel;
		/* FW support Bands */
		radio_cfg->param.band_cfg.fw_bands = pmadapter->fw_bands;
		PRINTM(MINFO, "Global config band = %d\n",
		       pmadapter->config_bands);
#ifdef STA_SUPPORT
#endif
	}

	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief Rx Abort Cfg
 *
 *  @param pmadapter   A pointer to mlan_adapter structure
 *  @param pioctl_req  A pointer to ioctl request buffer
 *
 *  @return        MLAN_STATUS_PENDING --success, otherwise fail
 */
mlan_status
wlan_misc_ioctl_rxabortcfg(pmlan_adapter pmadapter, pmlan_ioctl_req pioctl_req)
{
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_ds_misc_cfg *pmisc = (mlan_ds_misc_cfg *)pioctl_req->pbuf;
	mlan_status ret = MLAN_STATUS_SUCCESS;
	t_u16 cmd_action = 0;

	ENTER();

	if (pioctl_req->action == MLAN_ACT_SET)
		cmd_action = HostCmd_ACT_GEN_SET;
	else
		cmd_action = HostCmd_ACT_GEN_GET;
	ret = wlan_prepare_cmd(pmpriv, HostCmd_CMD_RX_ABORT_CFG, cmd_action, 0,
			       (t_void *)pioctl_req,
			       &(pmisc->param.rx_abort_cfg));
	if (ret == MLAN_STATUS_SUCCESS)
		ret = MLAN_STATUS_PENDING;

	LEAVE();
	return ret;
}

/**
 *  @brief Rx Abort Cfg ext
 *
 *  @param pmadapter   A pointer to mlan_adapter structure
 *  @param pioctl_req  A pointer to ioctl request buffer
 *
 *  @return        MLAN_STATUS_PENDING --success, otherwise fail
 */
mlan_status
wlan_misc_ioctl_rxabortcfg_ext(pmlan_adapter pmadapter,
			       pmlan_ioctl_req pioctl_req)
{
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_ds_misc_cfg *pmisc = (mlan_ds_misc_cfg *)pioctl_req->pbuf;
	mlan_status ret = MLAN_STATUS_SUCCESS;
	t_u16 cmd_action = 0;

	ENTER();

	if (pioctl_req->action == MLAN_ACT_SET)
		cmd_action = HostCmd_ACT_GEN_SET;
	else
		cmd_action = HostCmd_ACT_GEN_GET;
	ret = wlan_prepare_cmd(pmpriv, HostCmd_CMD_RX_ABORT_CFG_EXT, cmd_action,
			       0, (t_void *)pioctl_req,
			       &(pmisc->param.rx_abort_cfg_ext));
	if (ret == MLAN_STATUS_SUCCESS)
		ret = MLAN_STATUS_PENDING;

	LEAVE();
	return ret;
}

/**
 *  @brief Dot11mc unassociated FTM CFG
 *
 *  @param pmadapter   A pointer to mlan_adapter structure
 *  @param pioctl_req  A pointer to ioctl request buffer
 *
 *  @return        MLAN_STATUS_PENDING --success, otherwise fail
 */
mlan_status
wlan_misc_ioctl_dot11mc_unassoc_ftm_cfg(pmlan_adapter pmadapter,
					pmlan_ioctl_req pioctl_req)
{
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_ds_misc_cfg *pmisc = (mlan_ds_misc_cfg *)pioctl_req->pbuf;
	mlan_status ret = MLAN_STATUS_SUCCESS;
	t_u16 cmd_action = 0;

	ENTER();

	if (pioctl_req->action == MLAN_ACT_SET)
		cmd_action = HostCmd_ACT_GEN_SET;
	else
		cmd_action = HostCmd_ACT_GEN_GET;
	ret = wlan_prepare_cmd(pmpriv, HostCmd_CMD_DOT11MC_UNASSOC_FTM_CFG,
			       cmd_action, 0, (t_void *)pioctl_req,
			       &(pmisc->param.dot11mc_unassoc_ftm_cfg));
	if (ret == MLAN_STATUS_SUCCESS)
		ret = MLAN_STATUS_PENDING;

	LEAVE();
	return ret;
}

/**
 *  @brief Tx ampdu protection mode
 *
 *  @param pmadapter   A pointer to mlan_adapter structure
 *  @param pioctl_req  A pointer to ioctl request buffer
 *
 *  @return        MLAN_STATUS_PENDING --success, otherwise fail
 */
mlan_status
wlan_misc_ioctl_tx_ampdu_prot_mode(pmlan_adapter pmadapter,
				   pmlan_ioctl_req pioctl_req)
{
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_ds_misc_cfg *pmisc = (mlan_ds_misc_cfg *)pioctl_req->pbuf;
	mlan_status ret = MLAN_STATUS_SUCCESS;
	t_u16 cmd_action = 0;

	ENTER();

	if (pioctl_req->action == MLAN_ACT_SET)
		cmd_action = HostCmd_ACT_GEN_SET;
	else
		cmd_action = HostCmd_ACT_GEN_GET;
	ret = wlan_prepare_cmd(pmpriv, HostCmd_CMD_TX_AMPDU_PROT_MODE,
			       cmd_action, 0, (t_void *)pioctl_req,
			       &(pmisc->param.tx_ampdu_prot_mode));
	if (ret == MLAN_STATUS_SUCCESS)
		ret = MLAN_STATUS_PENDING;

	LEAVE();
	return ret;
}

/**
 *  @brief Rate adapt config
 *
 *  @param pmadapter   A pointer to mlan_adapter structure
 *  @param pioctl_req  A pointer to ioctl request buffer
 *
 *  @return        MLAN_STATUS_PENDING --success, otherwise fail
 */
mlan_status
wlan_misc_ioctl_rate_adapt_cfg(pmlan_adapter pmadapter,
			       pmlan_ioctl_req pioctl_req)
{
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_ds_misc_cfg *pmisc = (mlan_ds_misc_cfg *)pioctl_req->pbuf;
	mlan_status ret = MLAN_STATUS_SUCCESS;
	t_u16 cmd_action = 0;

	ENTER();

	if (pioctl_req->action == MLAN_ACT_SET)
		cmd_action = HostCmd_ACT_GEN_SET;
	else
		cmd_action = HostCmd_ACT_GEN_GET;
	ret = wlan_prepare_cmd(pmpriv, HostCmd_CMD_RATE_ADAPT_CFG, cmd_action,
			       0, (t_void *)pioctl_req,
			       &(pmisc->param.rate_adapt_cfg));
	if (ret == MLAN_STATUS_SUCCESS)
		ret = MLAN_STATUS_PENDING;

	LEAVE();
	return ret;
}

/**
 *  @brief CCK Desense config
 *
 *  @param pmadapter   A pointer to mlan_adapter structure
 *  @param pioctl_req  A pointer to ioctl request buffer
 *
 *  @return        MLAN_STATUS_PENDING --success, otherwise fail
 */
mlan_status
wlan_misc_ioctl_cck_desense_cfg(pmlan_adapter pmadapter,
				pmlan_ioctl_req pioctl_req)
{
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_ds_misc_cfg *pmisc = (mlan_ds_misc_cfg *)pioctl_req->pbuf;
	mlan_status ret = MLAN_STATUS_SUCCESS;
	t_u16 cmd_action = 0;

	ENTER();

	if (pioctl_req->action == MLAN_ACT_SET)
		cmd_action = HostCmd_ACT_GEN_SET;
	else
		cmd_action = HostCmd_ACT_GEN_GET;
	ret = wlan_prepare_cmd(pmpriv, HostCmd_CMD_CCK_DESENSE_CFG, cmd_action,
			       0, (t_void *)pioctl_req,
			       &(pmisc->param.cck_desense_cfg));
	if (ret == MLAN_STATUS_SUCCESS)
		ret = MLAN_STATUS_PENDING;

	LEAVE();
	return ret;
}

/**
 *  @brief config dynamic bandwidth
 *
 *  @param pmadapter    A pointer to mlan_adapter structure
 *  @param pioctl_req   Pointer to the IOCTL request buffer
 *
 *  @return             MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
mlan_status
wlan_misc_ioctl_dyn_bw(pmlan_adapter pmadapter, mlan_ioctl_req *pioctl_req)
{
	pmlan_private pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_ds_misc_cfg *misc = MNULL;
	t_u16 cmd_action = 0;
	mlan_status ret = MLAN_STATUS_SUCCESS;

	ENTER();

	misc = (mlan_ds_misc_cfg *)pioctl_req->pbuf;

	if (pioctl_req->action == MLAN_ACT_SET)
		cmd_action = HostCmd_ACT_GEN_SET;
	else if (pioctl_req->action == MLAN_ACT_GET)
		cmd_action = HostCmd_ACT_GEN_GET;
	else {
		PRINTM(MERROR, "Unsupported cmd_action\n");
		LEAVE();
		return MLAN_STATUS_FAILURE;
	}

	/* Send request to firmware */
	ret = wlan_prepare_cmd(pmpriv, HostCmd_CMD_DYN_BW, cmd_action, 0,
			       (t_void *)pioctl_req, &misc->param.dyn_bw);

	if (ret == MLAN_STATUS_SUCCESS)
		ret = MLAN_STATUS_PENDING;

	LEAVE();
	return ret;
}

/**
 *  @brief Set/Get low power mode configuration parameter
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_SUCCESS --success
 */
mlan_status
wlan_power_ioctl_set_get_lpm(pmlan_adapter pmadapter,
			     pmlan_ioctl_req pioctl_req)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_ds_power_cfg *pm_cfg = MNULL;
	t_u16 cmd_action = 0, lpm = 0;

	ENTER();

	pm_cfg = (mlan_ds_power_cfg *)pioctl_req->pbuf;
	cmd_action = HostCmd_ACT_GEN_GET;
	if (pioctl_req->action == MLAN_ACT_SET) {
		cmd_action = HostCmd_ACT_GEN_SET;
		lpm = pm_cfg->param.lpm;
	}

	ret = wlan_prepare_cmd(pmpriv, HostCmd_CMD_LOW_POWER_MODE_CFG,
			       cmd_action, 0, (t_void *)pioctl_req, &lpm);

	if (ret == MLAN_STATUS_SUCCESS)
		ret = MLAN_STATUS_PENDING;

	LEAVE();
	return ret;
}

/**
 *  @brief RF Test Mode config
 *
 *  @param pmadapter   A pointer to mlan_adapter structure
 *  @param pioctl_req  A pointer to ioctl request buffer
 *
 *  @return        MLAN_STATUS_PENDING --success, otherwise fail
 */
mlan_status
wlan_misc_ioctl_rf_test_cfg(pmlan_adapter pmadapter, pmlan_ioctl_req pioctl_req)
{
	mlan_private *pmpriv = MNULL;
	mlan_ds_misc_cfg *pmisc = MNULL;
	mlan_status ret = MLAN_STATUS_FAILURE;
	t_u16 cmd_action = 0;

	ENTER();

	if (!pioctl_req)
		goto done;

	pmpriv = pmadapter->priv[pioctl_req->bss_index];
	pmisc = (mlan_ds_misc_cfg *)pioctl_req->pbuf;

	switch (pmisc->sub_command) {
	case MLAN_OID_MISC_RF_TEST_GENERIC:
		if (pioctl_req->action == MLAN_ACT_SET)
			cmd_action = HostCmd_ACT_GEN_SET;
		else
			cmd_action = HostCmd_ACT_GEN_GET;
		ret = wlan_prepare_cmd(pmpriv, HostCmd_CMD_MFG_COMMAND,
				       cmd_action, 0, (t_void *)pioctl_req,
				       &(pmisc->param.mfg_generic_cfg));
		break;
	case MLAN_OID_MISC_RF_TEST_TX_CONT:
		if (pioctl_req->action == MLAN_ACT_SET)
			cmd_action = HostCmd_ACT_GEN_SET;
		else {
			PRINTM(MERROR, "Unsupported cmd_action\n");
			ret = MLAN_STATUS_FAILURE;
			goto done;
		}
		ret = wlan_prepare_cmd(pmpriv, HostCmd_CMD_MFG_COMMAND,
				       cmd_action, 0, (t_void *)pioctl_req,
				       &(pmisc->param.mfg_tx_cont));
		break;
	case MLAN_OID_MISC_RF_TEST_TX_FRAME:
		if (pioctl_req->action == MLAN_ACT_SET)
			cmd_action = HostCmd_ACT_GEN_SET;
		else {
			PRINTM(MERROR, "Unsupported cmd_action\n");
			ret = MLAN_STATUS_FAILURE;
			goto done;
		}
		ret = wlan_prepare_cmd(pmpriv, HostCmd_CMD_MFG_COMMAND,
				       cmd_action, 0, (t_void *)pioctl_req,
				       &(pmisc->param.mfg_tx_frame2));
		break;
	case MLAN_OID_MISC_RF_TEST_HE_POWER:
		if (pioctl_req->action == MLAN_ACT_SET)
			cmd_action = HostCmd_ACT_GEN_SET;
		else {
			PRINTM(MERROR, "Unsupported cmd_action\n");
			ret = MLAN_STATUS_FAILURE;
			goto done;
		}
		ret = wlan_prepare_cmd(pmpriv, HostCmd_CMD_MFG_COMMAND,
				       cmd_action, 0, (t_void *)pioctl_req,
				       &(pmisc->param.mfg_he_power));
		break;
	}

	if (ret == MLAN_STATUS_SUCCESS)
		ret = MLAN_STATUS_PENDING;
done:
	LEAVE();
	return ret;
}

/**
 *  @brief Range ext mode config
 *
 *  @param pmadapter   A pointer to mlan_adapter structure
 *  @param pioctl_req  A pointer to ioctl request buffer
 *
 *  @return        MLAN_STATUS_PENDING --success, otherwise fail
 */
mlan_status
wlan_misc_ioctl_range_ext(pmlan_adapter pmadapter, pmlan_ioctl_req pioctl_req)
{
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_ds_misc_cfg *pmisc = (mlan_ds_misc_cfg *)pioctl_req->pbuf;
	mlan_status ret = MLAN_STATUS_SUCCESS;
	t_u16 cmd_action = 0;

	ENTER();

	if (pioctl_req->action == MLAN_ACT_SET)
		cmd_action = HostCmd_ACT_GEN_SET;
	else
		cmd_action = HostCmd_ACT_GEN_GET;
	ret = wlan_prepare_cmd(pmpriv, HostCmd_CMD_RANGE_EXT, cmd_action, 0,
			       (t_void *)pioctl_req,
			       &(pmisc->param.range_ext_mode));
	if (ret == MLAN_STATUS_SUCCESS)
		ret = MLAN_STATUS_PENDING;

	LEAVE();
	return ret;

}

#ifdef UAP_SUPPORT
/**
 *  @brief set wacp mode
 *
 *  @param pmadapter   A pointer to mlan_adapter structure
 *  @param pioctl_req  A pointer to ioctl request buffer
 *
 *  @return            MLAN_STATUS_PENDING --success, otherwise fail
 */
mlan_status
wlan_misc_ioctl_wacp_mode(IN pmlan_adapter pmadapter,
			  IN pmlan_ioctl_req pioctl_req)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_ds_misc_cfg *misc = MNULL;
	t_u16 cmd_action;

	ENTER();

	misc = (mlan_ds_misc_cfg *)pioctl_req->pbuf;

	if (pioctl_req->action == MLAN_ACT_SET)
		cmd_action = HostCmd_ACT_GEN_SET;
	else
		cmd_action = HostCmd_ACT_GEN_GET;

	ret = wlan_prepare_cmd(pmpriv,
			       HOST_CMD_APCMD_SYS_CONFIGURE,
			       cmd_action,
			       0,
			       (t_void *)pioctl_req,
			       (t_void *)&misc->param.wacp_mode);

	if (ret == MLAN_STATUS_SUCCESS)
		ret = MLAN_STATUS_PENDING;

	LEAVE();
	return ret;
}
#endif
