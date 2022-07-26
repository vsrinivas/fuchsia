/** @file mlan_11ax.c
 *
 *  @brief This file contains the functions for 11ax related features.
 *
 *
 *  Copyright 2018-2021 NXP
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
#include "mlan_ioctl.h"
#include "mlan_fw.h"
#include "mlan_main.h"
#include "mlan_wmm.h"
#include "mlan_11n.h"
#include "mlan_11ax.h"
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

/********************************************************
			Global Functions
********************************************************/

#if 0
/**
 *  @brief This function prints the 802.11ax HE mac capability
 *
 *  @param pmadapter     A pointer to mlan_adapter structure
 *  @param cap           Capability value
 *
 *  @return        N/A
 */
static void
wlan_show_dot11axmaccap(pmlan_adapter pmadapter, t_u32 cap)
{
	ENTER();

	LEAVE();
	return;
}
#endif

/**
 *  @brief This function check if AP support TWT Response.
 *
 *  @param pbss_desc    A pointer to BSSDescriptor_t structure
 *
 *  @return        MTRUE/MFALSE
 */
static t_u8
wlan_check_ap_11ax_twt_supported(BSSDescriptor_t *pbss_desc)
{
	if (!pbss_desc->phe_cap)
		return MFALSE;
	if (!(pbss_desc->phe_cap->he_mac_cap[0] & HE_MAC_CAP_TWT_REQ_SUPPORT))
		return MFALSE;
	if (!pbss_desc->pext_cap)
		return MFALSE;
	if (!ISSUPP_EXTCAP_EXT_TWT_RESP(pbss_desc->pext_cap->ext_cap))
		return MFALSE;
	return MTRUE;
}

/**
 *  @brief This function check if we should enable TWT support
 *
 *  @param pbss_desc    A pointer to BSSDescriptor_t structure
 *
 *  @return        MTRUE/MFALSE
 */
t_u8
wlan_check_11ax_twt_supported(mlan_private *pmpriv, BSSDescriptor_t *pbss_desc)
{
	MrvlIEtypes_He_cap_t *phecap =
		(MrvlIEtypes_He_cap_t *) & pmpriv->user_he_cap;
	MrvlIEtypes_He_cap_t *hw_he_cap = (MrvlIEtypes_He_cap_t *)
		& pmpriv->adapter->hw_he_cap;
	if (pbss_desc && !wlan_check_ap_11ax_twt_supported(pbss_desc)) {
		PRINTM(MINFO, "AP don't support twt feature\n");
		return MFALSE;
	}
	if (pbss_desc) {
		if (pbss_desc->bss_band & BAND_A) {
			hw_he_cap = (MrvlIEtypes_He_cap_t *)
				& pmpriv->adapter->hw_he_cap;
			phecap = (MrvlIEtypes_He_cap_t *) & pmpriv->user_he_cap;
		} else {
			hw_he_cap = (MrvlIEtypes_He_cap_t *)
				& pmpriv->adapter->hw_2g_he_cap;
			phecap = (MrvlIEtypes_He_cap_t *)
				& pmpriv->user_2g_he_cap;
		}
	}
	if (!(hw_he_cap->he_mac_cap[0] & HE_MAC_CAP_TWT_REQ_SUPPORT)) {
		PRINTM(MINFO, "FW don't support TWT\n");
		return MFALSE;
	}
	if (phecap->he_mac_cap[0] & HE_MAC_CAP_TWT_REQ_SUPPORT)
		return MTRUE;
	PRINTM(MINFO, "USER HE_MAC_CAP don't support TWT\n");
	return MFALSE;
}

#if 0
/**
 *  @brief This function prints the 802.11ax HE PHY cap
 *
 *  @param pmadapter A pointer to mlan_adapter structure
 *  @param support   Support value
 *
 *  @return        N/A
 */
static void
wlan_show_dot11axphycap(pmlan_adapter pmadapter, t_u32 support)
{
	ENTER();

	LEAVE();
	return;
}
#endif
/**
 *  @brief This function fills the HE cap tlv out put format is LE, not CPU
 *
 *  @param priv         A pointer to mlan_private structure
 *  @param band         5G or 2.4 G
 *  @param phe_cap      A pointer to MrvlIEtypes_Data_t structure
 *  @param flag         TREU--pvht_cap has the setting for resp
 *                            MFALSE -- pvht_cap is clean
 *
 *  @return bytes added to the phe_cap
 */
