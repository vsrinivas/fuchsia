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
 *****************************************************************************/

#include <zircon/status.h>

#include <wlan/protocol/ieee80211.h>

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/iwl-trans.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/mvm/fw-api.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/mvm/mvm.h"
/*
 * iwl_mvm_rx_rx_phy_cmd - REPLY_RX_PHY_CMD handler
 *
 * Copies the phy information in mvm->last_phy_info, it will be used when the
 * actual data will come from the fw in the next packet.
 */
void iwl_mvm_rx_rx_phy_cmd(struct iwl_mvm* mvm, struct iwl_rx_cmd_buffer* rxb) {
  struct iwl_rx_packet* pkt = rxb_addr(rxb);

  memcpy(&mvm->last_phy_info, pkt->data, sizeof(mvm->last_phy_info));
  mvm->ampdu_ref++;

#ifdef CPTCFG_IWLWIFI_DEBUGFS
  if (mvm->last_phy_info.phy_flags & cpu_to_le16(RX_RES_PHY_FLAGS_AGG)) {
    spin_lock(&mvm->drv_stats_lock);
    mvm->drv_rx_stats.ampdu_count++;
    spin_unlock(&mvm->drv_stats_lock);
  }
#endif
}

/*
 * iwl_mvm_get_signal_strength - use new rx PHY INFO API
 * values are reported by the fw as positive values - need to negate
 * to obtain their dBM.  Account for missing antennas by replacing 0
 * values by -256dBm: practically 0 power and a non-feasible 8 bit value.
 *
 * Args:
 *   mvm: the MVM instance
 *   phy_info: the PHY info received in the last packet.
 *   rx_info: output. RSSI will be set in this structure.
 */
static void iwl_mvm_get_signal_strength(const struct iwl_mvm* mvm,
                                        const struct iwl_rx_phy_info* phy_info,
                                        wlan_rx_info_t* rx_info) {
  const int kS8Min = -128;
  int energy_a, energy_b, energy_c, max_energy;
  uint32_t val;

  val = le32_to_cpu(phy_info->non_cfg_phy[IWL_RX_INFO_ENERGY_ANT_ABC_IDX]);
  energy_a = (val & IWL_RX_INFO_ENERGY_ANT_A_MSK) >> IWL_RX_INFO_ENERGY_ANT_A_POS;
  energy_a = energy_a ? -energy_a : kS8Min;
  energy_b = (val & IWL_RX_INFO_ENERGY_ANT_B_MSK) >> IWL_RX_INFO_ENERGY_ANT_B_POS;
  energy_b = energy_b ? -energy_b : kS8Min;
  energy_c = (val & IWL_RX_INFO_ENERGY_ANT_C_MSK) >> IWL_RX_INFO_ENERGY_ANT_C_POS;
  energy_c = energy_c ? -energy_c : kS8Min;
  max_energy = MAX(energy_a, energy_b);
  max_energy = MAX(max_energy, energy_c);

  IWL_DEBUG_STATS(mvm, "energy In A %d B %d C %d , and max %d\n", energy_a, energy_b, energy_c,
                  max_energy);

  rx_info->valid_fields |= WLAN_RX_INFO_VALID_RSSI;
  rx_info->rssi_dbm = max_energy;
}

#if 0   // NEEDS_PORTING
/*
 * iwl_mvm_set_mac80211_rx_flag - translate fw status to mac80211 format
 * @mvm: the mvm object
 * @hdr: 80211 header
 * @stats: status in mac80211's format
 * @rx_pkt_status: status coming from fw
 *
 * returns non 0 value if the packet should be dropped
 */
