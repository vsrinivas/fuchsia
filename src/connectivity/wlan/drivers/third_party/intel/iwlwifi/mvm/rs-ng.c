/******************************************************************************
 *
 * Copyright(c) 2012 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2013 - 2015 Intel Mobile Communications GmbH
 * Copyright(c) 2016 - 2017 Intel Deutschland GmbH
 * Copyright(c) 2018        Intel Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  * Neither the name Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *****************************************************************************/
#include "mvm.h"
#include "rs.h"

static void iwl_start_agg(struct iwl_mvm *mvm, struct ieee80211_sta *sta,
			  int tid)
{
	struct iwl_mvm_sta *mvmsta = iwl_mvm_sta_from_mac80211(sta);
	struct iwl_mvm_tid_data *tid_data;

	tid_data = &mvmsta->tid_data[tid];
	if (tid_data->state == IWL_AGG_OFF &&
	    mvmsta->sta_state >= IEEE80211_STA_AUTHORIZED) {
		int ret = ieee80211_start_tx_ba_session(sta, tid, 0);
		if (ret == -EAGAIN) {
			ieee80211_stop_tx_ba_session(sta, tid);
			return;
		}
		if (ret == 0)
			tid_data->state = IWL_AGG_QUEUED;
	}
}

static u8 rs_fw_bw_from_sta_bw(struct ieee80211_sta *sta)
{
	switch (sta->bandwidth) {
	case IEEE80211_STA_RX_BW_160:
		return IWL_TLC_MNG_CH_WIDTH_160MHZ;
	case IEEE80211_STA_RX_BW_80:
		return IWL_TLC_MNG_CH_WIDTH_80MHZ;
	case IEEE80211_STA_RX_BW_40:
		return IWL_TLC_MNG_CH_WIDTH_40MHZ;
	case IEEE80211_STA_RX_BW_20:
	default:
		return IWL_TLC_MNG_CH_WIDTH_20MHZ;
	}
}

static u8 rs_fw_set_active_chains(u8 chains)
{
	u8 fw_chains = 0;

	if (chains & ANT_A)
		fw_chains |= IWL_TLC_MNG_CHAIN_A_MSK;
	if (chains & ANT_B)
		fw_chains |= IWL_TLC_MNG_CHAIN_B_MSK;
	if (chains & ANT_C)
		WARN(false,
		     "tlc doesn't support antenna C. chains: 0x%x\n",
		     chains);

	return fw_chains;
}

static u8 rs_fw_sgi_cw_support(struct ieee80211_sta *sta)
{
	struct ieee80211_sta_ht_cap *ht_cap = &sta->ht_cap;
	struct ieee80211_sta_vht_cap *vht_cap = &sta->vht_cap;
	struct ieee80211_sta_he_cap *he_cap = &sta->he_cap;
	u8 supp = 0;

	if (he_cap && he_cap->has_he)
		return 0;

	if (ht_cap->cap & IEEE80211_HT_CAP_SGI_20)
		supp |= BIT(IWL_TLC_MNG_CH_WIDTH_20MHZ);
	if (ht_cap->cap & IEEE80211_HT_CAP_SGI_40)
		supp |= BIT(IWL_TLC_MNG_CH_WIDTH_40MHZ);
	if (vht_cap->cap & IEEE80211_VHT_CAP_SHORT_GI_80)
		supp |= BIT(IWL_TLC_MNG_CH_WIDTH_80MHZ);
	if (vht_cap->cap & IEEE80211_VHT_CAP_SHORT_GI_160)
		supp |= BIT(IWL_TLC_MNG_CH_WIDTH_160MHZ);

	return supp;
}

static u16 rs_fw_set_config_flags(struct iwl_mvm *mvm,
				  struct ieee80211_sta *sta)
{
	struct ieee80211_sta_ht_cap *ht_cap = &sta->ht_cap;
	struct ieee80211_sta_vht_cap *vht_cap = &sta->vht_cap;
	struct ieee80211_sta_he_cap *he_cap = &sta->he_cap;
	bool vht_ena = vht_cap && vht_cap->vht_supported;
	u16 flags = 0;

