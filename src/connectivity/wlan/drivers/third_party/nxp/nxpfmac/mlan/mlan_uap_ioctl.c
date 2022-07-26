/** @file mlan_uap_ioctl.c
 *
 *  @brief This file contains the handling of AP mode ioctls
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
#include "mlan_11n.h"
#include "mlan_fw.h"
#include "mlan_11h.h"
#include "mlan_11ac.h"
#include "mlan_11ax.h"

/********************************************************
			Global Variables
********************************************************/

/********************************************************
			Local Functions
********************************************************/
/**
 *  @brief Stop BSS
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_PENDING --success, otherwise fail
 */
static mlan_status
wlan_uap_bss_ioctl_stop(pmlan_adapter pmadapter, pmlan_ioctl_req pioctl_req)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	pmlan_private pmpriv = pmadapter->priv[pioctl_req->bss_index];

	ENTER();

	ret = wlan_prepare_cmd(pmpriv, HOST_CMD_APCMD_BSS_STOP,
			       HostCmd_ACT_GEN_SET, 0, (t_void *)pioctl_req,
			       MNULL);
	if (ret == MLAN_STATUS_SUCCESS)
		ret = MLAN_STATUS_PENDING;

	LEAVE();
	return ret;
}

static t_bool
wlan_can_radar_det_skip(mlan_private *priv)
{
	mlan_private *priv_list[MLAN_MAX_BSS_NUM];
	mlan_private *pmpriv;
	mlan_adapter *pmadapter = priv->adapter;
	t_u8 pcount, i;

	/* In MBSS environment, if one of the BSS is already beaconing and DRCS
	 * is off then 11n_radar detection is not required for subsequent BSSes
	 * since they will follow the primary bss.
	 */
	if ((GET_BSS_ROLE(priv) == MLAN_BSS_ROLE_UAP)) {
		memset(pmadapter, priv_list, 0x00, sizeof(priv_list));
		pcount = wlan_get_privs_by_cond(pmadapter, wlan_is_intf_active,
						priv_list);
		for (i = 0; i < pcount; i++) {
			pmpriv = priv_list[i];
			if (GET_BSS_ROLE(pmpriv) == MLAN_BSS_ROLE_UAP)
				return MTRUE;
		}
	}
	return MFALSE;
}

/**
 *  @brief Callback to finish BSS IOCTL START
 *  Not to be called directly to initiate bss_start
 *
 *  @param priv A pointer to mlan_private structure (cast from t_void*)
 *
 *  @return     MLAN_STATUS_PENDING --success, otherwise fail
 *  @sa         wlan_uap_bss_ioctl_start
 */
static mlan_status
wlan_uap_callback_bss_ioctl_start(t_void *priv)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	mlan_private *pmpriv = (mlan_private *)priv;
	mlan_callbacks *pcb = (mlan_callbacks *)&pmpriv->adapter->callbacks;
	wlan_uap_get_info_cb_t *puap_state_chan_cb = &pmpriv->uap_state_chan_cb;
	t_u8 old_channel;
	t_bool under_nop = MFALSE;
	ENTER();
	/* clear callback now that we're here */
	puap_state_chan_cb->get_chan_callback = MNULL;

	/*
	 * Check if the region and channel requires we check for radar.
	 */
	if ((puap_state_chan_cb->bandcfg.chanBand == BAND_5GHZ) &&
	    !wlan_can_radar_det_skip(pmpriv) &&
	    wlan_11h_radar_detect_required(pmpriv,
					   puap_state_chan_cb->channel)) {
		/* If DFS repeater mode is on then before starting the uAP
		 * make sure that mlan0 is connected to some external AP
		 * for DFS channel operations.
		 */
		if (pmpriv->adapter->dfs_repeater) {
			pmlan_private tmpriv = MNULL;
			tmpriv = wlan_get_priv(pmpriv->adapter,
					       MLAN_BSS_ROLE_STA);

			if (tmpriv && !tmpriv->media_connected) {
				PRINTM(MERROR,
				       "BSS start is blocked when DFS-repeater\n"
				       "mode is on and STA is not connected\n");
				pcb->moal_ioctl_complete(pmpriv->adapter->
							 pmoal_handle,
							 puap_state_chan_cb->
							 pioctl_req_curr,
							 MLAN_STATUS_FAILURE);
				goto done;
			} else {
				/* STA is connected.
				 * Skip DFS check for bss_start since its a
				 * repeater mode
				 */
				goto prep_bss_start;
			}
		}

		/* first check if channel is under NOP */
		if (wlan_11h_is_channel_under_nop
		    (pmpriv->adapter, puap_state_chan_cb->channel)) {
			/* recently we've seen radar on this channel */
			ret = MLAN_STATUS_FAILURE;
			under_nop = MTRUE;
		}

		/* Check cached radar check on the channel */
		if (ret == MLAN_STATUS_SUCCESS)
			ret = wlan_11h_check_chan_report(pmpriv,
							 puap_state_chan_cb->
							 channel);

		/* Found radar: try to switch to a non-dfs channel */
		if (ret != MLAN_STATUS_SUCCESS) {
			old_channel = puap_state_chan_cb->channel;
			ret = wlan_11h_switch_non_dfs_chan(pmpriv,
							   &puap_state_chan_cb->
							   channel);

			if (ret == MLAN_STATUS_SUCCESS) {
				wlan_11h_update_bandcfg(pmpriv,
							&pmpriv->
							uap_state_chan_cb.
							bandcfg,
							puap_state_chan_cb->
							channel);
				PRINTM(MCMD_D,
				       "NOP: uap band config:0x%x  channel=%d\n",
				       pmpriv->uap_state_chan_cb.bandcfg,
				       puap_state_chan_cb->channel);

				ret = wlan_uap_set_channel(pmpriv,
							   pmpriv->
							   uap_state_chan_cb.
							   bandcfg,
							   puap_state_chan_cb->
							   channel);
				if (ret == MLAN_STATUS_SUCCESS) {
					if (under_nop) {
						PRINTM(MMSG,
						       "Channel %d under NOP,"
						       " switched to new channel %d successfully.\n",
						       old_channel,
						       puap_state_chan_cb->
						       channel);
					} else {
						PRINTM(MMSG,
						       "Radar found on channel %d,"
						       " switched to new channel %d successfully.\n",
						       old_channel,
						       puap_state_chan_cb->
						       channel);
					}
				} else {
					if (under_nop) {
						PRINTM(MMSG,
						       "Channel %d under NOP,"
						       " switch to new channel %d failed.\n",
						       old_channel,
						       puap_state_chan_cb->
						       channel);
					} else {
						PRINTM(MMSG,
						       "Radar found on channel %d,"
						       " switch to new channel %d failed.\n",
						       old_channel,
						       puap_state_chan_cb->
						       channel);
					}
					pcb->moal_ioctl_complete(pmpriv->
								 adapter->
								 pmoal_handle,
								 puap_state_chan_cb->
								 pioctl_req_curr,
								 MLAN_STATUS_FAILURE);
					goto done;
				}
			} else {
				if (under_nop) {
					PRINTM(MMSG,
					       "Channel %d under NOP, no switch channel available.\n",
					       old_channel);
				} else {
					PRINTM(MMSG,
					       "Radar found on channel %d, no switch channel available.\n",
					       old_channel);
				}
				/* No command sent with the ioctl, need manually
				 * signal completion */
				pcb->moal_ioctl_complete(pmpriv->adapter->
							 pmoal_handle,
							 puap_state_chan_cb->
							 pioctl_req_curr,
							 MLAN_STATUS_FAILURE);
				goto done;
			}
		} else {
			PRINTM(MINFO, "No Radar found on channel %d\n",
			       puap_state_chan_cb->channel);
		}
	}

prep_bss_start:
	/* else okay to send command:  not DFS channel or no radar */
	ret = wlan_prepare_cmd(pmpriv, HOST_CMD_APCMD_BSS_START,
			       HostCmd_ACT_GEN_SET, 0,
			       (t_void *)puap_state_chan_cb->pioctl_req_curr,
			       MNULL);
	if (ret == MLAN_STATUS_SUCCESS)
		ret = MLAN_STATUS_PENDING;

done:
	puap_state_chan_cb->pioctl_req_curr = MNULL;	/* prevent re-use */
	LEAVE();
	return ret;
}

/**
 *  @brief Start BSS
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_PENDING --success, otherwise fail
 */
/**
 *  @sa         wlan_uap_callback_bss_ioctl_start
 */
static mlan_status
wlan_uap_bss_ioctl_start(pmlan_adapter pmadapter, pmlan_ioctl_req pioctl_req)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	pmlan_private pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_ds_bss *bss = MNULL;

	ENTER();

	bss = (mlan_ds_bss *)pioctl_req->pbuf;
	pmpriv->uap_host_based = bss->param.host_based;
	if (!pmpriv->intf_state_11h.is_11h_host &&
	    pmpriv->intf_state_11h.is_11h_active) {
		/* if FW supports ACS+DFS then sequence is different */

		/* First check channel report, defer BSS_START CMD to callback.
		 */
		/* store params, issue command to get UAP channel, whose
		 * CMD_RESP will callback remainder of bss_start handling */
		pmpriv->uap_state_chan_cb.pioctl_req_curr = pioctl_req;
		pmpriv->uap_state_chan_cb.get_chan_callback =
			wlan_uap_callback_bss_ioctl_start;
		pmpriv->intf_state_11h.is_11h_host = MFALSE;
		ret = wlan_uap_get_channel(pmpriv);
		if (ret == MLAN_STATUS_SUCCESS)
			ret = MLAN_STATUS_PENDING;
	} else {
		ret = wlan_prepare_cmd(pmpriv, HOST_CMD_APCMD_BSS_START,
				       HostCmd_ACT_GEN_SET, 0,
				       (t_void *)pioctl_req, MNULL);
		if (ret == MLAN_STATUS_SUCCESS)
			ret = MLAN_STATUS_PENDING;
	}

	LEAVE();
	return ret;
}

/**
 *  @brief reset BSS
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_PENDING --success, otherwise fail
 */
static mlan_status
wlan_uap_bss_ioctl_reset(pmlan_adapter pmadapter, pmlan_ioctl_req pioctl_req)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	pmlan_private pmpriv = pmadapter->priv[pioctl_req->bss_index];
	t_u8 i = 0;

	ENTER();

	/*
	 * Reset any uap private parameters here
	 */
	for (i = 0; i < pmadapter->max_mgmt_ie_index; i++)
		memset(pmadapter, &pmpriv->mgmt_ie[i], 0, sizeof(custom_ie));
	pmpriv->add_ba_param.timeout = MLAN_DEFAULT_BLOCK_ACK_TIMEOUT;
	pmpriv->add_ba_param.tx_win_size = MLAN_UAP_AMPDU_DEF_TXWINSIZE;
	pmpriv->add_ba_param.rx_win_size = MLAN_UAP_AMPDU_DEF_RXWINSIZE;
	pmpriv->user_rxwinsize = pmpriv->add_ba_param.rx_win_size;

	for (i = 0; i < MAX_NUM_TID; i++) {
		pmpriv->aggr_prio_tbl[i].ampdu_user = tos_to_tid_inv[i];
		pmpriv->aggr_prio_tbl[i].amsdu = tos_to_tid_inv[i];
		pmpriv->addba_reject[i] = ADDBA_RSP_STATUS_ACCEPT;
	}
	pmpriv->aggr_prio_tbl[6].amsdu = BA_STREAM_NOT_ALLOWED;
	pmpriv->aggr_prio_tbl[7].amsdu = BA_STREAM_NOT_ALLOWED;

	pmpriv->aggr_prio_tbl[6].ampdu_user =
		pmpriv->aggr_prio_tbl[7].ampdu_user = BA_STREAM_NOT_ALLOWED;
	pmpriv->addba_reject[6] = pmpriv->addba_reject[7] =
		ADDBA_RSP_STATUS_REJECT;

	ret = wlan_prepare_cmd(pmpriv, HOST_CMD_APCMD_SYS_RESET,
			       HostCmd_ACT_GEN_SET, 0, (t_void *)pioctl_req,
			       MNULL);
	if (ret == MLAN_STATUS_SUCCESS)
		ret = MLAN_STATUS_PENDING;

	LEAVE();
	return ret;
}

