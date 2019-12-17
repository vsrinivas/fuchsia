/******************************************************************************
 *
 * Copyright(c) 2013 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2013 - 2015 Intel Mobile Communications GmbH
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

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/fw/api/coex.h"

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/iwl-debug.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/iwl-modparams.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/mvm/mvm.h"

#ifdef CPTCFG_IWLWIFI_LTE_COEX
#include "lte-coex.h"
#endif

#if 0   // NEEDS_PORTING
/* 20MHz / 40MHz below / 40Mhz above*/
static const __le64 iwl_ci_mask[][3] = {
    /* dummy entry for channel 0 */
    {cpu_to_le64(0), cpu_to_le64(0), cpu_to_le64(0)},
    {
        cpu_to_le64(0x0000001FFFULL),
        cpu_to_le64(0x0ULL),
        cpu_to_le64(0x00007FFFFFULL),
    },
    {
        cpu_to_le64(0x000000FFFFULL),
        cpu_to_le64(0x0ULL),
        cpu_to_le64(0x0003FFFFFFULL),
    },
    {
        cpu_to_le64(0x000003FFFCULL),
        cpu_to_le64(0x0ULL),
        cpu_to_le64(0x000FFFFFFCULL),
    },
    {
        cpu_to_le64(0x00001FFFE0ULL),
        cpu_to_le64(0x0ULL),
        cpu_to_le64(0x007FFFFFE0ULL),
    },
    {
        cpu_to_le64(0x00007FFF80ULL),
        cpu_to_le64(0x00007FFFFFULL),
        cpu_to_le64(0x01FFFFFF80ULL),
    },
    {
        cpu_to_le64(0x0003FFFC00ULL),
        cpu_to_le64(0x0003FFFFFFULL),
        cpu_to_le64(0x0FFFFFFC00ULL),
    },
    {
        cpu_to_le64(0x000FFFF000ULL),
        cpu_to_le64(0x000FFFFFFCULL),
        cpu_to_le64(0x3FFFFFF000ULL),
    },
    {
        cpu_to_le64(0x007FFF8000ULL),
        cpu_to_le64(0x007FFFFFE0ULL),
        cpu_to_le64(0xFFFFFF8000ULL),
    },
    {
        cpu_to_le64(0x01FFFE0000ULL),
        cpu_to_le64(0x01FFFFFF80ULL),
        cpu_to_le64(0xFFFFFE0000ULL),
    },
    {
        cpu_to_le64(0x0FFFF00000ULL),
        cpu_to_le64(0x0FFFFFFC00ULL),
        cpu_to_le64(0x0ULL),
    },
    {cpu_to_le64(0x3FFFC00000ULL), cpu_to_le64(0x3FFFFFF000ULL), cpu_to_le64(0x0)},
    {cpu_to_le64(0xFFFE000000ULL), cpu_to_le64(0xFFFFFF8000ULL), cpu_to_le64(0x0)},
    {cpu_to_le64(0xFFF8000000ULL), cpu_to_le64(0xFFFFFE0000ULL), cpu_to_le64(0x0)},
    {cpu_to_le64(0xFE00000000ULL), cpu_to_le64(0x0ULL), cpu_to_le64(0x0ULL)},
};

static enum iwl_bt_coex_lut_type iwl_get_coex_type(struct iwl_mvm* mvm,
                                                   const struct ieee80211_vif* vif) {
  struct ieee80211_chanctx_conf* chanctx_conf;
  enum iwl_bt_coex_lut_type ret;
  uint16_t phy_ctx_id;
  uint32_t primary_ch_phy_id, secondary_ch_phy_id;

  /*
   * Checking that we hold mvm->mutex is a good idea, but the rate
   * control can't acquire the mutex since it runs in Tx path.
   * So this is racy in that case, but in the worst case, the AMPDU
   * size limit will be wrong for a short time which is not a big
   * issue.
   */

  rcu_read_lock();

  chanctx_conf = rcu_dereference(vif->chanctx_conf);

  if (!chanctx_conf || chanctx_conf->def.chan->band != NL80211_BAND_2GHZ) {
    rcu_read_unlock();
    return BT_COEX_INVALID_LUT;
  }

  ret = BT_COEX_TX_DIS_LUT;

  if (mvm->cfg->bt_shared_single_ant) {
    rcu_read_unlock();
    return ret;
  }

  phy_ctx_id = *((uint16_t*)chanctx_conf->drv_priv);
  primary_ch_phy_id = le32_to_cpu(mvm->last_bt_ci_cmd.primary_ch_phy_id);
  secondary_ch_phy_id = le32_to_cpu(mvm->last_bt_ci_cmd.secondary_ch_phy_id);

  if (primary_ch_phy_id == phy_ctx_id) {
    ret = le32_to_cpu(mvm->last_bt_notif.primary_ch_lut);
  } else if (secondary_ch_phy_id == phy_ctx_id) {
    ret = le32_to_cpu(mvm->last_bt_notif.secondary_ch_lut);
  }
  /* else - default = TX TX disallowed */

  rcu_read_unlock();

  return ret;
}
#endif  // NEEDS_PORTING