static uint32_t iwl_mvm_set_mac80211_rx_flag(struct iwl_mvm* mvm, struct ieee80211_hdr* hdr,
                                             struct ieee80211_rx_status* stats,
                                             uint32_t rx_pkt_status, uint8_t* crypt_len) {
  if (!ieee80211_has_protected(hdr->frame_control) ||
      (rx_pkt_status & RX_MPDU_RES_STATUS_SEC_ENC_MSK) == RX_MPDU_RES_STATUS_SEC_NO_ENC) {
    return 0;
  }

  /* packet was encrypted with unknown alg */
  if ((rx_pkt_status & RX_MPDU_RES_STATUS_SEC_ENC_MSK) == RX_MPDU_RES_STATUS_SEC_ENC_ERR) {
    return 0;
  }

  switch (rx_pkt_status & RX_MPDU_RES_STATUS_SEC_ENC_MSK) {
    case RX_MPDU_RES_STATUS_SEC_CCM_ENC:
      /* alg is CCM: check MIC only */
      if (!(rx_pkt_status & RX_MPDU_RES_STATUS_MIC_OK)) {
        return -1;
      }

      stats->flag |= RX_FLAG_DECRYPTED;
      *crypt_len = IEEE80211_CCMP_HDR_LEN;
      return 0;

    case RX_MPDU_RES_STATUS_SEC_TKIP_ENC:
      /* Don't drop the frame and decrypt it in SW */
      if (!fw_has_api(&mvm->fw->ucode_capa, IWL_UCODE_TLV_API_DEPRECATE_TTAK) &&
          !(rx_pkt_status & RX_MPDU_RES_STATUS_TTAK_OK)) {
        return 0;
      }
      *crypt_len = IEEE80211_TKIP_IV_LEN;
      /* fall through if TTAK OK */

    case RX_MPDU_RES_STATUS_SEC_WEP_ENC:
      if (!(rx_pkt_status & RX_MPDU_RES_STATUS_ICV_OK)) {
        return -1;
      }

      stats->flag |= RX_FLAG_DECRYPTED;
      if ((rx_pkt_status & RX_MPDU_RES_STATUS_SEC_ENC_MSK) == RX_MPDU_RES_STATUS_SEC_WEP_ENC) {
        *crypt_len = IEEE80211_WEP_IV_LEN;
      }
      return 0;

    case RX_MPDU_RES_STATUS_SEC_EXT_ENC:
      if (!(rx_pkt_status & RX_MPDU_RES_STATUS_MIC_OK)) {
        return -1;
      }
      stats->flag |= RX_FLAG_DECRYPTED;
      return 0;

    default:
      /* Expected in monitor (not having the keys) */
      if (!mvm->monitor_on) {
        IWL_ERR(mvm, "Unhandled alg: 0x%x\n", rx_pkt_status);
      }
  }

  return 0;
}

static void iwl_mvm_rx_handle_tcm(struct iwl_mvm* mvm, struct ieee80211_sta* sta,
                                  struct ieee80211_hdr* hdr, uint32_t len,
                                  struct iwl_rx_phy_info* phy_info, uint32_t rate_n_flags) {
  struct iwl_mvm_sta* mvmsta;
  struct iwl_mvm_tcm_mac* mdata;
  struct iwl_mvm_vif* mvmvif;
  int mac;
  int ac = IEEE80211_AC_BE; /* treat non-QoS as BE */
  /* expected throughput in 100Kbps, single stream, 20 MHz */
  static const uint8_t thresh_tpt[] = {
      9, 18, 30, 42, 60, 78, 90, 96, 120, 135,
  };
  uint16_t thr;

  if (ieee80211_is_data_qos(hdr->frame_control)) {
    ac = tid_to_mac80211_ac[ieee80211_get_tid(hdr)];
  }

  mvmsta = iwl_mvm_sta_from_mac80211(sta);
  mac = mvmsta->mac_id_n_color & FW_CTXT_ID_MSK;

  if (time_after(jiffies, mvm->tcm.ts + MVM_TCM_PERIOD)) {
    schedule_delayed_work(&mvm->tcm.work, 0);
  }
  mdata = &mvm->tcm.data[mac];
  mdata->rx.pkts[ac]++;

  /* count the airtime only once for each ampdu */
  if (mdata->rx.last_ampdu_ref != mvm->ampdu_ref) {
    mdata->rx.last_ampdu_ref = mvm->ampdu_ref;
    mdata->rx.airtime += le16_to_cpu(phy_info->frame_time);
  }
  mvmvif = iwl_mvm_vif_from_mac80211(mvmsta->vif);

  if (!(rate_n_flags & (RATE_MCS_HT_MSK | RATE_MCS_VHT_MSK))) {
    return;
  }

  if (mdata->opened_rx_ba_sessions || mdata->uapsd_nonagg_detect.detected ||
      (!mvmvif->queue_params[IEEE80211_AC_VO].uapsd &&
       !mvmvif->queue_params[IEEE80211_AC_VI].uapsd &&
       !mvmvif->queue_params[IEEE80211_AC_BE].uapsd &&
       !mvmvif->queue_params[IEEE80211_AC_BK].uapsd) ||
      mvmsta->sta_id != mvmvif->ap_sta_id) {
    return;
  }

  if (rate_n_flags & RATE_MCS_HT_MSK) {
    thr = thresh_tpt[rate_n_flags & RATE_HT_MCS_RATE_CODE_MSK];
    thr *= 1 + ((rate_n_flags & RATE_HT_MCS_NSS_MSK) >> RATE_HT_MCS_NSS_POS);
  } else {
    if (WARN_ON((rate_n_flags & RATE_VHT_MCS_RATE_CODE_MSK) >= ARRAY_SIZE(thresh_tpt))) {
      return;
    }
    thr = thresh_tpt[rate_n_flags & RATE_VHT_MCS_RATE_CODE_MSK];
    thr *= 1 + ((rate_n_flags & RATE_VHT_MCS_NSS_MSK) >> RATE_VHT_MCS_NSS_POS);
  }

  thr <<= ((rate_n_flags & RATE_MCS_CHAN_WIDTH_MSK) >> RATE_MCS_CHAN_WIDTH_POS);

  mdata->uapsd_nonagg_detect.rx_bytes += len;
  ewma_rate_add(&mdata->uapsd_nonagg_detect.rate, thr);
}

