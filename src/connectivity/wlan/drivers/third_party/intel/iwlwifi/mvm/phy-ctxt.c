/******************************************************************************
 *
 * Copyright(c) 2012 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2013 - 2014 Intel Mobile Communications GmbH
 * Copyright(c) 2018           Intel Corporation
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

#include <wlan/protocol/info.h>

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/mvm/fw-api.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/mvm/mvm.h"

/* Maps the driver specific channel width definition to the fw values */
uint8_t iwl_mvm_get_channel_width(wlan_channel_t* chandef) {
  switch (chandef->cbw) {
    case CBW20:
      return PHY_VHT_CHANNEL_MODE20;
    case CBW40ABOVE:
    case CBW40BELOW:  // fall-thru
      return PHY_VHT_CHANNEL_MODE40;
    case CBW80:
      return PHY_VHT_CHANNEL_MODE80;
    case CBW160:
      return PHY_VHT_CHANNEL_MODE160;
    default:
      WARN(1, "Invalid channel width=%u", chandef->width);
      return PHY_VHT_CHANNEL_MODE20;
  }
}

/*
 * Maps the driver specific control channel position (relative to the center
 * freq) definitions to the the fw values.
 *
 * Here is the channel list: https://en.wikipedia.org/wiki/List_of_WLAN_channels
 */
uint8_t iwl_mvm_get_ctrl_pos(wlan_channel_t* chandef) {
  uint8_t primary = chandef->primary;
  uint8_t cbw = chandef->cbw;
  uint8_t base;

  if (chandef->cbw == CBW20) {
    // 20Mhz always uses the default value.
    return PHY_VHT_CTRL_POS_1_BELOW;
  }

  // TODO(WLAN-1216): move out the center freq calculation into a shared lib.

  if (36 <= primary && primary <= 64) {
    base = 36;
  } else if (100 <= primary && primary <= 128) {
    base = 100;
  } else if (132 <= primary && primary <= 144) {
    if (cbw == CBW160) {  // This group doesn't support 160MHz primary channels. Use default value.
      return PHY_VHT_CTRL_POS_1_BELOW;
    }
    base = 132;
  } else if (149 <= primary && primary <= 161) {
    if (cbw == CBW160) {  // This group doesn't support 160MHz primary channels. Use default value.
      return PHY_VHT_CTRL_POS_1_BELOW;
    }
    base = 149;
  } else {
    // 2.4GHz band or invalid channel index. Use default value.
    return PHY_VHT_CTRL_POS_1_BELOW;
  }

  uint8_t offset_to_base = primary - base;
  uint8_t mask;                          // used to mask the chan_index.
                                         // # of 1's means the bandwidth.
  bool has_bit2 = offset_to_base & 0x4;  // for HT40+/- checking
  switch (chandef->cbw) {
    case CBW40ABOVE:   // The secondary channel is above the primary.
      mask = 0x7;      // Keep 3 bits.
      if (has_bit2) {  // Channel 40, 48, 56 ... doesn't allow HT40+.
        return PHY_VHT_CTRL_POS_1_BELOW;
      }
      break;
    case CBW40BELOW:    // The secondary channel is below the primary.
      mask = 0x7;       // Keep 3 bits.
      if (!has_bit2) {  // Channel 36, 44, 52 ... doesn't allow HT40-.
        return PHY_VHT_CTRL_POS_1_BELOW;
      }
      break;
    case CBW80:
      mask = 0xf;  // Keep 4 bits.
      break;
    case CBW160:
      mask = 0x1f;  // Keep 5 bits.
      break;
    /*
     * The FW is expected to check the control channel position only
     * when in HT/VHT and the channel width is not 20MHz. Return
     * this value as the default one.
     */
    default:
      IWL_WARN(chandef, "Invalid channel bandwidth (primary=%u cbw=%u)\n", chandef->primary,
               chandef->cbw);
      return PHY_VHT_CTRL_POS_1_BELOW;
  }

  // Now, calculate offset from the primary channel index to the center.
  //
  // Take primary channel 48 @80MHz channel as example:
  //
  //            primary          | freq |           | mask=0xf |  half
  //            channel          |offset| chan num  | (CBW80)  |bandwidth=8 (40MHz)
  //  ==============================================================================
  //
  //   36  -------------------->     0    base           ^          ^
  //        base index                                   |          |
  //   40                           20    base + 4       |          |
  //                                      -------------- | -------- | <----------- center freq/index
  //   44                           40    base + 8       |     ^    v    | 10Mhz
  //                                                     |     |
  //   48  -------------------->    60    base + 12      |     v --- This is offset_to_center.
  //        offset_to_base = 12                          |
  //                                80                   v
  //
  // We can get:
  //
  //   offset_to_base = 48 - 36       # = 12 indexes
  //   mask = 0xf                     # 80MHz
  //   half_bandwidth = 8             # 40MHz
  //   center_index = 8 - 2 = 6       # 30MHz (offset to the base index)
  //   offset_to_center = 12 - 6 = 6  # 60MHz - 30MHz = +30MHz
  //
  uint8_t half_bandwith = (mask + 1) / 2;    // # of channel indexes within the half bandwidth.
  uint8_t center_index = half_bandwith - 2;  // 2 indexes = 2 * 5MHz = 10Mhz.
  int offset_to_center = (offset_to_base & mask) - center_index;

  const int mhz_per_index = 5;  // 5 MHz for each channel index.
  switch (offset_to_center * mhz_per_index) {
    case -70:
      return PHY_VHT_CTRL_POS_4_BELOW;
    case -50:
      return PHY_VHT_CTRL_POS_3_BELOW;
    case -30:
      return PHY_VHT_CTRL_POS_2_BELOW;
    case -10:
      return PHY_VHT_CTRL_POS_1_BELOW;
    case 10:
      return PHY_VHT_CTRL_POS_1_ABOVE;
    case 30:
      return PHY_VHT_CTRL_POS_2_ABOVE;
    case 50:
      return PHY_VHT_CTRL_POS_3_ABOVE;
    case 70:
      return PHY_VHT_CTRL_POS_4_ABOVE;
    default:
      IWL_WARN(chandef, "Invalid channel definition (primary=%u cbw=%u)\n", chandef->primary,
               chandef->cbw);
      return PHY_VHT_CTRL_POS_1_BELOW;
  }
}