zx_status_t iwl_mvm_send_bt_init_conf(struct iwl_mvm* mvm) {
  struct iwl_bt_coex_cmd bt_cmd = {};
  uint32_t mode;

  lockdep_assert_held(&mvm->mutex);

  if (unlikely(mvm->bt_force_ant_mode != BT_FORCE_ANT_DIS)) {
    switch (mvm->bt_force_ant_mode) {
      case BT_FORCE_ANT_BT:
        mode = BT_COEX_BT;
        break;
      case BT_FORCE_ANT_WIFI:
        mode = BT_COEX_WIFI;
        break;
      default:
        WARN_ON(1);
        mode = 0;
    }

    bt_cmd.mode = cpu_to_le32(mode);
    goto send_cmd;
  }

  mode = iwlwifi_mod_params.bt_coex_active ? BT_COEX_NW : BT_COEX_DISABLE;
  bt_cmd.mode = cpu_to_le32(mode);

  if (IWL_MVM_BT_COEX_SYNC2SCO) {
    bt_cmd.enabled_modules |= cpu_to_le32(BT_COEX_SYNC2SCO_ENABLED);
  }

  if (iwl_mvm_is_mplut_supported(mvm)) {
    bt_cmd.enabled_modules |= cpu_to_le32(BT_COEX_MPLUT_ENABLED);
  }

  bt_cmd.enabled_modules |= cpu_to_le32(BT_COEX_HIGH_BAND_RET);

send_cmd:
  memset(&mvm->last_bt_notif, 0, sizeof(mvm->last_bt_notif));
  memset(&mvm->last_bt_ci_cmd, 0, sizeof(mvm->last_bt_ci_cmd));

  return iwl_mvm_send_cmd_pdu(mvm, BT_CONFIG, 0, sizeof(bt_cmd), &bt_cmd);
}

#if 0  // NEEDS_PORTING
static int iwl_mvm_bt_coex_reduced_txp(struct iwl_mvm* mvm, uint8_t sta_id, bool enable) {
  struct iwl_bt_coex_reduced_txp_update_cmd cmd = {};
  struct iwl_mvm_sta* mvmsta;
  uint32_t value;
  int ret;

  mvmsta = iwl_mvm_sta_from_staid_protected(mvm, sta_id);
  if (!mvmsta) {
    return 0;
  }

  /* nothing to do */
  if (mvmsta->bt_reduced_txpower == enable) {
    return 0;
  }

  value = mvmsta->sta_id;

  if (enable) {
    value |= BT_REDUCED_TX_POWER_BIT;
  }

  IWL_DEBUG_COEX(mvm, "%sable reduced Tx Power for sta %d\n", enable ? "en" : "dis", sta_id);

  cmd.reduced_txp = cpu_to_le32(value);
  mvmsta->bt_reduced_txpower = enable;

  ret = iwl_mvm_send_cmd_pdu(mvm, BT_COEX_UPDATE_REDUCED_TXP, CMD_ASYNC, sizeof(cmd), &cmd);

  return ret;
}

struct iwl_bt_iterator_data {
  struct iwl_bt_coex_profile_notif* notif;
  struct iwl_mvm* mvm;
  struct ieee80211_chanctx_conf* primary;
  struct ieee80211_chanctx_conf* secondary;
  bool primary_ll;
  uint8_t primary_load;
  uint8_t secondary_load;
};

static inline void iwl_mvm_bt_coex_enable_rssi_event(struct iwl_mvm* mvm, struct ieee80211_vif* vif,
                                                     bool enable, int rssi) {
  struct iwl_mvm_vif* mvmvif = iwl_mvm_vif_from_mac80211(vif);

  mvmvif->bf_data.last_bt_coex_event = rssi;
  mvmvif->bf_data.bt_coex_max_thold = enable ? -IWL_MVM_BT_COEX_EN_RED_TXP_THRESH : 0;
  mvmvif->bf_data.bt_coex_min_thold = enable ? -IWL_MVM_BT_COEX_DIS_RED_TXP_THRESH : 0;
}