	if (mvm->cfg->ht_params->stbc &&
	    (num_of_ant(iwl_mvm_get_valid_tx_ant(mvm)) > 1)) {
		if (he_cap && he_cap->has_he) {
			if (he_cap->he_cap_elem.phy_cap_info[2] &
			    IEEE80211_HE_PHY_CAP2_STBC_RX_UNDER_80MHZ)
				flags |= IWL_TLC_MNG_CFG_FLAGS_STBC_MSK;

			if (he_cap->he_cap_elem.phy_cap_info[7] &
			    IEEE80211_HE_PHY_CAP7_STBC_RX_ABOVE_80MHZ)
				flags |= IWL_TLC_MNG_CFG_FLAGS_HE_STBC_160MHZ_MSK;
		} else if ((ht_cap &&
			    (ht_cap->cap & IEEE80211_HT_CAP_RX_STBC)) ||
			   (vht_ena &&
			    (vht_cap->cap & IEEE80211_VHT_CAP_RXSTBC_MASK)))
			flags |= IWL_TLC_MNG_CFG_FLAGS_STBC_MSK;
	}

	if (mvm->cfg->ht_params->ldpc &&
	    ((ht_cap && (ht_cap->cap & IEEE80211_HT_CAP_LDPC_CODING)) ||
	     (vht_ena && (vht_cap->cap & IEEE80211_VHT_CAP_RXLDPC))))
		flags |= IWL_TLC_MNG_CFG_FLAGS_LDPC_MSK;

	if (he_cap && he_cap->has_he &&
	    (he_cap->he_cap_elem.phy_cap_info[3] &
	     IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_RX_MASK))
		flags |= IWL_TLC_MNG_CFG_FLAGS_HE_DCM_NSS_1_MSK;

	return flags;
}

static
int rs_fw_vht_highest_rx_mcs_index(const struct ieee80211_sta_vht_cap *vht_cap,
				   int nss)
{
	u16 rx_mcs = le16_to_cpu(vht_cap->vht_mcs.rx_mcs_map) &
		(0x3 << (2 * (nss - 1)));
	rx_mcs >>= (2 * (nss - 1));

	switch (rx_mcs) {
	case IEEE80211_VHT_MCS_SUPPORT_0_7:
		return IWL_TLC_MNG_HT_RATE_MCS7;
	case IEEE80211_VHT_MCS_SUPPORT_0_8:
		return IWL_TLC_MNG_HT_RATE_MCS8;
	case IEEE80211_VHT_MCS_SUPPORT_0_9:
		return IWL_TLC_MNG_HT_RATE_MCS9;
	default:
		WARN_ON_ONCE(1);
		break;
	}

	return 0;
}

static void
rs_fw_vht_set_enabled_rates(const struct ieee80211_sta *sta,
			    const struct ieee80211_sta_vht_cap *vht_cap,
			    TLC_MNG_CONFIG_PARAMS_CMD_API_S *cmd)
{
	u16 supp;
	int i, highest_mcs;

	for (i = 0; i < sta->rx_nss; i++) {
		if (i == MAX_NSS)
			break;

		highest_mcs = rs_fw_vht_highest_rx_mcs_index(vht_cap, i + 1);
		if (!highest_mcs)
			continue;

		supp = BIT(highest_mcs + 1) - 1;
		if (sta->bandwidth == IEEE80211_STA_RX_BW_20)
			supp &= ~BIT(IWL_TLC_MNG_HT_RATE_MCS9);

		cmd->mcs[i][0] = (supp);
		if (sta->bandwidth == IEEE80211_STA_RX_BW_160)
			cmd->mcs[i][1] = cmd->mcs[i][0];
	}
}

static u16 rs_fw_he_ieee80211_mcs_to_rs_mcs(u16 mcs)
{
	switch (mcs) {
	case IEEE80211_HE_MCS_SUPPORT_0_7:
		return BIT(IWL_TLC_MNG_HT_RATE_MCS7 + 1) - 1;
	case IEEE80211_HE_MCS_SUPPORT_0_9:
		return BIT(IWL_TLC_MNG_HT_RATE_MCS9 + 1) - 1;
	case IEEE80211_HE_MCS_SUPPORT_0_11:
		return BIT(IWL_TLC_MNG_HT_RATE_MCS11 + 1) - 1;
	case IEEE80211_HE_MCS_NOT_SUPPORTED:
		return 0;
	}

	WARN(1, "invalid HE MCS %d\n", mcs);
	return 0;
}