static void iwl_mvm_rx_csum(struct ieee80211_sta* sta, struct sk_buff* skb, uint32_t status) {
  struct iwl_mvm_sta* mvmsta = iwl_mvm_sta_from_mac80211(sta);
  struct iwl_mvm_vif* mvmvif = iwl_mvm_vif_from_mac80211(mvmsta->vif);

  if (mvmvif->features & NETIF_F_RXCSUM && status & RX_MPDU_RES_STATUS_CSUM_DONE &&
      status & RX_MPDU_RES_STATUS_CSUM_OK) {
    skb->ip_summed = CHECKSUM_UNNECESSARY;
  }
}
#endif  // NEEDS_PORTING

/*
 * iwl_mvm_rx_rx_mpdu - REPLY_RX_MPDU_CMD handler
 *
 * Handles the actual data of the Rx packet from the fw
 *
 * Below is the packet layout from the firmware.
 *
 *                 rx_res,
 *   pkt           &pkt->data[]       frame
 *    |                |                |
 *    v                v                v
 *    0        4        8       10       12                   12 + res_len
 *   +--------+--------+-------+--------+--------------------+---------------+
 *   |        |        |    *rx_res     |                    |               |
 *   | len_n_ | cmd_   |----------------| MAC header ....    | rx_pkt_status |
 *   |  flags | header | byte_ | assist |                    |               |
 *   |        |        | count |        |                    |               |
 *   +--------+--------+-------+--------+--------------------+---------------+
 *                                       <----  res_len ---->
 *
 * - 'assist' contains the TCP offload info from FW. See 'enum iwl_csum_rx_assist_info'.
 * - 'rx_pkt_status' contains the flags parsed by FW (e.g. CRC_OK). See 'enum iwl_mvm_rx_status'.
 *
 * TODO(43218): replace 'napi' with something else to map to mvmvif.
 */