#define MVM_COEX_TCM_PERIOD (HZ * 10)

static void iwl_mvm_bt_coex_tcm_based_ci(struct iwl_mvm* mvm, struct iwl_bt_iterator_data* data) {
  unsigned long now = jiffies;

  if (!time_after(now, mvm->bt_coex_last_tcm_ts + MVM_COEX_TCM_PERIOD)) {
    return;
  }

  mvm->bt_coex_last_tcm_ts = now;

  /* We assume here that we don't have more than 2 vifs on 2.4GHz */

  /* if the primary is low latency, it will stay primary */
  if (data->primary_ll) {
    return;
  }

  if (data->primary_load >= data->secondary_load) {
    return;
  }

  swap(data->primary, data->secondary);
}

/* must be called under rcu_read_lock */
static void iwl_mvm_bt_notif_iterator(void* _data, uint8_t* mac, struct ieee80211_vif* vif) {
  struct iwl_mvm_vif* mvmvif = iwl_mvm_vif_from_mac80211(vif);
  struct iwl_bt_iterator_data* data = _data;
  struct iwl_mvm* mvm = data->mvm;
  struct ieee80211_chanctx_conf* chanctx_conf;
  /* default smps_mode is AUTOMATIC - only used for client modes */
  enum ieee80211_smps_mode smps_mode = IEEE80211_SMPS_AUTOMATIC;
  uint32_t bt_activity_grading, min_ag_for_static_smps;
  int ave_rssi;

  lockdep_assert_held(&mvm->mutex);

  switch (vif->type) {
    case NL80211_IFTYPE_STATION:
      break;
    case NL80211_IFTYPE_AP:
      if (!mvmvif->ap_ibss_active) {
        return;
      }
      break;
    default:
      return;
  }

  chanctx_conf = rcu_dereference(vif->chanctx_conf);

  /* If channel context is invalid or not on 2.4GHz .. */
  if ((!chanctx_conf || chanctx_conf->def.chan->band != NL80211_BAND_2GHZ)) {
    if (vif->type == NL80211_IFTYPE_STATION) {
      /* ... relax constraints and disable rssi events */
      iwl_mvm_update_smps(mvm, vif, IWL_MVM_SMPS_REQ_BT_COEX, smps_mode);
      iwl_mvm_bt_coex_reduced_txp(mvm, mvmvif->ap_sta_id, false);
      iwl_mvm_bt_coex_enable_rssi_event(mvm, vif, false, 0);
    }
    return;
  }

  if (fw_has_capa(&mvm->fw->ucode_capa, IWL_UCODE_TLV_CAPA_COEX_SCHEMA_2)) {
    min_ag_for_static_smps = BT_VERY_HIGH_TRAFFIC;
  } else {
    min_ag_for_static_smps = BT_HIGH_TRAFFIC;
  }

  bt_activity_grading = le32_to_cpu(data->notif->bt_activity_grading);
  if (bt_activity_grading >= min_ag_for_static_smps) {
    smps_mode = IEEE80211_SMPS_STATIC;
  } else if (bt_activity_grading >= BT_LOW_TRAFFIC) {
    smps_mode = IEEE80211_SMPS_DYNAMIC;
  }

  /* relax SMPS constraints for next association */
  if (!vif->bss_conf.assoc) {
    smps_mode = IEEE80211_SMPS_AUTOMATIC;
  }

  if (mvmvif->phy_ctxt && (mvm->last_bt_notif.rrc_status & BIT(mvmvif->phy_ctxt->id))) {
    smps_mode = IEEE80211_SMPS_AUTOMATIC;
  }

  IWL_DEBUG_COEX(data->mvm, "mac %d: bt_activity_grading %d smps_req %d\n", mvmvif->id,
                 bt_activity_grading, smps_mode);

  if (vif->type == NL80211_IFTYPE_STATION) {
    iwl_mvm_update_smps(mvm, vif, IWL_MVM_SMPS_REQ_BT_COEX, smps_mode);
  }

  /* low latency is always primary */
  if (iwl_mvm_vif_low_latency(mvmvif)) {
    data->primary_ll = true;

    data->secondary = data->primary;
    data->primary = chanctx_conf;
  }

  if (vif->type == NL80211_IFTYPE_AP) {
    if (!mvmvif->ap_ibss_active) {
      return;
    }

    if (chanctx_conf == data->primary) {
      return;
    }

    if (!data->primary_ll) {
      /*
       * downgrade the current primary no matter what its
       * type is.
       */
      data->secondary = data->primary;
      data->primary = chanctx_conf;
    } else {
      /* there is low latency vif - we will be secondary */
      data->secondary = chanctx_conf;
    }

    if (data->primary == chanctx_conf) {
      data->primary_load = mvm->tcm.result.load[mvmvif->id];
    } else if (data->secondary == chanctx_conf) {
      data->secondary_load = mvm->tcm.result.load[mvmvif->id];
    }
    return;
  }

  /*
   * STA / P2P Client, try to be primary if first vif. If we are in low
   * latency mode, we are already in primary and just don't do much
   */
  if (!data->primary || data->primary == chanctx_conf) {
    data->primary = chanctx_conf;
  } else if (!data->secondary)
  /* if secondary is not NULL, it might be a GO */
  {
    data->secondary = chanctx_conf;
  }

  if (data->primary == chanctx_conf) {
    data->primary_load = mvm->tcm.result.load[mvmvif->id];
  } else if (data->secondary == chanctx_conf) {
    data->secondary_load = mvm->tcm.result.load[mvmvif->id];
  }
  /*
   * don't reduce the Tx power if one of these is true:
   *  we are in LOOSE
   *  single share antenna product
   *  BT is inactive
   *  we are not associated
   */
  if (iwl_get_coex_type(mvm, vif) == BT_COEX_LOOSE_LUT || mvm->cfg->bt_shared_single_ant ||
      !vif->bss_conf.assoc || le32_to_cpu(mvm->last_bt_notif.bt_activity_grading) == BT_OFF) {
    iwl_mvm_bt_coex_reduced_txp(mvm, mvmvif->ap_sta_id, false);
    iwl_mvm_bt_coex_enable_rssi_event(mvm, vif, false, 0);
    return;
  }

  /* try to get the avg rssi from fw */
  ave_rssi = mvmvif->bf_data.ave_beacon_signal;

  /* if the RSSI isn't valid, fake it is very low */
  if (!ave_rssi) {
    ave_rssi = -100;
  }
  if (ave_rssi > -IWL_MVM_BT_COEX_EN_RED_TXP_THRESH) {
    if (iwl_mvm_bt_coex_reduced_txp(mvm, mvmvif->ap_sta_id, true)) {
      IWL_ERR(mvm, "Couldn't send BT_CONFIG cmd\n");
    }
  } else if (ave_rssi < -IWL_MVM_BT_COEX_DIS_RED_TXP_THRESH) {
    if (iwl_mvm_bt_coex_reduced_txp(mvm, mvmvif->ap_sta_id, false)) {
      IWL_ERR(mvm, "Couldn't send BT_CONFIG cmd\n");
    }
  }

  /* Begin to monitor the RSSI: it may influence the reduced Tx power */
  iwl_mvm_bt_coex_enable_rssi_event(mvm, vif, true, ave_rssi);
}

