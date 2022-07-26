/** @file mlan_11ac.c
 *
 *  @brief This file contains the functions for station ioctl.
 *
 *
 *  Copyright 2011-2021 NXP
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

#include "mlan.h"
#include "mlan_join.h"
#include "mlan_util.h"
#include "mlan_fw.h"
#include "mlan_main.h"
#include "mlan_wmm.h"
#include "mlan_11n.h"
#include "mlan_11ac.h"

/********************************************************
			Local Variables
********************************************************/

/********************************************************
			Global Variables
********************************************************/

/********************************************************
			Local Functions
********************************************************/
t_u16 wlan_convert_mcsmap_to_maxrate(mlan_private *priv, t_u16 bands,
				     t_u16 mcs_map);
/**
 *  @brief determine the center frquency center index for bandwidth
 *         of 80 MHz and 160 MHz
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param band         band
 *  @param pri_chan     primary channel
 *  @param chan_bw      channel bandwidth
 *
 *  @return             channel center frequency center, if found; O, otherwise
 */

t_u8
wlan_get_center_freq_idx(mlan_private *pmpriv, t_u16 band,
			 t_u32 pri_chan, t_u8 chan_bw)
{
	t_u8 center_freq_idx = 0;

	if (band & BAND_AAC) {
		switch (pri_chan) {
		case 36:
		case 40:
		case 44:
		case 48:
			if (chan_bw == CHANNEL_BW_80MHZ) {
				center_freq_idx = 42;
				break;
			}
			/* fall through */
		case 52:
		case 56:
		case 60:
		case 64:
			if (chan_bw == CHANNEL_BW_80MHZ) {
				center_freq_idx = 58;
				break;
			} else if (chan_bw == CHANNEL_BW_160MHZ) {
				center_freq_idx = 50;
				break;
			}
			/* fall through */
		case 100:
		case 104:
		case 108:
		case 112:
			if (chan_bw == CHANNEL_BW_80MHZ) {
				center_freq_idx = 106;
				break;
			}
			/* fall through */
		case 116:
		case 120:
		case 124:
		case 128:
			if (chan_bw == CHANNEL_BW_80MHZ) {
				center_freq_idx = 122;
				break;
			} else if (chan_bw == CHANNEL_BW_160MHZ) {
				center_freq_idx = 114;
				break;
			}
			/* fall through */
		case 132:
		case 136:
		case 140:
		case 144:
			if (chan_bw == CHANNEL_BW_80MHZ) {
				center_freq_idx = 138;
				break;
			}
			/* fall through */
		case 149:
		case 153:
		case 157:
		case 161:
			if (chan_bw == CHANNEL_BW_80MHZ) {
				center_freq_idx = 155;
				break;
			}
			/* fall through */
		case 165:
		case 169:
		case 173:
		case 177:
			if (chan_bw == CHANNEL_BW_80MHZ) {
				center_freq_idx = 171;
				break;
			}
			/* fall through */
		case 184:
		case 188:
		case 192:
		case 196:
			if (chan_bw == CHANNEL_BW_80MHZ) {
				center_freq_idx = 190;
				break;
			}
			/* fall through */
		default:	/* error. go to the default */
			center_freq_idx = 42;
		}
	}
	return center_freq_idx;
}

/**
 *  @brief This function gets the bitmap of nss which supports VHT mcs
 *
 *  @param mcs_map_set  VHT mcs map
 *
 *  @return             The bitmap of supported nss
 */
static t_u8
wlan_get_nss_vht_mcs(t_u16 mcs_map_set)
{
	t_u8 nss, nss_map = 0;
	for (nss = 1; nss <= 8; nss++) {
		if (GET_VHTNSSMCS(mcs_map_set, nss) != NO_NSS_SUPPORT)
			nss_map |= 1 << (nss - 1);
	}
	PRINTM(MCMND, "Supported nss bit map:0x%02x\n", nss_map);
	return nss_map;
}

/**
 *  @brief This function gets the bitmap of nss which supports VHT mcs
 *
 *  @param mcs_map_set  VHT mcs map
 *
 *  @return             The bitmap of supported nss
 */
static t_u8
wlan_get_nss_num_vht_mcs(t_u16 mcs_map_set)
{
	t_u8 nss, nss_num = 0;
	for (nss = 1; nss <= 8; nss++) {
		if (GET_VHTNSSMCS(mcs_map_set, nss) != NO_NSS_SUPPORT)
			nss_num++;
	}
	PRINTM(MCMND, "Supported nss:%d\n", nss_num);
	return nss_num;
}

/**
 *  @brief This function fills the cap info
 *
 *  @param priv         A pointer to mlan_private structure
 *  @param pht_cap      A pointer to MrvlIETypes_HTCap_t structure
 *  @param bands        Band configuration
 *
 *  @return             N/A
 */
static void
wlan_fill_cap_info(mlan_private *priv, VHT_capa_t *vht_cap, t_u8 bands)
{
	t_u32 usr_dot_11ac_dev_cap;
	ENTER();

	if (bands & BAND_A)
		usr_dot_11ac_dev_cap = priv->usr_dot_11ac_dev_cap_a;
	else
		usr_dot_11ac_dev_cap = priv->usr_dot_11ac_dev_cap_bg;

	vht_cap->vht_cap_info = usr_dot_11ac_dev_cap;

	RESET_VHTCAP_MAXMPDULEN(vht_cap->vht_cap_info);
	LEAVE();
}

/**
 *  @brief Set/get 11ac configuration
 *
 *  @param pmadapter    A pointer to mlan_adapter structure
 *  @param pioctl_req   A pointer to ioctl request buffer
 *
 *  @return     MLAN_STATUS_PENDING --success, otherwise fail
 */
static mlan_status
wlan_11ac_ioctl_vhtcfg(pmlan_adapter pmadapter, pmlan_ioctl_req pioctl_req)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_ds_11ac_cfg *cfg = MNULL;
	t_u16 cmd_action = 0;
	t_u32 usr_vht_cap_info = 0;
	t_u32 cfg_value = 0;
	t_u32 hw_value = 0;
	t_u8 nss = 0;
#if defined(PCIE9098) || defined(SD9098) || defined(USB9098) || defined(PCIE9097) || defined(USB9097) || defined(SD9097)
	t_u16 rx_nss = 0;
	t_u16 tx_nss = 0;
#endif

	ENTER();

