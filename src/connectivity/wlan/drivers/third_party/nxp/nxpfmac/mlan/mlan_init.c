/** @file mlan_init.c
 *
 *  @brief This file contains the initialization for FW
 *  and HW.
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

/********************************************************
Change log:
    10/13/2008: initial version
********************************************************/

#include "mlan.h"
#ifdef STA_SUPPORT
#include "mlan_join.h"
#endif
#include "mlan_util.h"
#include "mlan_fw.h"
#include "mlan_main.h"
#include "mlan_init.h"
#include "mlan_wmm.h"
#include "mlan_11n.h"
#include "mlan_11ac.h"
#include "mlan_11h.h"
#include "mlan_meas.h"
#ifdef SDIO
#include "mlan_sdio.h"
#endif
#ifdef PCIE
#include "mlan_pcie.h"
#endif /* PCIE */
#if defined(DRV_EMBEDDED_AUTHENTICATOR) || defined(DRV_EMBEDDED_SUPPLICANT)
#include "hostsa_init.h"
#endif
#include "mlan_11ax.h"

/********************************************************
			Global Variables
********************************************************/

/*******************************************************
			Local Functions
********************************************************/

/**
 *  @brief This function adds a BSS priority table
 *
 *  @param priv     A pointer to mlan_private structure
 *
 *  @return         MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
static mlan_status
wlan_add_bsspriotbl(pmlan_private priv)
{
	pmlan_adapter pmadapter = priv->adapter;
	mlan_bssprio_node *pbssprio = MNULL;
	mlan_status status = MLAN_STATUS_SUCCESS;

	ENTER();

	status = pmadapter->callbacks.moal_malloc(pmadapter->pmoal_handle,
						  sizeof(mlan_bssprio_node),
						  MLAN_MEM_DEF,
						  (t_u8 **)&pbssprio);
	if (status) {
		PRINTM(MERROR, "Failed to allocate bsspriotbl\n");
		LEAVE();
		return status;
	}

	pbssprio->priv = priv;

	util_init_list((pmlan_linked_list)pbssprio);

	if (!pmadapter->bssprio_tbl[priv->bss_priority].bssprio_cur)
		pmadapter->bssprio_tbl[priv->bss_priority].bssprio_cur =
			pbssprio;

	util_enqueue_list_tail(pmadapter->pmoal_handle,
			       &pmadapter->bssprio_tbl[priv->bss_priority].
			       bssprio_head, (pmlan_linked_list)pbssprio,
			       pmadapter->callbacks.moal_spin_lock,
			       pmadapter->callbacks.moal_spin_unlock);

	LEAVE();
	return status;
}

/**
 *  @brief This function deletes the BSS priority table
 *
 *  @param priv     A pointer to mlan_private structure
 *
 *  @return         N/A
 */
static t_void
wlan_delete_bsspriotbl(pmlan_private priv)
{
	int i;
	pmlan_adapter pmadapter = priv->adapter;
	mlan_bssprio_node *pbssprio_node = MNULL, *ptmp_node = MNULL,
		**ppcur = MNULL;
	pmlan_list_head phead;

	ENTER();

	for (i = 0; i < pmadapter->priv_num; ++i) {
		phead = &pmadapter->bssprio_tbl[i].bssprio_head;
		ppcur = &pmadapter->bssprio_tbl[i].bssprio_cur;
		PRINTM(MINFO,
		       "Delete BSS priority table, index = %d, i = %d, phead = %p, pcur = %p\n",
		       priv->bss_index, i, phead, *ppcur);
		if (*ppcur) {
			pbssprio_node =
				(mlan_bssprio_node *)util_peek_list(pmadapter->
								    pmoal_handle,
								    phead,
								    pmadapter->
								    callbacks.
								    moal_spin_lock,
								    pmadapter->
								    callbacks.
								    moal_spin_unlock);
			while (pbssprio_node &&
			       ((pmlan_list_head)pbssprio_node != phead)) {
				ptmp_node = pbssprio_node->pnext;
				if (pbssprio_node->priv == priv) {
					PRINTM(MINFO,
					       "Delete node, pnode = %p, pnext = %p\n",
					       pbssprio_node, ptmp_node);
					util_unlink_list(pmadapter->
							 pmoal_handle, phead,
							 (pmlan_linked_list)
							 pbssprio_node,
							 pmadapter->callbacks.
							 moal_spin_lock,
							 pmadapter->callbacks.
							 moal_spin_unlock);
					pmadapter->callbacks.
						moal_mfree(pmadapter->
							   pmoal_handle,
							   (t_u8 *)
							   pbssprio_node);
				}
				pbssprio_node = ptmp_node;
			}
			*ppcur = (mlan_bssprio_node *)phead;
		}
	}

	LEAVE();
}

/**
 *  @brief The function handles VDLL init
 *
 *  @param pmadapter    A pointer to mlan_adapter structure
 *
 *  @return             MLAN_STATUS_SUCCESS
 *
 */
static mlan_status
vdll_init(pmlan_adapter pmadapter)
{
	mlan_status status = MLAN_STATUS_SUCCESS;
	vdll_dnld_ctrl *ctrl = &pmadapter->vdll_ctrl;

	ENTER();
	memset(pmadapter, ctrl, 0, sizeof(vdll_dnld_ctrl));
#if defined(SDIO) || defined(PCIE)
	if (!IS_USB(pmadapter->card_type)) {
		ctrl->cmd_buf =
			wlan_alloc_mlan_buffer(pmadapter,
					       MRVDRV_SIZE_OF_CMD_BUFFER, 0,
					       MOAL_MALLOC_BUFFER);
		if (!ctrl->cmd_buf) {
			PRINTM(MERROR,
			       "vdll init: fail to alloc command buffer");
			status = MLAN_STATUS_FAILURE;
		}
	}
#endif
	LEAVE();
	return status;
}

/**
 *  @brief The function handles VDLL deinit
 *
 *  @param pmadapter    A pointer to mlan_adapter structure
 *
 *  @return             MLAN_STATUS_SUCCESS
 *
 */
static t_void
vdll_deinit(pmlan_adapter pmadapter)
{
	pmlan_callbacks pcb = &pmadapter->callbacks;
	ENTER();
	if (pmadapter->vdll_ctrl.vdll_mem != MNULL) {
		if (pcb->moal_vmalloc && pcb->moal_vfree)
			pcb->moal_vfree(pmadapter->pmoal_handle,
					(t_u8 *)pmadapter->vdll_ctrl.vdll_mem);
		else
			pcb->moal_mfree(pmadapter->pmoal_handle,
					(t_u8 *)pmadapter->vdll_ctrl.vdll_mem);
		pmadapter->vdll_ctrl.vdll_mem = MNULL;
		pmadapter->vdll_ctrl.vdll_len = 0;
	}
#if defined(SDIO) || defined(PCIE)
	if (!IS_USB(pmadapter->card_type) &&
	    pmadapter->vdll_ctrl.cmd_buf != MNULL) {
		wlan_free_mlan_buffer(pmadapter, pmadapter->vdll_ctrl.cmd_buf);
		pmadapter->vdll_ctrl.cmd_buf = MNULL;
	}
#endif
	LEAVE();
}

/********************************************************
			Global Functions
********************************************************/

/**
 *  @brief This function allocates buffer for the members of adapter
 *          structure like command buffer and BSSID list.
 *
 *  @param pmadapter    A pointer to mlan_adapter structure
 *
 *  @return             MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
mlan_status
wlan_allocate_adapter(pmlan_adapter pmadapter)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
#ifdef STA_SUPPORT
	t_u32 beacon_buffer_size;
	t_u32 buf_size;
	BSSDescriptor_t *ptemp_scan_table = MNULL;
	t_u8 chan_2g[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14 };
	t_u8 chan_5g[] = { 12, 16, 34, 38, 42, 46, 36, 40,
		44, 48, 52, 56, 60, 64, 100, 104,
		108, 112, 116, 120, 124, 128, 132, 136,
		140, 144, 149, 153, 157, 161, 165
	};
#endif
#ifdef SDIO
	t_u32 max_mp_regs = 0;
	t_u32 mp_tx_aggr_buf_size = 0;
	t_u32 mp_rx_aggr_buf_size = 0;
#endif

	ENTER();

#ifdef SDIO
	if (IS_SD(pmadapter->card_type)) {
		max_mp_regs = pmadapter->pcard_sd->reg->max_mp_regs;
		mp_tx_aggr_buf_size = pmadapter->pcard_sd->mp_tx_aggr_buf_size;
		mp_rx_aggr_buf_size = pmadapter->pcard_sd->mp_rx_aggr_buf_size;
	}
#endif

#ifdef STA_SUPPORT
	/* Allocate buffer to store the BSSID list */
	buf_size = sizeof(BSSDescriptor_t) * MRVDRV_MAX_BSSID_LIST;
	if (pmadapter->callbacks.moal_vmalloc &&
	    pmadapter->callbacks.moal_vfree)
		ret = pmadapter->callbacks.moal_vmalloc(pmadapter->pmoal_handle,
							buf_size,
							(t_u8 **)
							&ptemp_scan_table);
	else
		ret = pmadapter->callbacks.moal_malloc(pmadapter->pmoal_handle,
						       buf_size, MLAN_MEM_DEF,
						       (t_u8 **)
						       &ptemp_scan_table);
	if (ret != MLAN_STATUS_SUCCESS || !ptemp_scan_table) {
		PRINTM(MERROR, "Failed to allocate scan table\n");
		LEAVE();
		return MLAN_STATUS_FAILURE;
	}
	pmadapter->pscan_table = ptemp_scan_table;

	if (pmadapter->fixed_beacon_buffer)
		beacon_buffer_size = MAX_SCAN_BEACON_BUFFER;
	else
		beacon_buffer_size = DEFAULT_SCAN_BEACON_BUFFER;
	if (pmadapter->callbacks.moal_vmalloc &&
	    pmadapter->callbacks.moal_vfree)
		ret = pmadapter->callbacks.moal_vmalloc(pmadapter->pmoal_handle,
							beacon_buffer_size,
							(t_u8 **)&pmadapter->
							bcn_buf);
	else
		ret = pmadapter->callbacks.moal_malloc(pmadapter->pmoal_handle,
						       beacon_buffer_size,
						       MLAN_MEM_DEF,
						       (t_u8 **)&pmadapter->
						       bcn_buf);
	if (ret != MLAN_STATUS_SUCCESS || !pmadapter->bcn_buf) {
		PRINTM(MERROR, "Failed to allocate bcn buf\n");
		LEAVE();
		return MLAN_STATUS_FAILURE;
	}
	pmadapter->bcn_buf_size = beacon_buffer_size;

	pmadapter->num_in_chan_stats = sizeof(chan_2g);
	pmadapter->num_in_chan_stats += sizeof(chan_5g);
	buf_size = sizeof(ChanStatistics_t) * pmadapter->num_in_chan_stats;
	if (pmadapter->callbacks.moal_vmalloc &&
	    pmadapter->callbacks.moal_vfree)
		ret = pmadapter->callbacks.moal_vmalloc(pmadapter->pmoal_handle,
							buf_size,
							(t_u8 **)&pmadapter->
							pchan_stats);
	else
		ret = pmadapter->callbacks.moal_malloc(pmadapter->pmoal_handle,
						       buf_size, MLAN_MEM_DEF,
						       (t_u8 **)&pmadapter->
						       pchan_stats);
	if (ret != MLAN_STATUS_SUCCESS || !pmadapter->pchan_stats) {
		PRINTM(MERROR, "Failed to allocate channel statistics\n");
		LEAVE();
		return MLAN_STATUS_FAILURE;
	}