static void iwl_mvm_bt_coex_notif_handle(struct iwl_mvm* mvm) {
  struct iwl_bt_iterator_data data = {
      .mvm = mvm,
      .notif = &mvm->last_bt_notif,
  };
  struct iwl_bt_coex_ci_cmd cmd = {};
  uint8_t ci_bw_idx;

  /* Ignore updates if we are in force mode */
  if (unlikely(mvm->bt_force_ant_mode != BT_FORCE_ANT_DIS)) {
    return;
  }

  rcu_read_lock();
  ieee80211_iterate_active_interfaces_atomic(mvm->hw, IEEE80211_IFACE_ITER_NORMAL,
                                             iwl_mvm_bt_notif_iterator, &data);

  iwl_mvm_bt_coex_tcm_based_ci(mvm, &data);

  if (data.primary) {
    struct ieee80211_chanctx_conf* chan = data.primary;
    if (WARN_ON(!chan->def.chan)) {
      rcu_read_unlock();
      return;
    }

    if (chan->def.width < NL80211_CHAN_WIDTH_40) {
      ci_bw_idx = 0;
    } else {
      if (chan->def.center_freq1 > chan->def.chan->center_freq) {
        ci_bw_idx = 2;
      } else {
        ci_bw_idx = 1;
      }
    }

    cmd.bt_primary_ci = iwl_ci_mask[chan->def.chan->hw_value][ci_bw_idx];
    cmd.primary_ch_phy_id = cpu_to_le32(*((uint16_t*)data.primary->drv_priv));
  }

  if (data.secondary) {
    struct ieee80211_chanctx_conf* chan = data.secondary;
    if (WARN_ON(!data.secondary->def.chan)) {
      rcu_read_unlock();
      return;
    }

    if (chan->def.width < NL80211_CHAN_WIDTH_40) {
      ci_bw_idx = 0;
    } else {
      if (chan->def.center_freq1 > chan->def.chan->center_freq) {
        ci_bw_idx = 2;
      } else {
        ci_bw_idx = 1;
      }
    }

    cmd.bt_secondary_ci = iwl_ci_mask[chan->def.chan->hw_value][ci_bw_idx];
    cmd.secondary_ch_phy_id = cpu_to_le32(*((uint16_t*)data.secondary->drv_priv));
  }

  rcu_read_unlock();

  /* Don't spam the fw with the same command over and over */
  if (memcmp(&cmd, &mvm->last_bt_ci_cmd, sizeof(cmd))) {
    if (iwl_mvm_send_cmd_pdu(mvm, BT_COEX_CI, 0, sizeof(cmd), &cmd)) {
      IWL_ERR(mvm, "Failed to send BT_CI cmd\n");
    }
    memcpy(&mvm->last_bt_ci_cmd, &cmd, sizeof(cmd));
  }
}