/*
 * Construct the generic fields of the PHY context command
 */
static void iwl_mvm_phy_ctxt_cmd_hdr(struct iwl_mvm_phy_ctxt* ctxt, struct iwl_phy_context_cmd* cmd,
                                     uint32_t action, uint32_t apply_time) {
  memset(cmd, 0, sizeof(struct iwl_phy_context_cmd));

  cmd->id_and_color = cpu_to_le32(FW_CMD_ID_AND_COLOR(ctxt->id, ctxt->color));
  cmd->action = cpu_to_le32(action);
  cmd->apply_time = cpu_to_le32(apply_time);
}

/*
 * Add the phy configuration to the PHY context command
 */
static void iwl_mvm_phy_ctxt_cmd_data(struct iwl_mvm* mvm, struct iwl_phy_context_cmd* cmd,
                                      wlan_channel_t* chandef, uint8_t chains_static,
                                      uint8_t chains_dynamic) {
  uint8_t active_cnt, idle_cnt;

  /* Set the channel info data */
  cmd->ci.band = (chandef->primary < 14 ? PHY_BAND_24 : PHY_BAND_5);

  cmd->ci.channel = chandef->primary;
  cmd->ci.width = iwl_mvm_get_channel_width(chandef);
  cmd->ci.ctrl_pos = iwl_mvm_get_ctrl_pos(chandef);

  /* Set rx the chains */
  idle_cnt = chains_static;
  active_cnt = chains_dynamic;

#if 0   // NEEDS_PORTING
  /* In scenarios where we only ever use a single-stream rates,
   * i.e. legacy 11b/g/a associations, single-stream APs or even
   * static SMPS, enable both chains to get diversity, improving
   * the case where we're far enough from the AP that attenuation
   * between the two antennas is sufficiently different to impact
   * performance.
   */
  if (active_cnt == 1 && iwl_mvm_rx_diversity_allowed(mvm)) {
    idle_cnt = 2;
    active_cnt = 2;
  }
#endif  // NEEDS_PORTING

  cmd->rxchain_info = cpu_to_le32(iwl_mvm_get_valid_rx_ant(mvm) << PHY_RX_CHAIN_VALID_POS);
  cmd->rxchain_info |= cpu_to_le32(idle_cnt << PHY_RX_CHAIN_CNT_POS);
  cmd->rxchain_info |= cpu_to_le32(active_cnt << PHY_RX_CHAIN_MIMO_CNT_POS);
#ifdef CPTCFG_IWLWIFI_DEBUGFS
  if (unlikely(mvm->dbgfs_rx_phyinfo)) {
    cmd->rxchain_info = cpu_to_le32(mvm->dbgfs_rx_phyinfo);
  }
#endif

  cmd->txchain_info = cpu_to_le32(iwl_mvm_get_valid_tx_ant(mvm));
}

/*
 * Send a command to apply the current phy configuration. The command is send
 * only if something in the configuration changed: in case that this is the
 * first time that the phy configuration is applied or in case that the phy
 * configuration changed from the previous apply.
 */
static int iwl_mvm_phy_ctxt_apply(struct iwl_mvm* mvm, struct iwl_mvm_phy_ctxt* ctxt,
                                  wlan_channel_t* chandef, uint8_t chains_static,
                                  uint8_t chains_dynamic, uint32_t action, uint32_t apply_time) {
  struct iwl_phy_context_cmd cmd;
  int ret;

  /* Set the command header fields */
  iwl_mvm_phy_ctxt_cmd_hdr(ctxt, &cmd, action, apply_time);

  /* Set the command data */
  iwl_mvm_phy_ctxt_cmd_data(mvm, &cmd, chandef, chains_static, chains_dynamic);

  ret = iwl_mvm_send_cmd_pdu(mvm, PHY_CONTEXT_CMD, 0, sizeof(struct iwl_phy_context_cmd), &cmd);
  if (ret) {
    IWL_ERR(mvm, "PHY ctxt cmd error. ret=%d\n", ret);
  }
  return ret;
}