#endif

	/* Allocate command buffer */
	ret = wlan_alloc_cmd_buffer(pmadapter);
	if (ret != MLAN_STATUS_SUCCESS) {
		PRINTM(MERROR, "Failed to allocate command buffer\n");
		LEAVE();
		return MLAN_STATUS_FAILURE;
	}
#ifdef SDIO
	if (IS_SD(pmadapter->card_type)) {
		ret = pmadapter->callbacks.moal_malloc(pmadapter->pmoal_handle,
						       max_mp_regs +
						       DMA_ALIGNMENT,
						       MLAN_MEM_DEF |
						       MLAN_MEM_DMA,
						       (t_u8 **)&pmadapter->
						       pcard_sd->mp_regs_buf);
		if (ret != MLAN_STATUS_SUCCESS ||
		    !pmadapter->pcard_sd->mp_regs_buf) {
			PRINTM(MERROR, "Failed to allocate mp_regs_buf\n");
			LEAVE();
			return MLAN_STATUS_FAILURE;
		}
		pmadapter->pcard_sd->mp_regs =
			(t_u8 *)ALIGN_ADDR(pmadapter->pcard_sd->mp_regs_buf,
					   DMA_ALIGNMENT);

		ret = pmadapter->callbacks.moal_malloc(pmadapter->pmoal_handle,
						       MAX_SUPPORT_AMSDU_SIZE,
						       MLAN_MEM_DEF |
						       MLAN_MEM_DMA,
						       (t_u8 **)&pmadapter->
						       pcard_sd->rx_buffer);

		if (ret != MLAN_STATUS_SUCCESS ||
		    !pmadapter->pcard_sd->rx_buffer) {
			PRINTM(MERROR, "Failed to allocate receive buffer\n");
			LEAVE();
			return MLAN_STATUS_FAILURE;
		}
		pmadapter->pcard_sd->rx_buf =
			(t_u8 *)ALIGN_ADDR(pmadapter->pcard_sd->rx_buffer,
					   DMA_ALIGNMENT);

		pmadapter->pcard_sd->max_sp_tx_size = MAX_SUPPORT_AMSDU_SIZE;
		pmadapter->pcard_sd->max_sp_rx_size = MAX_SUPPORT_AMSDU_SIZE;
		ret = wlan_alloc_sdio_mpa_buffers(pmadapter,
						  mp_tx_aggr_buf_size,
						  mp_rx_aggr_buf_size);
		if (ret != MLAN_STATUS_SUCCESS) {
			PRINTM(MERROR,
			       "Failed to allocate sdio mp-a buffers\n");
			LEAVE();
			return MLAN_STATUS_FAILURE;
		}
#ifdef DEBUG_LEVEL1
		if (mlan_drvdbg & MMPA_D) {
			pmadapter->pcard_sd->mpa_buf_size =
				SDIO_MP_DBG_NUM *
				pmadapter->pcard_sd->mp_aggr_pkt_limit *
				MLAN_SDIO_BLOCK_SIZE;
			if (pmadapter->callbacks.moal_vmalloc &&
			    pmadapter->callbacks.moal_vfree)
				ret = pmadapter->callbacks.
					moal_vmalloc(pmadapter->pmoal_handle,
						     pmadapter->pcard_sd->
						     mpa_buf_size,
						     (t_u8 **)&pmadapter->
						     pcard_sd->mpa_buf);
			else
				ret = pmadapter->callbacks.
					moal_malloc(pmadapter->pmoal_handle,
						    pmadapter->pcard_sd->
						    mpa_buf_size, MLAN_MEM_DEF,
						    (t_u8 **)&pmadapter->
						    pcard_sd->mpa_buf);
			if (ret != MLAN_STATUS_SUCCESS ||
			    !pmadapter->pcard_sd->mpa_buf) {
				PRINTM(MERROR, "Failed to allocate mpa buf\n");
				LEAVE();
				return MLAN_STATUS_FAILURE;
			}
		}
#endif
	}
#endif

	pmadapter->psleep_cfm =
		wlan_alloc_mlan_buffer(pmadapter,
				       sizeof(opt_sleep_confirm_buffer), 0,
				       MOAL_MALLOC_BUFFER);

#ifdef PCIE
	/* Initialize PCIE ring buffer */
	if (IS_PCIE(pmadapter->card_type)) {
		ret = wlan_alloc_pcie_ring_buf(pmadapter);
		if (MLAN_STATUS_SUCCESS != ret) {
			PRINTM(MERROR,
			       "Failed to allocate PCIE host buffers\n");
			LEAVE();
			return MLAN_STATUS_FAILURE;
		}
	}
#endif /* PCIE */

	vdll_init(pmadapter);
	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function initializes the private structure
 *          and sets default values to the members of mlan_private.
 *
 *  @param priv     A pointer to mlan_private structure
 *
 *  @return         MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