void iwl_mvm_rx_bt_coex_notif(struct iwl_mvm* mvm, struct iwl_rx_cmd_buffer* rxb) {
  struct iwl_rx_packet* pkt = rxb_addr(rxb);
  struct iwl_bt_coex_profile_notif* notif = (void*)pkt->data;

  IWL_DEBUG_COEX(mvm, "BT Coex Notification received\n");
  IWL_DEBUG_COEX(mvm, "\tBT ci compliance %d\n", notif->bt_ci_compliance);
  IWL_DEBUG_COEX(mvm, "\tBT primary_ch_lut %d\n", le32_to_cpu(notif->primary_ch_lut));
  IWL_DEBUG_COEX(mvm, "\tBT secondary_ch_lut %d\n", le32_to_cpu(notif->secondary_ch_lut));
  IWL_DEBUG_COEX(mvm, "\tBT activity grading %d\n", le32_to_cpu(notif->bt_activity_grading));

  /* remember this notification for future use: rssi fluctuations */
  memcpy(&mvm->last_bt_notif, notif, sizeof(mvm->last_bt_notif));

  iwl_mvm_bt_coex_notif_handle(mvm);
}

void iwl_mvm_bt_rssi_event(struct iwl_mvm* mvm, struct ieee80211_vif* vif,
                           enum ieee80211_rssi_event_data rssi_event) {
  struct iwl_mvm_vif* mvmvif = iwl_mvm_vif_from_mac80211(vif);
  int ret;

  lockdep_assert_held(&mvm->mutex);

  /* Ignore updates if we are in force mode */
  if (unlikely(mvm->bt_force_ant_mode != BT_FORCE_ANT_DIS)) {
    return;
  }

  /*
   * Rssi update while not associated - can happen since the statistics
   * are handled asynchronously
   */
  if (mvmvif->ap_sta_id == IWL_MVM_INVALID_STA) {
    return;
  }

  /* No BT - reports should be disabled */
  if (le32_to_cpu(mvm->last_bt_notif.bt_activity_grading) == BT_OFF) {
    return;
  }

  IWL_DEBUG_COEX(mvm, "RSSI for %pM is now %s\n", vif->bss_conf.bssid,
                 rssi_event == RSSI_EVENT_HIGH ? "HIGH" : "LOW");

  /*
   * Check if rssi is good enough for reduced Tx power, but not in loose
   * scheme.
   */
  if (rssi_event == RSSI_EVENT_LOW || mvm->cfg->bt_shared_single_ant ||
      iwl_get_coex_type(mvm, vif) == BT_COEX_LOOSE_LUT) {
    ret = iwl_mvm_bt_coex_reduced_txp(mvm, mvmvif->ap_sta_id, false);
  } else {
    ret = iwl_mvm_bt_coex_reduced_txp(mvm, mvmvif->ap_sta_id, true);
  }

  if (ret) {
    IWL_ERR(mvm, "couldn't send BT_CONFIG HCMD upon RSSI event\n");
  }
}

#define LINK_QUAL_AGG_TIME_LIMIT_DEF (4000)
#define LINK_QUAL_AGG_TIME_LIMIT_BT_ACT (1200)