t_u16
wlan_fill_he_cap_tlv(mlan_private *pmpriv, t_u8 band,
		     MrvlIEtypes_Extension_t * phe_cap, t_u8 flag)
{
	pmlan_adapter pmadapter = pmpriv->adapter;
	t_u16 len = 0;
#if defined(PCIE9098) || defined(SD9098) || defined(USB9098) || defined(PCIE9097) || defined(SD9097) || defined(USB9097)
	t_u16 rx_nss = 0, tx_nss = 0;
#endif
	MrvlIEtypes_He_cap_t *phecap = MNULL;
	t_u8 nss = 0;
	t_u16 cfg_value = 0;
	t_u16 hw_value = 0;
	MrvlIEtypes_He_cap_t *phw_hecap = MNULL;

	if (!phe_cap) {
		LEAVE();
		return 0;
	}
	if (band & BAND_A) {
		memcpy_ext(pmadapter, (t_u8 *)phe_cap, pmpriv->user_he_cap,
			   pmpriv->user_hecap_len,
			   sizeof(MrvlIEtypes_He_cap_t));
		len = pmpriv->user_hecap_len;
		phw_hecap = (MrvlIEtypes_He_cap_t *) pmadapter->hw_he_cap;
	} else {
		memcpy_ext(pmadapter, (t_u8 *)phe_cap, pmpriv->user_2g_he_cap,
			   pmpriv->user_2g_hecap_len,
			   sizeof(MrvlIEtypes_He_cap_t));
		len = pmpriv->user_2g_hecap_len;
		phw_hecap = (MrvlIEtypes_He_cap_t *) pmadapter->hw_2g_he_cap;
	}
	phe_cap->type = wlan_cpu_to_le16(phe_cap->type);
	phe_cap->len = wlan_cpu_to_le16(phe_cap->len);
#if defined(PCIE9098) || defined(SD9098) || defined(USB9098) || defined(PCIE9097) || defined(SD9097) || defined(USB9097)
	if (IS_CARD9098(pmpriv->adapter->card_type) ||
	    IS_CARD9097(pmpriv->adapter->card_type)) {
		if (band & BAND_A) {
			rx_nss = GET_RXMCSSUPP(pmpriv->adapter->user_htstream >>
					       8);
			tx_nss = GET_TXMCSSUPP(pmpriv->adapter->user_htstream >>
					       8) & 0x0f;
		} else {
			rx_nss = GET_RXMCSSUPP(pmpriv->adapter->user_htstream);
			tx_nss = GET_TXMCSSUPP(pmpriv->adapter->user_htstream) &
				0x0f;
		}
	}
#endif
	phecap = (MrvlIEtypes_He_cap_t *) phe_cap;
	for (nss = 1; nss <= 8; nss++) {
		cfg_value = GET_HE_NSSMCS(phecap->rx_mcs_80, nss);
		hw_value = GET_HE_NSSMCS(phw_hecap->rx_mcs_80, nss);
#if defined(PCIE9098) || defined(SD9098) || defined(USB9098) || defined(PCIE9097) || defined(SD9097) || defined(USB9097)
		if ((rx_nss != 0) && (nss > rx_nss))
			cfg_value = NO_NSS_SUPPORT;
#endif
		if ((hw_value == NO_NSS_SUPPORT) ||
		    (cfg_value == NO_NSS_SUPPORT))
			SET_HE_NSSMCS(phecap->rx_mcs_80, nss, NO_NSS_SUPPORT);
		else
			SET_HE_NSSMCS(phecap->rx_mcs_80,
				      nss, MIN(cfg_value, hw_value));
	}
	for (nss = 1; nss <= 8; nss++) {
		cfg_value = GET_HE_NSSMCS(phecap->tx_mcs_80, nss);
		hw_value = GET_HE_NSSMCS(phw_hecap->tx_mcs_80, nss);
#if defined(PCIE9098) || defined(SD9098) || defined(USB9098) || defined(PCIE9097) || defined(SD9097) || defined(USB9097)
		if ((tx_nss != 0) && (nss > tx_nss))
			cfg_value = NO_NSS_SUPPORT;
#endif
		if ((hw_value == NO_NSS_SUPPORT) ||
		    (cfg_value == NO_NSS_SUPPORT))
			SET_HE_NSSMCS(phecap->tx_mcs_80, nss, NO_NSS_SUPPORT);
		else
			SET_HE_NSSMCS(phecap->tx_mcs_80,
				      nss, MIN(cfg_value, hw_value));
	}
	PRINTM(MCMND, "Set: HE rx mcs set 0x%08x tx mcs set 0x%08x\n",
	       phecap->rx_mcs_80, phecap->tx_mcs_80);

	DBG_HEXDUMP(MCMD_D, "fill_11ax_tlv", (t_u8 *)phecap, len);
	LEAVE();
	return len;
}

/**
 *  @brief This function append the 802_11ax HE capability  tlv
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param pbss_desc    A pointer to BSSDescriptor_t structure
 *  @param ppbuffer     A Pointer to command buffer pointer
 *
 *  @return bytes added to the buffer
 */