mlan_status
wlan_init_priv(pmlan_private priv)
{
	t_u32 i;
	pmlan_adapter pmadapter = priv->adapter;
	mlan_status ret = MLAN_STATUS_SUCCESS;
#ifdef USB
	pusb_tx_aggr_params pusb_tx_aggr = MNULL;
#endif

	ENTER();

	priv->media_connected = MFALSE;
	memset(pmadapter, priv->curr_addr, 0xff, MLAN_MAC_ADDR_LENGTH);

#ifdef STA_SUPPORT
	priv->pkt_tx_ctrl = 0;
	priv->bss_mode = MLAN_BSS_MODE_INFRA;
	priv->data_rate = 0;	/* Initially indicate the rate as auto */
	priv->is_data_rate_auto = MTRUE;
	priv->bcn_avg_factor = DEFAULT_BCN_AVG_FACTOR;
	priv->data_avg_factor = DEFAULT_DATA_AVG_FACTOR;

	priv->sec_info.wep_status = Wlan802_11WEPDisabled;
	priv->sec_info.authentication_mode = MLAN_AUTH_MODE_AUTO;
	priv->sec_info.encryption_mode = MLAN_ENCRYPTION_MODE_NONE;
	for (i = 0; i < MRVL_NUM_WEP_KEY; i++)
		memset(pmadapter, &priv->wep_key[i], 0, sizeof(mrvl_wep_key_t));
	priv->wep_key_curr_index = 0;
	priv->ewpa_query = MFALSE;
	priv->curr_pkt_filter =
		HostCmd_ACT_MAC_STATIC_DYNAMIC_BW_ENABLE |
		HostCmd_ACT_MAC_RTS_CTS_ENABLE |
		HostCmd_ACT_MAC_RX_ON | HostCmd_ACT_MAC_TX_ON |
		HostCmd_ACT_MAC_ETHERNETII_ENABLE;

	priv->beacon_period = MLAN_BEACON_INTERVAL;
	priv->pattempted_bss_desc = MNULL;
	memset(pmadapter, &priv->gtk_rekey, 0,
	       sizeof(mlan_ds_misc_gtk_rekey_data));
	memset(pmadapter, &priv->curr_bss_params, 0,
	       sizeof(priv->curr_bss_params));
	priv->listen_interval = MLAN_DEFAULT_LISTEN_INTERVAL;

	memset(pmadapter, &priv->assoc_rsp_buf, 0, sizeof(priv->assoc_rsp_buf));
	priv->assoc_rsp_size = 0;

	wlan_11d_priv_init(priv);
	wlan_11h_priv_init(priv);

#ifdef UAP_SUPPORT
	priv->is_11n_enabled = MFALSE;
	priv->is_11ac_enabled = MFALSE;
	priv->is_11ax_enabled = MFALSE;
	priv->uap_bss_started = MFALSE;
	priv->uap_host_based = MFALSE;
	memset(pmadapter, &priv->uap_state_chan_cb, 0,
	       sizeof(priv->uap_state_chan_cb));
#endif
#ifdef UAP_SUPPORT
	priv->num_drop_pkts = 0;
#endif
#if defined(STA_SUPPORT)
	priv->adhoc_state_prev = ADHOC_IDLE;
	memset(pmadapter, &priv->adhoc_last_start_ssid, 0,
	       sizeof(priv->adhoc_last_start_ssid));
#endif
	priv->atim_window = 0;
	priv->adhoc_state = ADHOC_IDLE;
	priv->tx_power_level = 0;
	priv->max_tx_power_level = 0;
	priv->min_tx_power_level = 0;
	priv->tx_rate = 0;
	priv->rxpd_rate_info = 0;
	priv->rx_pkt_info = MFALSE;
	/* refer to V15 CMD_TX_RATE_QUERY */
	priv->rxpd_vhtinfo = 0;
	priv->rxpd_rate = 0;
	priv->data_rssi_last = 0;
	priv->data_rssi_avg = 0;
	priv->data_nf_avg = 0;
	priv->data_nf_last = 0;
	priv->bcn_rssi_last = 0;
	priv->bcn_rssi_avg = 0;
	priv->bcn_nf_avg = 0;
	priv->bcn_nf_last = 0;
	priv->sec_info.ewpa_enabled = MFALSE;
	priv->sec_info.wpa_enabled = MFALSE;
	priv->sec_info.wpa2_enabled = MFALSE;
	memset(pmadapter, &priv->wpa_ie, 0, sizeof(priv->wpa_ie));
	memset(pmadapter, &priv->aes_key, 0, sizeof(priv->aes_key));
	priv->wpa_ie_len = 0;
	priv->wpa_is_gtk_set = MFALSE;
#if defined(STA_SUPPORT)
	priv->pmfcfg.mfpc = 0;
	priv->pmfcfg.mfpr = 0;
	memset(pmadapter, &priv->pmfcfg, 0, sizeof(priv->pmfcfg));
#endif
	priv->sec_info.wapi_enabled = MFALSE;
	priv->wapi_ie_len = 0;
	priv->sec_info.wapi_key_on = MFALSE;
	priv->osen_ie_len = 0;
	memset(pmadapter, &priv->osen_ie, 0, sizeof(priv->osen_ie));

	memset(pmadapter, &priv->wps, 0, sizeof(priv->wps));
	memset(pmadapter, &priv->gen_ie_buf, 0, sizeof(priv->gen_ie_buf));
	priv->gen_ie_buf_len = 0;
#endif /* STA_SUPPORT */
	priv->wmm_required = MTRUE;
	priv->wmm_enabled = MFALSE;
	priv->disconnect_reason_code = 0;
	priv->wmm_qosinfo = 0;
	priv->saved_wmm_qosinfo = 0;
	priv->host_tdls_cs_support = MTRUE;
	priv->host_tdls_uapsd_support = MTRUE;
	priv->tdls_cs_channel = 0;
	priv->supp_regulatory_class_len = 0;
	priv->chan_supp_len = 0;
	memset(pmadapter, &priv->chan_supp, 0, sizeof(priv->chan_supp));
	memset(pmadapter, &priv->supp_regulatory_class, 0,
	       sizeof(priv->supp_regulatory_class));
	priv->tdls_idle_time = TDLS_IDLE_TIMEOUT;
	priv->txaggrctrl = MTRUE;
	for (i = 0; i < MAX_MGMT_IE_INDEX; i++)
		memset(pmadapter, &priv->mgmt_ie[i], 0, sizeof(custom_ie));
	priv->mgmt_frame_passthru_mask = 0;
#ifdef STA_SUPPORT
	priv->pcurr_bcn_buf = MNULL;
	priv->curr_bcn_size = 0;
	memset(pmadapter, &priv->ext_cap, 0, sizeof(priv->ext_cap));

	SET_EXTCAP_OPERMODENTF(priv->ext_cap);
	SET_EXTCAP_TDLS(priv->ext_cap);
	SET_EXTCAP_QOS_MAP(priv->ext_cap);
	/* Save default Extended Capability */
	memcpy_ext(priv->adapter, &priv->def_ext_cap, &priv->ext_cap,
		   sizeof(priv->ext_cap), sizeof(priv->def_ext_cap));
#endif /* STA_SUPPORT */

	priv->amsdu_rx_cnt = 0;
	priv->msdu_in_rx_amsdu_cnt = 0;
	priv->amsdu_tx_cnt = 0;
	priv->msdu_in_tx_amsdu_cnt = 0;
	for (i = 0; i < MAX_NUM_TID; i++)
		priv->addba_reject[i] = ADDBA_RSP_STATUS_ACCEPT;
	priv->addba_reject[6] = ADDBA_RSP_STATUS_REJECT;
	priv->addba_reject[7] = ADDBA_RSP_STATUS_REJECT;
	memcpy_ext(priv->adapter, priv->ibss_addba_reject, priv->addba_reject,
		   sizeof(priv->addba_reject), sizeof(priv->ibss_addba_reject));
	priv->max_amsdu = 0;
#ifdef STA_SUPPORT
	if (priv->bss_type == MLAN_BSS_TYPE_STA) {
		priv->add_ba_param.tx_win_size = MLAN_STA_AMPDU_DEF_TXWINSIZE;
		priv->add_ba_param.rx_win_size = MLAN_STA_AMPDU_DEF_RXWINSIZE;
	}
#endif
#ifdef WIFI_DIRECT_SUPPORT
	if (priv->bss_type == MLAN_BSS_TYPE_WIFIDIRECT) {
		priv->add_ba_param.tx_win_size = MLAN_WFD_AMPDU_DEF_TXRXWINSIZE;
		priv->add_ba_param.rx_win_size = MLAN_WFD_AMPDU_DEF_TXRXWINSIZE;
	}
#endif
#ifdef UAP_SUPPORT
	if (priv->bss_type == MLAN_BSS_TYPE_UAP) {
		priv->add_ba_param.tx_win_size = MLAN_UAP_AMPDU_DEF_TXWINSIZE;
		priv->add_ba_param.rx_win_size = MLAN_UAP_AMPDU_DEF_RXWINSIZE;
		priv->aggr_prio_tbl[6].ampdu_user =
			priv->aggr_prio_tbl[7].ampdu_user =
			BA_STREAM_NOT_ALLOWED;
	}
#endif
	priv->user_rxwinsize = priv->add_ba_param.rx_win_size;
	memset(pmadapter, priv->rx_seq, 0, sizeof(priv->rx_seq));
	priv->port_ctrl_mode = MTRUE;
	priv->port_open = MFALSE;
	priv->prior_port_status = MFALSE;
	priv->tx_pause = MFALSE;
	priv->hotspot_cfg = 0;

	priv->intf_hr_len = pmadapter->ops.intf_header_len;
#ifdef USB
	if (IS_USB(pmadapter->card_type)) {
		pusb_tx_aggr =
			wlan_get_usb_tx_aggr_params(pmadapter, priv->port);
		if (pusb_tx_aggr && pusb_tx_aggr->aggr_ctrl.aggr_mode ==
		    MLAN_USB_AGGR_MODE_LEN_V2) {
			priv->intf_hr_len = MLAN_USB_TX_AGGR_HEADER;
		}
		priv->port = pmadapter->tx_data_ep;
	}
#endif
	ret = wlan_add_bsspriotbl(priv);
#if defined(DRV_EMBEDDED_AUTHENTICATOR) || defined(DRV_EMBEDDED_SUPPLICANT)
	hostsa_init(priv);
#endif

	LEAVE();
	return ret;
}

/**
 *  @brief This function initializes the adapter structure
 *          and sets default values to the members of adapter.
 *
 *  @param pmadapter    A pointer to mlan_adapter structure
 *
 *  @return             N/A
 */