uint16_t iwl_mvm_coex_agg_time_limit(struct iwl_mvm* mvm, struct ieee80211_sta* sta) {
  struct iwl_mvm_sta* mvmsta = iwl_mvm_sta_from_mac80211(sta);
  struct iwl_mvm_vif* mvmvif = iwl_mvm_vif_from_mac80211(mvmsta->vif);
  struct iwl_mvm_phy_ctxt* phy_ctxt = mvmvif->phy_ctxt;
  enum iwl_bt_coex_lut_type lut_type;

  if (mvm->last_bt_notif.ttc_status & BIT(phy_ctxt->id)) {
    return LINK_QUAL_AGG_TIME_LIMIT_DEF;
  }

#ifdef CPTCFG_IWLWIFI_FRQ_MGR
  /* 2G coex */
  if (mvm->coex_2g_enabled) {
    return LINK_QUAL_AGG_TIME_LIMIT_BT_ACT;
  }
#endif
  if (le32_to_cpu(mvm->last_bt_notif.bt_activity_grading) < BT_HIGH_TRAFFIC) {
    return LINK_QUAL_AGG_TIME_LIMIT_DEF;
  }

  lut_type = iwl_get_coex_type(mvm, mvmsta->vif);

  if (lut_type == BT_COEX_LOOSE_LUT || lut_type == BT_COEX_INVALID_LUT) {
    return LINK_QUAL_AGG_TIME_LIMIT_DEF;
  }

  /* tight coex, high bt traffic, reduce AGG time limit */
  return LINK_QUAL_AGG_TIME_LIMIT_BT_ACT;
}

bool iwl_mvm_bt_coex_is_mimo_allowed(struct iwl_mvm* mvm, struct ieee80211_sta* sta) {
  struct iwl_mvm_sta* mvmsta = iwl_mvm_sta_from_mac80211(sta);
  struct iwl_mvm_vif* mvmvif = iwl_mvm_vif_from_mac80211(mvmsta->vif);
  struct iwl_mvm_phy_ctxt* phy_ctxt = mvmvif->phy_ctxt;
  enum iwl_bt_coex_lut_type lut_type;

  if (mvm->last_bt_notif.ttc_status & BIT(phy_ctxt->id)) {
    return true;
  }

  if (le32_to_cpu(mvm->last_bt_notif.bt_activity_grading) < BT_HIGH_TRAFFIC) {
    return true;
  }

  /*
   * In Tight / TxTxDis, BT can't Rx while we Tx, so use both antennas
   * since BT is already killed.
   * In Loose, BT can Rx while we Tx, so forbid MIMO to let BT Rx while
   * we Tx.
   * When we are in 5GHz, we'll get BT_COEX_INVALID_LUT allowing MIMO.
   */
  lut_type = iwl_get_coex_type(mvm, mvmsta->vif);
  return lut_type != BT_COEX_LOOSE_LUT;
}

bool iwl_mvm_bt_coex_is_ant_avail(struct iwl_mvm* mvm, uint8_t ant) {
  /* there is no other antenna, shared antenna is always available */
  if (mvm->cfg->bt_shared_single_ant) {
    return true;
  }

  if (ant & mvm->cfg->non_shared_ant) {
    return true;
  }

  return le32_to_cpu(mvm->last_bt_notif.bt_activity_grading) < BT_HIGH_TRAFFIC;
}

bool iwl_mvm_bt_coex_is_shared_ant_avail(struct iwl_mvm* mvm) {
  /* there is no other antenna, shared antenna is always available */
  if (mvm->cfg->bt_shared_single_ant) {
    return true;
  }

  return le32_to_cpu(mvm->last_bt_notif.bt_activity_grading) < BT_HIGH_TRAFFIC;
}

bool iwl_mvm_bt_coex_is_tpc_allowed(struct iwl_mvm* mvm, enum nl80211_band band) {
  uint32_t bt_activity = le32_to_cpu(mvm->last_bt_notif.bt_activity_grading);

  if (band != NL80211_BAND_2GHZ) {
    return false;
  }

  return bt_activity >= BT_LOW_TRAFFIC;
}

uint8_t iwl_mvm_bt_coex_get_single_ant_msk(struct iwl_mvm* mvm, uint8_t enabled_ants) {
  if (fw_has_capa(&mvm->fw->ucode_capa, IWL_UCODE_TLV_CAPA_COEX_SCHEMA_2) &&
      (mvm->cfg->non_shared_ant & enabled_ants)) {
    return mvm->cfg->non_shared_ant;
  }

  return first_antenna(enabled_ants);
}