/**
 *  @brief This function to process add station.
 *
 *  @param pmadapter       A pointer to pmadapter.
 *
 *  @param pioctl_req      A pointer to pioctl_req
 *
 *  @return            	  MLAN_STATUS_SUCCESS/MLAN_STATUS_FAILURE
 */
static mlan_status
wlan_uap_bss_ioctl_add_station(pmlan_adapter pmadapter,
			       pmlan_ioctl_req pioctl_req)
{
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_status ret = MLAN_STATUS_SUCCESS;

	ENTER();
	ret = wlan_prepare_cmd(pmpriv, HostCmd_CMD_ADD_NEW_STATION,
			       HostCmd_ACT_ADD_STA, 0, (t_void *)pioctl_req,
			       MNULL);

	if (ret == MLAN_STATUS_SUCCESS)
		ret = MLAN_STATUS_PENDING;

	LEAVE();
	return ret;
}

/**
 *  @brief Set/Get MAC address
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_PENDING --success, otherwise fail
 */
static mlan_status
wlan_uap_bss_ioctl_mac_address(pmlan_adapter pmadapter,
			       pmlan_ioctl_req pioctl_req)
{
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_ds_bss *bss = MNULL;
	mlan_status ret = MLAN_STATUS_SUCCESS;
	t_u16 cmd_action = 0;

	ENTER();

	bss = (mlan_ds_bss *)pioctl_req->pbuf;
	if (pioctl_req->action == MLAN_ACT_SET) {
		memcpy_ext(pmadapter, pmpriv->curr_addr, &bss->param.mac_addr,
			   MLAN_MAC_ADDR_LENGTH, MLAN_MAC_ADDR_LENGTH);
		cmd_action = HostCmd_ACT_GEN_SET;
	} else
		cmd_action = HostCmd_ACT_GEN_GET;
	/* Send request to firmware */
	ret = wlan_prepare_cmd(pmpriv, HOST_CMD_APCMD_SYS_CONFIGURE, cmd_action,
			       0, (t_void *)pioctl_req, MNULL);

	if (ret == MLAN_STATUS_SUCCESS)
		ret = MLAN_STATUS_PENDING;

	LEAVE();
	return ret;
}

/**
 *  @brief Set/Get wmm param
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_PENDING --success, otherwise fail
 */
static mlan_status
wlan_uap_bss_ioctl_uap_wmm_param(pmlan_adapter pmadapter,
				 pmlan_ioctl_req pioctl_req)
{
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_status ret = MLAN_STATUS_SUCCESS;
	t_u16 cmd_action = 0;

	ENTER();

	if (pioctl_req->action == MLAN_ACT_SET)
		cmd_action = HostCmd_ACT_GEN_SET;
	else
		cmd_action = HostCmd_ACT_GEN_GET;
	/* Send request to firmware */
	ret = wlan_prepare_cmd(pmpriv, HOST_CMD_APCMD_SYS_CONFIGURE, cmd_action,
			       0, (t_void *)pioctl_req, MNULL);

	if (ret == MLAN_STATUS_SUCCESS)
		ret = MLAN_STATUS_PENDING;

	LEAVE();
	return ret;
}

/**
 *  @brief Handle channel switch
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_PENDING --success, otherwise fail
 */
static mlan_status
wlan_uap_bss_ioctl_action_chan_switch(pmlan_adapter pmadapter,
				      pmlan_ioctl_req pioctl_req)
{
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_status ret = MLAN_STATUS_SUCCESS;
	t_u16 cmd_action = 0;

	ENTER();

	cmd_action = HostCmd_ACT_GEN_SET;

	/* Send request to firmware */
	ret = wlan_prepare_cmd(pmpriv, HOST_CMD_APCMD_SYS_CONFIGURE, cmd_action,
			       0, (t_void *)pioctl_req, MNULL);

	if (ret == MLAN_STATUS_SUCCESS)
		ret = MLAN_STATUS_PENDING;

	LEAVE();
	return ret;
}

/**
 *  @brief Set/Get scan channels
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_PENDING --success, otherwise fail
 */
static mlan_status
wlan_uap_bss_ioctl_uap_scan_channels(pmlan_adapter pmadapter,
				     pmlan_ioctl_req pioctl_req)
{
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_status ret = MLAN_STATUS_SUCCESS;
	t_u16 cmd_action = 0;

	ENTER();

	if (pioctl_req->action == MLAN_ACT_SET)
		cmd_action = HostCmd_ACT_GEN_SET;
	else
		cmd_action = HostCmd_ACT_GEN_GET;
	/* Send request to firmware */
	ret = wlan_prepare_cmd(pmpriv, HOST_CMD_APCMD_SYS_CONFIGURE, cmd_action,
			       0, (t_void *)pioctl_req, MNULL);

	if (ret == MLAN_STATUS_SUCCESS)
		ret = MLAN_STATUS_PENDING;

	LEAVE();
	return ret;
}

/**
 *  @brief Set/Get UAP channel
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_PENDING --success, otherwise fail
 */
static mlan_status
wlan_uap_bss_ioctl_uap_channel(pmlan_adapter pmadapter,
			       pmlan_ioctl_req pioctl_req)
{
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_status ret = MLAN_STATUS_SUCCESS;
	t_u16 cmd_action = 0;

	ENTER();

	if (pioctl_req->action == MLAN_ACT_SET)
		cmd_action = HostCmd_ACT_GEN_SET;
	else
		cmd_action = HostCmd_ACT_GEN_GET;
	/* Send request to firmware */
	ret = wlan_prepare_cmd(pmpriv, HOST_CMD_APCMD_SYS_CONFIGURE, cmd_action,
			       0, (t_void *)pioctl_req, MNULL);

	if (ret == MLAN_STATUS_SUCCESS)
		ret = MLAN_STATUS_PENDING;

	LEAVE();
	return ret;
}

/**
 *  @brief Set/Get UAP operation control vaule
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_PENDING --success, otherwise fail
 */
static mlan_status
wlan_uap_bss_ioctl_uap_oper_ctrl(pmlan_adapter pmadapter,
				 pmlan_ioctl_req pioctl_req)
{
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_status ret = MLAN_STATUS_SUCCESS;
	t_u16 cmd_action = 0;

	ENTER();

	if (pmadapter->fw_ver == HOST_API_VERSION_V15 &&
	    pmadapter->fw_min_ver >= FW_MINOR_VERSION_1) {
		if (pioctl_req->action == MLAN_ACT_SET)
			cmd_action = HostCmd_ACT_GEN_SET;
		else
			cmd_action = HostCmd_ACT_GEN_GET;
		/* Send request to firmware */
		ret = wlan_prepare_cmd(pmpriv, HOST_CMD_APCMD_OPER_CTRL,
				       cmd_action, 0, (t_void *)pioctl_req,
				       (t_void *)pioctl_req->pbuf);

		if (ret == MLAN_STATUS_SUCCESS)
			ret = MLAN_STATUS_PENDING;
	} else {
		PRINTM(MMSG, "FW don't support uap oper ctrl\n");
	}
	LEAVE();
	return ret;
}

/**
 *  @brief Get Uap statistics
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_PENDING --success, otherwise fail
 */
static mlan_status
wlan_uap_get_stats(pmlan_adapter pmadapter, pmlan_ioctl_req pioctl_req)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	pmlan_private pmpriv = pmadapter->priv[pioctl_req->bss_index];

	ENTER();

	ret = wlan_prepare_cmd(pmpriv, HostCmd_CMD_802_11_SNMP_MIB,
			       HostCmd_ACT_GEN_GET, 0, (t_void *)pioctl_req,
			       MNULL);
	if (ret == MLAN_STATUS_SUCCESS)
		ret = MLAN_STATUS_PENDING;

	LEAVE();
	return ret;
}

/**
 *  @brief Get Uap MIB counters
 *
 *  @param pmadapter    A pointer to mlan_adapter structure
 *  @param pioctl_req   A pointer to ioctl request buffer
 *
 *  @return             MLAN_STATUS_PENDING --success, otherwise fail
 */
static mlan_status
wlan_uap_get_stats_log(pmlan_adapter pmadapter, pmlan_ioctl_req pioctl_req)
{
	pmlan_private pmpriv = MNULL;
	mlan_status ret = MLAN_STATUS_SUCCESS;

	ENTER();

	if (pioctl_req != MNULL) {
		pmpriv = pmadapter->priv[pioctl_req->bss_index];
	} else {
		PRINTM(MERROR, "MLAN IOCTL information is not present\n");
		ret = MLAN_STATUS_FAILURE;
		goto exit;
	}

	/* Check information buffer length of MLAN IOCTL */
	if (pioctl_req->buf_len < sizeof(mlan_ds_get_stats)) {
		PRINTM(MWARN,
		       "MLAN IOCTL information buffer length is too short.\n");
		pioctl_req->data_read_written = 0;
		pioctl_req->buf_len_needed = sizeof(mlan_ds_get_stats);
		pioctl_req->status_code = MLAN_ERROR_INVALID_PARAMETER;
		ret = MLAN_STATUS_RESOURCE;
		goto exit;
	}

	/* Send request to firmware */
	ret = wlan_prepare_cmd(pmpriv, HostCmd_CMD_802_11_GET_LOG,
			       HostCmd_ACT_GEN_GET, 0, (t_void *)pioctl_req,
			       MNULL);

	if (ret == MLAN_STATUS_SUCCESS)
		ret = MLAN_STATUS_PENDING;

exit:
	LEAVE();
	return ret;
}

/**
 *  @brief Set/Get AP config
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_PENDING --success, otherwise fail
 */
static mlan_status
wlan_uap_bss_ioctl_config(pmlan_adapter pmadapter, pmlan_ioctl_req pioctl_req)
{
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_status ret = MLAN_STATUS_SUCCESS;
	t_u16 cmd_action = 0;

	ENTER();

	if (pioctl_req->action == MLAN_ACT_SET)
		cmd_action = HostCmd_ACT_GEN_SET;
	else
		cmd_action = HostCmd_ACT_GEN_GET;

	/* Send request to firmware */
	ret = wlan_prepare_cmd(pmpriv, HOST_CMD_APCMD_SYS_CONFIGURE, cmd_action,
			       0, (t_void *)pioctl_req, MNULL);

	if (ret == MLAN_STATUS_SUCCESS)
		ret = MLAN_STATUS_PENDING;

	LEAVE();
	return ret;
}

/**
 *  @brief deauth sta
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_PENDING --success, otherwise fail
 */