t_void
wlan_init_adapter(pmlan_adapter pmadapter)
{
	opt_sleep_confirm_buffer *sleep_cfm_buf = MNULL;
#ifdef USB
	t_s32 i = 0;
#endif
	ENTER();

	if (pmadapter->psleep_cfm) {
		sleep_cfm_buf = (opt_sleep_confirm_buffer
				 *)(pmadapter->psleep_cfm->pbuf +
				    pmadapter->psleep_cfm->data_offset);
	}
#ifdef MFG_CMD_SUPPORT
	if (pmadapter->init_para.mfg_mode == MLAN_INIT_PARA_DISABLED)
		pmadapter->mfg_mode = MFALSE;
	else
		pmadapter->mfg_mode = pmadapter->init_para.mfg_mode;
#endif

#ifdef STA_SUPPORT
	pmadapter->pwarm_reset_ioctl_req = MNULL;
#endif
	pmadapter->pscan_ioctl_req = MNULL;
	pmadapter->cmd_sent = MFALSE;
	pmadapter->mlan_processing = MFALSE;
	pmadapter->main_process_cnt = 0;
	pmadapter->mlan_rx_processing = MFALSE;
	pmadapter->more_rx_task_flag = MFALSE;
	pmadapter->more_task_flag = MFALSE;
	pmadapter->delay_task_flag = MFALSE;
	pmadapter->data_sent = MFALSE;
	pmadapter->data_sent_cnt = 0;

#ifdef SDIO
	if (IS_SD(pmadapter->card_type)) {
		pmadapter->pcard_sd->int_mode = pmadapter->init_para.int_mode;
		pmadapter->pcard_sd->gpio_pin = pmadapter->init_para.gpio_pin;
		pmadapter->data_sent = MTRUE;
		pmadapter->pcard_sd->mp_rd_bitmap = 0;
		pmadapter->pcard_sd->mp_wr_bitmap = 0;
		pmadapter->pcard_sd->curr_rd_port =
			pmadapter->pcard_sd->reg->start_rd_port;
		pmadapter->pcard_sd->curr_wr_port =
			pmadapter->pcard_sd->reg->start_wr_port;
		pmadapter->pcard_sd->mp_data_port_mask =
			pmadapter->pcard_sd->reg->data_port_mask;
		pmadapter->pcard_sd->mp_invalid_update = 0;
		memset(pmadapter, pmadapter->pcard_sd->mp_update, 0,
		       sizeof(pmadapter->pcard_sd->mp_update));
		pmadapter->pcard_sd->mpa_tx.buf_len = 0;
		pmadapter->pcard_sd->mpa_tx.pkt_cnt = 0;
		pmadapter->pcard_sd->mpa_tx.start_port =
			pmadapter->pcard_sd->reg->start_wr_port;

		if (!pmadapter->init_para.mpa_tx_cfg)
			pmadapter->pcard_sd->mpa_tx.enabled = MFALSE;
		else if (pmadapter->init_para.mpa_tx_cfg ==
			 MLAN_INIT_PARA_DISABLED)
			pmadapter->pcard_sd->mpa_tx.enabled = MFALSE;
		else
			pmadapter->pcard_sd->mpa_tx.enabled = MTRUE;
		pmadapter->pcard_sd->mpa_tx.pkt_aggr_limit =
			pmadapter->pcard_sd->mp_aggr_pkt_limit;

		pmadapter->pcard_sd->mpa_rx.buf_len = 0;
		pmadapter->pcard_sd->mpa_rx.pkt_cnt = 0;
		pmadapter->pcard_sd->mpa_rx.start_port =
			pmadapter->pcard_sd->reg->start_rd_port;

		if (!pmadapter->init_para.mpa_rx_cfg)
			pmadapter->pcard_sd->mpa_rx.enabled = MFALSE;
		else if (pmadapter->init_para.mpa_rx_cfg ==
			 MLAN_INIT_PARA_DISABLED)
			pmadapter->pcard_sd->mpa_rx.enabled = MFALSE;
		else
			pmadapter->pcard_sd->mpa_rx.enabled = MTRUE;
		pmadapter->pcard_sd->mpa_rx.pkt_aggr_limit =
			pmadapter->pcard_sd->mp_aggr_pkt_limit;
	}
#endif

	pmadapter->rx_pkts_queued = 0;
	pmadapter->cmd_resp_received = MFALSE;
	pmadapter->event_received = MFALSE;
	pmadapter->data_received = MFALSE;
	pmadapter->seq_num = 0;
	pmadapter->num_cmd_timeout = 0;
	pmadapter->last_init_cmd = 0;
	pmadapter->pending_ioctl = MFALSE;
	pmadapter->scan_processing = MFALSE;
	pmadapter->cmd_timer_is_set = MFALSE;
	pmadapter->dnld_cmd_in_secs = 0;

	/* PnP and power profile */
	pmadapter->surprise_removed = MFALSE;
	/* FW hang report */
	pmadapter->fw_hang_report = MFALSE;
	pmadapter->ecsa_enable = MFALSE;
	pmadapter->getlog_enable = MFALSE;

	if (!pmadapter->init_para.ps_mode) {
		pmadapter->ps_mode = DEFAULT_PS_MODE;
	} else if (pmadapter->init_para.ps_mode == MLAN_INIT_PARA_DISABLED)
		pmadapter->ps_mode = Wlan802_11PowerModeCAM;
	else
		pmadapter->ps_mode = Wlan802_11PowerModePSP;
	pmadapter->ps_state = PS_STATE_AWAKE;
	pmadapter->need_to_wakeup = MFALSE;

#ifdef STA_SUPPORT
	pmadapter->scan_block = MFALSE;
	/* Scan type */
	pmadapter->scan_type = MLAN_SCAN_TYPE_ACTIVE;
	/* Scan mode */
	pmadapter->scan_mode = HostCmd_BSS_MODE_ANY;
	/* Scan time */
	pmadapter->specific_scan_time = MRVDRV_SPECIFIC_SCAN_CHAN_TIME;
	pmadapter->active_scan_time = MRVDRV_ACTIVE_SCAN_CHAN_TIME;
	pmadapter->passive_scan_time = MRVDRV_PASSIVE_SCAN_CHAN_TIME;
	if (!pmadapter->init_para.passive_to_active_scan)
		pmadapter->passive_to_active_scan = MLAN_PASS_TO_ACT_SCAN_EN;
	else if (pmadapter->init_para.passive_to_active_scan ==
		 MLAN_INIT_PARA_DISABLED)
		pmadapter->passive_to_active_scan = MLAN_PASS_TO_ACT_SCAN_DIS;
	else
		pmadapter->passive_to_active_scan = MLAN_PASS_TO_ACT_SCAN_EN;

	pmadapter->scan_chan_gap = 0;
	pmadapter->num_in_scan_table = 0;
	memset(pmadapter, pmadapter->pscan_table, 0,
	       (sizeof(BSSDescriptor_t) * MRVDRV_MAX_BSSID_LIST));
	pmadapter->active_scan_triggered = MFALSE;
	if (!pmadapter->init_para.ext_scan)
		pmadapter->ext_scan = EXT_SCAN_TYPE_ENH;
	else if (pmadapter->init_para.ext_scan == EXT_SCAN_TYPE_ENH)
		pmadapter->ext_scan = EXT_SCAN_TYPE_ENH;
	else
		pmadapter->ext_scan = MTRUE;
	pmadapter->ext_scan_enh = MFALSE;
	pmadapter->ext_scan_timeout = MFALSE;
	pmadapter->scan_probes = DEFAULT_PROBES;

	memset(pmadapter, pmadapter->bcn_buf, 0, pmadapter->bcn_buf_size);
	pmadapter->pbcn_buf_end = pmadapter->bcn_buf;

	pmadapter->radio_on = RADIO_ON;
	if (!pmadapter->multiple_dtim)
		pmadapter->multiple_dtim = MRVDRV_DEFAULT_MULTIPLE_DTIM;

	pmadapter->local_listen_interval = 0;	/* default value in firmware will
						   be used */
#endif /* STA_SUPPORT */

	pmadapter->is_deep_sleep = MFALSE;
	pmadapter->idle_time = DEEP_SLEEP_IDLE_TIME;
	if (!pmadapter->init_para.auto_ds)
		pmadapter->init_auto_ds = DEFAULT_AUTO_DS_MODE;
	else if (pmadapter->init_para.auto_ds == MLAN_INIT_PARA_DISABLED)
		pmadapter->init_auto_ds = MFALSE;
	else
		pmadapter->init_auto_ds = MTRUE;

	pmadapter->delay_null_pkt = MFALSE;
	pmadapter->delay_to_ps = DELAY_TO_PS_DEFAULT;
	pmadapter->enhanced_ps_mode = PS_MODE_AUTO;

	pmadapter->gen_null_pkt = MFALSE;	/* Disable NULL Pkt generation-default
						 */
	pmadapter->pps_uapsd_mode = MFALSE;	/* Disable pps/uapsd mode -default
						 */

	pmadapter->pm_wakeup_card_req = MFALSE;
	pmadapter->pm_wakeup_timeout = 0;

	pmadapter->pm_wakeup_fw_try = MFALSE;

	if (!pmadapter->init_para.max_tx_buf)
		pmadapter->max_tx_buf_size =
			pmadapter->pcard_info->max_tx_buf_size;
	else
		pmadapter->max_tx_buf_size =
			(t_u16)pmadapter->init_para.max_tx_buf;
	pmadapter->tx_buf_size = MLAN_TX_DATA_BUF_SIZE_2K;
	pmadapter->curr_tx_buf_size = MLAN_TX_DATA_BUF_SIZE_2K;

#ifdef USB
	if (IS_USB(pmadapter->card_type)) {
		for (i = 0; i < MAX_USB_TX_PORT_NUM; i++) {
			pmadapter->pcard_usb->usb_tx_aggr[i].aggr_ctrl.enable =
				MFALSE;
			pmadapter->pcard_usb->usb_tx_aggr[i]
				.aggr_ctrl.aggr_mode =
				MLAN_USB_AGGR_MODE_LEN_V2;
			pmadapter->pcard_usb->usb_tx_aggr[i]
				.aggr_ctrl.aggr_align =
				MLAN_USB_TX_AGGR_V2_ALIGN;
			pmadapter->pcard_usb->usb_tx_aggr[i].aggr_ctrl.
				aggr_max = MLAN_USB_TX_AGGR_MAX_LEN;
			pmadapter->pcard_usb->usb_tx_aggr[i].aggr_ctrl.
				aggr_tmo = MLAN_USB_TX_AGGR_TIMEOUT_MSEC * 1000;

			pmadapter->pcard_usb->usb_tx_aggr[i].pmbuf_aggr = MNULL;
			pmadapter->pcard_usb->usb_tx_aggr[i].aggr_len = 0;
			pmadapter->pcard_usb->usb_tx_aggr[i].hold_timeout_msec =
				MLAN_USB_TX_AGGR_TIMEOUT_MSEC;
			pmadapter->pcard_usb->usb_tx_aggr[i].port =
				pmadapter->tx_data_ep;
			pmadapter->pcard_usb->usb_tx_aggr[i].phandle =
				(t_void *)pmadapter;
		}

		pmadapter->pcard_usb->usb_rx_deaggr.aggr_ctrl.enable = MFALSE;
		pmadapter->pcard_usb->usb_rx_deaggr.aggr_ctrl.aggr_mode =
			MLAN_USB_AGGR_MODE_NUM;
		pmadapter->pcard_usb->usb_rx_deaggr.aggr_ctrl.aggr_align =
			MLAN_USB_RX_ALIGN_SIZE;
		pmadapter->pcard_usb->usb_rx_deaggr.aggr_ctrl.aggr_max =
			MLAN_USB_RX_MAX_AGGR_NUM;
		pmadapter->pcard_usb->usb_rx_deaggr.aggr_ctrl.aggr_tmo =
			MLAN_USB_RX_DEAGGR_TIMEOUT_USEC;

		pmadapter->pcard_usb->fw_usb_aggr = MTRUE;
	}
#endif

	pmadapter->is_hs_configured = MFALSE;
	pmadapter->hs_cfg.conditions = HOST_SLEEP_DEF_COND;
	pmadapter->hs_cfg.gpio = HOST_SLEEP_DEF_GPIO;
	pmadapter->hs_cfg.gap = HOST_SLEEP_DEF_GAP;
	pmadapter->hs_activated = MFALSE;
	pmadapter->min_wake_holdoff = HOST_SLEEP_DEF_WAKE_HOLDOFF;
	pmadapter->hs_inactivity_timeout = HOST_SLEEP_DEF_INACTIVITY_TIMEOUT;

	memset(pmadapter, pmadapter->event_body, 0,
	       sizeof(pmadapter->event_body));
	pmadapter->hw_dot_11n_dev_cap = 0;
	pmadapter->hw_dev_mcs_support = 0;
	pmadapter->coex_rx_winsize = 1;
#ifdef STA_SUPPORT
	pmadapter->chan_bandwidth = 0;
	pmadapter->tdls_status = TDLS_NOT_SETUP;
#endif /* STA_SUPPORT */

	pmadapter->min_ba_threshold = MIN_BA_THRESHOLD;
	pmadapter->hw_dot_11ac_dev_cap = 0;
	pmadapter->hw_dot_11ac_mcs_support = 0;
	pmadapter->max_sta_conn = 0;
	/* Initialize 802.11d */
	wlan_11d_init(pmadapter);

	wlan_11h_init(pmadapter);

	wlan_wmm_init(pmadapter);
	wlan_init_wmm_param(pmadapter);
	pmadapter->bypass_pkt_count = 0;
	if (pmadapter->psleep_cfm) {
		pmadapter->psleep_cfm->buf_type = MLAN_BUF_TYPE_CMD;
		pmadapter->psleep_cfm->data_len = sizeof(OPT_Confirm_Sleep);
		memset(pmadapter, &sleep_cfm_buf->ps_cfm_sleep, 0,
		       sizeof(OPT_Confirm_Sleep));
		sleep_cfm_buf->ps_cfm_sleep.command =
			wlan_cpu_to_le16(HostCmd_CMD_802_11_PS_MODE_ENH);
		sleep_cfm_buf->ps_cfm_sleep.size =
			wlan_cpu_to_le16(sizeof(OPT_Confirm_Sleep));
		sleep_cfm_buf->ps_cfm_sleep.result = 0;
		sleep_cfm_buf->ps_cfm_sleep.action =
			wlan_cpu_to_le16(SLEEP_CONFIRM);
		sleep_cfm_buf->ps_cfm_sleep.sleep_cfm.resp_ctrl =
			wlan_cpu_to_le16(RESP_NEEDED);
#ifdef USB
		if (IS_USB(pmadapter->card_type)) {
			sleep_cfm_buf->hdr =
				wlan_cpu_to_le32(MLAN_USB_TYPE_CMD);
			pmadapter->psleep_cfm->data_len += MLAN_TYPE_LEN;
		}
#endif
	}
	memset(pmadapter, &pmadapter->sleep_params, 0,
	       sizeof(pmadapter->sleep_params));
	memset(pmadapter, &pmadapter->sleep_period, 0,
	       sizeof(pmadapter->sleep_period));
	memset(pmadapter, &pmadapter->saved_sleep_period, 0,
	       sizeof(pmadapter->saved_sleep_period));
	pmadapter->tx_lock_flag = MFALSE;
	pmadapter->rx_lock_flag = MFALSE;
	pmadapter->main_lock_flag = MFALSE;
	pmadapter->null_pkt_interval = 0;
	pmadapter->fw_bands = 0;
	pmadapter->config_bands = 0;
	pmadapter->adhoc_start_band = 0;
	pmadapter->pscan_channels = MNULL;
	pmadapter->fw_release_number = 0;
	pmadapter->fw_cap_info = 0;
	memset(pmadapter, &pmadapter->upld_buf, 0, sizeof(pmadapter->upld_buf));
	pmadapter->upld_len = 0;
	pmadapter->event_cause = 0;
	pmadapter->pmlan_buffer_event = MNULL;
	memset(pmadapter, &pmadapter->region_channel, 0,
	       sizeof(pmadapter->region_channel));
	pmadapter->region_code = 0;
	memcpy_ext(pmadapter, pmadapter->country_code,
		   MRVDRV_DEFAULT_COUNTRY_CODE, COUNTRY_CODE_LEN,
		   COUNTRY_CODE_LEN);
	pmadapter->bcn_miss_time_out = DEFAULT_BCN_MISS_TIMEOUT;
#ifdef STA_SUPPORT
	memset(pmadapter, &pmadapter->arp_filter, 0,
	       sizeof(pmadapter->arp_filter));
	pmadapter->arp_filter_size = 0;
#endif /* STA_SUPPORT */

#ifdef PCIE
	if (IS_PCIE(pmadapter->card_type)) {
		pmadapter->pcard_pcie->txbd_wrptr = 0;
		pmadapter->pcard_pcie->txbd_rdptr = 0;
		pmadapter->pcard_pcie->rxbd_rdptr = 0;
		pmadapter->pcard_pcie->evtbd_rdptr = 0;
#if defined(PCIE8997) || defined(PCIE8897)
		if (!pmadapter->pcard_pcie->reg->use_adma) {
			pmadapter->pcard_pcie->rxbd_wrptr =
				pmadapter->pcard_pcie->reg->
				txrx_rw_ptr_rollover_ind;
			pmadapter->pcard_pcie->evtbd_wrptr =
				EVT_RW_PTR_ROLLOVER_IND;
		}
#endif
#if defined(PCIE9098) || defined(PCIE9097)
		if (pmadapter->pcard_pcie->reg->use_adma) {
			pmadapter->pcard_pcie->rxbd_wrptr =
				pmadapter->pcard_pcie->txrx_bd_size;
			pmadapter->pcard_pcie->evtbd_wrptr = MLAN_MAX_EVT_BD;
		}
#endif
	}
#endif
	LEAVE();
	return;
}

