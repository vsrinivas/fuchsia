/** @file mlan_wmm.h
 *
 *  @brief This file contains related macros, enum, and struct
 *  of wmm functionalities
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
    10/24/2008: initial version
****************************************************/

#ifndef _MLAN_WMM_H_
#define _MLAN_WMM_H_

/**
 *  @brief This function gets the TID
 *
 *  @param pmadapter    A pointer to mlan_adapter structure
 *  @param ptr          A pointer to RA list table
 *
 *  @return             TID
 */
static INLINE t_u32
wlan_get_tid(pmlan_adapter pmadapter, praListTbl ptr)
{
	pmlan_buffer mbuf;

	ENTER();
	mbuf = (pmlan_buffer)util_peek_list(pmadapter->pmoal_handle,
					    &ptr->buf_head, MNULL, MNULL);
	LEAVE();

	if (!mbuf) {
		return 0;	// The default TID,BE
	} else
		return mbuf->priority;
}

/**
 *  @brief This function gets the length of a list
 *
 *  @param head         A pointer to mlan_list_head
 *
 *  @return             Length of list
 */
static INLINE t_u32
wlan_wmm_list_len(pmlan_list_head head)
{
	pmlan_linked_list pos;
	t_u32 count = 0;

	ENTER();

	pos = head->pnext;

	while (pos != (pmlan_linked_list)head) {
		++count;
		pos = pos->pnext;
	}

	LEAVE();
	return count;
}

/**
 *  @brief This function requests a ralist lock
 *
 *  @param priv         A pointer to mlan_private structure
 *
 *  @return             N/A
 */
static INLINE t_void
wlan_request_ralist_lock(pmlan_private priv)
{
	mlan_adapter *pmadapter = priv->adapter;
	mlan_callbacks *pcb = (mlan_callbacks *)&pmadapter->callbacks;

	ENTER();

	/* Call MOAL spin lock callback function */
	pcb->moal_spin_lock(pmadapter->pmoal_handle,
			    priv->wmm.ra_list_spinlock);

	LEAVE();
	return;
}

/**
 *  @brief This function releases a lock on ralist
 *
 *  @param priv         A pointer to mlan_private structure
 *
 *  @return             N/A
 */
static INLINE t_void
wlan_release_ralist_lock(pmlan_private priv)
{
	mlan_adapter *pmadapter = priv->adapter;
	mlan_callbacks *pcb = (mlan_callbacks *)&pmadapter->callbacks;

	ENTER();

	/* Call MOAL spin unlock callback function */
	pcb->moal_spin_unlock(pmadapter->pmoal_handle,
			      priv->wmm.ra_list_spinlock);

	LEAVE();
	return;
}

/** Add buffer to WMM Tx queue */
void wlan_wmm_add_buf_txqueue(pmlan_adapter pmadapter, pmlan_buffer pmbuf);
/** Add to RA list */
void wlan_ralist_add(mlan_private *priv, t_u8 *ra);
/** Update the RA list */
int wlan_ralist_update(mlan_private *priv, t_u8 *old_ra, t_u8 *new_ra);

/** WMM status change command handler */
mlan_status wlan_cmd_wmm_status_change(pmlan_private priv);
/** Check if WMM lists are empty */
int wlan_wmm_lists_empty(pmlan_adapter pmadapter);
/** Process WMM transmission */
t_void wlan_wmm_process_tx(pmlan_adapter pmadapter);
/** Test to see if the ralist ptr is valid */
int wlan_is_ralist_valid(mlan_private *priv, raListTbl *ra_list, int tid);

raListTbl *wlan_wmm_get_ralist_node(pmlan_private priv, t_u8 tid,
				    t_u8 *ra_addr);
t_u8 wlan_get_random_ba_threshold(pmlan_adapter pmadapter);

/** Compute driver packet delay */
t_u8 wlan_wmm_compute_driver_packet_delay(pmlan_private priv,
					  const pmlan_buffer pmbuf);
/** Initialize WMM */
t_void wlan_wmm_init(pmlan_adapter pmadapter);
/** Initialize WMM paramter */
t_void wlan_init_wmm_param(pmlan_adapter pmadapter);
/** Setup WMM queues */
extern void wlan_wmm_setup_queues(pmlan_private priv);
/* Setup default queues */
void wlan_wmm_default_queue_priorities(pmlan_private priv);
/* process wmm_param_config command */
mlan_status wlan_cmd_wmm_param_config(pmlan_private pmpriv,
				      HostCmd_DS_COMMAND *cmd,
				      t_u8 cmd_action, t_void *pdata_buf);