void iwl_mvm_rx_rx_mpdu(struct iwl_mvm* mvm, struct napi_struct* napi,
                        struct iwl_rx_cmd_buffer* rxb) {
  // The PHY info was received in the last MVM packet.
  struct iwl_rx_phy_info* phy_info = &mvm->last_phy_info;
  uint16_t phy_flags = le16_to_cpu(phy_info->phy_flags);

  // Now, parse this packet.
  struct iwl_rx_packet* pkt = rxb_addr(rxb);
  struct iwl_rx_mpdu_res_start* rx_res = (struct iwl_rx_mpdu_res_start*)pkt->data;
  struct ieee80211_frame_header* frame = (void*)(pkt->data + sizeof(*rx_res));
  size_t res_len = le16_to_cpu(rx_res->byte_count);
  uint32_t rx_pkt_status = le32_to_cpup((__le32*)(pkt->data + sizeof(*rx_res) + res_len));

  // Prepare the meta info sent to MLME.
  wlan_rx_info_t rx_info = {};

#if 0   // NEEDS_PORTING
  // TODO(37594): Milestone: Connect to Protected Network
  /*
   * drop the packet if it has failed being decrypted by HW
   */
  if (iwl_mvm_set_mac80211_rx_flag(mvm, hdr, rx_status, rx_pkt_status, &crypt_len)) {
    IWL_DEBUG_DROP(mvm, "Bad decryption results 0x%08x\n", rx_pkt_status);
    kfree_skb(skb);
    return;
  }
#endif  // NEEDS_PORTING

  /*
   * Keep packets with CRC errors (and with overrun) for monitor mode
   * (otherwise the firmware discards them) but mark them as bad.
   */
  if (!(rx_pkt_status & RX_MPDU_RES_STATUS_CRC_OK) ||
      !(rx_pkt_status & RX_MPDU_RES_STATUS_OVERRUN_OK)) {
    IWL_DEBUG_RX(mvm, "Bad CRC or FIFO: 0x%08X.\n", rx_pkt_status);
    rx_info.rx_flags |= WLAN_RX_INFO_FLAGS_FCS_INVALID;
  }

  wlan_info_band_t band =
      phy_flags & RX_RES_PHY_FLAGS_BAND_24 ? WLAN_INFO_BAND_2GHZ : WLAN_INFO_BAND_5GHZ;
  rx_info.chan.primary = le16_to_cpu(phy_info->channel);

#if 0   // NEEDS_PORTING
  /* TSF as indicated by the firmware  is at INA time */
  rx_status->flag |= RX_FLAG_MACTIME_PLCP_START;
#endif  // NEEDS_PORTING

  iwl_mvm_get_signal_strength(mvm, phy_info, &rx_info);

#if 0  // NEEDS_PORTING
  IWL_DEBUG_STATS_LIMIT(mvm, "Rssi %d, TSF %llu\n", rx_status->signal,
                        (unsigned long long)rx_status->mactime);

  rcu_read_lock();
  if (rx_pkt_status & RX_MPDU_RES_STATUS_SRC_STA_FOUND) {
    uint32_t id = rx_pkt_status & RX_MPDU_RES_STATUS_STA_ID_MSK;

    id >>= RX_MDPU_RES_STATUS_STA_ID_SHIFT;

    if (!WARN_ON_ONCE(id >= ARRAY_SIZE(mvm->fw_id_to_mac_id))) {
      sta = rcu_dereference(mvm->fw_id_to_mac_id[id]);
      if (IS_ERR(sta)) {
        sta = NULL;
      }
    }
  } else if (!is_multicast_ether_addr(hdr->addr2)) {
    /* This is fine since we prevent two stations with the same
     * address from being added.
     */
    sta = ieee80211_find_sta_by_ifaddr(mvm->hw, hdr->addr2, NULL);
  }

  if (sta) {
    struct iwl_mvm_sta* mvmsta = iwl_mvm_sta_from_mac80211(sta);
    struct ieee80211_vif* tx_blocked_vif = rcu_dereference(mvm->csa_tx_blocked_vif);
    struct iwl_fw_dbg_trigger_tlv* trig;
    struct ieee80211_vif* vif = mvmsta->vif;

    /* We have tx blocked stations (with CS bit). If we heard
     * frames from a blocked station on a new channel we can
     * TX to it again.
     */
    if (unlikely(tx_blocked_vif) && vif == tx_blocked_vif) {
      struct iwl_mvm_vif* mvmvif = iwl_mvm_vif_from_mac80211(tx_blocked_vif);

      if (mvmvif->csa_target_freq == rx_status->freq) {
        iwl_mvm_sta_modify_disable_tx_ap(mvm, sta, false);
      }
    }

    rs_update_last_rssi(mvm, mvmsta, rx_status);

    trig = iwl_fw_dbg_trigger_on(&mvm->fwrt, ieee80211_vif_to_wdev(vif), FW_DBG_TRIGGER_RSSI);

    if (trig && ieee80211_is_beacon(hdr->frame_control)) {
      struct iwl_fw_dbg_trigger_low_rssi* rssi_trig;
      int32_t rssi;

      rssi_trig = (void*)trig->data;
      rssi = le32_to_cpu(rssi_trig->rssi);

      if (rx_status->signal < rssi) {
        iwl_fw_dbg_collect_trig(&mvm->fwrt, trig, NULL);
      }
    }

    if (!mvm->tcm.paused && len >= sizeof(*hdr) && !is_multicast_ether_addr(hdr->addr1) &&
        ieee80211_is_data(hdr->frame_control)) {
      iwl_mvm_rx_handle_tcm(mvm, sta, hdr, len, phy_info, rate_n_flags);
    }
#ifdef CPTCFG_IWLMVM_TDLS_PEER_CACHE
    /*
     * these packets are from the AP or the existing TDLS peer.
     * In both cases an existing station.
     */
    iwl_mvm_tdls_peer_cache_pkt(mvm, hdr, len, 0);
#endif  /* CPTCFG_IWLMVM_TDLS_PEER_CACHE */

    if (ieee80211_is_data(hdr->frame_control)) {
      iwl_mvm_rx_csum(sta, skb, rx_pkt_status);
    }
  }
  rcu_read_unlock();

  /* set the preamble flag if appropriate */
  if (phy_info->phy_flags & cpu_to_le16(RX_RES_PHY_FLAGS_SHORT_PREAMBLE)) {
    rx_status->enc_flags |= RX_ENC_FLAG_SHORTPRE;
  }

  if (phy_info->phy_flags & cpu_to_le16(RX_RES_PHY_FLAGS_AGG)) {
    /*
     * We know which subframes of an A-MPDU belong
     * together since we get a single PHY response
     * from the firmware for all of them
     */
    rx_status->flag |= RX_FLAG_AMPDU_DETAILS;
    rx_status->ampdu_reference = mvm->ampdu_ref;
  }
#endif  // NEEDS_PORTING

  // Parse rx_info fields from phy_info->rate_n_flags.
  //
  // See rate_n_flags bit fields definition in fw/api/rs.h.
  uint32_t rate_n_flags = le32_to_cpu(phy_info->rate_n_flags);

  switch (rate_n_flags & RATE_MCS_CHAN_WIDTH_MSK) {
    case RATE_MCS_CHAN_WIDTH_20:
      rx_info.chan.cbw = WLAN_CHANNEL_BANDWIDTH__20;
      break;
    case RATE_MCS_CHAN_WIDTH_40:
      rx_info.chan.cbw = WLAN_CHANNEL_BANDWIDTH__40;
      break;
    case RATE_MCS_CHAN_WIDTH_80:
      rx_info.chan.cbw = WLAN_CHANNEL_BANDWIDTH__80;
      break;
    case RATE_MCS_CHAN_WIDTH_160:
      rx_info.chan.cbw = WLAN_CHANNEL_BANDWIDTH__160;
      break;
  }

#if 0   // NEEDS_PORTING
  if (!(rate_n_flags & RATE_MCS_CCK_MSK) && rate_n_flags & RATE_MCS_SGI_MSK) {
    rx_status->enc_flags |= RX_ENC_FLAG_SHORT_GI;
  }
  if (rate_n_flags & RATE_HT_MCS_GF_MSK) {
    rx_status->enc_flags |= RX_ENC_FLAG_HT_GF;
  }
  if (rate_n_flags & RATE_MCS_LDPC_MSK) {
    rx_status->enc_flags |= RX_ENC_FLAG_LDPC;
  }
#endif  // NEEDS_PORTING

  // See rate_n_flags bit fields definition in fw/api/rs.h.
  if (rate_n_flags & RATE_MCS_HT_MSK) {
    rx_info.phy = WLAN_INFO_PHY_TYPE_HT;
#if 0   // NEEDS_PORTING
    // TODO(36683): Supports HT (802.11n)
    u8 stbc = (rate_n_flags & RATE_MCS_STBC_MSK) >>
        RATE_MCS_STBC_POS;
    rx_status->encoding = RX_ENC_HT;
    rx_status->rate_idx = rate_n_flags & RATE_HT_MCS_INDEX_MSK;
    rx_status->enc_flags |= stbc << RX_ENC_FLAG_STBC_SHIFT;
#endif  // NEEDS_PORTING
  } else if (rate_n_flags & RATE_MCS_VHT_MSK) {
    rx_info.phy = WLAN_INFO_PHY_TYPE_VHT;
#if 0   // NEEDS_PORTING
    // TODO(36684): Supports VHT (802.11ac)
    uint8_t stbc = (rate_n_flags & RATE_MCS_STBC_MSK) >> RATE_MCS_STBC_POS;
    rx_status->nss = ((rate_n_flags & RATE_VHT_MCS_NSS_MSK) >> RATE_VHT_MCS_NSS_POS) + 1;
    rx_status->rate_idx = rate_n_flags & RATE_VHT_MCS_RATE_CODE_MSK;
    rx_status->encoding = RX_ENC_VHT;
    rx_status->enc_flags |= stbc << RX_ENC_FLAG_STBC_SHIFT;
    if (rate_n_flags & RATE_MCS_BF_MSK) {
      rx_status->enc_flags |= RX_ENC_FLAG_BF;
    }
#endif  // NEEDS_PORTING
  } else {
    rx_info.phy =
        phy_flags & RX_RES_PHY_FLAGS_MOD_CCK ? WLAN_INFO_PHY_TYPE_CCK : WLAN_INFO_PHY_TYPE_OFDM;

    int mac80211_idx;
    zx_status_t status = iwl_mvm_legacy_rate_to_mac80211_idx(rate_n_flags, band, &mac80211_idx);
    if (status != ZX_OK) {
      IWL_ERR(mvm, "Cannot convert rate_n_flags (0x%08x, band=%d) to mac80211 index. status=%s\n",
              rate_n_flags, band, zx_status_get_string(status));
      return;
    }

    status = mac80211_idx_to_data_rate(band, mac80211_idx, &rx_info.data_rate);
    if (status != ZX_OK) {
      IWL_ERR(mvm, "Cannot convert mac80211 index (%d) to data rate for MLME (band=%d)\n",
              mac80211_idx, band);
      return;
    }
  }
  rx_info.valid_fields |= WLAN_RX_INFO_VALID_DATA_RATE;

#if 0  // NEEDS_PORTING
#ifdef CPTCFG_IWLWIFI_DEBUGFS
  iwl_mvm_update_frame_stats(mvm, rate_n_flags, rx_status->flag & RX_FLAG_AMPDU_DETAILS);
#endif

  if (unlikely((ieee80211_is_beacon(hdr->frame_control) ||
                ieee80211_is_probe_resp(hdr->frame_control)) &&
               mvm->sched_scan_pass_all == SCHED_SCAN_PASS_ALL_ENABLED)) {
    mvm->sched_scan_pass_all = SCHED_SCAN_PASS_ALL_FOUND;
  }

  if (unlikely(ieee80211_is_beacon(hdr->frame_control) ||
               ieee80211_is_probe_resp(hdr->frame_control))) {
    rx_status->boottime_ns = ktime_get_boot_ns();
  }

  /* Take a reference briefly to kick off a d0i3 entry delay so
   * we can handle bursts of RX packets without toggling the
   * state too often.  But don't do this for beacons if we are
   * going to idle because the beacon filtering changes we make
   * cause the firmware to send us collateral beacons. */
  take_ref = !(test_bit(STATUS_TRANS_GOING_IDLE, &mvm->trans->status) &&
               ieee80211_is_beacon(hdr->frame_control));

  if (take_ref) {
    iwl_mvm_ref(mvm, IWL_MVM_REF_RX);
  }
#endif  // NEEDS_PORTING

  // Send to MLME
  // TODO(43218): replace rxq->napi with interface instance so that we can map to mvmvif.
  wlanmac_ifc_recv(&mvm->mvmvif[0]->ifc, 0, frame, res_len, &rx_info);

#if 0   // NEEDS_PORTING
  if (take_ref) {
    iwl_mvm_unref(mvm, IWL_MVM_REF_RX);
  }
#endif  // NEEDS_PORTING
}