static mlan_status
wlan_uap_bss_ioctl_deauth_sta(pmlan_adapter pmadapter,
			      pmlan_ioctl_req pioctl_req)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	pmlan_private pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_ds_bss *bss = MNULL;

	ENTER();

	bss = (mlan_ds_bss *)pioctl_req->pbuf;
	ret = wlan_prepare_cmd(pmpriv, HOST_CMD_APCMD_STA_DEAUTH,
			       HostCmd_ACT_GEN_SET, 0, (t_void *)pioctl_req,
			       (t_void *)&bss->param.deauth_param);
	if (ret == MLAN_STATUS_SUCCESS)
		ret = MLAN_STATUS_PENDING;

	LEAVE();
	return ret;
}

/**
 *  @brief Get station list
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_PENDING --success, otherwise fail
 */
static mlan_status
wlan_uap_get_sta_list(pmlan_adapter pmadapter, pmlan_ioctl_req pioctl_req)
{
	pmlan_private pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_status ret = MLAN_STATUS_SUCCESS;

	ENTER();

	/* Send request to firmware */
	ret = wlan_prepare_cmd(pmpriv, HOST_CMD_APCMD_STA_LIST,
			       HostCmd_ACT_GEN_GET, 0, (t_void *)pioctl_req,
			       MNULL);

	if (ret == MLAN_STATUS_SUCCESS)
		ret = MLAN_STATUS_PENDING;

	LEAVE();
	return ret;
}

/**
 *  @brief soft_reset
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_PENDING --success, otherwise fail
 */
static mlan_status
wlan_uap_misc_ioctl_soft_reset(pmlan_adapter pmadapter,
			       pmlan_ioctl_req pioctl_req)
{
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_status ret = MLAN_STATUS_SUCCESS;

	ENTER();

	ret = wlan_prepare_cmd(pmpriv, HostCmd_CMD_SOFT_RESET,
			       HostCmd_ACT_GEN_SET, 0, (t_void *)pioctl_req,
			       MNULL);
	if (ret == MLAN_STATUS_SUCCESS)
		ret = MLAN_STATUS_PENDING;

	LEAVE();
	return ret;
}

/**
 *  @brief Tx data pause
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_PENDING --success, otherwise fail
 */
static mlan_status
wlan_uap_misc_ioctl_txdatapause(pmlan_adapter pmadapter,
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
	ret = wlan_prepare_cmd(pmpriv, HostCmd_CMD_CFG_TX_DATA_PAUSE,
			       cmd_action, 0, (t_void *)pioctl_req,
			       &(pmisc->param.tx_datapause));
	if (ret == MLAN_STATUS_SUCCESS)
		ret = MLAN_STATUS_PENDING;

	LEAVE();
	return ret;
}

/**
 *  @brief Set/Get Power mode
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_PENDING --success, otherwise fail
 */
static mlan_status
wlan_uap_pm_ioctl_mode(pmlan_adapter pmadapter, pmlan_ioctl_req pioctl_req)
{
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_ds_pm_cfg *pm = MNULL;
	mlan_status ret = MLAN_STATUS_SUCCESS;
	t_u16 cmd_action = 0;
	t_u32 cmd_oid = 0;

	ENTER();

	pm = (mlan_ds_pm_cfg *)pioctl_req->pbuf;
	if (pioctl_req->action == MLAN_ACT_SET) {
		if (pm->param.ps_mgmt.ps_mode == PS_MODE_INACTIVITY) {
			cmd_action = EN_AUTO_PS;
			cmd_oid = BITMAP_UAP_INACT_PS;
		} else if (pm->param.ps_mgmt.ps_mode == PS_MODE_PERIODIC_DTIM) {
			cmd_action = EN_AUTO_PS;
			cmd_oid = BITMAP_UAP_DTIM_PS;
		} else {
			cmd_action = DIS_AUTO_PS;
			cmd_oid = BITMAP_UAP_INACT_PS | BITMAP_UAP_DTIM_PS;
		}
	} else {
		cmd_action = GET_PS;
		cmd_oid = BITMAP_UAP_INACT_PS | BITMAP_UAP_DTIM_PS;
	}
	/* Send request to firmware */
	ret = wlan_prepare_cmd(pmpriv, HostCmd_CMD_802_11_PS_MODE_ENH,
			       cmd_action, cmd_oid, (t_void *)pioctl_req,
			       (t_void *)&pm->param.ps_mgmt);
	if ((ret == MLAN_STATUS_SUCCESS) &&
	    (pioctl_req->action == MLAN_ACT_SET) &&
	    (cmd_action == DIS_AUTO_PS)) {
		ret = wlan_prepare_cmd(pmpriv, HostCmd_CMD_802_11_PS_MODE_ENH,
				       GET_PS, 0, MNULL, MNULL);
	}
	if (ret == MLAN_STATUS_SUCCESS)
		ret = MLAN_STATUS_PENDING;

	LEAVE();
	return ret;
}

/**
 *  @brief Set WAPI IE
 *
 *  @param priv         A pointer to mlan_private structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return             MLAN_STATUS_PENDING --success, otherwise fail
 */
static mlan_status
wlan_uap_set_wapi_ie(mlan_private *priv, pmlan_ioctl_req pioctl_req)
{
	mlan_ds_misc_cfg *misc = MNULL;
	mlan_status ret = MLAN_STATUS_SUCCESS;

	ENTER();
	misc = (mlan_ds_misc_cfg *)pioctl_req->pbuf;
	if (misc->param.gen_ie.len) {
		if (misc->param.gen_ie.len > sizeof(priv->wapi_ie)) {
			PRINTM(MWARN, "failed to copy WAPI IE, too big\n");
			pioctl_req->status_code = MLAN_ERROR_INVALID_PARAMETER;
			LEAVE();
			return MLAN_STATUS_FAILURE;
		}
		memcpy_ext(priv->adapter, priv->wapi_ie,
			   misc->param.gen_ie.ie_data, misc->param.gen_ie.len,
			   sizeof(priv->wapi_ie));
		priv->wapi_ie_len = misc->param.gen_ie.len;
		PRINTM(MIOCTL, "Set wapi_ie_len=%d IE=%#x\n", priv->wapi_ie_len,
		       priv->wapi_ie[0]);
		DBG_HEXDUMP(MCMD_D, "wapi_ie", priv->wapi_ie,
			    priv->wapi_ie_len);
		if (priv->wapi_ie[0] == WAPI_IE)
			priv->sec_info.wapi_enabled = MTRUE;
	} else {
		memset(priv->adapter, priv->wapi_ie, 0, sizeof(priv->wapi_ie));
		priv->wapi_ie_len = misc->param.gen_ie.len;
		PRINTM(MINFO, "Reset wapi_ie_len=%d IE=%#x\n",
		       priv->wapi_ie_len, priv->wapi_ie[0]);
		priv->sec_info.wapi_enabled = MFALSE;
	}

	/* Send request to firmware */
	ret = wlan_prepare_cmd(priv, HOST_CMD_APCMD_SYS_CONFIGURE,
			       HostCmd_ACT_GEN_SET, 0, (t_void *)pioctl_req,
			       MNULL);

	if (ret == MLAN_STATUS_SUCCESS)
		ret = MLAN_STATUS_PENDING;

	LEAVE();
	return ret;
}

/**
 *  @brief Set generic IE
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_SUCCESS/MLAN_STATUS_PENDING --success,
 * otherwise fail
 */
static mlan_status
wlan_uap_misc_ioctl_gen_ie(pmlan_adapter pmadapter, pmlan_ioctl_req pioctl_req)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_ds_misc_cfg *misc = MNULL;
	IEEEtypes_VendorHeader_t *pvendor_ie = MNULL;

	ENTER();

	misc = (mlan_ds_misc_cfg *)pioctl_req->pbuf;

	if ((misc->param.gen_ie.type == MLAN_IE_TYPE_GEN_IE) &&
	    (pioctl_req->action == MLAN_ACT_SET)) {
		if (misc->param.gen_ie.len) {
			pvendor_ie = (IEEEtypes_VendorHeader_t *)
				misc->param.gen_ie.ie_data;
			if (pvendor_ie->element_id == WAPI_IE) {
				/* IE is a WAPI IE so call set_wapi function */
				ret = wlan_uap_set_wapi_ie(pmpriv, pioctl_req);
			}
		} else {
			/* clear WAPI IE */
			ret = wlan_uap_set_wapi_ie(pmpriv, pioctl_req);
		}
	}
	LEAVE();
	return ret;
}

/**
 *  @brief Set/Get WAPI status
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_SUCCESS --success
 */
static mlan_status
wlan_uap_sec_ioctl_wapi_enable(pmlan_adapter pmadapter,
			       pmlan_ioctl_req pioctl_req)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_ds_sec_cfg *sec = MNULL;
	ENTER();
	sec = (mlan_ds_sec_cfg *)pioctl_req->pbuf;
	if (pioctl_req->action == MLAN_ACT_GET) {
		if (pmpriv->wapi_ie_len)
			sec->param.wapi_enabled = MTRUE;
		else
			sec->param.wapi_enabled = MFALSE;
	} else {
		if (sec->param.wapi_enabled == MFALSE) {
			memset(pmpriv->adapter, pmpriv->wapi_ie, 0,
			       sizeof(pmpriv->wapi_ie));
			pmpriv->wapi_ie_len = 0;
			PRINTM(MINFO, "Reset wapi_ie_len=%d IE=%#x\n",
			       pmpriv->wapi_ie_len, pmpriv->wapi_ie[0]);
			pmpriv->sec_info.wapi_enabled = MFALSE;
		}
	}
	pioctl_req->data_read_written = sizeof(t_u32) + MLAN_SUB_COMMAND_SIZE;
	LEAVE();
	return ret;
}

/**
 *  @brief report mic error
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_PENDING --success, otherwise fail
 */
static mlan_status
wlan_uap_sec_ioctl_report_mic_error(pmlan_adapter pmadapter,
				    pmlan_ioctl_req pioctl_req)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	pmlan_private pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_ds_sec_cfg *sec = MNULL;

	ENTER();

	sec = (mlan_ds_sec_cfg *)pioctl_req->pbuf;
	ret = wlan_prepare_cmd(pmpriv, HOST_CMD_APCMD_REPORT_MIC,
			       HostCmd_ACT_GEN_SET, 0, (t_void *)pioctl_req,
			       (t_void *)sec->param.sta_mac);
	if (ret == MLAN_STATUS_SUCCESS)
		ret = MLAN_STATUS_PENDING;

	LEAVE();
	return ret;
}

/**
 *  @brief Set encrypt key
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_SUCCESS/MLAN_STATUS_PENDING --success,
 * otherwise fail
 */
static mlan_status
wlan_uap_sec_ioctl_set_encrypt_key(pmlan_adapter pmadapter,
				   pmlan_ioctl_req pioctl_req)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_ds_sec_cfg *sec = MNULL;

	ENTER();
	sec = (mlan_ds_sec_cfg *)pioctl_req->pbuf;
	if (pioctl_req->action != MLAN_ACT_SET) {
		pioctl_req->status_code = MLAN_ERROR_IOCTL_INVALID;
		LEAVE();
		return MLAN_STATUS_FAILURE;
	}
	if (!sec->param.encrypt_key.key_remove &&
	    !sec->param.encrypt_key.key_len) {
		PRINTM(MCMND, "Skip set key with key_len = 0\n");
		LEAVE();
		return ret;
	}

	ret = wlan_prepare_cmd(pmpriv, HostCmd_CMD_802_11_KEY_MATERIAL,
			       HostCmd_ACT_GEN_SET, KEY_INFO_ENABLED,
			       (t_void *)pioctl_req, &sec->param.encrypt_key);

	if (ret == MLAN_STATUS_SUCCESS)
		ret = MLAN_STATUS_PENDING;
	LEAVE();
	return ret;
}