/**
 *  @brief This function intializes the lock variables and
 *  the list heads for interface
 *
 *  @param pmadapter  A pointer to a mlan_adapter structure
 *  @param start_index   start index of mlan private
 *
 *  @return           MLAN_STATUS_SUCCESS -- on success,
 *                    otherwise MLAN_STATUS_FAILURE
 *
 */
mlan_status
wlan_init_priv_lock_list(pmlan_adapter pmadapter, t_u8 start_index)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	pmlan_private priv = MNULL;
	pmlan_callbacks pcb = &pmadapter->callbacks;
	t_s32 i = 0;
	t_u32 j = 0;
	for (i = start_index; i < pmadapter->priv_num; i++) {
		if (pmadapter->priv[i]) {
			priv = pmadapter->priv[i];
			if (pcb->moal_init_lock(pmadapter->pmoal_handle,
						&priv->rx_pkt_lock) !=
			    MLAN_STATUS_SUCCESS) {
				ret = MLAN_STATUS_FAILURE;
				goto error;
			}
			if (pcb->moal_init_lock(pmadapter->pmoal_handle,
						&priv->wmm.ra_list_spinlock) !=
			    MLAN_STATUS_SUCCESS) {
				ret = MLAN_STATUS_FAILURE;
				goto error;
			}
#ifdef STA_SUPPORT
			if (pcb->moal_init_lock(pmadapter->pmoal_handle,
						&priv->curr_bcn_buf_lock) !=
			    MLAN_STATUS_SUCCESS) {
				ret = MLAN_STATUS_FAILURE;
				goto error;
			}
#endif
		}
	}
	for (i = start_index; i < pmadapter->priv_num; ++i) {
		util_init_list_head((t_void *)pmadapter->pmoal_handle,
				    &pmadapter->bssprio_tbl[i].bssprio_head,
				    MTRUE, pmadapter->callbacks.moal_init_lock);
		pmadapter->bssprio_tbl[i].bssprio_cur = MNULL;
	}

	for (i = start_index; i < pmadapter->priv_num; i++) {
		if (pmadapter->priv[i]) {
			priv = pmadapter->priv[i];
			for (j = 0; j < MAX_NUM_TID; ++j) {
				util_init_list_head((t_void *)pmadapter->
						    pmoal_handle,
						    &priv->wmm.tid_tbl_ptr[j].
						    ra_list, MTRUE,
						    priv->adapter->callbacks.
						    moal_init_lock);
			}
			util_init_list_head((t_void *)pmadapter->pmoal_handle,
					    &priv->tx_ba_stream_tbl_ptr, MTRUE,
					    pmadapter->callbacks.
					    moal_init_lock);
			util_init_list_head((t_void *)pmadapter->pmoal_handle,
					    &priv->rx_reorder_tbl_ptr, MTRUE,
					    pmadapter->callbacks.
					    moal_init_lock);
			util_scalar_init((t_void *)pmadapter->pmoal_handle,
					 &priv->wmm.tx_pkts_queued, 0,
					 priv->wmm.ra_list_spinlock,
					 pmadapter->callbacks.moal_init_lock);
			util_scalar_init((t_void *)pmadapter->pmoal_handle,
					 &priv->wmm.highest_queued_prio,
					 HIGH_PRIO_TID,
					 priv->wmm.ra_list_spinlock,
					 pmadapter->callbacks.moal_init_lock);
			util_init_list_head((t_void *)pmadapter->pmoal_handle,
					    &priv->sta_list, MTRUE,
					    pmadapter->callbacks.
					    moal_init_lock);
			/* Initialize tdls_pending_txq */
			util_init_list_head((t_void *)pmadapter->pmoal_handle,
					    &priv->tdls_pending_txq, MTRUE,
					    pmadapter->callbacks.
					    moal_init_lock);
			/* Initialize bypass_txq */
			util_init_list_head((t_void *)pmadapter->pmoal_handle,
					    &priv->bypass_txq, MTRUE,
					    pmadapter->callbacks.
					    moal_init_lock);
		}
	}
error:
	LEAVE();
	return ret;
}

/**
 *  @brief This function intializes the lock variables and
 *  the list heads.
 *
 *  @param pmadapter  A pointer to a mlan_adapter structure
 *
 *  @return           MLAN_STATUS_SUCCESS -- on success,
 *                    otherwise MLAN_STATUS_FAILURE
 *
 */
mlan_status
wlan_init_lock_list(pmlan_adapter pmadapter)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	pmlan_callbacks pcb = &pmadapter->callbacks;
#if defined(USB)
	t_s32 i = 0;
#endif
	ENTER();

	if (pcb->moal_init_lock(pmadapter->pmoal_handle,
				&pmadapter->pmlan_lock) !=
	    MLAN_STATUS_SUCCESS) {
		ret = MLAN_STATUS_FAILURE;
		goto error;
	}
#if defined(SDIO) || defined(PCIE)
	if (!IS_USB(pmadapter->card_type)) {
		if (pcb->moal_init_lock(pmadapter->pmoal_handle,
					&pmadapter->pint_lock) !=
		    MLAN_STATUS_SUCCESS) {
			ret = MLAN_STATUS_FAILURE;
			goto error;
		}
	}
#endif
	if (pcb->moal_init_lock(pmadapter->pmoal_handle,
				&pmadapter->pmain_proc_lock) !=
	    MLAN_STATUS_SUCCESS) {
		ret = MLAN_STATUS_FAILURE;
		goto error;
	}
	if (pcb->moal_init_lock(pmadapter->pmoal_handle,
				&pmadapter->prx_proc_lock) !=
	    MLAN_STATUS_SUCCESS) {
		ret = MLAN_STATUS_FAILURE;
		goto error;
	}
	if (pcb->moal_init_lock(pmadapter->pmoal_handle,
				&pmadapter->pmlan_cmd_lock) !=
	    MLAN_STATUS_SUCCESS) {
		ret = MLAN_STATUS_FAILURE;
		goto error;
	}
#if defined(USB)
	if (IS_USB(pmadapter->card_type)) {
		for (i = 0; i < MAX_USB_TX_PORT_NUM; i++) {
			if (pcb->moal_init_lock(pmadapter->pmoal_handle,
						&pmadapter->pcard_usb->
						usb_tx_aggr[i]
						.paggr_lock) !=
			    MLAN_STATUS_SUCCESS) {
				ret = MLAN_STATUS_FAILURE;
				goto error;
			}
		}
	}
#endif

	util_init_list_head((t_void *)pmadapter->pmoal_handle,
			    &pmadapter->rx_data_queue, MTRUE,
			    pmadapter->callbacks.moal_init_lock);
	util_scalar_init((t_void *)pmadapter->pmoal_handle,
			 &pmadapter->pending_bridge_pkts, 0, MNULL,
			 pmadapter->callbacks.moal_init_lock);
	/* Initialize cmd_free_q */
	util_init_list_head((t_void *)pmadapter->pmoal_handle,
			    &pmadapter->cmd_free_q, MTRUE,
			    pmadapter->callbacks.moal_init_lock);
	/* Initialize cmd_pending_q */
	util_init_list_head((t_void *)pmadapter->pmoal_handle,
			    &pmadapter->cmd_pending_q, MTRUE,
			    pmadapter->callbacks.moal_init_lock);
	/* Initialize scan_pending_q */
	util_init_list_head((t_void *)pmadapter->pmoal_handle,
			    &pmadapter->scan_pending_q, MTRUE,
			    pmadapter->callbacks.moal_init_lock);

	/* Initialize ioctl_pending_q */
	util_init_list_head((t_void *)pmadapter->pmoal_handle,
			    &pmadapter->ioctl_pending_q, MTRUE,
			    pmadapter->callbacks.moal_init_lock);

error:
	LEAVE();
	return ret;
}

/**
 *  @brief This function releases the lock variables
 *
 *  @param pmadapter  A pointer to a mlan_adapter structure
 *
 *  @return           None
 *
 */