#if 0   // NEEDS_PORTING
struct iwl_mvm_stat_data {
  struct iwl_mvm* mvm;
  __le32 mac_id;
  uint8_t beacon_filter_average_energy;
  void* general;
};

static void iwl_mvm_stat_iterator(void* _data, uint8_t* mac, struct ieee80211_vif* vif) {
  struct iwl_mvm_stat_data* data = _data;
  struct iwl_mvm* mvm = data->mvm;
  int sig = -data->beacon_filter_average_energy;
  int last_event;
  int thold = vif->bss_conf.cqm_rssi_thold;
  int hyst = vif->bss_conf.cqm_rssi_hyst;
  uint16_t id = le32_to_cpu(data->mac_id);
  struct iwl_mvm_vif* mvmvif = iwl_mvm_vif_from_mac80211(vif);
  uint16_t vif_id = mvmvif->id;

  /* This doesn't need the MAC ID check since it's not taking the
   * data copied into the "data" struct, but rather the data from
   * the notification directly.
   */
  if (iwl_mvm_has_new_rx_stats_api(mvm)) {
    struct mvm_statistics_general* general = data->general;

    mvmvif->beacon_stats.num_beacons = le32_to_cpu(general->beacon_counter[vif_id]);
    mvmvif->beacon_stats.avg_signal = -general->beacon_average_energy[vif_id];
  } else {
    struct mvm_statistics_general_v8* general = data->general;

    mvmvif->beacon_stats.num_beacons = le32_to_cpu(general->beacon_counter[vif_id]);
    mvmvif->beacon_stats.avg_signal = -general->beacon_average_energy[vif_id];
  }

  if (mvmvif->id != id) {
    return;
  }

  if (vif->type != NL80211_IFTYPE_STATION) {
    return;
  }

  if (sig == 0) {
    IWL_DEBUG_RX(mvm, "RSSI is 0 - skip signal based decision\n");
    return;
  }

  mvmvif->bf_data.ave_beacon_signal = sig;

  /* BT Coex */
  if (mvmvif->bf_data.bt_coex_min_thold != mvmvif->bf_data.bt_coex_max_thold) {
    last_event = mvmvif->bf_data.last_bt_coex_event;
    if (sig > mvmvif->bf_data.bt_coex_max_thold &&
        (last_event <= mvmvif->bf_data.bt_coex_min_thold || last_event == 0)) {
      mvmvif->bf_data.last_bt_coex_event = sig;
      IWL_DEBUG_RX(mvm, "cqm_iterator bt coex high %d\n", sig);
      iwl_mvm_bt_rssi_event(mvm, vif, RSSI_EVENT_HIGH);
    } else if (sig < mvmvif->bf_data.bt_coex_min_thold &&
               (last_event >= mvmvif->bf_data.bt_coex_max_thold || last_event == 0)) {
      mvmvif->bf_data.last_bt_coex_event = sig;
      IWL_DEBUG_RX(mvm, "cqm_iterator bt coex low %d\n", sig);
      iwl_mvm_bt_rssi_event(mvm, vif, RSSI_EVENT_LOW);
    }
  }

  if (!(vif->driver_flags & IEEE80211_VIF_SUPPORTS_CQM_RSSI)) {
    return;
  }

  /* CQM Notification */
  last_event = mvmvif->bf_data.last_cqm_event;
  if (thold && sig < thold && (last_event == 0 || sig < last_event - hyst)) {
    mvmvif->bf_data.last_cqm_event = sig;
    IWL_DEBUG_RX(mvm, "cqm_iterator cqm low %d\n", sig);
    ieee80211_cqm_rssi_notify(vif, NL80211_CQM_RSSI_THRESHOLD_EVENT_LOW, sig, GFP_KERNEL);
  } else if (sig > thold && (last_event == 0 || sig > last_event + hyst)) {
    mvmvif->bf_data.last_cqm_event = sig;
    IWL_DEBUG_RX(mvm, "cqm_iterator cqm high %d\n", sig);
    ieee80211_cqm_rssi_notify(vif, NL80211_CQM_RSSI_THRESHOLD_EVENT_HIGH, sig, GFP_KERNEL);
  }
}