uint8_t iwl_mvm_bt_coex_tx_prio(struct iwl_mvm* mvm, struct ieee80211_hdr* hdr,
                                struct ieee80211_tx_info* info, uint8_t ac) {
  __le16 fc = hdr->frame_control;
  bool mplut_enabled = iwl_mvm_is_mplut_supported(mvm);

  if (info->band != NL80211_BAND_2GHZ) {
    return 0;
  }

  if (unlikely(mvm->bt_tx_prio)) {
    return mvm->bt_tx_prio - 1;
  }

  if (likely(ieee80211_is_data(fc))) {
    if (likely(ieee80211_is_data_qos(fc))) {
      switch (ac) {
        case IEEE80211_AC_BE:
          return mplut_enabled ? 1 : 0;
        case IEEE80211_AC_VI:
          return mplut_enabled ? 2 : 3;
        case IEEE80211_AC_VO:
          return 3;
        default:
          return 0;
      }
    } else if (is_multicast_ether_addr(hdr->addr1)) {
      return 3;
    } else {
      return 0;
    }
  } else if (ieee80211_is_mgmt(fc)) {
    return ieee80211_is_disassoc(fc) ? 0 : 3;
  } else if (ieee80211_is_ctl(fc)) {
    /* ignore cfend and cfendack frames as we never send those */
    return 3;
  }

  return 0;
}

void iwl_mvm_bt_coex_vif_change(struct iwl_mvm* mvm) { iwl_mvm_bt_coex_notif_handle(mvm); }

#ifdef CPTCFG_IWLWIFI_LTE_COEX
int iwl_mvm_send_lte_coex_config_cmd(struct iwl_mvm* mvm) {
  const struct iwl_lte_coex_config_cmd* cmd = &mvm->lte_state.config;

  if (!(mvm->fw->ucode_capa.flags & IWL_UCODE_TLV_FLAGS_LTE_COEX)) {
    IWL_DEBUG_COEX(mvm, "LTE-Coex not supported!\n");
    return -EOPNOTSUPP;
  }

  IWL_DEBUG_COEX(mvm,
                 "LTE-Coex: lte_coex_config_cmd:\n"
                 "\tstate: %d\n\tband: %d\n\tchan: %d\n",
                 le32_to_cpu(cmd->lte_state), le32_to_cpu(cmd->lte_band),
                 le32_to_cpu(cmd->lte_chan));

  IWL_DEBUG_COEX(mvm,
                 "\ttx safe freq min: %d\n\ttx safe freq max: %d\n"
                 "\trx safe freq min: %d\n\trx safe freq max: %d\n",
                 le32_to_cpu(cmd->tx_safe_freq_min), le32_to_cpu(cmd->tx_safe_freq_max),
                 le32_to_cpu(cmd->rx_safe_freq_min), le32_to_cpu(cmd->rx_safe_freq_max));

  return iwl_mvm_send_cmd_pdu(mvm, LTE_COEX_CONFIG_CMD, 0, sizeof(*cmd), cmd);
}

int iwl_mvm_send_lte_coex_wifi_reported_channel_cmd(struct iwl_mvm* mvm) {
  const struct iwl_lte_coex_wifi_reported_channel_cmd* cmd = &mvm->lte_state.rprtd_chan;

  if (!(mvm->fw->ucode_capa.flags & IWL_UCODE_TLV_FLAGS_LTE_COEX)) {
    IWL_DEBUG_COEX(mvm, "LTE-Coex not supported!\n");
    return -EOPNOTSUPP;
  }

  IWL_DEBUG_COEX(mvm,
                 "LTE-COEX: lte_coex_wifi_reported_channel_cmd:\n"
                 "\tchannel: %d\n\tbandwidth: %d\n",
                 le32_to_cpu(cmd->channel), le32_to_cpu(cmd->bandwidth));

  return iwl_mvm_send_cmd_pdu(mvm, LTE_COEX_WIFI_REPORTED_CHANNEL_CMD, 0, sizeof(*cmd), cmd);
}

int iwl_mvm_send_lte_coex_static_params_cmd(struct iwl_mvm* mvm) {
  const struct iwl_lte_coex_static_params_cmd* cmd = &mvm->lte_state.stat;

  if (!(mvm->fw->ucode_capa.flags & IWL_UCODE_TLV_FLAGS_LTE_COEX)) {
    IWL_DEBUG_COEX(mvm, "LTE-Coex not supported!\n");
    return -EOPNOTSUPP;
  }

  IWL_DEBUG_COEX(mvm,
                 "LTE-COEX: lte_coex_static_params_cmd:\n"
                 "\tmfu config[0]: %d\n\ttx power[0]: %d\n",
                 le32_to_cpu(cmd->mfu_config[0]), cmd->tx_power_in_dbm[0]);

  return iwl_mvm_send_cmd_pdu(mvm, LTE_COEX_STATIC_PARAMS_CMD, 0, sizeof(*cmd), cmd);
}