t_void
wlan_free_lock_list(pmlan_adapter pmadapter)
{
	pmlan_private priv = MNULL;
	pmlan_callbacks pcb = &pmadapter->callbacks;
	t_s32 i = 0;
	t_s32 j = 0;

	ENTER();

	if (pmadapter->pmlan_lock)
		pcb->moal_free_lock(pmadapter->pmoal_handle,
				    pmadapter->pmlan_lock);
#if defined(SDIO) || defined(PCIE)
	if (!IS_USB(pmadapter->card_type) && pmadapter->pint_lock)
		pcb->moal_free_lock(pmadapter->pmoal_handle,
				    pmadapter->pint_lock);
#endif
	if (pmadapter->prx_proc_lock)
		pcb->moal_free_lock(pmadapter->pmoal_handle,
				    pmadapter->prx_proc_lock);
	if (pmadapter->pmain_proc_lock)
		pcb->moal_free_lock(pmadapter->pmoal_handle,
				    pmadapter->pmain_proc_lock);
	if (pmadapter->pmlan_cmd_lock)
		pcb->moal_free_lock(pmadapter->pmoal_handle,
				    pmadapter->pmlan_cmd_lock);
#if defined(USB)
	if (IS_USB(pmadapter->card_type)) {
		for (i = 0; i < MAX_USB_TX_PORT_NUM; i++) {
			if (pmadapter->pcard_usb->usb_tx_aggr[i].paggr_lock)
				pcb->moal_free_lock(pmadapter->pmoal_handle,
						    pmadapter->pcard_usb->
						    usb_tx_aggr[i]
						    .paggr_lock);
		}
	}
#endif

	for (i = 0; i < pmadapter->priv_num; i++) {
		if (pmadapter->priv[i]) {
			priv = pmadapter->priv[i];
			if (priv->rx_pkt_lock)
				pcb->moal_free_lock(pmadapter->pmoal_handle,
						    priv->rx_pkt_lock);
			if (priv->wmm.ra_list_spinlock)
				pcb->moal_free_lock(pmadapter->pmoal_handle,
						    priv->wmm.ra_list_spinlock);
#ifdef STA_SUPPORT
			if (priv->curr_bcn_buf_lock)
				pcb->moal_free_lock(pmadapter->pmoal_handle,
						    priv->curr_bcn_buf_lock);
#endif
		}
	}

	/* Free lists */
	util_free_list_head((t_void *)pmadapter->pmoal_handle,
			    &pmadapter->rx_data_queue, pcb->moal_free_lock);

	util_scalar_free((t_void *)pmadapter->pmoal_handle,
			 &pmadapter->pending_bridge_pkts, pcb->moal_free_lock);
	util_free_list_head((t_void *)pmadapter->pmoal_handle,
			    &pmadapter->cmd_free_q,
			    pmadapter->callbacks.moal_free_lock);

	util_free_list_head((t_void *)pmadapter->pmoal_handle,
			    &pmadapter->cmd_pending_q,
			    pmadapter->callbacks.moal_free_lock);

	util_free_list_head((t_void *)pmadapter->pmoal_handle,
			    &pmadapter->scan_pending_q,
			    pmadapter->callbacks.moal_free_lock);

	util_free_list_head((t_void *)pmadapter->pmoal_handle,
			    &pmadapter->ioctl_pending_q,
			    pmadapter->callbacks.moal_free_lock);

	for (i = 0; i < pmadapter->priv_num; i++)
		util_free_list_head((t_void *)pmadapter->pmoal_handle,
				    &pmadapter->bssprio_tbl[i].bssprio_head,
				    pcb->moal_free_lock);

	for (i = 0; i < pmadapter->priv_num; i++) {
		if (pmadapter->priv[i]) {
			priv = pmadapter->priv[i];
			util_free_list_head((t_void *)pmadapter->pmoal_handle,
					    &priv->sta_list,
					    priv->adapter->callbacks.
					    moal_free_lock);
			util_free_list_head((t_void *)pmadapter->pmoal_handle,
					    &priv->tdls_pending_txq,
					    pmadapter->callbacks.
					    moal_free_lock);
			util_free_list_head((t_void *)pmadapter->pmoal_handle,
					    &priv->bypass_txq,
					    pmadapter->callbacks.
					    moal_free_lock);
			for (j = 0; j < MAX_NUM_TID; ++j)
				util_free_list_head((t_void *)priv->adapter->
						    pmoal_handle,
						    &priv->wmm.tid_tbl_ptr[j].
						    ra_list,
						    priv->adapter->callbacks.
						    moal_free_lock);
			util_free_list_head((t_void *)priv->adapter->
					    pmoal_handle,
					    &priv->tx_ba_stream_tbl_ptr,
					    priv->adapter->callbacks.
					    moal_free_lock);
			util_free_list_head((t_void *)priv->adapter->
					    pmoal_handle,
					    &priv->rx_reorder_tbl_ptr,
					    priv->adapter->callbacks.
					    moal_free_lock);
			util_scalar_free((t_void *)priv->adapter->pmoal_handle,
					 &priv->wmm.tx_pkts_queued,
					 priv->adapter->callbacks.
					 moal_free_lock);
			util_scalar_free((t_void *)priv->adapter->pmoal_handle,
					 &priv->wmm.highest_queued_prio,
					 priv->adapter->callbacks.
					 moal_free_lock);
		}
	}

	LEAVE();
	return;
}

/**
 *  @brief This function intializes the timers
 *
 *  @param pmadapter  A pointer to a mlan_adapter structure
 *
 *  @return           MLAN_STATUS_SUCCESS -- on success,
 *                    otherwise MLAN_STATUS_FAILURE
 *
 */
mlan_status
wlan_init_timer(pmlan_adapter pmadapter)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	pmlan_callbacks pcb = &pmadapter->callbacks;
#if defined(USB)
	t_s32 i = 0;
#endif
	ENTER();

	if (pcb->
	    moal_init_timer(pmadapter->pmoal_handle,
			    &pmadapter->pmlan_cmd_timer, wlan_cmd_timeout_func,
			    pmadapter) != MLAN_STATUS_SUCCESS) {
		ret = MLAN_STATUS_FAILURE;
		goto error;
	}
#if defined(USB)
	if (IS_USB(pmadapter->card_type)) {
		for (i = 0; i < MAX_USB_TX_PORT_NUM; i++) {
			if (pcb->moal_init_timer(pmadapter->pmoal_handle,
						 &pmadapter->pcard_usb->
						 usb_tx_aggr[i]
						 .paggr_hold_timer,
						 wlan_usb_tx_aggr_timeout_func,
						 &pmadapter->pcard_usb->
						 usb_tx_aggr[i]) !=
			    MLAN_STATUS_SUCCESS) {
				ret = MLAN_STATUS_FAILURE;
				goto error;
			}
		}
	}
#endif
	if (pcb->moal_init_timer(pmadapter->pmoal_handle,
				 &pmadapter->pwakeup_fw_timer,
				 wlan_wakeup_card_timeout_func,
				 pmadapter) != MLAN_STATUS_SUCCESS) {
		ret = MLAN_STATUS_FAILURE;
		goto error;
	}
	pmadapter->wakeup_fw_timer_is_set = MFALSE;
error:
	LEAVE();
	return ret;
}

/**
 *  @brief This function releases the timers
 *
 *  @param pmadapter  A pointer to a mlan_adapter structure
 *
 *  @return           None
 *
 */
t_void
wlan_free_timer(pmlan_adapter pmadapter)
{
	pmlan_callbacks pcb = &pmadapter->callbacks;
#if defined(USB)
	t_s32 i = 0;
#endif
	ENTER();

	if (pmadapter->pmlan_cmd_timer)
		pcb->moal_free_timer(pmadapter->pmoal_handle,
				     pmadapter->pmlan_cmd_timer);
#if defined(USB)
	if (IS_USB(pmadapter->card_type)) {
		for (i = 0; i < MAX_USB_TX_PORT_NUM; i++) {
			if (pmadapter->pcard_usb->usb_tx_aggr[i]
			    .paggr_hold_timer)
				pcb->moal_free_timer(pmadapter->pmoal_handle,
						     pmadapter->pcard_usb->
						     usb_tx_aggr[i]
						     .paggr_hold_timer);
		}
	}
#endif

	if (pmadapter->pwakeup_fw_timer)
		pcb->moal_free_timer(pmadapter->pmoal_handle,
				     pmadapter->pwakeup_fw_timer);

	LEAVE();
	return;
}

/**
 *  @brief  This function initializes firmware
 *
 *  @param pmadapter		A pointer to mlan_adapter
 *
 *  @return		MLAN_STATUS_SUCCESS, MLAN_STATUS_PENDING or
 * MLAN_STATUS_FAILURE
 */
mlan_status
wlan_init_fw(pmlan_adapter pmadapter)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
#ifdef PCIE
	pmlan_private priv = pmadapter->priv[0];
#endif
	ENTER();
	/* Initialize adapter structure */
	wlan_init_adapter(pmadapter);
#ifdef MFG_CMD_SUPPORT
	if (pmadapter->mfg_mode != MTRUE) {
#endif
		wlan_adapter_get_hw_spec(pmadapter);
#ifdef MFG_CMD_SUPPORT
	}
#ifdef PCIE
	else if (IS_PCIE(pmadapter->card_type)) {
		if (MLAN_STATUS_SUCCESS != wlan_set_pcie_buf_config(priv)) {
			ret = MLAN_STATUS_FAILURE;
			goto done;
		}
	}
#endif /* PCIE */
#endif /* MFG_CMD_SUPPORT */
	if (wlan_is_cmd_pending(pmadapter)) {
		/* Send the first command in queue and return */
		if (mlan_main_process(pmadapter) == MLAN_STATUS_FAILURE)
			ret = MLAN_STATUS_FAILURE;
		else
			ret = MLAN_STATUS_PENDING;
#if defined(MFG_CMD_SUPPORT) && defined(PCIE)
		if (IS_PCIE(pmadapter->card_type) && pmadapter->mfg_mode) {
			ret = MLAN_STATUS_SUCCESS;
		}
#endif
	}
#ifdef PCIE
done:
#endif
#ifdef MFG_CMD_SUPPORT
	if (pmadapter->mfg_mode == MTRUE) {
		pmadapter->hw_status = WlanHardwareStatusInitializing;
		ret = wlan_get_hw_spec_complete(pmadapter);
	}
#endif
	LEAVE();
	return ret;
}

/**
 *  @brief  This function udpate hw spec info to each interface
 *
 *  @param pmadapter		A pointer to mlan_adapter
 *
 *  @return		MLAN_STATUS_SUCCESS, MLAN_STATUS_PENDING or
 * MLAN_STATUS_FAILURE
 */