static inline void iwl_mvm_rx_stats_check_trigger(struct iwl_mvm* mvm, struct iwl_rx_packet* pkt) {
  struct iwl_fw_dbg_trigger_tlv* trig;
  struct iwl_fw_dbg_trigger_stats* trig_stats;
  uint32_t trig_offset, trig_thold;

  trig = iwl_fw_dbg_trigger_on(&mvm->fwrt, NULL, FW_DBG_TRIGGER_STATS);
  if (!trig) {
    return;
  }

  trig_stats = (void*)trig->data;

  trig_offset = le32_to_cpu(trig_stats->stop_offset);
  trig_thold = le32_to_cpu(trig_stats->stop_threshold);

  if (WARN_ON_ONCE(trig_offset >= iwl_rx_packet_payload_len(pkt))) {
    return;
  }

  if (le32_to_cpup((__le32*)(pkt->data + trig_offset)) < trig_thold) {
    return;
  }

  iwl_fw_dbg_collect_trig(&mvm->fwrt, trig, NULL);
}

void iwl_mvm_handle_rx_statistics(struct iwl_mvm* mvm, struct iwl_rx_packet* pkt) {
  struct iwl_mvm_stat_data data = {
      .mvm = mvm,
  };
  int expected_size;
  int i;
  uint8_t* energy;
  __le32 *bytes, *air_time;
  __le32 flags;

  if (!iwl_mvm_has_new_rx_stats_api(mvm)) {
    if (iwl_mvm_has_new_rx_api(mvm)) {
      expected_size = sizeof(struct iwl_notif_statistics_v11);
    } else {
      expected_size = sizeof(struct iwl_notif_statistics_v10);
    }
  } else {
    expected_size = sizeof(struct iwl_notif_statistics);
  }

  if (WARN_ONCE(iwl_rx_packet_payload_len(pkt) != expected_size,
                "received invalid statistics size (%d)!\n", iwl_rx_packet_payload_len(pkt))) {
    return;
  }

  if (!iwl_mvm_has_new_rx_stats_api(mvm)) {
    struct iwl_notif_statistics_v11* stats = (void*)&pkt->data;

    data.mac_id = stats->rx.general.mac_id;
    data.beacon_filter_average_energy = stats->general.common.beacon_filter_average_energy;

    mvm->rx_stats_v3 = stats->rx;

    mvm->radio_stats.rx_time = le64_to_cpu(stats->general.common.rx_time);
    mvm->radio_stats.tx_time = le64_to_cpu(stats->general.common.tx_time);
    mvm->radio_stats.on_time_rf = le64_to_cpu(stats->general.common.on_time_rf);
    mvm->radio_stats.on_time_scan = le64_to_cpu(stats->general.common.on_time_scan);

    data.general = &stats->general;

    flags = stats->flag;
  } else {
    struct iwl_notif_statistics* stats = (void*)&pkt->data;

    data.mac_id = stats->rx.general.mac_id;
    data.beacon_filter_average_energy = stats->general.common.beacon_filter_average_energy;

    mvm->rx_stats = stats->rx;

    mvm->radio_stats.rx_time = le64_to_cpu(stats->general.common.rx_time);
    mvm->radio_stats.tx_time = le64_to_cpu(stats->general.common.tx_time);
    mvm->radio_stats.on_time_rf = le64_to_cpu(stats->general.common.on_time_rf);
    mvm->radio_stats.on_time_scan = le64_to_cpu(stats->general.common.on_time_scan);

    data.general = &stats->general;

    flags = stats->flag;
  }

  iwl_mvm_rx_stats_check_trigger(mvm, pkt);

  ieee80211_iterate_active_interfaces(mvm->hw, IEEE80211_IFACE_ITER_NORMAL, iwl_mvm_stat_iterator,
                                      &data);

  if (!iwl_mvm_has_new_rx_api(mvm)) {
    return;
  }

  if (!iwl_mvm_has_new_rx_stats_api(mvm)) {
    struct iwl_notif_statistics_v11* v11 = (void*)&pkt->data;

    energy = (void*)&v11->load_stats.avg_energy;
    bytes = (void*)&v11->load_stats.byte_count;
    air_time = (void*)&v11->load_stats.air_time;
  } else {
    struct iwl_notif_statistics* stats = (void*)&pkt->data;

    energy = (void*)&stats->load_stats.avg_energy;
    bytes = (void*)&stats->load_stats.byte_count;
    air_time = (void*)&stats->load_stats.air_time;
  }

  rcu_read_lock();
  for (i = 0; i < ARRAY_SIZE(mvm->fw_id_to_mac_id); i++) {
    struct iwl_mvm_sta* sta;

    if (!energy[i]) {
      continue;
    }

    sta = iwl_mvm_sta_from_staid_rcu(mvm, i);
    if (!sta) {
      continue;
    }
    sta->avg_energy = energy[i];
  }
  rcu_read_unlock();

  /*
   * Don't update in case the statistics are not cleared, since
   * we will end up counting twice the same airtime, once in TCM
   * request and once in statistics notification.
   */
  if (!(le32_to_cpu(flags) & IWL_STATISTICS_REPLY_FLG_CLEAR)) {
    return;
  }

  spin_lock(&mvm->tcm.lock);
  for (i = 0; i < NUM_MAC_INDEX_DRIVER; i++) {
    struct iwl_mvm_tcm_mac* mdata = &mvm->tcm.data[i];
    uint32_t rx_bytes = le32_to_cpu(bytes[i]);
    uint32_t airtime = le32_to_cpu(air_time[i]);

    mdata->rx.airtime += airtime;
    mdata->uapsd_nonagg_detect.rx_bytes += rx_bytes;
    if (airtime) {
      /* re-init every time to store rate from FW */
      ewma_rate_init(&mdata->uapsd_nonagg_detect.rate);
      ewma_rate_add(&mdata->uapsd_nonagg_detect.rate, rx_bytes * 8 / airtime);
    }
  }
  spin_unlock(&mvm->tcm.lock);
}