/*
 * Send a command to add a PHY context based on the current HW configuration.
 */
int iwl_mvm_phy_ctxt_add(struct iwl_mvm* mvm, struct iwl_mvm_phy_ctxt* ctxt,
                         wlan_channel_t* chandef, uint8_t chains_static, uint8_t chains_dynamic) {
  WARN_ON(!test_bit(IWL_MVM_STATUS_IN_HW_RESTART, &mvm->status) && ctxt->ref);
  lockdep_assert_held(&mvm->mutex);

#ifdef CPTCFG_IWLWIFI_FRQ_MGR
  ctxt->fm_tx_power_limit = IWL_DEFAULT_MAX_TX_POWER;
#endif

  return iwl_mvm_phy_ctxt_apply(mvm, ctxt, chandef, chains_static, chains_dynamic,
                                FW_CTXT_ACTION_ADD, 0);
}

#if 0   // NEEDS_PORTING
/*
 * Update the number of references to the given PHY context. This is valid only
 * in case the PHY context was already created, i.e., its reference count > 0.
 */
void iwl_mvm_phy_ctxt_ref(struct iwl_mvm* mvm, struct iwl_mvm_phy_ctxt* ctxt) {
  lockdep_assert_held(&mvm->mutex);
  ctxt->ref++;
}

/*
 * Send a command to modify the PHY context based on the current HW
 * configuration. Note that the function does not check that the configuration
 * changed.
 */
int iwl_mvm_phy_ctxt_changed(struct iwl_mvm* mvm, struct iwl_mvm_phy_ctxt* ctxt,
                             struct cfg80211_chan_def* chandef, uint8_t chains_static,
                             uint8_t chains_dynamic) {
  enum iwl_ctxt_action action = FW_CTXT_ACTION_MODIFY;

  lockdep_assert_held(&mvm->mutex);

  if (fw_has_capa(&mvm->fw->ucode_capa, IWL_UCODE_TLV_CAPA_BINDING_CDB_SUPPORT) &&
      ctxt->channel->band != chandef->chan->band) {
    int ret;

    /* ... remove it here ...*/
    ret = iwl_mvm_phy_ctxt_apply(mvm, ctxt, chandef, chains_static, chains_dynamic,
                                 FW_CTXT_ACTION_REMOVE, 0);
    if (ret) {
      return ret;
    }

    /* ... and proceed to add it again */
    action = FW_CTXT_ACTION_ADD;
  }

  ctxt->channel = chandef->chan;
  ctxt->width = chandef->width;
  return iwl_mvm_phy_ctxt_apply(mvm, ctxt, chandef, chains_static, chains_dynamic, action, 0);
}

void iwl_mvm_phy_ctxt_unref(struct iwl_mvm* mvm, struct iwl_mvm_phy_ctxt* ctxt) {
  lockdep_assert_held(&mvm->mutex);

  if (WARN_ON_ONCE(!ctxt)) {
    return;
  }

  ctxt->ref--;

  /*
   * Move unused phy's to a default channel. When the phy is moved the,
   * fw will cleanup immediate quiet bit if it was previously set,
   * otherwise we might not be able to reuse this phy.
   */
  if (ctxt->ref == 0) {
    struct ieee80211_channel* chan;
    struct cfg80211_chan_def chandef;

    chan = &mvm->hw->wiphy->bands[NL80211_BAND_2GHZ]->channels[0];
    cfg80211_chandef_create(&chandef, chan, NL80211_CHAN_NO_HT);
    iwl_mvm_phy_ctxt_changed(mvm, ctxt, &chandef, 1, 1);
  }
}

static void iwl_mvm_binding_iterator(void* _data, uint8_t* mac, struct ieee80211_vif* vif) {
  unsigned long* data = _data;
  struct iwl_mvm_vif* mvmvif = iwl_mvm_vif_from_mac80211(vif);

  if (!mvmvif->phy_ctxt) {
    return;
  }

  if (vif->type == NL80211_IFTYPE_STATION || vif->type == NL80211_IFTYPE_AP) {
    __set_bit(mvmvif->phy_ctxt->id, data);
  }
}

int iwl_mvm_phy_ctx_count(struct iwl_mvm* mvm) {
  unsigned long phy_ctxt_counter = 0;

  ieee80211_iterate_active_interfaces_atomic(mvm->hw, IEEE80211_IFACE_ITER_NORMAL,
                                             iwl_mvm_binding_iterator, &phy_ctxt_counter);

  return hweight8(phy_ctxt_counter);
}
#endif  // NEEDS_PORTING