static void
wlan_update_hw_spec(pmlan_adapter pmadapter)
{
	t_u32 i;

	ENTER();

#ifdef STA_SUPPORT
	if (IS_SUPPORT_MULTI_BANDS(pmadapter))
		pmadapter->fw_bands = (t_u8)GET_FW_DEFAULT_BANDS(pmadapter);
	else
		pmadapter->fw_bands = BAND_B;

	if ((pmadapter->fw_bands & BAND_A) && (pmadapter->fw_bands & BAND_GN))
		pmadapter->fw_bands |= BAND_AN;
	if (!(pmadapter->fw_bands & BAND_G) && (pmadapter->fw_bands & BAND_GN))
		pmadapter->fw_bands &= ~BAND_GN;

	pmadapter->config_bands = pmadapter->fw_bands;
	for (i = 0; i < pmadapter->priv_num; i++) {
		if (pmadapter->priv[i]) {
			pmadapter->priv[i]->config_bands = pmadapter->fw_bands;
		}
	}

	if (pmadapter->fw_bands & BAND_A) {
		if (pmadapter->fw_bands & BAND_AN) {
			pmadapter->config_bands |= BAND_AN;
			for (i = 0; i < pmadapter->priv_num; i++) {
				if (pmadapter->priv[i])
					pmadapter->priv[i]->config_bands |=
						BAND_AN;
			}
		}
		if (pmadapter->fw_bands & BAND_AAC) {
			pmadapter->config_bands |= BAND_AAC;
			for (i = 0; i < pmadapter->priv_num; i++) {
				if (pmadapter->priv[i])
					pmadapter->priv[i]->config_bands |=
						BAND_AAC;
			}
		}
		pmadapter->adhoc_start_band = BAND_A;
		for (i = 0; i < pmadapter->priv_num; i++) {
			if (pmadapter->priv[i])
				pmadapter->priv[i]->adhoc_channel =
					DEFAULT_AD_HOC_CHANNEL_A;
		}
	} else if (pmadapter->fw_bands & BAND_G) {
		pmadapter->adhoc_start_band = BAND_G | BAND_B;
		for (i = 0; i < pmadapter->priv_num; i++) {
			if (pmadapter->priv[i])
				pmadapter->priv[i]->adhoc_channel =
					DEFAULT_AD_HOC_CHANNEL;
		}
	} else if (pmadapter->fw_bands & BAND_B) {
		pmadapter->adhoc_start_band = BAND_B;
		for (i = 0; i < pmadapter->priv_num; i++) {
			if (pmadapter->priv[i])
				pmadapter->priv[i]->adhoc_channel =
					DEFAULT_AD_HOC_CHANNEL;
		}
	}
#endif /* STA_SUPPORT */
	for (i = 0; i < pmadapter->priv_num; i++) {
		if (pmadapter->priv[i]->curr_addr[0] == 0xff)
			memmove(pmadapter, pmadapter->priv[i]->curr_addr,
				pmadapter->permanent_addr,
				MLAN_MAC_ADDR_LENGTH);
	}
	for (i = 0; i < pmadapter->priv_num; i++) {
		if (pmadapter->priv[i])
			wlan_update_11n_cap(pmadapter->priv[i]);
	}
	if (ISSUPP_BEAMFORMING(pmadapter->hw_dot_11n_dev_cap)) {
		PRINTM(MCMND, "Enable Beamforming\n");
		for (i = 0; i < pmadapter->priv_num; i++) {
			if (pmadapter->priv[i])
				pmadapter->priv[i]->tx_bf_cap =
					pmadapter->pcard_info->
					default_11n_tx_bf_cap;
		}
	}
	for (i = 0; i < pmadapter->priv_num; i++) {
		if (pmadapter->priv[i])
			wlan_update_11ac_cap(pmadapter->priv[i]);
	}
	if (IS_FW_SUPPORT_11AX(pmadapter)) {
		if (pmadapter->hw_2g_hecap_len) {
			pmadapter->fw_bands |= BAND_GAX;
			pmadapter->config_bands |= BAND_GAX;
		}
		if (pmadapter->hw_hecap_len) {
			pmadapter->fw_bands |= BAND_AAX;
			pmadapter->config_bands |= BAND_AAX;
		}
		for (i = 0; i < pmadapter->priv_num; i++) {
			if (pmadapter->priv[i]) {
				pmadapter->priv[i]->config_bands =
					pmadapter->config_bands;
				pmadapter->priv[i]->user_2g_hecap_len =
					pmadapter->hw_2g_hecap_len;
				memcpy_ext(pmadapter,
					   pmadapter->priv[i]->user_2g_he_cap,
					   pmadapter->hw_2g_he_cap,
					   pmadapter->hw_2g_hecap_len,
					   sizeof(pmadapter->priv[i]
						  ->user_2g_he_cap));
				pmadapter->priv[i]->user_hecap_len =
					pmadapter->hw_hecap_len;
				memcpy_ext(pmadapter,
					   pmadapter->priv[i]->user_he_cap,
					   pmadapter->hw_he_cap,
					   pmadapter->hw_hecap_len,
					   sizeof(pmadapter->priv[i]->
						  user_he_cap));
			}
		}
	}
	LEAVE();
	return;
}

/**
 *  @brief  This function initializes firmware for interface
 *
 *  @param pmadapter		A pointer to mlan_adapter
 *
 *  @return		MLAN_STATUS_SUCCESS, MLAN_STATUS_PENDING or
 * MLAN_STATUS_FAILURE
 */
static mlan_status
wlan_init_priv_fw(pmlan_adapter pmadapter)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	pmlan_private priv = MNULL;
	t_u8 i = 0;

	ENTER();

	wlan_init_priv_lock_list(pmadapter, 1);
	for (i = 0; i < pmadapter->priv_num; i++) {
		if (pmadapter->priv[i]) {
			priv = pmadapter->priv[i];

			/* Initialize private structure */
			ret = wlan_init_priv(priv);
			if (ret) {
				ret = MLAN_STATUS_FAILURE;
				goto done;
			}
		}
	}
#ifdef MFG_CMD_SUPPORT
	if (pmadapter->mfg_mode != MTRUE) {
#endif
		wlan_update_hw_spec(pmadapter);
		/* Issue firmware initialize commands for first BSS,
		 * for other interfaces it will be called after getting
		 * the last init command response of previous interface
		 */
		priv = wlan_get_priv(pmadapter, MLAN_BSS_ROLE_ANY);
		if (!priv) {
			ret = MLAN_STATUS_FAILURE;
			goto done;
		}

		ret = priv->ops.init_cmd(priv, MTRUE);
		if (ret == MLAN_STATUS_FAILURE)
			goto done;
#ifdef MFG_CMD_SUPPORT
	}
#endif /* MFG_CMD_SUPPORT */

	if (wlan_is_cmd_pending(pmadapter)) {
		/* Send the first command in queue and return */
		if (mlan_main_process(pmadapter) == MLAN_STATUS_FAILURE)
			ret = MLAN_STATUS_FAILURE;
		else
			ret = MLAN_STATUS_PENDING;
#if defined(MFG_CMD_SUPPORT) && defined(PCIE)
		if (IS_PCIE(pmadapter->card_type) && pmadapter->mfg_mode) {
			ret = MLAN_STATUS_SUCCESS;
			pmadapter->hw_status = WlanHardwareStatusReady;
		}
#endif
	} else {
		pmadapter->hw_status = WlanHardwareStatusReady;
	}
done:
	LEAVE();
	return ret;
}

/**
 *  @brief This function frees the structure of adapter
 *
 *  @param pmadapter    A pointer to mlan_adapter structure
 *
 *  @return             N/A
 */
t_void
wlan_free_adapter(pmlan_adapter pmadapter)
{
	mlan_callbacks *pcb = (mlan_callbacks *)&pmadapter->callbacks;
#if defined(USB)
	t_s32 i = 0;
#endif
	ENTER();

	if (!pmadapter) {
		PRINTM(MERROR, "The adapter is NULL\n");
		LEAVE();
		return;
	}

	wlan_cancel_all_pending_cmd(pmadapter, MTRUE);
	/* Free command buffer */
	PRINTM(MINFO, "Free Command buffer\n");
	wlan_free_cmd_buffer(pmadapter);

	if (pmadapter->cmd_timer_is_set) {
		/* Cancel command timeout timer */
		pcb->moal_stop_timer(pmadapter->pmoal_handle,
				     pmadapter->pmlan_cmd_timer);
		pmadapter->cmd_timer_is_set = MFALSE;
	}
#if defined(USB)
	if (IS_USB(pmadapter->card_type)) {
		for (i = 0; i < MAX_USB_TX_PORT_NUM; i++) {
			if (pmadapter->pcard_usb->usb_tx_aggr[i]
			    .aggr_hold_timer_is_set) {
				/* Cancel usb_tx_aggregation timeout timer */
				pcb->moal_stop_timer(pmadapter->pmoal_handle,
						     pmadapter->pcard_usb->
						     usb_tx_aggr[i]
						     .paggr_hold_timer);
				pmadapter->pcard_usb->usb_tx_aggr[i]
					.aggr_hold_timer_is_set = MFALSE;
			}
		}
	}
#endif
	if (pmadapter->wakeup_fw_timer_is_set) {
		/* Cancel wakeup card timer */
		pcb->moal_stop_timer(pmadapter->pmoal_handle,
				     pmadapter->pwakeup_fw_timer);
		pmadapter->wakeup_fw_timer_is_set = MFALSE;
	}
	wlan_free_fw_cfp_tables(pmadapter);
#ifdef STA_SUPPORT
	PRINTM(MINFO, "Free ScanTable\n");
	if (pmadapter->pscan_table) {
		if (pcb->moal_vmalloc && pcb->moal_vfree)
			pcb->moal_vfree(pmadapter->pmoal_handle,
					(t_u8 *)pmadapter->pscan_table);
		else
			pcb->moal_mfree(pmadapter->pmoal_handle,
					(t_u8 *)pmadapter->pscan_table);
		pmadapter->pscan_table = MNULL;
	}
	if (pmadapter->pchan_stats) {
		if (pcb->moal_vmalloc && pcb->moal_vfree)
			pcb->moal_vfree(pmadapter->pmoal_handle,
					(t_u8 *)pmadapter->pchan_stats);
		else
			pcb->moal_mfree(pmadapter->pmoal_handle,
					(t_u8 *)pmadapter->pchan_stats);
		pmadapter->pchan_stats = MNULL;
	}
	if (pmadapter->bcn_buf) {
		if (pcb->moal_vmalloc && pcb->moal_vfree)
			pcb->moal_vfree(pmadapter->pmoal_handle,
					(t_u8 *)pmadapter->bcn_buf);
		else
			pcb->moal_mfree(pmadapter->pmoal_handle,
					(t_u8 *)pmadapter->bcn_buf);
		pmadapter->bcn_buf = MNULL;
	}
#endif

	wlan_11h_cleanup(pmadapter);

#ifdef SDIO
	if (IS_SD(pmadapter->card_type)) {
		if (pmadapter->pcard_sd->mp_regs_buf) {
			pcb->moal_mfree(pmadapter->pmoal_handle,
					(t_u8 *)pmadapter->pcard_sd->
					mp_regs_buf);
			pmadapter->pcard_sd->mp_regs_buf = MNULL;
			pmadapter->pcard_sd->mp_regs = MNULL;
		}
		if (pmadapter->pcard_sd->rx_buffer) {
			pcb->moal_mfree(pmadapter->pmoal_handle,
					(t_u8 *)pmadapter->pcard_sd->rx_buffer);
			pmadapter->pcard_sd->rx_buffer = MNULL;
			pmadapter->pcard_sd->rx_buf = MNULL;
		}
		wlan_free_sdio_mpa_buffers(pmadapter);
#ifdef DEBUG_LEVEL1
		if (pmadapter->pcard_sd->mpa_buf) {
			if (pcb->moal_vmalloc && pcb->moal_vfree)
				pcb->moal_vfree(pmadapter->pmoal_handle,
						(t_u8 *)pmadapter->pcard_sd->
						mpa_buf);
			else
				pcb->moal_mfree(pmadapter->pmoal_handle,
						(t_u8 *)pmadapter->pcard_sd->
						mpa_buf);
			pmadapter->pcard_sd->mpa_buf = MNULL;
			pmadapter->pcard_sd->mpa_buf_size = 0;
		}
#endif
	}
#endif

	wlan_free_mlan_buffer(pmadapter, pmadapter->psleep_cfm);
	pmadapter->psleep_cfm = MNULL;

#ifdef PCIE
	if (IS_PCIE(pmadapter->card_type)) {
		/* Free ssu dma buffer just in case  */
		wlan_free_ssu_pcie_buf(pmadapter);
		/* Free PCIE ring buffers */
		wlan_free_pcie_ring_buf(pmadapter);
	}
#endif

	/* Free timers */
	wlan_free_timer(pmadapter);

	/* Free lock variables */
	wlan_free_lock_list(pmadapter);

#ifdef SDIO
	if (pmadapter->pcard_sd) {
		pcb->moal_mfree(pmadapter->pmoal_handle,
				(t_u8 *)pmadapter->pcard_sd);
		pmadapter->pcard_sd = MNULL;
	}
#endif
#ifdef PCIE
	if (pmadapter->pcard_pcie) {
		pcb->moal_mfree(pmadapter->pmoal_handle,
				(t_u8 *)pmadapter->pcard_pcie);
		pmadapter->pcard_pcie = MNULL;
	}
#endif
#ifdef USB
	if (pmadapter->pcard_usb) {
		pcb->moal_mfree(pmadapter->pmoal_handle,
				(t_u8 *)pmadapter->pcard_usb);
		pmadapter->pcard_usb = MNULL;
	}
#endif
	vdll_deinit(pmadapter);

	LEAVE();
	return;
}