#define VHT_CAP_INFO_BIT_FIELDS                                                \
	(MBIT(4) | MBIT(5) | MBIT(6) | MBIT(7) | MBIT(11) | MBIT(12) |         \
	 MBIT(19) | MBIT(20) | MBIT(21) | MBIT(22) | MBIT(28) | MBIT(29))

	cfg = (mlan_ds_11ac_cfg *)pioctl_req->pbuf;

	if (pioctl_req->action == MLAN_ACT_SET) {
		/** SET operation */
		/** validate the user input and correct it if necessary */
		if (pmpriv->bss_role == MLAN_BSS_ROLE_STA) {
			if (cfg->param.vht_cfg.txrx == 3) {
				PRINTM(MERROR,
				       "Configuration of VHT capabilities for TX/RX 3 is not supported in STA mode!\n");
				return MLAN_STATUS_FAILURE;
			}
		}
		if (pmpriv->bss_role == MLAN_BSS_ROLE_UAP) {
			if (cfg->param.vht_cfg.txrx != 3) {
				PRINTM(MERROR,
				       "Configuration of VHT capabilities for TX/RX %d is not supported in UAP mode!\n",
				       cfg->param.vht_cfg.txrx);
				return MLAN_STATUS_FAILURE;
			}
		}
		/** set bit fileds */
		usr_vht_cap_info = VHT_CAP_INFO_BIT_FIELDS &
			cfg->param.vht_cfg.vht_cap_info &
			pmadapter->hw_dot_11ac_dev_cap;
		/** set MAX MPDU LEN field (bit 0 - bit 1) */
		cfg_value =
			GET_VHTCAP_MAXMPDULEN(cfg->param.vht_cfg.vht_cap_info);
		hw_value =
			GET_VHTCAP_MAXMPDULEN(pmadapter->hw_dot_11ac_dev_cap);
		SET_VHTCAP_MAXMPDULEN(usr_vht_cap_info,
				      MIN(cfg_value, hw_value));
		/** set CHAN Width Set field (bit 2 - bit 3) */
		cfg_value = GET_VHTCAP_CHWDSET(cfg->param.vht_cfg.vht_cap_info);
		hw_value = GET_VHTCAP_CHWDSET(pmadapter->hw_dot_11ac_dev_cap);
		SET_VHTCAP_CHWDSET(usr_vht_cap_info, MIN(cfg_value, hw_value));
		/** set Rx STBC field (bit 8 - bit 10) */
		cfg_value = GET_VHTCAP_RXSTBC(cfg->param.vht_cfg.vht_cap_info);
		hw_value = GET_VHTCAP_RXSTBC(pmadapter->hw_dot_11ac_dev_cap);
		SET_VHTCAP_RXSTBC(usr_vht_cap_info, MIN(cfg_value, hw_value));
		/** set Steering Number of BFer Ant (bit 13 - bit 15) */
		cfg_value =
			GET_VHTCAP_SNBFERANT(cfg->param.vht_cfg.vht_cap_info);
		hw_value = GET_VHTCAP_SNBFERANT(pmadapter->hw_dot_11ac_dev_cap);
		SET_VHTCAP_SNBFERANT(usr_vht_cap_info,
				     MIN(cfg_value, hw_value));
		/** set Number of Sounding Dimension (bit 16 - bit 18) */
		cfg_value =
			GET_VHTCAP_NUMSNDDM(cfg->param.vht_cfg.vht_cap_info);
		hw_value = GET_VHTCAP_NUMSNDDM(pmadapter->hw_dot_11ac_dev_cap);
		SET_VHTCAP_NUMSNDDM(usr_vht_cap_info, MIN(cfg_value, hw_value));
		/** set Number of Max AMPDU Length Exponent (bit 23 - bit 25) */
		cfg_value =
			GET_VHTCAP_MAXAMPDULENEXP(cfg->param.vht_cfg.
						  vht_cap_info);
		hw_value =
			GET_VHTCAP_MAXAMPDULENEXP(pmadapter->
						  hw_dot_11ac_dev_cap);
		SET_VHTCAP_MAXAMPDULENEXP(usr_vht_cap_info,
					  MIN(cfg_value, hw_value));
		/** set VHT Link Adaptation Capable (bit 26 - bit 27) */
		cfg_value =
			GET_VHTCAP_LINKADPCAP(cfg->param.vht_cfg.vht_cap_info);
		hw_value =
			GET_VHTCAP_LINKADPCAP(pmadapter->hw_dot_11ac_dev_cap);
		SET_VHTCAP_LINKADPCAP(usr_vht_cap_info,
				      MIN(cfg_value, hw_value));
		/** update the user setting if it is beyond the hw capabiliteis
		 */
		cfg->param.vht_cfg.vht_cap_info = usr_vht_cap_info;
		PRINTM(MINFO, "Set: vht cap info  0x%x\n", usr_vht_cap_info);

		/** update the RX MCS map */
		if (cfg->param.vht_cfg.txrx & MLAN_RADIO_RX) {
#if defined(PCIE9098) || defined(SD9098) || defined(USB9098) || defined(PCIE9097) || defined(SD9097) || defined(USB9097)
			if (IS_CARD9098(pmadapter->card_type) ||
			    IS_CARD9097(pmadapter->card_type)) {
				if (cfg->param.vht_cfg.band == BAND_SELECT_A) {
					rx_nss = GET_RXMCSSUPP(pmadapter->
							       user_htstream >>
							       8);
					tx_nss = GET_TXMCSSUPP(pmadapter->
							       user_htstream >>
							       8) & 0x0f;
				} else {
					rx_nss = GET_RXMCSSUPP(pmadapter->
							       user_htstream);
					tx_nss = GET_TXMCSSUPP(pmadapter->
							       user_htstream) &
						0x0f;
				}
			}
#endif
			/* use the previous user value */
			if (cfg->param.vht_cfg.vht_rx_mcs == 0xffffffff)
				cfg->param.vht_cfg.vht_rx_mcs =
					GET_VHTMCS(pmpriv->
						   usr_dot_11ac_mcs_support);
			for (nss = 1; nss <= 8; nss++) {
				cfg_value =
					GET_VHTNSSMCS(cfg->param.vht_cfg.
						      vht_rx_mcs, nss);
				hw_value =
					GET_DEVNSSRXMCS(pmadapter->
							hw_dot_11ac_mcs_support,
							nss);
#if defined(PCIE9098) || defined(SD9098) || defined(USB9098) || defined(PCIE9097) || defined(SD9097) || defined(USB9097)
				if ((rx_nss != 0) && (nss > rx_nss))
					cfg_value = NO_NSS_SUPPORT;
#endif
				if ((hw_value == NO_NSS_SUPPORT) ||
				    (cfg_value == NO_NSS_SUPPORT))
					SET_VHTNSSMCS(cfg->param.vht_cfg.
						      vht_rx_mcs, nss,
						      NO_NSS_SUPPORT);
				else
					SET_VHTNSSMCS(cfg->param.vht_cfg.
						      vht_rx_mcs, nss,
						      MIN(cfg_value, hw_value));
			}
			PRINTM(MINFO, "Set: vht rx mcs set 0x%08x\n",
			       cfg->param.vht_cfg.vht_rx_mcs);
			/* use the previous user value */
			if (cfg->param.vht_cfg.vht_tx_mcs == 0xffffffff)
				cfg->param.vht_cfg.vht_tx_mcs =
					GET_VHTMCS(pmpriv->
						   usr_dot_11ac_mcs_support >>
						   16);
			for (nss = 1; nss <= 8; nss++) {
				cfg_value =
					GET_VHTNSSMCS(cfg->param.vht_cfg.
						      vht_tx_mcs, nss);
				hw_value =
					GET_DEVNSSTXMCS(pmadapter->
							hw_dot_11ac_mcs_support,
							nss);
#if defined(PCIE9098) || defined(SD9098) || defined(USB9098) || defined(PCIE9097) || defined(SD9097) || defined(USB9097)
				if ((tx_nss != 0) && (nss > tx_nss))
					cfg_value = NO_NSS_SUPPORT;
#endif
				if ((hw_value == NO_NSS_SUPPORT) ||
				    (cfg_value == NO_NSS_SUPPORT))
					SET_VHTNSSMCS(cfg->param.vht_cfg.
						      vht_tx_mcs, nss,
						      NO_NSS_SUPPORT);
				else
					SET_VHTNSSMCS(cfg->param.vht_cfg.
						      vht_tx_mcs, nss,
						      MIN(cfg_value, hw_value));
			}
			PRINTM(MINFO, "Set: vht tx mcs set 0x%08x\n",
			       cfg->param.vht_cfg.vht_tx_mcs);
			if (!cfg->param.vht_cfg.skip_usr_11ac_mcs_cfg) {
				RESET_DEVRXMCSMAP(pmpriv->
						  usr_dot_11ac_mcs_support);
				pmpriv->usr_dot_11ac_mcs_support |=
					GET_VHTMCS(cfg->param.vht_cfg.
						   vht_rx_mcs);
				RESET_DEVTXMCSMAP(pmpriv->
						  usr_dot_11ac_mcs_support);
				pmpriv->usr_dot_11ac_mcs_support |=
					(GET_VHTMCS
					 (cfg->param.vht_cfg.vht_tx_mcs)
					 << 16);
				PRINTM(MINFO, "Set: vht mcs set 0x%08x\n",
				       pmpriv->usr_dot_11ac_mcs_support);
			} else {
				PRINTM(MINFO,
				       "Skipped user 11ac mcs configuration\n");
				cfg->param.vht_cfg.skip_usr_11ac_mcs_cfg =
					MFALSE;
			}
		}
	}

	if (pmpriv->bss_role == MLAN_BSS_ROLE_STA) {
		if (cfg->param.vht_cfg.txrx & MLAN_RADIO_RX) {
			/* maximum VHT configuration used in association */
			if (pioctl_req->action == MLAN_ACT_SET) {
				if (cfg->param.vht_cfg.band == BAND_SELECT_BG)
					pmpriv->usr_dot_11ac_dev_cap_bg =
						usr_vht_cap_info;
				else if (cfg->param.vht_cfg.band ==
					 BAND_SELECT_A)
					pmpriv->usr_dot_11ac_dev_cap_a =
						usr_vht_cap_info;
				else {
					pmpriv->usr_dot_11ac_dev_cap_bg =
						usr_vht_cap_info;
					pmpriv->usr_dot_11ac_dev_cap_a =
						usr_vht_cap_info;
				}
				pmpriv->usr_dot_11ac_bw =
					cfg->param.vht_cfg.bwcfg;

			} else {
				/** GET operation */
				if (cfg->param.vht_cfg.band == BAND_SELECT_BG) {
					cfg->param.vht_cfg.vht_cap_info =
						pmpriv->usr_dot_11ac_dev_cap_bg;
					PRINTM(MINFO,
					       "Get: vht cap info for 2.4GHz 0x%x\n",
					       pmpriv->usr_dot_11ac_dev_cap_bg);
				} else if (cfg->param.vht_cfg.band ==
					   BAND_SELECT_A) {
					cfg->param.vht_cfg.vht_cap_info =
						pmpriv->usr_dot_11ac_dev_cap_a;
					PRINTM(MINFO,
					       "Get: vht cap info for 5GHz 0x%x\n",
					       pmpriv->usr_dot_11ac_dev_cap_a);
				} else {
					PRINTM(MINFO,
					       "Get: invalid band selection for vht cap info\n");
					ret = MLAN_STATUS_FAILURE;
				}
				cfg->param.vht_cfg.bwcfg =
					pmpriv->usr_dot_11ac_bw;
				cfg->param.vht_cfg.vht_rx_mcs =
					GET_DEVRXMCSMAP(pmpriv->
							usr_dot_11ac_mcs_support);
				cfg->param.vht_cfg.vht_tx_mcs =
					GET_DEVTXMCSMAP(pmpriv->
							usr_dot_11ac_mcs_support);
				cfg->param.vht_cfg.vht_rx_max_rate =
					wlan_convert_mcsmap_to_maxrate(pmpriv,
								       cfg->
								       param.
								       vht_cfg.
								       band,
								       cfg->
								       param.
								       vht_cfg.
								       vht_rx_mcs);
				cfg->param.vht_cfg.vht_tx_max_rate =
					wlan_convert_mcsmap_to_maxrate(pmpriv,
								       cfg->
								       param.
								       vht_cfg.
								       band,
								       cfg->
								       param.
								       vht_cfg.
								       vht_tx_mcs);
			}
			LEAVE();
			return ret;
		}
	}

	if (pioctl_req->action == MLAN_ACT_SET)
		cmd_action = HostCmd_ACT_GEN_SET;
	else
		cmd_action = HostCmd_ACT_GEN_GET;

	/* Send request to firmware */
	ret = wlan_prepare_cmd(pmpriv, HostCmd_CMD_11AC_CFG, cmd_action, 0,
			       (t_void *)pioctl_req,
			       (t_void *)&cfg->param.vht_cfg);
	if (ret == MLAN_STATUS_SUCCESS)
		ret = MLAN_STATUS_PENDING;

	LEAVE();
	return ret;
}