/**
 *  @brief Get BSS information
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_SUCCESS --success
 */
static mlan_status
wlan_uap_get_bss_info(pmlan_adapter pmadapter, pmlan_ioctl_req pioctl_req)
{
	pmlan_private pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_status ret = MLAN_STATUS_SUCCESS;
	mlan_ds_get_info *info;

	ENTER();

	info = (mlan_ds_get_info *)pioctl_req->pbuf;
	/* Connection status */
	info->param.bss_info.media_connected = pmpriv->media_connected;

	/* Radio status */
	info->param.bss_info.radio_on = pmadapter->radio_on;

	/* BSSID */
	memcpy_ext(pmadapter, &info->param.bss_info.bssid, pmpriv->curr_addr,
		   MLAN_MAC_ADDR_LENGTH, MLAN_MAC_ADDR_LENGTH);
	info->param.bss_info.scan_block = pmadapter->scan_block;

	info->param.bss_info.is_hs_configured = pmadapter->is_hs_configured;
	info->param.bss_info.is_11h_active =
		pmpriv->intf_state_11h.is_11h_active;
	info->param.bss_info.dfs_check_channel =
		pmpriv->adapter->state_dfs.dfs_check_channel;
	pioctl_req->data_read_written =
		sizeof(mlan_bss_info) + MLAN_SUB_COMMAND_SIZE;

	LEAVE();
	return ret;
}

/**
 *  @brief Set Host Sleep configurations
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_SUCCES/MLAN_STATUS_PENDING --success, otherwise
 * fail
 */
static mlan_status
wlan_uap_pm_ioctl_deepsleep(pmlan_adapter pmadapter, pmlan_ioctl_req pioctl_req)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_ds_pm_cfg *pm = MNULL;
	mlan_ds_auto_ds auto_ds;
	t_u32 mode;

	ENTER();

	if (pioctl_req->buf_len < sizeof(mlan_ds_pm_cfg)) {
		PRINTM(MWARN, "MLAN bss IOCTL length is too short.\n");
		pioctl_req->data_read_written = 0;
		pioctl_req->buf_len_needed = sizeof(mlan_ds_pm_cfg);
		pioctl_req->status_code = MLAN_ERROR_INVALID_PARAMETER;
		LEAVE();
		return MLAN_STATUS_RESOURCE;
	}
	pm = (mlan_ds_pm_cfg *)pioctl_req->pbuf;

	if (pioctl_req->action == MLAN_ACT_GET) {
		if (pmadapter->is_deep_sleep) {
			pm->param.auto_deep_sleep.auto_ds = DEEP_SLEEP_ON;
			pm->param.auto_deep_sleep.idletime =
				pmadapter->idle_time;
		} else
			pm->param.auto_deep_sleep.auto_ds = DEEP_SLEEP_OFF;
	} else {
		if (pmadapter->is_deep_sleep &&
		    pm->param.auto_deep_sleep.auto_ds == DEEP_SLEEP_ON) {
			PRINTM(MMSG, "uAP already in deep sleep mode\n");
			LEAVE();
			return MLAN_STATUS_FAILURE;
		}
		if (((mlan_ds_pm_cfg *)pioctl_req->pbuf)
		    ->param.auto_deep_sleep.auto_ds == DEEP_SLEEP_ON) {
			auto_ds.auto_ds = DEEP_SLEEP_ON;
			mode = EN_AUTO_PS;
			PRINTM(MINFO, "Auto Deep Sleep: on\n");
		} else {
			mode = DIS_AUTO_PS;
			auto_ds.auto_ds = DEEP_SLEEP_OFF;
			PRINTM(MINFO, "Auto Deep Sleep: off\n");
		}
		if (((mlan_ds_pm_cfg *)pioctl_req->pbuf)
		    ->param.auto_deep_sleep.idletime)
			auto_ds.idletime = ((mlan_ds_pm_cfg *)pioctl_req->pbuf)
				->param.auto_deep_sleep.idletime;
		else
			auto_ds.idletime = pmadapter->idle_time;
		ret = wlan_prepare_cmd(pmpriv, HostCmd_CMD_802_11_PS_MODE_ENH,
				       (t_u16)mode, BITMAP_AUTO_DS,
				       (t_void *)pioctl_req, &auto_ds);
		if (ret == MLAN_STATUS_SUCCESS)
			ret = MLAN_STATUS_PENDING;
	}
	LEAVE();
	return ret;
}

/**
 *  @brief Set Band Steering configurations
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_SUCCES/MLAN_STATUS_PENDING --success, otherwise
 * fail
 */
static mlan_status
wlan_misc_band_steering_cfg(pmlan_adapter pmadapter, pmlan_ioctl_req pioctl_req)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_ds_misc_cfg *pm = MNULL;

	ENTER();

	if (pioctl_req->buf_len < sizeof(mlan_ds_band_steer_cfg)) {
		PRINTM(MWARN, "MLAN bss IOCTL length is too short.\n");
		pioctl_req->data_read_written = 0;
		pioctl_req->buf_len_needed = sizeof(mlan_ds_band_steer_cfg);
		pioctl_req->status_code = MLAN_ERROR_INVALID_PARAMETER;
		LEAVE();
		return MLAN_STATUS_RESOURCE;
	}
	pm = (mlan_ds_misc_cfg *)pioctl_req->pbuf;

	ret = wlan_prepare_cmd(pmpriv, HostCmd_CMD_802_11_BAND_STEERING,
			       (t_u16)pm->param.band_steer_cfg.action, 0,
			       (t_void *)pioctl_req,
			       (t_void *)&pm->param.band_steer_cfg);
	LEAVE();
	if (ret == MLAN_STATUS_SUCCESS)
		ret = MLAN_STATUS_PENDING;
	return ret;
}

/**
 *  @brief Set Beacon Stuck Detect Mechanism Configurations
 *
 *  @param pmadapter   A pointer to mlan_adapter structure
 *  @param pioctl_req  A pointer to ioctl request buffer
 *
 *  @return            MLAN_STATUS_SUCCES/MLAN_STATUS_PENDING --success, otherwise fail
 */
static mlan_status
wlan_misc_beacon_stuck_cfg(IN pmlan_adapter pmadapter,
			   IN pmlan_ioctl_req pioctl_req)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_ds_misc_cfg *pm = MNULL;

	ENTER();

	if (pioctl_req->buf_len < sizeof(mlan_ds_beacon_stuck_param_cfg)) {
		PRINTM(MWARN, "MLAN bss IOCTL length is too short.\n");
		pioctl_req->data_read_written = 0;
		pioctl_req->buf_len_needed =
			sizeof(mlan_ds_beacon_stuck_param_cfg);
		pioctl_req->status_code = MLAN_ERROR_INVALID_PARAMETER;
		LEAVE();
		return MLAN_STATUS_RESOURCE;
	}
	pm = (mlan_ds_misc_cfg *)pioctl_req->pbuf;

	ret = wlan_prepare_cmd(pmpriv, HostCmd_CMD_UAP_BEACON_STUCK_CFG,
			       (t_u16)pm->param.beacon_stuck_cfg.action, 0,
			       (t_void *)pioctl_req,
			       (t_void *)&pm->param.beacon_stuck_cfg);

	LEAVE();

	if (ret == MLAN_STATUS_SUCCESS)
		ret = MLAN_STATUS_PENDING;

	return ret;
}

/**
 *  @brief Set SNMP MIB for 11D
 *
 *  @param pmadapter    A pointer to mlan_adapter structure
 *  @param pioctl_req   A pointer to ioctl request buffer
 *
 *  @return     MLAN_STATUS_PENDING --success, otherwise fail
 */
static mlan_status
wlan_uap_snmp_mib_11d(pmlan_adapter pmadapter, pmlan_ioctl_req pioctl_req)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_ds_snmp_mib *snmp = MNULL;
	state_11d_t flag;

	ENTER();

	if (pioctl_req->buf_len < sizeof(mlan_ds_snmp_mib)) {
		PRINTM(MWARN, "MLAN snmp_mib IOCTL length is too short.\n");
		pioctl_req->data_read_written = 0;
		pioctl_req->buf_len_needed = sizeof(mlan_ds_snmp_mib);
		LEAVE();
		return MLAN_STATUS_RESOURCE;
	}

	if ((pioctl_req->action == MLAN_ACT_SET) && pmpriv->uap_bss_started) {
		PRINTM(MIOCTL,
		       "11D setting cannot be changed while UAP bss is started.\n");
		pioctl_req->data_read_written = 0;
		LEAVE();
		return MLAN_STATUS_FAILURE;
	}

	snmp = (mlan_ds_snmp_mib *)pioctl_req->pbuf;
	flag = (snmp->param.oid_value) ? ENABLE_11D : DISABLE_11D;

	ret = wlan_11d_enable(pmpriv, (t_void *)pioctl_req, flag);
	if (ret == MLAN_STATUS_SUCCESS)
		ret = MLAN_STATUS_PENDING;

	LEAVE();
	return ret;
}

/**
 *  @brief Callback to finish domain_info handling
 *  Not to be called directly to initiate domain_info setting.
 *
 *  @param pmpriv   A pointer to mlan_private structure (cast from t_void*)
 *
 *  @return     MLAN_STATUS_PENDING --success, otherwise fail
 *  @sa         wlan_uap_domain_info
 */
static mlan_status
wlan_uap_callback_domain_info(t_void *priv)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	mlan_private *pmpriv = (mlan_private *)priv;
	wlan_uap_get_info_cb_t *puap_state_chan_cb = &pmpriv->uap_state_chan_cb;
	mlan_ds_11d_cfg *cfg11d;
	t_u8 band;
	pmlan_adapter pmadapter = pmpriv->adapter;
	pmlan_callbacks pcb = &pmadapter->callbacks;

	ENTER();
	/* clear callback now that we're here */
	puap_state_chan_cb->get_chan_callback = MNULL;

	if (!puap_state_chan_cb->pioctl_req_curr) {
		PRINTM(MERROR, "pioctl_req_curr is null\n");
		LEAVE();
		return ret;
	}
	cfg11d = (mlan_ds_11d_cfg *)puap_state_chan_cb->pioctl_req_curr->pbuf;
	band = (puap_state_chan_cb->bandcfg.chanBand == BAND_5GHZ) ? BAND_A :
		BAND_B;

	ret = wlan_11d_handle_uap_domain_info(pmpriv, band,
					      cfg11d->param.domain_tlv,
					      puap_state_chan_cb->
					      pioctl_req_curr);
	if (ret == MLAN_STATUS_SUCCESS)
		ret = MLAN_STATUS_PENDING;
	else {
		puap_state_chan_cb->pioctl_req_curr->status_code =
			MLAN_STATUS_FAILURE;
		pcb->moal_ioctl_complete(pmadapter->pmoal_handle,
					 puap_state_chan_cb->pioctl_req_curr,
					 MLAN_STATUS_FAILURE);
	}

	puap_state_chan_cb->pioctl_req_curr = MNULL;	/* prevent re-use */
	LEAVE();
	return ret;
}