/**
 *  @brief This function frees the structure of priv
 *
 *  @param pmpriv   A pointer to mlan_private structure
 *
 *  @return         N/A
 */
t_void
wlan_free_priv(mlan_private *pmpriv)
{
	ENTER();
	wlan_clean_txrx(pmpriv);
	wlan_delete_bsspriotbl(pmpriv);

#ifdef STA_SUPPORT
	wlan_free_curr_bcn(pmpriv);
#endif /* STA_SUPPORT */

#if defined(DRV_EMBEDDED_AUTHENTICATOR) || defined(DRV_EMBEDDED_SUPPLICANT)
	hostsa_cleanup(pmpriv);
#endif /*EMBEDDED AUTHENTICATOR */

	wlan_delete_station_list(pmpriv);
	LEAVE();
}

/**
 *  @brief This function init interface based on pmadapter's bss_attr table
 *
 *  @param pmadapter    A pointer to mlan_adapter structure
 *
 *  @return             N/A
 */
static mlan_status
wlan_init_interface(pmlan_adapter pmadapter)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	pmlan_callbacks pcb = MNULL;
	t_u8 i = 0;
	t_u32 j = 0;

	ENTER();

	pcb = &pmadapter->callbacks;
	for (i = 0; i < MLAN_MAX_BSS_NUM; i++) {
		if (pmadapter->bss_attr[i].active == MTRUE) {
			if (!pmadapter->priv[i]) {
				/* For valid bss_attr, allocate memory for
				 * private structure */
				if (pcb->moal_vmalloc && pcb->moal_vfree)
					ret = pcb->moal_vmalloc(pmadapter->
								pmoal_handle,
								sizeof
								(mlan_private),
								(t_u8 **)
								&pmadapter->
								priv[i]);
				else
					ret = pcb->moal_malloc(pmadapter->
							       pmoal_handle,
							       sizeof
							       (mlan_private),
							       MLAN_MEM_DEF,
							       (t_u8 **)
							       &pmadapter->
							       priv[i]);
				if (ret != MLAN_STATUS_SUCCESS ||
				    !pmadapter->priv[i]) {
					ret = MLAN_STATUS_FAILURE;
					goto error;
				}

				pmadapter->priv_num++;
				memset(pmadapter, pmadapter->priv[i], 0,
				       sizeof(mlan_private));
			}
			pmadapter->priv[i]->adapter = pmadapter;

			/* Save bss_type, frame_type & bss_priority */
			pmadapter->priv[i]->bss_type =
				(t_u8)pmadapter->bss_attr[i].bss_type;
			pmadapter->priv[i]->frame_type =
				(t_u8)pmadapter->bss_attr[i].frame_type;
			pmadapter->priv[i]->bss_priority =
				(t_u8)pmadapter->bss_attr[i].bss_priority;
			if (pmadapter->bss_attr[i].bss_type ==
			    MLAN_BSS_TYPE_STA)
				pmadapter->priv[i]->bss_role =
					MLAN_BSS_ROLE_STA;
			else if (pmadapter->bss_attr[i].bss_type ==
				 MLAN_BSS_TYPE_UAP)
				pmadapter->priv[i]->bss_role =
					MLAN_BSS_ROLE_UAP;
#ifdef WIFI_DIRECT_SUPPORT
			else if (pmadapter->bss_attr[i].bss_type ==
				 MLAN_BSS_TYPE_WIFIDIRECT) {
				pmadapter->priv[i]->bss_role =
					MLAN_BSS_ROLE_STA;
				if (pmadapter->bss_attr[i].bss_virtual)
					pmadapter->priv[i]->bss_virtual = MTRUE;
			}
#endif
			/* Save bss_index and bss_num */
			pmadapter->priv[i]->bss_index = i;
			pmadapter->priv[i]->bss_num =
				(t_u8)pmadapter->bss_attr[i].bss_num;

			/* init function table */
			for (j = 0; mlan_ops[j]; j++) {
				if (mlan_ops[j]->bss_role ==
				    GET_BSS_ROLE(pmadapter->priv[i])) {
					memcpy_ext(pmadapter,
						   &pmadapter->priv[i]->ops,
						   mlan_ops[j],
						   sizeof(mlan_operations),
						   sizeof(mlan_operations));
					break;
				}
			}
		}
	}
	/*wmm init */
	wlan_wmm_init(pmadapter);
	/* Initialize firmware, may return PENDING */
	ret = wlan_init_priv_fw(pmadapter);
	PRINTM(MINFO, "wlan_init_priv_fw returned ret=0x%x\n", ret);
error:
	LEAVE();
	return ret;
}

/**
 *  @brief The cmdresp handler calls this function for init_fw_complete callback
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *
 *  @return		MLAN_STATUS_SUCCESS
 *              The firmware initialization callback succeeded.
 */
mlan_status
wlan_get_hw_spec_complete(pmlan_adapter pmadapter)
{
	mlan_status status = MLAN_STATUS_SUCCESS;
	mlan_status ret = MLAN_STATUS_SUCCESS;
	pmlan_callbacks pcb = &pmadapter->callbacks;
	mlan_hw_info info;
	mlan_bss_tbl bss_tbl;

	ENTER();
#ifdef MFG_CMD_SUPPORT
	if (pmadapter->mfg_mode != MTRUE) {
#endif
		/* Check if hardware is ready */
		if (pmadapter->hw_status != WlanHardwareStatusInitializing)
			status = MLAN_STATUS_FAILURE;
		else {
			memset(pmadapter, &info, 0, sizeof(info));
			info.fw_cap = pmadapter->fw_cap_info;
			info.fw_cap_ext = pmadapter->fw_cap_ext;
			memset(pmadapter, &bss_tbl, 0, sizeof(bss_tbl));
			memcpy_ext(pmadapter, bss_tbl.bss_attr,
				   pmadapter->bss_attr, sizeof(mlan_bss_tbl),
				   sizeof(mlan_bss_tbl));
		}
		/* Invoke callback */
		ret = pcb->moal_get_hw_spec_complete(pmadapter->pmoal_handle,
						     status, &info, &bss_tbl);
		if (ret == MLAN_STATUS_SUCCESS && status == MLAN_STATUS_SUCCESS)
			memcpy_ext(pmadapter, pmadapter->bss_attr,
				   bss_tbl.bss_attr, sizeof(mlan_bss_tbl),
				   sizeof(mlan_bss_tbl));
		else {
			pmadapter->hw_status = WlanHardwareStatusNotReady;
			wlan_init_fw_complete(pmadapter);
		}
#ifdef MFG_CMD_SUPPORT
	}
#endif
	if (pmadapter->hw_status == WlanHardwareStatusInitializing)
		ret = wlan_init_interface(pmadapter);
	LEAVE();
	return ret;
}

/**
 *  @brief The cmdresp handler calls this function for init_fw_complete callback
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *
 *  @return		MLAN_STATUS_SUCCESS
 *              The firmware initialization callback succeeded.
 */
mlan_status
wlan_init_fw_complete(pmlan_adapter pmadapter)
{
	mlan_status status = MLAN_STATUS_SUCCESS;
	mlan_status ret = MLAN_STATUS_SUCCESS;
	pmlan_callbacks pcb = &pmadapter->callbacks;
	mlan_private *pmpriv = MNULL;

	ENTER();

	/* Check if hardware is ready */
	if (pmadapter->hw_status != WlanHardwareStatusReady)
		status = MLAN_STATUS_FAILURE;

	/* Reconfigure wmm parameter */
	if (status == MLAN_STATUS_SUCCESS) {
		pmpriv = wlan_get_priv(pmadapter, MLAN_BSS_ROLE_STA);
		if (pmpriv)
			status = wlan_prepare_cmd(pmpriv,
						  HostCmd_CMD_WMM_PARAM_CONFIG,
						  HostCmd_ACT_GEN_SET, 0,
						  MNULL, &pmadapter->ac_params);
	}
	/* Invoke callback */
	ret = pcb->moal_init_fw_complete(pmadapter->pmoal_handle, status);
	LEAVE();
	return ret;
}

/**
 *  @brief The cmdresp handler calls this function
 *          for shutdown_fw_complete callback
 *
 *  @param pmadapter    A pointer to mlan_adapter structure
 *
 *  @return             MLAN_STATUS_SUCCESS
 *                      The firmware shutdown callback succeeded.
 */
mlan_status
wlan_shutdown_fw_complete(pmlan_adapter pmadapter)
{
	pmlan_callbacks pcb = &pmadapter->callbacks;
	mlan_status status = MLAN_STATUS_SUCCESS;
	mlan_status ret = MLAN_STATUS_SUCCESS;

	ENTER();
	pmadapter->hw_status = WlanHardwareStatusNotReady;
	/* Invoke callback */
	ret = pcb->moal_shutdown_fw_complete(pmadapter->pmoal_handle, status);
	LEAVE();
	return ret;
}