/**
 *  @brief Get/Set Operating Mode Notification cfg
 *
 *  @param pmadapter    A pointer to mlan_adapter structure
 *  @param pioctl_req   A pointer to ioctl request buffer
 *
 *  @return     MLAN_STATUS_SUCCESS --success, otherwise fail
 */
static mlan_status
wlan_11ac_ioctl_opermodecfg(pmlan_adapter pmadapter, pmlan_ioctl_req pioctl_req)
{
	mlan_ds_11ac_cfg *cfg = MNULL;
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];
	t_u8 hw_bw_160or8080 = 0;
	t_u8 hw_rx_nss = 0;

	ENTER();

	cfg = (mlan_ds_11ac_cfg *)pioctl_req->pbuf;
	if (pioctl_req->action == MLAN_ACT_GET) {
		cfg->param.opermode_cfg.bw = pmpriv->usr_dot_11ac_opermode_bw;
		cfg->param.opermode_cfg.nss = pmpriv->usr_dot_11ac_opermode_nss;
	} else if (pioctl_req->action == MLAN_ACT_SET) {
		hw_bw_160or8080 =
			GET_VHTCAP_CHWDSET(pmadapter->hw_dot_11ac_dev_cap);
		hw_rx_nss =
			wlan_get_nss_num_vht_mcs(GET_DEVRXMCSMAP
						 (pmadapter->
						  hw_dot_11ac_mcs_support));
		if ((((cfg->param.opermode_cfg.bw - 1) > BW_80MHZ) &&
		     !hw_bw_160or8080) ||
		    (cfg->param.opermode_cfg.nss > hw_rx_nss)) {
			PRINTM(MERROR,
			       "bw or nss NOT supported. HW support bw_160or8080=%d rx_nss=%d.\n",
			       hw_bw_160or8080, hw_rx_nss);
			LEAVE();
			return MLAN_STATUS_FAILURE;
		}
		pmpriv->usr_dot_11ac_opermode_bw = cfg->param.opermode_cfg.bw;
		pmpriv->usr_dot_11ac_opermode_nss = cfg->param.opermode_cfg.nss;
	}

	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief Get supported MCS set
 *
 *  @param pmadapter    A pointer to mlan_adapter structure
 *  @param pioctl_req   A pointer to ioctl request buffer
 *
 *  @return     MLAN_STATUS_SUCCESS --success, otherwise fail
 */