/**
 *  @brief Set Domain Info for 11D
 *
 *  @param pmadapter    A pointer to mlan_adapter structure
 *  @param pioctl_req   A pointer to ioctl request buffer
 *
 *  @return     MLAN_STATUS_PENDING --success, otherwise fail
 *  @sa         wlan_uap_callback_domain_info
 */
static mlan_status
wlan_uap_domain_info(pmlan_adapter pmadapter, pmlan_ioctl_req pioctl_req)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];

	ENTER();

	if (pioctl_req->buf_len < sizeof(mlan_ds_11d_cfg)) {
		PRINTM(MWARN, "MLAN 11d_cfg IOCTL length is too short.\n");
		pioctl_req->data_read_written = 0;
		pioctl_req->buf_len_needed = sizeof(mlan_ds_11d_cfg);
		LEAVE();
		return MLAN_STATUS_RESOURCE;
	}

	if ((pioctl_req->action == MLAN_ACT_SET) && pmpriv->uap_bss_started) {
		PRINTM(MIOCTL,
		       "Domain_info cannot be changed while UAP bss is started.\n");
		pioctl_req->data_read_written = 0;
		LEAVE();
		return MLAN_STATUS_FAILURE;
	}

	/* store params, issue command to get UAP channel, whose CMD_RESP will
	 * callback remainder of domain_info handling */
	pmpriv->uap_state_chan_cb.pioctl_req_curr = pioctl_req;
	pmpriv->uap_state_chan_cb.get_chan_callback =
		wlan_uap_callback_domain_info;

	ret = wlan_uap_get_channel(pmpriv);
	if (ret == MLAN_STATUS_SUCCESS)
		ret = MLAN_STATUS_PENDING;

	LEAVE();
	return ret;
}

/**
 *  @brief Callback to finish 11H channel check handling.
 *  Not to be called directly to initiate channel check.
 *
 *  @param priv A pointer to mlan_private structure (cast from t_void*)
 *
 *  @return     MLAN_STATUS_SUCCESS/PENDING --success, otherwise fail
 *  @sa         wlan_uap_11h_channel_check_req
 */
static mlan_status
wlan_uap_callback_11h_channel_check_req(t_void *priv)
{
	mlan_status ret = MLAN_STATUS_FAILURE;
	mlan_private *pmpriv = (mlan_private *)priv;
	mlan_callbacks *pcb = (mlan_callbacks *)&pmpriv->adapter->callbacks;
	wlan_uap_get_info_cb_t *puap_state_chan_cb = &pmpriv->uap_state_chan_cb;
	Band_Config_t *pband_cfg = &puap_state_chan_cb->bandcfg;
	/* keep copy as local variable */
	pmlan_ioctl_req pioctl = puap_state_chan_cb->pioctl_req_curr;
	ENTER();
	/* clear callback now that we're here */
	puap_state_chan_cb->get_chan_callback = MNULL;
	/* clear early to avoid race condition */
	puap_state_chan_cb->pioctl_req_curr = MNULL;

	/*
	 * Check if the region and channel requires a channel availability
	 * check.
	 */
	if ((puap_state_chan_cb->bandcfg.chanBand == BAND_5GHZ) &&
	    !wlan_can_radar_det_skip(pmpriv) &&
	    wlan_11h_radar_detect_required(pmpriv,
					   puap_state_chan_cb->channel) &&
	    !wlan_11h_is_channel_under_nop(pmpriv->adapter,
					   puap_state_chan_cb->channel)) {
		/*
		 * Radar detection is required for this channel, make sure
		 * 11h is activated in the firmware
		 */
		ret = wlan_11h_activate(pmpriv, MNULL, MTRUE);
		ret = wlan_11h_config_master_radar_det(pmpriv, MTRUE);
		ret = wlan_11h_check_update_radar_det_state(pmpriv);

		/* Check for radar on the channel */
		ret = wlan_11h_issue_radar_detect(pmpriv, pioctl,
						  puap_state_chan_cb->channel,
						  *pband_cfg);
		if (ret == MLAN_STATUS_SUCCESS)
			ret = MLAN_STATUS_PENDING;
	} else {
		/* No command sent with the ioctl, need manually signal
		 * completion */
		pcb->moal_ioctl_complete(pmpriv->adapter->pmoal_handle, pioctl,
					 MLAN_STATUS_COMPLETE);
	}

	LEAVE();
	return ret;
}

/**
 *  @brief 802.11h uap start channel check
 *
 *  @param pmadapter    A pointer to mlan_adapter structure
 *  @param pioctl_req   A pointer to ioctl request buffer
 *
 *  @return     MLAN_STATUS_PENDING --success, otherwise fail
 *  @sa         wlan_uap_callback_11h_channel_check_req
 */
static mlan_status
wlan_uap_11h_channel_check_req(pmlan_adapter pmadapter,
			       pmlan_ioctl_req pioctl_req)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_ds_11h_cfg *p11h_cfg;

	ENTER();

	if (pioctl_req->buf_len < sizeof(mlan_ds_11h_cfg)) {
		PRINTM(MWARN, "MLAN 11h_cfg IOCTL length is too short.\n");
		pioctl_req->data_read_written = 0;
		pioctl_req->buf_len_needed = sizeof(mlan_ds_11h_cfg);
		LEAVE();
		return MLAN_STATUS_RESOURCE;
	}
	p11h_cfg = (mlan_ds_11h_cfg *)pioctl_req->pbuf;
	pmpriv->intf_state_11h.is_11h_host =
		p11h_cfg->param.chan_rpt_req.host_based;

	if (!pmpriv->intf_state_11h.is_11h_host) {
		/* store params, issue command to get UAP channel, whose
		 * CMD_RESP will callback remainder of 11H channel check
		 * handling */
		pmpriv->uap_state_chan_cb.pioctl_req_curr = pioctl_req;
		pmpriv->uap_state_chan_cb.get_chan_callback =
			wlan_uap_callback_11h_channel_check_req;

		ret = wlan_uap_get_channel(pmpriv);
		if (ret == MLAN_STATUS_SUCCESS)
			ret = MLAN_STATUS_PENDING;
	} else {
		if (!wlan_11h_is_active(pmpriv)) {
			/* active 11h extention in Fw */
			ret = wlan_11h_activate(pmpriv, MNULL, MTRUE);
			ret = wlan_11h_config_master_radar_det(pmpriv, MTRUE);
			ret = wlan_11h_check_update_radar_det_state(pmpriv);
		}
		if (p11h_cfg->param.chan_rpt_req.millisec_dwell_time) {
			if (pmpriv->adapter->dfs_test_params.
			    user_cac_period_msec) {
				PRINTM(MCMD_D,
				       "cfg80211 dfs_testing - user CAC period=%d (msec)\n",
				       pmpriv->adapter->dfs_test_params.
				       user_cac_period_msec);
				p11h_cfg->param.chan_rpt_req.
					millisec_dwell_time =
					pmpriv->adapter->dfs_test_params.
					user_cac_period_msec;
			}
			ret = wlan_prepare_cmd(pmpriv,
					       HostCmd_CMD_CHAN_REPORT_REQUEST,
					       HostCmd_ACT_GEN_SET, 0,
					       (t_void *)pioctl_req,
					       (t_void *)&p11h_cfg->param.
					       chan_rpt_req);

			if (ret == MLAN_STATUS_SUCCESS)
				ret = MLAN_STATUS_PENDING;
		}
	}

	LEAVE();
	return ret;
}

/**
 *  @brief Callback to finish 11H handling
 *  Not to be called directly to initiate 11H setting.
 *
 *  @param pmpriv   A pointer to mlan_private structure (cast from t_void*)
 *
 *  @return     MLAN_STATUS_PENDING --success, otherwise fail
 *  @sa         wlan_uap_snmp_mib_11h
 */
static mlan_status
wlan_uap_callback_snmp_mib_11h(t_void *priv)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	mlan_private *pmpriv = (mlan_private *)priv;
	wlan_uap_get_info_cb_t *puap_state_chan_cb = &pmpriv->uap_state_chan_cb;
	mlan_callbacks *pcb = (mlan_callbacks *)&pmpriv->adapter->callbacks;
	mlan_ds_snmp_mib *snmp;
	t_bool enable_11h;

	ENTER();
	/* clear callback now that we're here */
	puap_state_chan_cb->get_chan_callback = MNULL;

	snmp = (mlan_ds_snmp_mib *)puap_state_chan_cb->pioctl_req_curr->pbuf;
	enable_11h = (snmp->param.oid_value) ? MTRUE : MFALSE;

	if (enable_11h) {
		if ((puap_state_chan_cb->bandcfg.chanBand == BAND_5GHZ) &&
		    !wlan_can_radar_det_skip(pmpriv) &&
		    wlan_11h_radar_detect_required(pmpriv,
						   puap_state_chan_cb->
						   channel)) {
			if (!wlan_11h_is_master_radar_det_active(pmpriv))
				wlan_11h_config_master_radar_det(pmpriv, MTRUE);
		} else {
			puap_state_chan_cb->pioctl_req_curr->status_code =
				MLAN_STATUS_SUCCESS;
			pcb->moal_ioctl_complete(pmpriv->adapter->pmoal_handle,
						 puap_state_chan_cb->
						 pioctl_req_curr,
						 MLAN_STATUS_SUCCESS);
			goto done;
		}
	}

	ret = wlan_11h_activate(pmpriv,
				(t_void *)puap_state_chan_cb->pioctl_req_curr,
				enable_11h);
	wlan_11h_check_update_radar_det_state(pmpriv);
	if (ret == MLAN_STATUS_SUCCESS)
		ret = MLAN_STATUS_PENDING;
done:
	puap_state_chan_cb->pioctl_req_curr = MNULL;	/* prevent re-use */
	LEAVE();
	return ret;
}

/**
 *  @brief Set SNMP MIB for 11H
 *
 *  @param pmadapter    A pointer to mlan_adapter structure
 *  @param pioctl_req   A pointer to ioctl request buffer
 *
 *  @return     MLAN_STATUS_PENDING --success, otherwise fail
 *  @sa         wlan_uap_callback_snmp_mib_11h
 */
static mlan_status
wlan_uap_snmp_mib_11h(pmlan_adapter pmadapter, pmlan_ioctl_req pioctl_req)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_ds_snmp_mib *snmp = MNULL;
	t_bool enable;

	ENTER();

	if (pioctl_req->buf_len < sizeof(mlan_ds_snmp_mib)) {
		PRINTM(MWARN, "MLAN snmp_mib IOCTL length is too short.\n");
		pioctl_req->data_read_written = 0;
		pioctl_req->buf_len_needed = sizeof(mlan_ds_snmp_mib);
		LEAVE();
		return MLAN_STATUS_RESOURCE;
	}
	snmp = (mlan_ds_snmp_mib *)pioctl_req->pbuf;
	enable = (snmp->param.oid_value) ? MTRUE : MFALSE;

	if (enable) {
		/* first enable 11D if it is not enabled */
		if (!wlan_fw_11d_is_enabled(pmpriv)) {
			ret = wlan_11d_enable(pmpriv, MNULL, ENABLE_11D);
			if (ret != MLAN_STATUS_SUCCESS) {
				PRINTM(MERROR,
				       "Failed to first enable 11D before enabling 11H.\n");
				LEAVE();
				return ret;
			}
		}
	}

	/* store params, issue command to get UAP channel, whose CMD_RESP will
	 * callback remainder of 11H handling (and radar detect if DFS chan) */
	pmpriv->uap_state_chan_cb.pioctl_req_curr = pioctl_req;
	pmpriv->uap_state_chan_cb.get_chan_callback =
		wlan_uap_callback_snmp_mib_11h;

	ret = wlan_uap_get_channel(pmpriv);
	if (ret == MLAN_STATUS_SUCCESS)
		ret = MLAN_STATUS_PENDING;

	LEAVE();
	return ret;
}