int iwl_mvm_send_lte_fine_tuning_params_cmd(struct iwl_mvm* mvm) {
  const struct iwl_lte_coex_fine_tuning_params_cmd* cmd = &mvm->lte_state.ft;

  if (!(mvm->fw->ucode_capa.flags & IWL_UCODE_TLV_FLAGS_LTE_COEX)) {
    IWL_DEBUG_COEX(mvm, "LTE-Coex not supported!\n");
    return -EOPNOTSUPP;
  }

  IWL_DEBUG_COEX(mvm,
                 "LTE-COEX: lte_fine_tuning_params_cmd:\n"
                 "\trx protection assert timing: %d\n",
                 le32_to_cpu(cmd->rx_protection_assert_timing));

  IWL_DEBUG_COEX(mvm,
                 "\ttx protection assert timing: %d\n"
                 "\trx protection timeout: %d\n\tmin tx power: %d\n",
                 le32_to_cpu(cmd->tx_protection_assert_timing),
                 le32_to_cpu(cmd->rx_protection_timeout), le32_to_cpu(cmd->min_tx_power));

  IWL_DEBUG_COEX(mvm,
                 "\tul load uapsd threshold: %d\n"
                 "\trx failure during ul uapsd threshold: %d\n",
                 le32_to_cpu(cmd->lte_ul_load_uapsd_threshold),
                 le32_to_cpu(cmd->rx_failure_during_ul_uapsd_threshold));

  IWL_DEBUG_COEX(mvm,
                 "\trx failure during ul scan compensation threshold: %d\n"
                 "\trx duration for ack protection: %d\n",
                 le32_to_cpu(cmd->rx_failure_during_ul_sc_threshold),
                 le32_to_cpu(cmd->rx_duration_for_ack_protection_us));

  IWL_DEBUG_COEX(mvm,
                 "\tbeacon failure during ul counter: %d\n"
                 "\tdtim failure during ul counter: %d\n",
                 le32_to_cpu(cmd->beacon_failure_during_ul_counter),
                 le32_to_cpu(cmd->dtim_failure_during_ul_counter));

  return iwl_mvm_send_cmd_pdu(mvm, LTE_COEX_FINE_TUNING_PARAMS_CMD, 0, sizeof(*cmd), cmd);
}

int iwl_mvm_send_lte_sps_cmd(struct iwl_mvm* mvm) {
  const struct iwl_lte_coex_sps_cmd* cmd = &mvm->lte_state.sps;

  if (!(mvm->fw->ucode_capa.flags & IWL_UCODE_TLV_FLAGS_LTE_COEX)) {
    IWL_DEBUG_COEX(mvm, "LTE-Coex not supported!\n");
    return -EOPNOTSUPP;
  }

  IWL_DEBUG_COEX(mvm, "LTE-COEX: lte_sps_cmd:\n\tsps info: %d\n",
                 le32_to_cpu(cmd->lte_semi_persistent_info));

  return iwl_mvm_send_cmd_pdu(mvm, LTE_COEX_SPS_CMD, 0, sizeof(*cmd), cmd);
}

void iwl_mvm_reset_lte_state(struct iwl_mvm* mvm) {
  struct lte_coex_state* lte_state = &mvm->lte_state;

  lte_state->state = LTE_OFF;
  lte_state->has_config = 0;
  lte_state->has_rprtd_chan = 0;
  lte_state->has_sps = 0;
  lte_state->has_ft = 0;
}

void iwl_mvm_send_lte_commands(struct iwl_mvm* mvm) {
  struct lte_coex_state* lte_state = &mvm->lte_state;

  lockdep_assert_held(&mvm->mutex);

  if (lte_state->has_static) {
    iwl_mvm_send_lte_coex_static_params_cmd(mvm);
  }
  if (lte_state->has_rprtd_chan) {
    iwl_mvm_send_lte_coex_wifi_reported_channel_cmd(mvm);
  }
  if (lte_state->state != LTE_OFF) {
    iwl_mvm_send_lte_coex_config_cmd(mvm);
  }
  if (lte_state->has_sps) {
    iwl_mvm_send_lte_sps_cmd(mvm);
  }
  if (lte_state->has_ft) {
    iwl_mvm_send_lte_fine_tuning_params_cmd(mvm);
  }
}
#endif  /* CPTCFG_IWLWIFI_LTE_COEX */
#endif  // NEEDS_PORTING