int
wlan_cmd_append_11ax_tlv(mlan_private *pmpriv, BSSDescriptor_t *pbss_desc,
			 t_u8 **ppbuffer)
{
	pmlan_adapter pmadapter = pmpriv->adapter;
	MrvlIEtypes_He_cap_t *phecap = MNULL;
	int len = 0;
	t_u8 bw_80p80 = MFALSE;
#if defined(PCIE9098) || defined(SD9098) || defined(USB9098) || defined(PCIE9097) || defined(SD9097) || defined(USB9097)
	t_u16 rx_nss = 0, tx_nss = 0;
#endif
	t_u8 nss = 0;
	t_u16 cfg_value = 0;
	t_u16 hw_value = 0;
	MrvlIEtypes_He_cap_t *phw_hecap = MNULL;

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
	/** check if AP support HE, if not return right away */
	if (!pbss_desc->phe_cap) {
		LEAVE();
		return 0;
	}
	bw_80p80 = wlan_is_80_80_support(pmpriv, pbss_desc);
	phecap = (MrvlIEtypes_He_cap_t *) * ppbuffer;
	if (pbss_desc->bss_band & BAND_A) {
		memcpy_ext(pmadapter, *ppbuffer, pmpriv->user_he_cap,
			   pmpriv->user_hecap_len, pmpriv->user_hecap_len);
		*ppbuffer += pmpriv->user_hecap_len;
		len = pmpriv->user_hecap_len;
		phw_hecap = (MrvlIEtypes_He_cap_t *) pmadapter->hw_he_cap;
	} else {
		memcpy_ext(pmadapter, *ppbuffer, pmpriv->user_2g_he_cap,
			   pmpriv->user_2g_hecap_len,
			   pmpriv->user_2g_hecap_len);
		*ppbuffer += pmpriv->user_2g_hecap_len;
		len = pmpriv->user_2g_hecap_len;
		phw_hecap = (MrvlIEtypes_He_cap_t *) pmadapter->hw_2g_he_cap;
	}
	phecap->type = wlan_cpu_to_le16(phecap->type);
	phecap->len = wlan_cpu_to_le16(phecap->len);
#if defined(PCIE9098) || defined(SD9098) || defined(USB9098) || defined(PCIE9097) || defined(SD9097) || defined(USB9097)
	if (IS_CARD9098(pmpriv->adapter->card_type) ||
	    IS_CARD9097(pmpriv->adapter->card_type)) {
		if (pbss_desc->bss_band & BAND_A) {
			rx_nss = GET_RXMCSSUPP(pmpriv->adapter->user_htstream >>
					       8);
			tx_nss = GET_TXMCSSUPP(pmpriv->adapter->user_htstream >>
					       8) & 0x0f;
		} else {
			rx_nss = GET_RXMCSSUPP(pmpriv->adapter->user_htstream);
			tx_nss = GET_TXMCSSUPP(pmpriv->adapter->user_htstream) &
				0x0f;
		}
		/** force 1x1 when enable 80P80 */
		if (bw_80p80)
			rx_nss = tx_nss = 1;
	}
#endif
	for (nss = 1; nss <= 8; nss++) {
		cfg_value = GET_HE_NSSMCS(phecap->rx_mcs_80, nss);
		hw_value = GET_HE_NSSMCS(phw_hecap->rx_mcs_80, nss);
#if defined(PCIE9098) || defined(SD9098) || defined(USB9098) || defined(PCIE9097) || defined(SD9097) || defined(USB9097)
		if ((rx_nss != 0) && (nss > rx_nss))
			cfg_value = NO_NSS_SUPPORT;
#endif
		if ((hw_value == NO_NSS_SUPPORT) ||
		    (cfg_value == NO_NSS_SUPPORT))
			SET_HE_NSSMCS(phecap->rx_mcs_80, nss, NO_NSS_SUPPORT);
		else
			SET_HE_NSSMCS(phecap->rx_mcs_80,
				      nss, MIN(cfg_value, hw_value));
	}
	for (nss = 1; nss <= 8; nss++) {
		cfg_value = GET_HE_NSSMCS(phecap->tx_mcs_80, nss);
		hw_value = GET_HE_NSSMCS(phw_hecap->tx_mcs_80, nss);
#if defined(PCIE9098) || defined(SD9098) || defined(USB9098) || defined(PCIE9097) || defined(SD9097) || defined(USB9097)
		if ((tx_nss != 0) && (nss > tx_nss))
			cfg_value = NO_NSS_SUPPORT;
#endif
		if ((hw_value == NO_NSS_SUPPORT) ||
		    (cfg_value == NO_NSS_SUPPORT))
			SET_HE_NSSMCS(phecap->tx_mcs_80, nss, NO_NSS_SUPPORT);
		else
			SET_HE_NSSMCS(phecap->tx_mcs_80,
				      nss, MIN(cfg_value, hw_value));
	}
	PRINTM(MCMND, "Set: HE rx mcs set 0x%08x tx mcs set 0x%08x\n",
	       phecap->rx_mcs_80, phecap->tx_mcs_80);
	if (!bw_80p80) {
		/** reset BIT3 and BIT4 channel width ,not support 80 + 80*/
		/** not support 160Mhz now, if support,not reset bit3 */
		phecap->he_phy_cap[0] &= ~(MBIT(3) | MBIT(4));
	}
	DBG_HEXDUMP(MCMD_D, "append_11ax_tlv", (t_u8 *)phecap, len);

	LEAVE();
	return len;
}

/**
 *  @brief This function save the 11ax cap from FW.
 *
 *  @param pmadapater   A pointer to mlan_adapter
 *  @param hw_he_cap    A pointer to MrvlIEtypes_Extension_t
 *
 *  @return N/A
 */