static void
rs_fw_he_set_enabled_rates(const struct ieee80211_sta *sta,
			   const struct ieee80211_sta_he_cap *he_cap,
			   TLC_MNG_CONFIG_PARAMS_CMD_API_S *cmd)
{
	u16 mcs_160 = le16_to_cpu(he_cap->he_mcs_nss_supp.rx_mcs_160);
	u16 mcs_80 = le16_to_cpu(he_cap->he_mcs_nss_supp.rx_mcs_80);
	int i;

	for (i = 0; i < sta->rx_nss && i < MAX_NSS; i++) {
		u16 _mcs_160 = (mcs_160 >> (2 * i)) & 0x3;
		u16 _mcs_80 = (mcs_80 >> (2 * i)) & 0x3;

		cmd->mcs[i][0] =
			(rs_fw_he_ieee80211_mcs_to_rs_mcs(_mcs_80));
		cmd->mcs[i][1] =
			(rs_fw_he_ieee80211_mcs_to_rs_mcs(_mcs_160));
	}
}

static void rs_fw_set_supp_rates(struct ieee80211_sta *sta,
				 struct ieee80211_supported_band *sband,
				 TLC_MNG_CONFIG_PARAMS_CMD_API_S *cmd)
{
	int i;
	unsigned long tmp;
	unsigned long supp;
	const struct ieee80211_sta_ht_cap *ht_cap = &sta->ht_cap;
	const struct ieee80211_sta_vht_cap *vht_cap = &sta->vht_cap;
	const struct ieee80211_sta_he_cap *he_cap = &sta->he_cap;

	/* non HT rates */
	supp = 0;
	tmp = sta->supp_rates[sband->band];
	for_each_set_bit(i, &tmp, BITS_PER_LONG)
		supp |= BIT(sband->bitrates[i].hw_value);

	cmd->nonHt = supp;
	cmd->bestSuppMode = IWL_TLC_MNG_MODE_NON_HT;

	/* HT/VHT rates */
	if (he_cap && he_cap->has_he) {
		cmd->bestSuppMode = IWL_TLC_MNG_MODE_HE;
		rs_fw_he_set_enabled_rates(sta, he_cap, cmd);
	} else if (vht_cap && vht_cap->vht_supported) {
		cmd->bestSuppMode = IWL_TLC_MNG_MODE_VHT;
		rs_fw_vht_set_enabled_rates(sta, vht_cap, cmd);
	} else if (ht_cap && ht_cap->ht_supported) {
		cmd->bestSuppMode = IWL_TLC_MNG_MODE_HT;
		cmd->mcs[0][0] = (ht_cap->mcs.rx_mask[0]);
		cmd->mcs[1][0] = (ht_cap->mcs.rx_mask[1]);
	}
}


/// TODO: merge file?
#include "rateScaleMng.c"


static void rs_drv_rate_init(struct iwl_mvm *mvm, struct ieee80211_sta *sta,
			     enum nl80211_band band, bool update)
{
	struct ieee80211_hw *hw = mvm->hw;
	struct iwl_mvm_sta *mvmsta = iwl_mvm_sta_from_mac80211(sta);
	struct iwl_lq_sta *lq_sta = &mvmsta->lq_sta.rs_drv;
	struct ieee80211_supported_band *sband;
	RS_MNG_STA_INFO_S *staInfo = &lq_sta->pers;
	TLC_MNG_CONFIG_PARAMS_CMD_API_S config = {};

#ifdef CPTCFG_IWLWIFI_DEBUGFS
	iwl_mvm_reset_frame_stats(mvm);
#endif
	sband = hw->wiphy->bands[band];

	mvmsta->amsdu_enabled = 0;
	mvmsta->max_amsdu_len = sta->max_amsdu_len;

