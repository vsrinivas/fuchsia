/**
 * @file mlan_cmdevt.c
 *
 *  @brief This file contains the handling of CMD/EVENT in MLAN
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
    05/12/2009: initial version
************************************************************/
#include "mlan.h"
#ifdef STA_SUPPORT
#include "mlan_join.h"
#endif
#include "mlan_util.h"
#include "mlan_fw.h"
#include "mlan_main.h"
#include "mlan_wmm.h"
#include "mlan_11n.h"
#include "mlan_11ac.h"
#include "mlan_11ax.h"
#include "mlan_11h.h"
#ifdef SDIO
#include "mlan_sdio.h"
#endif /* SDIO */
#ifdef PCIE
#include "mlan_pcie.h"
#endif /* PCIE */
#include "mlan_init.h"

/********************************************************
			Local Variables
********************************************************/

/*******************************************************
			Global Variables
********************************************************/

/********************************************************
			Local Functions
********************************************************/
#ifdef STA_SUPPORT
/**
 *  @brief This function inserts scan command node to scan_pending_q.
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param pcmd_node    A pointer to cmd_ctrl_node structure
 *  @return             N/A
 */
static t_void
wlan_queue_scan_cmd(mlan_private *pmpriv, cmd_ctrl_node *pcmd_node)
{
	mlan_adapter *pmadapter = pmpriv->adapter;

	ENTER();

	if (pcmd_node == MNULL)
		goto done;
	pcmd_node->cmd_flag |= CMD_F_SCAN;

	util_enqueue_list_tail(pmadapter->pmoal_handle,
			       &pmadapter->scan_pending_q,
			       (pmlan_linked_list)pcmd_node, MNULL, MNULL);

done:
	LEAVE();
}

/**
 *  @brief This function check if cmd allowed to send to firmware
 *         during scan
 *
 *  @param cmd_id     cmd id
 *
 *  @return           MTRUE/MFALSE
 */
static t_u8
wlan_is_cmd_allowed_during_scan(t_u16 cmd_id)
{
	t_u8 ret = MTRUE;
	ENTER();
	switch (cmd_id) {
	case HostCmd_CMD_FUNC_INIT:
	case HostCmd_CMD_CFG_DATA:
	case HostCmd_CMD_REGION_POWER_CFG:
	case HostCmd_CHANNEL_TRPC_CONFIG:
	case HostCmd_CMD_FUNC_SHUTDOWN:
	case HostCmd_CMD_802_11_ASSOCIATE:
	case HostCmd_CMD_802_11_DEAUTHENTICATE:
	case HostCmd_CMD_802_11_DISASSOCIATE:
	case HostCmd_CMD_802_11_AD_HOC_START:
	case HostCmd_CMD_802_11_AD_HOC_JOIN:
	case HostCmd_CMD_802_11_AD_HOC_STOP:
	case HostCmd_CMD_11N_ADDBA_REQ:
	case HostCmd_CMD_11N_ADDBA_RSP:
	case HostCmd_CMD_11N_DELBA:
	case HostCmd_CMD_802_11_REMAIN_ON_CHANNEL:
	case HostCmd_CMD_TDLS_CONFIG:
	case HostCmd_CMD_TDLS_OPERATION:
	case HostCmd_CMD_SOFT_RESET:
#ifdef UAP_SUPPORT
	case HOST_CMD_APCMD_SYS_RESET:
	case HOST_CMD_APCMD_BSS_START:
	case HOST_CMD_APCMD_BSS_STOP:
	case HOST_CMD_APCMD_STA_DEAUTH:
#endif
	case HostCMD_APCMD_ACS_SCAN:
		ret = MFALSE;
		break;
	default:
		break;
	}
	LEAVE();
	return ret;
}

/**
 *  @brief This function move the cmd from scan_pending_q to
 *        cmd_pending_q
 *
 *  @param cmd_id     cmd id
 *
 *  @return           MTRUE/MFALSE
 */
t_void
wlan_move_cmd_to_cmd_pending_q(pmlan_adapter pmadapter)
{
	cmd_ctrl_node *pcmd_node = MNULL;

	ENTER();

	wlan_request_cmd_lock(pmadapter);
	while ((pcmd_node =
		(cmd_ctrl_node *)util_peek_list(pmadapter->pmoal_handle,
						&pmadapter->scan_pending_q,
						MNULL, MNULL))) {
		util_unlink_list(pmadapter->pmoal_handle,
				 &pmadapter->scan_pending_q,
				 (pmlan_linked_list)pcmd_node, MNULL, MNULL);
		wlan_insert_cmd_to_pending_q(pmadapter, pcmd_node, MTRUE);
	}
	wlan_release_cmd_lock(pmadapter);
	LEAVE();
}

/**
 *  @brief This function inserts command node to scan_pending_q or
 *  cmd_pending_q
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param pcmd_node    A pointer to cmd_ctrl_node structure
 *  @return             N/A
 */

static t_void
wlan_queue_cmd(mlan_private *pmpriv, cmd_ctrl_node *pcmd_node, t_u16 cmd_no)
{
	ENTER();
	if (pmpriv->adapter->scan_processing &&
	    pmpriv->adapter->ext_scan_type == EXT_SCAN_ENHANCE) {
		if (MFALSE == wlan_is_cmd_allowed_during_scan(cmd_no)) {
			PRINTM(MCMND, "QUEUE_CMD: cmd=0x%x scan_pending_q\n",
			       cmd_no);
			wlan_queue_scan_cmd(pmpriv, pcmd_node);
			return;
		}
	}
	wlan_insert_cmd_to_pending_q(pmpriv->adapter, pcmd_node, MTRUE);
	LEAVE();
}

/**
 *  @brief Internal function used to flush the scan pending queue
 *
 *  @param pmadapter    A pointer to mlan_adapter structure
 *
 *  @return             N/A
 */
static void
wlan_check_scan_queue(pmlan_adapter pmadapter)
{
	cmd_ctrl_node *pcmd_node = MNULL;
	t_u16 num = 0;

	pcmd_node = (cmd_ctrl_node *)util_peek_list(pmadapter->pmoal_handle,
						    &pmadapter->scan_pending_q,
						    MNULL, MNULL);
	if (!pcmd_node) {
		PRINTM(MERROR, "No pending scan command\n");
		return;
	}
	while (pcmd_node != (cmd_ctrl_node *)&pmadapter->scan_pending_q) {
		num++;
		pcmd_node = pcmd_node->pnext;
	}
	PRINTM(MERROR, "num_pending_scan=%d\n", num);
}
#endif

/**
 *  @brief This function will dump the pending commands id
 *
 *  @param pmadapter    A pointer to mlan_adapter
 *
 *  @return             N/A
 */
static void
wlan_dump_pending_commands(pmlan_adapter pmadapter)
{
	cmd_ctrl_node *pcmd_node = MNULL;
	HostCmd_DS_COMMAND *pcmd;

	ENTER();
	wlan_request_cmd_lock(pmadapter);
	pcmd_node = (cmd_ctrl_node *)util_peek_list(pmadapter->pmoal_handle,
						    &pmadapter->cmd_pending_q,
						    MNULL, MNULL);
	if (!pcmd_node) {
		wlan_release_cmd_lock(pmadapter);
		LEAVE();
		return;
	}
	while (pcmd_node != (cmd_ctrl_node *)&pmadapter->cmd_pending_q) {
		pcmd = (HostCmd_DS_COMMAND *)(pcmd_node->cmdbuf->pbuf +
					      pcmd_node->cmdbuf->data_offset);
		PRINTM(MERROR, "pending command id: 0x%x ioctl_buf=%p\n",
		       wlan_le16_to_cpu(pcmd->command), pcmd_node->pioctl_buf);
		pcmd_node = pcmd_node->pnext;
	}
#ifdef STA_SUPPORT
	wlan_check_scan_queue(pmadapter);
#endif
	wlan_release_cmd_lock(pmadapter);
	LEAVE();
	return;
}

#define REASON_CODE_NO_CMD_NODE 1
#define REASON_CODE_CMD_TIMEOUT 2
#define REASON_CODE_CMD_TO_CARD_FAILURE 3
#define REASON_CODE_EXT_SCAN_TIMEOUT 4
/**
 *  @brief This function dump debug info
 *
 *  @return     N/A
 */
static t_void
wlan_dump_info(mlan_adapter *pmadapter, t_u8 reason)
{
	cmd_ctrl_node *pcmd_node = MNULL;
#ifdef DEBUG_LEVEL1
	t_u32 sec = 0, usec = 0;
#endif
	t_u16 i;
#ifdef SDIO
	t_u8 j;
	t_u8 mp_aggr_pkt_limit;
#endif
	t_u16 cmd_id, cmd_act;
	mlan_private *pmpriv = wlan_get_priv(pmadapter, MLAN_BSS_ROLE_ANY);

	ENTER();

	PRINTM(MERROR, "------------Dump info-----------\n", reason);
	switch (reason) {
	case REASON_CODE_NO_CMD_NODE:
		pmadapter->dbg.num_no_cmd_node++;
		PRINTM(MERROR, "No Free command node\n");
		break;
	case REASON_CODE_CMD_TIMEOUT:
		PRINTM(MERROR, "Commmand Timeout\n");
		break;
	case REASON_CODE_CMD_TO_CARD_FAILURE:
		PRINTM(MERROR, "Command to card failure\n");
		break;
	case REASON_CODE_EXT_SCAN_TIMEOUT:
		PRINTM(MERROR, "EXT_SCAN_STATUS event Timeout\n");
		break;
	default:
		break;
	}
	if ((reason == REASON_CODE_NO_CMD_NODE) &&
	    (pmadapter->dbg.num_no_cmd_node > 1)) {
		if (pmadapter->dbg.num_no_cmd_node >= 5) {
			if (pmpriv)
				wlan_recv_event(pmpriv,
						MLAN_EVENT_ID_DRV_DBG_DUMP,
						MNULL);
		}
		LEAVE();
		return;
	}
	wlan_dump_pending_commands(pmadapter);
	if (reason != REASON_CODE_CMD_TIMEOUT) {
		if (!pmadapter->curr_cmd) {
			PRINTM(MERROR, "CurCmd Empty\n");
		} else {
			pcmd_node = pmadapter->curr_cmd;
			cmd_id = pmadapter->dbg.last_cmd_id
				[pmadapter->dbg.last_cmd_index];
			cmd_act = pmadapter->dbg.last_cmd_act
				[pmadapter->dbg.last_cmd_index];
			PRINTM_GET_SYS_TIME(MERROR, &sec, &usec);
			PRINTM(MERROR,
			       "Current cmd id (%lu.%06lu) = 0x%x, act = 0x%x\n",
			       sec, usec, cmd_id, cmd_act);
#if defined(SDIO) || defined(PCIE)
			if (!IS_USB(pmadapter->card_type) && pcmd_node->cmdbuf) {
				t_u8 *pcmd_buf;
				pcmd_buf = pcmd_node->cmdbuf->pbuf +
					pcmd_node->cmdbuf->data_offset +
					pmadapter->ops.intf_header_len;
				for (i = 0; i < 16; i++)
					PRINTM(MERROR, "%02x ", *pcmd_buf++);
				PRINTM(MERROR, "\n");
			}
#endif
			pmpriv = pcmd_node->priv;
			if (pmpriv)
				PRINTM(MERROR, "BSS type = %d BSS role= %d\n",
				       pmpriv->bss_type, pmpriv->bss_role);
		}
	}
	PRINTM(MERROR, "mlan_processing =%d\n", pmadapter->mlan_processing);
	PRINTM(MERROR, "main_lock_flag =%d\n", pmadapter->main_lock_flag);
	PRINTM(MERROR, "main_process_cnt =%d\n", pmadapter->main_process_cnt);
	PRINTM(MERROR, "delay_task_flag =%d\n", pmadapter->delay_task_flag);
	PRINTM(MERROR, "mlan_rx_processing =%d\n",
	       pmadapter->mlan_rx_processing);
	PRINTM(MERROR, "rx_pkts_queued=%d\n", pmadapter->rx_pkts_queued);
	PRINTM(MERROR, "more_task_flag = %d\n", pmadapter->more_task_flag);
	PRINTM(MERROR, "num_cmd_timeout = %d\n", pmadapter->num_cmd_timeout);
	PRINTM(MERROR, "last_cmd_index = %d\n", pmadapter->dbg.last_cmd_index);
	PRINTM(MERROR, "last_cmd_id = ");
	for (i = 0; i < DBG_CMD_NUM; i++)
		PRINTM(MERROR, "0x%x ", pmadapter->dbg.last_cmd_id[i]);
	PRINTM(MERROR, "\n");
	PRINTM(MERROR, "last_cmd_act = ");
	for (i = 0; i < DBG_CMD_NUM; i++)
		PRINTM(MERROR, "0x%x ", pmadapter->dbg.last_cmd_act[i]);
	PRINTM(MERROR, "\n");
	PRINTM(MERROR, "last_cmd_resp_index = %d\n",
	       pmadapter->dbg.last_cmd_resp_index);
	PRINTM(MERROR, "last_cmd_resp_id = ");
	for (i = 0; i < DBG_CMD_NUM; i++)
		PRINTM(MERROR, "0x%x ", pmadapter->dbg.last_cmd_resp_id[i]);
	PRINTM(MERROR, "\n");
	PRINTM(MERROR, "last_event_index = %d\n",
	       pmadapter->dbg.last_event_index);
	PRINTM(MERROR, "last_event = ");
	for (i = 0; i < DBG_CMD_NUM; i++)
		PRINTM(MERROR, "0x%x ", pmadapter->dbg.last_event[i]);
	PRINTM(MERROR, "\n");

	PRINTM(MERROR, "num_data_h2c_failure = %d\n",
	       pmadapter->dbg.num_tx_host_to_card_failure);
	PRINTM(MERROR, "num_cmd_h2c_failure = %d\n",
	       pmadapter->dbg.num_cmd_host_to_card_failure);
#ifdef SDIO
	if (IS_SD(pmadapter->card_type)) {
		PRINTM(MERROR, "num_data_c2h_failure = %d\n",
		       pmadapter->dbg.num_rx_card_to_host_failure);
		PRINTM(MERROR, "num_cmdevt_c2h_failure = %d\n",
		       pmadapter->dbg.num_cmdevt_card_to_host_failure);
		PRINTM(MERROR, "num_int_read_failure = %d\n",
		       pmadapter->dbg.num_int_read_failure);
		PRINTM(MERROR, "last_int_status = %d\n",
		       pmadapter->dbg.last_int_status);
	}
#endif
	PRINTM(MERROR, "num_alloc_buffer_failure = %d\n",
	       pmadapter->dbg.num_alloc_buffer_failure);
	PRINTM(MERROR, "num_pkt_dropped = %d\n",
	       pmadapter->dbg.num_pkt_dropped);
	PRINTM(MERROR, "num_no_cmd_node = %d\n",
	       pmadapter->dbg.num_no_cmd_node);
	PRINTM(MERROR, "num_event_deauth = %d\n",
	       pmadapter->dbg.num_event_deauth);
	PRINTM(MERROR, "num_event_disassoc = %d\n",
	       pmadapter->dbg.num_event_disassoc);
	PRINTM(MERROR, "num_event_link_lost = %d\n",
	       pmadapter->dbg.num_event_link_lost);
	PRINTM(MERROR, "num_cmd_deauth = %d\n", pmadapter->dbg.num_cmd_deauth);
	PRINTM(MERROR, "num_cmd_assoc_success = %d\n",
	       pmadapter->dbg.num_cmd_assoc_success);
	PRINTM(MERROR, "num_cmd_assoc_failure = %d\n",
	       pmadapter->dbg.num_cmd_assoc_failure);
	PRINTM(MERROR, "num_cons_assoc_failure = %d\n",
	       pmadapter->dbg.num_cons_assoc_failure);
	PRINTM(MERROR, "cmd_resp_received=%d\n", pmadapter->cmd_resp_received);
	PRINTM(MERROR, "event_received=%d\n", pmadapter->event_received);

	PRINTM(MERROR, "max_tx_buf_size=%d\n", pmadapter->max_tx_buf_size);
	PRINTM(MERROR, "tx_buf_size=%d\n", pmadapter->tx_buf_size);
	PRINTM(MERROR, "curr_tx_buf_size=%d\n", pmadapter->curr_tx_buf_size);

	PRINTM(MERROR, "data_sent=%d cmd_sent=%d\n", pmadapter->data_sent,
	       pmadapter->cmd_sent);

	PRINTM(MERROR, "ps_mode=%d ps_state=%d\n", pmadapter->ps_mode,
	       pmadapter->ps_state);
	PRINTM(MERROR, "wakeup_dev_req=%d wakeup_tries=%d wakeup_timeout=%d\n",
	       pmadapter->pm_wakeup_card_req, pmadapter->pm_wakeup_fw_try,
	       pmadapter->pm_wakeup_timeout);
	PRINTM(MERROR, "hs_configured=%d hs_activated=%d\n",
	       pmadapter->is_hs_configured, pmadapter->hs_activated);
	PRINTM(MERROR, "pps_uapsd_mode=%d sleep_pd=%d\n",
	       pmadapter->pps_uapsd_mode, pmadapter->sleep_period.period);
	PRINTM(MERROR, "tx_lock_flag = %d\n", pmadapter->tx_lock_flag);
	PRINTM(MERROR, "scan_processing = %d\n", pmadapter->scan_processing);
	PRINTM(MERROR, "bypass_pkt_count=%d\n", pmadapter->bypass_pkt_count);
#ifdef SDIO
	if (IS_SD(pmadapter->card_type)) {
		mp_aggr_pkt_limit = pmadapter->pcard_sd->mp_aggr_pkt_limit;
		PRINTM(MERROR, "mp_rd_bitmap=0x%x curr_rd_port=0x%x\n",
		       pmadapter->pcard_sd->mp_rd_bitmap,
		       pmadapter->pcard_sd->curr_rd_port);
		PRINTM(MERROR, "mp_wr_bitmap=0x%x curr_wr_port=0x%x\n",
		       pmadapter->pcard_sd->mp_wr_bitmap,
		       pmadapter->pcard_sd->curr_wr_port);
		PRINTM(MMSG, "mp_data_port_mask = 0x%x\n",
		       pmadapter->pcard_sd->mp_data_port_mask);

		PRINTM(MERROR,
		       "last_recv_rd_bitmap=0x%x mp_invalid_update=%d\n",
		       pmadapter->pcard_sd->last_recv_rd_bitmap,
		       pmadapter->pcard_sd->mp_invalid_update);
		PRINTM(MERROR, "last_recv_wr_bitmap=0x%x last_mp_index=%d\n",
		       pmadapter->pcard_sd->last_recv_wr_bitmap,
		       pmadapter->pcard_sd->last_mp_index);
		for (i = 0; i < SDIO_MP_DBG_NUM; i++) {
			PRINTM(MERROR,
			       "mp_wr_bitmap: 0x%x mp_wr_ports=0x%x len=%d curr_wr_port=0x%x\n",
			       pmadapter->pcard_sd->last_mp_wr_bitmap[i],
			       pmadapter->pcard_sd->last_mp_wr_ports[i],
			       pmadapter->pcard_sd->last_mp_wr_len[i],
			       pmadapter->pcard_sd->last_curr_wr_port[i]);
			for (j = 0; j < mp_aggr_pkt_limit; j++) {
				PRINTM(MERROR, "0x%02x ",
				       pmadapter->pcard_sd->last_mp_wr_info
				       [i * mp_aggr_pkt_limit + j]);
			}
			PRINTM(MERROR, "\n");
		}
	}
#endif
#ifdef PCIE
	if (IS_PCIE(pmadapter->card_type)) {
		PRINTM(MERROR, "txbd_rdptr=0x%x txbd_wrptr=0x%x\n",
		       pmadapter->pcard_pcie->txbd_rdptr,
		       pmadapter->pcard_pcie->txbd_wrptr);
		PRINTM(MERROR, "rxbd_rdptr=0x%x rxbd_wrptr=0x%x\n",
		       pmadapter->pcard_pcie->rxbd_rdptr,
		       pmadapter->pcard_pcie->rxbd_wrptr);
		PRINTM(MERROR, "evtbd_rdptr=0x%x evt_wrptr=0x%x\n",
		       pmadapter->pcard_pcie->evtbd_rdptr,
		       pmadapter->pcard_pcie->evtbd_wrptr);
		PRINTM(MERROR, "last_wr_index:%d\n",
		       pmadapter->pcard_pcie->txbd_wrptr &
		       (pmadapter->pcard_pcie->txrx_bd_size - 1));
		PRINTM(MERROR, " txrx_bd_size = %d\n",
		       pmadapter->pcard_pcie->txrx_bd_size);
		PRINTM(MERROR, "Tx pkt size:\n");
		for (i = 0; i < pmadapter->pcard_pcie->txrx_bd_size; i++) {
			PRINTM(MERROR, "%04d ",
			       pmadapter->pcard_pcie->last_tx_pkt_size[i]);
			if (((i + 1) % 16) == 0)
				PRINTM(MERROR, "\n");
		}
	}
#endif
	for (i = 0; i < pmadapter->priv_num; ++i) {
		if (pmadapter->priv[i])
			wlan_dump_ralist(pmadapter->priv[i]);
	}
	if (reason != REASON_CODE_CMD_TIMEOUT) {
		if ((pmadapter->dbg.num_no_cmd_node >= 5)
		    || (pmadapter->pm_wakeup_card_req &&
			pmadapter->pm_wakeup_fw_try)
		    || (reason == REASON_CODE_EXT_SCAN_TIMEOUT)
			) {
			if (pmpriv)
				wlan_recv_event(pmpriv,
						MLAN_EVENT_ID_DRV_DBG_DUMP,
						MNULL);
			else {
				pmpriv = wlan_get_priv(pmadapter,
						       MLAN_BSS_ROLE_ANY);
				if (pmpriv)
					wlan_recv_event(pmpriv,
							MLAN_EVENT_ID_DRV_DBG_DUMP,
							MNULL);
			}
		}
	}
	PRINTM(MERROR, "-------- Dump info End---------\n", reason);
	LEAVE();
	return;
}

/**
 *  @brief This function convert a given character to hex
 *
 *  @param chr        Character to be converted
 *
 *  @return           The converted hex if chr is a valid hex, else 0
 */
static t_u32
wlan_hexval(t_u8 chr)
{
	if (chr >= '0' && chr <= '9')
		return chr - '0';
	if (chr >= 'A' && chr <= 'F')
		return chr - 'A' + 10;
	if (chr >= 'a' && chr <= 'f')
		return chr - 'a' + 10;

	return 0;
}

/**
 *  @brief This function convert a given string to hex
 *
 *  @param a            A pointer to string to be converted
 *
 *  @return             The converted hex value if param a is a valid hex, else
 * 0
 */
static int
wlan_atox(t_u8 *a)
{
	int i = 0;

	ENTER();

	while (wlan_isxdigit(*a))
		i = i * 16 + wlan_hexval(*a++);

	LEAVE();
	return i;
}

/**
 *  @brief This function parse cal data from ASCII to hex
 *
 *  @param src          A pointer to source data
 *  @param len          Source data length
 *  @param dst          A pointer to a buf to store the parsed data
 *
 *  @return             The parsed hex data length
 */
static t_u32
wlan_parse_cal_cfg(t_u8 *src, t_size len, t_u8 *dst)
{
	t_u8 *ptr;
	t_u8 *dptr;

	ENTER();
	ptr = src;
	dptr = dst;

	while (ptr - src < len) {
		if (*ptr && (wlan_isspace(*ptr) || *ptr == '\t')) {
			ptr++;
			continue;
		}

		if (wlan_isxdigit(*ptr)) {
			*dptr++ = wlan_atox(ptr);
			ptr += 2;
		} else {
			ptr++;
		}
	}
	LEAVE();
	return dptr - dst;
}

/**
 *  @brief This function finds first occurrence of a char in a string
 *
 *  @param s            A pointer to the string to be searched
 *  @param c            The character to search for
 *
 *  @return             Location of the first occurrence of the char
 *                      if found, else NULL
 */
static t_u8 *
wlan_strchr(t_u8 *s, int c)
{
	t_u8 *pos = s;
	while (*pos != '\0') {
		if (*pos == (t_u8)c)
			return pos;
		pos++;
	}
	return MNULL;
}

#define CFG_TYPE_HOSTCMD 0
#define CFG_TYPE_DPDFILE 1

/**
 *    @brief WOAL parse ASCII format raw data to hex format
 *
 *    @param pmpriv       MOAL handle
 *    @param cfg_type     Conf file type
 *    @param data         Source data
 *    @param size         data length
 *    @return             MLAN_STATUS_SUCCESS--success, otherwise--fail
 */
static t_u32
wlan_process_hostcmd_cfg(pmlan_private pmpriv,
			 t_u16 cfg_type, t_u8 *data, t_size size)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	t_u8 *pos = data;
	t_u8 *intf_s, *intf_e;
	t_u8 *buf = MNULL;
	t_u8 *ptr = MNULL;
	t_u32 cmd_len = 0;
	t_u8 start_raw = MFALSE;
	mlan_ds_misc_cmd *hostcmd;
	HostCmd_DS_GEN *pcmd = MNULL;
	HostCmd_DS_802_11_CFG_DATA *pcfg_cmd = MNULL;
	mlan_adapter *pmadapter = MNULL;
	mlan_callbacks *pcb = MNULL;
	t_u8 hostcmd_flag = MFALSE;

	ENTER();
	if (!pmpriv) {
		PRINTM(MERROR, "pmpriv is NULL\n");
		LEAVE();
		return MLAN_STATUS_FAILURE;
	}
	pmadapter = pmpriv->adapter;
	pcb = (mlan_callbacks *)&pmadapter->callbacks;
	ret = pcb->moal_malloc(pmadapter->pmoal_handle,
			       sizeof(mlan_ds_misc_cmd), MLAN_MEM_DEF,
			       (t_u8 **)&hostcmd);
	if (ret || !hostcmd) {
		PRINTM(MERROR, "Could not allocate buffer space!\n");
		LEAVE();
		return ret;
	}
	buf = hostcmd->cmd;
	ptr = buf;
	while ((pos - data) < size) {
		while (*pos == ' ' || *pos == '\t')
			pos++;
		if (*pos == '#') {	/* Line comment */
			while (*pos != '\n')
				pos++;
			pos++;
		}
		if ((*pos == '\r' && *(pos + 1) == '\n') || *pos == '\n' ||
		    *pos == '\0') {
			pos++;
			continue;	/* Needn't process this line */
		}

		if (*pos == '}') {
			if (cfg_type == CFG_TYPE_DPDFILE && pcmd) {
				/* Fill command head for DPD RAW data conf */
				hostcmd->len = ptr - buf;
				pcmd->command =
					wlan_cpu_to_le16(HostCmd_CMD_CFG_DATA);
				pcmd->size = wlan_cpu_to_le16(hostcmd->len);
				pcfg_cmd = (HostCmd_DS_802_11_CFG_DATA
					    *)((t_u8 *)pcmd + S_DS_GEN);
				pcfg_cmd->action =
					wlan_cpu_to_le16(HostCmd_ACT_GEN_SET);
				pcfg_cmd->type = wlan_cpu_to_le16(OID_TYPE_DPD);
				pcfg_cmd->data_len =
					wlan_cpu_to_le16(hostcmd->len -
							 S_DS_GEN -
							 sizeof
							 (HostCmd_DS_802_11_CFG_DATA));
				pcmd = MNULL;
				pcfg_cmd = MNULL;
			} else {
				/* For hostcmd data conf */
				cmd_len = *((t_u16 *)(buf + sizeof(t_u16)));
				hostcmd->len = cmd_len;
			}
			ret = wlan_prepare_cmd(pmpriv, 0, 0, 0, MNULL,
					       (t_void *)hostcmd);
			memset(pmadapter, buf, 0, MRVDRV_SIZE_OF_CMD_BUFFER);
			ptr = buf;
			start_raw = MFALSE;
			pos++;
			continue;
		}

		if (start_raw == MFALSE) {
			intf_s = wlan_strchr(pos, '=');
			if (intf_s) {
				if (*(intf_s + 1) == '=')
					hostcmd_flag = MTRUE;
				intf_e = wlan_strchr(intf_s, '{');
			} else
				intf_e = MNULL;

			if (intf_s && intf_e) {
				start_raw = MTRUE;
				pos = intf_e + 1;
				/* Reserve command head for DPD RAW data conf */
				if (cfg_type == CFG_TYPE_DPDFILE &&
				    !hostcmd_flag) {
					pcmd = (HostCmd_DS_GEN *)ptr;
					ptr += S_DS_GEN +
						sizeof
						(HostCmd_DS_802_11_CFG_DATA);
				}
				continue;
			}
		}

		if (start_raw) {
			/* Raw data block exists */
			while (*pos != '\n') {
				if ((*pos <= 'f' && *pos >= 'a') ||
				    (*pos <= 'F' && *pos >= 'A') ||
				    (*pos <= '9' && *pos >= '0')) {
					*ptr++ = wlan_atox(pos);
					pos += 2;
				} else
					pos++;
			}
		}
	}
	pcb->moal_mfree(pmadapter->pmoal_handle, (t_u8 *)hostcmd);
	LEAVE();
	return ret;
}

/**
 *  @brief This function initializes the command node.
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param pcmd_node    A pointer to cmd_ctrl_node structure
 *  @param cmd_no       cmd id
 *  @param pioctl_buf   A pointer to MLAN IOCTL Request buffer
 *  @param pdata_buf    A pointer to information buffer
 *
 *  @return             N/A
 */
static void
wlan_init_cmd_node(pmlan_private pmpriv,
		   cmd_ctrl_node *pcmd_node, t_u32 cmd_no,
		   t_void *pioctl_buf, t_void *pdata_buf)
{
	mlan_adapter *pmadapter = pmpriv->adapter;

	ENTER();

	if (pcmd_node == MNULL) {
		LEAVE();
		return;
	}
	pcmd_node->priv = pmpriv;
	pcmd_node->cmd_no = cmd_no;
	pcmd_node->pioctl_buf = pioctl_buf;
	pcmd_node->pdata_buf = pdata_buf;

#ifdef USB
	if (IS_USB(pmadapter->card_type)) {
		pcmd_node->cmdbuf =
			wlan_alloc_mlan_buffer(pmadapter,
					       MRVDRV_SIZE_OF_CMD_BUFFER, 0,
					       MOAL_MALLOC_BUFFER);
		if (!pcmd_node->cmdbuf) {
			PRINTM(MERROR, "Failed to allocate cmd_buffer\n");
			LEAVE();
			return;
		}
	}
#endif /* USB */
#if defined(SDIO) || defined(PCIE)
	if (!IS_USB(pmadapter->card_type))
		pcmd_node->cmdbuf = pcmd_node->pmbuf;
#endif

	/* Make sure head_ptr for cmd buf is Align */
	pcmd_node->cmdbuf->data_offset = 0;
	memset(pmadapter, pcmd_node->cmdbuf->pbuf, 0,
	       MRVDRV_SIZE_OF_CMD_BUFFER);

	/* Prepare mlan_buffer for command sending */
	pcmd_node->cmdbuf->buf_type = MLAN_BUF_TYPE_CMD;
#ifdef USB
	if (IS_USB(pmadapter->card_type))
		pcmd_node->cmdbuf->data_offset += MLAN_TYPE_LEN;
#endif
#if defined(SDIO) || defined(PCIE)
	if (!IS_USB(pmadapter->card_type))
		pcmd_node->cmdbuf->data_offset +=
			pmadapter->ops.intf_header_len;
#endif

	LEAVE();
}

/**
 *  @brief This function gets a free command node if available in
 *              command free queue.
 *
 *  @param pmadapter        A pointer to mlan_adapter structure
 *
 *  @return cmd_ctrl_node   A pointer to cmd_ctrl_node structure or MNULL
 */
static cmd_ctrl_node *
wlan_get_cmd_node(mlan_adapter *pmadapter)
{
	cmd_ctrl_node *pcmd_node;

	ENTER();

	if (pmadapter == MNULL) {
		LEAVE();
		return MNULL;
	}
	wlan_request_cmd_lock(pmadapter);
	if (util_peek_list(pmadapter->pmoal_handle, &pmadapter->cmd_free_q,
			   MNULL, MNULL)) {
		pcmd_node =
			(cmd_ctrl_node *)util_dequeue_list(pmadapter->
							   pmoal_handle,
							   &pmadapter->
							   cmd_free_q, MNULL,
							   MNULL);
	} else {
		PRINTM(MERROR,
		       "GET_CMD_NODE: cmd_ctrl_node is not available\n");
		pcmd_node = MNULL;
	}
	wlan_release_cmd_lock(pmadapter);
	LEAVE();
	return pcmd_node;
}

/**
 *  @brief This function cleans command node.
 *
 *  @param pmadapter    A pointer to mlan_adapter structure
 *  @param pcmd_node    A pointer to cmd_ctrl_node structure
 *
 *  @return             N/A
 */
static t_void
wlan_clean_cmd_node(pmlan_adapter pmadapter, cmd_ctrl_node *pcmd_node)
{
	ENTER();

	if (pcmd_node == MNULL) {
		LEAVE();
		return;
	}
	pcmd_node->cmd_no = 0;
	pcmd_node->cmd_flag = 0;
	pcmd_node->pioctl_buf = MNULL;
	pcmd_node->pdata_buf = MNULL;

#ifdef USB
	if (IS_USB(pmadapter->card_type) && pcmd_node->cmdbuf) {
		wlan_free_mlan_buffer(pmadapter, pcmd_node->cmdbuf);
		pcmd_node->cmdbuf = MNULL;
	}
#endif

	if (pcmd_node->respbuf) {
		pmadapter->ops.cmdrsp_complete(pmadapter, pcmd_node->respbuf,
					       MLAN_STATUS_SUCCESS);
		pcmd_node->respbuf = MNULL;
	}

	LEAVE();
	return;
}

#ifdef STA_SUPPORT
/**
 *  @brief This function will return the pointer to the first entry in
 *          pending cmd which is scan command
 *
 *  @param pmadapter    A pointer to mlan_adapter
 *
 *  @return             A pointer to first entry match pioctl_req
 */
static cmd_ctrl_node *
wlan_get_pending_scan_cmd(pmlan_adapter pmadapter)
{
	cmd_ctrl_node *pcmd_node = MNULL;

	ENTER();

	pcmd_node = (cmd_ctrl_node *)util_peek_list(pmadapter->pmoal_handle,
						    &pmadapter->cmd_pending_q,
						    MNULL, MNULL);
	if (!pcmd_node) {
		LEAVE();
		return MNULL;
	}
	while (pcmd_node != (cmd_ctrl_node *)&pmadapter->cmd_pending_q) {
		if (pcmd_node->cmd_flag & CMD_F_SCAN) {
			LEAVE();
			return pcmd_node;
		}
		pcmd_node = pcmd_node->pnext;
	}
	LEAVE();
	return MNULL;
}
#endif

/**
 *  @brief This function will return the pointer to the first entry in
 *          pending cmd which matches the given pioctl_req
 *
 *  @param pmadapter    A pointer to mlan_adapter
 *  @param pioctl_req   A pointer to mlan_ioctl_req buf
 *
 *  @return             A pointer to first entry match pioctl_req
 */
static cmd_ctrl_node *
wlan_get_pending_ioctl_cmd(pmlan_adapter pmadapter, pmlan_ioctl_req pioctl_req)
{
	cmd_ctrl_node *pcmd_node = MNULL;

	ENTER();

	pcmd_node = (cmd_ctrl_node *)util_peek_list(pmadapter->pmoal_handle,
						    &pmadapter->cmd_pending_q,
						    MNULL, MNULL);
	if (!pcmd_node) {
		LEAVE();
		return MNULL;
	}
	while (pcmd_node != (cmd_ctrl_node *)&pmadapter->cmd_pending_q) {
		if (pcmd_node->pioctl_buf &&
		    (pcmd_node->pioctl_buf == pioctl_req)) {
			LEAVE();
			return pcmd_node;
		}
		pcmd_node = pcmd_node->pnext;
	}
	LEAVE();
	return MNULL;
}

/**
 *  @brief This function will return the pointer to the first entry in
 *          pending cmd which matches the given bss_index
 *
 *  @param pmadapter    A pointer to mlan_adapter
 *  @param bss_index    bss_index
 *
 *  @return             A pointer to first entry match pioctl_req
 */
static cmd_ctrl_node *
wlan_get_bss_pending_ioctl_cmd(pmlan_adapter pmadapter, t_u32 bss_index)
{
	cmd_ctrl_node *pcmd_node = MNULL;
	mlan_ioctl_req *pioctl_buf = MNULL;
	ENTER();

	pcmd_node = (cmd_ctrl_node *)util_peek_list(pmadapter->pmoal_handle,
						    &pmadapter->cmd_pending_q,
						    MNULL, MNULL);
	if (!pcmd_node) {
		LEAVE();
		return MNULL;
	}
	while (pcmd_node != (cmd_ctrl_node *)&pmadapter->cmd_pending_q) {
		if (pcmd_node->pioctl_buf) {
			pioctl_buf = (mlan_ioctl_req *)pcmd_node->pioctl_buf;
			if (pioctl_buf->bss_index == bss_index) {
				LEAVE();
				return pcmd_node;
			}
		}
		pcmd_node = pcmd_node->pnext;
	}
	LEAVE();
	return MNULL;
}

/**
 *  @brief This function handles the command response of host_cmd
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param resp         A pointer to HostCmd_DS_COMMAND
 *  @param pioctl_buf   A pointer to mlan_ioctl_req structure
 *
 *  @return        MLAN_STATUS_SUCCESS
 */
static mlan_status
wlan_ret_host_cmd(pmlan_private pmpriv,
		  HostCmd_DS_COMMAND *resp, mlan_ioctl_req *pioctl_buf)
{
	mlan_ds_misc_cfg *misc;
	t_u16 size = wlan_le16_to_cpu(resp->size);

	ENTER();

	PRINTM(MINFO, "host command response size = %d\n", size);
	size = MIN(size, MRVDRV_SIZE_OF_CMD_BUFFER);
	if (pioctl_buf) {
		misc = (mlan_ds_misc_cfg *)pioctl_buf->pbuf;
		misc->param.hostcmd.len = size;
		memcpy_ext(pmpriv->adapter, misc->param.hostcmd.cmd,
			   (void *)resp, size, MRVDRV_SIZE_OF_CMD_BUFFER);
	}

	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function sends host command to firmware.
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param cmd          A pointer to HostCmd_DS_COMMAND structure
 *  @param pdata_buf    A pointer to data buffer
 *  @param cmd_no       A pointer to cmd_no
 *  @return             MLAN_STATUS_SUCCESS
 */
static mlan_status
wlan_cmd_host_cmd(pmlan_private pmpriv,
		  HostCmd_DS_COMMAND *cmd, t_void *pdata_buf, t_u16 *cmd_no)
{
	mlan_ds_misc_cmd *pcmd_ptr = (mlan_ds_misc_cmd *)pdata_buf;

	ENTER();

	/* Copy the HOST command to command buffer */
	memcpy_ext(pmpriv->adapter, (void *)cmd, pcmd_ptr->cmd, pcmd_ptr->len,
		   MRVDRV_SIZE_OF_CMD_BUFFER);
	*cmd_no = wlan_le16_to_cpu(cmd->command);
	PRINTM(MCMND, "Prepare Host command: 0x%x size = %d\n", *cmd_no,
	       pcmd_ptr->len);
	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function get the cmd timeout value
 *
 *  @param cmd_id     cmd id
 *
 *  @return           timeout value for this command
 */
static t_u16
wlan_get_cmd_timeout(t_u16 cmd_id)
{
	t_u16 timeout;
	ENTER();
	switch (cmd_id) {
	case HostCmd_CMD_802_11_SCAN:
	case HostCmd_CMD_802_11_SCAN_EXT:
		timeout = MRVDRV_TIMER_10S * 2;
		break;
	case HostCmd_CMD_FUNC_INIT:
	case HostCmd_CMD_FUNC_SHUTDOWN:
	case HostCmd_CMD_802_11_ASSOCIATE:
	case HostCmd_CMD_802_11_DEAUTHENTICATE:
	case HostCmd_CMD_802_11_DISASSOCIATE:
	case HostCmd_CMD_802_11_AD_HOC_START:
	case HostCmd_CMD_802_11_AD_HOC_JOIN:
	case HostCmd_CMD_802_11_AD_HOC_STOP:
	case HostCmd_CMD_11N_ADDBA_REQ:
	case HostCmd_CMD_11N_ADDBA_RSP:
	case HostCmd_CMD_11N_DELBA:
	case HostCmd_CMD_802_11_REMAIN_ON_CHANNEL:
	case HostCmd_CMD_TDLS_CONFIG:
	case HostCmd_CMD_TDLS_OPERATION:
	case HostCmd_CMD_SUPPLICANT_PMK:
	case HostCmd_CMD_SUPPLICANT_PROFILE:
	case HostCmd_CMD_SOFT_RESET:
#ifdef UAP_SUPPORT
	case HOST_CMD_APCMD_SYS_RESET:
	case HOST_CMD_APCMD_BSS_START:
	case HOST_CMD_APCMD_BSS_STOP:
	case HOST_CMD_APCMD_STA_DEAUTH:
#endif
	case HostCMD_APCMD_ACS_SCAN:
		timeout = MRVDRV_TIMER_5S;
		break;
	default:
#ifdef IMX_SUPPORT
		/*
		 * During the roaming test and the 5AP connection test, cmd timeout are observed
		 * for commands like 0x5e, 0x16, 0xd1. Observed that response has come just after
		 * default timeout of 2 seconds for these commands. This random timeout is not
		 * observed when the default timeout is increased to 5 seconds
		 * (As an work around, Increase the default timeout to 5 seconds.
		 * Need to further debug exact reason for delay in cmd responses)
		 *
		 */
		timeout = MRVDRV_TIMER_1S * 5;
#else
		timeout = MRVDRV_TIMER_1S * 5;
#endif
		break;
	}
	LEAVE();
	return timeout;
}

/**
 *  @brief This function downloads a command to firmware.
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param pcmd_node    A pointer to cmd_ctrl_node structure
 *
 *  @return             MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
static mlan_status
wlan_dnld_cmd_to_fw(mlan_private *pmpriv, cmd_ctrl_node *pcmd_node)
{
	mlan_adapter *pmadapter = pmpriv->adapter;
	mlan_callbacks *pcb = (mlan_callbacks *)&pmadapter->callbacks;
	mlan_status ret = MLAN_STATUS_SUCCESS;
	HostCmd_DS_COMMAND *pcmd;
	mlan_ioctl_req *pioctl_buf = MNULL;
	t_u16 cmd_code;
	t_u16 cmd_size;
	t_u32 age_ts_usec;
#ifdef USB
	t_u32 tmp;
#endif
#ifdef DEBUG_LEVEL1
	t_u32 sec = 0, usec = 0;
#endif
	t_u16 timeout = 0;

	ENTER();

	if (pcmd_node)
		if (pcmd_node->pioctl_buf != MNULL)
			pioctl_buf = (mlan_ioctl_req *)pcmd_node->pioctl_buf;
	if (!pmadapter || !pcmd_node) {
		if (pioctl_buf)
			pioctl_buf->status_code = MLAN_ERROR_CMD_DNLD_FAIL;
		ret = MLAN_STATUS_FAILURE;
		goto done;
	}

	pcmd = (HostCmd_DS_COMMAND *)(pcmd_node->cmdbuf->pbuf +
				      pcmd_node->cmdbuf->data_offset);

	/* Sanity test */
	if (pcmd == MNULL || pcmd->size == 0) {
		PRINTM(MERROR,
		       "DNLD_CMD: pcmd is null or command size is zero, "
		       "Not sending\n");
		if (pioctl_buf)
			pioctl_buf->status_code = MLAN_ERROR_CMD_DNLD_FAIL;
		wlan_request_cmd_lock(pmadapter);
		wlan_insert_cmd_to_free_q(pmadapter, pcmd_node);
		wlan_release_cmd_lock(pmadapter);
		ret = MLAN_STATUS_FAILURE;
		goto done;
	}

	/* Set command sequence number */
	pmadapter->seq_num++;
	pcmd->seq_num =
		wlan_cpu_to_le16(HostCmd_SET_SEQ_NO_BSS_INFO
				 (pmadapter->seq_num, pcmd_node->priv->bss_num,
				  pcmd_node->priv->bss_type));
	cmd_code = wlan_le16_to_cpu(pcmd->command);
	pcmd_node->cmd_no = cmd_code;
	timeout = wlan_get_cmd_timeout(cmd_code);
	cmd_size = wlan_le16_to_cpu(pcmd->size);

	pcmd_node->cmdbuf->data_len = cmd_size;

	wlan_request_cmd_lock(pmadapter);
	pmadapter->curr_cmd = pcmd_node;
	wlan_release_cmd_lock(pmadapter);

	/* Save the last command id and action to debug log */
	pmadapter->dbg.last_cmd_index =
		(pmadapter->dbg.last_cmd_index + 1) % DBG_CMD_NUM;
	pmadapter->dbg.last_cmd_id[pmadapter->dbg.last_cmd_index] = cmd_code;
	pmadapter->dbg.last_cmd_act[pmadapter->dbg.last_cmd_index] =
		wlan_le16_to_cpu(*(t_u16 *)((t_u8 *)pcmd + S_DS_GEN));
	pmadapter->callbacks.moal_get_system_time(pmadapter->pmoal_handle,
						  &pmadapter->dnld_cmd_in_secs,
						  &age_ts_usec);

#ifdef USB
	if (IS_USB(pmadapter->card_type)) {
		/* Add extra header for USB */
		if (pcmd_node->cmdbuf->data_offset < MLAN_TYPE_LEN) {
			PRINTM(MERROR,
			       "DNLD_CMD: data_offset is too small=%d\n",
			       pcmd_node->cmdbuf->data_offset);
			if (pioctl_buf)
				pioctl_buf->status_code =
					MLAN_ERROR_CMD_DNLD_FAIL;

			wlan_request_cmd_lock(pmadapter);
			wlan_insert_cmd_to_free_q(pmadapter, pcmd_node);
			pmadapter->curr_cmd = MNULL;
			wlan_release_cmd_lock(pmadapter);
			if (pmadapter->dbg.last_cmd_index)
				pmadapter->dbg.last_cmd_index--;
			else
				pmadapter->dbg.last_cmd_index = DBG_CMD_NUM - 1;
			ret = MLAN_STATUS_FAILURE;
			goto done;
		}
		tmp = wlan_cpu_to_le32(MLAN_USB_TYPE_CMD);
		memcpy_ext(pmadapter, (t_u8 *)pcmd - MLAN_TYPE_LEN,
			   (t_u8 *)&tmp, MLAN_TYPE_LEN, MLAN_TYPE_LEN);
		pcmd_node->cmdbuf->data_offset -= MLAN_TYPE_LEN;
		pcmd_node->cmdbuf->data_len += MLAN_TYPE_LEN;
	}
#endif

	PRINTM_GET_SYS_TIME(MCMND, &sec, &usec);
	PRINTM_NETINTF(MCMND, pmpriv);
	PRINTM(MCMND,
	       "DNLD_CMD (%lu.%06lu): 0x%x, act 0x%x, len %d, seqno 0x%x timeout %d\n",
	       sec, usec, cmd_code,
	       wlan_le16_to_cpu(*(t_u16 *)((t_u8 *)pcmd + S_DS_GEN)), cmd_size,
	       wlan_le16_to_cpu(pcmd->seq_num), timeout);
	DBG_HEXDUMP(MCMD_D, "DNLD_CMD", (t_u8 *)pcmd, cmd_size);

#if defined(SDIO) || defined(PCIE)
	if (!IS_USB(pmadapter->card_type)) {
		pcmd_node->cmdbuf->data_offset -=
			pmadapter->ops.intf_header_len;
		pcmd_node->cmdbuf->data_len += pmadapter->ops.intf_header_len;
	}
#endif

	/* Send the command to lower layer */
	ret = pmadapter->ops.host_to_card(pmpriv, MLAN_TYPE_CMD,
					  pcmd_node->cmdbuf, MNULL);

#ifdef USB
	if (IS_USB(pmadapter->card_type) && (ret == MLAN_STATUS_PENDING))
		pcmd_node->cmdbuf = MNULL;
#endif

	if (ret == MLAN_STATUS_FAILURE) {
		PRINTM(MERROR, "DNLD_CMD: Host to Card Failed\n");
		if (pcmd_node->pioctl_buf) {
			pioctl_buf = (mlan_ioctl_req *)pcmd_node->pioctl_buf;
			pioctl_buf->status_code = MLAN_ERROR_CMD_DNLD_FAIL;
		}

		wlan_request_cmd_lock(pmadapter);
		wlan_insert_cmd_to_free_q(pmadapter, pmadapter->curr_cmd);
		pmadapter->curr_cmd = MNULL;
		wlan_release_cmd_lock(pmadapter);
		if (pmadapter->dbg.last_cmd_index)
			pmadapter->dbg.last_cmd_index--;
		else
			pmadapter->dbg.last_cmd_index = DBG_CMD_NUM - 1;

		pmadapter->dbg.num_cmd_host_to_card_failure++;
		wlan_dump_info(pmadapter, REASON_CODE_CMD_TO_CARD_FAILURE);
		ret = MLAN_STATUS_FAILURE;
		goto done;
	}

	/* Clear BSS_NO_BITS from HostCmd */
	cmd_code &= HostCmd_CMD_ID_MASK;

	/* For the command who has no command response, we should return here */
	if (cmd_code == HostCmd_CMD_FW_DUMP_EVENT
	    || cmd_code == HostCmd_CMD_SOFT_RESET) {
		if (pcmd_node->pioctl_buf) {
			PRINTM(MMSG,
			       "CMD(0x%x) has no cmd resp: free curr_cmd and do ioctl_complete\n",
			       cmd_code);
			pioctl_buf = (mlan_ioctl_req *)pcmd_node->pioctl_buf;
			wlan_request_cmd_lock(pmadapter);
			wlan_insert_cmd_to_free_q(pmadapter,
						  pmadapter->curr_cmd);
			pmadapter->curr_cmd = MNULL;
			wlan_release_cmd_lock(pmadapter);
		}
		goto done;
	}

	/* Setup the timer after transmit command */
	pcb->moal_start_timer(pmadapter->pmoal_handle,
			      pmadapter->pmlan_cmd_timer, MFALSE, timeout);

	pmadapter->cmd_timer_is_set = MTRUE;

	ret = MLAN_STATUS_SUCCESS;

done:
	LEAVE();
	return ret;
}

/**
 *  @brief This function sends sleep confirm command to firmware.
 *
 *  @param pmadapter  A pointer to mlan_adapter structure
 *
 *  @return           MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
static mlan_status
wlan_dnld_sleep_confirm_cmd(mlan_adapter *pmadapter)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	static t_u32 i;
#if defined(SDIO) || defined(PCIE)
	t_u16 cmd_len = 0;
#endif
	opt_sleep_confirm_buffer *sleep_cfm_buf =
		(opt_sleep_confirm_buffer *)(pmadapter->psleep_cfm->pbuf +
					     pmadapter->psleep_cfm->
					     data_offset);
	mlan_buffer *pmbuf = MNULL;
	mlan_private *pmpriv = MNULL;

	ENTER();

	pmpriv = wlan_get_priv(pmadapter, MLAN_BSS_ROLE_ANY);
	if (!pmpriv) {
		LEAVE();
		return MLAN_STATUS_FAILURE;
	}
#if defined(SDIO) || defined(PCIE)
	if (!IS_USB(pmadapter->card_type)) {
		cmd_len = sizeof(OPT_Confirm_Sleep);
		pmbuf = pmadapter->psleep_cfm;
	}
#endif
	pmadapter->seq_num++;
	sleep_cfm_buf->ps_cfm_sleep.seq_num =
		wlan_cpu_to_le16(HostCmd_SET_SEQ_NO_BSS_INFO
				 (pmadapter->seq_num, pmpriv->bss_num,
				  pmpriv->bss_type));
	DBG_HEXDUMP(MCMD_D, "SLEEP_CFM", &sleep_cfm_buf->ps_cfm_sleep,
		    sizeof(OPT_Confirm_Sleep));

	/* Send sleep confirm command to firmware */
#ifdef USB
	if (IS_USB(pmadapter->card_type)) {
		pmbuf = wlan_alloc_mlan_buffer(pmadapter,
					       sizeof(opt_sleep_confirm_buffer),
					       0, MOAL_MALLOC_BUFFER);

		if (!pmbuf) {
			PRINTM(MERROR,
			       "Failed to allocate sleep confirm buffers\n");
			LEAVE();
			return MLAN_STATUS_FAILURE;
		}
		pmbuf->buf_type = MLAN_BUF_TYPE_CMD;
		pmbuf->data_len = pmadapter->psleep_cfm->data_len;
		memcpy_ext(pmadapter, pmbuf->pbuf + pmbuf->data_offset,
			   pmadapter->psleep_cfm->pbuf +
			   pmadapter->psleep_cfm->data_offset,
			   pmadapter->psleep_cfm->data_len, pmbuf->data_len);
	}
#endif /* USB */

#if defined(SDIO) || defined(PCIE)
	if (!IS_USB(pmadapter->card_type))
		pmadapter->psleep_cfm->data_len =
			cmd_len + pmadapter->ops.intf_header_len;
#endif

	if (pmbuf)
		ret = pmadapter->ops.host_to_card(pmpriv, MLAN_TYPE_CMD, pmbuf,
						  MNULL);

#ifdef USB
	if (IS_USB(pmadapter->card_type) && (ret != MLAN_STATUS_PENDING))
		wlan_free_mlan_buffer(pmadapter, pmbuf);
#endif
	if (ret == MLAN_STATUS_FAILURE) {
		PRINTM(MERROR, "SLEEP_CFM: failed\n");
		pmadapter->dbg.num_cmd_sleep_cfm_host_to_card_failure++;
		goto done;
	} else {
		if (GET_BSS_ROLE(pmpriv) == MLAN_BSS_ROLE_UAP)
			pmadapter->ps_state = PS_STATE_SLEEP_CFM;
#ifdef STA_SUPPORT
		if (GET_BSS_ROLE(pmpriv) == MLAN_BSS_ROLE_STA) {
			if (!sleep_cfm_buf->ps_cfm_sleep.sleep_cfm.resp_ctrl) {
				/* Response is not needed for sleep confirm
				 * command */
				pmadapter->ps_state = PS_STATE_SLEEP;
			} else {
				pmadapter->ps_state = PS_STATE_SLEEP_CFM;
			}

			if (!sleep_cfm_buf->ps_cfm_sleep.sleep_cfm.resp_ctrl &&
			    (pmadapter->is_hs_configured &&
			     !pmadapter->sleep_period.period)) {
				pmadapter->pm_wakeup_card_req = MTRUE;
				wlan_host_sleep_activated_event(wlan_get_priv
								(pmadapter,
								 MLAN_BSS_ROLE_STA),
								MTRUE);
			}
		}
#endif /* STA_SUPPORT */

		PRINTM_NETINTF(MEVENT, pmpriv);
#define NUM_SC_PER_LINE 16
		if (++i % NUM_SC_PER_LINE == 0)
			PRINTM(MEVENT, "+\n");
		else
			PRINTM(MEVENT, "+");
	}

done:
	LEAVE();
	return ret;
}

/**
 *  @brief Fetch bitmap rate index
 *
 *  @param rate_scope	A pointer to MrvlRateScope_t
 *
 *  @return		bitmap rate index
 */
static t_u16
wlan_get_bitmap_index(MrvlRateScope_t *rate_scope)
{
	t_u16 index = 0;

	if (rate_scope != MNULL) {
		index += NELEMENTS(rate_scope->ht_mcs_rate_bitmap);
		index += NELEMENTS(rate_scope->vht_mcs_rate_bitmap);
	}

	return index;
}

/********************************************************
			Global Functions
********************************************************/

/**
 *  @brief Event handler
 *
 *  @param priv     A pointer to mlan_private structure
 *  @param event_id Event ID
 *  @param pmevent  Event buffer
 *
 *  @return         MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_recv_event(pmlan_private priv, mlan_event_id event_id, t_void *pmevent)
{
	pmlan_callbacks pcb = MNULL;

	ENTER();

	if (!priv) {
		LEAVE();
		return MLAN_STATUS_FAILURE;
	}
	pcb = &priv->adapter->callbacks;

	if (pmevent)
		/* The caller has provided the event. */
		pcb->moal_recv_event(priv->adapter->pmoal_handle,
				     (pmlan_event)pmevent);
	else {
		mlan_event mevent;

		memset(priv->adapter, &mevent, 0, sizeof(mlan_event));
		mevent.bss_index = priv->bss_index;
		mevent.event_id = event_id;
		mevent.event_len = 0;

		pcb->moal_recv_event(priv->adapter->pmoal_handle, &mevent);
	}

	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function allocates the command buffer and links
 *          it to command free queue.
 *
 *  @param pmadapter    A pointer to mlan_adapter structure
 *
 *  @return             MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
mlan_status
wlan_alloc_cmd_buffer(mlan_adapter *pmadapter)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	mlan_callbacks *pcb = (mlan_callbacks *)&pmadapter->callbacks;
	cmd_ctrl_node *pcmd_array = MNULL;
	t_u32 buf_size;
	t_u32 i;

	ENTER();

	/* Allocate and initialize cmd_ctrl_node */
	buf_size = sizeof(cmd_ctrl_node) * MRVDRV_NUM_OF_CMD_BUFFER;
	ret = pcb->moal_malloc(pmadapter->pmoal_handle, buf_size,
			       MLAN_MEM_DEF | MLAN_MEM_DMA,
			       (t_u8 **)&pcmd_array);
	if (ret != MLAN_STATUS_SUCCESS || !pcmd_array) {
		PRINTM(MERROR,
		       "ALLOC_CMD_BUF: Failed to allocate pcmd_array\n");
		ret = MLAN_STATUS_FAILURE;
		goto done;
	}

	pmadapter->cmd_pool = pcmd_array;
	memset(pmadapter, pmadapter->cmd_pool, 0, buf_size);

#if defined(PCIE) || defined(SDIO)
	if (!IS_USB(pmadapter->card_type)) {
		/* Allocate and initialize command buffers */
		for (i = 0; i < MRVDRV_NUM_OF_CMD_BUFFER; i++) {
			pcmd_array[i].pmbuf =
				wlan_alloc_mlan_buffer(pmadapter,
						       MRVDRV_SIZE_OF_CMD_BUFFER,
						       0, MOAL_MALLOC_BUFFER);
			if (!pcmd_array[i].pmbuf) {
				PRINTM(MERROR,
				       "ALLOC_CMD_BUF: Failed to allocate command buffer\n");
				ret = MLAN_STATUS_FAILURE;
				goto done;
			}
		}
	}
#endif
	wlan_request_cmd_lock(pmadapter);
	for (i = 0; i < MRVDRV_NUM_OF_CMD_BUFFER; i++)
		wlan_insert_cmd_to_free_q(pmadapter, &pcmd_array[i]);
	wlan_release_cmd_lock(pmadapter);
	ret = MLAN_STATUS_SUCCESS;
done:
	LEAVE();
	return ret;
}

/**
 *  @brief This function frees the command buffer.
 *
 *  @param pmadapter    A pointer to mlan_adapter structure
 *
 *  @return             MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_free_cmd_buffer(mlan_adapter *pmadapter)
{
	mlan_callbacks *pcb = (mlan_callbacks *)&pmadapter->callbacks;
	cmd_ctrl_node *pcmd_array;
	t_u32 i;

	ENTER();

	/* Need to check if cmd pool is allocated or not */
	if (pmadapter->cmd_pool == MNULL) {
		PRINTM(MINFO, "FREE_CMD_BUF: cmd_pool is Null\n");
		goto done;
	}

	pcmd_array = pmadapter->cmd_pool;

	/* Release shared memory buffers */
	for (i = 0; i < MRVDRV_NUM_OF_CMD_BUFFER; i++) {
#ifdef USB
		if (IS_USB(pmadapter->card_type) && pcmd_array[i].cmdbuf) {
			PRINTM(MINFO, "Free all the USB command buffer.\n");
			wlan_free_mlan_buffer(pmadapter, pcmd_array[i].cmdbuf);
			pcmd_array[i].cmdbuf = MNULL;
		}
#endif
#if defined(SDIO) || defined(PCIE)
		if (!IS_USB(pmadapter->card_type) && pcmd_array[i].pmbuf) {
			PRINTM(MINFO, "Free all the command buffer.\n");
			wlan_free_mlan_buffer(pmadapter, pcmd_array[i].pmbuf);
			pcmd_array[i].pmbuf = MNULL;
		}
#endif
		if (pcmd_array[i].respbuf) {
#ifdef USB
			if (IS_USB(pmadapter->card_type))
				pmadapter->callbacks.
					moal_recv_complete(pmadapter->
							   pmoal_handle,
							   pcmd_array[i].
							   respbuf,
							   pmadapter->rx_cmd_ep,
							   MLAN_STATUS_SUCCESS);
#endif
#if defined(SDIO) || defined(PCIE)
			if (!IS_USB(pmadapter->card_type))
				wlan_free_mlan_buffer(pmadapter,
						      pcmd_array[i].respbuf);
#endif
			pcmd_array[i].respbuf = MNULL;
		}
	}
	/* Release cmd_ctrl_node */
	if (pmadapter->cmd_pool) {
		PRINTM(MINFO, "Free command pool.\n");
		pcb->moal_mfree(pmadapter->pmoal_handle,
				(t_u8 *)pmadapter->cmd_pool);
		pmadapter->cmd_pool = MNULL;
	}

done:
	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function handles events generated by firmware
 *
 *  @param pmadapter    A pointer to mlan_adapter structure
 *
 *  @return             MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
mlan_status
wlan_process_event(pmlan_adapter pmadapter)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	pmlan_private priv = wlan_get_priv(pmadapter, MLAN_BSS_ROLE_ANY);
	pmlan_buffer pmbuf = pmadapter->pmlan_buffer_event;
	t_u32 eventcause = pmadapter->event_cause;
#ifdef DEBUG_LEVEL1
	t_u32 in_ts_sec = 0, in_ts_usec = 0;
#endif
	ENTER();

	/* Save the last event to debug log */
	pmadapter->dbg.last_event_index =
		(pmadapter->dbg.last_event_index + 1) % DBG_CMD_NUM;
	pmadapter->dbg.last_event[pmadapter->dbg.last_event_index] =
		(t_u16)eventcause;

	if ((eventcause & EVENT_ID_MASK) == EVENT_RADAR_DETECTED) {
		if (wlan_11h_dfs_event_preprocessing(pmadapter) ==
		    MLAN_STATUS_SUCCESS) {
			memcpy_ext(pmadapter, (t_u8 *)&eventcause,
				   pmbuf->pbuf + pmbuf->data_offset,
				   sizeof(eventcause), sizeof(eventcause));
		} else {
			priv = wlan_get_priv_by_id(pmadapter,
						   EVENT_GET_BSS_NUM
						   (eventcause),
						   EVENT_GET_BSS_TYPE
						   (eventcause));
			if (priv)
				PRINTM_NETINTF(MEVENT, priv);
			PRINTM(MERROR,
			       "Error processing DFS Event: 0x%x\n",
			       eventcause);
			goto done;
		}
	}
	/* Get BSS number and corresponding priv */
	priv = wlan_get_priv_by_id(pmadapter, EVENT_GET_BSS_NUM(eventcause),
				   EVENT_GET_BSS_TYPE(eventcause));
	if (!priv)
		priv = wlan_get_priv(pmadapter, MLAN_BSS_ROLE_ANY);
	if (!priv) {
		ret = MLAN_STATUS_FAILURE;
		goto done;
	}

	/* Clear BSS_NO_BITS from event */
	eventcause &= EVENT_ID_MASK;
	pmadapter->event_cause = eventcause;

	if (pmbuf) {
		pmbuf->bss_index = priv->bss_index;
		memcpy_ext(pmadapter, pmbuf->pbuf + pmbuf->data_offset,
			   (t_u8 *)&eventcause, sizeof(eventcause),
			   sizeof(eventcause));
	}

	if (eventcause != EVENT_PS_SLEEP && eventcause != EVENT_PS_AWAKE
	    && eventcause != EVENT_FW_DUMP_INFO) {
		PRINTM_GET_SYS_TIME(MEVENT, &in_ts_sec, &in_ts_usec);
		PRINTM_NETINTF(MEVENT, priv);
		PRINTM(MEVENT, "%lu.%06lu : Event: 0x%x\n", in_ts_sec,
		       in_ts_usec, eventcause);
	}

	ret = priv->ops.process_event(priv);
done:
	pmadapter->event_cause = 0;
	pmadapter->pmlan_buffer_event = MNULL;
	if (pmbuf)
		pmadapter->ops.event_complete(pmadapter, pmbuf,
					      MLAN_STATUS_SUCCESS);

	LEAVE();
	return ret;
}

/**
 *  @brief This function requests a lock on command queue.
 *
 *  @param pmadapter    A pointer to mlan_adapter structure
 *
 *  @return             N/A
 */
t_void
wlan_request_cmd_lock(mlan_adapter *pmadapter)
{
	mlan_callbacks *pcb = (mlan_callbacks *)&pmadapter->callbacks;

	ENTER();

	/* Call MOAL spin lock callback function */
	pcb->moal_spin_lock(pmadapter->pmoal_handle, pmadapter->pmlan_cmd_lock);

	LEAVE();
	return;
}

/**
 *  @brief This function releases a lock on command queue.
 *
 *  @param pmadapter    A pointer to mlan_adapter structure
 *
 *  @return             N/A
 */
t_void
wlan_release_cmd_lock(mlan_adapter *pmadapter)
{
	mlan_callbacks *pcb = (mlan_callbacks *)&pmadapter->callbacks;

	ENTER();

	/* Call MOAL spin unlock callback function */
	pcb->moal_spin_unlock(pmadapter->pmoal_handle,
			      pmadapter->pmlan_cmd_lock);

	LEAVE();
	return;
}

/**
 *  @brief This function prepare the command before sending to firmware.
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param cmd_no       Command number
 *  @param cmd_action   Command action: GET or SET
 *  @param cmd_oid      Cmd oid: treated as sub command
 *  @param pioctl_buf   A pointer to MLAN IOCTL Request buffer
 *  @param pdata_buf    A pointer to information buffer
 *
 *  @return             MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
mlan_status
wlan_prepare_cmd(mlan_private *pmpriv, t_u16 cmd_no,
		 t_u16 cmd_action, t_u32 cmd_oid,
		 t_void *pioctl_buf, t_void *pdata_buf)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	mlan_adapter *pmadapter = MNULL;
	cmd_ctrl_node *pcmd_node = MNULL;
	HostCmd_DS_COMMAND *cmd_ptr = MNULL;
	pmlan_ioctl_req pioctl_req = (mlan_ioctl_req *)pioctl_buf;

	ENTER();

	if (!pmpriv) {
		LEAVE();
		return MLAN_STATUS_FAILURE;
	}
	pmadapter = pmpriv->adapter;

	/* Sanity test */
	if (!pmadapter || pmadapter->surprise_removed) {
		PRINTM(MERROR, "PREP_CMD: Card is Removed\n");
		if (pioctl_req)
			pioctl_req->status_code = MLAN_ERROR_FW_NOT_READY;
		ret = MLAN_STATUS_FAILURE;
		goto done;
	}

	if (pmadapter->hw_status == WlanHardwareStatusReset) {
		if ((cmd_no != HostCmd_CMD_FUNC_INIT)
#ifdef PCIE
		    && (cmd_no != HostCmd_CMD_PCIE_HOST_BUF_DETAILS)
#endif
			) {
			PRINTM(MERROR, "PREP_CMD: FW is in reset state\n");
			if (pioctl_req)
				pioctl_req->status_code =
					MLAN_ERROR_FW_NOT_READY;
			ret = MLAN_STATUS_FAILURE;
			goto done;
		}
	}

	/* Get a new command node */
	pcmd_node = wlan_get_cmd_node(pmadapter);

	if (pcmd_node == MNULL) {
		PRINTM(MERROR, "PREP_CMD: No free cmd node\n");
		wlan_dump_info(pmadapter, REASON_CODE_NO_CMD_NODE);
		if (pioctl_req)
			pioctl_req->status_code = MLAN_ERROR_NO_MEM;
		ret = MLAN_STATUS_FAILURE;
		goto done;
	}
	/** reset num no cmd node */
	pmadapter->dbg.num_no_cmd_node = 0;

	/* Initialize the command node */
	wlan_init_cmd_node(pmpriv, pcmd_node, cmd_no, pioctl_buf, pdata_buf);

	if (pcmd_node->cmdbuf == MNULL) {
		PRINTM(MERROR, "PREP_CMD: No free cmd buf\n");
		if (pioctl_req)
			pioctl_req->status_code = MLAN_ERROR_NO_MEM;
		ret = MLAN_STATUS_FAILURE;
		goto done;
	}

	cmd_ptr = (HostCmd_DS_COMMAND *)(pcmd_node->cmdbuf->pbuf +
					 pcmd_node->cmdbuf->data_offset);
	cmd_ptr->command = cmd_no;
	cmd_ptr->result = 0;

	/* Prepare command */
	if (cmd_no)
		ret = pmpriv->ops.prepare_cmd(pmpriv, cmd_no, cmd_action,
					      cmd_oid, pioctl_buf, pdata_buf,
					      cmd_ptr);
	else {
		ret = wlan_cmd_host_cmd(pmpriv, cmd_ptr, pdata_buf, &cmd_no);
		pcmd_node->cmd_flag |= CMD_F_HOSTCMD;
	}

	/* Return error, since the command preparation failed */
	if (ret != MLAN_STATUS_SUCCESS) {
		PRINTM(MERROR, "PREP_CMD: Command 0x%x preparation failed\n",
		       cmd_no);
		pcmd_node->pioctl_buf = MNULL;
		if (pioctl_req)
			pioctl_req->status_code = MLAN_ERROR_CMD_DNLD_FAIL;
		wlan_request_cmd_lock(pmadapter);
		wlan_insert_cmd_to_free_q(pmadapter, pcmd_node);
		wlan_release_cmd_lock(pmadapter);
		ret = MLAN_STATUS_FAILURE;
		goto done;
	}

	wlan_request_cmd_lock(pmadapter);
	/* Send command */
#ifdef STA_SUPPORT
	if (cmd_no == HostCmd_CMD_802_11_SCAN
	    || cmd_no == HostCmd_CMD_802_11_SCAN_EXT) {
		if (cmd_no == HostCmd_CMD_802_11_SCAN_EXT &&
		    pmadapter->ext_scan && pmadapter->ext_scan_enh &&
		    pmadapter->ext_scan_type == EXT_SCAN_ENHANCE) {
			wlan_insert_cmd_to_pending_q(pmadapter, pcmd_node,
						     MTRUE);
		} else
			wlan_queue_scan_cmd(pmpriv, pcmd_node);
	} else {
#endif
		if ((cmd_no == HostCmd_CMD_802_11_HS_CFG_ENH) &&
		    (cmd_action == HostCmd_ACT_GEN_SET) &&
		    (pmadapter->hs_cfg.conditions == HOST_SLEEP_CFG_CANCEL))
			wlan_insert_cmd_to_pending_q(pmadapter, pcmd_node,
						     MFALSE);
		else
			wlan_queue_cmd(pmpriv, pcmd_node, cmd_no);
#ifdef STA_SUPPORT
	}
#endif
	wlan_release_cmd_lock(pmadapter);
done:
	LEAVE();
	return ret;
}

/**
 *  @brief This function inserts command node to cmd_free_q
 *              after cleaning it.
 *
 *  @param pmadapter    A pointer to mlan_adapter structure
 *  @param pcmd_node    A pointer to cmd_ctrl_node structure
 *
 *  @return             N/A
 */
t_void
wlan_insert_cmd_to_free_q(mlan_adapter *pmadapter, cmd_ctrl_node *pcmd_node)
{
	mlan_callbacks *pcb = (mlan_callbacks *)&pmadapter->callbacks;
	mlan_ioctl_req *pioctl_req = MNULL;
	ENTER();

	if (pcmd_node == MNULL)
		goto done;
	if (pcmd_node->pioctl_buf) {
		pioctl_req = (mlan_ioctl_req *)pcmd_node->pioctl_buf;
		if (pioctl_req->status_code != MLAN_ERROR_NO_ERROR)
			pcb->moal_ioctl_complete(pmadapter->pmoal_handle,
						 pioctl_req,
						 MLAN_STATUS_FAILURE);
		else
			pcb->moal_ioctl_complete(pmadapter->pmoal_handle,
						 pioctl_req,
						 MLAN_STATUS_SUCCESS);
	}
	/* Clean the node */
	wlan_clean_cmd_node(pmadapter, pcmd_node);

	/* Insert node into cmd_free_q */
	util_enqueue_list_tail(pmadapter->pmoal_handle, &pmadapter->cmd_free_q,
			       (pmlan_linked_list)pcmd_node, MNULL, MNULL);
done:
	LEAVE();
}

/**
 *  @brief This function queues the command to cmd list.
 *
 *  @param pmadapter    A pointer to mlan_adapter structure
 *  @param pcmd_node    A pointer to cmd_ctrl_node structure
 *  @param add_tail      Specify if the cmd needs to be queued in the header or
 * tail
 *
 *  @return             N/A
 */
t_void
wlan_insert_cmd_to_pending_q(mlan_adapter *pmadapter,
			     cmd_ctrl_node *pcmd_node, t_u32 add_tail)
{
	HostCmd_DS_COMMAND *pcmd = MNULL;
	t_u16 command;

	ENTER();

	if (pcmd_node == MNULL) {
		PRINTM(MERROR, "QUEUE_CMD: pcmd_node is MNULL\n");
		goto done;
	}

	pcmd = (HostCmd_DS_COMMAND *)(pcmd_node->cmdbuf->pbuf +
				      pcmd_node->cmdbuf->data_offset);

	command = wlan_le16_to_cpu(pcmd->command);

	/* Exit_PS command needs to be queued in the header always. */
	if (command == HostCmd_CMD_802_11_PS_MODE_ENH) {
		HostCmd_DS_802_11_PS_MODE_ENH *pm = &pcmd->params.psmode_enh;
		if (wlan_le16_to_cpu(pm->action) == DIS_AUTO_PS) {
			if (pmadapter->ps_state != PS_STATE_AWAKE)
				add_tail = MFALSE;
		}
	}

	if (add_tail) {
		util_enqueue_list_tail(pmadapter->pmoal_handle,
				       &pmadapter->cmd_pending_q,
				       (pmlan_linked_list)pcmd_node, MNULL,
				       MNULL);
	} else {
		util_enqueue_list_head(pmadapter->pmoal_handle,
				       &pmadapter->cmd_pending_q,
				       (pmlan_linked_list)pcmd_node, MNULL,
				       MNULL);
	}

	PRINTM_NETINTF(MCMND, pcmd_node->priv);
	PRINTM(MCMND, "QUEUE_CMD: cmd=0x%x is queued\n", command);

done:
	LEAVE();
	return;
}

/**
 *  @brief This function executes next command in command
 *      pending queue. It will put firmware back to PS mode
 *      if applicable.
 *
 *  @param pmadapter     A pointer to mlan_adapter structure
 *
 *  @return             MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
mlan_status
wlan_exec_next_cmd(mlan_adapter *pmadapter)
{
	mlan_private *priv = MNULL;
	cmd_ctrl_node *pcmd_node = MNULL;
	mlan_status ret = MLAN_STATUS_SUCCESS;
	HostCmd_DS_COMMAND *pcmd;

	ENTER();

	/* Sanity test */
	if (pmadapter == MNULL) {
		PRINTM(MERROR, "EXEC_NEXT_CMD: pmadapter is MNULL\n");
		ret = MLAN_STATUS_FAILURE;
		goto done;
	}
	/* Check if already in processing */
	if (pmadapter->curr_cmd) {
		PRINTM(MERROR,
		       "EXEC_NEXT_CMD: there is command in processing!\n");
		ret = MLAN_STATUS_FAILURE;
		goto done;
	}

	wlan_request_cmd_lock(pmadapter);
	/* Check if any command is pending */
	pcmd_node = (cmd_ctrl_node *)util_peek_list(pmadapter->pmoal_handle,
						    &pmadapter->cmd_pending_q,
						    MNULL, MNULL);

	if (pcmd_node) {
		pcmd = (HostCmd_DS_COMMAND *)(pcmd_node->cmdbuf->pbuf +
					      pcmd_node->cmdbuf->data_offset);
		priv = pcmd_node->priv;

		if (pmadapter->ps_state != PS_STATE_AWAKE) {
			PRINTM(MERROR,
			       "Cannot send command in sleep state, this should not happen\n");
			wlan_release_cmd_lock(pmadapter);
			goto done;
		}

		util_unlink_list(pmadapter->pmoal_handle,
				 &pmadapter->cmd_pending_q,
				 (pmlan_linked_list)pcmd_node, MNULL, MNULL);
		wlan_release_cmd_lock(pmadapter);
		ret = wlan_dnld_cmd_to_fw(priv, pcmd_node);
		priv = wlan_get_priv(pmadapter, MLAN_BSS_ROLE_ANY);
		/* Any command sent to the firmware when host is in sleep mode,
		 * should de-configure host sleep */
		/* We should skip the host sleep configuration command itself
		 * though */
		if (priv && (pcmd->command !=
			     wlan_cpu_to_le16(HostCmd_CMD_802_11_HS_CFG_ENH))) {
			if (pmadapter->hs_activated == MTRUE) {
				pmadapter->is_hs_configured = MFALSE;
				wlan_host_sleep_activated_event(priv, MFALSE);
			}
		}
		goto done;
	} else {
		wlan_release_cmd_lock(pmadapter);
	}
	ret = MLAN_STATUS_SUCCESS;
done:
	LEAVE();
	return ret;
}

/**
 *  @brief This function handles the command response
 *
 *  @param pmadapter    A pointer to mlan_adapter structure
 *
 *  @return             MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
mlan_status
wlan_process_cmdresp(mlan_adapter *pmadapter)
{
	HostCmd_DS_COMMAND *resp = MNULL;
	mlan_private *pmpriv = wlan_get_priv(pmadapter, MLAN_BSS_ROLE_ANY);
	mlan_private *pmpriv_next = MNULL;
	mlan_status ret = MLAN_STATUS_SUCCESS;
	t_u16 orig_cmdresp_no;
	t_u16 cmdresp_no;
	t_u16 cmdresp_result;
	mlan_ioctl_req *pioctl_buf = MNULL;
	mlan_callbacks *pcb = (mlan_callbacks *)&pmadapter->callbacks;
#ifdef DEBUG_LEVEL1
	t_u32 sec = 0, usec = 0;
#endif
	t_u32 i;

	ENTER();

	if (pmadapter->curr_cmd)
		if (pmadapter->curr_cmd->pioctl_buf != MNULL) {
			pioctl_buf = (mlan_ioctl_req *)
				pmadapter->curr_cmd->pioctl_buf;
		}

	if (!pmadapter->curr_cmd || !pmadapter->curr_cmd->respbuf) {
		resp = (HostCmd_DS_COMMAND *)pmadapter->upld_buf;
		resp->command = wlan_le16_to_cpu(resp->command);
		PRINTM(MERROR, "CMD_RESP: No curr_cmd, 0x%x\n", resp->command);
		if (pioctl_buf)
			pioctl_buf->status_code = MLAN_ERROR_CMD_RESP_FAIL;
		ret = MLAN_STATUS_FAILURE;
		goto done;
	}

	DBG_HEXDUMP(MCMD_D, "CMD_RESP",
		    pmadapter->curr_cmd->respbuf->pbuf +
		    pmadapter->curr_cmd->respbuf->data_offset,
		    pmadapter->curr_cmd->respbuf->data_len);

	resp = (HostCmd_DS_COMMAND *)(pmadapter->curr_cmd->respbuf->pbuf +
				      pmadapter->curr_cmd->respbuf->
				      data_offset);
	orig_cmdresp_no = wlan_le16_to_cpu(resp->command);
	cmdresp_no = (orig_cmdresp_no & HostCmd_CMD_ID_MASK);
	if (pmadapter->curr_cmd->cmd_no != cmdresp_no) {
		PRINTM(MERROR, "cmdresp error: cmd=0x%x cmd_resp=0x%x\n",
		       pmadapter->curr_cmd->cmd_no, cmdresp_no);
		ret = MLAN_STATUS_FAILURE;
		goto done;
	}
	pmadapter->dnld_cmd_in_secs = 0;
	/* Now we got response from FW, cancel the command timer */
	if (pmadapter->cmd_timer_is_set) {
		/* Cancel command timeout timer */
		pcb->moal_stop_timer(pmadapter->pmoal_handle,
				     pmadapter->pmlan_cmd_timer);
		/* Cancel command timeout timer */
		pmadapter->cmd_timer_is_set = MFALSE;
	}
	pmadapter->num_cmd_timeout = 0;
	wlan_request_cmd_lock(pmadapter);
	if (pmadapter->curr_cmd->cmd_flag & CMD_F_CANCELED) {
		cmd_ctrl_node *free_cmd = pmadapter->curr_cmd;
		pmadapter->curr_cmd = MNULL;
		PRINTM(MCMND, "CMD_RESP: 0x%x been canceled!\n",
		       wlan_le16_to_cpu(resp->command));
		if (pioctl_buf)
			pioctl_buf->status_code = MLAN_ERROR_CMD_CANCEL;
		wlan_insert_cmd_to_free_q(pmadapter, free_cmd);
		wlan_release_cmd_lock(pmadapter);
		ret = MLAN_STATUS_FAILURE;
		goto done;
	} else {
		wlan_release_cmd_lock(pmadapter);
	}
	if (pmadapter->curr_cmd->cmd_flag & CMD_F_HOSTCMD) {
		/* Copy original response back to response buffer */
		if (pmpriv)
			wlan_ret_host_cmd(pmpriv, resp, pioctl_buf);
	}
	resp->size = wlan_le16_to_cpu(resp->size);
	resp->seq_num = wlan_le16_to_cpu(resp->seq_num);
	resp->result = wlan_le16_to_cpu(resp->result);

	/* Get BSS number and corresponding priv */
	pmpriv = wlan_get_priv_by_id(pmadapter,
				     HostCmd_GET_BSS_NO(resp->seq_num),
				     HostCmd_GET_BSS_TYPE(resp->seq_num));
	if (!pmpriv)
		pmpriv = wlan_get_priv(pmadapter, MLAN_BSS_ROLE_ANY);
	/* Clear RET_BIT from HostCmd */
	resp->command = (orig_cmdresp_no & HostCmd_CMD_ID_MASK);
	if (!pmpriv) {
		ret = MLAN_STATUS_FAILURE;
		goto done;
	}
	cmdresp_no = resp->command;

	cmdresp_result = resp->result;

	/* Save the last command response to debug log */
	pmadapter->dbg.last_cmd_resp_index =
		(pmadapter->dbg.last_cmd_resp_index + 1) % DBG_CMD_NUM;
	pmadapter->dbg.last_cmd_resp_id[pmadapter->dbg.last_cmd_resp_index] =
		orig_cmdresp_no;

	PRINTM_GET_SYS_TIME(MCMND, &sec, &usec);
	PRINTM_NETINTF(MCMND, pmadapter->curr_cmd->priv);
	PRINTM(MCMND,
	       "CMD_RESP (%lu.%06lu): 0x%x, result %d, len %d, seqno 0x%x\n",
	       sec, usec, orig_cmdresp_no, cmdresp_result, resp->size,
	       resp->seq_num);

	if (!(orig_cmdresp_no & HostCmd_RET_BIT)) {
		PRINTM(MERROR, "CMD_RESP: Invalid response to command!\n");
		if (pioctl_buf)
			pioctl_buf->status_code = MLAN_ERROR_FW_CMDRESP;
		wlan_request_cmd_lock(pmadapter);
		wlan_insert_cmd_to_free_q(pmadapter, pmadapter->curr_cmd);
		pmadapter->curr_cmd = MNULL;
		wlan_release_cmd_lock(pmadapter);
		ret = MLAN_STATUS_FAILURE;
		goto done;
	}

	if (pmadapter->curr_cmd->cmd_flag & CMD_F_HOSTCMD) {
		pmadapter->curr_cmd->cmd_flag &= ~CMD_F_HOSTCMD;
		if ((cmdresp_result == HostCmd_RESULT_OK)
		    && (cmdresp_no == HostCmd_CMD_802_11_HS_CFG_ENH)
			)
			ret = wlan_ret_802_11_hs_cfg(pmpriv, resp, pioctl_buf);
	} else {
		/* handle response */
		ret = pmpriv->ops.process_cmdresp(pmpriv, cmdresp_no, resp,
						  pioctl_buf);
	}

	/* Check init command response */
	if (pmadapter->hw_status == WlanHardwareStatusInitializing ||
	    pmadapter->hw_status == WlanHardwareStatusGetHwSpec) {
		if (ret == MLAN_STATUS_FAILURE) {
#ifdef STA_SUPPORT
			if (pmadapter->pwarm_reset_ioctl_req) {
				/* warm reset failure */
				pmadapter->pwarm_reset_ioctl_req->status_code =
					MLAN_ERROR_CMD_RESP_FAIL;
				pcb->moal_ioctl_complete(pmadapter->
							 pmoal_handle,
							 pmadapter->
							 pwarm_reset_ioctl_req,
							 MLAN_STATUS_FAILURE);
				pmadapter->pwarm_reset_ioctl_req = MNULL;
				goto done;
			}
#endif
			PRINTM(MERROR,
			       "cmd 0x%02x failed during initialization\n",
			       cmdresp_no);
			wlan_init_fw_complete(pmadapter);
			goto done;
		}
#ifdef STA_SUPPORT
#ifdef PCIE
		/* init adma write pointer */
		if (IS_PCIE(pmadapter->card_type) &&
		    cmdresp_no == HostCmd_CMD_FUNC_SHUTDOWN &&
		    pmadapter->pwarm_reset_ioctl_req) {
#if defined(PCIE9098) || defined(PCIE9097)
			if (pmadapter->pcard_pcie->reg->use_adma)
#endif
				wlan_pcie_init_fw(pmadapter);
		}
#endif
#endif
	}

	wlan_request_cmd_lock(pmadapter);
	if (pmadapter->curr_cmd) {
		cmd_ctrl_node *free_cmd = pmadapter->curr_cmd;
		pioctl_buf = (mlan_ioctl_req *)pmadapter->curr_cmd->pioctl_buf;
		pmadapter->curr_cmd = MNULL;
		if (pioctl_buf && (ret == MLAN_STATUS_SUCCESS))
			pioctl_buf->status_code = MLAN_ERROR_NO_ERROR;
		else if (pioctl_buf && (ret == MLAN_STATUS_FAILURE) &&
			 !pioctl_buf->status_code)
			pioctl_buf->status_code = MLAN_ERROR_CMD_RESP_FAIL;

		/* Clean up and put current command back to cmd_free_q */
		wlan_insert_cmd_to_free_q(pmadapter, free_cmd);
	}
	wlan_release_cmd_lock(pmadapter);

	if ((pmadapter->hw_status == WlanHardwareStatusInitializing) &&
	    (pmadapter->last_init_cmd == cmdresp_no)) {
		i = pmpriv->bss_index + 1;
		while (i < pmadapter->priv_num &&
		       (!(pmpriv_next = pmadapter->priv[i]) ||
			pmpriv_next->bss_virtual))
			i++;
		if (!pmpriv_next || i >= pmadapter->priv_num) {
#ifdef STA_SUPPORT
			if (pmadapter->pwarm_reset_ioctl_req) {
				/* warm reset complete */
				PRINTM(MMSG, "wlan: warm reset complete\n");
				pmadapter->hw_status = WlanHardwareStatusReady;
				pcb->moal_ioctl_complete(pmadapter->
							 pmoal_handle,
							 pmadapter->
							 pwarm_reset_ioctl_req,
							 MLAN_STATUS_SUCCESS);
				pmadapter->pwarm_reset_ioctl_req = MNULL;
				goto done;
			}
#endif
			pmadapter->hw_status = WlanHardwareStatusInitdone;
		} else {
			/* Issue init commands for the next interface */
			ret = pmpriv_next->ops.init_cmd(pmpriv_next, MFALSE);
		}
	} else if ((pmadapter->hw_status == WlanHardwareStatusGetHwSpec) &&
		   (HostCmd_CMD_GET_HW_SPEC == cmdresp_no)) {
		pmadapter->hw_status = WlanHardwareStatusGetHwSpecdone;
	}
done:
	LEAVE();
	return ret;
}

/**
 *  @brief This function handles the timeout of command sending.
 *          It will re-send the same command again.
 *
 *  @param function_context   A pointer to function_context
 *  @return                   N/A
 */
t_void
wlan_cmd_timeout_func(t_void *function_context)
{
	mlan_adapter *pmadapter = (mlan_adapter *)function_context;
	cmd_ctrl_node *pcmd_node = MNULL;
	mlan_ioctl_req *pioctl_buf = MNULL;
#ifdef DEBUG_LEVEL1
	t_u32 sec = 0, usec = 0;
#endif
#if defined(SDIO) || defined(PCIE)
	t_u8 i;
#endif
	mlan_private *pmpriv = MNULL;

	ENTER();

	pmadapter->cmd_timer_is_set = MFALSE;
	if (!pmadapter->curr_cmd) {
		if (pmadapter->ext_scan && pmadapter->ext_scan_enh &&
		    pmadapter->scan_processing) {
			PRINTM(MMSG, "Ext scan enh timeout\n");
			pmadapter->ext_scan_timeout = MTRUE;
			wlan_dump_info(pmadapter, REASON_CODE_EXT_SCAN_TIMEOUT);
			goto exit;
		}
		PRINTM(MWARN, "CurCmd Empty\n");
		goto exit;
	}
	pmadapter->num_cmd_timeout++;
	pcmd_node = pmadapter->curr_cmd;
	if (pcmd_node->pioctl_buf != MNULL) {
		pioctl_buf = (mlan_ioctl_req *)pcmd_node->pioctl_buf;
		pioctl_buf->status_code = MLAN_ERROR_CMD_TIMEOUT;
	}

	pmadapter->dbg.timeout_cmd_id =
		pmadapter->dbg.last_cmd_id[pmadapter->dbg.last_cmd_index];
	pmadapter->dbg.timeout_cmd_act =
		pmadapter->dbg.last_cmd_act[pmadapter->dbg.last_cmd_index];
	PRINTM_GET_SYS_TIME(MERROR, &sec, &usec);
	PRINTM(MERROR, "Timeout cmd id (%lu.%06lu) = 0x%x, act = 0x%x\n", sec,
	       usec, pmadapter->dbg.timeout_cmd_id,
	       pmadapter->dbg.timeout_cmd_act);
#if defined(SDIO) || defined(PCIE)
	if (!IS_USB(pmadapter->card_type) && pcmd_node->cmdbuf) {
		t_u8 *pcmd_buf;
		pcmd_buf = pcmd_node->cmdbuf->pbuf +
			pcmd_node->cmdbuf->data_offset +
			pmadapter->ops.intf_header_len;
		for (i = 0; i < 16; i++)
			PRINTM(MERROR, "%02x ", *pcmd_buf++);
		PRINTM(MERROR, "\n");
	}
#endif
#ifdef PCIE
	if (IS_PCIE(pmadapter->card_type))
		pmadapter->ops.debug_dump(pmadapter);
#endif
	pmpriv = pcmd_node->priv;
	if (pmpriv)
		PRINTM(MERROR, "BSS type = %d BSS role= %d\n", pmpriv->bss_type,
		       pmpriv->bss_role);
	wlan_dump_info(pmadapter, REASON_CODE_CMD_TIMEOUT);

	if (pmadapter->hw_status == WlanHardwareStatusInitializing ||
	    pmadapter->hw_status == WlanHardwareStatusGetHwSpec)
		wlan_init_fw_complete(pmadapter);
	else {
		/* Signal MOAL to perform extra handling for debugging */
		if (pmpriv) {
			wlan_recv_event(pmpriv, MLAN_EVENT_ID_DRV_DBG_DUMP,
					MNULL);
		} else {
			wlan_recv_event(wlan_get_priv(pmadapter,
						      MLAN_BSS_ROLE_ANY),
					MLAN_EVENT_ID_DRV_DBG_DUMP, MNULL);
		}
	}

exit:
	LEAVE();
	return;
}

#ifdef STA_SUPPORT
/**
 *  @brief Internal function used to flush the scan pending queue
 *
 *  @param pmadapter    A pointer to mlan_adapter structure
 *
 *  @return             N/A
 */
t_void
wlan_flush_scan_queue(pmlan_adapter pmadapter)
{
	cmd_ctrl_node *pcmd_node = MNULL;
	mlan_ioctl_req *pioctl_buf = MNULL;
	HostCmd_DS_COMMAND *pcmd = MNULL;
	t_u16 cmd_no = 0;
	pmlan_callbacks pcb = &pmadapter->callbacks;
	ENTER();

	wlan_request_cmd_lock(pmadapter);
	while ((pcmd_node =
		(cmd_ctrl_node *)util_peek_list(pmadapter->pmoal_handle,
						&pmadapter->scan_pending_q,
						MNULL, MNULL))) {
		util_unlink_list(pmadapter->pmoal_handle,
				 &pmadapter->scan_pending_q,
				 (pmlan_linked_list)pcmd_node, MNULL, MNULL);
		pcmd = (HostCmd_DS_COMMAND *)(pcmd_node->cmdbuf->pbuf +
					      pcmd_node->cmdbuf->data_offset);
		cmd_no = wlan_le16_to_cpu(pcmd->command);
		PRINTM(MCMND, "flush scan queue: cmd 0x%02x\n", cmd_no);
		if (pcmd_node->pioctl_buf &&
		    cmd_no != HostCmd_CMD_802_11_SCAN
		    && cmd_no != HostCmd_CMD_802_11_SCAN_EXT) {
			pioctl_buf = (mlan_ioctl_req *)pcmd_node->pioctl_buf;
			pcmd_node->pioctl_buf = MNULL;
			pioctl_buf->status_code = MLAN_ERROR_CMD_CANCEL;
			pcb->moal_ioctl_complete(pmadapter->pmoal_handle,
						 pioctl_buf,
						 MLAN_STATUS_FAILURE);
		}
		wlan_insert_cmd_to_free_q(pmadapter, pcmd_node);
	}

	pmadapter->scan_processing = MFALSE;
	wlan_release_cmd_lock(pmadapter);

	LEAVE();
}

/**
 *  @brief Cancel pending SCAN ioctl cmd.
 *
 *  @param pmadapter    A pointer to mlan_adapter
 *  @param pioctl_req   A pointer to pmlan_ioctl_req
 *
 *  @return             MLAN_STATUS_SUCCESS/MLAN_STATUS_PENDING
 */
mlan_status
wlan_cancel_pending_scan_cmd(pmlan_adapter pmadapter,
			     pmlan_ioctl_req pioctl_req)
{
	pmlan_callbacks pcb = &pmadapter->callbacks;
	cmd_ctrl_node *pcmd_node = MNULL;
	mlan_ioctl_req *pioctl_buf = MNULL;
	pmlan_private priv = MNULL;
	mlan_status status = MLAN_STATUS_SUCCESS;
	ENTER();

	PRINTM(MIOCTL, "Cancel scan command\n");
	wlan_request_cmd_lock(pmadapter);
	/* IOCTL will be completed, avoid calling IOCTL complete again from
	 * EVENT/CMDRESP */
	if (pmadapter->pscan_ioctl_req) {
		pioctl_buf = pmadapter->pscan_ioctl_req;
		priv = pmadapter->priv[pioctl_buf->bss_index];
		pmadapter->pscan_ioctl_req = MNULL;
		pioctl_buf->status_code = MLAN_ERROR_CMD_CANCEL;
		pcb->moal_ioctl_complete(pmadapter->pmoal_handle, pioctl_buf,
					 MLAN_STATUS_FAILURE);
	}

	if (pmadapter->curr_cmd && pmadapter->curr_cmd->pioctl_buf) {
		pioctl_buf = (mlan_ioctl_req *)pmadapter->curr_cmd->pioctl_buf;
		if (pioctl_buf->req_id == MLAN_IOCTL_SCAN) {
			PRINTM(MIOCTL, "wlan_cancel_scan: current command\n");
			pcmd_node = pmadapter->curr_cmd;
			pcmd_node->pioctl_buf = MNULL;
			pcmd_node->cmd_flag |= CMD_F_CANCELED;
			pioctl_buf->status_code = MLAN_ERROR_CMD_CANCEL;
			pcb->moal_ioctl_complete(pmadapter->pmoal_handle,
						 pioctl_buf,
						 MLAN_STATUS_FAILURE);
		}
	}
	while ((pcmd_node = wlan_get_pending_scan_cmd(pmadapter)) != MNULL) {
		PRINTM(MIOCTL,
		       "wlan_cancel_scan: find scan command in cmd_pending_q\n");
		util_unlink_list(pmadapter->pmoal_handle,
				 &pmadapter->cmd_pending_q,
				 (pmlan_linked_list)pcmd_node, MNULL, MNULL);
		wlan_insert_cmd_to_free_q(pmadapter, pcmd_node);
	}
	wlan_release_cmd_lock(pmadapter);
	if (pmadapter->scan_processing &&
	    pmadapter->ext_scan_type == EXT_SCAN_ENHANCE) {
		if (priv) {
			if (MLAN_STATUS_SUCCESS ==
			    wlan_prepare_cmd(priv, HostCmd_CMD_802_11_SCAN_EXT,
					     HostCmd_ACT_GEN_SET, 0, pioctl_req,
					     MNULL)) {
				wlan_recv_event(priv,
						MLAN_EVENT_ID_DRV_DEFER_HANDLING,
						MNULL);
				status = MLAN_STATUS_PENDING;
			}
		}
	} else
		/* Cancel all pending scan command */
		wlan_flush_scan_queue(pmadapter);
	LEAVE();
	return status;
}
#endif

/**
 *  @brief Cancel all pending cmd.
 *
 *  @param pmadapter    A pointer to mlan_adapter
 *  @param flag         MTRUE/MFALSE
 *
 *  @return             N/A
 */
t_void
wlan_cancel_all_pending_cmd(pmlan_adapter pmadapter, t_u8 flag)
{
	cmd_ctrl_node *pcmd_node = MNULL;
	pmlan_callbacks pcb = &pmadapter->callbacks;
	mlan_ioctl_req *pioctl_buf = MNULL;
#ifdef STA_SUPPORT
	pmlan_private priv = MNULL;
#endif
	ENTER();
	/* Cancel current cmd */
	wlan_request_cmd_lock(pmadapter);
#ifdef STA_SUPPORT
	/* IOCTL will be completed, avoid calling IOCTL complete again from
	 * EVENT/CMDRESP */
	if (pmadapter->pscan_ioctl_req) {
		pioctl_buf = pmadapter->pscan_ioctl_req;
		priv = pmadapter->priv[pioctl_buf->bss_index];
		pmadapter->pscan_ioctl_req = MNULL;
		pioctl_buf->status_code = MLAN_ERROR_CMD_CANCEL;
		pcb->moal_ioctl_complete(pmadapter->pmoal_handle, pioctl_buf,
					 MLAN_STATUS_FAILURE);
	}
#endif
	if (pmadapter->curr_cmd) {
		pcmd_node = pmadapter->curr_cmd;
		if (pcmd_node->pioctl_buf) {
			pioctl_buf = (mlan_ioctl_req *)pcmd_node->pioctl_buf;
			pcmd_node->pioctl_buf = MNULL;
			pioctl_buf->status_code = MLAN_ERROR_CMD_CANCEL;
			pcb->moal_ioctl_complete(pmadapter->pmoal_handle,
						 pioctl_buf,
						 MLAN_STATUS_FAILURE);
		}
		if (flag) {
			pmadapter->curr_cmd = MNULL;
			wlan_insert_cmd_to_free_q(pmadapter, pcmd_node);
		}
	}

	/* Cancel all pending command */
	while ((pcmd_node =
		(cmd_ctrl_node *)util_peek_list(pmadapter->pmoal_handle,
						&pmadapter->cmd_pending_q,
						MNULL, MNULL))) {
		util_unlink_list(pmadapter->pmoal_handle,
				 &pmadapter->cmd_pending_q,
				 (pmlan_linked_list)pcmd_node, MNULL, MNULL);
		if (pcmd_node->pioctl_buf) {
			pioctl_buf = (mlan_ioctl_req *)pcmd_node->pioctl_buf;
			pioctl_buf->status_code = MLAN_ERROR_CMD_CANCEL;
			pcb->moal_ioctl_complete(pmadapter->pmoal_handle,
						 pioctl_buf,
						 MLAN_STATUS_FAILURE);
			pcmd_node->pioctl_buf = MNULL;
		}
		wlan_insert_cmd_to_free_q(pmadapter, pcmd_node);
	}
	wlan_release_cmd_lock(pmadapter);
	/* Cancel all pending scan command */
	wlan_flush_scan_queue(pmadapter);
	LEAVE();
}

/**
 *  @brief Cancel specific bss's pending ioctl cmd.
 *
 *  @param pmadapter    A pointer to mlan_adapter
 *  @param bss_index    BSS index
 *
 *  @return             N/A
 */
t_void
wlan_cancel_bss_pending_cmd(pmlan_adapter pmadapter, t_u32 bss_index)
{
	pmlan_callbacks pcb = &pmadapter->callbacks;
	cmd_ctrl_node *pcmd_node = MNULL;
	mlan_ioctl_req *pioctl_buf = MNULL;
#ifdef STA_SUPPORT
	t_u8 flash_scan = MFALSE;
#endif
#ifdef STA_SUPPORT
	pmlan_private priv = MNULL;
#endif
	ENTER();

	PRINTM(MIOCTL, "MOAL Cancel BSS IOCTL: bss_index=%d\n", (int)bss_index);
	wlan_request_cmd_lock(pmadapter);
#ifdef STA_SUPPORT
	if (pmadapter->pscan_ioctl_req &&
	    (pmadapter->pscan_ioctl_req->bss_index == bss_index)) {
		/* IOCTL will be completed, avoid calling IOCTL complete again
		 * from EVENT/CMDRESP */
		flash_scan = MTRUE;
		pioctl_buf = pmadapter->pscan_ioctl_req;
		priv = pmadapter->priv[pioctl_buf->bss_index];
		pmadapter->pscan_ioctl_req = MNULL;
		pioctl_buf->status_code = MLAN_ERROR_CMD_CANCEL;
		pcb->moal_ioctl_complete(pmadapter->pmoal_handle, pioctl_buf,
					 MLAN_STATUS_FAILURE);
	}
#endif
	if (pmadapter->curr_cmd && pmadapter->curr_cmd->pioctl_buf) {
		pioctl_buf = (mlan_ioctl_req *)pmadapter->curr_cmd->pioctl_buf;
		if (pioctl_buf->bss_index == bss_index) {
			pcmd_node = pmadapter->curr_cmd;
			pcmd_node->pioctl_buf = MNULL;
			pcmd_node->cmd_flag |= CMD_F_CANCELED;
#ifdef STA_SUPPORT
			if (pioctl_buf->req_id == MLAN_IOCTL_SCAN)
				flash_scan = MTRUE;
#endif
			pioctl_buf->status_code = MLAN_ERROR_CMD_CANCEL;
			pcb->moal_ioctl_complete(pmadapter->pmoal_handle,
						 pioctl_buf,
						 MLAN_STATUS_FAILURE);
		}
	}
	while ((pcmd_node =
		wlan_get_bss_pending_ioctl_cmd(pmadapter,
					       bss_index)) != MNULL) {
		util_unlink_list(pmadapter->pmoal_handle,
				 &pmadapter->cmd_pending_q,
				 (pmlan_linked_list)pcmd_node, MNULL, MNULL);
		pioctl_buf = (mlan_ioctl_req *)pcmd_node->pioctl_buf;
		pcmd_node->pioctl_buf = MNULL;
#ifdef STA_SUPPORT
		if (pioctl_buf->req_id == MLAN_IOCTL_SCAN)
			flash_scan = MTRUE;
#endif
		pioctl_buf->status_code = MLAN_ERROR_CMD_CANCEL;
		pcb->moal_ioctl_complete(pmadapter->pmoal_handle, pioctl_buf,
					 MLAN_STATUS_FAILURE);
		wlan_insert_cmd_to_free_q(pmadapter, pcmd_node);
	}
	wlan_release_cmd_lock(pmadapter);
#ifdef STA_SUPPORT
	if (flash_scan) {
		if (pmadapter->scan_processing &&
		    pmadapter->ext_scan_type == EXT_SCAN_ENHANCE) {
			if (priv) {
				if (MLAN_STATUS_FAILURE ==
				    wlan_prepare_cmd(priv,
						     HostCmd_CMD_802_11_SCAN_EXT,
						     HostCmd_ACT_GEN_SET, 0,
						     MNULL, MNULL))
					PRINTM(MERROR,
					       "failed to prepare command");
				wlan_recv_event(priv,
						MLAN_EVENT_ID_DRV_DEFER_HANDLING,
						MNULL);
			}
		} else
			/* Cancel all pending scan command */
			wlan_flush_scan_queue(pmadapter);
	}
#endif
	LEAVE();
	return;
}

/**
 *  @brief Cancel pending ioctl cmd.
 *
 *  @param pmadapter    A pointer to mlan_adapter
 *  @param pioctl_req   A pointer to mlan_ioctl_req buf
 *
 *  @return             N/A
 */
t_void
wlan_cancel_pending_ioctl(pmlan_adapter pmadapter, pmlan_ioctl_req pioctl_req)
{
	pmlan_callbacks pcb = &pmadapter->callbacks;
	cmd_ctrl_node *pcmd_node = MNULL;
	t_u8 find = MFALSE;
#ifdef STA_SUPPORT
	pmlan_private priv = MNULL;
#endif

	ENTER();

	PRINTM(MIOCTL, "MOAL Cancel IOCTL: 0x%x sub_id=0x%x action=%d\n",
	       pioctl_req->req_id, *((t_u32 *)pioctl_req->pbuf),
	       (int)pioctl_req->action);

	wlan_request_cmd_lock(pmadapter);
#ifdef STA_SUPPORT
	/* IOCTL will be completed, avoid calling IOCTL complete again from
	 * EVENT/CMDRESP */
	if (pmadapter->pscan_ioctl_req == pioctl_req) {
		priv = pmadapter->priv[pioctl_req->bss_index];
		pmadapter->pscan_ioctl_req = MNULL;
		find = MTRUE;
	}
#endif
	if ((pmadapter->curr_cmd) &&
	    (pmadapter->curr_cmd->pioctl_buf == pioctl_req)) {
		pcmd_node = pmadapter->curr_cmd;
		pcmd_node->pioctl_buf = MNULL;
		pcmd_node->cmd_flag |= CMD_F_CANCELED;
		find = MTRUE;
	}

	while ((pcmd_node = wlan_get_pending_ioctl_cmd(pmadapter,
						       pioctl_req)) != MNULL) {
		util_unlink_list(pmadapter->pmoal_handle,
				 &pmadapter->cmd_pending_q,
				 (pmlan_linked_list)pcmd_node, MNULL, MNULL);
		pcmd_node->pioctl_buf = MNULL;
		find = MTRUE;
		wlan_insert_cmd_to_free_q(pmadapter, pcmd_node);
	}
	wlan_release_cmd_lock(pmadapter);
#ifdef STA_SUPPORT
	if (pioctl_req->req_id == MLAN_IOCTL_SCAN) {
		if (pmadapter->scan_processing &&
		    pmadapter->ext_scan_type == EXT_SCAN_ENHANCE) {
			if (priv) {
				if (MLAN_STATUS_FAILURE ==
				    wlan_prepare_cmd(priv,
						     HostCmd_CMD_802_11_SCAN_EXT,
						     HostCmd_ACT_GEN_SET, 0,
						     MNULL, MNULL))
					PRINTM(MERROR,
					       "Failed to prepare command");
				wlan_recv_event(priv,
						MLAN_EVENT_ID_DRV_DEFER_HANDLING,
						MNULL);
			}
		} else
			/* Cancel all pending scan command */
			wlan_flush_scan_queue(pmadapter);
	}
#endif
	if (find) {
		pioctl_req->status_code = MLAN_ERROR_CMD_CANCEL;
		pcb->moal_ioctl_complete(pmadapter->pmoal_handle, pioctl_req,
					 MLAN_STATUS_FAILURE);
	}

	LEAVE();
	return;
}

/**
 *  @brief This function convert mlan_wifi_rate to wifi_rate.
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param pmlan_rate          A pointer to mlan_wifi_rate structure
 *  @param prate   A pointer to wifi_rate
 *
 *  @return           N/A
 */
t_void
wlan_fill_hal_wifi_rate(pmlan_private pmpriv,
			mlan_wifi_rate * pmlan_rate, wifi_rate * prate)
{
	t_u8 index = 0;
	t_u8 rate_info = 0;

	ENTER();

	prate->preamble = pmlan_rate->preamble;
	prate->nss = pmlan_rate->nss;
	prate->bw = pmlan_rate->bw;
	prate->rateMcsIdx = pmlan_rate->rateMcsIdx;
	prate->reserved = 0;
	prate->bitrate = wlan_le32_to_cpu(pmlan_rate->bitrate);

	if (!prate->bitrate) {
		index = prate->rateMcsIdx;
		index |= prate->nss << 4;
		if (prate->preamble == WIFI_PREAMBLE_HT)
			rate_info = MLAN_RATE_FORMAT_HT;
		else if (prate->preamble == WIFI_PREAMBLE_VHT)
			rate_info = MLAN_RATE_FORMAT_VHT;
		else
			rate_info = MLAN_RATE_FORMAT_LG;
		rate_info |= prate->bw << 2;
		PRINTM(MCMND, "index=0x%x rate_info=0x%x\n", index, rate_info);
		/** For rateMcsIdx, OFDM/CCK rate code would be as per ieee std
		 * in the units of 0.5mbps. HT/VHT it would be mcs index */
		/** For bitrate, in 100kbps */
		if (rate_info == MLAN_RATE_FORMAT_LG)
			prate->bitrate = prate->rateMcsIdx * 5;
		else
			prate->bitrate =
				wlan_index_to_data_rate(pmpriv->adapter, index,
							rate_info, 0) * 5;
		PRINTM(MCMND, "bitrate(in 100kbps)=%d\n", prate->bitrate);
	}

	LEAVE();
}

/**
 *  @brief Handle the version_ext resp
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param resp         A pointer to HostCmd_DS_COMMAND
 *  @param pioctl_buf   A pointer to mlan_ioctl_req structure
 *
 *  @return             MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_ret_ver_ext(pmlan_private pmpriv,
		 HostCmd_DS_COMMAND *resp, mlan_ioctl_req *pioctl_buf)
{
	HostCmd_DS_VERSION_EXT *ver_ext = &resp->params.verext;
	mlan_ds_get_info *info;
	ENTER();
	if (pioctl_buf) {
		info = (mlan_ds_get_info *)pioctl_buf->pbuf;
		info->param.ver_ext.version_str_sel = ver_ext->version_str_sel;
		memcpy_ext(pmpriv->adapter, info->param.ver_ext.version_str,
			   ver_ext->version_str, sizeof(char) * 128,
			   sizeof(char) * MLAN_MAX_VER_STR_LEN);
	}
	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief Handle the rx mgmt forward registration resp
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param resp         A pointer to HostCmd_DS_COMMAND
 *  @param pioctl_buf   A pointer to mlan_ioctl_req structure
 *
 *  @return             MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_ret_rx_mgmt_ind(pmlan_private pmpriv,
		     HostCmd_DS_COMMAND *resp, mlan_ioctl_req *pioctl_buf)
{
	mlan_ds_misc_cfg *misc = MNULL;
	ENTER();

	if (pioctl_buf) {
		misc = (mlan_ds_misc_cfg *)pioctl_buf->pbuf;
		misc->param.mgmt_subtype_mask =
			wlan_le32_to_cpu(resp->params.rx_mgmt_ind.
					 mgmt_subtype_mask);
	}

	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function checks conditions and prepares to
 *              send sleep confirm command to firmware if OK.
 *
 *  @param pmadapter    A pointer to mlan_adapter structure
 *
 *  @return             N/A
 */
t_void
wlan_check_ps_cond(mlan_adapter *pmadapter)
{
	ENTER();

	if (!pmadapter->cmd_sent && !pmadapter->curr_cmd &&
	    !pmadapter->keep_wakeup && !wlan_is_tx_pending(pmadapter) &&
	    !IS_CARD_RX_RCVD(pmadapter)) {
		wlan_dnld_sleep_confirm_cmd(pmadapter);
	} else {
		PRINTM(MCMND, "Delay Sleep Confirm (%s%s%s%s)\n",
		       (pmadapter->cmd_sent) ? "D" : "",
		       (pmadapter->curr_cmd) ? "C" : "",
		       (wlan_is_tx_pending(pmadapter)) ? "T" : "",
		       (IS_CARD_RX_RCVD(pmadapter)) ? "R" : "");
	}

	LEAVE();
}

/**
 *  @brief This function sends the HS_ACTIVATED event to the application
 *
 *  @param priv         A pointer to mlan_private structure
 *  @param activated    MTRUE if activated, MFALSE if de-activated
 *
 *  @return             N/A
 */
t_void
wlan_host_sleep_activated_event(pmlan_private priv, t_u8 activated)
{
	ENTER();

	if (!priv) {
		LEAVE();
		return;
	}

	if (activated) {
		if (priv->adapter->is_hs_configured) {
			priv->adapter->hs_activated = MTRUE;
			wlan_update_rxreorder_tbl(priv->adapter, MTRUE);
			PRINTM(MEVENT, "hs_activated\n");
			wlan_recv_event(priv, MLAN_EVENT_ID_DRV_HS_ACTIVATED,
					MNULL);
		} else
			PRINTM(MWARN, "hs_activated: HS not configured !!!\n");
	} else {
		PRINTM(MEVENT, "hs_deactived\n");
		priv->adapter->hs_activated = MFALSE;
		wlan_recv_event(priv, MLAN_EVENT_ID_DRV_HS_DEACTIVATED, MNULL);
	}

	LEAVE();
	return;
}

/**
 *  @brief This function sends the HS_WAKEUP event to the application
 *
 *  @param priv         A pointer to mlan_private structure
 *
 *  @return             N/A
 */
t_void
wlan_host_sleep_wakeup_event(pmlan_private priv)
{
	ENTER();

	if (priv->adapter->is_hs_configured)
		wlan_recv_event(priv, MLAN_EVENT_ID_FW_HS_WAKEUP, MNULL);
	else
		PRINTM(MWARN, "hs_wakeup: Host Sleep not configured !!!\n");

	LEAVE();
}

/**
 *  @brief This function handles the command response of hs_cfg
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param resp         A pointer to HostCmd_DS_COMMAND
 *  @param pioctl_buf   A pointer to mlan_ioctl_req structure
 *
 *  @return             MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_ret_802_11_hs_cfg(pmlan_private pmpriv,
		       HostCmd_DS_COMMAND *resp, mlan_ioctl_req *pioctl_buf)
{
	pmlan_adapter pmadapter = pmpriv->adapter;
	HostCmd_DS_802_11_HS_CFG_ENH *phs_cfg = &resp->params.opt_hs_cfg;

	ENTER();

	phs_cfg->params.hs_config.conditions =
		wlan_le32_to_cpu(phs_cfg->params.hs_config.conditions);
	phs_cfg->action = wlan_le16_to_cpu(phs_cfg->action);
	PRINTM(MCMND,
	       "CMD_RESP: HS_CFG cmd reply result=%#x,"
	       " action=0x%x conditions=0x%x gpio=0x%x gap=0x%x\n",
	       resp->result, phs_cfg->action,
	       phs_cfg->params.hs_config.conditions,
	       phs_cfg->params.hs_config.gpio, phs_cfg->params.hs_config.gap);
	if ((phs_cfg->action == HS_ACTIVATE &&
	     !pmadapter->pcard_info->supp_ps_handshake) ||
	    pmadapter->pcard_info->supp_ps_handshake) {
		/* clean up curr_cmd to allow suspend */
		if (pioctl_buf)
			pioctl_buf->status_code = MLAN_ERROR_NO_ERROR;
		/* Clean up and put current command back to cmd_free_q */
		wlan_request_cmd_lock(pmadapter);
		wlan_insert_cmd_to_free_q(pmadapter, pmadapter->curr_cmd);
		pmadapter->curr_cmd = MNULL;
		wlan_release_cmd_lock(pmadapter);
		if (!pmadapter->pcard_info->supp_ps_handshake) {
			wlan_host_sleep_activated_event(pmpriv, MTRUE);
			goto done;
		}
	}
	if (phs_cfg->params.hs_config.conditions != HOST_SLEEP_CFG_CANCEL) {
		pmadapter->is_hs_configured = MTRUE;
		if (pmadapter->pcard_info->supp_ps_handshake)
			wlan_host_sleep_activated_event(pmpriv, MTRUE);
	} else {
		pmadapter->is_hs_configured = MFALSE;
		if (pmadapter->hs_activated)
			wlan_host_sleep_activated_event(pmpriv, MFALSE);
	}

done:
	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief Perform hs related activities on receiving the power up interrupt
 *
 *  @param pmadapter  A pointer to the adapter structure
 *  @return           N/A
 */
t_void
wlan_process_hs_config(pmlan_adapter pmadapter)
{
	ENTER();
	PRINTM(MINFO, "Recevie interrupt/data in HS mode\n");
	if (pmadapter->hs_cfg.gap == HOST_SLEEP_CFG_GAP_FF)
		pmadapter->ops.wakeup_card(pmadapter, MTRUE);
	LEAVE();
	return;
}

/**
 *  @brief Check sleep confirm command response and set the state to ASLEEP
 *
 *  @param pmadapter  A pointer to the adapter structure
 *  @param pbuf       A pointer to the command response buffer
 *  @param upld_len   Command response buffer length
 *  @return           N/A
 */
void
wlan_process_sleep_confirm_resp(pmlan_adapter pmadapter, t_u8 *pbuf,
				t_u32 upld_len)
{
	HostCmd_DS_COMMAND *cmd;
	pmlan_private pmpriv;

	ENTER();

	if (!upld_len) {
		PRINTM(MERROR, "Command size is 0\n");
		LEAVE();
		return;
	}
	cmd = (HostCmd_DS_COMMAND *)pbuf;
	cmd->result = wlan_le16_to_cpu(cmd->result);
	cmd->command = wlan_le16_to_cpu(cmd->command);
	cmd->seq_num = wlan_le16_to_cpu(cmd->seq_num);

	pmpriv = wlan_get_priv_by_id(pmadapter,
				     HostCmd_GET_BSS_NO(cmd->seq_num),
				     HostCmd_GET_BSS_TYPE(cmd->seq_num));
	/* Update sequence number */
	cmd->seq_num = HostCmd_GET_SEQ_NO(cmd->seq_num);
	/* Clear RET_BIT from HostCmd */
	cmd->command &= HostCmd_CMD_ID_MASK;
	if (!pmpriv)
		pmpriv = wlan_get_priv(pmadapter, MLAN_BSS_ROLE_ANY);
	if (cmd->command != HostCmd_CMD_802_11_PS_MODE_ENH) {
		PRINTM(MERROR,
		       "Received unexpected response for command %x, result = %x\n",
		       cmd->command, cmd->result);
		LEAVE();
		return;
	}
	PRINTM_NETINTF(MEVENT, pmpriv);
	PRINTM(MEVENT, "#\n");
	if (cmd->result != MLAN_STATUS_SUCCESS) {
		PRINTM(MERROR, "Sleep confirm command failed\n");
		pmadapter->pm_wakeup_card_req = MFALSE;
		pmadapter->ps_state = PS_STATE_AWAKE;
		LEAVE();
		return;
	}
	pmadapter->pm_wakeup_card_req = MTRUE;

	if (pmadapter->is_hs_configured) {
		wlan_host_sleep_activated_event(wlan_get_priv
						(pmadapter, MLAN_BSS_ROLE_ANY),
						MTRUE);
	}
	pmadapter->ps_state = PS_STATE_SLEEP;
	LEAVE();
}

/**
 *  @brief This function prepares command of power mode
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param cmd          A pointer to HostCmd_DS_COMMAND structure
 *  @param cmd_action   the action: GET or SET
 *  @param ps_bitmap    PS bitmap
 *  @param pdata_buf    A pointer to data buffer
 *  @return             MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_cmd_enh_power_mode(pmlan_private pmpriv,
			HostCmd_DS_COMMAND *cmd,
			t_u16 cmd_action, t_u16 ps_bitmap, t_void *pdata_buf)
{
	HostCmd_DS_802_11_PS_MODE_ENH *psmode_enh = &cmd->params.psmode_enh;
	t_u8 *tlv = MNULL;
	t_u16 cmd_size = 0;

	ENTER();

	PRINTM(MCMND, "PS Command: action = 0x%x, bitmap = 0x%x\n", cmd_action,
	       ps_bitmap);

	cmd->command = wlan_cpu_to_le16(HostCmd_CMD_802_11_PS_MODE_ENH);
	if (cmd_action == DIS_AUTO_PS) {
		psmode_enh->action = wlan_cpu_to_le16(DIS_AUTO_PS);
		psmode_enh->params.ps_bitmap = wlan_cpu_to_le16(ps_bitmap);
		cmd->size = wlan_cpu_to_le16(S_DS_GEN + AUTO_PS_FIX_SIZE);
	} else if (cmd_action == GET_PS) {
		psmode_enh->action = wlan_cpu_to_le16(GET_PS);
		psmode_enh->params.ps_bitmap = wlan_cpu_to_le16(ps_bitmap);
		cmd->size = wlan_cpu_to_le16(S_DS_GEN + AUTO_PS_FIX_SIZE);
	} else if (cmd_action == EN_AUTO_PS) {
		psmode_enh->action = wlan_cpu_to_le16(EN_AUTO_PS);
		psmode_enh->params.auto_ps.ps_bitmap =
			wlan_cpu_to_le16(ps_bitmap);
		cmd_size = S_DS_GEN + AUTO_PS_FIX_SIZE;
		tlv = (t_u8 *)cmd + cmd_size;
		if (ps_bitmap & BITMAP_STA_PS) {
			pmlan_adapter pmadapter = pmpriv->adapter;
			MrvlIEtypes_ps_param_t *ps_tlv =
				(MrvlIEtypes_ps_param_t *)tlv;
			ps_param *ps_mode = &ps_tlv->param;
			ps_tlv->header.type =
				wlan_cpu_to_le16(TLV_TYPE_PS_PARAM);
			ps_tlv->header.len =
				wlan_cpu_to_le16(sizeof(MrvlIEtypes_ps_param_t)
						 - sizeof(MrvlIEtypesHeader_t));
			cmd_size += sizeof(MrvlIEtypes_ps_param_t);
			tlv += sizeof(MrvlIEtypes_ps_param_t);
			ps_mode->null_pkt_interval =
				wlan_cpu_to_le16(pmadapter->null_pkt_interval);
			ps_mode->multiple_dtims =
				wlan_cpu_to_le16(pmadapter->multiple_dtim);
			ps_mode->bcn_miss_timeout =
				wlan_cpu_to_le16(pmadapter->bcn_miss_time_out);
			ps_mode->local_listen_interval =
				wlan_cpu_to_le16(pmadapter->
						 local_listen_interval);
			ps_mode->delay_to_ps =
				wlan_cpu_to_le16(pmadapter->delay_to_ps);
			ps_mode->mode =
				wlan_cpu_to_le16(pmadapter->enhanced_ps_mode);
		}
		if (ps_bitmap & BITMAP_BCN_TMO) {
			MrvlIEtypes_bcn_timeout_t *bcn_tmo_tlv =
				(MrvlIEtypes_bcn_timeout_t *) tlv;
			mlan_ds_bcn_timeout *bcn_tmo =
				(mlan_ds_bcn_timeout *) pdata_buf;
			bcn_tmo_tlv->header.type =
				wlan_cpu_to_le16(TLV_TYPE_BCN_TIMEOUT);
			bcn_tmo_tlv->header.len =
				wlan_cpu_to_le16(sizeof
						 (MrvlIEtypes_bcn_timeout_t) -
						 sizeof(MrvlIEtypesHeader_t));
			bcn_tmo_tlv->bcn_miss_tmo_window =
				wlan_cpu_to_le16(bcn_tmo->bcn_miss_tmo_window);
			bcn_tmo_tlv->bcn_miss_tmo_period =
				wlan_cpu_to_le16(bcn_tmo->bcn_miss_tmo_period);
			bcn_tmo_tlv->bcn_rq_tmo_window =
				wlan_cpu_to_le16(bcn_tmo->bcn_rq_tmo_window);
			bcn_tmo_tlv->bcn_rq_tmo_period =
				wlan_cpu_to_le16(bcn_tmo->bcn_rq_tmo_period);
			cmd_size += sizeof(MrvlIEtypes_bcn_timeout_t);
			tlv += sizeof(MrvlIEtypes_bcn_timeout_t);

			psmode_enh->params.auto_ps.ps_bitmap = wlan_cpu_to_le16((ps_bitmap & (~BITMAP_BCN_TMO)) | BITMAP_STA_PS);
		}
		if (ps_bitmap & BITMAP_AUTO_DS) {
			MrvlIEtypes_auto_ds_param_t *auto_ps_tlv =
				(MrvlIEtypes_auto_ds_param_t *)tlv;
			auto_ds_param *auto_ds = &auto_ps_tlv->param;
			t_u16 idletime = 0;
			auto_ps_tlv->header.type =
				wlan_cpu_to_le16(TLV_TYPE_AUTO_DS_PARAM);
			auto_ps_tlv->header.len =
				wlan_cpu_to_le16(sizeof
						 (MrvlIEtypes_auto_ds_param_t) -
						 sizeof(MrvlIEtypesHeader_t));
			cmd_size += sizeof(MrvlIEtypes_auto_ds_param_t);
			tlv += sizeof(MrvlIEtypes_auto_ds_param_t);
			if (pdata_buf)
				idletime =
					((mlan_ds_auto_ds *)pdata_buf)->
					idletime;
			auto_ds->deep_sleep_timeout =
				wlan_cpu_to_le16(idletime);
		}
#if defined(UAP_SUPPORT)
		if (pdata_buf &&
		    (ps_bitmap & (BITMAP_UAP_INACT_PS | BITMAP_UAP_DTIM_PS))) {
			mlan_ds_ps_mgmt *ps_mgmt = (mlan_ds_ps_mgmt *)pdata_buf;
			MrvlIEtypes_sleep_param_t *sleep_tlv = MNULL;
			MrvlIEtypes_inact_sleep_param_t *inact_tlv = MNULL;
			if (ps_mgmt->flags & PS_FLAG_SLEEP_PARAM) {
				sleep_tlv = (MrvlIEtypes_sleep_param_t *)tlv;
				sleep_tlv->header.type =
					wlan_cpu_to_le16
					(TLV_TYPE_AP_SLEEP_PARAM);
				sleep_tlv->header.len =
					wlan_cpu_to_le16(sizeof
							 (MrvlIEtypes_sleep_param_t)
							 -
							 sizeof
							 (MrvlIEtypesHeader_t));
				sleep_tlv->ctrl_bitmap =
					wlan_cpu_to_le32(ps_mgmt->sleep_param.
							 ctrl_bitmap);
				sleep_tlv->min_sleep =
					wlan_cpu_to_le32(ps_mgmt->sleep_param.
							 min_sleep);
				sleep_tlv->max_sleep =
					wlan_cpu_to_le32(ps_mgmt->sleep_param.
							 max_sleep);
				cmd_size += sizeof(MrvlIEtypes_sleep_param_t);
				tlv += sizeof(MrvlIEtypes_sleep_param_t);
			}
			if (ps_mgmt->flags & PS_FLAG_INACT_SLEEP_PARAM) {
				inact_tlv =
					(MrvlIEtypes_inact_sleep_param_t *)tlv;
				inact_tlv->header.type =
					wlan_cpu_to_le16
					(TLV_TYPE_AP_INACT_SLEEP_PARAM);
				inact_tlv->header.len =
					wlan_cpu_to_le16(sizeof
							 (MrvlIEtypes_inact_sleep_param_t)
							 -
							 sizeof
							 (MrvlIEtypesHeader_t));
				inact_tlv->inactivity_to =
					wlan_cpu_to_le32(ps_mgmt->inact_param.
							 inactivity_to);
				inact_tlv->min_awake =
					wlan_cpu_to_le32(ps_mgmt->inact_param.
							 min_awake);
				inact_tlv->max_awake =
					wlan_cpu_to_le32(ps_mgmt->inact_param.
							 max_awake);
				cmd_size +=
					sizeof(MrvlIEtypes_inact_sleep_param_t);
				tlv += sizeof(MrvlIEtypes_inact_sleep_param_t);
			}
		}
#endif
		cmd->size = wlan_cpu_to_le16(cmd_size);
	}

	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function handles the command response of ps_mode_enh
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param resp         A pointer to HostCmd_DS_COMMAND
 *  @param pioctl_buf   A pointer to mlan_ioctl_req structure
 *
 *  @return             MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_ret_enh_power_mode(pmlan_private pmpriv,
			HostCmd_DS_COMMAND *resp, mlan_ioctl_req *pioctl_buf)
{
	pmlan_adapter pmadapter = pmpriv->adapter;
	MrvlIEtypesHeader_t *mrvl_tlv = MNULL;
	MrvlIEtypes_auto_ds_param_t *auto_ds_tlv = MNULL;
	HostCmd_DS_802_11_PS_MODE_ENH *ps_mode = &resp->params.psmode_enh;

	ENTER();

	ps_mode->action = wlan_le16_to_cpu(ps_mode->action);
	PRINTM(MINFO, "CMD_RESP: PS_MODE cmd reply result=%#x action=0x%X\n",
	       resp->result, ps_mode->action);
	if (ps_mode->action == EN_AUTO_PS) {
		ps_mode->params.auto_ps.ps_bitmap =
			wlan_le16_to_cpu(ps_mode->params.auto_ps.ps_bitmap);
		if (ps_mode->params.auto_ps.ps_bitmap & BITMAP_AUTO_DS) {
			PRINTM(MCMND, "Enabled auto deep sleep\n");
			pmpriv->adapter->is_deep_sleep = MTRUE;
			mrvl_tlv = (MrvlIEtypesHeader_t *)((t_u8 *)ps_mode +
							   AUTO_PS_FIX_SIZE);
			while (wlan_le16_to_cpu(mrvl_tlv->type) !=
			       TLV_TYPE_AUTO_DS_PARAM) {
				mrvl_tlv =
					(MrvlIEtypesHeader_t
					 *)((t_u8 *)mrvl_tlv +
					    wlan_le16_to_cpu(mrvl_tlv->len) +
					    sizeof(MrvlIEtypesHeader_t));
			}
			auto_ds_tlv = (MrvlIEtypes_auto_ds_param_t *)mrvl_tlv;
			pmpriv->adapter->idle_time =
				wlan_le16_to_cpu(auto_ds_tlv->param.
						 deep_sleep_timeout);
		}
		if (ps_mode->params.auto_ps.ps_bitmap & BITMAP_STA_PS) {
			PRINTM(MCMND, "Enabled STA power save\n");
			if (pmadapter->sleep_period.period) {
				PRINTM(MCMND,
				       "Setting uapsd/pps mode to TRUE\n");
			}
		}
#if defined(UAP_SUPPORT)
		if (ps_mode->params.auto_ps.ps_bitmap &
		    (BITMAP_UAP_INACT_PS | BITMAP_UAP_DTIM_PS)) {
			pmadapter->ps_mode = Wlan802_11PowerModePSP;
			PRINTM(MCMND, "Enabled uAP power save\n");
		}
#endif
	} else if (ps_mode->action == DIS_AUTO_PS) {
		ps_mode->params.ps_bitmap =
			wlan_cpu_to_le16(ps_mode->params.ps_bitmap);
		if (ps_mode->params.ps_bitmap & BITMAP_AUTO_DS) {
			pmpriv->adapter->is_deep_sleep = MFALSE;
			PRINTM(MCMND, "Disabled auto deep sleep\n");
		}
		if (ps_mode->params.ps_bitmap & BITMAP_STA_PS) {
			PRINTM(MCMND, "Disabled STA power save\n");
			if (pmadapter->sleep_period.period) {
				pmadapter->delay_null_pkt = MFALSE;
				pmadapter->tx_lock_flag = MFALSE;
				pmadapter->pps_uapsd_mode = MFALSE;
			}
		}
#if defined(UAP_SUPPORT)
		if (ps_mode->params.ps_bitmap &
		    (BITMAP_UAP_INACT_PS | BITMAP_UAP_DTIM_PS)) {
			pmadapter->ps_mode = Wlan802_11PowerModeCAM;
			PRINTM(MCMND, "Disabled uAP power save\n");
		}
#endif
	} else if (ps_mode->action == GET_PS) {
		ps_mode->params.ps_bitmap =
			wlan_le16_to_cpu(ps_mode->params.ps_bitmap);
		if (ps_mode->params.auto_ps.ps_bitmap &
		    (BITMAP_STA_PS | BITMAP_UAP_INACT_PS | BITMAP_UAP_DTIM_PS))
			pmadapter->ps_mode = Wlan802_11PowerModePSP;
		else
			pmadapter->ps_mode = Wlan802_11PowerModeCAM;
		PRINTM(MCMND, "ps_bitmap=0x%x\n", ps_mode->params.ps_bitmap);
		if (pioctl_buf) {
			mlan_ds_pm_cfg *pm_cfg =
				(mlan_ds_pm_cfg *)pioctl_buf->pbuf;
			if (pm_cfg->sub_command == MLAN_OID_PM_CFG_IEEE_PS) {
				if (ps_mode->params.auto_ps.ps_bitmap &
				    BITMAP_STA_PS)
					pm_cfg->param.ps_mode = 1;
				else
					pm_cfg->param.ps_mode = 0;
			}
#if defined(UAP_SUPPORT)
			if (pm_cfg->sub_command == MLAN_OID_PM_CFG_PS_MODE) {
				MrvlIEtypes_sleep_param_t *sleep_tlv = MNULL;
				MrvlIEtypes_inact_sleep_param_t *inact_tlv =
					MNULL;
				MrvlIEtypesHeader_t *tlv = MNULL;
				t_u16 tlv_type = 0;
				t_u16 tlv_len = 0;
				t_u16 tlv_buf_left = 0;
				pm_cfg->param.ps_mgmt.flags = PS_FLAG_PS_MODE;
				if (ps_mode->params.ps_bitmap &
				    BITMAP_UAP_INACT_PS)
					pm_cfg->param.ps_mgmt.ps_mode =
						PS_MODE_INACTIVITY;
				else if (ps_mode->params.ps_bitmap &
					 BITMAP_UAP_DTIM_PS)
					pm_cfg->param.ps_mgmt.ps_mode =
						PS_MODE_PERIODIC_DTIM;
				else
					pm_cfg->param.ps_mgmt.ps_mode =
						PS_MODE_DISABLE;
				tlv_buf_left = resp->size -
					(S_DS_GEN + AUTO_PS_FIX_SIZE);
				tlv = (MrvlIEtypesHeader_t *)((t_u8 *)ps_mode +
							      AUTO_PS_FIX_SIZE);
				while (tlv_buf_left >=
				       sizeof(MrvlIEtypesHeader_t)) {
					tlv_type = wlan_le16_to_cpu(tlv->type);
					tlv_len = wlan_le16_to_cpu(tlv->len);
					switch (tlv_type) {
					case TLV_TYPE_AP_SLEEP_PARAM:
						sleep_tlv =
							(MrvlIEtypes_sleep_param_t
							 *)tlv;
						pm_cfg->param.ps_mgmt.flags |=
							PS_FLAG_SLEEP_PARAM;
						pm_cfg->param.ps_mgmt.
							sleep_param.
							ctrl_bitmap =
							wlan_le32_to_cpu
							(sleep_tlv->
							 ctrl_bitmap);
						pm_cfg->param.ps_mgmt.
							sleep_param.min_sleep =
							wlan_le32_to_cpu
							(sleep_tlv->min_sleep);
						pm_cfg->param.ps_mgmt.
							sleep_param.max_sleep =
							wlan_le32_to_cpu
							(sleep_tlv->max_sleep);
						break;
					case TLV_TYPE_AP_INACT_SLEEP_PARAM:
						inact_tlv =
							(MrvlIEtypes_inact_sleep_param_t
							 *)tlv;
						pm_cfg->param.ps_mgmt.flags |=
							PS_FLAG_INACT_SLEEP_PARAM;
						pm_cfg->param.ps_mgmt.
							inact_param.
							inactivity_to =
							wlan_le32_to_cpu
							(inact_tlv->
							 inactivity_to);
						pm_cfg->param.ps_mgmt.
							inact_param.min_awake =
							wlan_le32_to_cpu
							(inact_tlv->min_awake);
						pm_cfg->param.ps_mgmt.
							inact_param.max_awake =
							wlan_le32_to_cpu
							(inact_tlv->max_awake);
						break;
					}
					tlv_buf_left -=
						tlv_len +
						sizeof(MrvlIEtypesHeader_t);
					tlv = (MrvlIEtypesHeader_t
					       *)((t_u8 *)tlv +
						  tlv_len +
						  sizeof(MrvlIEtypesHeader_t));
				}
			}
#endif
		}
	}

	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function handles the command response of tx rate query
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param resp         A pointer to HostCmd_DS_COMMAND
 *  @param pioctl_buf   A pointer to mlan_ioctl_req structure
 *
 *  @return             MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_ret_802_11_tx_rate_query(pmlan_private pmpriv,
			      HostCmd_DS_COMMAND *resp,
			      mlan_ioctl_req *pioctl_buf)
{
	mlan_adapter *pmadapter = pmpriv->adapter;
	mlan_ds_rate *rate = MNULL;
	ENTER();

	pmpriv->tx_rate = resp->params.tx_rate.tx_rate;
	pmpriv->tx_rate_info = resp->params.tx_rate.tx_rate_info;
	if (pmpriv->adapter->pcard_info->v14_fw_api) {
		pmpriv->tx_rate_info =
			wlan_convert_v14_tx_rate_info(pmpriv,
						      pmpriv->tx_rate_info);
		PRINTM(MINFO,
		       "%s: v14_fw_api=%d tx_rate=%d tx_rate_info=0x%x->0x%x\n",
		       __func__, pmpriv->adapter->pcard_info->v14_fw_api,
		       pmpriv->tx_rate, resp->params.tx_rate.tx_rate_info,
		       pmpriv->tx_rate_info);
	}
	if ((pmpriv->tx_rate_info & 0x3) == MLAN_RATE_FORMAT_HE)
		pmpriv->ext_tx_rate_info =
			resp->params.tx_rate.ext_tx_rate_info;
	else
		pmpriv->ext_tx_rate_info = 0;

	if (!pmpriv->is_data_rate_auto) {
		pmpriv->data_rate =
			wlan_index_to_data_rate(pmadapter, pmpriv->tx_rate,
						pmpriv->tx_rate_info,
						pmpriv->ext_tx_rate_info);
	}

	if (pioctl_buf) {
		rate = (mlan_ds_rate *)pioctl_buf->pbuf;
		if (rate->sub_command == MLAN_OID_RATE_CFG) {
			if (rate->param.rate_cfg.rate_type == MLAN_RATE_INDEX) {
				if ((pmpriv->tx_rate_info & 0x3) ==
				    MLAN_RATE_FORMAT_VHT
				    || ((pmpriv->tx_rate_info & 0x3) ==
					MLAN_RATE_FORMAT_HE)
					)

					/* VHT rate */
					rate->param.rate_cfg.rate =
						(pmpriv->tx_rate) & 0xF;
				else if ((pmpriv->tx_rate_info & 0x3) ==
					 MLAN_RATE_FORMAT_HT)
					/* HT rate */
					rate->param.rate_cfg.rate =
						pmpriv->tx_rate +
						MLAN_RATE_INDEX_MCS0;
				else
					/* LG rate */
					/* For HostCmd_CMD_802_11_TX_RATE_QUERY,
					 * there is a hole (0x4) in rate table
					 * between HR/DSSS and OFDM rates,
					 * so minus 1 for OFDM rate index */
					rate->param.rate_cfg.rate =
						(pmpriv->tx_rate >
						 MLAN_RATE_INDEX_OFDM0) ?
						pmpriv->tx_rate - 1 :
						pmpriv->tx_rate;
			} else {
				/* rate_type = MLAN_RATE_VALUE */
				rate->param.rate_cfg.rate =
					wlan_index_to_data_rate(pmadapter,
								pmpriv->tx_rate,
								pmpriv->
								tx_rate_info,
								pmpriv->
								ext_tx_rate_info);
			}
		} else if (rate->sub_command == MLAN_OID_GET_DATA_RATE) {
			/* Tx rate info */
			if ((pmpriv->tx_rate_info & 0x3) == MLAN_RATE_FORMAT_VHT
			    ||
			    (pmpriv->tx_rate_info & 0x3) ==
			    MLAN_RATE_FORMAT_HE) {
				/* AX/VHT rate */
				rate->param.data_rate.tx_rate_format =
					pmpriv->tx_rate_info & 0x3;
				rate->param.data_rate.tx_ht_bw =
					(pmpriv->tx_rate_info & 0xC) >> 2;
				if ((pmpriv->tx_rate_info & 0x3) ==
				    MLAN_RATE_FORMAT_HE)
					rate->param.data_rate.tx_ht_gi =
						(pmpriv->tx_rate_info & 0x10) >>
						4 |
						(pmpriv->tx_rate_info & 0x80) >>
						6;
				else
					rate->param.data_rate.tx_ht_gi =
						(pmpriv->tx_rate_info & 0x10) >>
						4;
				rate->param.data_rate.tx_nss =
					((pmpriv->tx_rate) >> 4) & 0x03;
				rate->param.data_rate.tx_mcs_index =
					(pmpriv->tx_rate) & 0xF;
				if ((pmpriv->tx_rate_info & 0x3) ==
				    MLAN_RATE_FORMAT_VHT
				    || (pmpriv->tx_rate_info & 0x3) ==
				    MLAN_RATE_FORMAT_HE)
					rate->param.data_rate.tx_data_rate =
						wlan_index_to_data_rate
						(pmadapter, pmpriv->tx_rate,
						 pmpriv->tx_rate_info,
						 pmpriv->ext_tx_rate_info);
			} else if ((pmpriv->tx_rate_info & 0x3) ==
				   MLAN_RATE_FORMAT_HT) {
				/* HT rate */
				rate->param.data_rate.tx_rate_format =
					MLAN_RATE_FORMAT_HT;
				rate->param.data_rate.tx_ht_bw =
					(pmpriv->tx_rate_info & 0xC) >> 2;

				rate->param.data_rate.tx_ht_gi =
					(pmpriv->tx_rate_info & 0x10) >> 4;
				rate->param.data_rate.tx_mcs_index =
					pmpriv->tx_rate;
				rate->param.data_rate.tx_data_rate =
					wlan_index_to_data_rate(pmadapter,
								pmpriv->tx_rate,
								pmpriv->
								tx_rate_info,
								pmpriv->
								ext_tx_rate_info);
			} else {
				/* LG rate */
				rate->param.data_rate.tx_rate_format =
					MLAN_RATE_FORMAT_LG;
				/* For HostCmd_CMD_802_11_TX_RATE_QUERY,
				 * there is a hole in rate table
				 * between HR/DSSS and OFDM rates,
				 * so minus 1 for OFDM rate index */
				rate->param.data_rate.tx_data_rate =
					(pmpriv->tx_rate >
					 MLAN_RATE_INDEX_OFDM0) ?
					pmpriv->tx_rate - 1 : pmpriv->tx_rate;
			}

			/* Rx rate info */
			if ((pmpriv->rxpd_rate_info & 0x3) ==
			    MLAN_RATE_FORMAT_VHT
			    || (pmpriv->rxpd_rate_info & 0x3) ==
			    MLAN_RATE_FORMAT_HE) {
				/* VHT rate */
				rate->param.data_rate.rx_rate_format =
					pmpriv->rxpd_rate_info & 0x3;
				rate->param.data_rate.rx_ht_bw =
					(pmpriv->rxpd_rate_info & 0xC) >> 2;
				if ((pmpriv->rxpd_rate_info & 0x3) ==
				    MLAN_RATE_FORMAT_HE)
					rate->param.data_rate.rx_ht_gi =
						(pmpriv->rxpd_rate_info &
						 0x10) >>
						4 |
						(pmpriv->rxpd_rate_info &
						 0x80) >> 6;
				else
					rate->param.data_rate.rx_ht_gi =
						(pmpriv->rxpd_rate_info &
						 0x10) >> 4;
				rate->param.data_rate.rx_nss =
					((pmpriv->rxpd_rate) >> 4) & 0x3;
				rate->param.data_rate.rx_mcs_index =
					(pmpriv->rxpd_rate) & 0xF;
				if ((pmpriv->rxpd_rate_info & 0x3) ==
				    MLAN_RATE_FORMAT_VHT
				    || (pmpriv->rxpd_rate_info & 0x3) ==
				    MLAN_RATE_FORMAT_HE)
					rate->param.data_rate.rx_data_rate =
						wlan_index_to_data_rate
						(pmadapter, pmpriv->rxpd_rate,
						 pmpriv->rxpd_rate_info,
						 pmpriv->rxpd_rx_info);
			} else if ((pmpriv->rxpd_rate_info & 0x3) ==
				   MLAN_RATE_FORMAT_HT) {
				/* HT rate */
				rate->param.data_rate.rx_rate_format =
					MLAN_RATE_FORMAT_HT;
				rate->param.data_rate.rx_ht_bw =
					(pmpriv->rxpd_rate_info & 0xC) >> 2;
				rate->param.data_rate.rx_ht_gi =
					(pmpriv->rxpd_rate_info & 0x10) >> 4;
				rate->param.data_rate.rx_mcs_index =
					pmpriv->rxpd_rate;
				rate->param.data_rate.rx_data_rate =
					wlan_index_to_data_rate(pmadapter,
								pmpriv->
								rxpd_rate,
								pmpriv->
								rxpd_rate_info,
								0);
			} else {
				/* LG rate */
				rate->param.data_rate.rx_rate_format =
					MLAN_RATE_FORMAT_LG;
				/* For rate index in RxPD,
				 * there is a hole in rate table
				 * between HR/DSSS and OFDM rates,
				 * so minus 1 for OFDM rate index */
				rate->param.data_rate.rx_data_rate =
					(pmpriv->rxpd_rate >
					 MLAN_RATE_INDEX_OFDM0) ?
					pmpriv->rxpd_rate - 1 :
					pmpriv->rxpd_rate;
			}
		}
		pioctl_buf->data_read_written =
			sizeof(mlan_data_rate) + MLAN_SUB_COMMAND_SIZE;
	}
	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 * @brief This function prepares command of robustcoex.
 *
 * @param pmpriv       A pointer to mlan_private structure
 * @param cmd          A pointer to HostCmd_DS_COMMAND structure
 * @param cmd_action   The action: GET or SET
 * @param pdata_buf    A pointer to data buffer
 *
 * @return             MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_cmd_robustcoex(pmlan_private pmpriv,
		    HostCmd_DS_COMMAND *cmd, t_u16 cmd_action, t_u16 *pdata_buf)
{
	HostCmd_DS_802_11_ROBUSTCOEX *rbstcx = &cmd->params.robustcoexparams;
	mlan_ds_misc_robustcoex_params *robustcoex_params = MNULL;
	MrvlIEtypes_RobustcoexSourceGPIO_t *tlv =
		(MrvlIEtypes_RobustcoexSourceGPIO_t *) (rbstcx->tlv_buf);

	ENTER();

	cmd->command = wlan_cpu_to_le16(HostCmd_CMD_802_11_ROBUSTCOEX);
	cmd->size = sizeof(HostCmd_DS_802_11_ROBUSTCOEX) + S_DS_GEN;
	rbstcx->action = wlan_cpu_to_le16(cmd_action);
	switch (cmd_action) {
	case HostCmd_ACT_GEN_SET:
		robustcoex_params =
			(mlan_ds_misc_robustcoex_params *) pdata_buf;
		if (robustcoex_params->method == ROBUSTCOEX_GPIO_CFG) {
			cmd->size += sizeof(MrvlIEtypes_RobustcoexSourceGPIO_t);
			tlv->header.type =
				wlan_cpu_to_le16(TLV_TYPE_ROBUSTCOEX);
			tlv->header.len =
				wlan_cpu_to_le16(sizeof
						 (MrvlIEtypes_RobustcoexSourceGPIO_t)
						 - sizeof(MrvlIEtypesHeader_t));
			tlv->enable = (t_u8)robustcoex_params->enable;
			tlv->gpio_num = (t_u8)robustcoex_params->gpio_num;
			tlv->gpio_polarity =
				(t_u8)robustcoex_params->gpio_polarity;
		}
		break;
	case HostCmd_ACT_GEN_GET:
	default:
		break;
	}
	cmd->size = wlan_cpu_to_le16(cmd->size);
	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

#if defined(PCIE)
/**
 * @brief This function enables SSU support.
 *
 * @param pmpriv       A pointer to mlan_private structure
 * @param cmd          A pointer to HostCmd_DS_COMMAND structure
 * @param cmd_action   The action: GET or SET
 * @param pdata_buf    A pointer to data buffer
 *
 * @return             MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_cmd_ssu(pmlan_private pmpriv, HostCmd_DS_COMMAND *cmd,
	     t_u16 cmd_action, t_u16 *pdata_buf)
{
	HostCmd_DS_SSU_CFG *ssu_cfg_cmd = &cmd->params.ssu_params;
	mlan_ds_ssu_params *ssu_params = MNULL;
	mlan_status ret = MLAN_STATUS_SUCCESS;
	mlan_adapter *pmadapter = pmpriv->adapter;

	ENTER();

	cmd->command = wlan_cpu_to_le16(HostCmd_CMD_SSU);
	cmd->size = sizeof(HostCmd_DS_SSU_CFG) + S_DS_GEN;
	ssu_cfg_cmd->action = wlan_cpu_to_le16(cmd_action);
	switch (cmd_action) {
	case HostCmd_ACT_GEN_SET:
	case HostCmd_ACT_GEN_SET_DEFAULT:
		ssu_params = (mlan_ds_ssu_params *) pdata_buf;
		ssu_cfg_cmd->nskip = wlan_cpu_to_le32(ssu_params->nskip);
		ssu_cfg_cmd->nsel = wlan_cpu_to_le32(ssu_params->nsel);
		ssu_cfg_cmd->adcdownsample =
			wlan_cpu_to_le32(ssu_params->adcdownsample);
		ssu_cfg_cmd->mask_adc_pkt =
			wlan_cpu_to_le32(ssu_params->mask_adc_pkt);
		ssu_cfg_cmd->out_16bits =
			wlan_cpu_to_le32(ssu_params->out_16bits);
		ssu_cfg_cmd->spec_pwr_enable =
			wlan_cpu_to_le32(ssu_params->spec_pwr_enable);
		ssu_cfg_cmd->rate_deduction =
			wlan_cpu_to_le32(ssu_params->rate_deduction);
		ssu_cfg_cmd->n_pkt_avg =
			wlan_cpu_to_le32(ssu_params->n_pkt_avg);
		/* Initialize PCIE ring buffer */
		ret = wlan_alloc_ssu_pcie_buf(pmadapter);
		if (MLAN_STATUS_SUCCESS != ret) {
			PRINTM(MERROR,
			       "Failed to allocate PCIE host buffers for SSU\n");
			LEAVE();
			return MLAN_STATUS_FAILURE;
		}
		ssu_cfg_cmd->buffer_base_addr[0] =
			wlan_cpu_to_le32((t_u32)pmadapter->ssu_buf->buf_pa);
		ssu_cfg_cmd->buffer_base_addr[1] = wlan_cpu_to_le32((t_u32)
								    ((t_u64)
								     (pmadapter->
								      ssu_buf->
								      buf_pa >>
								      32)));
		ssu_cfg_cmd->buffer_pool_size =
			wlan_cpu_to_le32(MLAN_SSU_BUF_SIZE);
		break;
	case HostCmd_ACT_GEN_GET:
	default:
		break;
	}
	cmd->size = wlan_cpu_to_le16(cmd->size);
	LEAVE();
	return MLAN_STATUS_SUCCESS;
}
#endif

/**
 * @brief This function prepares command of dmcs config.
 *
 * @param pmpriv       A pointer to mlan_private structure
 * @param cmd          A pointer to HostCmd_DS_COMMAND structure
 * @param cmd_action   The action: GET or SET
 * @param pdata_buf    A pointer to data buffer
 *
 * @return             MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_cmd_dmcs_config(pmlan_private pmpriv,
		     HostCmd_DS_COMMAND *cmd,
		     t_u16 cmd_action, t_void *pdata_buf)
{
	HostCmd_DS_DMCS_CFG *dmcs = &cmd->params.dmcs;
	mlan_ds_misc_mapping_policy *dmcs_params = MNULL;
	t_u8 *mapping_policy = (t_u8 *)dmcs->tlv_buf;

	ENTER();

	cmd->command = wlan_cpu_to_le16(HostCmd_CMD_DMCS_CONFIG);
	cmd->size = sizeof(HostCmd_DS_DMCS_CFG) + S_DS_GEN;
	dmcs->action = wlan_cpu_to_le16(cmd_action);
	dmcs_params = (mlan_ds_misc_mapping_policy *) pdata_buf;
	dmcs->subcmd = wlan_cpu_to_le16(dmcs_params->subcmd);
	switch (dmcs->subcmd) {
	case 0:
		cmd->size += sizeof(t_u8);
		*mapping_policy = dmcs_params->mapping_policy;
		break;
	case 1:
	default:
		break;
	}
	cmd->size = wlan_cpu_to_le16(cmd->size);
	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function handles the command response of dmcs config
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param resp         A pointer to HostCmd_DS_COMMAND
 *  @param pioctl_buf   A pointer to mlan_ioctl_req structure
 *
 *  @return             MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
mlan_status
wlan_ret_dmcs_config(pmlan_private pmpriv,
		     HostCmd_DS_COMMAND *resp, mlan_ioctl_req *pioctl_buf)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	HostCmd_DS_DMCS_CFG *dmcs = &resp->params.dmcs;
	MrvlIEtypes_DmcsStatus_t *dmcs_status;
	mlan_ds_misc_cfg *cfg = MNULL;
	t_u16 tlv_buf_left = 0;
	t_u16 tlv_type = 0, tlv_len = 0;
	MrvlIEtypesHeader_t *tlv = MNULL;
	int i = 0;

	ENTER();
	if (pioctl_buf) {
		cfg = (mlan_ds_misc_cfg *)pioctl_buf->pbuf;
		tlv = (MrvlIEtypesHeader_t *)((t_u8 *)dmcs +
					      sizeof(HostCmd_DS_DMCS_CFG));
		tlv_buf_left =
			resp->size - (sizeof(HostCmd_DS_DMCS_CFG) + S_DS_GEN);
		while (tlv_buf_left > sizeof(MrvlIEtypesHeader_t)) {
			tlv_type = wlan_le16_to_cpu(tlv->type);
			tlv_len = wlan_le16_to_cpu(tlv->len);
			if (tlv_buf_left <
			    (tlv_len + sizeof(MrvlIEtypesHeader_t))) {
				PRINTM(MERROR,
				       "Error while processing DMCS status tlv, bytes_left < TLV len\n");
				ret = MLAN_STATUS_FAILURE;
				break;
			}
			switch (tlv_type) {
			case TLV_TYPE_DMCS_STATUS:
				dmcs_status = (MrvlIEtypes_DmcsStatus_t *) tlv;
				cfg->param.dmcs_status.mapping_policy =
					dmcs_status->mapping_policy;
				memset(pmpriv->adapter,
				       &cfg->param.dmcs_status.radio_status, 0,
				       sizeof(dmcsStatus_t));
				for (i = 0; i < MAX_NUM_MAC; i++) {
					memcpy_ext(pmpriv->adapter,
						   &cfg->param.dmcs_status.
						   radio_status[i],
						   &dmcs_status->
						   radio_status[i],
						   sizeof(dmcsStatus_t),
						   sizeof(dmcsStatus_t));
				}
				break;
			default:
				break;
			}
			tlv_buf_left -= tlv_len + sizeof(MrvlIEtypesHeader_t);
			tlv = (MrvlIEtypesHeader_t
			       *)((t_u8 *)tlv + tlv_len +
				  sizeof(MrvlIEtypesHeader_t));
		}
		pioctl_buf->data_read_written =
			sizeof(mlan_ds_misc_dmcs_status);
	}
	LEAVE();
	return ret;
}

/**
 *  @brief This function prepares command of tx_rate_cfg.
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param cmd          A pointer to HostCmd_DS_COMMAND structure
 *  @param cmd_action   The action: GET or SET
 *  @param pdata_buf    A pointer to data buffer
 *  @param pioctl_buf	A pointer to ioctl buffer
 *
 *  @return             MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_cmd_tx_rate_cfg(pmlan_private pmpriv,
		     HostCmd_DS_COMMAND *cmd,
		     t_u16 cmd_action, t_void *pdata_buf,
		     mlan_ioctl_req *pioctl_buf)
{
	HostCmd_DS_TX_RATE_CFG *rate_cfg =
		(HostCmd_DS_TX_RATE_CFG *)&(cmd->params.tx_rate_cfg);
	MrvlRateScope_t *rate_scope;
	MrvlRateDropPattern_t *rate_drop;
	MrvlIETypes_rate_setting_t *rate_setting_tlv;
	mlan_ds_rate *ds_rate = MNULL;
	t_u16 *pbitmap_rates = (t_u16 *)pdata_buf;

	t_u32 i;

	ENTER();

	cmd->command = wlan_cpu_to_le16(HostCmd_CMD_TX_RATE_CFG);

	rate_cfg->action = wlan_cpu_to_le16(cmd_action);
	rate_cfg->cfg_index = 0;

	rate_scope = (MrvlRateScope_t *)rate_cfg->tlv_buf;
	rate_scope->type = wlan_cpu_to_le16(TLV_TYPE_RATE_SCOPE);
	rate_scope->length = wlan_cpu_to_le16(sizeof(MrvlRateScope_t) -
					      sizeof(MrvlIEtypesHeader_t));
	if (pbitmap_rates != MNULL) {
		rate_scope->hr_dsss_rate_bitmap =
			wlan_cpu_to_le16(pbitmap_rates[0]);
		rate_scope->ofdm_rate_bitmap =
			wlan_cpu_to_le16(pbitmap_rates[1]);
		for (i = 0; i < NELEMENTS(rate_scope->ht_mcs_rate_bitmap); i++)
			rate_scope->ht_mcs_rate_bitmap[i] =
				wlan_cpu_to_le16(pbitmap_rates[2 + i]);
		for (i = 0; i < NELEMENTS(rate_scope->vht_mcs_rate_bitmap); i++)
			rate_scope->vht_mcs_rate_bitmap[i] =
				wlan_cpu_to_le16(pbitmap_rates
						 [2 +
						  NELEMENTS(rate_scope->
							    ht_mcs_rate_bitmap)
						  + i]);
		if (IS_FW_SUPPORT_11AX(pmpriv->adapter)) {
			for (i = 0;
			     i < NELEMENTS(rate_scope->he_mcs_rate_bitmap); i++)
				rate_scope->he_mcs_rate_bitmap
					[i] = wlan_cpu_to_le16(pbitmap_rates
							       [2 +
								wlan_get_bitmap_index
								(rate_scope)
								+ i]);
		} else {
			rate_scope->length =
				wlan_cpu_to_le16(sizeof(MrvlRateScope_t) -
						 sizeof(rate_scope->
							he_mcs_rate_bitmap) -
						 sizeof(MrvlIEtypesHeader_t));
		}
	} else {
		rate_scope->hr_dsss_rate_bitmap =
			wlan_cpu_to_le16(pmpriv->bitmap_rates[0]);
		rate_scope->ofdm_rate_bitmap =
			wlan_cpu_to_le16(pmpriv->bitmap_rates[1]);
		for (i = 0; i < NELEMENTS(rate_scope->ht_mcs_rate_bitmap); i++)
			rate_scope->ht_mcs_rate_bitmap[i] =
				wlan_cpu_to_le16(pmpriv->bitmap_rates[2 + i]);
		for (i = 0; i < NELEMENTS(rate_scope->vht_mcs_rate_bitmap); i++)
			rate_scope->vht_mcs_rate_bitmap[i] =
				wlan_cpu_to_le16(pmpriv->
						 bitmap_rates[2 +
							      NELEMENTS
							      (rate_scope->
							       ht_mcs_rate_bitmap)
							      + i]);
		if (IS_FW_SUPPORT_11AX(pmpriv->adapter)) {
			for (i = 0;
			     i < NELEMENTS(rate_scope->vht_mcs_rate_bitmap);
			     i++)
				rate_scope->he_mcs_rate_bitmap
					[i] =
					wlan_cpu_to_le16(pmpriv->
							 bitmap_rates[2 +
								      wlan_get_bitmap_index
								      (rate_scope)
								      + i]);
		} else {
			rate_scope->length =
				wlan_cpu_to_le16(sizeof(MrvlRateScope_t) -
						 sizeof(rate_scope->
							he_mcs_rate_bitmap) -
						 sizeof(MrvlIEtypesHeader_t));
		}
	}

	rate_drop =
		(MrvlRateDropPattern_t *)((t_u8 *)rate_scope +
					  wlan_le16_to_cpu(rate_scope->length) +
					  sizeof(MrvlIEtypesHeader_t));
	rate_drop->type = wlan_cpu_to_le16(TLV_TYPE_RATE_DROP_PATTERN);
	rate_drop->length = wlan_cpu_to_le16(sizeof(rate_drop->rate_drop_mode));
	rate_drop->rate_drop_mode = 0;

	cmd->size =
		wlan_cpu_to_le16(S_DS_GEN + sizeof(HostCmd_DS_TX_RATE_CFG) +
				 rate_scope->length +
				 sizeof(MrvlIEtypesHeader_t) +
				 sizeof(MrvlRateDropPattern_t));
	if (pioctl_buf && pmpriv->adapter->pcard_info->v17_fw_api) {
		ds_rate = (mlan_ds_rate *)pioctl_buf->pbuf;
		rate_setting_tlv = (MrvlIETypes_rate_setting_t
				    *) ((t_u8 *)rate_drop +
					sizeof(MrvlRateDropPattern_t));
		rate_setting_tlv->header.type =
			wlan_cpu_to_le16(TLV_TYPE_TX_RATE_CFG);
		rate_setting_tlv->header.len =
			wlan_cpu_to_le16(sizeof
					 (rate_setting_tlv->rate_setting));
		rate_setting_tlv->rate_setting =
			wlan_cpu_to_le16(ds_rate->param.rate_cfg.rate_setting);
		PRINTM(MCMND, "he rate setting = %d\n",
		       rate_setting_tlv->rate_setting);
		cmd->size =
			wlan_cpu_to_le16(S_DS_GEN +
					 sizeof(HostCmd_DS_TX_RATE_CFG) +
					 rate_scope->length +
					 sizeof(MrvlIEtypesHeader_t) +
					 sizeof(MrvlRateDropPattern_t) +
					 sizeof(MrvlIETypes_rate_setting_t));
	}
	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function handles the command response of tx_rate_cfg
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param resp         A pointer to HostCmd_DS_COMMAND
 *  @param pioctl_buf   A pointer to mlan_ioctl_req structure
 *
 *  @return             MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
mlan_status
wlan_ret_tx_rate_cfg(pmlan_private pmpriv,
		     HostCmd_DS_COMMAND *resp, mlan_ioctl_req *pioctl_buf)
{
	mlan_adapter *pmadapter = pmpriv->adapter;
	mlan_ds_rate *ds_rate = MNULL;
	HostCmd_DS_TX_RATE_CFG *prate_cfg = MNULL;
	MrvlRateScope_t *prate_scope;
	MrvlIEtypesHeader_t *head = MNULL;
	t_u16 tlv, tlv_buf_len = 0;
	t_u8 *tlv_buf;
	t_u32 i;
	t_s32 index;
	mlan_status ret = MLAN_STATUS_SUCCESS;
	MrvlIETypes_rate_setting_t *rate_setting_tlv = MNULL;
	t_u16 rate_setting = 0xffff;

	ENTER();

	if (resp == MNULL) {
		LEAVE();
		return MLAN_STATUS_FAILURE;
	}
	prate_cfg = (HostCmd_DS_TX_RATE_CFG *)&(resp->params.tx_rate_cfg);

	tlv_buf = (t_u8 *)prate_cfg->tlv_buf;
	if (tlv_buf) {
		tlv_buf_len = resp->size -
			(sizeof(HostCmd_DS_TX_RATE_CFG) + S_DS_GEN);
		tlv_buf_len = wlan_le16_to_cpu(tlv_buf_len);
	}

	while (tlv_buf && tlv_buf_len > 0) {
		tlv = (*tlv_buf);
		tlv = tlv | (*(tlv_buf + 1) << 8);

		switch (tlv) {
		case TLV_TYPE_RATE_SCOPE:
			prate_scope = (MrvlRateScope_t *)tlv_buf;
			pmpriv->bitmap_rates[0] =
				wlan_le16_to_cpu(prate_scope->
						 hr_dsss_rate_bitmap);
			pmpriv->bitmap_rates[1] =
				wlan_le16_to_cpu(prate_scope->ofdm_rate_bitmap);
			for (i = 0;
			     i < NELEMENTS(prate_scope->ht_mcs_rate_bitmap);
			     i++)
				pmpriv->bitmap_rates[2 + i] =
					wlan_le16_to_cpu(prate_scope->
							 ht_mcs_rate_bitmap[i]);
			for (i = 0;
			     i < NELEMENTS(prate_scope->vht_mcs_rate_bitmap);
			     i++)
				pmpriv->bitmap_rates[2 +
						     sizeof(prate_scope->
							    ht_mcs_rate_bitmap)
						     / sizeof(t_u16) + i] =
					wlan_le16_to_cpu(prate_scope->
							 vht_mcs_rate_bitmap
							 [i]);
			if (IS_FW_SUPPORT_11AX(pmadapter)) {
				for (i = 0;
				     i <
				     NELEMENTS(prate_scope->he_mcs_rate_bitmap);
				     i++)
					pmpriv->bitmap_rates
						[2 +
						 sizeof(prate_scope->
							ht_mcs_rate_bitmap) /
						 sizeof(t_u16) +
						 sizeof(prate_scope->
							vht_mcs_rate_bitmap)
						 / sizeof(t_u16) + i] =
						wlan_le16_to_cpu(prate_scope->
								 he_mcs_rate_bitmap
								 [i]);
			}
			break;
		case TLV_TYPE_TX_RATE_CFG:
			rate_setting_tlv =
				(MrvlIETypes_rate_setting_t *) tlv_buf;
			rate_setting = rate_setting_tlv->rate_setting;
			break;
			/* Add RATE_DROP tlv here */
		}

		head = (MrvlIEtypesHeader_t *)tlv_buf;
		head->len = wlan_le16_to_cpu(head->len);
		tlv_buf += head->len + sizeof(MrvlIEtypesHeader_t);
		tlv_buf_len -= (head->len + sizeof(MrvlIEtypesHeader_t));
	}

	pmpriv->is_data_rate_auto = wlan_is_rate_auto(pmpriv);

	if (pmpriv->is_data_rate_auto) {
		pmpriv->data_rate = 0;
	} else {
		ret = wlan_prepare_cmd(pmpriv, HostCmd_CMD_802_11_TX_RATE_QUERY,
				       HostCmd_ACT_GEN_GET, 0, MNULL, MNULL);
	}

	if (pioctl_buf) {
		ds_rate = (mlan_ds_rate *)pioctl_buf->pbuf;
		if (ds_rate == MNULL) {
			PRINTM(MERROR, "Request buffer not found!\n");
			LEAVE();
			return MLAN_STATUS_FAILURE;
		}
		if (pmpriv->is_data_rate_auto) {
			ds_rate->param.rate_cfg.is_rate_auto = MTRUE;
			ds_rate->param.rate_cfg.rate_format =
				MLAN_RATE_FORMAT_AUTO;
		} else {
			ds_rate->param.rate_cfg.is_rate_auto = MFALSE;
			/* check the LG rate */
			index = wlan_get_rate_index(pmadapter,
						    &pmpriv->bitmap_rates[0],
						    4);
			if (index != -1) {
				if ((index >= MLAN_RATE_BITMAP_OFDM0) &&
				    (index <= MLAN_RATE_BITMAP_OFDM7))
					index -= (MLAN_RATE_BITMAP_OFDM0 -
						  MLAN_RATE_INDEX_OFDM0);
				ds_rate->param.rate_cfg.rate_format =
					MLAN_RATE_FORMAT_LG;
				ds_rate->param.rate_cfg.rate = index;
			}
			/* check the HT rate */
			index = wlan_get_rate_index(pmadapter,
						    &pmpriv->bitmap_rates[2],
						    16);
			if (index != -1) {
				ds_rate->param.rate_cfg.rate_format =
					MLAN_RATE_FORMAT_HT;
				ds_rate->param.rate_cfg.rate = index;
			}
			/* check the VHT rate */
			index = wlan_get_rate_index(pmadapter,
						    &pmpriv->bitmap_rates[10],
						    16);
			if (index != -1) {
				ds_rate->param.rate_cfg.rate_format =
					MLAN_RATE_FORMAT_VHT;
				ds_rate->param.rate_cfg.rate = index % 16;
				ds_rate->param.rate_cfg.nss = index / 16;
				ds_rate->param.rate_cfg.nss += MLAN_RATE_NSS1;
			}
			/* check the HE rate */
			if (IS_FW_SUPPORT_11AX(pmadapter)) {
				index = wlan_get_rate_index(pmadapter,
							    &pmpriv->
							    bitmap_rates[18],
							    16);
				if (index != -1) {
					ds_rate->param.rate_cfg.rate_format =
						MLAN_RATE_FORMAT_HE;
					ds_rate->param.rate_cfg.rate =
						index % 16;
					ds_rate->param.rate_cfg.nss =
						index / 16;
					ds_rate->param.rate_cfg.nss +=
						MLAN_RATE_NSS1;
				}
			}
			ds_rate->param.rate_cfg.rate_setting = rate_setting;
			PRINTM(MINFO, "Rate index is %d\n",
			       ds_rate->param.rate_cfg.rate);
		}
		for (i = 0; i < MAX_BITMAP_RATES_SIZE; i++) {
			ds_rate->param.rate_cfg.bitmap_rates[i] =
				pmpriv->bitmap_rates[i];
		}
	}

	LEAVE();
	return ret;
}

/**
 *  @brief  This function issues adapter specific commands
 *          to initialize firmware
 *
 *  @param pmadapter    A pointer to mlan_adapter structure
 *
 *  @return             MLAN_STATUS_PENDING or MLAN_STATUS_FAILURE
 */
mlan_status
wlan_adapter_get_hw_spec(pmlan_adapter pmadapter)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	pmlan_private priv = wlan_get_priv(pmadapter, MLAN_BSS_ROLE_ANY);
#if defined(SDIO)
	/*
	 * This should be issued in the very first to config
	 *   SDIO_GPIO interrupt mode.
	 */
	if (IS_SD(pmadapter->card_type) &&
	    (wlan_set_sdio_gpio_int(priv) != MLAN_STATUS_SUCCESS)) {
		ret = MLAN_STATUS_FAILURE;
		goto done;
	}
#endif

#ifdef PCIE
	if (IS_PCIE(pmadapter->card_type) &&
	    (MLAN_STATUS_SUCCESS != wlan_set_pcie_buf_config(priv))) {
		ret = MLAN_STATUS_FAILURE;
		goto done;
	}
#endif

	ret = wlan_prepare_cmd(priv, HostCmd_CMD_FUNC_INIT, HostCmd_ACT_GEN_SET,
			       0, MNULL, MNULL);
	if (ret) {
		ret = MLAN_STATUS_FAILURE;
		goto done;
	}
	/** DPD data dnld cmd prepare */
	if ((pmadapter->pdpd_data) && (pmadapter->dpd_data_len > 0)) {
		ret = wlan_process_hostcmd_cfg(priv, CFG_TYPE_DPDFILE,
					       pmadapter->pdpd_data,
					       pmadapter->dpd_data_len);
		if (ret) {
			ret = MLAN_STATUS_FAILURE;
			goto done;
		}
		pmadapter->pdpd_data = MNULL;
		pmadapter->dpd_data_len = 0;
	}
	if ((pmadapter->ptxpwr_data) && (pmadapter->txpwr_data_len > 0)) {
		ret = wlan_process_hostcmd_cfg(priv, CFG_TYPE_HOSTCMD,
					       pmadapter->ptxpwr_data,
					       pmadapter->txpwr_data_len);
		if (ret) {
			ret = MLAN_STATUS_FAILURE;
			goto done;
		}
		pmadapter->ptxpwr_data = MNULL;
		pmadapter->txpwr_data_len = 0;
	}
	if (!pmadapter->pdpd_data &&
	    (pmadapter->dpd_data_len == UNKNOW_DPD_LENGTH)) {
		ret = wlan_prepare_cmd(priv, HostCmd_CMD_CFG_DATA,
				       HostCmd_ACT_GEN_GET, OID_TYPE_DPD, MNULL,
				       MNULL);
		if (ret) {
			ret = MLAN_STATUS_FAILURE;
			goto done;
		}
	}
	/** Cal data dnld cmd prepare */
	if ((pmadapter->pcal_data) && (pmadapter->cal_data_len > 0)) {
		ret = wlan_prepare_cmd(priv, HostCmd_CMD_CFG_DATA,
				       HostCmd_ACT_GEN_SET, OID_TYPE_CAL, MNULL,
				       MNULL);
		if (ret) {
			ret = MLAN_STATUS_FAILURE;
			goto done;
		}
		pmadapter->pcal_data = MNULL;
		pmadapter->cal_data_len = 0;
	}
	/* Get FW region and cfp tables */
	ret = wlan_prepare_cmd(priv, HostCmd_CMD_CHAN_REGION_CFG,
			       HostCmd_ACT_GEN_GET, 0, MNULL, MNULL);
	if (ret) {
		ret = MLAN_STATUS_FAILURE;
		goto done;
	}
	/*
	 * Get HW spec
	 */
	ret = wlan_prepare_cmd(priv, HostCmd_CMD_GET_HW_SPEC,
			       HostCmd_ACT_GEN_GET, 0, MNULL, MNULL);
	if (ret) {
		ret = MLAN_STATUS_FAILURE;
		goto done;
	}
	ret = MLAN_STATUS_PENDING;
done:
	LEAVE();
	return ret;
}

/**
 *  @brief  This function issues adapter specific commands
 *          to initialize firmware
 *
 *  @param pmadapter    A pointer to mlan_adapter structure
 *
 *  @return             MLAN_STATUS_PENDING or MLAN_STATUS_FAILURE
 */
mlan_status
wlan_adapter_init_cmd(pmlan_adapter pmadapter)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	pmlan_private pmpriv = MNULL;
#ifdef STA_SUPPORT
	pmlan_private pmpriv_sta = MNULL;
#endif
	ENTER();

	pmpriv = wlan_get_priv(pmadapter, MLAN_BSS_ROLE_ANY);
#ifdef STA_SUPPORT
	pmpriv_sta = wlan_get_priv(pmadapter, MLAN_BSS_ROLE_STA);
#endif

#ifdef SDIO
	if (IS_SD(pmadapter->card_type)) {
	}
#endif

	ret = wlan_prepare_cmd(pmpriv, HostCmd_CMD_RECONFIGURE_TX_BUFF,
			       HostCmd_ACT_GEN_SET, 0, MNULL,
			       &pmadapter->max_tx_buf_size);
	if (ret) {
		ret = MLAN_STATUS_FAILURE;
		goto done;
	}
#if defined(STA_SUPPORT)
	if (pmpriv_sta && (pmpriv_sta->state_11d.user_enable_11d == ENABLE_11D)) {
		/* Send command to FW to enable 11d */
		ret = wlan_prepare_cmd(pmpriv_sta, HostCmd_CMD_802_11_SNMP_MIB,
				       HostCmd_ACT_GEN_SET, Dot11D_i, MNULL,
				       &pmpriv_sta->state_11d.user_enable_11d);
		if (ret) {
			ret = MLAN_STATUS_FAILURE;
			goto done;
		}
	}
#endif

#ifdef STA_SUPPORT
	if (pmpriv_sta && (pmadapter->ps_mode == Wlan802_11PowerModePSP)) {
		ret = wlan_prepare_cmd(pmpriv_sta,
				       HostCmd_CMD_802_11_PS_MODE_ENH,
				       EN_AUTO_PS, BITMAP_STA_PS, MNULL, MNULL);
		if (ret) {
			ret = MLAN_STATUS_FAILURE;
			goto done;
		}
	}
#endif

	if (pmadapter->init_auto_ds) {
		mlan_ds_auto_ds auto_ds;
		/* Enable auto deep sleep */
		auto_ds.idletime = pmadapter->idle_time;
		ret = wlan_prepare_cmd(pmpriv, HostCmd_CMD_802_11_PS_MODE_ENH,
				       EN_AUTO_PS, BITMAP_AUTO_DS, MNULL,
				       &auto_ds);
		if (ret) {
			ret = MLAN_STATUS_FAILURE;
			goto done;
		}
	}
#define DEF_AUTO_NULL_PKT_PERIOD 30
	if (pmpriv_sta) {
		t_u32 value = DEF_AUTO_NULL_PKT_PERIOD;
		ret = wlan_prepare_cmd(pmpriv_sta, HostCmd_CMD_802_11_SNMP_MIB,
				       HostCmd_ACT_GEN_SET, NullPktPeriod_i,
				       MNULL, &value);
		if (ret) {
			ret = MLAN_STATUS_FAILURE;
			goto done;
		}
	}
	if (pmadapter->init_para.indrstcfg != 0xffffffff) {
		mlan_ds_ind_rst_cfg ind_rst_cfg;
		ind_rst_cfg.ir_mode = pmadapter->init_para.indrstcfg & 0xff;
		ind_rst_cfg.gpio_pin =
			(pmadapter->init_para.indrstcfg & 0xff00) >> 8;
		ret = wlan_prepare_cmd(pmpriv,
				       HostCmd_CMD_INDEPENDENT_RESET_CFG,
				       HostCmd_ACT_GEN_SET, 0, MNULL,
				       (t_void *)&ind_rst_cfg);
		if (ret) {
			ret = MLAN_STATUS_FAILURE;
			goto done;
		}
	}

	if (pmadapter->inact_tmo) {
		ret = wlan_prepare_cmd(pmpriv,
				       HostCmd_CMD_802_11_PS_INACTIVITY_TIMEOUT,
				       HostCmd_ACT_GEN_SET, 0, MNULL,
				       &pmadapter->inact_tmo);
		if (ret) {
			ret = MLAN_STATUS_FAILURE;
			goto done;
		}
	}
	/* Send request to firmware */
	ret = wlan_prepare_cmd(pmpriv, HostCmd_CMD_802_11_RF_ANTENNA,
			       HostCmd_ACT_GEN_GET, 0, MNULL, MNULL);
	if (ret) {
		ret = MLAN_STATUS_FAILURE;
		goto done;
	}
	ret = MLAN_STATUS_PENDING;
done:
	LEAVE();
	return ret;
}

#ifdef RX_PACKET_COALESCE
mlan_status
wlan_cmd_rx_pkt_coalesce_cfg(pmlan_private pmpriv,
			     HostCmd_DS_COMMAND *cmd,
			     t_u16 cmd_action, t_void *pdata_buf)
{
	mlan_ds_misc_rx_packet_coalesce *rx_pkt_cfg =
		(mlan_ds_misc_rx_packet_coalesce *)pdata_buf;
	HostCmd_DS_RX_PKT_COAL_CFG *prx_coal_cfg =
		(HostCmd_DS_RX_PKT_COAL_CFG *)&cmd->params.rx_pkt_coal_cfg;

	ENTER();

	cmd->command = wlan_cpu_to_le16(HostCmd_CMD_RX_PKT_COALESCE_CFG);
	prx_coal_cfg->action = wlan_cpu_to_le16(cmd_action);

	if (cmd_action == HostCmd_ACT_GEN_SET) {
		prx_coal_cfg->packet_threshold =
			wlan_cpu_to_le32(rx_pkt_cfg->packet_threshold);
		prx_coal_cfg->delay = wlan_cpu_to_le16(rx_pkt_cfg->delay);
		PRINTM(MCMND,
		       "Set RX coal config: packet threshold=%d delay=%d\n",
		       rx_pkt_cfg->packet_threshold, rx_pkt_cfg->delay);
		cmd->size =
			wlan_cpu_to_le16(S_DS_GEN +
					 sizeof(HostCmd_DS_RX_PKT_COAL_CFG));
	} else {
		cmd->size = wlan_cpu_to_le16(S_DS_GEN + sizeof(cmd_action));
	}

	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function handles the command response of RX_PACKET_COAL_CFG
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param resp         A pointer to HostCmd_DS_COMMAND
 *  @param pioctl_buf   A pointer to mlan_ioctl_req structure
 *
 *  @return             MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_ret_rx_pkt_coalesce_cfg(pmlan_private pmpriv,
			     const HostCmd_DS_COMMAND *resp,
			     mlan_ioctl_req *pioctl_buf)
{
	mlan_ds_misc_cfg *pcfg = MNULL;
	const HostCmd_DS_RX_PKT_COAL_CFG *presp_cfg =
		&resp->params.rx_pkt_coal_cfg;

	ENTER();

	if (pioctl_buf) {
		pcfg = (mlan_ds_misc_cfg *)pioctl_buf->pbuf;
		pcfg->param.rx_coalesce.packet_threshold =
			wlan_le32_to_cpu(presp_cfg->packet_threshold);
		pcfg->param.rx_coalesce.delay =
			wlan_le16_to_cpu(presp_cfg->delay);
		PRINTM(MCMND,
		       "Get rx pkt coalesce info: packet threshold=%d delay=%d\n",
		       pcfg->param.rx_coalesce.packet_threshold,
		       pcfg->param.rx_coalesce.delay);
		pioctl_buf->buf_len = sizeof(mlan_ds_misc_rx_packet_coalesce);
	}

	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

#endif

/**
 *  @brief This function download the vdll block.
 *
 *  @param pmadapter    A pointer to mlan_adapter structure
 *  @param block            A pointer to VDLL block
 *  @param block_len      The VDLL block length
 *
 *  @return             MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_download_vdll_block(mlan_adapter *pmadapter, t_u8 *block, t_u16 block_len)
{
	mlan_status status = MLAN_STATUS_FAILURE;
	mlan_status ret = MLAN_STATUS_PENDING;
#if defined(SDIO) || defined(PCIE)
	pvdll_dnld_ctrl ctrl = &pmadapter->vdll_ctrl;
#endif
	mlan_buffer *pmbuf = MNULL;
	mlan_private *pmpriv = wlan_get_priv(pmadapter, MLAN_BSS_ROLE_ANY);
	HostCmd_DS_GEN *cmd_hdr = MNULL;
	t_u16 msg_len = block_len + sizeof(HostCmd_DS_GEN);
#ifdef USB
	t_u32 tmp;
#endif
	ENTER();
#if defined(SDIO) || defined(PCIE)
	if (!IS_USB(pmadapter->card_type)) {
		pmbuf = ctrl->cmd_buf;
		if (pmbuf)
			pmbuf->data_offset += pmadapter->ops.intf_header_len;
	}
#endif
#ifdef USB
	if (IS_USB(pmadapter->card_type)) {
		pmbuf = wlan_alloc_mlan_buffer(pmadapter,
					       MRVDRV_SIZE_OF_CMD_BUFFER, 0,
					       MOAL_MALLOC_BUFFER);
		if (pmbuf) {
			tmp = wlan_cpu_to_le32(MLAN_USB_TYPE_VDLL);
			memcpy_ext(pmadapter,
				   (t_u8 *)(pmbuf->pbuf + pmbuf->data_offset),
				   (t_u8 *)&tmp, MLAN_TYPE_LEN, MLAN_TYPE_LEN);
			pmbuf->data_offset += MLAN_TYPE_LEN;
		}
	}
#endif
	if (!pmbuf) {
		PRINTM(MERROR, "dnld vdll: Fail to alloc vdll buf");
		goto done;
	}
	cmd_hdr = (HostCmd_DS_GEN *)(pmbuf->pbuf + pmbuf->data_offset);
	cmd_hdr->command = wlan_cpu_to_le16(HostCmd_CMD_VDLL);
	cmd_hdr->seq_num = wlan_cpu_to_le16(0xFF00);
	cmd_hdr->size = wlan_cpu_to_le16(msg_len);

	pmadapter->callbacks.moal_memcpy_ext(pmadapter->pmoal_handle,
					     pmbuf->pbuf + pmbuf->data_offset +
					     sizeof(HostCmd_DS_GEN),
					     block, block_len, block_len);

	pmbuf->data_len = msg_len;

#if defined(SDIO) || defined(PCIE)
	if (!IS_USB(pmadapter->card_type)) {
		pmbuf->data_offset -= pmadapter->ops.intf_header_len;
		pmbuf->data_len += pmadapter->ops.intf_header_len;
	}
#endif
#ifdef USB
	if (IS_USB(pmadapter->card_type)) {
		pmbuf->data_offset -= MLAN_TYPE_LEN;
		pmbuf->data_len += MLAN_TYPE_LEN;
	}
#endif
	PRINTM_NETINTF(MCMND, pmpriv);
	PRINTM(MCMND, "DNLD_VDLL : block_len=%d\n", block_len);

	ret = pmadapter->ops.host_to_card(pmpriv, MLAN_TYPE_VDLL, pmbuf, MNULL);

	if (ret == MLAN_STATUS_FAILURE)
		PRINTM(MERROR, "DNLD_VDLL: Host to Card Failed\n");
	else
		status = MLAN_STATUS_SUCCESS;

done:
	if ((ret == MLAN_STATUS_FAILURE) || (ret == MLAN_STATUS_SUCCESS)) {
#ifdef USB
		if (IS_USB(pmadapter->card_type))
			wlan_free_mlan_buffer(pmadapter, pmbuf);
#endif
	}
	LEAVE();
	return status;
}

/**
 *  @brief The function Get the VDLL image from moal
 *
 *  @param pmadapter    A pointer to mlan_adapter structure
 *  @param offset          offset
 *
 *  @return             MLAN_STATUS_SUCCESS
 *
 */
static mlan_status
wlan_get_vdll_image(pmlan_adapter pmadapter, t_u32 vdll_len)
{
	mlan_status status = MLAN_STATUS_SUCCESS;
	vdll_dnld_ctrl *ctrl = &pmadapter->vdll_ctrl;
	pmlan_callbacks pcb = &pmadapter->callbacks;

	ENTER();

	if (ctrl->vdll_mem) {
		PRINTM(MCMND, "VDLL mem is not empty: %p len=%d\n",
		       ctrl->vdll_mem, ctrl->vdll_len);
		goto done;
	}
	if (pcb->moal_vmalloc && pcb->moal_vfree)
		status = pcb->moal_vmalloc(pmadapter->pmoal_handle, vdll_len,
					   (t_u8 **)&ctrl->vdll_mem);
	else
		status = pcb->moal_malloc(pmadapter->pmoal_handle, vdll_len,
					  MLAN_MEM_DEF,
					  (t_u8 **)&ctrl->vdll_mem);

	if (status != MLAN_STATUS_SUCCESS) {
		PRINTM(MERROR, "VDLL: Fail to alloc vdll memory");
		goto done;
	}

	if (MLAN_STATUS_SUCCESS !=
	    pcb->moal_get_vdll_data(pmadapter->pmoal_handle, vdll_len,
				    ctrl->vdll_mem)) {
		PRINTM(MERROR, "VDLL: firmware image not available\n");
		status = MLAN_STATUS_FAILURE;
		if (pcb->moal_vmalloc && pcb->moal_vfree)
			pcb->moal_vfree(pmadapter->pmoal_handle,
					(t_u8 *)ctrl->vdll_mem);
		else
			pcb->moal_mfree(pmadapter->pmoal_handle,
					(t_u8 *)ctrl->vdll_mem);
		ctrl->vdll_mem = MNULL;
		ctrl->vdll_len = 0;
		goto done;
	}
	/*allocate a memory to store all VDLL images */
	ctrl->vdll_len = vdll_len;
	PRINTM(MMSG, "VDLL image: len=%d\n", ctrl->vdll_len);
done:
	LEAVE();
	return status;
}

/**
 *  @brief This function handle the multi_chan info event
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param pevent       A pointer to event buffer
 *
 *  @return             MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_process_vdll_event(pmlan_private pmpriv, pmlan_buffer pevent)
{
	mlan_status status = MLAN_STATUS_SUCCESS;
	vdll_ind *ind = MNULL;
	t_u32 offset = 0;
	t_u16 block_len = 0;
	mlan_adapter *pmadapter = pmpriv->adapter;
	vdll_dnld_ctrl *ctrl = &pmadapter->vdll_ctrl;

	ENTER();
	ind = (vdll_ind *) (pevent->pbuf + pevent->data_offset +
			    sizeof(mlan_event_id));
	switch (wlan_le16_to_cpu(ind->type)) {
	case VDLL_IND_TYPE_REQ:
		offset = wlan_le32_to_cpu(ind->offset);
		block_len = wlan_le16_to_cpu(ind->block_len);
		PRINTM(MEVENT, "VDLL_IND: type=%d offset = 0x%x, len = %d\n",
		       wlan_le16_to_cpu(ind->type), offset, block_len);
		if (offset <= ctrl->vdll_len) {
			block_len = MIN(block_len, ctrl->vdll_len - offset);
			if (!pmadapter->cmd_sent) {
				status = wlan_download_vdll_block(pmadapter,
								  ctrl->
								  vdll_mem +
								  offset,
								  block_len);
				if (status)
					PRINTM(MERROR,
					       "Fail to download VDLL block\n");
			} else {
				PRINTM(MCMND,
				       "cmd_sent=1, delay download VDLL block\n");
				ctrl->pending_block_len = block_len;
				ctrl->pending_block = ctrl->vdll_mem + offset;
			}
		} else {
			PRINTM(MERROR,
			       "Invalid VDLL req: offset=0x%x, len=%d, vdll_len=%d\n",
			       offset, block_len, ctrl->vdll_len);
		}
		break;

	case VDLL_IND_TYPE_OFFSET:
		offset = wlan_le32_to_cpu(ind->offset);
		PRINTM(MEVENT, "VDLL_IND (OFFSET): offset=0x%x\n", offset);
		wlan_get_vdll_image(pmadapter, offset);
		break;
	case VDLL_IND_TYPE_ERR_SIG:
		PRINTM(MERROR, "VDLL_IND (SIG ERR).\n");
		break;
	case VDLL_IND_TYPE_ERR_ID:
		PRINTM(MERROR, "VDLL_IND (ID ERR).\n");
		break;
	default:
		PRINTM(MERROR, "unknow vdll ind type=%d\n", ind->type);
		break;
	}
	LEAVE();
	return status;
}

/**
 *  @brief This function prepares command of get_hw_spec.
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param pcmd         A pointer to HostCmd_DS_COMMAND structure
 *
 *  @return             MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_cmd_get_hw_spec(pmlan_private pmpriv, HostCmd_DS_COMMAND *pcmd)
{
	HostCmd_DS_GET_HW_SPEC *hw_spec = &pcmd->params.hw_spec;

	ENTER();

	pcmd->command = wlan_cpu_to_le16(HostCmd_CMD_GET_HW_SPEC);
	pcmd->size =
		wlan_cpu_to_le16(sizeof(HostCmd_DS_GET_HW_SPEC) + S_DS_GEN);
	memcpy_ext(pmpriv->adapter, hw_spec->permanent_addr, pmpriv->curr_addr,
		   MLAN_MAC_ADDR_LENGTH, MLAN_MAC_ADDR_LENGTH);

	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

#ifdef SDIO
/**
 *  @brief This function prepares command of sdio rx aggr command.
 *
 *  @param pcmd         A pointer to HostCmd_DS_COMMAND structure
 *  @param cmd_action   Command action: GET or SET
 *  @param pdata_buf    A pointer to new setting buf

 *  @return             MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_cmd_sdio_rx_aggr_cfg(HostCmd_DS_COMMAND *pcmd,
			  t_u16 cmd_action, t_void *pdata_buf)
{
	HostCmd_DS_SDIO_SP_RX_AGGR_CFG *cfg = &pcmd->params.sdio_rx_aggr;

	pcmd->command = wlan_cpu_to_le16(HostCmd_CMD_SDIO_SP_RX_AGGR_CFG);
	pcmd->size = wlan_cpu_to_le16(sizeof(HostCmd_DS_SDIO_SP_RX_AGGR_CFG) +
				      S_DS_GEN);
	cfg->action = cmd_action;
	if (cmd_action == HostCmd_ACT_GEN_SET)
		cfg->enable = *(t_u8 *)pdata_buf;
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function handles the command response of sdio rx aggr command
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param resp         A pointer to HostCmd_DS_COMMAND
 *
 *  @return             MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
mlan_status
wlan_ret_sdio_rx_aggr_cfg(pmlan_private pmpriv, HostCmd_DS_COMMAND *resp)
{
	mlan_adapter *pmadapter = pmpriv->adapter;
	HostCmd_DS_SDIO_SP_RX_AGGR_CFG *cfg = &resp->params.sdio_rx_aggr;

	pmadapter->pcard_sd->sdio_rx_aggr_enable = cfg->enable;
	pmadapter->pcard_sd->sdio_rx_block_size =
		wlan_le16_to_cpu(cfg->sdio_block_size);
	PRINTM(MMSG, "SDIO rx aggr: %d block_size=%d\n", cfg->enable,
	       pmadapter->pcard_sd->sdio_rx_block_size);
	if (!pmadapter->pcard_sd->sdio_rx_block_size)
		pmadapter->pcard_sd->sdio_rx_aggr_enable = MFALSE;
	if (pmadapter->pcard_sd->sdio_rx_aggr_enable) {
		pmadapter->pcard_sd->max_sp_rx_size = SDIO_CMD53_MAX_SIZE;
		wlan_re_alloc_sdio_rx_mpa_buffer(pmadapter);
	}
	return MLAN_STATUS_SUCCESS;
}
#endif

/**
 *  @brief This function prepares command of set_cfg_data.
 *
 *  @param pmpriv       A pointer to mlan_private strcture
 *  @param pcmd         A pointer to HostCmd_DS_COMMAND structure
 *  @param cmd_action   Command action: GET or SET
 *  @param pdata_buf    A pointer to cal_data buf
 *
 *  @return             MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_cmd_cfg_data(pmlan_private pmpriv,
		  HostCmd_DS_COMMAND *pcmd, t_u16 cmd_action,
		  t_u32 cmd_oid, t_void *pdata_buf)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	HostCmd_DS_802_11_CFG_DATA *pcfg_data = &(pcmd->params.cfg_data);
	pmlan_adapter pmadapter = pmpriv->adapter;
	t_u32 len = 0;
	t_u32 data_offset;
	t_u8 *temp_pcmd = (t_u8 *)pcmd;

	ENTER();

	data_offset = S_DS_GEN + sizeof(HostCmd_DS_802_11_CFG_DATA);

	if ((cmd_oid == OID_TYPE_CAL) && (pmadapter->pcal_data) &&
	    (pmadapter->cal_data_len > 0)) {
		len = wlan_parse_cal_cfg((t_u8 *)pmadapter->pcal_data,
					 pmadapter->cal_data_len,
					 (t_u8 *)(temp_pcmd + data_offset));
	}

	pcfg_data->action = cmd_action;
	pcfg_data->type = cmd_oid;
	pcfg_data->data_len = len;

	pcmd->command = HostCmd_CMD_CFG_DATA;
	pcmd->size = pcfg_data->data_len + data_offset;

	pcmd->command = wlan_cpu_to_le16(pcmd->command);
	pcmd->size = wlan_cpu_to_le16(pcmd->size);

	pcfg_data->action = wlan_cpu_to_le16(pcfg_data->action);
	pcfg_data->type = wlan_cpu_to_le16(pcfg_data->type);
	pcfg_data->data_len = wlan_cpu_to_le16(pcfg_data->data_len);

	LEAVE();
	return ret;
}

/**
 *  @brief This function handles the command response of set_cfg_data
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param resp         A pointer to HostCmd_DS_COMMAND
 *  @param pioctl_buf   A pointer to A pointer to mlan_ioctl_req
 *
 *  @return             MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
mlan_status
wlan_ret_cfg_data(IN pmlan_private pmpriv,
		  IN HostCmd_DS_COMMAND *resp, IN t_void *pioctl_buf)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	t_u8 event_buf[100];
	mlan_cmdresp_event *pevent = (mlan_cmdresp_event *) event_buf;
	mlan_adapter *pmadapter = pmpriv->adapter;
	HostCmd_DS_802_11_CFG_DATA *pcfg_data = &resp->params.cfg_data;
	t_u16 action;
	t_u16 type;

	ENTER();

	if (resp->result != HostCmd_RESULT_OK) {
		PRINTM(MERROR, "CFG data cmd resp failed\n");
		ret = MLAN_STATUS_FAILURE;
	}

	if (!pmadapter->pdpd_data &&
	    (pmadapter->dpd_data_len == UNKNOW_DPD_LENGTH)
	    && pmadapter->hw_status == WlanHardwareStatusGetHwSpec) {
		action = wlan_le16_to_cpu(pcfg_data->action);
		type = wlan_le16_to_cpu(pcfg_data->type);
		if (action == HostCmd_ACT_GEN_GET && (type == OID_TYPE_DPD)) {
			pcfg_data->action =
				wlan_cpu_to_le16(HostCmd_ACT_GEN_SET);
			pevent->bss_index = pmpriv->bss_index;
			pevent->event_id = MLAN_EVENT_ID_STORE_HOST_CMD_RESP;
			pevent->resp = (t_u8 *)resp;
			pevent->event_len = wlan_le16_to_cpu(resp->size);
			wlan_recv_event(pmpriv,
					MLAN_EVENT_ID_STORE_HOST_CMD_RESP,
					(mlan_event *)pevent);
		}
	}

	LEAVE();
	return ret;
}

/**
 *  @brief This function prepares command of mac_control.
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param pcmd         A pointer to HostCmd_DS_COMMAND structure
 *  @param cmd_action   Command action
 *  @param pdata_buf    A pointer to command information buffer
 *
 *  @return             MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
mlan_status
wlan_cmd_mac_control(pmlan_private pmpriv,
		     HostCmd_DS_COMMAND *pcmd,
		     t_u16 cmd_action, t_void *pdata_buf)
{
	HostCmd_DS_MAC_CONTROL *pmac = &pcmd->params.mac_ctrl;
	t_u32 action = *((t_u32 *)pdata_buf);

	ENTER();

	if (cmd_action != HostCmd_ACT_GEN_SET) {
		PRINTM(MERROR, "wlan_cmd_mac_control(): support SET only.\n");
		LEAVE();
		return MLAN_STATUS_FAILURE;
	}

	pcmd->command = wlan_cpu_to_le16(HostCmd_CMD_MAC_CONTROL);
	pcmd->size =
		wlan_cpu_to_le16(sizeof(HostCmd_DS_MAC_CONTROL) + S_DS_GEN);
	pmac->action = wlan_cpu_to_le32(action);

	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function handles the command response of mac_control
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param resp         A pointer to HostCmd_DS_COMMAND
 *  @param pioctl_buf   A pointer to mlan_ioctl_req structure
 *
 *  @return             MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_ret_mac_control(pmlan_private pmpriv,
		     HostCmd_DS_COMMAND *resp, mlan_ioctl_req *pioctl_buf)
{
	ENTER();
	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function handles the command response of get_hw_spec
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param resp         A pointer to HostCmd_DS_COMMAND
 *  @param pioctl_buf   A pointer to command buffer
 *
 *  @return             MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
mlan_status
wlan_ret_get_hw_spec(pmlan_private pmpriv,
		     HostCmd_DS_COMMAND *resp, t_void *pioctl_buf)
{
	HostCmd_DS_GET_HW_SPEC *hw_spec = &resp->params.hw_spec;
	mlan_adapter *pmadapter = pmpriv->adapter;
	mlan_status ret = MLAN_STATUS_SUCCESS;
	t_u32 i;
	t_u16 left_len;
	t_u16 tlv_type = 0;
	t_u16 tlv_len = 0;
	MrvlIEtypes_fw_ver_info_t *api_rev = MNULL;
	t_u16 api_id = 0;
	MrvlIEtypesHeader_t *tlv = MNULL;
	pmlan_ioctl_req pioctl_req = (mlan_ioctl_req *)pioctl_buf;
	MrvlIEtypes_Max_Conn_t *tlv_max_conn = MNULL;
	MrvlIEtypes_Extension_t *ext_tlv = MNULL;
	MrvlIEtypes_fw_cap_info_t *fw_cap_tlv = MNULL;

	ENTER();

	pmadapter->fw_cap_info = wlan_le32_to_cpu(hw_spec->fw_cap_info);
	pmadapter->fw_cap_info &= pmadapter->init_para.dev_cap_mask;

	PRINTM(MMSG, "fw_cap_info=0x%x, dev_cap_mask=0x%x\n",
	       wlan_le32_to_cpu(hw_spec->fw_cap_info),
	       pmadapter->init_para.dev_cap_mask);
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
		if (pmadapter->priv[i])
			pmadapter->priv[i]->config_bands = pmadapter->fw_bands;
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
		if (pmadapter->fw_bands & BAND_GAC) {
			pmadapter->config_bands |= BAND_GAC;
			for (i = 0; i < pmadapter->priv_num; i++) {
				if (pmadapter->priv[i])
					pmadapter->priv[i]->config_bands |=
						BAND_GAC;
			}
		}
		pmadapter->adhoc_start_band = BAND_A;
		pmpriv->adhoc_channel = DEFAULT_AD_HOC_CHANNEL_A;
	} else if (pmadapter->fw_bands & BAND_G) {
		pmadapter->adhoc_start_band = BAND_G | BAND_B;
		pmpriv->adhoc_channel = DEFAULT_AD_HOC_CHANNEL;
	} else if (pmadapter->fw_bands & BAND_B) {
		pmadapter->adhoc_start_band = BAND_B;
		pmpriv->adhoc_channel = DEFAULT_AD_HOC_CHANNEL;
	}
#endif /* STA_SUPPORT */

	pmadapter->fw_release_number =
		wlan_le32_to_cpu(hw_spec->fw_release_number);
	pmadapter->number_of_antenna =
		wlan_le16_to_cpu(hw_spec->number_of_antenna) & 0x00ff;
	pmadapter->antinfo =
		(wlan_le16_to_cpu(hw_spec->number_of_antenna) & 0xff00) >> 8;
	PRINTM(MCMND, "num_ant=%d, antinfo=0x%x\n",
	       pmadapter->number_of_antenna, pmadapter->antinfo);

	PRINTM(MINFO, "GET_HW_SPEC: fw_release_number- 0x%X\n",
	       pmadapter->fw_release_number);
	PRINTM(MINFO, "GET_HW_SPEC: Permanent addr- " MACSTR "\n",
	       MAC2STR(hw_spec->permanent_addr));
	PRINTM(MINFO, "GET_HW_SPEC: hw_if_version=0x%X  version=0x%X\n",
	       wlan_le16_to_cpu(hw_spec->hw_if_version),
	       wlan_le16_to_cpu(hw_spec->version));

	if (pmpriv->curr_addr[0] == 0xff)
		memmove(pmadapter, pmpriv->curr_addr, hw_spec->permanent_addr,
			MLAN_MAC_ADDR_LENGTH);
	memmove(pmadapter, pmadapter->permanent_addr, hw_spec->permanent_addr,
		MLAN_MAC_ADDR_LENGTH);
	pmadapter->hw_dot_11n_dev_cap =
		wlan_le32_to_cpu(hw_spec->dot_11n_dev_cap);
	pmadapter->hw_dev_mcs_support = hw_spec->dev_mcs_support;
	for (i = 0; i < pmadapter->priv_num; i++) {
		if (pmadapter->priv[i])
			wlan_update_11n_cap(pmadapter->priv[i]);
	}

	wlan_show_dot11ndevcap(pmadapter, pmadapter->hw_dot_11n_dev_cap);
	wlan_show_devmcssupport(pmadapter, pmadapter->hw_dev_mcs_support);
#if defined(PCIE9098) || defined(SD9098) || defined(USB9098) || defined(PCIE9097) || defined(SD9097) || defined(USB9097)
	pmadapter->user_htstream = pmadapter->hw_dev_mcs_support;
	/** separate stream config for 2.4G and 5G, will be changed according to
	 * antenna cfg*/
	if (pmadapter->fw_bands & BAND_A)
		pmadapter->user_htstream |= (pmadapter->user_htstream << 8);
	PRINTM(MCMND, "user_htstream=0x%x\n", pmadapter->user_htstream);
#endif

	if (ISSUPP_BEAMFORMING(pmadapter->hw_dot_11n_dev_cap)) {
		PRINTM(MCMND, "Enable Beamforming\n");
		for (i = 0; i < pmadapter->priv_num; i++) {
			if (pmadapter->priv[i])
				pmadapter->priv[i]->tx_bf_cap =
					pmadapter->pcard_info->
					default_11n_tx_bf_cap;
		}
	}
	pmadapter->hw_dot_11ac_dev_cap =
		wlan_le32_to_cpu(hw_spec->Dot11acDevCap);
	pmadapter->hw_dot_11ac_mcs_support =
		wlan_le32_to_cpu(hw_spec->Dot11acMcsSupport);
	for (i = 0; i < pmadapter->priv_num; i++) {
		if (pmadapter->priv[i])
			wlan_update_11ac_cap(pmadapter->priv[i]);
	}
	wlan_show_dot11acdevcap(pmadapter, pmadapter->hw_dot_11ac_dev_cap);
	wlan_show_dot11acmcssupport(pmadapter,
				    pmadapter->hw_dot_11ac_mcs_support);

#ifdef SDIO
	if (IS_SD(pmadapter->card_type)) {
		pmadapter->pcard_sd->mp_end_port =
			wlan_le16_to_cpu(hw_spec->mp_end_port);

		for (i = 1; i <= (unsigned)(pmadapter->pcard_sd->max_ports -
					    pmadapter->pcard_sd->mp_end_port);
		     i++)
			pmadapter->pcard_sd->mp_data_port_mask &=
				~(1 << (pmadapter->pcard_sd->max_ports - i));
	}
#endif

	pmadapter->max_mgmt_ie_index =
		wlan_le16_to_cpu(hw_spec->mgmt_buf_count);
	PRINTM(MCMND, "GET_HW_SPEC: mgmt IE count=%d\n",
	       pmadapter->max_mgmt_ie_index);
	if (!pmadapter->max_mgmt_ie_index ||
	    pmadapter->max_mgmt_ie_index > MAX_MGMT_IE_INDEX)
		pmadapter->max_mgmt_ie_index = MAX_MGMT_IE_INDEX;

	pmadapter->region_code = wlan_le16_to_cpu(hw_spec->region_code);
	for (i = 0; i < MRVDRV_MAX_REGION_CODE; i++) {
		/* Use the region code to search for the index */
		if (pmadapter->region_code == region_code_index[i])
			break;
	}
	/* If it's unidentified region code, use the default */
	if (i >= MRVDRV_MAX_REGION_CODE) {
		pmadapter->region_code = MRVDRV_DEFAULT_REGION_CODE;
		PRINTM(MWARN,
		       "unidentified region code, use the default (0x%02x)\n",
		       MRVDRV_DEFAULT_REGION_CODE);
	}
	/* Synchronize CFP code with region code */
	pmadapter->cfp_code_bg = pmadapter->region_code;
	pmadapter->cfp_code_a = pmadapter->region_code;

	if (pmadapter->fw_cap_info & ENHANCE_EXT_SCAN_ENABLE)
		pmadapter->ext_scan_enh = MTRUE;

#ifdef SDIO
	if (IS_SD(pmadapter->card_type)) {
		if ((pmadapter->fw_cap_info & SDIO_SP_RX_AGGR_ENABLE) &&
		    pmadapter->pcard_sd->sdio_rx_aggr_enable) {
			t_u8 sdio_sp_rx_aggr = MTRUE;
			ret = wlan_prepare_cmd(pmpriv,
					       HostCmd_CMD_SDIO_SP_RX_AGGR_CFG,
					       HostCmd_ACT_GEN_SET, 0, MNULL,
					       &sdio_sp_rx_aggr);
			if (ret) {
				ret = MLAN_STATUS_FAILURE;
				goto done;
			}
		} else {
			pmadapter->pcard_sd->sdio_rx_aggr_enable = MFALSE;
			PRINTM(MCMND, "FW: SDIO rx aggr disabled 0x%x\n",
			       pmadapter->fw_cap_info);
		}
	}
#endif

	if (wlan_set_regiontable(pmpriv, (t_u8)pmadapter->region_code,
				 pmadapter->fw_bands)) {
		if (pioctl_req)
			pioctl_req->status_code = MLAN_ERROR_CMD_SCAN_FAIL;
		ret = MLAN_STATUS_FAILURE;
		goto done;
	}
#ifdef STA_SUPPORT
	if (wlan_11d_set_universaltable(pmpriv, pmadapter->fw_bands)) {
		if (pioctl_req)
			pioctl_req->status_code = MLAN_ERROR_CMD_SCAN_FAIL;
		ret = MLAN_STATUS_FAILURE;
		goto done;
	}
#endif /* STA_SUPPORT */
	if (pmadapter->fw_cap_info & FW_CAPINFO_ECSA) {
		t_u8 ecsa_enable = MTRUE;
		pmadapter->ecsa_enable = MTRUE;
		PRINTM(MCMND, "pmadapter->ecsa_enable=%d\n",
		       pmadapter->ecsa_enable);
		ret = wlan_prepare_cmd(pmpriv, HostCmd_CMD_802_11_SNMP_MIB,
				       HostCmd_ACT_GEN_SET, ECSAEnable_i, MNULL,
				       &ecsa_enable);
		if (ret) {
			ret = MLAN_STATUS_FAILURE;
			goto done;
		}
	}
	if (pmadapter->fw_cap_info & FW_CAPINFO_GET_LOG) {
		pmadapter->getlog_enable = MTRUE;
		PRINTM(MCMND, "pmadapter->getlog_enable=%d\n",
		       pmadapter->getlog_enable);
	}

	left_len = resp->size - sizeof(HostCmd_DS_GET_HW_SPEC) - S_DS_GEN;
	tlv = (MrvlIEtypesHeader_t *)((t_u8 *)hw_spec +
				      sizeof(HostCmd_DS_GET_HW_SPEC));
	while (left_len > sizeof(MrvlIEtypesHeader_t)) {
		tlv_type = wlan_le16_to_cpu(tlv->type);
		tlv_len = wlan_le16_to_cpu(tlv->len);
		switch (tlv_type) {
		case TLV_TYPE_FW_VER_INFO:
			api_rev = (MrvlIEtypes_fw_ver_info_t *) tlv;
			api_id = wlan_le16_to_cpu(api_rev->api_id);
			switch (api_id) {
			case FW_API_VER_ID:
				pmadapter->fw_ver = api_rev->major_ver;
				pmadapter->fw_min_ver = api_rev->minor_ver;
				PRINTM(MCMND, "fw ver=%d.%d\n",
				       api_rev->major_ver, api_rev->minor_ver);
				break;
			case UAP_FW_API_VER_ID:
				pmadapter->uap_fw_ver = api_rev->major_ver;
				PRINTM(MCMND, "uap fw ver=%d.%d\n",
				       api_rev->major_ver, api_rev->minor_ver);
				break;
			case CHANRPT_API_VER_ID:
				pmadapter->chanrpt_param_bandcfg =
					api_rev->minor_ver;
				PRINTM(MCMND, "chanrpt api ver=%d.%d\n",
				       api_rev->major_ver, api_rev->minor_ver);
				break;
			case FW_HOTFIX_VER_ID:
				pmadapter->fw_hotfix_ver = api_rev->major_ver;
				PRINTM(MCMND, "fw hotfix ver=%d\n",
				       api_rev->major_ver);
				break;
			default:
				break;
			}
			break;
		case TLV_TYPE_MAX_CONN:
			tlv_max_conn = (MrvlIEtypes_Max_Conn_t *) tlv;
			PRINTM(MMSG, "max_p2p_conn = %d, max_sta_conn = %d\n",
			       tlv_max_conn->max_p2p_conn,
			       tlv_max_conn->max_sta_conn);
			if (tlv_max_conn->max_p2p_conn &&
			    tlv_max_conn->max_sta_conn)
				pmadapter->max_sta_conn =
					MIN(tlv_max_conn->max_sta_conn,
					    tlv_max_conn->max_p2p_conn);
			else if (tlv_max_conn->max_sta_conn)
				pmadapter->max_sta_conn =
					tlv_max_conn->max_sta_conn;
			else if (tlv_max_conn->max_p2p_conn)
				pmadapter->max_sta_conn =
					tlv_max_conn->max_p2p_conn;
			else
				pmadapter->max_sta_conn = 0;
			break;
		case TLV_TYPE_EXTENSION_ID:
			ext_tlv = (MrvlIEtypes_Extension_t *) tlv;
			if (ext_tlv->ext_id == HE_CAPABILITY) {
				ext_tlv->type = tlv_type;
				ext_tlv->len = tlv_len;
				wlan_update_11ax_cap(pmadapter,
						     (MrvlIEtypes_Extension_t *)
						     ext_tlv);
			}

			break;
		case TLV_TYPE_FW_CAP_INFO:
			fw_cap_tlv = (MrvlIEtypes_fw_cap_info_t *) tlv;
			pmadapter->fw_cap_info =
				wlan_le32_to_cpu(fw_cap_tlv->fw_cap_info);
			pmadapter->fw_cap_ext =
				wlan_le32_to_cpu(fw_cap_tlv->fw_cap_ext);
			PRINTM(MCMND, "fw_cap_info=0x%x fw_cap_ext=0x%x\n",
			       pmadapter->fw_cap_info, pmadapter->fw_cap_ext);
			break;
		default:
			break;
		}
		left_len -= (sizeof(MrvlIEtypesHeader_t) + tlv_len);
		tlv = (MrvlIEtypesHeader_t *)((t_u8 *)tlv + tlv_len +
					      sizeof(MrvlIEtypesHeader_t));
	}
done:
	LEAVE();
	return ret;
}

/**
 *  @brief This function prepares command of radio_control.
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param cmd          A pointer to HostCmd_DS_COMMAND structure
 *  @param cmd_action   The action: GET or SET
 *  @param pdata_buf    A pointer to data buffer
 *
 *  @return             MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_cmd_802_11_radio_control(pmlan_private pmpriv,
			      HostCmd_DS_COMMAND *cmd,
			      t_u16 cmd_action, t_void *pdata_buf)
{
	HostCmd_DS_802_11_RADIO_CONTROL *pradio_control = &cmd->params.radio;
	t_u32 radio_ctl;
	ENTER();
	cmd->size = wlan_cpu_to_le16((sizeof(HostCmd_DS_802_11_RADIO_CONTROL)) +
				     S_DS_GEN);
	cmd->command = wlan_cpu_to_le16(HostCmd_CMD_802_11_RADIO_CONTROL);
	pradio_control->action = wlan_cpu_to_le16(cmd_action);
	memcpy_ext(pmpriv->adapter, &radio_ctl, pdata_buf, sizeof(t_u32),
		   sizeof(radio_ctl));
	pradio_control->control = wlan_cpu_to_le16((t_u16)radio_ctl);
	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function handles the command response of radio_control
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param resp         A pointer to HostCmd_DS_COMMAND
 *  @param pioctl_buf   A pointer to mlan_ioctl_req structure
 *
 *  @return             MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_ret_802_11_radio_control(pmlan_private pmpriv,
			      HostCmd_DS_COMMAND *resp,
			      mlan_ioctl_req *pioctl_buf)
{
	HostCmd_DS_802_11_RADIO_CONTROL *pradio_ctrl =
		(HostCmd_DS_802_11_RADIO_CONTROL *)&resp->params.radio;
	mlan_ds_radio_cfg *radio_cfg = MNULL;
	mlan_adapter *pmadapter = pmpriv->adapter;

	ENTER();
	pmadapter->radio_on = wlan_le16_to_cpu(pradio_ctrl->control);
	if (pioctl_buf) {
		radio_cfg = (mlan_ds_radio_cfg *)pioctl_buf->pbuf;
		radio_cfg->param.radio_on_off = (t_u32)pmadapter->radio_on;
		pioctl_buf->data_read_written = sizeof(mlan_ds_radio_cfg);
	}
	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function prepares command of remain_on_channel.
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param cmd          A pointer to HostCmd_DS_COMMAND structure
 *  @param cmd_action   The action: GET or SET
 *  @param pdata_buf    A pointer to data buffer
 *
 *  @return             MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_cmd_remain_on_channel(pmlan_private pmpriv,
			   HostCmd_DS_COMMAND *cmd,
			   t_u16 cmd_action, t_void *pdata_buf)
{
	HostCmd_DS_REMAIN_ON_CHANNEL *remain_channel =
		&cmd->params.remain_on_chan;
	mlan_ds_remain_chan *cfg = (mlan_ds_remain_chan *)pdata_buf;
	ENTER();
	cmd->size = wlan_cpu_to_le16((sizeof(HostCmd_DS_REMAIN_ON_CHANNEL)) +
				     S_DS_GEN);
	cmd->command = wlan_cpu_to_le16(HostCmd_CMD_802_11_REMAIN_ON_CHANNEL);
	remain_channel->action = cmd_action;
	if (cmd_action == HostCmd_ACT_GEN_SET) {
		if (cfg->remove) {
			remain_channel->action = HostCmd_ACT_GEN_REMOVE;
		} else {
			remain_channel->bandcfg = cfg->bandcfg;
			remain_channel->channel = cfg->channel;
			remain_channel->remain_period =
				wlan_cpu_to_le32(cfg->remain_period);
		}
	}
	remain_channel->action = wlan_cpu_to_le16(remain_channel->action);

	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function handles the command response of remain_on_channel
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param resp         A pointer to HostCmd_DS_COMMAND
 *  @param pioctl_buf   A pointer to mlan_ioctl_req structure
 *
 *  @return             MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_ret_remain_on_channel(pmlan_private pmpriv,
			   HostCmd_DS_COMMAND *resp, mlan_ioctl_req *pioctl_buf)
{
	HostCmd_DS_REMAIN_ON_CHANNEL *remain_channel =
		&resp->params.remain_on_chan;
	mlan_ds_radio_cfg *radio_cfg = MNULL;

	ENTER();
	if (pioctl_buf) {
		radio_cfg = (mlan_ds_radio_cfg *)pioctl_buf->pbuf;
		radio_cfg->param.remain_chan.status = remain_channel->status;
		radio_cfg->param.remain_chan.bandcfg = remain_channel->bandcfg;
		radio_cfg->param.remain_chan.channel = remain_channel->channel;
		radio_cfg->param.remain_chan.remain_period =
			wlan_le32_to_cpu(remain_channel->remain_period);
		pioctl_buf->data_read_written = sizeof(mlan_ds_radio_cfg);
	}
	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

#ifdef WIFI_DIRECT_SUPPORT
/**
 *  @brief This function prepares command of wifi direct mode.
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param cmd          A pointer to HostCmd_DS_COMMAND structure
 *  @param cmd_action   The action: GET or SET
 *  @param pdata_buf    A pointer to data buffer
 *
 *  @return             MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_cmd_wifi_direct_mode(pmlan_private pmpriv,
			  HostCmd_DS_COMMAND *cmd,
			  t_u16 cmd_action, t_void *pdata_buf)
{
	HostCmd_DS_WIFI_DIRECT_MODE *wfd_mode = &cmd->params.wifi_direct_mode;
	t_u16 mode = *((t_u16 *)pdata_buf);
	ENTER();
	cmd->size = wlan_cpu_to_le16((sizeof(HostCmd_DS_WIFI_DIRECT_MODE)) +
				     S_DS_GEN);
	cmd->command = wlan_cpu_to_le16(HOST_CMD_WIFI_DIRECT_MODE_CONFIG);
	wfd_mode->action = wlan_cpu_to_le16(cmd_action);
	if (cmd_action == HostCmd_ACT_GEN_SET)
		wfd_mode->mode = wlan_cpu_to_le16(mode);

	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function handles the command response of wifi direct mode
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param resp         A pointer to HostCmd_DS_COMMAND
 *  @param pioctl_buf   A pointer to mlan_ioctl_req structure
 *
 *  @return             MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_ret_wifi_direct_mode(pmlan_private pmpriv,
			  HostCmd_DS_COMMAND *resp, mlan_ioctl_req *pioctl_buf)
{
	HostCmd_DS_WIFI_DIRECT_MODE *wfd_mode = &resp->params.wifi_direct_mode;
	mlan_ds_bss *bss = MNULL;

	ENTER();
	if (pioctl_buf) {
		bss = (mlan_ds_bss *)pioctl_buf->pbuf;
		bss->param.wfd_mode = wlan_le16_to_cpu(wfd_mode->mode);
		pioctl_buf->data_read_written = sizeof(mlan_ds_bss);
	}
	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function prepares command of p2p_params_config.
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param cmd          A pointer to HostCmd_DS_COMMAND structure
 *  @param cmd_action   The action: GET or SET
 *  @param pdata_buf    A pointer to data buffer
 *
 *  @return             MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_cmd_p2p_params_config(pmlan_private pmpriv,
			   HostCmd_DS_COMMAND *cmd,
			   t_u16 cmd_action, t_void *pdata_buf)
{
	HostCmd_DS_WIFI_DIRECT_PARAM_CONFIG *p2p_config =
		&cmd->params.p2p_params_config;
	mlan_ds_wifi_direct_config *cfg =
		(mlan_ds_wifi_direct_config *)pdata_buf;
	MrvlIEtypes_NoA_setting_t *pnoa_tlv = MNULL;
	MrvlIEtypes_OPP_PS_setting_t *popp_ps_tlv = MNULL;
	t_u8 *tlv = MNULL;
	ENTER();

	cmd->size = sizeof(HostCmd_DS_WIFI_DIRECT_PARAM_CONFIG) + S_DS_GEN;
	cmd->command = wlan_cpu_to_le16(HOST_CMD_P2P_PARAMS_CONFIG);
	p2p_config->action = wlan_cpu_to_le16(cmd_action);
	if (cmd_action == HostCmd_ACT_GEN_SET) {
		tlv = (t_u8 *)p2p_config->tlv_buf;
		if (cfg->flags & WIFI_DIRECT_NOA) {
			pnoa_tlv = (MrvlIEtypes_NoA_setting_t *)tlv;
			pnoa_tlv->header.type =
				wlan_cpu_to_le16(TLV_TYPE_WIFI_DIRECT_NOA);
			pnoa_tlv->header.len =
				wlan_cpu_to_le16(sizeof
						 (MrvlIEtypes_NoA_setting_t) -
						 sizeof(MrvlIEtypesHeader_t));
			pnoa_tlv->enable = cfg->noa_enable;
			pnoa_tlv->index = wlan_cpu_to_le16(cfg->index);
			pnoa_tlv->noa_count = cfg->noa_count;
			pnoa_tlv->noa_duration =
				wlan_cpu_to_le32(cfg->noa_duration);
			pnoa_tlv->noa_interval =
				wlan_cpu_to_le32(cfg->noa_interval);
			cmd->size += sizeof(MrvlIEtypes_NoA_setting_t);
			tlv += sizeof(MrvlIEtypes_NoA_setting_t);
			PRINTM(MCMND,
			       "Set NOA: enable=%d index=%d, count=%d, duration=%d interval=%d\n",
			       cfg->noa_enable, cfg->index, cfg->noa_count,
			       (int)cfg->noa_duration, (int)cfg->noa_interval);
		}
		if (cfg->flags & WIFI_DIRECT_OPP_PS) {
			popp_ps_tlv = (MrvlIEtypes_OPP_PS_setting_t *)tlv;
			popp_ps_tlv->header.type =
				wlan_cpu_to_le16(TLV_TYPE_WIFI_DIRECT_OPP_PS);
			popp_ps_tlv->header.len =
				wlan_cpu_to_le16(sizeof
						 (MrvlIEtypes_OPP_PS_setting_t)
						 - sizeof(MrvlIEtypesHeader_t));

			popp_ps_tlv->enable = cfg->ct_window;
			popp_ps_tlv->enable |= cfg->opp_ps_enable << 7;
			cmd->size += sizeof(MrvlIEtypes_OPP_PS_setting_t);
			PRINTM(MCMND, "Set OPP_PS: enable=%d ct_win=%d\n",
			       cfg->opp_ps_enable, cfg->ct_window);
		}
	} else if (cmd_action == HostCmd_ACT_GEN_GET) {
		tlv = (t_u8 *)p2p_config->tlv_buf;
		if (cfg->flags & WIFI_DIRECT_NOA) {
			pnoa_tlv = (MrvlIEtypes_NoA_setting_t *)tlv;
			pnoa_tlv->header.type =
				wlan_cpu_to_le16(TLV_TYPE_WIFI_DIRECT_NOA);
			pnoa_tlv->header.len =
				wlan_cpu_to_le16(sizeof
						 (MrvlIEtypes_NoA_setting_t) -
						 sizeof(MrvlIEtypesHeader_t));
			cmd->size += sizeof(MrvlIEtypes_NoA_setting_t);
			tlv += sizeof(MrvlIEtypes_NoA_setting_t);
		}

		if (cfg->flags & WIFI_DIRECT_OPP_PS) {
			popp_ps_tlv = (MrvlIEtypes_OPP_PS_setting_t *)tlv;
			popp_ps_tlv->header.type =
				wlan_cpu_to_le16(TLV_TYPE_WIFI_DIRECT_OPP_PS);
			popp_ps_tlv->header.len =
				wlan_cpu_to_le16(sizeof
						 (MrvlIEtypes_OPP_PS_setting_t)
						 - sizeof(MrvlIEtypesHeader_t));
			cmd->size += sizeof(MrvlIEtypes_OPP_PS_setting_t);
		}
	}
	cmd->size = wlan_cpu_to_le16(cmd->size);
	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function handles the command response of p2p_params_config
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param resp         A pointer to HostCmd_DS_COMMAND
 *  @param pioctl_buf   A pointer to mlan_ioctl_req structure
 *
 *  @return             MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_ret_p2p_params_config(pmlan_private pmpriv,
			   HostCmd_DS_COMMAND *resp, mlan_ioctl_req *pioctl_buf)
{
	HostCmd_DS_WIFI_DIRECT_PARAM_CONFIG *p2p_config =
		&resp->params.p2p_params_config;
	mlan_ds_misc_cfg *cfg = MNULL;
	MrvlIEtypes_NoA_setting_t *pnoa_tlv = MNULL;
	MrvlIEtypes_OPP_PS_setting_t *popp_ps_tlv = MNULL;
	MrvlIEtypesHeader_t *tlv = MNULL;
	t_u16 tlv_buf_left = 0;
	t_u16 tlv_type = 0;
	t_u16 tlv_len = 0;

	ENTER();
	if (wlan_le16_to_cpu(p2p_config->action) == HostCmd_ACT_GEN_GET) {
		if (pioctl_buf) {
			cfg = (mlan_ds_misc_cfg *)pioctl_buf->pbuf;
			tlv = (MrvlIEtypesHeader_t *)(p2p_config->tlv_buf);
			tlv_buf_left =
				resp->size -
				(sizeof(HostCmd_DS_WIFI_DIRECT_PARAM_CONFIG) +
				 S_DS_GEN);
			while (tlv_buf_left >= sizeof(MrvlIEtypesHeader_t)) {
				tlv_type = wlan_le16_to_cpu(tlv->type);
				tlv_len = wlan_le16_to_cpu(tlv->len);
				if (tlv_buf_left <
				    (tlv_len + sizeof(MrvlIEtypesHeader_t))) {
					PRINTM(MERROR,
					       "Error processing p2p param config TLVs, bytes left < TLV length\n");
					break;
				}
				switch (tlv_type) {
				case TLV_TYPE_WIFI_DIRECT_NOA:
					pnoa_tlv = (MrvlIEtypes_NoA_setting_t *)
						tlv;
					cfg->param.p2p_config.flags |=
						WIFI_DIRECT_NOA;
					cfg->param.p2p_config.noa_enable =
						pnoa_tlv->enable;
					cfg->param.p2p_config.index =
						wlan_le16_to_cpu(pnoa_tlv->
								 index);
					cfg->param.p2p_config.noa_count =
						pnoa_tlv->noa_count;
					cfg->param.p2p_config.noa_duration =
						wlan_le32_to_cpu(pnoa_tlv->
								 noa_duration);
					cfg->param.p2p_config.noa_interval =
						wlan_le32_to_cpu(pnoa_tlv->
								 noa_interval);
					PRINTM(MCMND,
					       "Get NOA: enable=%d index=%d, count=%d, duration=%d interval=%d\n",
					       cfg->param.p2p_config.noa_enable,
					       cfg->param.p2p_config.index,
					       cfg->param.p2p_config.noa_count,
					       (int)cfg->param.p2p_config.
					       noa_duration,
					       (int)cfg->param.p2p_config.
					       noa_interval);
					break;
				case TLV_TYPE_WIFI_DIRECT_OPP_PS:
					popp_ps_tlv =
						(MrvlIEtypes_OPP_PS_setting_t *)
						tlv;
					cfg->param.p2p_config.flags |=
						WIFI_DIRECT_OPP_PS;
					cfg->param.p2p_config.opp_ps_enable =
						(popp_ps_tlv->enable & 0x80) >>
						7;
					cfg->param.p2p_config.ct_window =
						popp_ps_tlv->enable & 0x7f;
					PRINTM(MCMND,
					       "Get OPP_PS: enable=%d ct_win=%d\n",
					       cfg->param.p2p_config.
					       opp_ps_enable,
					       cfg->param.p2p_config.ct_window);
					break;
				default:
					break;
				}
				tlv_buf_left -=
					tlv_len + sizeof(MrvlIEtypesHeader_t);
				tlv = (MrvlIEtypesHeader_t
				       *)((t_u8 *)tlv + tlv_len +
					  sizeof(MrvlIEtypesHeader_t));
			}
			pioctl_buf->data_read_written =
				sizeof(mlan_ds_wifi_direct_config);
		}
	}
	LEAVE();
	return MLAN_STATUS_SUCCESS;
}
#endif

/**
 *  @brief This function prepares command of GPIO TSF LATCH.
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param cmd          A pointer to HostCmd_DS_COMMAND structure
 *  @param cmd_action   The action: GET or SET
 *  @param pioctl_buf   A pointer to mlan_ioctl_req buf
 *  @param pdata_buf    A pointer to data buffer
 *
 *  @return             MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_cmd_gpio_tsf_latch(pmlan_private pmpriv,
			HostCmd_DS_COMMAND *cmd,
			t_u16 cmd_action,
			mlan_ioctl_req *pioctl_buf, t_void *pdata_buf)
{
	HostCmd_DS_GPIO_TSF_LATCH_PARAM_CONFIG *gpio_tsf_config =
		&cmd->params.gpio_tsf_latch;
	mlan_ds_gpio_tsf_latch *cfg = (mlan_ds_gpio_tsf_latch *) pdata_buf;
	mlan_ds_misc_cfg *misc_cfg = (mlan_ds_misc_cfg *)pioctl_buf->pbuf;

	mlan_ds_tsf_info *tsf_info = (mlan_ds_tsf_info *) pdata_buf;
	MrvlIEtypes_GPIO_TSF_LATCH_CONFIG *gpio_tsf_latch_config = MNULL;
	MrvlIEtypes_GPIO_TSF_LATCH_REPORT *gpio_tsf_latch_report = MNULL;
	t_u8 *tlv = MNULL;
	ENTER();

	cmd->size = sizeof(HostCmd_DS_GPIO_TSF_LATCH_PARAM_CONFIG) + S_DS_GEN;
	cmd->command = wlan_cpu_to_le16(HOST_CMD_GPIO_TSF_LATCH_PARAM_CONFIG);
	gpio_tsf_config->action = wlan_cpu_to_le16(cmd_action);
	if (cmd_action == HostCmd_ACT_GEN_SET) {
		tlv = (t_u8 *)gpio_tsf_config->tlv_buf;
		if (misc_cfg->sub_command == MLAN_OID_MISC_GPIO_TSF_LATCH) {
			gpio_tsf_latch_config =
				(MrvlIEtypes_GPIO_TSF_LATCH_CONFIG *) tlv;
			gpio_tsf_latch_config->header.type =
				wlan_cpu_to_le16
				(TLV_TYPE_GPIO_TSF_LATCH_CONFIG);
			gpio_tsf_latch_config->header.len =
				wlan_cpu_to_le16(sizeof
						 (MrvlIEtypes_GPIO_TSF_LATCH_CONFIG)
						 - sizeof(MrvlIEtypesHeader_t));
			gpio_tsf_latch_config->clock_sync_mode =
				cfg->clock_sync_mode;
			gpio_tsf_latch_config->clock_sync_Role =
				cfg->clock_sync_Role;
			gpio_tsf_latch_config->clock_sync_gpio_pin_number =
				cfg->clock_sync_gpio_pin_number;
			gpio_tsf_latch_config->clock_sync_gpio_level_toggle =
				cfg->clock_sync_gpio_level_toggle;
			gpio_tsf_latch_config->clock_sync_gpio_pulse_width =
				wlan_cpu_to_le16(cfg->
						 clock_sync_gpio_pulse_width);
			cmd->size += sizeof(MrvlIEtypes_GPIO_TSF_LATCH_CONFIG);
			tlv += sizeof(MrvlIEtypes_GPIO_TSF_LATCH_CONFIG);
			PRINTM(MCMND,
			       "Set GPIO TSF latch config: Mode=%d Role=%d, GPIO Pin Number=%d, GPIO level/toggle=%d GPIO pulse width=%d\n",
			       cfg->clock_sync_mode, cfg->clock_sync_Role,
			       cfg->clock_sync_gpio_pin_number,
			       cfg->clock_sync_gpio_level_toggle,
			       (int)cfg->clock_sync_gpio_pulse_width);
		}
	} else if (cmd_action == HostCmd_ACT_GEN_GET) {
		tlv = (t_u8 *)gpio_tsf_config->tlv_buf;
		if (misc_cfg->sub_command == MLAN_OID_MISC_GPIO_TSF_LATCH) {
			gpio_tsf_latch_config =
				(MrvlIEtypes_GPIO_TSF_LATCH_CONFIG *) tlv;
			gpio_tsf_latch_config->header.type =
				wlan_cpu_to_le16
				(TLV_TYPE_GPIO_TSF_LATCH_CONFIG);
			gpio_tsf_latch_config->header.len =
				wlan_cpu_to_le16(sizeof
						 (MrvlIEtypes_GPIO_TSF_LATCH_CONFIG)
						 - sizeof(MrvlIEtypesHeader_t));
			cmd->size += sizeof(MrvlIEtypes_GPIO_TSF_LATCH_CONFIG);
			tlv += sizeof(MrvlIEtypes_GPIO_TSF_LATCH_CONFIG);
		}

		if (misc_cfg->sub_command == MLAN_OID_MISC_GET_TSF_INFO) {
			gpio_tsf_latch_report =
				(MrvlIEtypes_GPIO_TSF_LATCH_REPORT *) tlv;
			gpio_tsf_latch_report->header.type =
				wlan_cpu_to_le16
				(TLV_TYPE_GPIO_TSF_LATCH_REPORT);
			gpio_tsf_latch_report->header.len =
				wlan_cpu_to_le16(sizeof
						 (MrvlIEtypes_GPIO_TSF_LATCH_REPORT)
						 - sizeof(MrvlIEtypesHeader_t));
			gpio_tsf_latch_report->tsf_format =
				wlan_cpu_to_le16(tsf_info->tsf_format);
			PRINTM(MCMND, "Get TSF info: format=%d\n",
			       tsf_info->tsf_format);
			cmd->size += sizeof(MrvlIEtypes_GPIO_TSF_LATCH_REPORT);
		}
	}
	cmd->size = wlan_cpu_to_le16(cmd->size);
	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function handles the command response of GPIO TSF Latch
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param resp         A pointer to HostCmd_DS_COMMAND
 *  @param pioctl_buf   A pointer to mlan_ioctl_req structure
 *
 *  @return             MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_ret_gpio_tsf_latch(pmlan_private pmpriv,
			HostCmd_DS_COMMAND *resp, mlan_ioctl_req *pioctl_buf)
{
	HostCmd_DS_GPIO_TSF_LATCH_PARAM_CONFIG *gpio_tsf_config =
		&resp->params.gpio_tsf_latch;
	mlan_ds_misc_cfg *cfg = MNULL;
	MrvlIEtypes_GPIO_TSF_LATCH_CONFIG *gpio_tsf_latch_config = MNULL;
	MrvlIEtypes_GPIO_TSF_LATCH_REPORT *gpio_tsf_latch_report = MNULL;
	MrvlIEtypesHeader_t *tlv = MNULL;
	t_u16 tlv_buf_left = 0;
	t_u16 tlv_type = 0;
	t_u16 tlv_len = 0;

	ENTER();
	if (wlan_le16_to_cpu(gpio_tsf_config->action) == HostCmd_ACT_GEN_GET) {
		if (pioctl_buf) {
			cfg = (mlan_ds_misc_cfg *)pioctl_buf->pbuf;
			tlv = (MrvlIEtypesHeader_t *)(gpio_tsf_config->tlv_buf);
			tlv_buf_left =
				resp->size -
				(sizeof(HostCmd_DS_GPIO_TSF_LATCH_PARAM_CONFIG)
				 + S_DS_GEN);
			while (tlv_buf_left >= sizeof(MrvlIEtypesHeader_t)) {
				tlv_type = wlan_le16_to_cpu(tlv->type);
				tlv_len = wlan_le16_to_cpu(tlv->len);
				if (tlv_buf_left <
				    (tlv_len + sizeof(MrvlIEtypesHeader_t))) {
					PRINTM(MERROR,
					       "Error processing gpio tsf latch config TLVs, bytes left < TLV length\n");
					break;
				}
				switch (tlv_type) {
				case TLV_TYPE_GPIO_TSF_LATCH_CONFIG:
					if (cfg->sub_command ==
					    MLAN_OID_MISC_GPIO_TSF_LATCH) {
						gpio_tsf_latch_config =
							(MrvlIEtypes_GPIO_TSF_LATCH_CONFIG
							 *) tlv;
						cfg->param.
							gpio_tsf_latch_config.
							clock_sync_mode =
							gpio_tsf_latch_config->
							clock_sync_mode;
						cfg->param.
							gpio_tsf_latch_config.
							clock_sync_Role =
							gpio_tsf_latch_config->
							clock_sync_Role;
						cfg->param.
							gpio_tsf_latch_config.
							clock_sync_gpio_pin_number
							=
							gpio_tsf_latch_config->
							clock_sync_gpio_pin_number;
						cfg->param.
							gpio_tsf_latch_config.
							clock_sync_gpio_level_toggle
							=
							gpio_tsf_latch_config->
							clock_sync_gpio_level_toggle;
						cfg->param.
							gpio_tsf_latch_config.
							clock_sync_gpio_pulse_width
							=
							wlan_le16_to_cpu
							(gpio_tsf_latch_config->
							 clock_sync_gpio_pulse_width);
						PRINTM(MCMND,
						       "Get GPIO TSF latch config: Mode=%d Role=%d, GPIO Pin Number=%d, GPIO level/toggle=%d GPIO pulse width=%d\n",
						       cfg->param.
						       gpio_tsf_latch_config.
						       clock_sync_mode,
						       cfg->param.
						       gpio_tsf_latch_config.
						       clock_sync_Role,
						       cfg->param.
						       gpio_tsf_latch_config.
						       clock_sync_gpio_pin_number,
						       cfg->param.
						       gpio_tsf_latch_config.
						       clock_sync_gpio_level_toggle,
						       (int)cfg->param.
						       gpio_tsf_latch_config.
						       clock_sync_gpio_pulse_width);
					}
					break;
				case TLV_TYPE_GPIO_TSF_LATCH_REPORT:
					if (cfg->sub_command ==
					    MLAN_OID_MISC_GET_TSF_INFO) {
						gpio_tsf_latch_report =
							(MrvlIEtypes_GPIO_TSF_LATCH_REPORT
							 *) tlv;
						cfg->param.tsf_info.tsf_format =
							wlan_le16_to_cpu
							(gpio_tsf_latch_report->
							 tsf_format);
						cfg->param.tsf_info.tsf_info =
							wlan_le16_to_cpu
							(gpio_tsf_latch_report->
							 tsf_info);
						cfg->param.tsf_info.tsf =
							wlan_le64_to_cpu
							(gpio_tsf_latch_report->
							 tsf);
						cfg->param.tsf_info.tsf_offset =
							wlan_le16_to_cpu
							(gpio_tsf_latch_report->
							 tsf_offset);
						PRINTM(MCMND,
						       "Get GPIO TSF latch report : format=%d\n info=%d tsf=%llu offset=%d",
						       cfg->param.tsf_info.
						       tsf_format,
						       cfg->param.tsf_info.
						       tsf_info,
						       cfg->param.tsf_info.tsf,
						       cfg->param.tsf_info.
						       tsf_offset);
					}
					break;
				default:
					break;
				}
				tlv_buf_left -=
					tlv_len + sizeof(MrvlIEtypesHeader_t);
				tlv = (MrvlIEtypesHeader_t
				       *)((t_u8 *)tlv + tlv_len +
					  sizeof(MrvlIEtypesHeader_t));
			}
			if (cfg->sub_command == MLAN_OID_MISC_GPIO_TSF_LATCH)
				pioctl_buf->data_read_written =
					sizeof(mlan_ds_gpio_tsf_latch);
			else if (cfg->sub_command == MLAN_OID_MISC_GET_TSF_INFO)
				pioctl_buf->data_read_written =
					sizeof(mlan_ds_tsf_info);

		}
	}
	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function prepares command of mimo switch configuration.
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param cmd          A pointer to HostCmd_DS_COMMAND structure
 *  @param pdata_buf    A pointer to data buffer
 *  @return             MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_cmd_802_11_mimo_switch(pmlan_private pmpriv,
			    HostCmd_DS_COMMAND *cmd, t_void *pdata_buf)
{
	HostCmd_DS_MIMO_SWITCH *mimo_switch_cmd = &cmd->params.mimo_switch;
	mlan_ds_mimo_switch *pmimo_switch = (mlan_ds_mimo_switch *) pdata_buf;

	ENTER();

	cmd->command = wlan_cpu_to_le16(HostCmd_CMD_802_11_MIMO_SWITCH);
	cmd->size =
		wlan_cpu_to_le16((sizeof(HostCmd_DS_MIMO_SWITCH)) + S_DS_GEN);
	mimo_switch_cmd->txpath_antmode = pmimo_switch->txpath_antmode;
	mimo_switch_cmd->rxpath_antmode = pmimo_switch->rxpath_antmode;

	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function prepares command of hs wakeup reason.
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param cmd          A pointer to HostCmd_DS_COMMAND structure
 *  @param pdata_buf    A pointer to data buffer
 *  @return             MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_cmd_hs_wakeup_reason(pmlan_private pmpriv,
			  HostCmd_DS_COMMAND *cmd, t_void *pdata_buf)
{
	ENTER();

	cmd->command = wlan_cpu_to_le16(HostCmd_CMD_HS_WAKEUP_REASON);
	cmd->size = wlan_cpu_to_le16((sizeof(HostCmd_DS_HS_WAKEUP_REASON)) +
				     S_DS_GEN);

	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function handles the command response of
 *          hs wakeup reason
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param resp         A pointer to HostCmd_DS_COMMAND
 *  @param pioctl_buf   A pointer to command buffer
 *
 *  @return             MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_ret_hs_wakeup_reason(pmlan_private pmpriv,
			  HostCmd_DS_COMMAND *resp, mlan_ioctl_req *pioctl_buf)
{
	HostCmd_DS_HS_WAKEUP_REASON *hs_wakeup_reason =
		(HostCmd_DS_HS_WAKEUP_REASON *)&resp->params.hs_wakeup_reason;
	mlan_ds_pm_cfg *pm_cfg = MNULL;

	ENTER();

	pm_cfg = (mlan_ds_pm_cfg *)pioctl_buf->pbuf;
	pm_cfg->param.wakeup_reason.hs_wakeup_reason =
		wlan_le16_to_cpu(hs_wakeup_reason->wakeup_reason);
	pioctl_buf->data_read_written = sizeof(mlan_ds_pm_cfg);

	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function prepares command of tx_rx_pkt_stats
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param cmd          A pointer to HostCmd_DS_COMMAND structure
 *  @param pioctl_buf   A pointer to mlan_ioctl_req structure
 *  @param pdata_buf    A pointer to information buffer
 *  @return         MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
mlan_status
wlan_cmd_tx_rx_pkt_stats(pmlan_private pmpriv,
			 HostCmd_DS_COMMAND *cmd,
			 pmlan_ioctl_req pioctl_buf, t_void *pdata_buf)
{
	HostCmd_DS_TX_RX_HISTOGRAM *ptx_rx_histogram =
		&cmd->params.tx_rx_histogram;
	mlan_ds_misc_tx_rx_histogram *ptx_rx_pkt_stats =
		(mlan_ds_misc_tx_rx_histogram *) pdata_buf;
	mlan_status ret = MLAN_STATUS_SUCCESS;

	ENTER();

	if (!ptx_rx_pkt_stats) {
		ret = MLAN_STATUS_FAILURE;
		goto done;
	}
	cmd->command = wlan_cpu_to_le16(HOST_CMD_TX_RX_PKT_STATS);
	cmd->size =
		wlan_cpu_to_le16(sizeof(HostCmd_DS_TX_RX_HISTOGRAM) + S_DS_GEN);

	ptx_rx_histogram->enable = ptx_rx_pkt_stats->enable;
	ptx_rx_histogram->action = wlan_cpu_to_le16(ptx_rx_pkt_stats->action);
done:
	LEAVE();
	return ret;
}

/**
 *  @brief This function handles the command response of tx_rx_pkt_stats
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param resp         A pointer to HostCmd_DS_COMMAND
 *  @param pioctl_buf   A pointer to mlan_ioctl_req structure
 *
 *  @return             MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_ret_tx_rx_pkt_stats(pmlan_private pmpriv,
			 HostCmd_DS_COMMAND *resp, mlan_ioctl_req *pioctl_buf)
{
	HostCmd_DS_TX_RX_HISTOGRAM *ptx_rx_histogram =
		&resp->params.tx_rx_histogram;
	mlan_ds_misc_cfg *info;
	t_u16 cmdsize = wlan_le16_to_cpu(resp->size), length;
	t_u32 *pos, count = 0;

	ENTER();

	if (pioctl_buf) {
		ptx_rx_histogram->action =
			wlan_le16_to_cpu(ptx_rx_histogram->action);
		info = (mlan_ds_misc_cfg *)pioctl_buf->pbuf;
		length = cmdsize - S_DS_GEN -
			sizeof(HostCmd_DS_TX_RX_HISTOGRAM);
		if (length > 0) {
			info->param.tx_rx_histogram.size = length;
			memcpy_ext(pmpriv->adapter,
				   info->param.tx_rx_histogram.value,
				   (t_u8 *)ptx_rx_histogram +
				   sizeof(HostCmd_DS_TX_RX_HISTOGRAM),
				   length, info->param.tx_rx_histogram.size);
			pos = (t_u32 *)info->param.tx_rx_histogram.value;
			while (length - 4 * count) {
				*pos = wlan_le32_to_cpu(*pos);
				pos += 4;
				count++;
			}
		}
	}

	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/*
 *  @brief This function prepares command of cwmode control.
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param cmd          A pointer to HostCmd_DS_COMMAND structure
 *  @param cmd_action   The action: GET or SET
 *  @param pdata_buf    A pointer to data buffer
 *
 *  @return             MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_cmd_cw_mode_ctrl(pmlan_private pmpriv,
		      HostCmd_DS_COMMAND *cmd,
		      t_u16 cmd_action, t_void *pdata_buf)
{
	HostCmd_DS_CW_MODE_CTRL *cwmode_ctrl = &cmd->params.cwmode;
	mlan_ds_cw_mode_ctrl *cw_mode = (mlan_ds_cw_mode_ctrl *) pdata_buf;
	ENTER();
	cmd->size =
		wlan_cpu_to_le16((sizeof(HostCmd_DS_CW_MODE_CTRL)) + S_DS_GEN);
	cmd->command = wlan_cpu_to_le16(HostCmd_CMD_CW_MODE_CTRL);
	cwmode_ctrl->action = wlan_cpu_to_le16(cmd_action);

	if (cmd_action == HostCmd_ACT_GEN_SET) {
		cwmode_ctrl->mode = cw_mode->mode;
		cwmode_ctrl->channel = cw_mode->channel;
		cwmode_ctrl->chanInfo = cw_mode->chanInfo;
		cwmode_ctrl->txPower = wlan_cpu_to_le16(cw_mode->txPower);
		cwmode_ctrl->rateInfo = wlan_cpu_to_le32(cw_mode->rateInfo);
		cwmode_ctrl->pktLength = wlan_cpu_to_le16(cw_mode->pktLength);
	}
	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/*
 *  @brief This function handles the command response of cwmode_ctrl
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param resp         A pointer to HostCmd_DS_COMMAND
 *  @param pioctl_buf   A pointer to mlan_ioctl_req structure
 *
 *  @return             MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_ret_cw_mode_ctrl(pmlan_private pmpriv,
		      HostCmd_DS_COMMAND *resp, mlan_ioctl_req *pioctl_buf)
{
	HostCmd_DS_CW_MODE_CTRL *cwmode_resp = &resp->params.cwmode;
	mlan_ds_misc_cfg *misc = MNULL;

	ENTER();
	if (pioctl_buf) {
		misc = (mlan_ds_misc_cfg *)pioctl_buf->pbuf;
		misc->param.cwmode.mode = cwmode_resp->mode;
		misc->param.cwmode.channel = cwmode_resp->channel;
		misc->param.cwmode.chanInfo = cwmode_resp->chanInfo;
		misc->param.cwmode.txPower =
			wlan_le16_to_cpu(cwmode_resp->txPower);
		misc->param.cwmode.rateInfo =
			wlan_le32_to_cpu(cwmode_resp->rateInfo);
		;
		misc->param.cwmode.pktLength =
			wlan_le16_to_cpu(cwmode_resp->pktLength);
		;
		pioctl_buf->data_read_written = sizeof(mlan_ds_misc_cfg);
	}
	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function prepares command of rf_antenna.
 *
 *  @param pmpriv   A pointer to mlan_private structure
 *  @param cmd      A pointer to HostCmd_DS_COMMAND structure
 *  @param cmd_action   The action: GET or SET
 *  @param pdata_buf    A pointer to data buffer
 *
 *  @return         MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_cmd_802_11_rf_antenna(pmlan_private pmpriv,
			   HostCmd_DS_COMMAND *cmd,
			   t_u16 cmd_action, t_void *pdata_buf)
{
	HostCmd_DS_802_11_RF_ANTENNA *pantenna = &cmd->params.antenna;
	mlan_ds_ant_cfg *ant_cfg = (mlan_ds_ant_cfg *)pdata_buf;
	typedef struct _HostCmd_DS_802_11_RF_ANTENNA_1X1 {
		/** Action */
		t_u16 action;
		/**  Antenna or 0xffff (diversity) */
		t_u16 antenna_mode;
		/** Evaluate time */
		t_u16 evaluate_time;
		/** Current antenna */
		t_u16 current_antenna;
	} HostCmd_DS_802_11_RF_ANTENNA_1X1;
	HostCmd_DS_802_11_RF_ANTENNA_1X1 *pantenna_1x1 =
		(HostCmd_DS_802_11_RF_ANTENNA_1X1 *)&cmd->params.antenna;
	mlan_ds_ant_cfg_1x1 *ant_cfg_1x1 = (mlan_ds_ant_cfg_1x1 *) pdata_buf;

	ENTER();
	cmd->command = wlan_cpu_to_le16(HostCmd_CMD_802_11_RF_ANTENNA);
	if (!IS_STREAM_2X2(pmpriv->adapter->feature_control))
		cmd->size =
			wlan_cpu_to_le16(sizeof
					 (HostCmd_DS_802_11_RF_ANTENNA_1X1) +
					 S_DS_GEN);
	else
		cmd->size =
			wlan_cpu_to_le16(sizeof(HostCmd_DS_802_11_RF_ANTENNA) +
					 S_DS_GEN);

	if (cmd_action == HostCmd_ACT_GEN_SET) {
		if (IS_STREAM_2X2(pmpriv->adapter->feature_control)) {
			pantenna->action_tx =
				wlan_cpu_to_le16(HostCmd_ACT_SET_TX);
			pantenna->tx_antenna_mode =
				wlan_cpu_to_le16((t_u16)ant_cfg->tx_antenna);
			pantenna->action_rx =
				wlan_cpu_to_le16(HostCmd_ACT_SET_RX);
			pantenna->rx_antenna_mode =
				wlan_cpu_to_le16((t_u16)ant_cfg->rx_antenna);
		} else {
			pantenna_1x1->action =
				wlan_cpu_to_le16(HostCmd_ACT_SET_BOTH);
			pantenna_1x1->antenna_mode =
				wlan_cpu_to_le16((t_u16)ant_cfg_1x1->antenna);
			pantenna_1x1->evaluate_time = wlan_cpu_to_le16((t_u16)
								       ant_cfg_1x1->
								       evaluate_time);
		}
	} else {
		if (IS_STREAM_2X2(pmpriv->adapter->feature_control)) {
			pantenna->action_tx =
				wlan_cpu_to_le16(HostCmd_ACT_GET_TX);
			pantenna->action_rx =
				wlan_cpu_to_le16(HostCmd_ACT_GET_RX);
		} else {
			pantenna_1x1->action =
				wlan_cpu_to_le16(HostCmd_ACT_GET_BOTH);
		}
	}
	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function handles the command response of rf_antenna
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param resp         A pointer to HostCmd_DS_COMMAND
 *  @param pioctl_buf   A pointer to mlan_ioctl_req structure
 *
 *  @return             MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_ret_802_11_rf_antenna(pmlan_private pmpriv,
			   HostCmd_DS_COMMAND *resp, mlan_ioctl_req *pioctl_buf)
{
	HostCmd_DS_802_11_RF_ANTENNA *pantenna = &resp->params.antenna;
	t_u16 tx_ant_mode = wlan_le16_to_cpu(pantenna->tx_antenna_mode);
	t_u16 rx_ant_mode = wlan_le16_to_cpu(pantenna->rx_antenna_mode);
#if defined(PCIE9098) || defined(SD9098) || defined(USB9098) || defined(PCIE9097) || defined(SD9097) || defined(USB9097)
	mlan_adapter *pmadapter = pmpriv->adapter;
#endif
	typedef struct _HostCmd_DS_802_11_RF_ANTENNA_1X1 {
		/** Action */
		t_u16 action;
		/**  Antenna or 0xffff (diversity) */
		t_u16 antenna_mode;
		/** Evaluate time */
		t_u16 evaluate_time;
		/** Current antenna */
		t_u16 current_antenna;
	} HostCmd_DS_802_11_RF_ANTENNA_1X1;
	HostCmd_DS_802_11_RF_ANTENNA_1X1 *pantenna_1x1 =
		(HostCmd_DS_802_11_RF_ANTENNA_1X1 *)&resp->params.antenna;
	t_u16 ant_mode = wlan_le16_to_cpu(pantenna_1x1->antenna_mode);
	t_u16 evaluate_time = wlan_le16_to_cpu(pantenna_1x1->evaluate_time);
	t_u16 current_antenna = wlan_le16_to_cpu(pantenna_1x1->current_antenna);
	mlan_ds_radio_cfg *radio = MNULL;

	ENTER();

	if (IS_STREAM_2X2(pmpriv->adapter->feature_control)) {
		PRINTM(MCMND,
		       "RF_ANT_RESP: Tx action = 0x%x, Tx Mode = 0x%04x"
		       " Rx action = 0x%x, Rx Mode = 0x%04x\n",
		       wlan_le16_to_cpu(pantenna->action_tx), tx_ant_mode,
		       wlan_le16_to_cpu(pantenna->action_rx), rx_ant_mode);
#if defined(PCIE9098) || defined(SD9098) || defined(USB9098) || defined(PCIE9097) || defined(SD9097) || defined(USB9097)
		if (IS_CARD9098(pmadapter->card_type) ||
		    IS_CARD9097(pmadapter->card_type)) {
			tx_ant_mode &= 0x0303;
			rx_ant_mode &= 0x0303;
			/** 2G antcfg TX */
			if (tx_ant_mode & 0x00FF) {
				pmadapter->user_htstream &= ~0xF0;
				pmadapter->user_htstream |=
					(bitcount(tx_ant_mode & 0x00FF) << 4);
			}
			/* 5G antcfg tx */
			if (tx_ant_mode & 0xFF00) {
				pmadapter->user_htstream &= ~0xF000;
				pmadapter->user_htstream |=
					(bitcount(tx_ant_mode & 0xFF00) << 12);
			}
			/* 2G antcfg RX */
			if (rx_ant_mode & 0x00FF) {
				pmadapter->user_htstream &= ~0xF;
				pmadapter->user_htstream |=
					bitcount(rx_ant_mode & 0x00FF);
			}
			/* 5G antcfg RX */
			if (rx_ant_mode & 0xFF00) {
				pmadapter->user_htstream &= ~0xF00;
				pmadapter->user_htstream |=
					(bitcount(rx_ant_mode & 0xFF00) << 8);
			}
			PRINTM(MCMND,
			       "user_htstream=0x%x, tx_antenna=0x%x rx_antenna=0x%x\n",
			       pmadapter->user_htstream, tx_ant_mode,
			       rx_ant_mode);
		}
#endif
	} else
		PRINTM(MINFO,
		       "RF_ANT_RESP: action = 0x%x, Mode = 0x%04x, Evaluate time = %d, Current antenna = %d\n",
		       wlan_le16_to_cpu(pantenna_1x1->action), ant_mode,
		       evaluate_time, current_antenna);

	if (pioctl_buf) {
		radio = (mlan_ds_radio_cfg *)pioctl_buf->pbuf;
		if (IS_STREAM_2X2(pmpriv->adapter->feature_control)) {
			radio->param.ant_cfg.tx_antenna = tx_ant_mode;
			radio->param.ant_cfg.rx_antenna = rx_ant_mode;
		} else {
			radio->param.ant_cfg_1x1.antenna = ant_mode;
			radio->param.ant_cfg_1x1.evaluate_time = evaluate_time;
			radio->param.ant_cfg_1x1.current_antenna =
				current_antenna;
		}
	}

	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function prepares command of reg_access.
 *
 *  @param priv         A pointer to mlan_priv register.
 *  @param cmd          A pointer to HostCmd_DS_COMMAND structure
 *  @param cmd_action   the action: GET or SET
 *  @param pdata_buf    A pointer to data buffer
 *  @return             MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
mlan_status
wlan_cmd_reg_access(pmlan_private pmpriv,
		    HostCmd_DS_COMMAND *cmd, t_u16 cmd_action,
		    t_void *pdata_buf)
{
	mlan_ds_reg_rw *reg_rw;
#if defined(PCIE9098) || defined(SD9098) || defined(USB9098) || defined(PCIE9097) || defined(USB9097) || defined(SD9097)
	MrvlIEtypes_Reg_type_t *tlv;
	mlan_adapter *pmadapter = pmpriv->adapter;
#endif

	ENTER();

	reg_rw = (mlan_ds_reg_rw *)pdata_buf;
	switch (cmd->command) {
	case HostCmd_CMD_MAC_REG_ACCESS:{
			HostCmd_DS_MAC_REG_ACCESS *mac_reg;
			cmd->size =
				wlan_cpu_to_le16(sizeof
						 (HostCmd_DS_MAC_REG_ACCESS) +
						 S_DS_GEN);
			mac_reg =
				(HostCmd_DS_MAC_REG_ACCESS *)&cmd->params.
				mac_reg;
			mac_reg->action = wlan_cpu_to_le16(cmd_action);
			mac_reg->offset =
				wlan_cpu_to_le16((t_u16)reg_rw->offset);
			mac_reg->value = wlan_cpu_to_le32(reg_rw->value);
#if defined(PCIE9098) || defined(SD9098) || defined(USB9098) || defined(PCIE9097) || defined(USB9097) || defined(SD9097)
			if ((reg_rw->type == MLAN_REG_MAC2) &&
			    (IS_CARD9098(pmadapter->card_type) ||
			     IS_CARD9097(pmadapter->card_type))) {
				tlv = (MrvlIEtypes_Reg_type_t
				       *) ((t_u8 *)cmd +
					   sizeof(HostCmd_DS_MAC_REG_ACCESS) +
					   S_DS_GEN);
				tlv->header.type =
					wlan_cpu_to_le16
					(TLV_TYPE_REG_ACCESS_CTRL);
				tlv->header.len =
					wlan_cpu_to_le16(sizeof(t_u8));
				tlv->type = MLAN_REG_MAC2;
				cmd->size =
					wlan_cpu_to_le16(sizeof
							 (HostCmd_DS_MAC_REG_ACCESS)
							 + S_DS_GEN +
							 sizeof
							 (MrvlIEtypes_Reg_type_t));
			}
#endif
			break;
		}
	case HostCmd_CMD_BBP_REG_ACCESS:{
			HostCmd_DS_BBP_REG_ACCESS *bbp_reg;
			cmd->size =
				wlan_cpu_to_le16(sizeof
						 (HostCmd_DS_BBP_REG_ACCESS) +
						 S_DS_GEN);
			bbp_reg =
				(HostCmd_DS_BBP_REG_ACCESS *)&cmd->params.
				bbp_reg;
			bbp_reg->action = wlan_cpu_to_le16(cmd_action);
			bbp_reg->offset =
				wlan_cpu_to_le16((t_u16)reg_rw->offset);
			bbp_reg->value = (t_u8)reg_rw->value;
#if defined(PCIE9098) || defined(SD9098) || defined(USB9098) || defined(PCIE9097) || defined(USB9097) || defined(SD9097)
			if ((reg_rw->type == MLAN_REG_BBP2) &&
			    (IS_CARD9098(pmadapter->card_type) ||
			     IS_CARD9097(pmadapter->card_type))) {
				tlv = (MrvlIEtypes_Reg_type_t
				       *) ((t_u8 *)cmd +
					   sizeof(HostCmd_DS_BBP_REG_ACCESS) +
					   S_DS_GEN);
				tlv->header.type =
					wlan_cpu_to_le16
					(TLV_TYPE_REG_ACCESS_CTRL);
				tlv->header.len =
					wlan_cpu_to_le16(sizeof(t_u8));
				tlv->type = MLAN_REG_BBP2;
				cmd->size =
					wlan_cpu_to_le16(sizeof
							 (HostCmd_DS_BBP_REG_ACCESS)
							 + S_DS_GEN +
							 sizeof
							 (MrvlIEtypes_Reg_type_t));
			}
#endif
			break;
		}
	case HostCmd_CMD_RF_REG_ACCESS:{
			HostCmd_DS_RF_REG_ACCESS *rf_reg;
			cmd->size =
				wlan_cpu_to_le16(sizeof
						 (HostCmd_DS_RF_REG_ACCESS) +
						 S_DS_GEN);
			rf_reg = (HostCmd_DS_RF_REG_ACCESS *)&cmd->params.
				rf_reg;
			rf_reg->action = wlan_cpu_to_le16(cmd_action);
			rf_reg->offset =
				wlan_cpu_to_le16((t_u16)reg_rw->offset);
			rf_reg->value = (t_u8)reg_rw->value;
#if defined(PCIE9098) || defined(SD9098) || defined(USB9098) || defined(PCIE9097) || defined(USB9097) || defined(SD9097)
			if ((reg_rw->type == MLAN_REG_RF2) &&
			    (IS_CARD9098(pmadapter->card_type) ||
			     IS_CARD9097(pmadapter->card_type))) {
				tlv = (MrvlIEtypes_Reg_type_t
				       *) ((t_u8 *)cmd +
					   sizeof(HostCmd_DS_RF_REG_ACCESS) +
					   S_DS_GEN);
				tlv->header.type =
					wlan_cpu_to_le16
					(TLV_TYPE_REG_ACCESS_CTRL);
				tlv->header.len =
					wlan_cpu_to_le16(sizeof(t_u8));
				tlv->type = MLAN_REG_RF2;
				cmd->size =
					wlan_cpu_to_le16(sizeof
							 (HostCmd_DS_RF_REG_ACCESS)
							 + S_DS_GEN +
							 sizeof
							 (MrvlIEtypes_Reg_type_t));
			}
#endif
			break;
		}
	case HostCmd_CMD_CAU_REG_ACCESS:{
			HostCmd_DS_RF_REG_ACCESS *cau_reg;
			cmd->size =
				wlan_cpu_to_le16(sizeof
						 (HostCmd_DS_RF_REG_ACCESS) +
						 S_DS_GEN);
			cau_reg =
				(HostCmd_DS_RF_REG_ACCESS *)&cmd->params.rf_reg;
			cau_reg->action = wlan_cpu_to_le16(cmd_action);
			cau_reg->offset =
				wlan_cpu_to_le16((t_u16)reg_rw->offset);
			cau_reg->value = (t_u8)reg_rw->value;
			break;
		}
	case HostCmd_CMD_TARGET_ACCESS:{
			HostCmd_DS_TARGET_ACCESS *target;
			cmd->size =
				wlan_cpu_to_le16(sizeof
						 (HostCmd_DS_TARGET_ACCESS) +
						 S_DS_GEN);
			target = (HostCmd_DS_TARGET_ACCESS *)&cmd->params.
				target;
			target->action = wlan_cpu_to_le16(cmd_action);
			target->csu_target =
				wlan_cpu_to_le16(MLAN_CSU_TARGET_PSU);
			target->address =
				wlan_cpu_to_le16((t_u16)reg_rw->offset);
			target->data = (t_u8)reg_rw->value;
			break;
		}
	case HostCmd_CMD_802_11_EEPROM_ACCESS:{
			mlan_ds_read_eeprom *rd_eeprom =
				(mlan_ds_read_eeprom *)pdata_buf;
			HostCmd_DS_802_11_EEPROM_ACCESS *cmd_eeprom =
				(HostCmd_DS_802_11_EEPROM_ACCESS *)&cmd->params.
				eeprom;
			cmd->size =
				wlan_cpu_to_le16(sizeof
						 (HostCmd_DS_802_11_EEPROM_ACCESS)
						 + S_DS_GEN);
			cmd_eeprom->action = wlan_cpu_to_le16(cmd_action);
			cmd_eeprom->offset =
				wlan_cpu_to_le16(rd_eeprom->offset);
			cmd_eeprom->byte_count =
				wlan_cpu_to_le16(rd_eeprom->byte_count);
			cmd_eeprom->value = 0;
			break;
		}
	case HostCmd_CMD_BCA_REG_ACCESS:{
			HostCmd_DS_BCA_REG_ACCESS *bca_reg;
			cmd->size =
				wlan_cpu_to_le16(sizeof
						 (HostCmd_DS_BCA_REG_ACCESS) +
						 S_DS_GEN);
			bca_reg =
				(HostCmd_DS_BCA_REG_ACCESS *) & cmd->params.
				bca_reg;
			bca_reg->action = wlan_cpu_to_le16(cmd_action);
			bca_reg->offset =
				wlan_cpu_to_le16((t_u16)reg_rw->offset);
			bca_reg->value = wlan_cpu_to_le32(reg_rw->value);
#if defined(PCIE9098) || defined(SD9098) || defined(USB9098) || defined(PCIE9097) || defined(USB9097) || defined(SD9097)
			if ((reg_rw->type == MLAN_REG_BCA2) &&
			    (IS_CARD9098(pmadapter->card_type) ||
			     IS_CARD9097(pmadapter->card_type))) {
				tlv = (MrvlIEtypes_Reg_type_t
				       *) ((t_u8 *)cmd +
					   sizeof(HostCmd_DS_BCA_REG_ACCESS) +
					   S_DS_GEN);
				tlv->header.type =
					wlan_cpu_to_le16
					(TLV_TYPE_REG_ACCESS_CTRL);
				tlv->header.len =
					wlan_cpu_to_le16(sizeof(t_u8));
				tlv->type = MLAN_REG_BCA2;
				cmd->size =
					wlan_cpu_to_le16(sizeof
							 (HostCmd_DS_BCA_REG_ACCESS)
							 + S_DS_GEN +
							 sizeof
							 (MrvlIEtypes_Reg_type_t));
			}
#endif
			break;
		}
	default:
		LEAVE();
		return MLAN_STATUS_FAILURE;
	}
	cmd->command = wlan_cpu_to_le16(cmd->command);

	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function handles the command response of reg_access
 *
 *  @param pmadapter    A pointer to mlan_adapter structure
 *  @param type         The type of reg access (MAC, BBP or RF)
 *  @param resp         A pointer to HostCmd_DS_COMMAND
 *  @param pioctl_buf   A pointer to command buffer
 *
 *  @return             MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
mlan_status
wlan_ret_reg_access(mlan_adapter *pmadapter, t_u16 type,
		    HostCmd_DS_COMMAND *resp, mlan_ioctl_req *pioctl_buf)
{
	mlan_ds_reg_mem *reg_mem = MNULL;
	mlan_ds_reg_rw *reg_rw = MNULL;

	ENTER();

	if (pioctl_buf) {
		reg_mem = (mlan_ds_reg_mem *)pioctl_buf->pbuf;
		reg_rw = &reg_mem->param.reg_rw;
		switch (type) {
		case HostCmd_CMD_MAC_REG_ACCESS:{
				HostCmd_DS_MAC_REG_ACCESS *reg;
				reg = (HostCmd_DS_MAC_REG_ACCESS *)&resp->
					params.mac_reg;
				reg_rw->offset =
					(t_u32)wlan_le16_to_cpu(reg->offset);
				reg_rw->value = wlan_le32_to_cpu(reg->value);
				break;
			}
		case HostCmd_CMD_BBP_REG_ACCESS:{
				HostCmd_DS_BBP_REG_ACCESS *reg;
				reg = (HostCmd_DS_BBP_REG_ACCESS *)&resp->
					params.bbp_reg;
				reg_rw->offset =
					(t_u32)wlan_le16_to_cpu(reg->offset);
				reg_rw->value = (t_u32)reg->value;
				break;
			}

		case HostCmd_CMD_RF_REG_ACCESS:{
				HostCmd_DS_RF_REG_ACCESS *reg;
				reg = (HostCmd_DS_RF_REG_ACCESS *)&resp->params.
					rf_reg;
				reg_rw->offset =
					(t_u32)wlan_le16_to_cpu(reg->offset);
				reg_rw->value = (t_u32)reg->value;
				break;
			}
		case HostCmd_CMD_CAU_REG_ACCESS:{
				HostCmd_DS_RF_REG_ACCESS *reg;
				reg = (HostCmd_DS_RF_REG_ACCESS *)&resp->params.
					rf_reg;
				reg_rw->offset =
					(t_u32)wlan_le16_to_cpu(reg->offset);
				reg_rw->value = (t_u32)reg->value;
				break;
			}
		case HostCmd_CMD_TARGET_ACCESS:{
				HostCmd_DS_TARGET_ACCESS *reg;
				reg = (HostCmd_DS_TARGET_ACCESS *)&resp->params.
					target;
				reg_rw->offset =
					(t_u32)wlan_le16_to_cpu(reg->address);
				reg_rw->value = (t_u32)reg->data;
				break;
			}
		case HostCmd_CMD_802_11_EEPROM_ACCESS:{
				mlan_ds_read_eeprom *eeprom =
					&reg_mem->param.rd_eeprom;
				HostCmd_DS_802_11_EEPROM_ACCESS *cmd_eeprom =
					(HostCmd_DS_802_11_EEPROM_ACCESS *)
					&resp->params.eeprom;
				cmd_eeprom->byte_count =
					wlan_le16_to_cpu(cmd_eeprom->
							 byte_count);
				PRINTM(MINFO, "EEPROM read len=%x\n",
				       cmd_eeprom->byte_count);
				if (eeprom->byte_count < cmd_eeprom->byte_count) {
					eeprom->byte_count = 0;
					PRINTM(MINFO,
					       "EEPROM read return length is too big\n");
					pioctl_buf->status_code =
						MLAN_ERROR_CMD_RESP_FAIL;
					LEAVE();
					return MLAN_STATUS_FAILURE;
				}
				eeprom->offset =
					wlan_le16_to_cpu(cmd_eeprom->offset);
				eeprom->byte_count = cmd_eeprom->byte_count;
				if (eeprom->byte_count > 0) {
					memcpy_ext(pmadapter, &eeprom->value,
						   &cmd_eeprom->value,
						   eeprom->byte_count,
						   MAX_EEPROM_DATA);
					HEXDUMP("EEPROM",
						(char *)&eeprom->value,
						MIN(MAX_EEPROM_DATA,
						    eeprom->byte_count));
				}
				break;
			}
		case HostCmd_CMD_BCA_REG_ACCESS:{
				HostCmd_DS_BCA_REG_ACCESS *reg;
				reg = (HostCmd_DS_BCA_REG_ACCESS *) & resp->
					params.bca_reg;
				reg_rw->offset =
					(t_u32)wlan_le16_to_cpu(reg->offset);
				reg_rw->value = wlan_le32_to_cpu(reg->value);
				break;
			}
		default:
			pioctl_buf->status_code = MLAN_ERROR_CMD_RESP_FAIL;
			LEAVE();
			return MLAN_STATUS_FAILURE;
		}
	}

	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function prepares command of mem_access.
 *
 *  @param cmd          A pointer to HostCmd_DS_COMMAND structure
 *  @param cmd_action   the action: GET or SET
 *  @param pdata_buf    A pointer to data buffer
 *  @return             MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_cmd_mem_access(HostCmd_DS_COMMAND *cmd, t_u16 cmd_action,
		    t_void *pdata_buf)
{
	mlan_ds_mem_rw *mem_rw = (mlan_ds_mem_rw *)pdata_buf;
	HostCmd_DS_MEM_ACCESS *mem_access =
		(HostCmd_DS_MEM_ACCESS *)&cmd->params.mem;

	ENTER();

	cmd->command = wlan_cpu_to_le16(HostCmd_CMD_MEM_ACCESS);
	cmd->size = wlan_cpu_to_le16(sizeof(HostCmd_DS_MEM_ACCESS) + S_DS_GEN);

	mem_access->action = wlan_cpu_to_le16(cmd_action);
	mem_access->addr = wlan_cpu_to_le32(mem_rw->addr);
	mem_access->value = wlan_cpu_to_le32(mem_rw->value);

	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function handles the command response of mem_access
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param resp         A pointer to HostCmd_DS_COMMAND
 *  @param pioctl_buf   A pointer to command buffer
 *
 *  @return             MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_ret_mem_access(pmlan_private pmpriv,
		    HostCmd_DS_COMMAND *resp, mlan_ioctl_req *pioctl_buf)
{
	mlan_ds_reg_mem *reg_mem = MNULL;
	mlan_ds_mem_rw *mem_rw = MNULL;
	HostCmd_DS_MEM_ACCESS *mem = (HostCmd_DS_MEM_ACCESS *)&resp->params.mem;

	ENTER();

	if (pioctl_buf) {
		reg_mem = (mlan_ds_reg_mem *)pioctl_buf->pbuf;
		mem_rw = &reg_mem->param.mem_rw;

		mem_rw->addr = wlan_le32_to_cpu(mem->addr);
		mem_rw->value = wlan_le32_to_cpu(mem->value);
	}

	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *
 *  @brief This function handles coex events generated by firmware
 *
 *  @param priv A pointer to mlan_private structure
 *  @param pevent   A pointer to event buf
 *
 *  @return     N/A
 */
void
wlan_bt_coex_wlan_param_update_event(pmlan_private priv, pmlan_buffer pevent)
{
	pmlan_adapter pmadapter = priv->adapter;
	MrvlIEtypesHeader_t *tlv = MNULL;
	MrvlIETypes_BtCoexAggrWinSize_t *pCoexWinsize = MNULL;
	MrvlIEtypes_BtCoexScanTime_t *pScantlv = MNULL;
	t_s32 len = pevent->data_len - sizeof(t_u32);
	t_u8 *pCurrent_ptr = pevent->pbuf + pevent->data_offset + sizeof(t_u32);
	t_u16 tlv_type, tlv_len;

	ENTER();

	while (len >= (t_s32)sizeof(MrvlIEtypesHeader_t)) {
		tlv = (MrvlIEtypesHeader_t *)pCurrent_ptr;
		tlv_len = wlan_le16_to_cpu(tlv->len);
		tlv_type = wlan_le16_to_cpu(tlv->type);
		if ((tlv_len + (t_s32)sizeof(MrvlIEtypesHeader_t)) > len)
			break;
		switch (tlv_type) {
		case TLV_BTCOEX_WL_AGGR_WINSIZE:
			pCoexWinsize = (MrvlIETypes_BtCoexAggrWinSize_t *) tlv;
			pmadapter->coex_win_size = pCoexWinsize->coex_win_size;
			pmadapter->coex_tx_win_size = pCoexWinsize->tx_win_size;
			pmadapter->coex_rx_win_size = pCoexWinsize->rx_win_size;
			wlan_coex_ampdu_rxwinsize(pmadapter);
			wlan_update_ampdu_txwinsize(pmadapter);
			break;
		case TLV_BTCOEX_WL_SCANTIME:
			pScantlv = (MrvlIEtypes_BtCoexScanTime_t *) tlv;
			pmadapter->coex_scan = pScantlv->coex_scan;
			pmadapter->coex_min_scan_time =
				wlan_le16_to_cpu(pScantlv->min_scan_time);
			pmadapter->coex_max_scan_time =
				wlan_le16_to_cpu(pScantlv->max_scan_time);
			break;
		default:
			break;
		}
		len -= tlv_len + sizeof(MrvlIEtypesHeader_t);
		pCurrent_ptr += tlv_len + sizeof(MrvlIEtypesHeader_t);
	}
	PRINTM(MEVENT,
	       "coex_scan=%d min_scan=%d coex_win=%d, tx_win=%d rx_win=%d\n",
	       pmadapter->coex_scan, pmadapter->coex_min_scan_time,
	       pmadapter->coex_win_size, pmadapter->coex_tx_win_size,
	       pmadapter->coex_rx_win_size);

	LEAVE();
}

/**
 *  @brief This function prepares command of supplicant pmk
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param cmd          A pointer to HostCmd_DS_COMMAND structure
 *  @param cmd_action   The action: GET or SET
 *  @param pdata_buf    A pointer to data buffer
 *
 *  @return             MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
mlan_status
wlan_cmd_802_11_supplicant_pmk(pmlan_private pmpriv,
			       HostCmd_DS_COMMAND *cmd,
			       t_u16 cmd_action, t_void *pdata_buf)
{
	MrvlIEtypes_PMK_t *ppmk_tlv = MNULL;
	MrvlIEtypes_Passphrase_t *ppassphrase_tlv = MNULL;
	MrvlIEtypes_SAE_Password_t *psae_password_tlv = MNULL;
	MrvlIEtypes_SsIdParamSet_t *pssid_tlv = MNULL;
	MrvlIEtypes_Bssid_t *pbssid_tlv = MNULL;
	HostCmd_DS_802_11_SUPPLICANT_PMK *pesupplicant_psk =
		&cmd->params.esupplicant_psk;
	t_u8 *ptlv_buffer = (t_u8 *)pesupplicant_psk->tlv_buffer;
	mlan_ds_sec_cfg *sec = (mlan_ds_sec_cfg *)pdata_buf;
	mlan_ds_passphrase *psk = MNULL;
	t_u8 zero_mac[] = { 0, 0, 0, 0, 0, 0 };
	t_u8 ssid_flag = 0, bssid_flag = 0, pmk_flag = 0, passphrase_flag = 0;
	t_u8 sae_password_flag = 0;

	ENTER();
	psk = (mlan_ds_passphrase *)&sec->param.passphrase;

	/*
	 * Parse the rest of the buf here
	 *  1) <ssid="valid ssid"> - This will get the passphrase, AKMP
	 *     for specified ssid, if none specified then it will get all.
	 *     Eg: iwpriv <mlanX> passphrase 0:ssid=nxp
	 *  2) <psk="psk">:<passphrase="passphare">:<bssid="00:50:43:ef:23:f3">
	 *     <ssid="valid ssid"> - passphrase and psk cannot be provided to
	 *     the same SSID, Takes one SSID at a time, If ssid= is present
	 *     the it should contain a passphrase or psk. If no arguments are
	 *     provided then AKMP=802.1x, and passphrase should be provided
	 *     after association.
	 *     End of each parameter should be followed by a ':'(except for the
	 *     last parameter) as the delimiter. If ':' has to be used in
	 *     an SSID then a '/' should be preceded to ':' as a escape.
	 *     Eg:iwpriv <mlanX> passphrase
	 *               "1:ssid=mrvl AP:psk=abcdefgh:bssid=00:50:43:ef:23:f3"
	 *     iwpriv <mlanX> passphrase
	 *            "1:ssid=nxp/: AP:psk=abcdefgd:bssid=00:50:43:ef:23:f3"
	 *     iwpriv <mlanX> passphrase "1:ssid=mrvlAP:psk=abcdefgd"
	 *  3) <ssid="valid ssid"> - This will clear the passphrase
	 *     for specified ssid, if none specified then it will clear all.
	 *     Eg: iwpriv <mlanX> passphrase 2:ssid=nxp
	 */

	/* -1 is for t_u8 TlvBuffer[1] as this should not be included */
	cmd->size = sizeof(HostCmd_DS_802_11_SUPPLICANT_PMK) + S_DS_GEN - 1;
	if (psk && memcmp(pmpriv->adapter, (t_u8 *)&psk->bssid, zero_mac,
			  sizeof(zero_mac))) {
		pbssid_tlv = (MrvlIEtypes_Bssid_t *)ptlv_buffer;
		pbssid_tlv->header.type = wlan_cpu_to_le16(TLV_TYPE_BSSID);
		pbssid_tlv->header.len = MLAN_MAC_ADDR_LENGTH;
		memcpy_ext(pmpriv->adapter, pbssid_tlv->bssid,
			   (t_u8 *)&psk->bssid, MLAN_MAC_ADDR_LENGTH,
			   MLAN_MAC_ADDR_LENGTH);
		ptlv_buffer +=
			(pbssid_tlv->header.len + sizeof(MrvlIEtypesHeader_t));
		cmd->size +=
			(pbssid_tlv->header.len + sizeof(MrvlIEtypesHeader_t));
		pbssid_tlv->header.len =
			wlan_cpu_to_le16(pbssid_tlv->header.len);
		bssid_flag = 1;
	}
	if (psk && (psk->psk_type == MLAN_PSK_PMK)) {
		ppmk_tlv = (MrvlIEtypes_PMK_t *)ptlv_buffer;
		ppmk_tlv->header.type = wlan_cpu_to_le16(TLV_TYPE_PMK);
		ppmk_tlv->header.len = MLAN_MAX_KEY_LENGTH;
		memcpy_ext(pmpriv->adapter, ppmk_tlv->pmk, psk->psk.pmk.pmk,
			   MLAN_MAX_KEY_LENGTH, MLAN_MAX_KEY_LENGTH);
		ptlv_buffer +=
			(ppmk_tlv->header.len + sizeof(MrvlIEtypesHeader_t));
		cmd->size +=
			(ppmk_tlv->header.len + sizeof(MrvlIEtypesHeader_t));
		ppmk_tlv->header.len = wlan_cpu_to_le16(ppmk_tlv->header.len);
		pmk_flag = 1;
	}
	if (psk->ssid.ssid_len) {
		pssid_tlv = (MrvlIEtypes_SsIdParamSet_t *)ptlv_buffer;
		pssid_tlv->header.type = wlan_cpu_to_le16(TLV_TYPE_SSID);
		pssid_tlv->header.len = (t_u16)MIN(MLAN_MAX_SSID_LENGTH,
						   psk->ssid.ssid_len);
		memcpy_ext(pmpriv->adapter, (t_u8 *)pssid_tlv->ssid,
			   (t_u8 *)psk->ssid.ssid, psk->ssid.ssid_len,
			   MLAN_MAX_SSID_LENGTH);
		ptlv_buffer += (pssid_tlv->header.len +
				sizeof(MrvlIEtypesHeader_t));
		cmd->size += (pssid_tlv->header.len +
			      sizeof(MrvlIEtypesHeader_t));
		pssid_tlv->header.len = wlan_cpu_to_le16(pssid_tlv->header.len);
		ssid_flag = 1;
	}
	if (psk->psk_type == MLAN_PSK_PASSPHRASE) {
		ppassphrase_tlv = (MrvlIEtypes_Passphrase_t *)ptlv_buffer;
		ppassphrase_tlv->header.type =
			wlan_cpu_to_le16(TLV_TYPE_PASSPHRASE);
		ppassphrase_tlv->header.len =
			(t_u16)MIN(MLAN_MAX_PASSPHRASE_LENGTH,
				   psk->psk.passphrase.passphrase_len);
		memcpy_ext(pmpriv->adapter, ppassphrase_tlv->passphrase,
			   psk->psk.passphrase.passphrase,
			   psk->psk.passphrase.passphrase_len,
			   MLAN_MAX_PASSPHRASE_LENGTH);
		ptlv_buffer += (ppassphrase_tlv->header.len +
				sizeof(MrvlIEtypesHeader_t));
		cmd->size += (ppassphrase_tlv->header.len +
			      sizeof(MrvlIEtypesHeader_t));
		ppassphrase_tlv->header.len =
			wlan_cpu_to_le16(ppassphrase_tlv->header.len);
		passphrase_flag = 1;
	}
	if (psk->psk_type == MLAN_PSK_SAE_PASSWORD) {
		psae_password_tlv = (MrvlIEtypes_SAE_Password_t *) ptlv_buffer;
		psae_password_tlv->header.type =
			wlan_cpu_to_le16(TLV_TYPE_SAE_PASSWORD);
		psae_password_tlv->header.len =
			(t_u16)MIN(MLAN_MAX_SAE_PASSWORD_LENGTH,
				   psk->psk.sae_password.sae_password_len);
		memcpy_ext(pmpriv->adapter, psae_password_tlv->sae_password,
			   psk->psk.sae_password.sae_password,
			   psk->psk.sae_password.sae_password_len,
			   MLAN_MAX_SAE_PASSWORD_LENGTH);
		ptlv_buffer +=
			(psae_password_tlv->header.len +
			 sizeof(MrvlIEtypesHeader_t));
		cmd->size +=
			(psae_password_tlv->header.len +
			 sizeof(MrvlIEtypesHeader_t));
		psae_password_tlv->header.len =
			wlan_cpu_to_le16(psae_password_tlv->header.len);
		sae_password_flag = 1;
	}
	if ((cmd_action == HostCmd_ACT_GEN_SET) &&
	    ((ssid_flag || bssid_flag) && (!pmk_flag && !passphrase_flag)
	     && (!pmk_flag && !sae_password_flag)
	    )) {
		PRINTM(MERROR,
		       "Invalid case,ssid/bssid present without pmk, passphrase or sae password\n");
		LEAVE();
		return MLAN_STATUS_FAILURE;
	}
	cmd->command = wlan_cpu_to_le16(HostCmd_CMD_SUPPLICANT_PMK);
	pesupplicant_psk->action = wlan_cpu_to_le16(cmd_action);
	pesupplicant_psk->cache_result = 0;
	cmd->size = wlan_cpu_to_le16(cmd->size);
	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief Handle the supplicant pmk response
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param resp         A pointer to HostCmd_DS_COMMAND
 *  @param pioctl_buf   A pointer to mlan_ioctl_req structure
 *
 *  @return        MLAN_STATUS_SUCCESS MLAN_STATUS_FAILURE
 */
mlan_status
wlan_ret_802_11_supplicant_pmk(pmlan_private pmpriv,
			       HostCmd_DS_COMMAND *resp,
			       mlan_ioctl_req *pioctl_buf)
{
	HostCmd_DS_802_11_SUPPLICANT_PMK *supplicant_pmk_resp =
		&resp->params.esupplicant_psk;
	mlan_ds_sec_cfg *sec_buf = MNULL;
	mlan_ds_sec_cfg *sec = MNULL;
	mlan_adapter *pmadapter = pmpriv->adapter;
	pmlan_callbacks pcb = &pmadapter->callbacks;
	MrvlIEtypes_PMK_t *ppmk_tlv = MNULL;
	MrvlIEtypes_Passphrase_t *passphrase_tlv = MNULL;
	MrvlIEtypes_SAE_Password_t *psae_password_tlv = MNULL;
	MrvlIEtypes_SsIdParamSet_t *pssid_tlv = MNULL;
	MrvlIEtypes_Bssid_t *pbssid_tlv = MNULL;
	t_u8 *tlv_buf = (t_u8 *)supplicant_pmk_resp->tlv_buffer;
	t_u16 action = wlan_le16_to_cpu(supplicant_pmk_resp->action);
	int tlv_buf_len = 0;
	t_u16 tlv;
	mlan_status ret = MLAN_STATUS_SUCCESS;

	ENTER();
	tlv_buf_len = resp->size -
		(sizeof(HostCmd_DS_802_11_SUPPLICANT_PMK) + S_DS_GEN - 1);

	if (pioctl_buf) {
		if (((mlan_ds_bss *)pioctl_buf->pbuf)->sub_command ==
		    MLAN_OID_BSS_FIND_BSS) {
			ret = pcb->moal_malloc(pmadapter->pmoal_handle,
					       sizeof(mlan_ds_sec_cfg),
					       MLAN_MEM_DEF, (t_u8 **)&sec_buf);
			if (ret || !sec_buf) {
				PRINTM(MERROR, "Could not allocate sec_buf!\n");
				LEAVE();
				return ret;
			}
			sec = sec_buf;
		} else {
			sec = (mlan_ds_sec_cfg *)pioctl_buf->pbuf;
		}
		if (action == HostCmd_ACT_GEN_GET) {
			while (tlv_buf_len > 0) {
				tlv = (*tlv_buf) | (*(tlv_buf + 1) << 8);
				if ((tlv != TLV_TYPE_SSID) &&
				    (tlv != TLV_TYPE_BSSID) &&
				    (tlv != TLV_TYPE_PASSPHRASE) &&
				    (tlv != TLV_TYPE_PMK)
				    && (tlv != TLV_TYPE_SAE_PASSWORD)
					)
					break;
				switch (tlv) {
				case TLV_TYPE_SSID:
					pssid_tlv =
						(MrvlIEtypes_SsIdParamSet_t *)
						tlv_buf;
					pssid_tlv->header.len =
						wlan_le16_to_cpu(pssid_tlv->
								 header.len);
					memcpy_ext(pmpriv->adapter,
						   sec->param.passphrase.ssid.
						   ssid, pssid_tlv->ssid,
						   pssid_tlv->header.len,
						   MLAN_MAX_SSID_LENGTH);
					sec->param.passphrase.ssid.ssid_len =
						MIN(MLAN_MAX_SSID_LENGTH,
						    pssid_tlv->header.len);
					tlv_buf +=
						pssid_tlv->header.len +
						sizeof(MrvlIEtypesHeader_t);
					tlv_buf_len -=
						(pssid_tlv->header.len +
						 sizeof(MrvlIEtypesHeader_t));
					break;
				case TLV_TYPE_BSSID:
					pbssid_tlv =
						(MrvlIEtypes_Bssid_t *)tlv_buf;
					pbssid_tlv->header.len =
						wlan_le16_to_cpu(pbssid_tlv->
								 header.len);
					memcpy_ext(pmpriv->adapter,
						   &sec->param.passphrase.bssid,
						   pbssid_tlv->bssid,
						   MLAN_MAC_ADDR_LENGTH,
						   MLAN_MAC_ADDR_LENGTH);
					tlv_buf +=
						pbssid_tlv->header.len +
						sizeof(MrvlIEtypesHeader_t);
					tlv_buf_len -=
						(pbssid_tlv->header.len +
						 sizeof(MrvlIEtypesHeader_t));
					break;
				case TLV_TYPE_PASSPHRASE:
					passphrase_tlv =
						(MrvlIEtypes_Passphrase_t *)
						tlv_buf;
					passphrase_tlv->header.len =
						wlan_le16_to_cpu
						(passphrase_tlv->header.len);
					sec->param.passphrase.psk_type =
						MLAN_PSK_PASSPHRASE;
					sec->param.passphrase.psk.passphrase.
						passphrase_len =
						passphrase_tlv->header.len;
					memcpy_ext(pmpriv->adapter,
						   sec->param.passphrase.psk.
						   passphrase.passphrase,
						   passphrase_tlv->passphrase,
						   passphrase_tlv->header.len,
						   MLAN_MAX_PASSPHRASE_LENGTH);
					tlv_buf +=
						passphrase_tlv->header.len +
						sizeof(MrvlIEtypesHeader_t);
					tlv_buf_len -=
						(passphrase_tlv->header.len +
						 sizeof(MrvlIEtypesHeader_t));
					break;
				case TLV_TYPE_SAE_PASSWORD:
					psae_password_tlv =
						(MrvlIEtypes_SAE_Password_t *)
						tlv_buf;
					psae_password_tlv->header.len =
						wlan_le16_to_cpu
						(psae_password_tlv->header.len);
					sec->param.passphrase.psk_type =
						MLAN_PSK_SAE_PASSWORD;
					sec->param.passphrase.psk.sae_password.
						sae_password_len =
						psae_password_tlv->header.len;
					memcpy_ext(pmpriv->adapter,
						   sec->param.passphrase.psk.
						   sae_password.sae_password,
						   psae_password_tlv->
						   sae_password,
						   psae_password_tlv->header.
						   len,
						   MLAN_MAX_SAE_PASSWORD_LENGTH);
					tlv_buf +=
						psae_password_tlv->header.len +
						sizeof(MrvlIEtypesHeader_t);
					tlv_buf_len -=
						(psae_password_tlv->header.len +
						 sizeof(MrvlIEtypesHeader_t));
					break;
				case TLV_TYPE_PMK:
					ppmk_tlv = (MrvlIEtypes_PMK_t *)tlv_buf;
					ppmk_tlv->header.len =
						wlan_le16_to_cpu(ppmk_tlv->
								 header.len);
					sec->param.passphrase.psk_type =
						MLAN_PSK_PMK;
					memcpy_ext(pmpriv->adapter,
						   sec->param.passphrase.psk.
						   pmk.pmk, ppmk_tlv->pmk,
						   ppmk_tlv->header.len,
						   MLAN_MAX_KEY_LENGTH);
					tlv_buf +=
						ppmk_tlv->header.len +
						sizeof(MrvlIEtypesHeader_t);
					tlv_buf_len -=
						(ppmk_tlv->header.len +
						 sizeof(MrvlIEtypesHeader_t));
					break;
				}
			}
#ifdef STA_SUPPORT
			if (GET_BSS_ROLE(pmpriv) == MLAN_BSS_ROLE_STA &&
			    ((mlan_ds_bss *)pioctl_buf->pbuf)->sub_command ==
			    MLAN_OID_BSS_FIND_BSS) {
				wlan_set_ewpa_mode(pmpriv,
						   &sec->param.passphrase);
				ret = wlan_find_bss(pmpriv, pioctl_buf);
			}
#endif

		} else if (action == HostCmd_ACT_GEN_SET) {
			PRINTM(MINFO, "Esupp PMK set: enable ewpa query\n");
			pmpriv->ewpa_query = MTRUE;
		}
		if (sec_buf)
			pcb->moal_mfree(pmadapter->pmoal_handle,
					(t_u8 *)sec_buf);
	}

	LEAVE();
	return ret;
}

/**
 *  @brief This function prepares command of independent reset.
 *
 *  @param cmd          A pointer to HostCmd_DS_COMMAND structure
 *  @param cmd_action   the action: GET or SET
 *  @param pdata_buf    A pointer to data buffer
 *  @return             MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_cmd_ind_rst_cfg(HostCmd_DS_COMMAND *cmd,
		     t_u16 cmd_action, t_void *pdata_buf)
{
	mlan_ds_ind_rst_cfg *pdata_ind_rst = (mlan_ds_ind_rst_cfg *) pdata_buf;
	HostCmd_DS_INDEPENDENT_RESET_CFG *ind_rst_cfg =
		(HostCmd_DS_INDEPENDENT_RESET_CFG *) & cmd->params.ind_rst_cfg;

	ENTER();

	cmd->command = wlan_cpu_to_le16(HostCmd_CMD_INDEPENDENT_RESET_CFG);
	cmd->size = wlan_cpu_to_le16(sizeof(HostCmd_DS_INDEPENDENT_RESET_CFG) +
				     S_DS_GEN);

	ind_rst_cfg->action = wlan_cpu_to_le16(cmd_action);
	if (cmd_action == HostCmd_ACT_GEN_SET) {
		ind_rst_cfg->ir_mode = pdata_ind_rst->ir_mode;
		ind_rst_cfg->gpio_pin = pdata_ind_rst->gpio_pin;
	}

	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function handles the command response of independent reset
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param resp         A pointer to HostCmd_DS_COMMAND
 *  @param pioctl_buf   A pointer to command buffer
 *
 *  @return             MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_ret_ind_rst_cfg(pmlan_private pmpriv,
		     HostCmd_DS_COMMAND *resp, mlan_ioctl_req *pioctl_buf)
{
	mlan_ds_misc_cfg *misc = MNULL;
	const HostCmd_DS_INDEPENDENT_RESET_CFG *ind_rst_cfg =
		(HostCmd_DS_INDEPENDENT_RESET_CFG *) & resp->params.ind_rst_cfg;

	ENTER();

	if (pioctl_buf) {
		misc = (mlan_ds_misc_cfg *)pioctl_buf->pbuf;

		if (wlan_le16_to_cpu(ind_rst_cfg->action) ==
		    HostCmd_ACT_GEN_GET) {
			misc->param.ind_rst_cfg.ir_mode = ind_rst_cfg->ir_mode;
			misc->param.ind_rst_cfg.gpio_pin =
				ind_rst_cfg->gpio_pin;
		}
	}

	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function prepares command of ps inactivity timeout.
 *
 *  @param pmpriv      A pointer to mlan_private structure
 *  @param cmd          A pointer to HostCmd_DS_COMMAND structure
 *  @param cmd_action   the action: GET or SET
 *  @param pdata_buf    A pointer to data buffer
 *  @return             MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_cmd_ps_inactivity_timeout(pmlan_private pmpriv,
			       HostCmd_DS_COMMAND *cmd,
			       t_u16 cmd_action, t_void *pdata_buf)
{
	t_u16 timeout = *((t_u16 *)pdata_buf);
	HostCmd_DS_802_11_PS_INACTIVITY_TIMEOUT *ps_inact_tmo =
		(HostCmd_DS_802_11_PS_INACTIVITY_TIMEOUT *)
		& cmd->params.ps_inact_tmo;

	ENTER();

	cmd->command =
		wlan_cpu_to_le16(HostCmd_CMD_802_11_PS_INACTIVITY_TIMEOUT);
	cmd->size =
		wlan_cpu_to_le16(sizeof(HostCmd_DS_802_11_PS_INACTIVITY_TIMEOUT)
				 + S_DS_GEN);

	ps_inact_tmo->action = wlan_cpu_to_le16(cmd_action);
	if (cmd_action == HostCmd_ACT_GEN_SET)
		ps_inact_tmo->inact_tmo = wlan_cpu_to_le16(timeout);

	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function prepares command of HostCmd_CMD_GET_TSF
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param cmd          A pointer to HostCmd_DS_COMMAND structure
 *  @param cmd_action   The action: GET
 *  @return             MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_cmd_get_tsf(pmlan_private pmpriv, HostCmd_DS_COMMAND *cmd,
		 t_u16 cmd_action)
{
	ENTER();

	cmd->command = wlan_cpu_to_le16(HostCmd_CMD_GET_TSF);
	cmd->size = wlan_cpu_to_le16((sizeof(HostCmd_DS_TSF)) + S_DS_GEN);

	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function handles the command response of HostCmd_CMD_GET_TSF
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param resp         A pointer to HostCmd_DS_COMMAND
 *  @param pioctl_buf   A pointer to mlan_ioctl_req structure
 *
 *  @return             MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_ret_get_tsf(pmlan_private pmpriv,
		 HostCmd_DS_COMMAND *resp, mlan_ioctl_req *pioctl_buf)
{
	mlan_ds_misc_cfg *misc_cfg = MNULL;
	HostCmd_DS_TSF *tsf_pointer = (HostCmd_DS_TSF *) & resp->params.tsf;

	ENTER();
	if (pioctl_buf) {
		misc_cfg = (mlan_ds_misc_cfg *)pioctl_buf->pbuf;
		misc_cfg->param.misc_tsf = wlan_le64_to_cpu(tsf_pointer->tsf);
	}

	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function handles the command response of chan_region_cfg
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param resp         A pointer to HostCmd_DS_COMMAND
 *  @param pioctl_buf   A pointer to command buffer
 *
 *  @return             MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_ret_chan_region_cfg(pmlan_private pmpriv,
			 HostCmd_DS_COMMAND *resp, mlan_ioctl_req *pioctl_buf)
{
	mlan_adapter *pmadapter = pmpriv->adapter;
	t_u16 action;
	HostCmd_DS_CHAN_REGION_CFG *reg = MNULL;
	t_u8 *tlv_buf = MNULL;
	t_u16 tlv_buf_left;
	mlan_ds_misc_cfg *misc_cfg = MNULL;
	mlan_ds_misc_chnrgpwr_cfg *cfg = MNULL;
	mlan_status ret = MLAN_STATUS_SUCCESS;

	ENTER();

	reg = (HostCmd_DS_CHAN_REGION_CFG *) & resp->params.reg_cfg;
	if (!reg) {
		ret = MLAN_STATUS_FAILURE;
		goto done;
	}

	action = wlan_le16_to_cpu(reg->action);
	if (action != HostCmd_ACT_GEN_GET) {
		ret = MLAN_STATUS_FAILURE;
		goto done;
	}

	tlv_buf = (t_u8 *)reg + sizeof(*reg);
	tlv_buf_left = wlan_le16_to_cpu(resp->size) - S_DS_GEN - sizeof(*reg);

	/* Add FW cfp tables and region info */
	wlan_add_fw_cfp_tables(pmpriv, tlv_buf, tlv_buf_left);
	if (pmadapter->otp_region) {
		wlan_set_regiontable(pmpriv,
				     (t_u8)pmadapter->region_code,
				     pmadapter->fw_bands);
	}
	if (!pioctl_buf)
		goto done;

	if (!pioctl_buf->pbuf) {
		ret = MLAN_STATUS_FAILURE;
		goto done;
	}

	misc_cfg = (mlan_ds_misc_cfg *)pioctl_buf->pbuf;

	if (misc_cfg->sub_command == MLAN_OID_MISC_GET_REGIONPWR_CFG) {
		cfg = (mlan_ds_misc_chnrgpwr_cfg *) & (misc_cfg->param.
						       rgchnpwr_cfg);
		cfg->length = wlan_le16_to_cpu(resp->size);
		memcpy_ext(pmpriv->adapter, cfg->chnrgpwr_buf, (t_u8 *)resp,
			   cfg->length, sizeof(cfg->chnrgpwr_buf));
	} else {
		memset(pmpriv->adapter, &misc_cfg->param.custom_reg_domain, 0,
		       sizeof(mlan_ds_custom_reg_domain));
		if (pmadapter->otp_region)
			memcpy_ext(pmpriv->adapter,
				   &misc_cfg->param.custom_reg_domain.region,
				   pmadapter->otp_region,
				   sizeof(otp_region_info_t),
				   sizeof(otp_region_info_t));
		if (pmadapter->cfp_otp_bg) {
			misc_cfg->param.custom_reg_domain.num_bg_chan =
				pmadapter->tx_power_table_bg_rows;
			memcpy_ext(pmpriv->adapter,
				   (t_u8 *)misc_cfg->param.custom_reg_domain.
				   cfp_tbl, (t_u8 *)pmadapter->cfp_otp_bg,
				   pmadapter->tx_power_table_bg_rows *
				   sizeof(chan_freq_power_t),
				   pmadapter->tx_power_table_bg_rows *
				   sizeof(chan_freq_power_t));
		}
		if (pmadapter->cfp_otp_a) {
			misc_cfg->param.custom_reg_domain.num_a_chan =
				pmadapter->tx_power_table_a_rows;
			memcpy_ext(pmpriv->adapter,
				   (t_u8 *)misc_cfg->param.custom_reg_domain.
				   cfp_tbl +
				   pmadapter->tx_power_table_bg_rows *
				   sizeof(chan_freq_power_t),
				   (t_u8 *)pmadapter->cfp_otp_a,
				   pmadapter->tx_power_table_a_rows *
				   sizeof(chan_freq_power_t),
				   pmadapter->tx_power_table_a_rows *
				   sizeof(chan_freq_power_t));
		}
	}
done:
	LEAVE();
	return ret;
}

/**
 *  @brief This function prepares command of packet aggragation
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param cmd          A pointer to HostCmd_DS_COMMAND structure
 *  @param cmd_action   the action: GET or SET
 *  @param pdata_buf    A pointer to data buffer
 *  @return             MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
mlan_status
wlan_cmd_packet_aggr_ctrl(pmlan_private pmpriv,
			  HostCmd_DS_COMMAND *cmd,
			  t_u16 cmd_action, t_void *pdata_buf)
{
	HostCmd_DS_PACKET_AGGR_CTRL *aggr_ctrl = &cmd->params.aggr_ctrl;
	mlan_ds_misc_aggr_ctrl *aggr = (mlan_ds_misc_aggr_ctrl *) pdata_buf;

	ENTER();

	cmd->command = wlan_cpu_to_le16(HostCmd_CMD_PACKET_AGGR_CTRL);
	aggr_ctrl->action = wlan_cpu_to_le16(cmd_action);
	cmd->size = wlan_cpu_to_le16(sizeof(HostCmd_DS_PACKET_AGGR_CTRL) +
				     S_DS_GEN);
	aggr_ctrl->aggr_enable = 0;

	if (aggr->tx.enable)
		aggr_ctrl->aggr_enable |= MBIT(0);
	aggr_ctrl->aggr_enable = wlan_cpu_to_le16(aggr_ctrl->aggr_enable);

	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function handles the command response of
 *  packet aggregation
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param resp         A pointer to HostCmd_DS_COMMAND
 *  @param pioctl_buf   A pointer to command buffer
 *
 *  @return             MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_ret_packet_aggr_ctrl(pmlan_private pmpriv,
			  HostCmd_DS_COMMAND *resp, mlan_ioctl_req *pioctl_buf)
{
	mlan_ds_misc_cfg *misc = MNULL;
	HostCmd_DS_PACKET_AGGR_CTRL *aggr_ctrl = &resp->params.aggr_ctrl;
	mlan_ds_misc_aggr_ctrl *aggr = MNULL;
#if defined(USB)
	mlan_adapter *pmadapter = pmpriv->adapter;
	t_u8 i;
	t_u8 change = 0;
	usb_tx_aggr_params *pusb_tx_aggr = MNULL;
#endif

	ENTER();

	aggr_ctrl->aggr_enable = wlan_le16_to_cpu(aggr_ctrl->aggr_enable);
	aggr_ctrl->tx_aggr_max_size =
		wlan_le16_to_cpu(aggr_ctrl->tx_aggr_max_size);
	aggr_ctrl->tx_aggr_max_num =
		wlan_le16_to_cpu(aggr_ctrl->tx_aggr_max_num);
	aggr_ctrl->tx_aggr_align = wlan_le16_to_cpu(aggr_ctrl->tx_aggr_align);
	PRINTM(MCMND, "enable=0x%x, tx_size=%d, tx_num=%d, tx_align=%d\n",
	       aggr_ctrl->aggr_enable, aggr_ctrl->tx_aggr_max_size,
	       aggr_ctrl->tx_aggr_max_num, aggr_ctrl->tx_aggr_align);
#if defined(USB)
	if (IS_USB(pmadapter->card_type)) {
		if (aggr_ctrl->aggr_enable & MBIT(0)) {
			if (!pmadapter->pcard_usb->usb_tx_aggr[0]
			    .aggr_ctrl.enable) {
				pmadapter->pcard_usb->usb_tx_aggr[0]
					.aggr_ctrl.enable = MTRUE;
				change = MTRUE;
			}

		} else {
			if (pmadapter->pcard_usb->usb_tx_aggr[0]
			    .aggr_ctrl.enable) {
				pmadapter->pcard_usb->usb_tx_aggr[0]
					.aggr_ctrl.enable = MFALSE;
				change = MTRUE;
			}
		}
		pmadapter->pcard_usb->usb_tx_aggr[0].aggr_ctrl.aggr_mode =
			MLAN_USB_AGGR_MODE_LEN_V2;
		pmadapter->pcard_usb->usb_tx_aggr[0].aggr_ctrl.aggr_align =
			aggr_ctrl->tx_aggr_align;
		pmadapter->pcard_usb->usb_tx_aggr[0].aggr_ctrl.aggr_max =
			aggr_ctrl->tx_aggr_max_size;
		pmadapter->pcard_usb->usb_tx_aggr[0].aggr_ctrl.aggr_tmo =
			MLAN_USB_TX_AGGR_TIMEOUT_MSEC * 1000;
		if (change) {
			wlan_reset_usb_tx_aggr(pmadapter);
			for (i = 0; i < pmadapter->priv_num; ++i) {
				if (pmadapter->priv[i]) {
					pusb_tx_aggr =
						wlan_get_usb_tx_aggr_params
						(pmadapter, pmadapter->priv[i]
						 ->port);
					if (pusb_tx_aggr &&
					    pusb_tx_aggr->aggr_ctrl.aggr_mode ==
					    MLAN_USB_AGGR_MODE_LEN_V2)
						pmadapter->priv[i]->
							intf_hr_len =
							MLAN_USB_TX_AGGR_HEADER;
					else
						pmadapter->priv[i]->
							intf_hr_len =
							USB_INTF_HEADER_LEN;
				}
			}
		}
	}
#endif
	if (pioctl_buf) {
		misc = (mlan_ds_misc_cfg *)pioctl_buf->pbuf;
		aggr = (mlan_ds_misc_aggr_ctrl *) & (misc->param.aggr_params);
		if (aggr_ctrl->aggr_enable & MBIT(0))
			aggr->tx.enable = MTRUE;
		else
			aggr->tx.enable = MFALSE;
		aggr->tx.aggr_align = aggr_ctrl->tx_aggr_align;
		aggr->tx.aggr_max_size = aggr_ctrl->tx_aggr_max_size;
		aggr->tx.aggr_max_num = aggr_ctrl->tx_aggr_max_num;
#if defined(USB)
		if (IS_USB(pmadapter->card_type))
			aggr->tx.aggr_tmo = pmadapter->pcard_usb->usb_tx_aggr[0]
				.aggr_ctrl.aggr_tmo;
#endif
	}
	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function sends fw dump event command to firmware.
 *
 *  @param pmpriv         A pointer to mlan_private structure
 *  @param cmd          Hostcmd ID
 *  @param cmd_action   Command action
 *  @param pdata_buf    A void pointer to information buffer
 *  @return             N/A
 */
mlan_status
wlan_cmd_fw_dump_event(pmlan_private pmpriv,
		       HostCmd_DS_COMMAND *cmd,
		       t_u16 cmd_action, t_void *pdata_buf)
{
	ENTER();

	cmd->command = wlan_cpu_to_le16(HostCmd_CMD_FW_DUMP_EVENT);
	cmd->size = S_DS_GEN;
	cmd->size = wlan_cpu_to_le16(cmd->size);

	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function prepares command of get link layer statistics.
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param cmd          A pointer to HostCmd_DS_COMMAND structure
 *  @param cmd_action   the action: GET or SET
 *  @param pdata_buf    A pointer to data buffer
 *
 *  @return             MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 *
 */
mlan_status
wlan_cmd_802_11_link_statistic(pmlan_private pmpriv,
			       HostCmd_DS_COMMAND *cmd,
			       t_u16 cmd_action, mlan_ioctl_req *pioctl_buf)
{
	mlan_ds_get_info *info = (mlan_ds_get_info *)(pioctl_buf->pbuf);
	HostCmd_DS_802_11_LINK_STATISTIC *ll_stat =
		&cmd->params.get_link_statistic;
	wifi_link_layer_params *ll_params = MNULL;

	ENTER();

	cmd->command = wlan_cpu_to_le16(HostCmd_CMD_802_11_LINK_STATS);
	cmd->size = wlan_cpu_to_le16(S_DS_GEN +
				     sizeof(HostCmd_DS_802_11_LINK_STATISTIC));
	ll_stat->action = wlan_cpu_to_le16(cmd_action);

	switch (cmd_action) {
	case HostCmd_ACT_GEN_SET:
		ll_params =
			(wifi_link_layer_params *) info->param.link_statistic;
		ll_stat->mpdu_size_threshold =
			wlan_cpu_to_le32(ll_params->mpdu_size_threshold);
		ll_stat->aggressive_statistics_gathering =
			wlan_cpu_to_le32(ll_params->
					 aggressive_statistics_gathering);
		break;
	case HostCmd_ACT_GEN_GET:
		/** ll_stat->stat_type = wlan_cpu_to_le16(stat_type); */
		break;
	default:
		break;
	}

	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function fill link layer statistic from firmware
 *
 *  @param priv       					A pointer to mlan_private
 * structure
 *  @param link_statistic_ioctl_buf,    A pointer to fill ioctl buffer
 *  @param resp         				A pointer to
 * HostCmd_DS_COMMAND
 *
 *  @return             MLAN_STATUS_SUCCESS
 */
static void
wlan_fill_link_statistic(mlan_private *priv,
			 char *link_statistic_ioctl_buf,
			 HostCmd_DS_COMMAND *resp)
{
	char *link_statistic = link_statistic_ioctl_buf;
	wifi_radio_stat *radio_stat = MNULL;
	wifi_iface_stat *iface_stat = MNULL;
	mlan_wifi_iface_stat *fw_ifaceStat = MNULL;
	mlan_wifi_radio_stat *fw_radioStat = MNULL;
	t_u32 num_radio = 0;
	int i = 0, chan_idx = 0, peerIdx = 0, rate_idx = 0;
	t_u16 left_len = 0, tlv_type = 0, tlv_len = 0;
	MrvlIEtypesHeader_t *tlv = MNULL;
	HostCmd_DS_802_11_LINK_STATISTIC *plink_stat =
		(HostCmd_DS_802_11_LINK_STATISTIC *) & resp->params.
		get_link_statistic;

	/* TLV parse */
	left_len = resp->size - sizeof(HostCmd_DS_802_11_LINK_STATISTIC) -
		S_DS_GEN;
	tlv = (MrvlIEtypesHeader_t *)(plink_stat->value);
	DBG_HEXDUMP(MDAT_D, "tlv:", (void *)tlv, 1024);
	while (left_len > sizeof(MrvlIEtypesHeader_t)) {
		tlv_type = wlan_le16_to_cpu(tlv->type);
		tlv_len = wlan_le16_to_cpu(tlv->len);
		switch (tlv_type) {
		case TLV_TYPE_LL_STAT_IFACE:
			fw_ifaceStat = (mlan_wifi_iface_stat
					*) ((t_u8 *)tlv +
					    sizeof(MrvlIEtypesHeader_t));
			break;
		case TLV_TYPE_LL_STAT_RADIO:
			fw_radioStat = (mlan_wifi_radio_stat
					*) ((t_u8 *)tlv +
					    sizeof(MrvlIEtypesHeader_t));
			num_radio = MAX_RADIO;
			break;
		default:
			break;
		}
		left_len -= (sizeof(MrvlIEtypesHeader_t) + tlv_len);
		tlv = (MrvlIEtypesHeader_t *)((t_u8 *)tlv + tlv_len +
					      sizeof(MrvlIEtypesHeader_t));
	}

	if (!fw_ifaceStat || !fw_radioStat) {
		PRINTM(MERROR, "!fw_ifaceStat || !fw_radioStat\n");
		return;
	}

	*((t_u32 *)link_statistic) = num_radio;
	link_statistic += sizeof(num_radio);

	/* Fill radio stats array */
	for (i = 0; i < num_radio; i++) {
		radio_stat = (wifi_radio_stat *) link_statistic;
		link_statistic += sizeof(wifi_radio_stat);

		radio_stat->radio = wlan_le32_to_cpu(fw_radioStat[i].radio);

		radio_stat->on_time = wlan_le32_to_cpu(fw_radioStat[i].on_time);
		radio_stat->tx_time = wlan_le32_to_cpu(fw_radioStat[i].tx_time);
		radio_stat->reserved0 =
			wlan_le32_to_cpu(fw_radioStat[i].reserved0);
		radio_stat->rx_time = wlan_le32_to_cpu(fw_radioStat[i].rx_time);
		radio_stat->on_time_scan =
			wlan_le32_to_cpu(fw_radioStat[i].on_time_scan);
		radio_stat->on_time_nbd =
			wlan_le32_to_cpu(fw_radioStat[i].on_time_nbd);
		radio_stat->on_time_gscan =
			wlan_le32_to_cpu(fw_radioStat[i].on_time_gscan);
		radio_stat->on_time_roam_scan =
			wlan_le32_to_cpu(fw_radioStat[i].on_time_roam_scan);
		radio_stat->on_time_pno_scan =
			wlan_le32_to_cpu(fw_radioStat[i].on_time_pno_scan);
		radio_stat->on_time_hs20 =
			wlan_le32_to_cpu(fw_radioStat[i].on_time_hs20);

		radio_stat->num_channels =
			wlan_le32_to_cpu(fw_radioStat[i].num_channels);
		for (chan_idx = 0; chan_idx < radio_stat->num_channels;
		     chan_idx++) {
			if (radio_stat->num_channels > MAX_NUM_CHAN) {
				radio_stat->num_channels =
					wlan_le32_to_cpu(MAX_NUM_CHAN);
				PRINTM(MERROR,
				       "%s : radio_stat->num_channels=%d\n",
				       __func__, radio_stat->num_channels);
				break;
			}
			radio_stat->channels[chan_idx].channel.width =
				wlan_le32_to_cpu(fw_radioStat[i]
						 .channels[chan_idx]
						 .channel.width);
			radio_stat->channels[chan_idx].channel.center_freq =
				wlan_le32_to_cpu(fw_radioStat[i]
						 .channels[chan_idx]
						 .channel.center_freq);
			radio_stat->channels[chan_idx].channel.center_freq0 =
				wlan_le32_to_cpu(fw_radioStat[i]
						 .channels[chan_idx]
						 .channel.center_freq0);
			radio_stat->channels[chan_idx].channel.center_freq1 =
				wlan_le32_to_cpu(fw_radioStat[i]
						 .channels[chan_idx]
						 .channel.center_freq1);

			radio_stat->channels[chan_idx]
				.on_time =
				wlan_le32_to_cpu(fw_radioStat[i].
						 channels[chan_idx].on_time);
			radio_stat->channels[chan_idx].cca_busy_time =
				wlan_le32_to_cpu(fw_radioStat[i]
						 .channels[chan_idx]
						 .cca_busy_time);
		}
	}

	/* Fill iface stats */
	iface_stat = (wifi_iface_stat *) link_statistic;

	/* get wifi_interface_link_layer_info in driver, not in firmware */
	if (priv->bss_role == MLAN_BSS_ROLE_STA) {
		iface_stat->info.mode = MLAN_INTERFACE_STA;
		if (priv->media_connected)
			iface_stat->info.state = MLAN_ASSOCIATING;
		else
			iface_stat->info.state = MLAN_DISCONNECTED;
		iface_stat->info.roaming = MLAN_ROAMING_IDLE;
		iface_stat->info.capabilities = MLAN_CAPABILITY_QOS;
		memcpy_ext(priv->adapter, iface_stat->info.ssid,
			   priv->curr_bss_params.bss_descriptor.ssid.ssid,
			   MLAN_MAX_SSID_LENGTH, MLAN_MAX_SSID_LENGTH);
		memcpy_ext(priv->adapter, iface_stat->info.bssid,
			   priv->curr_bss_params.bss_descriptor.mac_address,
			   MLAN_MAC_ADDR_LENGTH, MLAN_MAC_ADDR_LENGTH);
	} else {
		iface_stat->info.mode = MLAN_INTERFACE_SOFTAP;
		iface_stat->info.capabilities = MLAN_CAPABILITY_QOS;
	}
	memcpy_ext(priv->adapter, iface_stat->info.mac_addr, priv->curr_addr,
		   MLAN_MAC_ADDR_LENGTH, MLAN_MAC_ADDR_LENGTH);
	memcpy_ext(priv->adapter, iface_stat->info.ap_country_str,
		   priv->adapter->country_code, COUNTRY_CODE_LEN,
		   COUNTRY_CODE_LEN);
	memcpy_ext(priv->adapter, iface_stat->info.country_str,
		   priv->adapter->country_code, COUNTRY_CODE_LEN,
		   COUNTRY_CODE_LEN);

	iface_stat->beacon_rx = wlan_le32_to_cpu(fw_ifaceStat->beacon_rx);
	iface_stat->average_tsf_offset =
		wlan_le64_to_cpu(fw_ifaceStat->average_tsf_offset);
	iface_stat->leaky_ap_detected =
		wlan_le32_to_cpu(fw_ifaceStat->leaky_ap_detected);
	iface_stat->leaky_ap_avg_num_frames_leaked =
		wlan_le32_to_cpu(fw_ifaceStat->leaky_ap_avg_num_frames_leaked);
	iface_stat->leaky_ap_guard_time =
		wlan_le32_to_cpu(fw_ifaceStat->leaky_ap_guard_time);

	/* Value of iface_stat should be Reaccumulate by each peer */
	iface_stat->mgmt_rx = wlan_le32_to_cpu(fw_ifaceStat->mgmt_rx);
	iface_stat->mgmt_action_rx =
		wlan_le32_to_cpu(fw_ifaceStat->mgmt_action_rx);
	iface_stat->mgmt_action_tx =
		wlan_le32_to_cpu(fw_ifaceStat->mgmt_action_tx);

	iface_stat->rssi_mgmt = wlan_le32_to_cpu(fw_ifaceStat->rssi_mgmt);
	iface_stat->rssi_data = wlan_le32_to_cpu(fw_ifaceStat->rssi_data);
	iface_stat->rssi_ack = wlan_le32_to_cpu(fw_ifaceStat->rssi_ack);

	for (i = WMM_AC_BK; i <= WMM_AC_VO; i++) {
		iface_stat->ac[i].ac = i;
		iface_stat->ac[i].tx_mpdu =
			wlan_le32_to_cpu(fw_ifaceStat->ac[i].tx_mpdu);
		iface_stat->ac[i].rx_mpdu =
			wlan_le32_to_cpu(fw_ifaceStat->ac[i].rx_mpdu);
		iface_stat->ac[i].tx_mcast =
			wlan_le32_to_cpu(fw_ifaceStat->ac[i].tx_mcast);
		iface_stat->ac[i].rx_mcast =
			wlan_le32_to_cpu(fw_ifaceStat->ac[i].rx_mcast);
		iface_stat->ac[i].rx_ampdu =
			wlan_le32_to_cpu(fw_ifaceStat->ac[i].rx_ampdu);
		iface_stat->ac[i].tx_ampdu =
			wlan_le32_to_cpu(fw_ifaceStat->ac[i].tx_ampdu);
		iface_stat->ac[i].mpdu_lost =
			wlan_le32_to_cpu(fw_ifaceStat->ac[i].mpdu_lost);
		iface_stat->ac[i].retries =
			wlan_le32_to_cpu(fw_ifaceStat->ac[i].retries);
		iface_stat->ac[i].retries_short =
			wlan_le32_to_cpu(fw_ifaceStat->ac[i].retries_short);
		iface_stat->ac[i].retries_long =
			wlan_le32_to_cpu(fw_ifaceStat->ac[i].retries_long);
		iface_stat->ac[i].contention_time_min =
			wlan_le32_to_cpu(fw_ifaceStat->ac[i].
					 contention_time_min);
		iface_stat->ac[i].contention_time_max =
			wlan_le32_to_cpu(fw_ifaceStat->ac[i].
					 contention_time_max);
		iface_stat->ac[i].contention_time_avg =
			wlan_le32_to_cpu(fw_ifaceStat->ac[i].
					 contention_time_avg);
		iface_stat->ac[i].contention_num_samples =
			wlan_le32_to_cpu(fw_ifaceStat->ac[i].
					 contention_num_samples);
	}

	/* LL_STAT V3: STA-solution: support maxium 1 peers for AP */
	iface_stat->num_peers = wlan_le32_to_cpu(fw_ifaceStat->num_peers);
	for (peerIdx = 0; peerIdx < iface_stat->num_peers; peerIdx++) {
		iface_stat->peer_info[peerIdx].type =
			fw_ifaceStat->peer_info[peerIdx].type;
		memcpy_ext(priv->adapter,
			   iface_stat->peer_info[peerIdx].peer_mac_address,
			   fw_ifaceStat->peer_info[peerIdx].peer_mac_address,
			   MLAN_MAC_ADDR_LENGTH, MLAN_MAC_ADDR_LENGTH);
		iface_stat->peer_info[peerIdx].capabilities =
			wlan_le32_to_cpu(fw_ifaceStat->peer_info[peerIdx].
					 capabilities);
		iface_stat->peer_info[peerIdx].num_rate =
			wlan_le32_to_cpu(fw_ifaceStat->peer_info[peerIdx].
					 num_rate);

		PRINTM(MINFO,
		       "bitrate  tx_mpdu  rx_mpdu  mpdu_lost retries retries_short retries_long\n");
		for (rate_idx = 0;
		     rate_idx < iface_stat->peer_info[peerIdx].num_rate;
		     rate_idx++) {
			wlan_fill_hal_wifi_rate(priv,
						&fw_ifaceStat->
						peer_info[peerIdx]
						.rate_stats[rate_idx]
						.rate,
						&iface_stat->peer_info[peerIdx]
						.rate_stats[rate_idx]
						.rate);

			iface_stat->peer_info[peerIdx]
				.rate_stats[rate_idx]
				.tx_mpdu =
				wlan_le32_to_cpu(fw_ifaceStat->
						 peer_info[peerIdx]
						 .rate_stats[rate_idx]
						 .tx_mpdu);
			iface_stat->peer_info[peerIdx]
				.rate_stats[rate_idx]
				.rx_mpdu =
				wlan_le32_to_cpu(fw_ifaceStat->
						 peer_info[peerIdx]
						 .rate_stats[rate_idx]
						 .rx_mpdu);
			iface_stat->peer_info[peerIdx]
				.rate_stats[rate_idx]
				.mpdu_lost =
				wlan_le32_to_cpu(fw_ifaceStat->
						 peer_info[peerIdx]
						 .rate_stats[rate_idx]
						 .mpdu_lost);
			iface_stat->peer_info[peerIdx]
				.rate_stats[rate_idx]
				.retries =
				wlan_le32_to_cpu(fw_ifaceStat->
						 peer_info[peerIdx]
						 .rate_stats[rate_idx]
						 .retries);
			iface_stat->peer_info[peerIdx]
				.rate_stats[rate_idx]
				.retries_short =
				wlan_le32_to_cpu(fw_ifaceStat->
						 peer_info[peerIdx]
						 .rate_stats[rate_idx]
						 .retries_short);
			iface_stat->peer_info[peerIdx]
				.rate_stats[rate_idx]
				.retries_long =
				wlan_le32_to_cpu(fw_ifaceStat->
						 peer_info[peerIdx]
						 .rate_stats[rate_idx]
						 .retries_long);
			PRINTM(MDAT_D,
			       "0x%x  0x%x  0x%x  0x%x  0x%x  0x%x  0x%x\n",
			       iface_stat->peer_info[peerIdx]
			       .rate_stats[rate_idx]
			       .rate.bitrate, iface_stat->peer_info[peerIdx]
			       .rate_stats[rate_idx]
			       .tx_mpdu, iface_stat->peer_info[peerIdx]
			       .rate_stats[rate_idx]
			       .rx_mpdu, iface_stat->peer_info[peerIdx]
			       .rate_stats[rate_idx]
			       .mpdu_lost, iface_stat->peer_info[peerIdx]
			       .rate_stats[rate_idx]
			       .retries, iface_stat->peer_info[peerIdx]
			       .rate_stats[rate_idx]
			       .retries_short, iface_stat->peer_info[peerIdx]
			       .rate_stats[rate_idx]
			       .retries_long);
		}
	}
}

/**
 *  @brief This function handles the command response of get_link_statistic
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param resp         A pointer to HostCmd_DS_COMMAND
 *  @param pioctl_buf   A pointer to mlan_ioctl_req structure
 *
 *  @return             MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_ret_get_link_statistic(pmlan_private pmpriv,
			    HostCmd_DS_COMMAND *resp,
			    mlan_ioctl_req *pioctl_buf)
{
	mlan_ds_get_info *info;
	t_u8 *link_statistic = MNULL;
	t_u16 action = wlan_le16_to_cpu(resp->params.get_link_statistic.action);

	ENTER();

	if (pioctl_buf) {
		info = (mlan_ds_get_info *)pioctl_buf->pbuf;
		link_statistic = info->param.link_statistic;

		switch (action) {
		case HostCmd_ACT_GEN_GET:
			wlan_fill_link_statistic(pmpriv, link_statistic, resp);
			break;
		case HostCmd_ACT_GEN_SET:
		case HostCmd_ACT_GEN_REMOVE:
			/* nothing to do */
			break;
		default:
			break;
		}
		/* Indicate ioctl complete */
		pioctl_buf->data_read_written =
			BUF_MAXLEN + MLAN_SUB_COMMAND_SIZE;
	}

	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function sends boot sleep configure command to firmware.
 *
 *  @param pmpriv         A pointer to mlan_private structure
 *  @param cmd          Hostcmd ID
 *  @param cmd_action   Command action
 *  @param pdata_buf    A void pointer to information buffer
 *  @return             MLAN_STATUS_SUCCESS/ MLAN_STATUS_FAILURE
 */
mlan_status
wlan_cmd_boot_sleep(pmlan_private pmpriv,
		    HostCmd_DS_COMMAND *cmd, t_u16 cmd_action,
		    t_void *pdata_buf)
{
	HostCmd_DS_BOOT_SLEEP *boot_sleep = MNULL;
	t_u16 enable = *(t_u16 *)pdata_buf;

	ENTER();

	cmd->command = wlan_cpu_to_le16(HostCmd_CMD_BOOT_SLEEP);
	boot_sleep = &cmd->params.boot_sleep;
	boot_sleep->action = wlan_cpu_to_le16(cmd_action);
	boot_sleep->enable = wlan_cpu_to_le16(enable);

	cmd->size = S_DS_GEN + sizeof(HostCmd_DS_BOOT_SLEEP);

	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function handles the command response of boot sleep cfg
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param resp         A pointer to HostCmd_DS_COMMAND
 *  @param pioctl_buf   A pointer to mlan_ioctl_req structure
 *
 *  @return        MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_ret_boot_sleep(pmlan_private pmpriv,
		    HostCmd_DS_COMMAND *resp, mlan_ioctl_req *pioctl_buf)
{
	HostCmd_DS_BOOT_SLEEP *boot_sleep = &resp->params.boot_sleep;
	mlan_ds_misc_cfg *cfg = (mlan_ds_misc_cfg *)pioctl_buf->pbuf;

	ENTER();

	cfg->param.boot_sleep = wlan_le16_to_cpu(boot_sleep->enable);
	PRINTM(MCMND, "boot sleep cfg status %u", cfg->param.boot_sleep);

	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

#if defined(DRV_EMBEDDED_AUTHENTICATOR) || defined(DRV_EMBEDDED_SUPPLICANT)

/**
 *  @brief This function handles send crypto command
 *
 *  @param pmpriv         A pointer to mlan_private structure
 *  @param cmd          Hostcmd ID
 *  @param cmd_action   Command action
 *  @param pdata_buf    A void pointer to information buffer
 *
 *  @return        MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_cmd_crypto(pmlan_private pmpriv, HostCmd_DS_COMMAND *cmd,
		t_u16 cmd_action, t_u16 *pdata_buf)
{
	HostCmd_DS_CRYPTO *cry_cmd = &cmd->params.crypto_cmd;
	mlan_ds_sup_cfg *cfg = (mlan_ds_sup_cfg *) pdata_buf;
#if defined(DRV_EMBEDDED_AUTHENTICATOR) || defined(DRV_EMBEDDED_SUPPLICANT)
	subcmd_prf_hmac_sha1_t *prf_hmac_sha1 = MNULL;
	subcmd_hmac_sha1_t *hmac_sha1 = MNULL;
	subcmd_hmac_sha256_t *hmac_sha256 = MNULL;
	subcmd_sha256_t *sha256 = MNULL;
	subcmd_rijndael_t *rijndael = MNULL;
	subcmd_rc4_t *rc4 = MNULL;
	subcmd_md5_t *md5 = MNULL;
	subcmd_mrvl_f_t *mrvl_f = MNULL;
	subcmd_sha256_kdf_t *sha256_kdf = MNULL;
	t_u8 *ptlv = MNULL;
	t_u8 tlv_bitmap = 0;
	t_u32 i = 0;
#endif
	ENTER();

	cmd->command = wlan_cpu_to_le16(HostCmd_CMD_CRYPTO);
	cmd->size = S_DS_GEN + sizeof(HostCmd_DS_CRYPTO);
	cry_cmd->action = wlan_cpu_to_le16(cmd_action);
	cry_cmd->subCmdCode = cfg->sub_command;
	switch (cfg->sub_command) {
#if defined(DRV_EMBEDDED_AUTHENTICATOR) || defined(DRV_EMBEDDED_SUPPLICANT)
	case HostCmd_CMD_CRYPTO_SUBCMD_PRF_HMAC_SHA1:
		tlv_bitmap = BIT_TLV_TYPE_CRYPTO_KEY |
			BIT_TLV_TYPE_CRYPTO_KEY_PREFIX |
			BIT_TLV_TYPE_CRYPTO_KEY_DATA_BLK;
		/* set subcmd start */
		prf_hmac_sha1 = (subcmd_prf_hmac_sha1_t *) cry_cmd->subCmd;
		prf_hmac_sha1->output_len = cfg->output_len;
		/* set tlv start */
		ptlv = prf_hmac_sha1->tlv;
		cmd->size += sizeof(subcmd_prf_hmac_sha1_t);
		break;
	case HostCmd_CMD_CRYPTO_SUBCMD_HMAC_SHA1:
		tlv_bitmap = BIT_TLV_TYPE_CRYPTO_KEY |
			BIT_TLV_TYPE_CRYPTO_KEY_DATA_BLK;
		/* set subcmd start */
		hmac_sha1 = (subcmd_hmac_sha1_t *) cry_cmd->subCmd;
		hmac_sha1->output_len = cfg->output_len;
		hmac_sha1->data_blks_nr = cfg->data_blks_nr;
		/* set tlv start */
		ptlv = hmac_sha1->tlv;
		cmd->size += sizeof(subcmd_hmac_sha1_t);
		break;
	case HostCmd_CMD_CRYPTO_SUBCMD_HMAC_SHA256:
		tlv_bitmap = BIT_TLV_TYPE_CRYPTO_KEY |
			BIT_TLV_TYPE_CRYPTO_KEY_DATA_BLK;
		/* set subcmd start */
		hmac_sha256 = (subcmd_hmac_sha256_t *) cry_cmd->subCmd;
		hmac_sha256->output_len = cfg->output_len;
		hmac_sha256->data_blks_nr = cfg->data_blks_nr;
		/* set tlv start */
		ptlv = hmac_sha256->tlv;
		cmd->size += sizeof(subcmd_hmac_sha256_t);
		break;
	case HostCmd_CMD_CRYPTO_SUBCMD_SHA256:
		tlv_bitmap = BIT_TLV_TYPE_CRYPTO_KEY_DATA_BLK;
		/* set subcmd start */
		sha256 = (subcmd_sha256_t *) cry_cmd->subCmd;
		sha256->output_len = cfg->output_len;
		sha256->data_blks_nr = cfg->data_blks_nr;
		/* set tlv start */
		ptlv = sha256->tlv;
		cmd->size += sizeof(subcmd_sha256_t);
		break;
	case HostCmd_CMD_CRYPTO_SUBCMD_RIJNDAEL:
		tlv_bitmap = BIT_TLV_TYPE_CRYPTO_KEY |
			BIT_TLV_TYPE_CRYPTO_KEY_DATA_BLK;
		/* set subcmd start */
		rijndael = (subcmd_rijndael_t *) cry_cmd->subCmd;
		rijndael->sub_action_code = cfg->sub_action_code;
		rijndael->output_len = cfg->output_len;
		/* set tlv start */
		ptlv = rijndael->tlv;
		cmd->size += sizeof(subcmd_rijndael_t);
		break;
	case HostCmd_CMD_CRYPTO_SUBCMD_RC4:
		tlv_bitmap = BIT_TLV_TYPE_CRYPTO_KEY |
			BIT_TLV_TYPE_CRYPTO_KEY_IV |
			BIT_TLV_TYPE_CRYPTO_KEY_DATA_BLK;
		/* set subcmd start */
		rc4 = (subcmd_rc4_t *) cry_cmd->subCmd;
		rc4->skip_bytes = cfg->skip_bytes;
		rc4->output_len = cfg->output_len;
		/* set tlv start */
		ptlv = rc4->tlv;
		cmd->size += sizeof(subcmd_rc4_t);
		break;
	case HostCmd_CMD_CRYPTO_SUBCMD_MD5:
		tlv_bitmap = BIT_TLV_TYPE_CRYPTO_KEY |
			BIT_TLV_TYPE_CRYPTO_KEY_DATA_BLK;
		/* set subcmd start */
		md5 = (subcmd_md5_t *) cry_cmd->subCmd;
		md5->output_len = cfg->output_len;
		/* set tlv start */
		ptlv = md5->tlv;
		cmd->size += sizeof(subcmd_md5_t);
		break;
	case HostCmd_CMD_CRYPTO_SUBCMD_MRVL_F:
		tlv_bitmap = BIT_TLV_TYPE_CRYPTO_KEY |
			BIT_TLV_TYPE_CRYPTO_KEY_DATA_BLK;
		/* set subcmd start */
		mrvl_f = (subcmd_mrvl_f_t *) cry_cmd->subCmd;
		mrvl_f->iterations = cfg->iteration;
		mrvl_f->count = cfg->count;
		mrvl_f->output_len = cfg->output_len;
		/* set tlv start */
		ptlv = mrvl_f->tlv;
		cmd->size += sizeof(subcmd_mrvl_f_t);
		break;
	case HostCmd_CMD_CRYPTO_SUBCMD_SHA256_KDF:
		tlv_bitmap = BIT_TLV_TYPE_CRYPTO_KEY |
			BIT_TLV_TYPE_CRYPTO_KEY_PREFIX |
			BIT_TLV_TYPE_CRYPTO_KEY_DATA_BLK;
		/* set subcmd start */
		sha256_kdf = (subcmd_sha256_kdf_t *) cry_cmd->subCmd;
		sha256_kdf->output_len = cfg->output_len;
		/* set tlv start */
		ptlv = sha256_kdf->tlv;
		cmd->size += sizeof(subcmd_sha256_kdf_t);
		break;
#endif
	default:
		break;
	}
#if defined(DRV_EMBEDDED_AUTHENTICATOR) || defined(DRV_EMBEDDED_SUPPLICANT)
	/* add tlv */
	if (tlv_bitmap & BIT_TLV_TYPE_CRYPTO_KEY) {
		((MrvlIEParamSet_t *)ptlv)->Type =
			wlan_cpu_to_le16(TLV_TYPE_CRYPTO_KEY);
		((MrvlIEParamSet_t *)ptlv)->Length =
			wlan_cpu_to_le16(cfg->key_len);
		memcpy_ext(pmpriv->adapter,
			   (t_u8 *)ptlv + sizeof(MrvlIEParamSet_t), cfg->key,
			   cfg->key_len, cfg->key_len);
		cmd->size += cfg->key_len + sizeof(MrvlIEParamSet_t);
		ptlv += cfg->key_len + sizeof(MrvlIEParamSet_t);
	}

	if (tlv_bitmap & BIT_TLV_TYPE_CRYPTO_KEY_PREFIX) {
		((MrvlIEParamSet_t *)ptlv)->Type =
			wlan_cpu_to_le16(TLV_TYPE_CRYPTO_KEY_PREFIX);
		((MrvlIEParamSet_t *)ptlv)->Length =
			wlan_cpu_to_le16(cfg->key_prefix_len);
		memcpy_ext(pmpriv->adapter, ptlv + sizeof(MrvlIEParamSet_t),
			   cfg->key_prefix, cfg->key_prefix_len,
			   cfg->key_prefix_len);
		cmd->size += cfg->key_prefix_len + sizeof(MrvlIEParamSet_t);
		ptlv += cfg->key_prefix_len + sizeof(MrvlIEParamSet_t);
	}

	if (tlv_bitmap & BIT_TLV_TYPE_CRYPTO_KEY_IV) {
		((MrvlIEParamSet_t *)ptlv)->Type =
			wlan_cpu_to_le16(TLV_TYPE_CRYPTO_KEY_IV);
		((MrvlIEParamSet_t *)ptlv)->Length =
			wlan_cpu_to_le16(cfg->key_iv_len);
		memcpy_ext(pmpriv->adapter, ptlv + sizeof(MrvlIEParamSet_t),
			   cfg->key_iv, cfg->key_iv_len, cfg->key_iv_len);
		cmd->size += cfg->key_iv_len + sizeof(MrvlIEParamSet_t);
		ptlv += cfg->key_iv_len + sizeof(MrvlIEParamSet_t);
	}

	if (tlv_bitmap & BIT_TLV_TYPE_CRYPTO_KEY_DATA_BLK) {
		t_u16 data_blk_len = 0;
		t_u8 *pdata_blk = MNULL;
		for (i = 0; i < cfg->data_blks_nr; i++) {
			data_blk_len = *(cfg->key_data_blk_len + i);
			pdata_blk = *(cfg->key_data_blk + i);
			((MrvlIEParamSet_t *)ptlv)->Type =
				wlan_cpu_to_le16(TLV_TYPE_CRYPTO_KEY_DATA_BLK);
			((MrvlIEParamSet_t *)ptlv)->Length =
				wlan_cpu_to_le16(data_blk_len);
			memcpy_ext(pmpriv->adapter,
				   ptlv + sizeof(MrvlIEParamSet_t), pdata_blk,
				   data_blk_len, data_blk_len);
			cmd->size += data_blk_len + sizeof(MrvlIEParamSet_t);
			ptlv += data_blk_len + sizeof(MrvlIEParamSet_t);
		}
	}
#endif
	HEXDUMP("HostCmd_DS_COMMAND wlan_cmd_crypto", cmd, cmd->size);

	cmd->size = wlan_cpu_to_le16(cmd->size);
	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function handles the command response of crypto command
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param resp         A pointer to HostCmd_DS_COMMAND
 *  @param pioctl_buf   A pointer to mlan_ioctl_req structure
 *
 *  @return        MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_ret_crypto(pmlan_private pmpriv,
		HostCmd_DS_COMMAND *resp, mlan_ioctl_req *pioctl_buf)
{
	HostCmd_DS_CRYPTO *crypto_cmd = &resp->params.crypto_cmd;
#if defined(DRV_EMBEDDED_AUTHENTICATOR) || defined(DRV_EMBEDDED_SUPPLICANT)
	mlan_adapter *pmadapter = pmpriv->adapter;
	mlan_callbacks *pcb = (pmlan_callbacks)&pmadapter->callbacks;
	mlan_ds_sup_cfg *cfg = (mlan_ds_sup_cfg *) pioctl_buf->pbuf;
#endif

	ENTER();
#if defined(DRV_EMBEDDED_AUTHENTICATOR) || defined(DRV_EMBEDDED_SUPPLICANT)
	if (!cfg) {
		PRINTM(MERROR, "wlan_ret_crypto cfg is null \n");
		goto done;
	}
	if (resp->result == HostCmd_RESULT_OK) {
		/* copy the result */
		memcpy_ext(pmpriv->adapter, cfg->output,
			   (t_u8 *)crypto_cmd + sizeof(HostCmd_DS_CRYPTO) +
			   sizeof(cfg->output_len),
			   cfg->output_len, cfg->output_len);
	}

	/* Prevent the ioctl from completing when the cmd is freed */
	if (cfg->call_back) {
		pmadapter->curr_cmd->pioctl_buf = MNULL;
		/* trigger wait q */
		pcb->moal_notify_hostcmd_complete(pmadapter->pmoal_handle,
						  pmpriv->bss_index);
	}
#endif
done:
	LEAVE();
	return MLAN_STATUS_SUCCESS;
}
#endif

/**
 *  @brief This function prepares command of mac_address.
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param cmd          A pointer to HostCmd_DS_COMMAND structure
 *  @param cmd_action   The action: GET or SET
 *
 *  @return             MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_cmd_802_11_mac_address(pmlan_private pmpriv,
			    HostCmd_DS_COMMAND *cmd, t_u16 cmd_action)
{
	ENTER();
	cmd->command = wlan_cpu_to_le16(HostCmd_CMD_802_11_MAC_ADDRESS);
	cmd->size = wlan_cpu_to_le16(sizeof(HostCmd_DS_802_11_MAC_ADDRESS) +
				     S_DS_GEN);
	cmd->result = 0;

	cmd->params.mac_addr.action = wlan_cpu_to_le16(cmd_action);

	if (cmd_action == HostCmd_ACT_GEN_SET) {
		memcpy_ext(pmpriv->adapter, cmd->params.mac_addr.mac_addr,
			   pmpriv->curr_addr, MLAN_MAC_ADDR_LENGTH,
			   MLAN_MAC_ADDR_LENGTH);
		/* HEXDUMP("SET_CMD: MAC ADDRESS-", priv->CurrentAddr, 6); */
	}
	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function handles the command response of mac_address
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param resp         A pointer to HostCmd_DS_COMMAND
 *  @param pioctl_buf   A pointer to mlan_ioctl_req structure
 *
 *  @return             MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_ret_802_11_mac_address(pmlan_private pmpriv,
			    HostCmd_DS_COMMAND *resp,
			    mlan_ioctl_req *pioctl_buf)
{
	HostCmd_DS_802_11_MAC_ADDRESS *pmac_addr = &resp->params.mac_addr;
	mlan_ds_bss *bss = MNULL;

	ENTER();

	memcpy_ext(pmpriv->adapter, pmpriv->curr_addr, pmac_addr->mac_addr,
		   MLAN_MAC_ADDR_LENGTH, MLAN_MAC_ADDR_LENGTH);

	PRINTM(MINFO, "MAC address: " MACSTR "\n", MAC2STR(pmpriv->curr_addr));
	if (pioctl_buf) {
		bss = (mlan_ds_bss *)pioctl_buf->pbuf;
		memcpy_ext(pmpriv->adapter, &bss->param.mac_addr,
			   pmpriv->curr_addr, MLAN_MAC_ADDR_LENGTH,
			   MLAN_MAC_ADDR_LENGTH);
		pioctl_buf->data_read_written =
			MLAN_MAC_ADDR_LENGTH + MLAN_SUB_COMMAND_SIZE;
	}
	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function prepares command of Rx abort cfg
 *
 *  @param pmpriv      A pointer to mlan_private structure
 *  @param cmd          A pointer to HostCmd_DS_COMMAND structure
 *  @param cmd_action   the action: GET or SET
 *  @param pdata_buf    A pointer to data buffer
 *  @return         MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_cmd_rxabortcfg(pmlan_private pmpriv,
		    HostCmd_DS_COMMAND *cmd, t_u16 cmd_action,
		    t_void *pdata_buf)
{
	HostCmd_DS_CMD_RX_ABORT_CFG *cfg_cmd =
		(HostCmd_DS_CMD_RX_ABORT_CFG *) & cmd->params.rx_abort_cfg;
	mlan_ds_misc_rx_abort_cfg *cfg =
		(mlan_ds_misc_rx_abort_cfg *) pdata_buf;

	ENTER();

	cmd->command = wlan_cpu_to_le16(HostCmd_CMD_RX_ABORT_CFG);
	cmd->size = wlan_cpu_to_le16(sizeof(HostCmd_DS_CMD_RX_ABORT_CFG) +
				     S_DS_GEN);
	cfg_cmd->action = wlan_cpu_to_le16(cmd_action);

	if (cmd_action == HostCmd_ACT_GEN_SET) {
		cfg_cmd->enable = (t_u8)cfg->enable;
		cfg_cmd->rssi_threshold = (t_s8)cfg->rssi_threshold;
	}

	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function handles the command response of Rx Abort Cfg
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param resp         A pointer to HostCmd_DS_COMMAND
 *  @param pioctl_buf   A pointer to mlan_ioctl_req structure
 *
 *  @return             MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_ret_rxabortcfg(pmlan_private pmpriv,
		    HostCmd_DS_COMMAND *resp, mlan_ioctl_req *pioctl_buf)
{
	HostCmd_DS_CMD_RX_ABORT_CFG *cfg_cmd =
		(HostCmd_DS_CMD_RX_ABORT_CFG *) & resp->params.rx_abort_cfg;
	mlan_ds_misc_cfg *misc_cfg = MNULL;

	ENTER();

	if (pioctl_buf) {
		misc_cfg = (mlan_ds_misc_cfg *)pioctl_buf->pbuf;
		misc_cfg->param.rx_abort_cfg.enable = (t_u8)cfg_cmd->enable;
		misc_cfg->param.rx_abort_cfg.rssi_threshold =
			(t_s8)cfg_cmd->rssi_threshold;
	}
	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function prepares command of Rx abort cfg ext
 *
 *  @param pmpriv      A pointer to mlan_private structure
 *  @param cmd          A pointer to HostCmd_DS_COMMAND structure
 *  @param cmd_action   the action: GET or SET
 *  @param pdata_buf    A pointer to data buffer
 *  @return         MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_cmd_rxabortcfg_ext(pmlan_private pmpriv,
			HostCmd_DS_COMMAND *cmd,
			t_u16 cmd_action, t_void *pdata_buf)
{
	HostCmd_DS_CMD_RX_ABORT_CFG_EXT *cfg_cmd =
		(HostCmd_DS_CMD_RX_ABORT_CFG_EXT *) & cmd->params.
		rx_abort_cfg_ext;
	mlan_ds_misc_rx_abort_cfg_ext *cfg =
		(mlan_ds_misc_rx_abort_cfg_ext *) pdata_buf;

	ENTER();

	cmd->command = wlan_cpu_to_le16(HostCmd_CMD_RX_ABORT_CFG_EXT);
	cmd->size = wlan_cpu_to_le16(sizeof(HostCmd_DS_CMD_RX_ABORT_CFG_EXT) +
				     S_DS_GEN);
	cfg_cmd->action = wlan_cpu_to_le16(cmd_action);

	if (cmd_action == HostCmd_ACT_GEN_SET) {
		cfg_cmd->enable = (t_u8)cfg->enable;
		cfg_cmd->rssi_margin = (t_s8)cfg->rssi_margin;
		cfg_cmd->ceil_rssi_threshold = (t_s8)cfg->ceil_rssi_threshold;
	}

	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function handles the command response of Rx Abort Cfg ext
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param resp         A pointer to HostCmd_DS_COMMAND
 *  @param pioctl_buf   A pointer to mlan_ioctl_req structure
 *
 *  @return             MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_ret_rxabortcfg_ext(pmlan_private pmpriv,
			HostCmd_DS_COMMAND *resp, mlan_ioctl_req *pioctl_buf)
{
	HostCmd_DS_CMD_RX_ABORT_CFG_EXT *cfg_cmd =
		(HostCmd_DS_CMD_RX_ABORT_CFG_EXT *) & resp->params.
		rx_abort_cfg_ext;
	mlan_ds_misc_cfg *misc_cfg = MNULL;

	ENTER();

	if (pioctl_buf) {
		misc_cfg = (mlan_ds_misc_cfg *)pioctl_buf->pbuf;
		misc_cfg->param.rx_abort_cfg_ext.enable = cfg_cmd->enable;
		misc_cfg->param.rx_abort_cfg_ext.rssi_margin =
			cfg_cmd->rssi_margin;
		misc_cfg->param.rx_abort_cfg_ext.ceil_rssi_threshold =
			cfg_cmd->ceil_rssi_threshold;
	}
	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 * @brief This function sets the hal/phy cfg params
 *
 * @param pmpriv       A pointer to mlan_private structure
 * @param cmd          A pointer to HostCmd_DS_COMMAND structure
 * @param cmd_action   The action: GET or SET
 * @param pdata_buf    A pointer to data buffer
 *
 * @return             MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_cmd_hal_phy_cfg(pmlan_private pmpriv,
		     HostCmd_DS_COMMAND *cmd,
		     t_u16 cmd_action, t_u16 *pdata_buf)
{
	HostCmd_DS_HAL_PHY_CFG *hal_phy_cfg_cmd =
		&cmd->params.hal_phy_cfg_params;
	mlan_ds_hal_phy_cfg_params *hal_phy_cfg_params = MNULL;

	ENTER();

	cmd->command = wlan_cpu_to_le16(HostCmd_CMD_HAL_PHY_CFG);
	cmd->size = sizeof(HostCmd_DS_HAL_PHY_CFG) + S_DS_GEN;
	hal_phy_cfg_cmd->action = wlan_cpu_to_le16(cmd_action);
	hal_phy_cfg_params = (mlan_ds_hal_phy_cfg_params *) pdata_buf;
	hal_phy_cfg_cmd->dot11b_psd_mask_cfg =
		hal_phy_cfg_params->dot11b_psd_mask_cfg;
	cmd->size = wlan_cpu_to_le16(cmd->size);
	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function handles the command response of hal_phy_cfg
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param resp         A pointer to HostCmd_DS_COMMAND
 *  @param pioctl_buf   A pointer to mlan_ioctl_req structure
 *
 *  @return             MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_ret_hal_phy_cfg(pmlan_private pmpriv,
		     HostCmd_DS_COMMAND *resp, mlan_ioctl_req *pioctl_buf)
{
	HostCmd_DS_HAL_PHY_CFG *cfg_cmd =
		(HostCmd_DS_HAL_PHY_CFG *) & resp->params.hal_phy_cfg_params;
	mlan_ds_misc_cfg *misc_cfg = MNULL;

	ENTER();

	if (pioctl_buf) {
		misc_cfg = (mlan_ds_misc_cfg *)pioctl_buf->pbuf;
		misc_cfg->param.hal_phy_cfg_params.dot11b_psd_mask_cfg =
			cfg_cmd->dot11b_psd_mask_cfg;
	}
	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function prepares command of Dot11mc unassoc ftm cfg
 *
 *  @param pmpriv      A pointer to mlan_private structure
 *  @param cmd          A pointer to HostCmd_DS_COMMAND structure
 *  @param cmd_action   the action: GET or SET
 *  @param pdata_buf    A pointer to data buffer
 *  @return         MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_cmd_dot11mc_unassoc_ftm_cfg(pmlan_private pmpriv,
				 HostCmd_DS_COMMAND *cmd,
				 t_u16 cmd_action, t_void *pdata_buf)
{
	HostCmd_DS_CMD_DOT11MC_UNASSOC_FTM_CFG *cfg_cmd =
		(HostCmd_DS_CMD_DOT11MC_UNASSOC_FTM_CFG *) & cmd->params.
		dot11mc_unassoc_ftm_cfg;
	mlan_ds_misc_dot11mc_unassoc_ftm_cfg *cfg =
		(mlan_ds_misc_dot11mc_unassoc_ftm_cfg *) pdata_buf;

	ENTER();

	cmd->command = wlan_cpu_to_le16(HostCmd_CMD_DOT11MC_UNASSOC_FTM_CFG);
	cmd->size =
		wlan_cpu_to_le16(sizeof(HostCmd_DS_CMD_DOT11MC_UNASSOC_FTM_CFG)
				 + S_DS_GEN);
	cfg_cmd->action = wlan_cpu_to_le16(cmd_action);

	if (cmd_action == HostCmd_ACT_GEN_SET) {
		cfg_cmd->state = wlan_cpu_to_le16(cfg->state);
	}

	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function handles the command response of Dot11mc unassoc ftm cfg
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param resp         A pointer to HostCmd_DS_COMMAND
 *  @param pioctl_buf   A pointer to mlan_ioctl_req structure
 *
 *  @return             MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_ret_dot11mc_unassoc_ftm_cfg(pmlan_private pmpriv,
				 HostCmd_DS_COMMAND *resp,
				 mlan_ioctl_req *pioctl_buf)
{
	HostCmd_DS_CMD_DOT11MC_UNASSOC_FTM_CFG *cfg_cmd =
		(HostCmd_DS_CMD_DOT11MC_UNASSOC_FTM_CFG *) & resp->params.
		dot11mc_unassoc_ftm_cfg;
	mlan_ds_misc_cfg *misc_cfg = MNULL;

	ENTER();

	if (pioctl_buf) {
		misc_cfg = (mlan_ds_misc_cfg *)pioctl_buf->pbuf;
		misc_cfg->param.dot11mc_unassoc_ftm_cfg.state =
			wlan_le16_to_cpu(cfg_cmd->state);
	}
	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function prepares command of Tx ampdu prot mode
 *
 *  @param pmpriv      A pointer to mlan_private structure
 *  @param cmd          A pointer to HostCmd_DS_COMMAND structure
 *  @param cmd_action   the action: GET or SET
 *  @param pdata_buf    A pointer to data buffer
 *  @return         MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_cmd_tx_ampdu_prot_mode(pmlan_private pmpriv,
			    HostCmd_DS_COMMAND *cmd,
			    t_u16 cmd_action, t_void *pdata_buf)
{
	HostCmd_DS_CMD_TX_AMPDU_PROT_MODE *cfg_cmd =
		(HostCmd_DS_CMD_TX_AMPDU_PROT_MODE *) & cmd->params.
		tx_ampdu_prot_mode;
	mlan_ds_misc_tx_ampdu_prot_mode *cfg =
		(mlan_ds_misc_tx_ampdu_prot_mode *) pdata_buf;

	ENTER();

	cmd->command = wlan_cpu_to_le16(HostCmd_CMD_TX_AMPDU_PROT_MODE);
	cmd->size = wlan_cpu_to_le16(sizeof(HostCmd_DS_CMD_TX_AMPDU_PROT_MODE) +
				     S_DS_GEN);
	cfg_cmd->action = wlan_cpu_to_le16(cmd_action);

	if (cmd_action == HostCmd_ACT_GEN_SET) {
		cfg_cmd->mode = wlan_cpu_to_le16(cfg->mode);
	}

	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function handles the command response of Tx ampdu prot mode
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param resp         A pointer to HostCmd_DS_COMMAND
 *  @param pioctl_buf   A pointer to mlan_ioctl_req structure
 *
 *  @return             MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_ret_tx_ampdu_prot_mode(pmlan_private pmpriv,
			    HostCmd_DS_COMMAND *resp,
			    mlan_ioctl_req *pioctl_buf)
{
	HostCmd_DS_CMD_TX_AMPDU_PROT_MODE *cfg_cmd =
		(HostCmd_DS_CMD_TX_AMPDU_PROT_MODE *) & resp->params.
		tx_ampdu_prot_mode;
	mlan_ds_misc_cfg *misc_cfg = MNULL;

	ENTER();

	if (pioctl_buf) {
		misc_cfg = (mlan_ds_misc_cfg *)pioctl_buf->pbuf;
		misc_cfg->param.tx_ampdu_prot_mode.mode =
			wlan_le16_to_cpu(cfg_cmd->mode);
	}
	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function prepares command of Rate Adapt cfg
 *
 *  @param pmpriv      A pointer to mlan_private structure
 *  @param cmd          A pointer to HostCmd_DS_COMMAND structure
 *  @param cmd_action   the action: GET or SET
 *  @param pdata_buf    A pointer to data buffer
 *  @return         MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_cmd_rate_adapt_cfg(pmlan_private pmpriv,
			HostCmd_DS_COMMAND *cmd,
			t_u16 cmd_action, t_void *pdata_buf)
{
	HostCmd_DS_CMD_RATE_ADAPT_CFG *cfg_cmd =
		(HostCmd_DS_CMD_RATE_ADAPT_CFG *) & cmd->params.rate_adapt_cfg;
	mlan_ds_misc_rate_adapt_cfg *cfg =
		(mlan_ds_misc_rate_adapt_cfg *) pdata_buf;

	ENTER();

	cmd->command = wlan_cpu_to_le16(HostCmd_CMD_RATE_ADAPT_CFG);
	cmd->size = wlan_cpu_to_le16(sizeof(HostCmd_DS_CMD_RATE_ADAPT_CFG) +
				     S_DS_GEN);
	cfg_cmd->action = wlan_cpu_to_le16(cmd_action);

	if (cmd_action == HostCmd_ACT_GEN_SET) {
		cfg_cmd->sr_rateadapt = (t_u8)cfg->sr_rateadapt;
		cfg_cmd->ra_low_thresh = (t_u8)cfg->ra_low_thresh;
		cfg_cmd->ra_high_thresh = (t_u8)cfg->ra_high_thresh;
		cfg_cmd->ra_interval = wlan_cpu_to_le16(cfg->ra_interval);
	}

	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function handles the command response of Rate Adapt Cfg
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param resp         A pointer to HostCmd_DS_COMMAND
 *  @param pioctl_buf   A pointer to mlan_ioctl_req structure
 *
 *  @return             MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_ret_rate_adapt_cfg(pmlan_private pmpriv,
			HostCmd_DS_COMMAND *resp, mlan_ioctl_req *pioctl_buf)
{
	HostCmd_DS_CMD_RATE_ADAPT_CFG *cfg_cmd =
		(HostCmd_DS_CMD_RATE_ADAPT_CFG *) & resp->params.rate_adapt_cfg;
	mlan_ds_misc_cfg *misc_cfg = MNULL;

	ENTER();

	if (pioctl_buf) {
		misc_cfg = (mlan_ds_misc_cfg *)pioctl_buf->pbuf;
		misc_cfg->param.rate_adapt_cfg.sr_rateadapt =
			(t_u8)cfg_cmd->sr_rateadapt;
		misc_cfg->param.rate_adapt_cfg.ra_low_thresh =
			(t_u8)cfg_cmd->ra_low_thresh;
		misc_cfg->param.rate_adapt_cfg.ra_high_thresh =
			(t_u8)cfg_cmd->ra_high_thresh;
		misc_cfg->param.rate_adapt_cfg.ra_interval =
			wlan_le16_to_cpu(cfg_cmd->ra_interval);
	}
	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function prepares command of CCK Desense cfg
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param cmd          A pointer to HostCmd_DS_COMMAND structure
 *  @param cmd_action   the action: GET or SET
 *  @param pdata_buf    A pointer to data buffer
 *  @return             MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_cmd_cck_desense_cfg(pmlan_private pmpriv,
			 HostCmd_DS_COMMAND *cmd,
			 t_u16 cmd_action, t_void *pdata_buf)
{
	HostCmd_DS_CMD_CCK_DESENSE_CFG *cfg_cmd =
		(HostCmd_DS_CMD_CCK_DESENSE_CFG *) & cmd->params.
		cck_desense_cfg;
	mlan_ds_misc_cck_desense_cfg *cfg =
		(mlan_ds_misc_cck_desense_cfg *) pdata_buf;

	ENTER();

	cmd->command = wlan_cpu_to_le16(HostCmd_CMD_CCK_DESENSE_CFG);
	cmd->size = wlan_cpu_to_le16(sizeof(HostCmd_DS_CMD_CCK_DESENSE_CFG) +
				     S_DS_GEN);
	cfg_cmd->action = wlan_cpu_to_le16(cmd_action);

	if (cmd_action == HostCmd_ACT_GEN_SET) {
		cfg_cmd->mode = wlan_cpu_to_le16(cfg->mode);
		cfg_cmd->margin = (t_s8)cfg->margin;
		cfg_cmd->ceil_thresh = (t_s8)cfg->ceil_thresh;
		cfg_cmd->num_on_intervals = (t_u8)cfg->num_on_intervals;
		cfg_cmd->num_off_intervals = (t_u8)cfg->num_off_intervals;
	}

	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function handles the command response of CCK Desense Cfg
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param resp         A pointer to HostCmd_DS_COMMAND
 *  @param pioctl_buf   A pointer to mlan_ioctl_req structure
 *
 *  @return             MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_ret_cck_desense_cfg(pmlan_private pmpriv,
			 HostCmd_DS_COMMAND *resp, mlan_ioctl_req *pioctl_buf)
{
	HostCmd_DS_CMD_CCK_DESENSE_CFG *cfg_cmd =
		(HostCmd_DS_CMD_CCK_DESENSE_CFG *) & resp->params.
		cck_desense_cfg;
	mlan_ds_misc_cfg *misc_cfg = MNULL;

	ENTER();

	if (pioctl_buf) {
		misc_cfg = (mlan_ds_misc_cfg *)pioctl_buf->pbuf;
		misc_cfg->param.cck_desense_cfg.mode =
			wlan_le16_to_cpu(cfg_cmd->mode);
		misc_cfg->param.cck_desense_cfg.margin = (t_s8)cfg_cmd->margin;
		misc_cfg->param.cck_desense_cfg.ceil_thresh =
			(t_s8)cfg_cmd->ceil_thresh;
		misc_cfg->param.cck_desense_cfg.num_on_intervals =
			(t_u8)cfg_cmd->num_on_intervals;
		misc_cfg->param.cck_desense_cfg.num_off_intervals =
			(t_u8)cfg_cmd->num_off_intervals;
	}
	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function sends dynamic bandwidth command to firmware.
 *
 *  @param pmpriv         A pointer to mlan_private structure
 *  @param cmd          Hostcmd ID
 *  @param cmd_action   Command action
 *  @param pdata_buf    A void pointer to information buffer
 *  @return             N/A
 */
mlan_status
wlan_cmd_config_dyn_bw(pmlan_private pmpriv,
		       HostCmd_DS_COMMAND *cmd,
		       t_u16 cmd_action, t_void *pdata_buf)
{
	HostCmd_DS_DYN_BW *dyn_bw_cmd = &cmd->params.dyn_bw;

	ENTER();

	cmd->command = wlan_cpu_to_le16(HostCmd_CMD_DYN_BW);
	cmd->size = S_DS_GEN + sizeof(HostCmd_DS_DYN_BW);
	dyn_bw_cmd->action = wlan_cpu_to_le16(cmd_action);
	dyn_bw_cmd->dyn_bw = wlan_cpu_to_le16(*(t_u16 *)pdata_buf);
	cmd->size = wlan_cpu_to_le16(cmd->size);

	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function handles the command response of dyn_bw
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param resp         A pointer to HostCmd_DS_COMMAND
 *  @param pioctl_buf   A pointer to mlan_ioctl_req structure
 *
 *  @return        MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_ret_dyn_bw(pmlan_private pmpriv,
		HostCmd_DS_COMMAND *resp, mlan_ioctl_req *pioctl_buf)
{
	mlan_ds_misc_cfg *cfg = MNULL;
	HostCmd_DS_DYN_BW *dyn_bw = &resp->params.dyn_bw;

	ENTER();
	if (pioctl_buf &&
	    (wlan_le16_to_cpu(dyn_bw->action) == HostCmd_ACT_GEN_GET)) {
		cfg = (mlan_ds_misc_cfg *)pioctl_buf->pbuf;
		cfg->param.dyn_bw = wlan_le16_to_cpu(dyn_bw->dyn_bw);
		PRINTM(MCMND, "Get dynamic bandwidth 0x%x\n",
		       cfg->param.dyn_bw);
	}
	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function prepares command of CHAN_TRPC_CONFIG
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param cmd          A pointer to HostCmd_DS_COMMAND structure
 *  @param cmd_action   the action: GET or SET
 *  @param pdata_buf    A pointer to data buffer
 *  @return             MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
mlan_status
wlan_cmd_get_chan_trpc_config(pmlan_private pmpriv,
			      HostCmd_DS_COMMAND *cmd,
			      t_u16 cmd_action, t_void *pdata_buf)
{
	HostCmd_DS_CHANNEL_TRPC_CONFIG *trpc_cfg = &cmd->params.ch_trpc_config;
	mlan_ds_misc_chan_trpc_cfg *cfg =
		(mlan_ds_misc_chan_trpc_cfg *) pdata_buf;

	ENTER();

	cmd->command = wlan_cpu_to_le16(HostCmd_CHANNEL_TRPC_CONFIG);
	trpc_cfg->action = wlan_cpu_to_le16(cmd_action);
	cmd->size = wlan_cpu_to_le16(sizeof(HostCmd_DS_CHANNEL_TRPC_CONFIG) +
				     S_DS_GEN);
	trpc_cfg->sub_band = wlan_cpu_to_le16(cfg->sub_band);
	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function prepares command of LOW_POWER_MODE_CFG
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param cmd          A pointer to HostCmd_DS_COMMAND structure
 *  @param cmd_action   the action: GET or SET
 *  @param pdata_buf    A pointer to data buffer
 *  @return             MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
mlan_status
wlan_cmd_set_get_low_power_mode_cfg(pmlan_private pmpriv,
				    HostCmd_DS_COMMAND *cmd,
				    t_u16 cmd_action, t_void *pdata_buf)
{
	HostCmd_DS_LOW_POWER_MODE_CFG *lpm_cfg = &cmd->params.lpm_cfg;
	t_u16 lpm = *(t_u16 *)pdata_buf;

	ENTER();

	cmd->command = wlan_cpu_to_le16(HostCmd_CMD_LOW_POWER_MODE_CFG);
	cmd->size = wlan_cpu_to_le16(sizeof(HostCmd_DS_LOW_POWER_MODE_CFG) +
				     S_DS_GEN);
	lpm_cfg->action = wlan_cpu_to_le16(cmd_action);

	if (cmd_action == HostCmd_ACT_GEN_SET)
		lpm_cfg->lpm = wlan_cpu_to_le16(lpm);

	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function handles the command response of low power mode
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param resp         A pointer to HostCmd_DS_COMMAND
 *  @param pioctl_buf   A pointer to command buffer
 *
 *  @return             MLAN_STATUS_SUCCESS

 */
mlan_status
wlan_ret_set_get_low_power_mode_cfg(pmlan_private pmpriv,
				    HostCmd_DS_COMMAND *resp,
				    mlan_ioctl_req *pioctl_buf)
{
	mlan_ds_power_cfg *cfg = MNULL;
	HostCmd_DS_LOW_POWER_MODE_CFG *lpm_cfg = &resp->params.lpm_cfg;

	ENTER();

	if (pioctl_buf &&
	    (wlan_le16_to_cpu(lpm_cfg->action) == HostCmd_ACT_GEN_GET)) {
		cfg = (mlan_ds_power_cfg *)pioctl_buf->pbuf;
		cfg->param.lpm = wlan_le16_to_cpu(lpm_cfg->lpm);
		PRINTM(MCMND, "Get low power mode %d\n", cfg->param.lpm);
	}

	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function handles the command response of
 *  packet aggregation
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param resp         A pointer to HostCmd_DS_COMMAND
 *  @param pioctl_buf   A pointer to command buffer
 *
 *  @return             MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_ret_get_chan_trpc_config(pmlan_private pmpriv,
			      HostCmd_DS_COMMAND *resp,
			      mlan_ioctl_req *pioctl_buf)
{
	mlan_ds_misc_cfg *misc = MNULL;
	HostCmd_DS_CHANNEL_TRPC_CONFIG *trpc_cfg = &resp->params.ch_trpc_config;
	mlan_ds_misc_chan_trpc_cfg *cfg = MNULL;
	mlan_adapter *pmadapter = pmpriv->adapter;

	ENTER();
	if (pioctl_buf) {
		misc = (mlan_ds_misc_cfg *)pioctl_buf->pbuf;
		cfg = (mlan_ds_misc_chan_trpc_cfg *) & (misc->param.trpc_cfg);
		cfg->sub_band = wlan_le16_to_cpu(trpc_cfg->sub_band);
		cfg->length = wlan_le16_to_cpu(resp->size);
		memcpy_ext(pmadapter, cfg->trpc_buf, (t_u8 *)resp, cfg->length,
			   sizeof(cfg->trpc_buf));
	}
	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function prepares command of RANGE_EXT
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param cmd          A pointer to HostCmd_DS_COMMAND structure
 *  @param cmd_action   the action: GET or SET
 *  @param pdata_buf    A pointer to data buffer
 *  @return             MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
mlan_status
wlan_cmd_range_ext(pmlan_private pmpriv,
		   HostCmd_DS_COMMAND *cmd, t_u16 cmd_action, t_void *pdata_buf)
{
	HostCmd_DS_RANGE_EXT *range_ext = &cmd->params.range_ext;
	t_u8 mode = *(t_u8 *)pdata_buf;

	ENTER();

	cmd->command = wlan_cpu_to_le16(HostCmd_CMD_RANGE_EXT);
	cmd->size = wlan_cpu_to_le16(sizeof(HostCmd_DS_RANGE_EXT) + S_DS_GEN);
	range_ext->action = wlan_cpu_to_le16(cmd_action);

	if (cmd_action == HostCmd_ACT_GEN_SET)
		range_ext->mode = mode;

	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function handles the command response of RANGE_EXT
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param resp         A pointer to HostCmd_DS_COMMAND
 *  @param pioctl_buf   A pointer to command buffer
 *
 *  @return             MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_ret_range_ext(pmlan_private pmpriv,
		   HostCmd_DS_COMMAND *resp, mlan_ioctl_req *pioctl_buf)
{
	mlan_ds_misc_cfg *misc_cfg = MNULL;
	HostCmd_DS_RANGE_EXT *range_ext = &resp->params.range_ext;

	ENTER();

	if (pioctl_buf &&
	    (wlan_le16_to_cpu(range_ext->action) == HostCmd_ACT_GEN_GET)) {
		misc_cfg = (mlan_ds_misc_cfg *)pioctl_buf->pbuf;
		misc_cfg->param.range_ext_mode = range_ext->mode;
		PRINTM(MCMND, "Get range ext mode %d\n",
		       misc_cfg->param.range_ext_mode);
	}

	LEAVE();
	return MLAN_STATUS_SUCCESS;
}