void
wlan_update_11ax_cap(mlan_adapter *pmadapter,
		     MrvlIEtypes_Extension_t * hw_he_cap)
{
	MrvlIEtypes_He_cap_t *phe_cap = MNULL;
	t_u8 i = 0;
	t_u8 he_cap_2g = 0;

	ENTER();
	if ((hw_he_cap->len + sizeof(MrvlIEtypesHeader_t)) >
	    sizeof(pmadapter->hw_he_cap)) {
		PRINTM(MERROR, "hw_he_cap too big, len=%d\n", hw_he_cap->len);
		LEAVE();
		return;
	}
	phe_cap = (MrvlIEtypes_He_cap_t *) hw_he_cap;
	if (phe_cap->he_phy_cap[0] &
	    (AX_2G_40MHZ_SUPPORT | AX_2G_20MHZ_SUPPORT)) {
		pmadapter->hw_2g_hecap_len =
			hw_he_cap->len + sizeof(MrvlIEtypesHeader_t);
		memcpy_ext(pmadapter, pmadapter->hw_2g_he_cap,
			   (t_u8 *)hw_he_cap,
			   hw_he_cap->len + sizeof(MrvlIEtypesHeader_t),
			   sizeof(pmadapter->hw_2g_he_cap));
		pmadapter->fw_bands |= BAND_GAX;
		pmadapter->config_bands |= BAND_GAX;
		he_cap_2g = MTRUE;
		DBG_HEXDUMP(MCMD_D, "2.4G HE capability IE ",
			    (t_u8 *)pmadapter->hw_2g_he_cap,
			    pmadapter->hw_2g_hecap_len);
	} else {
		pmadapter->fw_bands |= BAND_AAX;
		pmadapter->config_bands |= BAND_AAX;
		pmadapter->hw_hecap_len =
			hw_he_cap->len + sizeof(MrvlIEtypesHeader_t);
		memcpy_ext(pmadapter, pmadapter->hw_he_cap, (t_u8 *)hw_he_cap,
			   hw_he_cap->len + sizeof(MrvlIEtypesHeader_t),
			   sizeof(pmadapter->hw_he_cap));
		DBG_HEXDUMP(MCMD_D, "5G HE capability IE ",
			    (t_u8 *)pmadapter->hw_he_cap,
			    pmadapter->hw_hecap_len);
	}
	for (i = 0; i < pmadapter->priv_num; i++) {
		if (pmadapter->priv[i]) {
			pmadapter->priv[i]->config_bands =
				pmadapter->config_bands;
			if (he_cap_2g) {
				pmadapter->priv[i]->user_2g_hecap_len =
					pmadapter->hw_2g_hecap_len;
				memcpy_ext(pmadapter,
					   pmadapter->priv[i]->user_2g_he_cap,
					   pmadapter->hw_2g_he_cap,
					   pmadapter->hw_2g_hecap_len,
					   sizeof(pmadapter->priv[i]
						  ->user_2g_he_cap));
			} else {
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
 *  @brief This function check if 11AX is allowed in bandcfg
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param bss_band     bss band
 *
 *  @return 0--not allowed, other value allowed
 */
t_u16
wlan_11ax_bandconfig_allowed(mlan_private *pmpriv, t_u16 bss_band)
{
	if (!IS_FW_SUPPORT_11AX(pmpriv->adapter))
		return MFALSE;
	if (pmpriv->bss_mode == MLAN_BSS_MODE_IBSS) {
		if (bss_band & BAND_G)
			return (pmpriv->adapter->adhoc_start_band & BAND_GAX);
		else if (bss_band & BAND_A)
			return (pmpriv->adapter->adhoc_start_band & BAND_AAX);
	} else {
		if (bss_band & BAND_G)
			return (pmpriv->config_bands & BAND_GAX);
		else if (bss_band & BAND_A)
			return (pmpriv->config_bands & BAND_AAX);
	}
	return MFALSE;
}

/**
 *  @brief Set 11ax configuration
 *
 *  @param pmadapter    A pointer to mlan_adapter structure
 *  @param pioctl_req   A pointer to ioctl request buffer
 *
 *  @return     MLAN_STATUS_PENDING --success, otherwise fail
 */
static mlan_status
wlan_11ax_ioctl_hecfg(pmlan_adapter pmadapter, pmlan_ioctl_req pioctl_req)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_ds_11ax_cfg *cfg = MNULL;
	t_u16 cmd_action = 0;

	ENTER();

	if (pioctl_req->buf_len < sizeof(mlan_ds_11ax_cfg)) {
		PRINTM(MINFO, "MLAN bss IOCTL length is too short.\n");
		pioctl_req->data_read_written = 0;
		pioctl_req->buf_len_needed = sizeof(mlan_ds_11ax_cfg);
		pioctl_req->status_code = MLAN_ERROR_INVALID_PARAMETER;
		LEAVE();
		return MLAN_STATUS_RESOURCE;
	}

	cfg = (mlan_ds_11ax_cfg *) pioctl_req->pbuf;

	if ((cfg->param.he_cfg.band & MBIT(0)) &&
	    !(pmadapter->fw_bands & BAND_GAX)) {
		PRINTM(MERROR, "FW don't support 2.4G AX\n");
		return MLAN_STATUS_FAILURE;
	}
	if ((cfg->param.he_cfg.band & MBIT(1)) &&
	    !(pmadapter->fw_bands & BAND_AAX)) {
		PRINTM(MERROR, "FW don't support 5G AX\n");
		return MLAN_STATUS_FAILURE;
	}
	if (pioctl_req->action == MLAN_ACT_SET)
		cmd_action = HostCmd_ACT_GEN_SET;
	else
		cmd_action = HostCmd_ACT_GEN_GET;

	/* Send request to firmware */
	ret = wlan_prepare_cmd(pmpriv, HostCmd_CMD_11AX_CFG, cmd_action, 0,
			       (t_void *)pioctl_req,
			       (t_void *)&cfg->param.he_cfg);
	if (ret == MLAN_STATUS_SUCCESS)
		ret = MLAN_STATUS_PENDING;

	LEAVE();
	return ret;
}

/**
 *  @brief 11ax configuration handler
 *
 *  @param pmadapter    A pointer to mlan_adapter structure
 *  @param pioctl_req   A pointer to ioctl request buffer
 *
 *  @return     MLAN_STATUS_SUCCESS --success, otherwise fail
 */
mlan_status
wlan_11ax_cfg_ioctl(pmlan_adapter pmadapter, pmlan_ioctl_req pioctl_req)
{
	mlan_status status = MLAN_STATUS_SUCCESS;
	mlan_ds_11ax_cfg *cfg = MNULL;

	ENTER();

	cfg = (mlan_ds_11ax_cfg *) pioctl_req->pbuf;
	switch (cfg->sub_command) {
	case MLAN_OID_11AX_HE_CFG:
		status = wlan_11ax_ioctl_hecfg(pmadapter, pioctl_req);
		break;
	case MLAN_OID_11AX_CMD_CFG:
		status = wlan_11ax_ioctl_cmd(pmadapter, pioctl_req);
		break;
	case MLAN_OID_11AX_TWT_CFG:
		status = wlan_11ax_ioctl_twtcfg(pmadapter, pioctl_req);
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
 *  @brief This function prepares 11ax cfg command
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param cmd      A pointer to HostCmd_DS_COMMAND structure
 *  @param cmd_action   the action: GET or SET
 *  @param pdata_buf    A pointer to data buffer
 *  @return         MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_cmd_11ax_cfg(pmlan_private pmpriv,
		  HostCmd_DS_COMMAND *cmd, t_u16 cmd_action, t_void *pdata_buf)
{
	pmlan_adapter pmadapter = pmpriv->adapter;
	HostCmd_DS_11AX_CFG *axcfg = &cmd->params.axcfg;
	mlan_ds_11ax_he_cfg *hecfg = (mlan_ds_11ax_he_cfg *) pdata_buf;
	MrvlIEtypes_Extension_t *tlv = MNULL;
	t_u8 *pos = MNULL;

	ENTER();
	cmd->command = wlan_cpu_to_le16(HostCmd_CMD_11AX_CFG);
	cmd->size = sizeof(HostCmd_DS_11AX_CFG) + S_DS_GEN;

	axcfg->action = wlan_cpu_to_le16(cmd_action);
	axcfg->band_config = hecfg->band & 0xFF;

	pos = (t_u8 *)axcfg->val;
	/**HE Capability */
	if (hecfg->he_cap.len && (hecfg->he_cap.ext_id == HE_CAPABILITY)) {
		tlv = (MrvlIEtypes_Extension_t *) pos;
		tlv->type = wlan_cpu_to_le16(hecfg->he_cap.id);
		tlv->len = wlan_cpu_to_le16(hecfg->he_cap.len);
		memcpy_ext(pmadapter, &tlv->ext_id, &hecfg->he_cap.ext_id,
			   hecfg->he_cap.len,
			   MRVDRV_SIZE_OF_CMD_BUFFER - cmd->size);
		cmd->size += hecfg->he_cap.len + sizeof(MrvlIEtypesHeader_t);
		pos += hecfg->he_cap.len + sizeof(MrvlIEtypesHeader_t);
	}

	cmd->size = wlan_cpu_to_le16(cmd->size);

	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function handles the command response of 11axcfg
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param resp         A pointer to HostCmd_DS_COMMAND
 *  @param pioctl_buf   A pointer to mlan_ioctl_req structure
 *
 *  @return        MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_ret_11ax_cfg(pmlan_private pmpriv,
		  HostCmd_DS_COMMAND *resp, mlan_ioctl_req *pioctl_buf)
{
	pmlan_adapter pmadapter = pmpriv->adapter;
	mlan_ds_11ax_cfg *cfg = MNULL;
	mlan_ds_11ax_he_capa *hecap = MNULL;
	HostCmd_DS_11AX_CFG *axcfg = &resp->params.axcfg;
	MrvlIEtypes_Extension_t *tlv = MNULL;
	t_u16 left_len = 0, tlv_type = 0, tlv_len = 0;

	ENTER();

	if (pioctl_buf == MNULL)
		goto done;

	cfg = (mlan_ds_11ax_cfg *) pioctl_buf->pbuf;
	cfg->param.he_cfg.band = axcfg->band_config;
	hecap = (mlan_ds_11ax_he_capa *) & cfg->param.he_cfg.he_cap;

	/* TLV parse */
	left_len = resp->size - sizeof(HostCmd_DS_11AX_CFG) - S_DS_GEN;
	tlv = (MrvlIEtypes_Extension_t *) axcfg->val;

	while (left_len > sizeof(MrvlIEtypesHeader_t)) {
		tlv_type = wlan_le16_to_cpu(tlv->type);
		tlv_len = wlan_le16_to_cpu(tlv->len);
		if (tlv_type == EXTENSION) {
			switch (tlv->ext_id) {
			case HE_CAPABILITY:
				hecap->id = tlv_type;
				hecap->len = tlv_len;
				memcpy_ext(pmadapter, (t_u8 *)&hecap->ext_id,
					   (t_u8 *)&tlv->ext_id, tlv_len,
					   sizeof(mlan_ds_11ax_he_capa) -
					   sizeof(MrvlIEtypesHeader_t));
				if (cfg->param.he_cfg.band & MBIT(1)) {
					memcpy_ext(pmadapter,
						   (t_u8 *)&pmpriv->user_he_cap,
						   (t_u8 *)tlv,
						   tlv_len +
						   sizeof(MrvlIEtypesHeader_t),
						   sizeof(pmpriv->user_he_cap));
					pmpriv->user_hecap_len = MIN(tlv_len +
								     sizeof
								     (MrvlIEtypesHeader_t),
								     sizeof
								     (pmpriv->
								      user_he_cap));
					PRINTM(MCMND, "user_hecap_len=%d\n",
					       pmpriv->user_hecap_len);
				} else {
					memcpy_ext(pmadapter,
						   (t_u8 *)&pmpriv->
						   user_2g_he_cap, (t_u8 *)tlv,
						   tlv_len +
						   sizeof(MrvlIEtypesHeader_t),
						   sizeof(pmpriv->
							  user_2g_he_cap));
					pmpriv->user_2g_hecap_len =
						MIN(tlv_len +
						    sizeof(MrvlIEtypesHeader_t),
						    sizeof(pmpriv->
							   user_2g_he_cap));
					PRINTM(MCMND, "user_2g_hecap_len=%d\n",
					       pmpriv->user_2g_hecap_len);
				}
				break;
			default:
				break;
			}
		}

		left_len -= (sizeof(MrvlIEtypesHeader_t) + tlv_len);
		tlv = (MrvlIEtypes_Extension_t *) ((t_u8 *)tlv + tlv_len +
						   sizeof(MrvlIEtypesHeader_t));
	}

done:
	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief 11ax command handler
 *
 *  @param pmadapter    A pointer to mlan_adapter structure
 *  @param pioctl_req   A pointer to ioctl request buffer
 *
 *  @return     MLAN_STATUS_SUCCESS --success, otherwise fail
 */
mlan_status
wlan_11ax_ioctl_cmd(pmlan_adapter pmadapter, pmlan_ioctl_req pioctl_req)
{
	mlan_status status = MLAN_STATUS_SUCCESS;
	mlan_ds_11ax_cmd_cfg *cfg = MNULL;
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];
	t_u16 cmd_action = 0;

	ENTER();

	if (pioctl_req->buf_len < sizeof(mlan_ds_11ax_cmd_cfg)) {
		PRINTM(MINFO, "MLAN bss IOCTL length is too short.\n");
		pioctl_req->data_read_written = 0;
		pioctl_req->buf_len_needed = sizeof(mlan_ds_11ax_cmd_cfg);
		pioctl_req->status_code = MLAN_ERROR_INVALID_PARAMETER;
		LEAVE();
		return MLAN_STATUS_RESOURCE;
	}
	cfg = (mlan_ds_11ax_cmd_cfg *) pioctl_req->pbuf;

	if (pioctl_req->action == MLAN_ACT_SET)
		cmd_action = HostCmd_ACT_GEN_SET;
	else
		cmd_action = HostCmd_ACT_GEN_GET;

	/* Send request to firmware */
	status = wlan_prepare_cmd(pmpriv, HostCmd_CMD_11AX_CMD, cmd_action, 0,
				  (t_void *)pioctl_req, (t_void *)cfg);
	if (status == MLAN_STATUS_SUCCESS)
		status = MLAN_STATUS_PENDING;

	LEAVE();
	return status;
}

/**
 *  @brief This function prepares 11ax command
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param cmd      A pointer to HostCmd_DS_COMMAND structure
 *  @param cmd_action   the action: GET or SET
 *  @param pdata_buf    A pointer to data buffer
 *  @return         MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_cmd_11ax_cmd(pmlan_private pmpriv,
		  HostCmd_DS_COMMAND *cmd, t_u16 cmd_action, t_void *pdata_buf)
{
	pmlan_adapter pmadapter = pmpriv->adapter;
	HostCmd_DS_11AX_CMD_CFG *axcmd = &cmd->params.axcmd;
	mlan_ds_11ax_cmd_cfg *ds_11ax_cmd = (mlan_ds_11ax_cmd_cfg *) pdata_buf;
	mlan_ds_11ax_sr_cmd *sr_cmd =
		(mlan_ds_11ax_sr_cmd *) & ds_11ax_cmd->param;
	mlan_ds_11ax_beam_cmd *beam_cmd =
		(mlan_ds_11ax_beam_cmd *) & ds_11ax_cmd->param;
	mlan_ds_11ax_htc_cmd *htc_cmd =
		(mlan_ds_11ax_htc_cmd *) & ds_11ax_cmd->param;
	mlan_ds_11ax_txop_cmd *txop_cmd =
		(mlan_ds_11ax_txop_cmd *) & ds_11ax_cmd->param;
	mlan_ds_11ax_txomi_cmd *txomi_cmd =
		(mlan_ds_11ax_txomi_cmd *) & ds_11ax_cmd->param;
	mlan_ds_11ax_toltime_cmd *toltime_cmd =
		(mlan_ds_11ax_toltime_cmd *) & ds_11ax_cmd->param;
	MrvlIEtypes_Data_t *tlv = MNULL;

	ENTER();
	cmd->command = wlan_cpu_to_le16(HostCmd_CMD_11AX_CMD);
	cmd->size = sizeof(HostCmd_DS_11AX_CMD_CFG) + S_DS_GEN;

	axcmd->action = wlan_cpu_to_le16(cmd_action);
	axcmd->sub_id = wlan_cpu_to_le16(ds_11ax_cmd->sub_id);
	switch (ds_11ax_cmd->sub_id) {
	case MLAN_11AXCMD_SR_SUBID:
		tlv = (MrvlIEtypes_Data_t *)axcmd->val;
		tlv->header.type = wlan_cpu_to_le16(sr_cmd->type);
		tlv->header.len = wlan_cpu_to_le16(sr_cmd->len);
		memcpy_ext(pmadapter, tlv->data,
			   &sr_cmd->param.obss_pd_offset.offset, sr_cmd->len,
			   sr_cmd->len);
		cmd->size += sizeof(MrvlIEtypesHeader_t) + sr_cmd->len;
		break;
	case MLAN_11AXCMD_BEAM_SUBID:
		axcmd->val[0] = beam_cmd->value;
		cmd->size += sizeof(t_u8);
		break;
	case MLAN_11AXCMD_HTC_SUBID:
		axcmd->val[0] = htc_cmd->value;
		cmd->size += sizeof(t_u8);
		break;
	case MLAN_11AXCMD_TXOPRTS_SUBID:
		memcpy_ext(pmadapter, axcmd->val, &txop_cmd->rts_thres,
			   sizeof(t_u16), sizeof(t_u16));
		cmd->size += sizeof(t_u16);
		break;
	case MLAN_11AXCMD_TXOMI_SUBID:
		memcpy_ext(pmadapter, axcmd->val, &txomi_cmd->omi,
			   sizeof(t_u16), sizeof(t_u16));
		cmd->size += sizeof(t_u16);
		break;
	case MLAN_11AXCMD_OBSS_TOLTIME_SUBID:
		memcpy_ext(pmadapter, axcmd->val, &toltime_cmd->tol_time,
			   sizeof(t_u32), sizeof(t_u32));
		cmd->size += sizeof(t_u32);
		break;
	default:
		PRINTM(MERROR, "Unknown subcmd %x\n", ds_11ax_cmd->sub_id);
		break;
	}

	cmd->size = wlan_cpu_to_le16(cmd->size);

	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function handles the command response of 11axcmd
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param resp         A pointer to HostCmd_DS_COMMAND
 *  @param pioctl_buf   A pointer to mlan_ioctl_req structure
 *
 *  @return        MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_ret_11ax_cmd(pmlan_private pmpriv,
		  HostCmd_DS_COMMAND *resp, mlan_ioctl_req *pioctl_buf)
{
	pmlan_adapter pmadapter = pmpriv->adapter;
	mlan_ds_11ax_cmd_cfg *cfg = MNULL;
	HostCmd_DS_11AX_CMD_CFG *axcmd = &resp->params.axcmd;
	MrvlIEtypes_Data_t *tlv = MNULL;
	t_s16 left_len = 0;
	t_u16 tlv_len = 0;

	ENTER();

	if (pioctl_buf == MNULL)
		goto done;

	cfg = (mlan_ds_11ax_cmd_cfg *) pioctl_buf->pbuf;
	cfg->sub_id = wlan_le16_to_cpu(axcmd->sub_id);

	switch (axcmd->sub_id) {
	case MLAN_11AXCMD_SR_SUBID:
		/* TLV parse */
		left_len =
			resp->size - sizeof(HostCmd_DS_11AX_CMD_CFG) - S_DS_GEN;
		// tlv = (MrvlIEtypes_Extension_t *)axcfg->val;
		tlv = (MrvlIEtypes_Data_t *)axcmd->val;
		while (left_len > (t_s16)sizeof(MrvlIEtypesHeader_t)) {
			tlv_len = wlan_le16_to_cpu(tlv->header.len);
			memcpy_ext(pmadapter,
				   cfg->param.sr_cfg.param.obss_pd_offset.
				   offset, tlv->data, tlv_len, tlv_len);
			left_len -= (sizeof(MrvlIEtypesHeader_t) + tlv_len);
			tlv = (MrvlIEtypes_Data_t
			       *)((t_u8 *)tlv + tlv_len +
				  sizeof(MrvlIEtypesHeader_t));
		}
		break;
	case MLAN_11AXCMD_BEAM_SUBID:
		cfg->param.beam_cfg.value = *axcmd->val;
		break;
	case MLAN_11AXCMD_HTC_SUBID:
		cfg->param.htc_cfg.value = *axcmd->val;
		break;
	case MLAN_11AXCMD_TXOPRTS_SUBID:
		memcpy_ext(pmadapter, &cfg->param.txop_cfg.rts_thres,
			   axcmd->val, sizeof(t_u16), sizeof(t_u16));
		break;
	case MLAN_11AXCMD_TXOMI_SUBID:
		memcpy_ext(pmadapter, &cfg->param.txomi_cfg.omi, axcmd->val,
			   sizeof(t_u16), sizeof(t_u16));
		break;
	case MLAN_11AXCMD_OBSS_TOLTIME_SUBID:
		memcpy_ext(pmadapter, &cfg->param.toltime_cfg.tol_time,
			   axcmd->val, sizeof(t_u32), sizeof(t_u32));
		break;
	default:
		PRINTM(MERROR, "Unknown subcmd %x\n", axcmd->sub_id);
		break;
	}

done:
	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief              This function prepares TWT cfg command to configure
 * setup/teardown
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param cmd          A pointer to HostCmd_DS_COMMAND structure
 *  @param cmd_action   The action: GET or SET
 *  @param pdata_buf    A pointer to data buffer
 *  @return             Status returned
 */
mlan_status
wlan_cmd_twt_cfg(pmlan_private pmpriv,
		 HostCmd_DS_COMMAND *cmd, t_u16 cmd_action, t_void *pdata_buf)
{
	pmlan_adapter pmadapter = pmpriv->adapter;
	HostCmd_DS_TWT_CFG *hostcmd_twtcfg =
		(HostCmd_DS_TWT_CFG *) & cmd->params.twtcfg;
	mlan_ds_twtcfg *ds_twtcfg = (mlan_ds_twtcfg *) pdata_buf;
	hostcmd_twt_setup *twt_setup_params = MNULL;
	hostcmd_twt_teardown *twt_teardown_params = MNULL;
	mlan_status ret = MLAN_STATUS_SUCCESS;

	ENTER();
	cmd->command = wlan_cpu_to_le16(HostCmd_CMD_TWT_CFG);

	hostcmd_twtcfg->action = wlan_cpu_to_le16(cmd_action);
	hostcmd_twtcfg->sub_id = wlan_cpu_to_le16(ds_twtcfg->sub_id);

	cmd->size = S_DS_GEN + sizeof(hostcmd_twtcfg->action) +
		sizeof(hostcmd_twtcfg->sub_id);

	switch (hostcmd_twtcfg->sub_id) {
	case MLAN_11AX_TWT_SETUP_SUBID:
		twt_setup_params = &hostcmd_twtcfg->param.twt_setup;
		memset(pmadapter, twt_setup_params, 0x00,
		       sizeof(hostcmd_twtcfg->param.twt_setup));
		twt_setup_params->implicit =
			ds_twtcfg->param.twt_setup.implicit;
		twt_setup_params->announced =
			ds_twtcfg->param.twt_setup.announced;
		twt_setup_params->trigger_enabled =
			ds_twtcfg->param.twt_setup.trigger_enabled;
		twt_setup_params->twt_info_disabled =
			ds_twtcfg->param.twt_setup.twt_info_disabled;
		twt_setup_params->negotiation_type =
			ds_twtcfg->param.twt_setup.negotiation_type;
		twt_setup_params->twt_wakeup_duration =
			ds_twtcfg->param.twt_setup.twt_wakeup_duration;
		twt_setup_params->flow_identifier =
			ds_twtcfg->param.twt_setup.flow_identifier;
		twt_setup_params->hard_constraint =
			ds_twtcfg->param.twt_setup.hard_constraint;
		twt_setup_params->twt_exponent =
			ds_twtcfg->param.twt_setup.twt_exponent;
		twt_setup_params->twt_mantissa =
			wlan_cpu_to_le16(ds_twtcfg->param.twt_setup.
					 twt_mantissa);
		twt_setup_params->twt_request =
			ds_twtcfg->param.twt_setup.twt_request;
		cmd->size += sizeof(hostcmd_twtcfg->param.twt_setup);
		break;
	case MLAN_11AX_TWT_TEARDOWN_SUBID:
		twt_teardown_params = &hostcmd_twtcfg->param.twt_teardown;
		memset(pmadapter, twt_teardown_params, 0x00,
		       sizeof(hostcmd_twtcfg->param.twt_teardown));
		twt_teardown_params->flow_identifier =
			ds_twtcfg->param.twt_teardown.flow_identifier;
		twt_teardown_params->negotiation_type =
			ds_twtcfg->param.twt_teardown.negotiation_type;
		twt_teardown_params->teardown_all_twt =
			ds_twtcfg->param.twt_teardown.teardown_all_twt;
		cmd->size += sizeof(hostcmd_twtcfg->param.twt_teardown);
		break;
	default:
		PRINTM(MERROR, "Unknown subcmd %x\n", ds_twtcfg->sub_id);
		ret = MLAN_STATUS_FAILURE;
		break;
	}

	cmd->size = wlan_cpu_to_le16(cmd->size);

	LEAVE();
	return ret;
}

/**
 *  @brief              TWT config command handler
 *
 *  @param pmadapter    A pointer to mlan_adapter structure
 *  @param pioctl_req   A pointer to ioctl request buffer
 *
 *  @return             MLAN_STATUS_SUCCESS --success, otherwise fail
 */
mlan_status
wlan_11ax_ioctl_twtcfg(pmlan_adapter pmadapter, pmlan_ioctl_req pioctl_req)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_ds_twtcfg *cfg = MNULL;
	t_u16 cmd_action = 0;

	ENTER();

	if (pioctl_req->buf_len < sizeof(mlan_ds_twtcfg)) {
		PRINTM(MERROR, "MLAN bss IOCTL length is too short.\n");
		pioctl_req->data_read_written = 0;
		pioctl_req->buf_len_needed = sizeof(mlan_ds_twtcfg);
		pioctl_req->status_code = MLAN_ERROR_INVALID_PARAMETER;
		LEAVE();
		return MLAN_STATUS_RESOURCE;
	}

	cfg = (mlan_ds_twtcfg *) pioctl_req->pbuf;

	if (pioctl_req->action == MLAN_ACT_SET)
		cmd_action = HostCmd_ACT_GEN_SET;
	else
		cmd_action = HostCmd_ACT_GEN_GET;

	/* Send request to firmware */
	ret = wlan_prepare_cmd(pmpriv, HostCmd_CMD_TWT_CFG, cmd_action, 0,
			       (t_void *)pioctl_req, (t_void *)cfg);
	if (ret == MLAN_STATUS_SUCCESS)
		ret = MLAN_STATUS_PENDING;

	LEAVE();
	return ret;
}