void iwl_mvm_rx_statistics(struct iwl_mvm* mvm, struct iwl_rx_cmd_buffer* rxb) {
  iwl_mvm_handle_rx_statistics(mvm, rxb_addr(rxb));
}

void iwl_mvm_window_status_notif(struct iwl_mvm* mvm, struct iwl_rx_cmd_buffer* rxb) {
  struct iwl_rx_packet* pkt = rxb_addr(rxb);
  struct iwl_ba_window_status_notif* notif = (void*)pkt->data;
  int i;
  uint32_t pkt_len = iwl_rx_packet_payload_len(pkt);

  if (WARN_ONCE(pkt_len != sizeof(*notif),
                "Received window status notification of wrong size (%u)\n", pkt_len)) {
    return;
  }

  rcu_read_lock();
  for (i = 0; i < BA_WINDOW_STREAMS_MAX; i++) {
    struct ieee80211_sta* sta;
    uint8_t sta_id, tid;
    uint64_t bitmap;
    uint32_t ssn;
    uint16_t ratid;
    uint16_t received_mpdu;

    ratid = le16_to_cpu(notif->ra_tid[i]);
    /* check that this TID is valid */
    if (!(ratid & BA_WINDOW_STATUS_VALID_MSK)) {
      continue;
    }

    received_mpdu = le16_to_cpu(notif->mpdu_rx_count[i]);
    if (received_mpdu == 0) {
      continue;
    }

    tid = ratid & BA_WINDOW_STATUS_TID_MSK;
    /* get the station */
    sta_id = (ratid & BA_WINDOW_STATUS_STA_ID_MSK) >> BA_WINDOW_STATUS_STA_ID_POS;
    sta = rcu_dereference(mvm->fw_id_to_mac_id[sta_id]);
    if (IS_ERR_OR_NULL(sta)) {
      continue;
    }
    bitmap = le64_to_cpu(notif->bitmap[i]);
    ssn = le32_to_cpu(notif->start_seq_num[i]);

    /* update mac80211 with the bitmap for the reordering buffer */
    ieee80211_mark_rx_ba_filtered_frames(sta, tid, ssn, bitmap, received_mpdu);
  }
  rcu_read_unlock();
}
#endif  // NEEDS_PORTING