/* process wmm_param_config command response */
mlan_status wlan_ret_wmm_param_config(pmlan_private pmpriv,
				      const HostCmd_DS_COMMAND *resp,
				      mlan_ioctl_req *pioctl_buf);

#ifdef STA_SUPPORT
/** Process WMM association request */
extern t_u32 wlan_wmm_process_association_req(pmlan_private priv,
					      t_u8 **ppAssocBuf,
					      IEEEtypes_WmmParameter_t *pWmmIE,
					      IEEEtypes_HTCap_t *pHTCap);
#endif /* STA_SUPPORT */

/** setup wmm queue priorities */
void wlan_wmm_setup_queue_priorities(pmlan_private priv,
				     IEEEtypes_WmmParameter_t *wmm_ie);

/* Get tid_down from tid */
int wlan_get_wmm_tid_down(mlan_private *priv, int tid);
/** Downgrade WMM priority queue */
void wlan_wmm_setup_ac_downgrade(pmlan_private priv);
/** select WMM queue */
t_u8 wlan_wmm_select_queue(mlan_private *pmpriv, t_u8 tid);
t_void wlan_wmm_delete_peer_ralist(pmlan_private priv, t_u8 *mac);

#ifdef STA_SUPPORT
/*
 *  Functions used in the cmd handling routine
 */
/** WMM ADDTS request command handler */
extern mlan_status wlan_cmd_wmm_addts_req(pmlan_private pmpriv,
					  HostCmd_DS_COMMAND *cmd,
					  t_void *pdata_buf);
/** WMM DELTS request command handler */
extern mlan_status wlan_cmd_wmm_delts_req(pmlan_private pmpriv,
					  HostCmd_DS_COMMAND *cmd,
					  t_void *pdata_buf);
/** WMM QUEUE_STATS command handler */
extern mlan_status wlan_cmd_wmm_queue_stats(pmlan_private pmpriv,
					    HostCmd_DS_COMMAND *cmd,
					    t_void *pdata_buf);
/** WMM TS_STATUS command handler */
extern mlan_status wlan_cmd_wmm_ts_status(pmlan_private pmpriv,
					  HostCmd_DS_COMMAND *cmd,
					  t_void *pdata_buf);

/*
 *  Functions used in the cmdresp handling routine
 */
/** WMM get status command response handler */
extern mlan_status wlan_ret_wmm_get_status(pmlan_private priv, t_u8 *ptlv,
					   int resp_len);
/** WMM ADDTS request command response handler */
extern mlan_status wlan_ret_wmm_addts_req(pmlan_private pmpriv,
					  const HostCmd_DS_COMMAND *resp,
					  mlan_ioctl_req *pioctl_buf);
/** WMM DELTS request command response handler */
extern mlan_status wlan_ret_wmm_delts_req(pmlan_private pmpriv,
					  const HostCmd_DS_COMMAND *resp,
					  mlan_ioctl_req *pioctl_buf);
/** WMM QUEUE_STATS command response handler */
extern mlan_status wlan_ret_wmm_queue_stats(pmlan_private pmpriv,
					    const HostCmd_DS_COMMAND *resp,
					    mlan_ioctl_req *pioctl_buf);
/** WMM TS_STATUS command response handler */
extern mlan_status wlan_ret_wmm_ts_status(pmlan_private pmpriv,
					  HostCmd_DS_COMMAND *resp,
					  mlan_ioctl_req *pioctl_buf);

extern t_u8 tos_to_tid_inv[];
extern t_u8 ac_to_tid[4][2];
#endif /* STA_SUPPORT */

/** WMM QUEUE_CONFIG command handler */
extern mlan_status wlan_cmd_wmm_queue_config(pmlan_private pmpriv,
					     HostCmd_DS_COMMAND *cmd,
					     t_void *pdata_buf);

/** WMM QUEUE_CONFIG command response handler */
extern mlan_status wlan_ret_wmm_queue_config(pmlan_private pmpriv,
					     const HostCmd_DS_COMMAND *resp,
					     mlan_ioctl_req *pioctl_buf);

mlan_status wlan_wmm_cfg_ioctl(pmlan_adapter pmadapter,
			       pmlan_ioctl_req pioctl_req);
#endif /* !_MLAN_WMM_H_ */