static mlan_status
wlan_11ac_ioctl_supported_mcs_set(pmlan_adapter pmadapter,
				  pmlan_ioctl_req pioctl_req)
{
	/*mlan_ds_11ac_cfg *cfg= MNULL; */
	/*int rx_mcs_supp; */
	/*t_u8 mcs_set[NUM_MCS_SUPP]; */

	ENTER();
#if 0
	if (pioctl_req->action == MLAN_ACT_SET) {
		PRINTM(MERROR, "Set operation is not supported\n");
		pioctl_req->status_code = MLAN_ERROR_IOCTL_INVALID;
		LEAVE();
		return MLAN_STATUS_FAILURE;
	}
	rx_mcs_supp = GET_11ACRXMCSSUPP(pmadapter->usr_dot_11ac_mcs_support);
	/* Set MCS */
	memset(pmadapter, (t_u8 *)mcs_set, 0xff, rx_mcs_supp);
	/* Clear all the other values */
	memset(pmadapter, (t_u8 *)&mcs_set[rx_mcs_supp], 0,
	       NUM_MCS_FIELD - rx_mcs_supp);
	/* Set MCS32 with 40MHz support */
	if (ISSUPP_CHANWIDTH80(pmadapter->usr_dot_11ac_dev_cap_bg)
	    || ISSUPP_CHANWIDTH80(pmadapter->usr_dot_11ac_dev_cap_a)
		)
		SETHT_MCS32(mcs_set);

	cfg = (mlan_ds_11ac_cfg *)pioctl_req->pbuf;
	memcpy_ext(pmadapter, cfg->param.supported_mcs_set, mcs_set,
		   NUM_MCS_SUPP, sizeof(cfg->param.supported_mcs_set));

#endif
	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/********************************************************
			Global Functions
********************************************************/

/**
 *  @brief This function prints the 802.11ac device capability
 *
 *  @param pmadapter     A pointer to mlan_adapter structure
 *  @param cap           Capability value
 *
 *  @return        N/A
 */
void
wlan_show_dot11acdevcap(pmlan_adapter pmadapter, t_u32 cap)
{
	ENTER();

	switch (GET_VHTCAP_MAXMPDULEN(cap)) {
	case 0x0:
		PRINTM(MINFO,
		       "GET_HW_SPEC: Maximum MPDU length = 3895 octets\n");
		break;
	case 0x1:
		PRINTM(MINFO,
		       "GET_HW_SPEC: Maximum MPDU length = 7991 octets\n");
		break;
	case 0x2:
		PRINTM(MINFO,
		       "GET_HW_SPEC: Maximum MPDU length = 11454 octets\n");
		break;
	default:
		PRINTM(MINFO, "Unsupport value\n");
		break;
	}

	PRINTM(MINFO, "GET_HW_SPEC: HTC-VHT %s\n",
	       (ISSUPP_11ACVHTHTCVHT(cap) ? "supported" : "not supported"));
	PRINTM(MINFO, "GET_HW_SPEC: VHT TXOP PS %s\n",
	       (ISSUPP_11ACVHTTXOPPS(cap) ? "supported" : "not supported"));
	PRINTM(MINFO, "GET_HW_SPEC: MU RX beamformee %s\n",
	       (ISSUPP_11ACMURXBEAMFORMEE(cap) ? "supported" :
		"not supported"));
	PRINTM(MINFO, "GET_HW_SPEC: MU TX beamformee %s\n",
	       (ISSUPP_11ACMUTXBEAMFORMEE(cap) ? "supported" :
		"not supported"));
	PRINTM(MINFO, "GET_HW_SPEC: SU RX Beamformee %s\n",
	       (ISSUPP_11ACSUBEAMFORMEE(cap) ? "supported" : "not supported"));
	PRINTM(MINFO, "GET_HW_SPEC: SU TX Beamformer %s\n",
	       (ISSUPP_11ACSUBEAMFORMER(cap) ? "supported" : "not supported"));
	PRINTM(MINFO, "GET_HW_SPEC: Rx STBC %s\n",
	       (ISSUPP_11ACRXSTBC(cap) ? "supported" : "not supported"));
	PRINTM(MINFO, "GET_HW_SPEC: Tx STBC %s\n",
	       (ISSUPP_11ACTXSTBC(cap) ? "supported" : "not supported"));
	PRINTM(MINFO, "GET_HW_SPEC: Short GI %s for 160MHz BW\n",
	       (ISSUPP_11ACSGI160(cap) ? "supported" : "not supported"));
	PRINTM(MINFO, "GET_HW_SPEC: Short GI %s for 80MHz BW\n",
	       (ISSUPP_11ACSGI80(cap) ? "supported" : "not supported"));
	PRINTM(MINFO, "GET_HW_SPEC: LDPC coding %s\n",
	       (ISSUPP_11ACLDPC(cap) ? "supported" : "not supported"));
	PRINTM(MINFO, "GET_HW_SPEC: Channel BW 20/40/80/160/80+80 MHz %s\n",
	       (ISSUPP_11ACBW8080(cap) ? "supported" : "not supported"));
	PRINTM(MINFO, "GET_HW_SPEC: Channel BW 20/40/80/160 MHz %s\n",
	       (ISSUPP_11ACBW160(cap) ? "supported" : "not supported"));

	LEAVE();
	return;
}

/**
 *  @brief This function prints the 802.11ac device MCS
 *
 *  @param pmadapter A pointer to mlan_adapter structure
 *  @param support   Support value
 *
 *  @return        N/A
 */
void
wlan_show_dot11acmcssupport(pmlan_adapter pmadapter, t_u32 support)
{
	ENTER();

	PRINTM(MINFO, "GET_HW_SPEC: MCSs for %2dx%2d MIMO\n",
	       GET_DEVRXMCSMAP(support), GET_DEVTXMCSMAP(support));

	LEAVE();
	return;
}

/**
 *  @brief This function converts the 2-bit MCS map to the highest long GI
 *  VHT PPDU data rate
 *
 *  @param priv         A pointer to mlan_private structure
 *  @param bands        Supported bands
 *  @param mcs_map      2-bit MCS map
 *
 *  @return             the max data rate for long GI
 */
t_u16
wlan_convert_mcsmap_to_maxrate(mlan_private *priv, t_u16 bands, t_u16 mcs_map)
{
	t_u8 i;
	t_u8 nss;
	t_u8 max_mcs;
	t_u16 max_rate = 0;
	t_u32 usr_vht_cap_info = 0;
	t_u32 usr_dot_11n_dev_cap;

	/* tables of the MCS map to the highest data rate (in Mbps)
	 * supported for long GI */
	t_u16 max_rate_lgi_20MHZ[8][3] = {
		{0x41, 0x4E, 0x0},	/* NSS = 1 */
		{0x82, 0x9C, 0x0},	/* NSS = 2 */
		{0xC3, 0xEA, 0x104},	/* NSS = 3 */
		{0x104, 0x138, 0x0},	/* NSS = 4 */
		{0x145, 0x186, 0x0},	/* NSS = 5 */
		{0x186, 0x1D4, 0x208},	/* NSS = 6 */
		{0x1C7, 0x222, 0x0},	/* NSS = 7 */
		{0x208, 0x270, 0x0}	/* NSS = 8 */
	};

	t_u16 max_rate_lgi_40MHZ[8][3] = {
		{0x87, 0xA2, 0xB4},	/* NSS = 1 */
		{0x10E, 0x144, 0x168},	/* NSS = 2 */
		{0x195, 0x1E6, 0x21C},	/* NSS = 3 */
		{0x21C, 0x288, 0x2D0},	/* NSS = 4 */
		{0x2A3, 0x32A, 0x384},	/* NSS = 5 */
		{0x32A, 0x3CC, 0x438},	/* NSS = 6 */
		{0x3B1, 0x46E, 0x4EC},	/* NSS = 7 */
		{0x438, 0x510, 0x5A0}	/* NSS = 8 */
	};

	t_u16 max_rate_lgi_80MHZ[8][3] = {
		{0x124, 0x15F, 0x186},	/* NSS = 1 */
		{0x249, 0x2BE, 0x30C},	/* NSS = 2 */
		{0x36D, 0x41D, 0x492},	/* NSS = 3 */
		{0x492, 0x57C, 0x618},	/* NSS = 4 */
		{0x5B6, 0x6DB, 0x79E},	/* NSS = 5 */
		{0x6DB, 0x83A, 0x0},	/* NSS = 6 */
		{0x7FF, 0x999, 0xAAA},	/* NSS = 7 */
		{0x924, 0xAF8, 0xC30}	/* NSS = 8 */
	};
	t_u16 max_rate_lgi_160MHZ[8][3] = {
		{0x249, 0x2BE, 0x30C},	/* NSS = 1 */
		{0x492, 0x57C, 0x618},	/* NSS = 2 */
		{0x6DB, 0x83A, 0x0},	/* NSS = 3 */
		{0x924, 0xAF8, 0xC30},	/* NSS = 4 */
		{0xB6D, 0xDB6, 0xF3C},	/* NSS = 5 */
		{0xDB6, 0x1074, 0x1248},	/* NSS = 6 */
		{0xFFF, 0x1332, 0x1554},	/* NSS = 7 */
		{0x1248, 0x15F0, 0x1860}	/* NSS = 8 */
	};

	if (bands & BAND_AAC) {
		usr_vht_cap_info = priv->usr_dot_11ac_dev_cap_a;
		usr_dot_11n_dev_cap = priv->usr_dot_11n_dev_cap_a;
	} else {
		usr_vht_cap_info = priv->usr_dot_11ac_dev_cap_bg;
		usr_dot_11n_dev_cap = priv->usr_dot_11n_dev_cap_bg;
	}

	/* find the max NSS supported */
	nss = 0;
	for (i = 0; i < 8; i++) {
		max_mcs = (mcs_map >> (2 * i)) & 0x3;
		if (max_mcs < 3)
			nss = i;
	}

	max_mcs = (mcs_map >> (2 * nss)) & 0x3;
	/* if max_mcs is 3, nss must be 0 (SS = 1). Thus, max mcs is MCS 9 */
	if (max_mcs >= 3)
		max_mcs = 2;

	if (GET_VHTCAP_CHWDSET(usr_vht_cap_info)) {
		/* support 160 MHz */
		max_rate = max_rate_lgi_160MHZ[nss][max_mcs];
		if (max_mcs >= 1 && max_rate == 0)
			/* MCS9 is not supported in NSS6 */
			max_rate = max_rate_lgi_160MHZ[nss][max_mcs - 1];

	} else {
		if (priv->usr_dot_11ac_bw == BW_FOLLOW_VHTCAP) {
			max_rate = max_rate_lgi_80MHZ[nss][max_mcs];
			if (max_mcs >= 1 && max_rate == 0)
				/* MCS9 is not supported in NSS3 */
				max_rate = max_rate_lgi_80MHZ[nss][max_mcs - 1];
		} else {
			if (ISSUPP_CHANWIDTH40(usr_dot_11n_dev_cap)) {
				max_rate = max_rate_lgi_40MHZ[nss][max_mcs];
			} else {
				max_rate = max_rate_lgi_20MHZ[nss][max_mcs];
				/* MCS9 is not supported in NSS1/2/4/5/7/8 */
				if (max_mcs >= 1 && max_rate == 0)
					max_rate = max_rate_lgi_20MHZ[nss]
						[max_mcs - 1];
			}
		}
	}
	PRINTM(MCMND, "max_rate=%dM\n", max_rate);
	return max_rate;
}

/**
 *  @brief This function fills the VHT cap tlv out put format is LE, not CPU
 *
 *  @param priv         A pointer to mlan_private structure
 *  @param pvht_cap      A pointer to MrvlIETypes_HTCap_t structure
 *  @param bands        Band configuration
 *  @param flag         TREU--pvht_cap has the setting for resp
 *                      MFALSE -- pvht_cap is clean
 *  @param bw_80p80     TRUE -- enable 80p80
 *  @return             N/A
 */
void
wlan_fill_vht_cap_tlv(mlan_private *priv, MrvlIETypes_VHTCap_t *pvht_cap,
		      t_u16 bands, t_u8 flag, t_u8 bw_80p80)
{
	t_u16 mcs_map_user = 0;
	t_u16 mcs_map_resp = 0;
	t_u16 mcs_map_result = 0;
	t_u16 mcs_user = 0;
	t_u16 mcs_resp = 0;
	t_u16 nss;
#if defined(PCIE9098) || defined(SD9098) || defined(USB9098) || defined(PCIE9097) || defined(SD9097) || defined(USB9097)
	t_u16 rx_nss = 0, tx_nss = 0;
#endif
	ENTER();

	/* Fill VHT cap info */
	wlan_fill_cap_info(priv, &pvht_cap->vht_cap, bands);
	/* clear 80p80 in vht_cap_info */
	if (!bw_80p80)
		pvht_cap->vht_cap.vht_cap_info &= ~(MBIT(2) | MBIT(3));
	pvht_cap->vht_cap.vht_cap_info =
		wlan_cpu_to_le32(pvht_cap->vht_cap.vht_cap_info);

	/* Fill VHT MCS Set */
	/* rx MCS Set, find the minimum of the user rx mcs and ap rx mcs */
	mcs_map_resp = mcs_map_user =
		GET_DEVRXMCSMAP(priv->usr_dot_11ac_mcs_support);
	if (flag)
		mcs_map_resp =
			wlan_le16_to_cpu(pvht_cap->vht_cap.mcs_sets.rx_mcs_map);
#if defined(PCIE9098) || defined(SD9098) || defined(USB9098) || defined(PCIE9097) || defined(SD9097) || defined(USB9097)
	if (IS_CARD9098(priv->adapter->card_type) ||
	    IS_CARD9097(priv->adapter->card_type)) {
		if (bands & BAND_A) {
			rx_nss = GET_RXMCSSUPP(priv->adapter->user_htstream >>
					       8);
			tx_nss = GET_TXMCSSUPP(priv->adapter->user_htstream >>
					       8) & 0x0f;
		} else {
			rx_nss = GET_RXMCSSUPP(priv->adapter->user_htstream);
			tx_nss = GET_TXMCSSUPP(priv->adapter->user_htstream) &
				0x0f;
		}
		/** force 1x1 when enable 80P80 */
		if (bw_80p80)
			rx_nss = tx_nss = 1;
	}
#endif
	mcs_map_result = 0;
	for (nss = 1; nss <= 8; nss++) {
		mcs_user = GET_VHTNSSMCS(mcs_map_user, nss);
		mcs_resp = GET_VHTNSSMCS(mcs_map_resp, nss);
#if defined(PCIE9098) || defined(SD9098) || defined(USB9098) || defined(PCIE9097) || defined(SD9097) || defined(USB9097)
		if ((rx_nss != 0) && (nss > rx_nss))
			mcs_user = NO_NSS_SUPPORT;
#endif
		if ((mcs_user == NO_NSS_SUPPORT) ||
		    (mcs_resp == NO_NSS_SUPPORT))
			SET_VHTNSSMCS(mcs_map_result, nss, NO_NSS_SUPPORT);
		else
			SET_VHTNSSMCS(mcs_map_result, nss,
				      MIN(mcs_user, mcs_resp));
	}
	/* rx MCS map */
	pvht_cap->vht_cap.mcs_sets.rx_mcs_map =
		wlan_cpu_to_le16(mcs_map_result);

	/* rx highest rate */
	pvht_cap->vht_cap.mcs_sets.rx_max_rate =
		wlan_convert_mcsmap_to_maxrate(priv, bands, mcs_map_result);
	pvht_cap->vht_cap.mcs_sets.rx_max_rate =
		wlan_cpu_to_le16(pvht_cap->vht_cap.mcs_sets.rx_max_rate);

	/* tx MCS Set find the minimum of the user tx mcs and ap tx mcs */
	mcs_map_resp = mcs_map_user =
		GET_DEVTXMCSMAP(priv->usr_dot_11ac_mcs_support);
	if (flag)
		mcs_map_resp =
			wlan_le16_to_cpu(pvht_cap->vht_cap.mcs_sets.tx_mcs_map);
	mcs_map_result = 0;
	for (nss = 1; nss <= 8; nss++) {
		mcs_user = GET_VHTNSSMCS(mcs_map_user, nss);
		mcs_resp = GET_VHTNSSMCS(mcs_map_resp, nss);
#if defined(PCIE9098) || defined(SD9098) || defined(USB9098) || defined(PCIE9097) || defined(SD9097) || defined(USB9097)
		if ((tx_nss != 0) && (nss > tx_nss))
			mcs_user = NO_NSS_SUPPORT;
#endif
		if ((mcs_user == NO_NSS_SUPPORT) ||
		    (mcs_resp == NO_NSS_SUPPORT))
			SET_VHTNSSMCS(mcs_map_result, nss, NO_NSS_SUPPORT);
		else
			SET_VHTNSSMCS(mcs_map_result, nss,
				      MIN(mcs_user, mcs_resp));
	}

	/* tx MCS map */
	pvht_cap->vht_cap.mcs_sets.tx_mcs_map =
		wlan_cpu_to_le16(mcs_map_result);
	/* tx highest rate */
	pvht_cap->vht_cap.mcs_sets.tx_max_rate =
		wlan_convert_mcsmap_to_maxrate(priv, bands, mcs_map_result);
	pvht_cap->vht_cap.mcs_sets.tx_max_rate =
		wlan_cpu_to_le16(pvht_cap->vht_cap.mcs_sets.tx_max_rate);

	LEAVE();
	return;
}

/**
 *  @brief This function fills the VHT cap tlv out put format is CPU
 *
 *  @param priv         A pointer to mlan_private structure
 *  @param pvht_cap      A pointer to MrvlIETypes_HTCap_t structure
 *  @param bands        Band configuration
 *
 *  @return             N/A
 */
void
wlan_fill_vht_cap_ie(mlan_private *priv, IEEEtypes_VHTCap_t *pvht_cap,
		     t_u16 bands)
{
	ENTER();

	pvht_cap->ieee_hdr.element_id = VHT_CAPABILITY;
	pvht_cap->ieee_hdr.len = sizeof(VHT_capa_t);

	/* Fill VHT cap info */
	wlan_fill_cap_info(priv, &pvht_cap->vht_cap, bands);

	/* rx MCS map */
	pvht_cap->vht_cap.mcs_sets.rx_mcs_map =
		GET_DEVRXMCSMAP(priv->usr_dot_11ac_mcs_support);

	/* rx highest rate */
	pvht_cap->vht_cap.mcs_sets.rx_max_rate =
		wlan_convert_mcsmap_to_maxrate(priv, bands,
					       pvht_cap->vht_cap.mcs_sets.
					       rx_mcs_map);

	/* tx MCS map */
	pvht_cap->vht_cap.mcs_sets.tx_mcs_map =
		GET_DEVTXMCSMAP(priv->usr_dot_11ac_mcs_support);
	/* tx highest rate */
	pvht_cap->vht_cap.mcs_sets.tx_max_rate =
		wlan_convert_mcsmap_to_maxrate(priv, bands,
					       pvht_cap->vht_cap.mcs_sets.
					       tx_mcs_map);

	LEAVE();
	return;
}

/*
 *  @brief This function check if AP is in 11ac mode
 *
 *  @param priv         A pointer to mlan_private structure
 *
 *  @return             MTRUE/MFALSE
 */
t_u8
wlan_is_ap_in_11ac_mode(mlan_private *priv)
{
	BSSDescriptor_t *pbss_desc;
	IEEEtypes_VHTOprat_t *vht_oprat = MNULL;
	pbss_desc = &priv->curr_bss_params.bss_descriptor;
	vht_oprat = pbss_desc->pvht_oprat;
	if (!pbss_desc->pvht_cap)
		return MFALSE;
	if (vht_oprat && (vht_oprat->ieee_hdr.element_id == VHT_OPERATION)) {
		if (vht_oprat->chan_width == VHT_OPER_CHWD_20_40MHZ)
			return MFALSE;
		else
			return MTRUE;
	} else
		return MFALSE;
}

/**
 *  @brief This function fills the VHTOperation ie out put format is CPU
 *
 *  @param priv         A pointer to mlan_private structure
 *  @param vht_oprat    A pointer to IEEEtypes_VHTOprat_t structure
 *  @param sta_ptr      A pointer to sta_node
 *
 *  @return             N/A
 */
void
wlan_fill_tdls_vht_oprat_ie(mlan_private *priv,
			    IEEEtypes_VHTOprat_t *vht_oprat, sta_node *sta_ptr)
{
	t_u8 supp_chwd_set;
	t_u8 peer_supp_chwd_set;
	t_u8 ap_supp_chwd_set;
	t_u32 usr_vht_cap_info;

	t_u16 mcs_map_user = 0;
	t_u16 mcs_map_resp = 0;
	t_u16 mcs_map_result = 0;
	t_u16 mcs_user = 0;
	t_u16 mcs_resp = 0;
	t_u16 nss;
	t_u8 chan_bw = 0;
	BSSDescriptor_t *pbss_desc;
	IEEEtypes_VHTCap_t *pvht_cap = &sta_ptr->vht_cap;
	IEEEtypes_VHTCap_t *ap_vht_cap = MNULL;
	ENTER();

	pbss_desc = &priv->curr_bss_params.bss_descriptor;

	/* Check if AP is in 11ac mode */
	if (MFALSE == wlan_is_ap_in_11ac_mode(priv)) {
		if (sta_ptr->ExtCap.ieee_hdr.element_id != EXT_CAPABILITY) {
			PRINTM(MMSG, "No Peer's Ext_cap info\n");
			return;
		}
		if (!ISSUPP_EXTCAP_TDLS_WIDER_BANDWIDTH
		    (sta_ptr->ExtCap.ext_cap)) {
			PRINTM(MMSG,
			       "Peer don't support Wider Bandwitch in Ext_cap\n");
			return;
		}
	} else {
		ap_vht_cap = pbss_desc->pvht_cap;
	}

	vht_oprat->ieee_hdr.element_id = VHT_OPERATION;
	vht_oprat->ieee_hdr.len =
		sizeof(IEEEtypes_VHTOprat_t) - sizeof(IEEEtypes_Header_t);

	if (pbss_desc->bss_band & BAND_A)
		usr_vht_cap_info = priv->usr_dot_11ac_dev_cap_a;
	else
		usr_vht_cap_info = priv->usr_dot_11ac_dev_cap_bg;

	/* find the minmum bandwith between AP/TDLS peers */
	supp_chwd_set = GET_VHTCAP_CHWDSET(usr_vht_cap_info);
	peer_supp_chwd_set = GET_VHTCAP_CHWDSET(pvht_cap->vht_cap.vht_cap_info);
	supp_chwd_set = MIN(supp_chwd_set, peer_supp_chwd_set);

	/* We need check AP's bandwidth when TDLS_WIDER_BANDWIDTH is off */
	if (ap_vht_cap &&
	    !ISSUPP_EXTCAP_TDLS_WIDER_BANDWIDTH(sta_ptr->ExtCap.ext_cap)) {
		ap_supp_chwd_set =
			GET_VHTCAP_CHWDSET(ap_vht_cap->vht_cap.vht_cap_info);
		supp_chwd_set = MIN(supp_chwd_set, ap_supp_chwd_set);
	}
	switch (supp_chwd_set) {
	case VHT_CAP_CHWD_80MHZ:
		vht_oprat->chan_width = VHT_OPER_CHWD_80MHZ;
		break;
	case VHT_CAP_CHWD_160MHZ:
		vht_oprat->chan_width = VHT_OPER_CHWD_160MHZ;
		break;
	case VHT_CAP_CHWD_80_80MHZ:
		vht_oprat->chan_width = VHT_OPER_CHWD_80_80MHZ;
		break;
	}

	/* Fill BASIC VHT MCS and NSS Set */
	/* rx MCS Set, find the minimum of the user rx mcs and peer rx mcs */
	mcs_map_user = GET_DEVRXMCSMAP(priv->usr_dot_11ac_mcs_support);
	mcs_map_resp = pvht_cap->vht_cap.mcs_sets.rx_mcs_map;
	mcs_map_result = 0;
	for (nss = 1; nss <= 8; nss++) {
		mcs_user = GET_VHTNSSMCS(mcs_map_user, nss);
		mcs_resp = GET_VHTNSSMCS(mcs_map_resp, nss);
		if ((mcs_user == NO_NSS_SUPPORT) ||
		    (mcs_resp == NO_NSS_SUPPORT))
			SET_VHTNSSMCS(mcs_map_result, nss, NO_NSS_SUPPORT);
		else
			SET_VHTNSSMCS(mcs_map_result, nss,
				      MIN(mcs_user, mcs_resp));
	}
	/* Basic MCS map */
	vht_oprat->basic_MCS_map = mcs_map_result;
	switch (vht_oprat->chan_width) {
	case VHT_OPER_CHWD_80MHZ:
		chan_bw = CHANNEL_BW_80MHZ;
		break;
	case VHT_OPER_CHWD_160MHZ:
		chan_bw = CHANNEL_BW_160MHZ;
		break;
	case VHT_OPER_CHWD_80_80MHZ:
		chan_bw = CHANNEL_BW_80MHZ;
		break;
	}
	vht_oprat->chan_center_freq_1 =
		wlan_get_center_freq_idx(priv, BAND_AAC, pbss_desc->channel,
					 chan_bw);

	LEAVE();
	return;
}

/**
 *  @brief This function would check whether support 80+80Mhz
 *
 *  @param pmpriv A pointer to mlan_private structure
 *  @param pbss_desc   A pointer to BSSDescriptor_t structure
 *
 *  @return  ret  suport 80+80Mhz or not
 */
t_u8
wlan_is_80_80_support(mlan_private *pmpriv, BSSDescriptor_t *pbss_desc)
{
	t_u8 ret = MFALSE;
#if defined(PCIE9098) || defined(SD9098) || defined(USB9098) || defined(PCIE9097) || defined(SD9097) || defined(USB9097)
	t_u16 rx_nss = 0, tx_nss = 0;
	IEEEtypes_VHTCap_t *pvht_cap = pbss_desc->pvht_cap;
	MrvlIEtypes_He_cap_t *phecap = MNULL;
	IEEEtypes_HECap_t *pBsshecap = MNULL;
#endif

	ENTER();

#if defined(PCIE9098) || defined(SD9098) || defined(USB9098) || defined(PCIE9097) || defined(SD9097) || defined(USB9097)
	if (!IS_CARD9098(pmpriv->adapter->card_type) &&
	    !IS_CARD9097(pmpriv->adapter->card_type))
		return ret;
	/** check band A */
	if (!(pbss_desc->bss_band & BAND_A))
		return ret;

	/** check band A antenna setting */
	rx_nss = GET_RXMCSSUPP(pmpriv->adapter->user_htstream >> 8);
	tx_nss = GET_TXMCSSUPP(pmpriv->adapter->user_htstream >> 8) & 0x0f;
	/** check if support 2*2 */
	if (rx_nss != 2 || tx_nss != 2)
		return ret;
	/** check if AP support AC 80P80 */
	if (ISSUPP_11ACBW8080(pmpriv->usr_dot_11ac_dev_cap_a) && pvht_cap &&
	    ISSUPP_11ACBW8080(pvht_cap->vht_cap.vht_cap_info))
		ret = MTRUE;
	/** check if AP support AX 80P80 */
	if (pbss_desc->phe_cap) {
		pBsshecap = (IEEEtypes_HECap_t *) pbss_desc->phe_cap;
		phecap = (MrvlIEtypes_He_cap_t *) pmpriv->user_he_cap;
		if (ret && (phecap->he_phy_cap[0] & MBIT(4)) &&
		    (pBsshecap->he_phy_cap[0] & MBIT(4)))
			ret = MTRUE;
		else
			ret = MFALSE;
	}
#endif
	LEAVE();
	return ret;
}

/**
 *  @brief This function append the 802_11N tlv
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param pbss_desc    A pointer to BSSDescriptor_t structure
 *  @param ppbuffer     A Pointer to command buffer pointer
 *
 *  @return bytes added to the buffer
 */
int
wlan_cmd_append_11ac_tlv(mlan_private *pmpriv, BSSDescriptor_t *pbss_desc,
			 t_u8 **ppbuffer)
{
	pmlan_adapter pmadapter = pmpriv->adapter;
	MrvlIETypes_VHTCap_t *pvht_cap;
	MrvlIETypes_OperModeNtf_t *pmrvl_oper_mode;
	t_u16 mcs_map_user = 0;
	t_u16 nss;
	int ret_len = 0;
	t_u8 bw_80p80 = MFALSE;
#if defined(PCIE9098) || defined(SD9098) || defined(USB9098) || defined(PCIE9097) || defined(USB9097) || defined(SD9097)
	t_u16 rx_nss = 0;
#endif

	ENTER();

	/* Null Checks */
	if (ppbuffer == MNULL) {
		LEAVE();
		return 0;
	}
	if (*ppbuffer == MNULL) {
		LEAVE();
		return 0;
	}

	/* VHT Capabilities IE */
	if (pbss_desc->pvht_cap &&
	    wlan_get_nss_vht_mcs(pbss_desc->pvht_cap->vht_cap.mcs_sets.
				 rx_mcs_map)) {
		pvht_cap = (MrvlIETypes_VHTCap_t *)*ppbuffer;
		memset(pmadapter, pvht_cap, 0, sizeof(MrvlIETypes_VHTCap_t));
		pvht_cap->header.type = wlan_cpu_to_le16(VHT_CAPABILITY);
		pvht_cap->header.len = sizeof(VHT_capa_t);
		memcpy_ext(pmadapter,
			   (t_u8 *)pvht_cap + sizeof(MrvlIEtypesHeader_t),
			   (t_u8 *)pbss_desc->pvht_cap +
			   sizeof(IEEEtypes_Header_t),
			   pvht_cap->header.len, sizeof(VHT_capa_t));
		bw_80p80 = wlan_is_80_80_support(pmpriv, pbss_desc);
		wlan_fill_vht_cap_tlv(pmpriv, pvht_cap, pbss_desc->bss_band,
				      MTRUE, bw_80p80);

		HEXDUMP("VHT_CAPABILITIES IE", (t_u8 *)pvht_cap,
			sizeof(MrvlIETypes_VHTCap_t));
		*ppbuffer += sizeof(MrvlIETypes_VHTCap_t);
		ret_len += sizeof(MrvlIETypes_VHTCap_t);
		pvht_cap->header.len = wlan_cpu_to_le16(pvht_cap->header.len);
	} else {
		LEAVE();
		return 0;
	}

	/* Operating Mode Notification IE */
	pmrvl_oper_mode = (MrvlIETypes_OperModeNtf_t *)*ppbuffer;
	memset(pmadapter, pmrvl_oper_mode, 0,
	       sizeof(MrvlIETypes_OperModeNtf_t));
	pmrvl_oper_mode->header.type = wlan_cpu_to_le16(OPER_MODE_NTF);
	pmrvl_oper_mode->header.len = sizeof(t_u8);

	if (pmpriv->usr_dot_11ac_opermode_bw ||
	    pmpriv->usr_dot_11ac_opermode_nss) {
		pmrvl_oper_mode->oper_mode |=
			(pmpriv->usr_dot_11ac_opermode_nss - 1) << 4;
		pmrvl_oper_mode->oper_mode |=
			pmpriv->usr_dot_11ac_opermode_bw - 1;
		if (pbss_desc->bss_band & BAND_G) {
			if (!(IS_OPER_MODE_20M(pmrvl_oper_mode->oper_mode))) {
				if (pbss_desc->pht_cap->ht_cap.ht_cap_info &
				    MBIT(1))
					SET_OPER_MODE_40M(pmrvl_oper_mode->
							  oper_mode);
				else
					SET_OPER_MODE_20M(pmrvl_oper_mode->
							  oper_mode);
			}
		}
	} else {
		/** set default bandwidth:80M*/
		SET_OPER_MODE_80M(pmrvl_oper_mode->oper_mode);
#if defined(PCIE9098) || defined(SD9098) || defined(USB9098) || defined(PCIE9097) || defined(SD9097) || defined(USB9097)
		if (IS_CARD9098(pmadapter->card_type) ||
		    IS_CARD9097(pmadapter->card_type)) {
			if (pbss_desc->bss_band & BAND_A)
				rx_nss = GET_RXMCSSUPP(pmadapter->
						       user_htstream >> 8);
			else
				rx_nss = GET_RXMCSSUPP(pmadapter->
						       user_htstream);
		}
#endif
		mcs_map_user =
			GET_DEVRXMCSMAP(pmpriv->usr_dot_11ac_mcs_support);
		nss = wlan_get_nss_num_vht_mcs(mcs_map_user);

#if defined(PCIE9098) || defined(SD9098) || defined(USB9098) || defined(PCIE9097) || defined(SD9097) || defined(USB9097)
		if (IS_CARD9098(pmadapter->card_type) ||
		    IS_CARD9097(pmadapter->card_type)) {
			PRINTM(MCMND, "rx_nss=%d nss=%d\n", rx_nss, nss);
			nss = MIN(rx_nss, nss);
		}
#endif

		pmrvl_oper_mode->oper_mode |= (nss - 1) << 4;

		switch (pbss_desc->curr_bandwidth) {
		case BW_20MHZ:
			SET_OPER_MODE_20M(pmrvl_oper_mode->oper_mode);
			break;
		case BW_40MHZ:
			SET_OPER_MODE_40M(pmrvl_oper_mode->oper_mode);
			break;
		case BW_80MHZ:
		default:
			break;
		}
	}
	HEXDUMP("OPER MODE NTF IE", (t_u8 *)pmrvl_oper_mode,
		sizeof(MrvlIETypes_OperModeNtf_t));
	*ppbuffer += sizeof(MrvlIETypes_OperModeNtf_t);
	ret_len += sizeof(MrvlIETypes_OperModeNtf_t);
	pmrvl_oper_mode->header.len =
		wlan_cpu_to_le16(pmrvl_oper_mode->header.len);

	LEAVE();
	return ret_len;
}

/**
 *  @brief 11ac configuration handler
 *
 *  @param pmadapter    A pointer to mlan_adapter structure
 *  @param pioctl_req   A pointer to ioctl request buffer
 *
 *  @return     MLAN_STATUS_SUCCESS --success, otherwise fail
 */
mlan_status
wlan_11ac_cfg_ioctl(pmlan_adapter pmadapter, pmlan_ioctl_req pioctl_req)
{
	mlan_status status = MLAN_STATUS_SUCCESS;
	mlan_ds_11ac_cfg *cfg = MNULL;

	ENTER();

	if (pioctl_req->buf_len < sizeof(mlan_ds_11ac_cfg)) {
		PRINTM(MINFO, "MLAN bss IOCTL length is too short.\n");
		pioctl_req->data_read_written = 0;
		pioctl_req->buf_len_needed = sizeof(mlan_ds_11ac_cfg);
		pioctl_req->status_code = MLAN_ERROR_INVALID_PARAMETER;
		LEAVE();
		return MLAN_STATUS_RESOURCE;
	}
	cfg = (mlan_ds_11ac_cfg *)pioctl_req->pbuf;
	switch (cfg->sub_command) {
	case MLAN_OID_11AC_VHT_CFG:
		status = wlan_11ac_ioctl_vhtcfg(pmadapter, pioctl_req);
		break;
	case MLAN_OID_11AC_CFG_SUPPORTED_MCS_SET:
		status = wlan_11ac_ioctl_supported_mcs_set(pmadapter,
							   pioctl_req);
		break;
	case MLAN_OID_11AC_OPERMODE_CFG:
		status = wlan_11ac_ioctl_opermodecfg(pmadapter, pioctl_req);
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
 *  @brief This function prepares 11ac cfg command
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param cmd      A pointer to HostCmd_DS_COMMAND structure
 *  @param cmd_action   the action: GET or SET
 *  @param pdata_buf    A pointer to data buffer
 *  @return         MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_cmd_11ac_cfg(pmlan_private pmpriv,
		  HostCmd_DS_COMMAND *cmd, t_u16 cmd_action, t_void *pdata_buf)
{
	pmlan_adapter pmadapter = pmpriv->adapter;
	HostCmd_DS_11AC_CFG *vhtcfg = &cmd->params.vhtcfg;
	mlan_ds_11ac_vht_cfg *vht_cfg = (mlan_ds_11ac_vht_cfg *)pdata_buf;

	ENTER();
	cmd->command = wlan_cpu_to_le16(HostCmd_CMD_11AC_CFG);
	cmd->size = wlan_cpu_to_le16(sizeof(HostCmd_DS_11AC_CFG) + S_DS_GEN);
	vhtcfg->action = wlan_cpu_to_le16(cmd_action);
	vhtcfg->band_config = vht_cfg->band & 0xFF;
	// block user enable 80MHZ
	if (IS_FW_SUPPORT_NO_80MHZ(pmadapter))
		vht_cfg->bwcfg = 0;

	vhtcfg->misc_config = vht_cfg->txrx & 0x3;
	if (vhtcfg->misc_config != 2)
		vhtcfg->misc_config |= (vht_cfg->bwcfg << 2);

	vhtcfg->vht_cap_info = wlan_cpu_to_le32(vht_cfg->vht_cap_info);
	vht_cfg->vht_rx_mcs = wlan_cpu_to_le32(vht_cfg->vht_rx_mcs);
	memcpy_ext(pmadapter, &vhtcfg->vht_supp_mcs_set[0],
		   &vht_cfg->vht_rx_mcs, sizeof(t_u32), sizeof(t_u32));
	vht_cfg->vht_tx_mcs = wlan_cpu_to_le32(vht_cfg->vht_tx_mcs);
	memcpy_ext(pmadapter, &vhtcfg->vht_supp_mcs_set[4],
		   &vht_cfg->vht_tx_mcs, sizeof(t_u32), sizeof(t_u32));
	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function handles the command response of 11accfg
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param resp         A pointer to HostCmd_DS_COMMAND
 *  @param pioctl_buf   A pointer to mlan_ioctl_req structure
 *
 *  @return        MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_ret_11ac_cfg(pmlan_private pmpriv,
		  HostCmd_DS_COMMAND *resp, mlan_ioctl_req *pioctl_buf)
{
	pmlan_adapter pmadapter = pmpriv->adapter;
	mlan_ds_11ac_cfg *cfg = MNULL;
	HostCmd_DS_11AC_CFG *vhtcfg = &resp->params.vhtcfg;

	ENTER();
	if (pioctl_buf &&
	    (wlan_le16_to_cpu(vhtcfg->action) == HostCmd_ACT_GEN_GET)) {
		cfg = (mlan_ds_11ac_cfg *)pioctl_buf->pbuf;
		cfg->param.vht_cfg.band = vhtcfg->band_config;
		cfg->param.vht_cfg.txrx = vhtcfg->misc_config & 0x03;
		if (cfg->param.vht_cfg.txrx & 0x1)
			cfg->param.vht_cfg.bwcfg =
				(vhtcfg->misc_config & 0x04) >> 2;
		else
			cfg->param.vht_cfg.bwcfg = 0;

		cfg->param.vht_cfg.vht_cap_info =
			wlan_le32_to_cpu(vhtcfg->vht_cap_info);
		memcpy_ext(pmadapter, &cfg->param.vht_cfg.vht_rx_mcs,
			   &vhtcfg->vht_supp_mcs_set[0], sizeof(t_u32),
			   sizeof(t_u32));
		cfg->param.vht_cfg.vht_rx_mcs =
			wlan_le32_to_cpu(cfg->param.vht_cfg.vht_rx_mcs);
		memcpy_ext(pmadapter, &cfg->param.vht_cfg.vht_tx_mcs,
			   &vhtcfg->vht_supp_mcs_set[4], sizeof(t_u32),
			   sizeof(t_u32));
		cfg->param.vht_cfg.vht_tx_mcs =
			wlan_le32_to_cpu(cfg->param.vht_cfg.vht_tx_mcs);
		cfg->param.vht_cfg.vht_rx_max_rate =
			wlan_convert_mcsmap_to_maxrate(pmpriv,
						       cfg->param.vht_cfg.band,
						       cfg->param.vht_cfg.
						       vht_rx_mcs);
		cfg->param.vht_cfg.vht_tx_max_rate =
			wlan_convert_mcsmap_to_maxrate(pmpriv,
						       cfg->param.vht_cfg.band,
						       cfg->param.vht_cfg.
						       vht_tx_mcs);
	}
	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

void
wlan_update_11ac_cap(mlan_private *pmpriv)
{
	mlan_adapter *pmadapter = pmpriv->adapter;

	pmpriv->usr_dot_11ac_mcs_support = pmadapter->hw_dot_11ac_mcs_support;
	pmpriv->usr_dot_11ac_dev_cap_bg =
		pmadapter->hw_dot_11ac_dev_cap &
		~DEFALUT_11AC_CAP_BEAMFORMING_RESET_MASK;
	pmpriv->usr_dot_11ac_dev_cap_a =
		pmadapter->hw_dot_11ac_dev_cap &
		~DEFALUT_11AC_CAP_BEAMFORMING_RESET_MASK;
	pmpriv->usr_dot_11ac_bw = BW_FOLLOW_VHTCAP;
}

/**
 *  @brief This function check if 11AC is allowed in bandcfg
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param bss_band     bss band
 *
 *  @return 0--not allowed, other value allowed
 */
t_u8
wlan_11ac_bandconfig_allowed(mlan_private *pmpriv, t_u16 bss_band)
{
	if (pmpriv->bss_mode == MLAN_BSS_MODE_IBSS) {
		if (bss_band & BAND_G)
			return (pmpriv->adapter->adhoc_start_band & BAND_GAC);
		else if (bss_band & BAND_A)
			return (pmpriv->adapter->adhoc_start_band & BAND_AAC);
	} else {
		if (bss_band & BAND_G)
			return (pmpriv->config_bands & BAND_GAC);
		else if (bss_band & BAND_A)
			return (pmpriv->config_bands & BAND_AAC);
	}
	return 0;
}