	config.maxChWidth = update ?
		rs_fw_bw_from_sta_bw(sta) : IWL_TLC_MNG_CH_WIDTH_20MHZ;
	config.configFlags = rs_fw_set_config_flags(mvm, sta);
	config.chainsEnabled =
		rs_fw_set_active_chains(iwl_mvm_get_valid_tx_ant(mvm));
	config.maxMpduLen = sta->max_amsdu_len;
	config.sgiChWidthSupport = rs_fw_sgi_cw_support(sta);
	config.amsduSupported = iwl_mvm_is_csum_supported(mvm);
	config.band = sband->band;
	rs_fw_set_supp_rates(sta, sband, &config);

	cmdHandlerTlcMngConfig(mvm, sta, mvmsta, staInfo, &config, update);
}

void iwl_mvm_rs_rate_init(struct iwl_mvm *mvm, struct ieee80211_sta *sta,
			  enum nl80211_band band, bool update)
{
	if (iwl_mvm_has_tlc_offload(mvm))
		rs_fw_rate_init(mvm, sta, band, update);
	else
		rs_drv_rate_init(mvm, sta, band, update);
}

void iwl_mvm_rs_tx_status(struct iwl_mvm *mvm, struct ieee80211_sta *sta,
			  int tid, struct ieee80211_tx_info *info,
			  bool is_ndp)
{
	struct iwl_mvm_sta *mvmsta = iwl_mvm_sta_from_mac80211(sta);
	struct iwl_lq_sta *lq_sta = &mvmsta->lq_sta.rs_drv;
	RS_MNG_STA_INFO_S *staInfo = &lq_sta->pers;
	TLC_STAT_COMMON_API_S stats;
	int failures = info->status.rates[0].count - 1;
	bool acked = !!(info->flags & IEEE80211_TX_STAT_ACK);

	if ((info->flags & IEEE80211_TX_CTL_AMPDU) &&
	    !(info->flags & IEEE80211_TX_STAT_AMPDU))
		return;

	if (info->flags & IEEE80211_TX_STAT_AMPDU) {
		stats.baTxed = info->status.ampdu_len;
		stats.baAcked = info->status.ampdu_ack_len;
		stats.trafficLoad = stats.baTxed;
		stats.txed[0] = stats.baTxed;
		stats.txed[1] = 0;
		stats.acked[0] = stats.baAcked;
		stats.acked[1] = 0;
	} else {
		stats.baTxed = 0;
		stats.baAcked = 0;
		stats.trafficLoad = 0;
		stats.txed[0] = 1;
		stats.txed[1] = !!failures;
		stats.acked[0] = !failures && acked;
		stats.acked[1] = !!failures && acked;
	}

	tlcStatUpdateHandler(staInfo, &stats, mvm, sta, tid, is_ndp);
}

#ifdef CPTCFG_IWLWIFI_DEBUGFS
void iwl_mvm_reset_frame_stats(struct iwl_mvm *mvm)
{
}

void iwl_mvm_update_frame_stats(struct iwl_mvm *mvm, u32 rate, bool agg)
{
}

/* TODO */
int rs_pretty_print_rate(char *buf, int bufsz, const u32 rate)
{
	return 0;
}
#endif


static void *rs_alloc(struct ieee80211_hw *hw, struct dentry *debugfsdir)
{
	return IWL_MAC80211_GET_MVM(hw);
}

static void rs_free(void *priv)
{
}

static void *rs_alloc_sta(void *priv, struct ieee80211_sta *sta, gfp_t gfp)
{
	struct iwl_mvm *mvm = priv;
	struct iwl_mvm_sta *mvmsta = iwl_mvm_sta_from_mac80211(sta);
	struct iwl_lq_sta *lq_sta = &mvmsta->lq_sta.rs_drv;
	RS_MNG_STA_INFO_S *staInfo = &lq_sta->pers;
	void *priv_sta = lq_sta;

	rsMngResetStaInfo(mvm, sta, mvmsta, staInfo, false);
	return priv_sta;
}

static void rs_rate_init(void *priv, struct ieee80211_supported_band *sband,
			 struct cfg80211_chan_def *chandef,
			 struct ieee80211_sta *sta, void *priv_sta)
{
}