/**
 *  @brief ACS scan
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_PENDING --success, otherwise fail
 */
static mlan_status
wlan_uap_bss_ioctl_acs_scan(pmlan_adapter pmadapter, pmlan_ioctl_req pioctl_req)
{
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_status ret = MLAN_STATUS_SUCCESS;

	ENTER();
	if (pmadapter->uap_fw_ver < UAP_FW_VERSION_2) {
		PRINTM(MIOCTL, "FW don't support ACS SCAN API\n");
		return MLAN_STATUS_FAILURE;
	}
	/* Send request to firmware */
	ret = wlan_prepare_cmd(pmpriv, HostCMD_APCMD_ACS_SCAN,
			       HostCmd_ACT_GEN_SET, 0, (t_void *)pioctl_req,
			       MNULL);

	if (ret == MLAN_STATUS_SUCCESS)
		ret = MLAN_STATUS_PENDING;

	LEAVE();
	return ret;
}

/********************************************************
			Global Functions
********************************************************/

/**
 *  @brief Issue CMD to UAP firmware to get current channel
 *
 *  @param pmpriv   A pointer to mlan_private structure
 *
 *  @return         MLAN_STATUS_SUCCESS --success, otherwise fail
 */
mlan_status
wlan_uap_get_channel(pmlan_private pmpriv)
{
	MrvlIEtypes_channel_band_t tlv_chan_band;
	mlan_status ret = MLAN_STATUS_SUCCESS;

	ENTER();
	memset(pmpriv->adapter, &tlv_chan_band, 0, sizeof(tlv_chan_band));
	tlv_chan_band.header.type = TLV_TYPE_UAP_CHAN_BAND_CONFIG;
	tlv_chan_band.header.len = sizeof(MrvlIEtypes_channel_band_t) -
		sizeof(MrvlIEtypesHeader_t);

	ret = wlan_prepare_cmd(pmpriv, HOST_CMD_APCMD_SYS_CONFIGURE,
			       HostCmd_ACT_GEN_GET, 0, MNULL, &tlv_chan_band);
	LEAVE();
	return ret;
}

/**
 *  @brief Issue CMD to UAP firmware to set current channel
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param uap_band_cfg UAP band configuration
 *  @param channel      New channel
 *
 *  @return         MLAN_STATUS_SUCCESS --success, otherwise fail
 */
mlan_status
wlan_uap_set_channel(pmlan_private pmpriv,
		     Band_Config_t uap_band_cfg, t_u8 channel)
{
	MrvlIEtypes_channel_band_t tlv_chan_band;
	mlan_status ret = MLAN_STATUS_SUCCESS;

	ENTER();
	memset(pmpriv->adapter, &tlv_chan_band, 0, sizeof(tlv_chan_band));
	tlv_chan_band.header.type = TLV_TYPE_UAP_CHAN_BAND_CONFIG;
	tlv_chan_band.header.len = sizeof(MrvlIEtypes_channel_band_t) -
		sizeof(MrvlIEtypesHeader_t);
	tlv_chan_band.bandcfg = uap_band_cfg;
	tlv_chan_band.channel = channel;

	ret = wlan_prepare_cmd(pmpriv, HOST_CMD_APCMD_SYS_CONFIGURE,
			       HostCmd_ACT_GEN_SET, 0, MNULL, &tlv_chan_band);
	LEAVE();
	return ret;
}

/**
 *  @brief Issue CMD to UAP firmware to get current beacon and dtim periods
 *
 *  @param pmpriv   A pointer to mlan_private structure
 *
 *  @return         MLAN_STATUS_SUCCESS --success, otherwise fail
 */
mlan_status
wlan_uap_get_beacon_dtim(pmlan_private pmpriv)
{
	t_u8 tlv_buffer[sizeof(MrvlIEtypes_beacon_period_t) +
			sizeof(MrvlIEtypes_dtim_period_t)];
	MrvlIEtypes_beacon_period_t *ptlv_beacon_pd;
	MrvlIEtypes_dtim_period_t *ptlv_dtim_pd;
	mlan_status ret = MLAN_STATUS_SUCCESS;

	ENTER();

	memset(pmpriv->adapter, &tlv_buffer, 0, sizeof(tlv_buffer));
	ptlv_beacon_pd = (MrvlIEtypes_beacon_period_t *)tlv_buffer;
	ptlv_beacon_pd->header.type = TLV_TYPE_UAP_BEACON_PERIOD;
	ptlv_beacon_pd->header.len = sizeof(MrvlIEtypes_beacon_period_t) -
		sizeof(MrvlIEtypesHeader_t);

	ptlv_dtim_pd =
		(MrvlIEtypes_dtim_period_t
		 *)(tlv_buffer + sizeof(MrvlIEtypes_beacon_period_t));
	ptlv_dtim_pd->header.type = TLV_TYPE_UAP_DTIM_PERIOD;
	ptlv_dtim_pd->header.len =
		sizeof(MrvlIEtypes_dtim_period_t) - sizeof(MrvlIEtypesHeader_t);

	ret = wlan_prepare_cmd(pmpriv, HOST_CMD_APCMD_SYS_CONFIGURE,
			       HostCmd_ACT_GEN_GET, 0, MNULL, tlv_buffer);
	LEAVE();
	return ret;
}

/**
 *  @brief Set/Get deauth control.
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_PENDING --success, otherwise fail
 */
static mlan_status
wlan_uap_snmp_mib_ctrl_deauth(pmlan_adapter pmadapter,
			      pmlan_ioctl_req pioctl_req)
{
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_status ret = MLAN_STATUS_SUCCESS;
	mlan_ds_snmp_mib *mib = (mlan_ds_snmp_mib *)pioctl_req->pbuf;
	t_u16 cmd_action = 0;

	ENTER();

	mib = (mlan_ds_snmp_mib *)pioctl_req->pbuf;
	if (pioctl_req->action == MLAN_ACT_SET) {
		cmd_action = HostCmd_ACT_GEN_SET;
	} else {
		cmd_action = HostCmd_ACT_GEN_GET;
	}

	/* Send command to firmware */
	ret = wlan_prepare_cmd(pmpriv, HostCmd_CMD_802_11_SNMP_MIB, cmd_action,
			       StopDeauth_i, (t_void *)pioctl_req,
			       &mib->param.deauthctrl);

	if (ret == MLAN_STATUS_SUCCESS)
		ret = MLAN_STATUS_PENDING;

	LEAVE();
	return ret;
}

/**
 *  @brief MLAN uap ioctl handler
 *
 *  @param adapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_SUCCESS --success, otherwise fail
 */
mlan_status
wlan_ops_uap_ioctl(t_void *adapter, pmlan_ioctl_req pioctl_req)
{
	pmlan_adapter pmadapter = (pmlan_adapter)adapter;
	mlan_status status = MLAN_STATUS_SUCCESS;
	mlan_ds_bss *bss = MNULL;
	mlan_ds_get_info *pget_info = MNULL;
	mlan_ds_misc_cfg *misc = MNULL;
	mlan_ds_sec_cfg *sec = MNULL;
	mlan_ds_power_cfg *power = MNULL;
	mlan_ds_pm_cfg *pm = MNULL;
	mlan_ds_11d_cfg *cfg11d = MNULL;
	mlan_ds_snmp_mib *snmp = MNULL;
	mlan_ds_11h_cfg *cfg11h = MNULL;
	mlan_ds_radio_cfg *radiocfg = MNULL;
	mlan_ds_rate *rate = MNULL;
	mlan_ds_reg_mem *reg_mem = MNULL;
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];
#if defined(STA_SUPPORT) && defined(UAP_SUPPORT)
	pmlan_ds_scan pscan;
#endif

	ENTER();
	switch (pioctl_req->req_id) {
	case MLAN_IOCTL_BSS:
		bss = (mlan_ds_bss *)pioctl_req->pbuf;
		if (bss->sub_command == MLAN_OID_BSS_MAC_ADDR)
			status = wlan_uap_bss_ioctl_mac_address(pmadapter,
								pioctl_req);
		else if (bss->sub_command == MLAN_OID_BSS_STOP)
			status = wlan_uap_bss_ioctl_stop(pmadapter, pioctl_req);
		else if (bss->sub_command == MLAN_OID_BSS_START)
			status = wlan_uap_bss_ioctl_start(pmadapter,
							  pioctl_req);
		else if (bss->sub_command == MLAN_OID_UAP_BSS_CONFIG)
			status = wlan_uap_bss_ioctl_config(pmadapter,
							   pioctl_req);
		else if (bss->sub_command == MLAN_OID_UAP_DEAUTH_STA)
			status = wlan_uap_bss_ioctl_deauth_sta(pmadapter,
							       pioctl_req);
		else if (bss->sub_command == MLAN_OID_UAP_BSS_RESET)
			status = wlan_uap_bss_ioctl_reset(pmadapter,
							  pioctl_req);
#if defined(STA_SUPPORT) && defined(UAP_SUPPORT)
		else if (bss->sub_command == MLAN_OID_BSS_ROLE) {
			util_enqueue_list_tail(pmadapter->pmoal_handle,
					       &pmadapter->ioctl_pending_q,
					       (pmlan_linked_list)pioctl_req,
					       pmadapter->callbacks.
					       moal_spin_lock,
					       pmadapter->callbacks.
					       moal_spin_unlock);
			pmadapter->pending_ioctl = MTRUE;
			status = MLAN_STATUS_PENDING;
		}
#endif
#ifdef WIFI_DIRECT_SUPPORT
		else if (bss->sub_command == MLAN_OID_WIFI_DIRECT_MODE)
			status = wlan_bss_ioctl_wifi_direct_mode(pmadapter,
								 pioctl_req);
#endif
		else if (bss->sub_command == MLAN_OID_BSS_REMOVE)
			status = wlan_bss_ioctl_bss_remove(pmadapter,
							   pioctl_req);
		else if (bss->sub_command == MLAN_OID_UAP_CFG_WMM_PARAM)
			status = wlan_uap_bss_ioctl_uap_wmm_param(pmadapter,
								  pioctl_req);
		else if (bss->sub_command == MLAN_OID_UAP_SCAN_CHANNELS)
			status = wlan_uap_bss_ioctl_uap_scan_channels(pmadapter,
								      pioctl_req);
		else if (bss->sub_command == MLAN_OID_UAP_CHANNEL)
			status = wlan_uap_bss_ioctl_uap_channel(pmadapter,
								pioctl_req);
		else if (bss->sub_command == MLAN_OID_UAP_ACS_SCAN)
			status = wlan_uap_bss_ioctl_acs_scan(pmadapter,
							     pioctl_req);
		else if (bss->sub_command == MLAN_OID_UAP_OPER_CTRL)
			status = wlan_uap_bss_ioctl_uap_oper_ctrl(pmadapter,
								  pioctl_req);
		else if (bss->sub_command == MLAN_OID_UAP_ADD_STATION)
			status = wlan_uap_bss_ioctl_add_station(pmadapter,
								pioctl_req);
		else if (bss->sub_command == MLAN_OID_ACTION_CHAN_SWITCH)
			status = wlan_uap_bss_ioctl_action_chan_switch
				(pmadapter, pioctl_req);
		break;
#if defined(STA_SUPPORT) && defined(UAP_SUPPORT)
	case MLAN_IOCTL_SCAN:
		pscan = (mlan_ds_scan *)pioctl_req->pbuf;
		if ((pscan->sub_command == MLAN_OID_SCAN_NORMAL) &&
		    (pioctl_req->action == MLAN_ACT_GET)) {
			PRINTM(MIOCTL, "Get scan table in uap\n");
			pscan->param.scan_resp.pscan_table =
				(t_u8 *)pmadapter->pscan_table;
			pscan->param.scan_resp.num_in_scan_table =
				pmadapter->num_in_scan_table;
			pscan->param.scan_resp.age_in_secs =
				pmadapter->age_in_secs;
			pioctl_req->data_read_written =
				sizeof(mlan_scan_resp) + MLAN_SUB_COMMAND_SIZE;
			pscan->param.scan_resp.pchan_stats =
				(t_u8 *)pmadapter->pchan_stats;
			pscan->param.scan_resp.num_in_chan_stats =
				pmadapter->num_in_chan_stats;
		}
		break;
#endif
	case MLAN_IOCTL_GET_INFO:
		pget_info = (mlan_ds_get_info *)pioctl_req->pbuf;
		if (pget_info->sub_command == MLAN_OID_GET_VER_EXT)
			status = wlan_get_info_ver_ext(pmadapter, pioctl_req);
		else if (pget_info->sub_command == MLAN_OID_GET_DEBUG_INFO)
			status = wlan_get_info_debug_info(pmadapter,
							  pioctl_req);
		else if (pget_info->sub_command == MLAN_OID_GET_STATS)
			status = wlan_uap_get_stats(pmadapter, pioctl_req);
		else if (pget_info->sub_command == MLAN_OID_GET_UAP_STATS_LOG)
			status = wlan_uap_get_stats_log(pmadapter, pioctl_req);
		else if (pget_info->sub_command == MLAN_OID_UAP_STA_LIST)
			status = wlan_uap_get_sta_list(pmadapter, pioctl_req);
		else if (pget_info->sub_command == MLAN_OID_GET_BSS_INFO)
			status = wlan_uap_get_bss_info(pmadapter, pioctl_req);
		else if (pget_info->sub_command == MLAN_OID_GET_FW_INFO) {
			pioctl_req->data_read_written =
				sizeof(mlan_fw_info) + MLAN_SUB_COMMAND_SIZE;
			memcpy_ext(pmadapter,
				   &pget_info->param.fw_info.mac_addr,
				   pmpriv->curr_addr, MLAN_MAC_ADDR_LENGTH,
				   MLAN_MAC_ADDR_LENGTH);
			pget_info->param.fw_info.fw_ver =
				pmadapter->fw_release_number;
			pget_info->param.fw_info.hotfix_version =
				pmadapter->fw_hotfix_ver;
			pget_info->param.fw_info.fw_bands = pmadapter->fw_bands;
			pget_info->param.fw_info.ecsa_enable =
				pmadapter->ecsa_enable;
			pget_info->param.fw_info.getlog_enable =
				pmadapter->getlog_enable;
			pget_info->param.fw_info.hw_dev_mcs_support =
				pmadapter->hw_dev_mcs_support;
			pget_info->param.fw_info.hw_dot_11n_dev_cap =
				pmadapter->hw_dot_11n_dev_cap;
			pget_info->param.fw_info.usr_dev_mcs_support =
				pmpriv->usr_dev_mcs_support;
			if (IS_FW_SUPPORT_NO_80MHZ(pmadapter))
				pget_info->param.fw_info.prohibit_80mhz = MTRUE;
			else
				pget_info->param.fw_info.prohibit_80mhz =
					MFALSE;
			pget_info->param.fw_info.hw_dot_11ac_mcs_support =
				pmadapter->hw_dot_11ac_mcs_support;
			pget_info->param.fw_info.hw_dot_11ac_dev_cap =
				pmadapter->hw_dot_11ac_dev_cap;
			pget_info->param.fw_info.usr_dot_11ac_dev_cap_bg =
				pmpriv->usr_dot_11ac_dev_cap_bg;
			pget_info->param.fw_info.usr_dot_11ac_mcs_support =
				pmpriv->usr_dot_11ac_mcs_support;
			pget_info->param.fw_info.usr_dot_11ac_dev_cap_a =
				pmpriv->usr_dot_11ac_dev_cap_a;
			pget_info->param.fw_info.hw_hecap_len =
				pmadapter->hw_hecap_len;
			pget_info->param.fw_info.hw_2g_hecap_len =
				pmadapter->hw_2g_hecap_len;
			memcpy_ext(pmadapter,
				   pget_info->param.fw_info.hw_he_cap,
				   pmadapter->hw_he_cap,
				   pmadapter->hw_hecap_len,
				   sizeof(pget_info->param.fw_info.hw_he_cap));
			memcpy_ext(pmadapter,
				   pget_info->param.fw_info.hw_2g_he_cap,
				   pmadapter->hw_2g_he_cap,
				   pmadapter->hw_2g_hecap_len,
				   sizeof(pget_info->param.fw_info.
					  hw_2g_he_cap));
			pget_info->param.fw_info.region_code =
				pmadapter->region_code;
			if (pmadapter->otp_region &&
			    pmadapter->otp_region->force_reg)
				pget_info->param.fw_info.force_reg = MTRUE;
			else
				pget_info->param.fw_info.force_reg = MFALSE;
			pget_info->param.fw_info.fw_supplicant_support =
				IS_FW_SUPPORT_SUPPLICANT(pmadapter) ? 0x01 :
				0x00;
			pget_info->param.fw_info.antinfo = pmadapter->antinfo;
			pget_info->param.fw_info.max_ap_assoc_sta =
				pmadapter->max_sta_conn;
		} else if (pget_info->sub_command == MLAN_OID_LINK_STATS)
			status = wlan_ioctl_link_statistic(pmpriv, pioctl_req);
		break;
	case MLAN_IOCTL_MISC_CFG:
		misc = (mlan_ds_misc_cfg *)pioctl_req->pbuf;
		if (misc->sub_command == MLAN_OID_MISC_INIT_SHUTDOWN)
			status = wlan_misc_ioctl_init_shutdown(pmadapter,
							       pioctl_req);
		if (misc->sub_command == MLAN_OID_MISC_SOFT_RESET)
			status = wlan_uap_misc_ioctl_soft_reset(pmadapter,
								pioctl_req);
		if (misc->sub_command == MLAN_OID_MISC_HOST_CMD)
			status = wlan_misc_ioctl_host_cmd(pmadapter,
							  pioctl_req);
		if (misc->sub_command == MLAN_OID_MISC_REGION)
			status = wlan_misc_ioctl_region(pmadapter, pioctl_req);
		if (misc->sub_command == MLAN_OID_MISC_GEN_IE)
			status = wlan_uap_misc_ioctl_gen_ie(pmadapter,
							    pioctl_req);
		if (misc->sub_command == MLAN_OID_MISC_CUSTOM_IE)
			status = wlan_misc_ioctl_custom_ie_list(pmadapter,
								pioctl_req,
								MTRUE);
		if (misc->sub_command == MLAN_OID_MISC_TX_DATAPAUSE)
			status = wlan_uap_misc_ioctl_txdatapause(pmadapter,
								 pioctl_req);
		if (misc->sub_command == MLAN_OID_MISC_RX_MGMT_IND)
			status = wlan_reg_rx_mgmt_ind(pmadapter, pioctl_req);
#ifdef DEBUG_LEVEL1
		if (misc->sub_command == MLAN_OID_MISC_DRVDBG)
			status = wlan_set_drvdbg(pmadapter, pioctl_req);
#endif

		if (misc->sub_command == MLAN_OID_MISC_TXCONTROL)
			status = wlan_misc_ioctl_txcontrol(pmadapter,
							   pioctl_req);
		if (misc->sub_command == MLAN_OID_MISC_MAC_CONTROL)
			status = wlan_misc_ioctl_mac_control(pmadapter,
							     pioctl_req);
#ifdef RX_PACKET_COALESCE
		if (misc->sub_command == MLAN_OID_MISC_RX_PACKET_COALESCE)
			status = wlan_misc_ioctl_rx_pkt_coalesce_config
				(pmadapter, pioctl_req);
#endif
#ifdef WIFI_DIRECT_SUPPORT
		if (misc->sub_command == MLAN_OID_MISC_WIFI_DIRECT_CONFIG)
			status = wlan_misc_p2p_config(pmadapter, pioctl_req);
#endif
		if (misc->sub_command == MLAN_OID_MISC_GPIO_TSF_LATCH)
			status = wlan_misc_gpio_tsf_latch_config(pmadapter,
								 pioctl_req);
		if (misc->sub_command == MLAN_OID_MISC_GET_TSF_INFO)
			status = wlan_misc_get_tsf_info(pmadapter, pioctl_req);

		if (misc->sub_command == MLAN_OID_MISC_DFS_REAPTER_MODE) {
			mlan_ds_misc_cfg *misc_cfg = MNULL;

			misc_cfg = (mlan_ds_misc_cfg *)pioctl_req->pbuf;
			misc_cfg->param.dfs_repeater.mode =
				pmadapter->dfs_repeater;
			pioctl_req->data_read_written =
				sizeof(mlan_ds_misc_dfs_repeater);

			status = MLAN_STATUS_SUCCESS;
		}
		if (misc->sub_command == MLAN_OID_MISC_IND_RST_CFG)
			status = wlan_misc_ioctl_ind_rst_cfg(pmadapter,
							     pioctl_req);
		if (misc->sub_command == MLAN_OID_MISC_GET_TSF)
			status = wlan_misc_ioctl_get_tsf(pmadapter, pioctl_req);
		if (misc->sub_command == MLAN_OID_MISC_GET_CHAN_REGION_CFG)
			status = wlan_misc_chan_reg_cfg(pmadapter, pioctl_req);
		if (misc->sub_command == MLAN_OID_MISC_OPER_CLASS_CHECK)
			status = wlan_misc_ioctl_operclass_validation(pmadapter,
								      pioctl_req);
		if (misc->sub_command == MLAN_OID_MISC_OPER_CLASS)
			status = wlan_misc_ioctl_oper_class(pmadapter,
							    pioctl_req);
		if (misc->sub_command == MLAN_OID_MISC_AGGR_CTRL)
			status = wlan_misc_ioctl_aggr_ctrl(pmadapter,
							   pioctl_req);
		if (misc->sub_command == MLAN_OID_MISC_PER_PKT_CFG)
			status = wlan_misc_per_pkt_cfg(pmadapter, pioctl_req);
		if (misc->sub_command == MLAN_OID_MISC_FW_DUMP_EVENT)
			status = wlan_misc_ioctl_fw_dump_event(pmadapter,
							       pioctl_req);
		if (misc->sub_command == MLAN_OID_MISC_RX_ABORT_CFG)
			status = wlan_misc_ioctl_rxabortcfg(pmadapter,
							    pioctl_req);
		if (misc->sub_command == MLAN_OID_MISC_RX_ABORT_CFG_EXT)
			status = wlan_misc_ioctl_rxabortcfg_ext(pmadapter,
								pioctl_req);
		if (misc->sub_command == MLAN_OID_MISC_TX_AMPDU_PROT_MODE)
			status = wlan_misc_ioctl_tx_ampdu_prot_mode(pmadapter,
								    pioctl_req);
		if (misc->sub_command == MLAN_OID_MISC_DOT11MC_UNASSOC_FTM_CFG)
			status = wlan_misc_ioctl_dot11mc_unassoc_ftm_cfg
				(pmadapter, pioctl_req);
		if (misc->sub_command == MLAN_OID_MISC_HAL_PHY_CFG)
			status = wlan_misc_hal_phy_cfg(pmadapter, pioctl_req);
		if (misc->sub_command == MLAN_OID_MISC_RATE_ADAPT_CFG)
			status = wlan_misc_ioctl_rate_adapt_cfg(pmadapter,
								pioctl_req);
		if (misc->sub_command == MLAN_OID_MISC_CCK_DESENSE_CFG)
			status = wlan_misc_ioctl_cck_desense_cfg(pmadapter,
								 pioctl_req);
		if (misc->sub_command == MLAN_OID_MISC_ROBUSTCOEX)
			status = wlan_misc_robustcoex(pmadapter, pioctl_req);
		if (misc->sub_command == MLAN_OID_MISC_DMCS_CONFIG)
			status = wlan_misc_dmcs_config(pmadapter, pioctl_req);
		if (misc->sub_command == MLAN_OID_MISC_GET_TX_RX_HISTOGRAM)
			status = wlan_get_tx_rx_histogram(pmadapter,
							  pioctl_req);
		if (misc->sub_command == MLAN_OID_MISC_CFP_INFO)
			status = wlan_get_cfpinfo(pmadapter, pioctl_req);
		if (misc->sub_command == MLAN_OID_MISC_BOOT_SLEEP)
			status = wlan_misc_bootsleep(pmadapter, pioctl_req);
		if (misc->sub_command == MLAN_OID_MISC_DYN_BW)
			status = wlan_misc_ioctl_dyn_bw(pmadapter, pioctl_req);
		if (misc->sub_command == MLAN_OID_MISC_GET_CHAN_TRPC_CFG)
			status = wlan_get_chan_trpc_cfg(pmadapter, pioctl_req);
		if (misc->sub_command == MLAN_OID_MISC_BAND_STEERING)
			status = wlan_misc_band_steering_cfg(pmadapter,
							     pioctl_req);
		if (misc->sub_command == MLAN_OID_MISC_BEACON_STUCK)
			status = wlan_misc_beacon_stuck_cfg(pmadapter,
							    pioctl_req);
		if (misc->sub_command == MLAN_OID_MISC_GET_REGIONPWR_CFG)
			status = wlan_get_rgchnpwr_cfg(pmadapter, pioctl_req);
		if (misc->sub_command == MLAN_OID_MISC_CFP_TABLE)
			status = wlan_get_cfp_table(pmadapter, pioctl_req);
		if (misc->sub_command == MLAN_OID_MISC_RANGE_EXT)
			status = wlan_misc_ioctl_range_ext(pmadapter,
							   pioctl_req);
		if (misc->sub_command == MLAN_OID_MISC_WACP_MODE)
			status = wlan_misc_ioctl_wacp_mode(pmadapter,
							   pioctl_req);
		break;
	case MLAN_IOCTL_POWER_CFG:
		power = (mlan_ds_power_cfg *)pioctl_req->pbuf;
		if (power->sub_command == MLAN_OID_POWER_LOW_POWER_MODE)
			status = wlan_power_ioctl_set_get_lpm(pmadapter,
							      pioctl_req);
		break;
	case MLAN_IOCTL_PM_CFG:
		pm = (mlan_ds_pm_cfg *)pioctl_req->pbuf;
		if (pm->sub_command == MLAN_OID_PM_CFG_PS_MODE)
			status = wlan_uap_pm_ioctl_mode(pmadapter, pioctl_req);
		if (pm->sub_command == MLAN_OID_PM_CFG_DEEP_SLEEP)
			status = wlan_uap_pm_ioctl_deepsleep(pmadapter,
							     pioctl_req);
		if (pm->sub_command == MLAN_OID_PM_CFG_HS_CFG)
			status = wlan_pm_ioctl_hscfg(pmadapter, pioctl_req);
		if (pm->sub_command == MLAN_OID_PM_HS_WAKEUP_REASON)
			status = wlan_get_hs_wakeup_reason(pmadapter,
							   pioctl_req);
		if (pm->sub_command == MLAN_OID_PM_MGMT_FILTER)
			status = wlan_config_mgmt_filter(pmadapter, pioctl_req);
		if (pm->sub_command == MLAN_OID_PM_INFO)
			status = wlan_get_pm_info(pmadapter, pioctl_req);
		break;
	case MLAN_IOCTL_SNMP_MIB:
		snmp = (mlan_ds_snmp_mib *)pioctl_req->pbuf;
		if (snmp->sub_command == MLAN_OID_SNMP_MIB_CTRL_DEAUTH)
			status = wlan_uap_snmp_mib_ctrl_deauth(pmadapter,
							       pioctl_req);
		if (snmp->sub_command == MLAN_OID_SNMP_MIB_DOT11D)
			status = wlan_uap_snmp_mib_11d(pmadapter, pioctl_req);
		if (snmp->sub_command == MLAN_OID_SNMP_MIB_DOT11H)
			status = wlan_uap_snmp_mib_11h(pmadapter, pioctl_req);
		break;
	case MLAN_IOCTL_SEC_CFG:
		sec = (mlan_ds_sec_cfg *)pioctl_req->pbuf;
		if (sec->sub_command == MLAN_OID_SEC_CFG_ENCRYPT_KEY)
			status = wlan_uap_sec_ioctl_set_encrypt_key(pmadapter,
								    pioctl_req);
		if (sec->sub_command == MLAN_OID_SEC_CFG_WAPI_ENABLED)
			status = wlan_uap_sec_ioctl_wapi_enable(pmadapter,
								pioctl_req);
		if (sec->sub_command == MLAN_OID_SEC_CFG_REPORT_MIC_ERR)
			status = wlan_uap_sec_ioctl_report_mic_error(pmadapter,
								     pioctl_req);
		break;
	case MLAN_IOCTL_11N_CFG:
		status = wlan_11n_cfg_ioctl(pmadapter, pioctl_req);
		break;
	case MLAN_IOCTL_11D_CFG:
		cfg11d = (mlan_ds_11d_cfg *)pioctl_req->pbuf;
		if (cfg11d->sub_command == MLAN_OID_11D_DOMAIN_INFO)
			status = wlan_uap_domain_info(pmadapter, pioctl_req);
		else if (cfg11d->sub_command == MLAN_OID_11D_DOMAIN_INFO_EXT)
			status = wlan_11d_cfg_domain_info(pmadapter,
							  pioctl_req);
		break;
	case MLAN_IOCTL_11H_CFG:
		cfg11h = (mlan_ds_11h_cfg *)pioctl_req->pbuf;
		if (cfg11h->sub_command == MLAN_OID_11H_CHANNEL_CHECK)
			status = wlan_uap_11h_channel_check_req(pmadapter,
								pioctl_req);
		if (cfg11h->sub_command == MLAN_OID_11H_DFS_TESTING)
			status = wlan_11h_ioctl_dfs_testing(pmadapter,
							    pioctl_req);
		if (cfg11h->sub_command == MLAN_OID_11H_CHAN_NOP_INFO)
			status = wlan_11h_ioctl_get_channel_nop_info(pmadapter,
								     pioctl_req);
		if (cfg11h->sub_command == MLAN_OID_11H_CHAN_REPORT_REQUEST)
			status = wlan_11h_ioctl_dfs_chan_report(pmpriv,
								pioctl_req);
		if (cfg11h->sub_command == MLAN_OID_11H_CHAN_SWITCH_COUNT)
			status = wlan_11h_ioctl_chan_switch_count(pmadapter,
								  pioctl_req);
		if (cfg11h->sub_command == MLAN_OID_11H_DFS_W53_CFG)
			status = wlan_11h_ioctl_dfs_w53_cfg(pmadapter,
							    pioctl_req);
		break;
	case MLAN_IOCTL_RADIO_CFG:
		radiocfg = (mlan_ds_radio_cfg *)pioctl_req->pbuf;
		if (radiocfg->sub_command == MLAN_OID_MIMO_SWITCH)
			status = wlan_radio_ioctl_mimo_switch_cfg(pmadapter,
								  pioctl_req);
		if (radiocfg->sub_command == MLAN_OID_RADIO_CTRL)
			status = wlan_radio_ioctl_radio_ctl(pmadapter,
							    pioctl_req);
		if (radiocfg->sub_command == MLAN_OID_REMAIN_CHAN_CFG)
			status = wlan_radio_ioctl_remain_chan_cfg(pmadapter,
								  pioctl_req);
		if (radiocfg->sub_command == MLAN_OID_ANT_CFG)
			status = wlan_radio_ioctl_ant_cfg(pmadapter,
							  pioctl_req);
		if (radiocfg->sub_command == MLAN_OID_BAND_CFG)
			status = wlan_radio_ioctl_band_cfg(pmadapter,
							   pioctl_req);
		break;
	case MLAN_IOCTL_RATE:
		rate = (mlan_ds_rate *)pioctl_req->pbuf;
		if (rate->sub_command == MLAN_OID_RATE_CFG)
			status = wlan_rate_ioctl_cfg(pmadapter, pioctl_req);
		else if (rate->sub_command == MLAN_OID_GET_DATA_RATE)
			status = wlan_rate_ioctl_get_data_rate(pmadapter,
							       pioctl_req);
		break;
	case MLAN_IOCTL_11AC_CFG:
		status = wlan_11ac_cfg_ioctl(pmadapter, pioctl_req);
		break;
	case MLAN_IOCTL_11AX_CFG:
		status = wlan_11ax_cfg_ioctl(pmadapter, pioctl_req);
		break;
	case MLAN_IOCTL_REG_MEM:
		reg_mem = (mlan_ds_reg_mem *)pioctl_req->pbuf;
		if (reg_mem->sub_command == MLAN_OID_REG_RW)
			status = wlan_reg_mem_ioctl_reg_rw(pmadapter,
							   pioctl_req);
		else if (reg_mem->sub_command == MLAN_OID_EEPROM_RD)
			status = wlan_reg_mem_ioctl_read_eeprom(pmadapter,
								pioctl_req);
		else if (reg_mem->sub_command == MLAN_OID_MEM_RW)
			status = wlan_reg_mem_ioctl_mem_rw(pmadapter,
							   pioctl_req);
		break;
	case MLAN_IOCTL_WMM_CFG:
		status = wlan_wmm_cfg_ioctl(pmadapter, pioctl_req);
		break;
	default:
		pioctl_req->status_code = MLAN_ERROR_IOCTL_INVALID;
		break;
	}
	LEAVE();
	return status;
}