static void rs_rate_update(void *priv, struct ieee80211_supported_band *sband,
			   struct cfg80211_chan_def *chandef,
			   struct ieee80211_sta *sta, void *priv_sta,
			   u32 changed)
{
	struct iwl_mvm *mvm = priv;
	struct iwl_mvm_sta *mvmsta = iwl_mvm_sta_from_mac80211(sta);
	int tid;

	if (!mvmsta->vif)
		return;

	for (tid = 0; tid < IWL_MAX_TID_COUNT; tid++)
		ieee80211_stop_tx_ba_session(sta, tid);

	iwl_mvm_rs_rate_init(mvm, sta, sband->band, true);
}

static void rs_free_sta(void *priv, struct ieee80211_sta *sta,
			void *priv_sta)
{
}

static inline u8 rs_get_tid(struct ieee80211_hdr *hdr)
{
	int tid = IWL_MAX_TID_COUNT;

	if (ieee80211_is_data_qos(hdr->frame_control))
		tid = ieee80211_get_tid(hdr);

	return tid;
}

static void rs_tx_status(void *priv, struct ieee80211_supported_band *sband,
			 struct ieee80211_sta *sta, void *priv_sta,
			 struct sk_buff *skb)
{
	struct iwl_mvm *mvm = priv;
	struct iwl_mvm_sta *mvmsta = iwl_mvm_sta_from_mac80211(sta);
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct ieee80211_hdr *hdr = (void *)skb->data;

	if (!mvmsta->vif)
		return;

	if (!ieee80211_is_data(hdr->frame_control) ||
	    (info->flags & IEEE80211_TX_CTL_NO_ACK))
		return;

	iwl_mvm_rs_tx_status(mvm, sta, rs_get_tid(hdr), info,
			     ieee80211_is_qos_nullfunc(hdr->frame_control));
}

static void rs_get_rate(void *priv, struct ieee80211_sta *sta, void *priv_sta,
			struct ieee80211_tx_rate_control *txrc)
{
	struct iwl_mvm_sta *mvmsta = NULL;
	u32 hwrate;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(txrc->skb);

	if (sta) {
		mvmsta = iwl_mvm_sta_from_mac80211(sta);
		if (!mvmsta->vif) {
			sta = NULL;
			mvmsta = NULL;
		}
	}

	if (rate_control_send_low(sta, mvmsta, txrc))
		return;

	if (!mvmsta)
		return;

	hwrate = le32_to_cpu(mvmsta->lq_sta.rs_drv.lq.rs_table[0]);
	iwl_mvm_hwrate_to_tx_rate(hwrate, info->band, &info->control.rates[0]);
	info->control.rates[0].count = 1;
}

static const struct rate_control_ops rs_ops = {
	.name = RS_NAME,
	.alloc = rs_alloc,
	.free = rs_free,
	.alloc_sta = rs_alloc_sta,
	.rate_init = rs_rate_init,
	.rate_update = rs_rate_update,
	.free_sta = rs_free_sta,
	.tx_status = rs_tx_status,
	.get_rate = rs_get_rate,
};

int iwl_mvm_rate_control_register(void)
{
	return ieee80211_rate_control_register(&rs_ops);
}

void iwl_mvm_rate_control_unregister(void)
{
	ieee80211_rate_control_unregister(&rs_ops);
}

static int rs_drv_tx_protection(struct iwl_mvm *mvm,
				struct iwl_mvm_sta *mvmsta,
				bool enable)
{
	if (enable)
		mvmsta->tx_protection++;
	else
		mvmsta->tx_protection--;

	if (mvmsta->tx_protection)
		mvmsta->lq_sta.rs_drv.lq.flags |= LQ_FLAG_USE_RTS_MSK;
	else
		mvmsta->lq_sta.rs_drv.lq.flags &= ~LQ_FLAG_USE_RTS_MSK;

	return iwl_mvm_send_lq_cmd(mvm, &mvmsta->lq_sta.rs_drv.lq, false);
}

int iwl_mvm_tx_protection(struct iwl_mvm *mvm, struct iwl_mvm_sta *mvmsta,
			  bool enable)
{
	if (iwl_mvm_has_tlc_offload(mvm))
		return rs_fw_tx_protection(mvm, mvmsta, enable);
	else
		return rs_drv_tx_protection(mvm, mvmsta, enable);
}

void rs_update_last_rssi(struct iwl_mvm *mvm,
			 struct iwl_mvm_sta *mvmsta,
			 struct ieee80211_rx_status *rx_status)
{
}
