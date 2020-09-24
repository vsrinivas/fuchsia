// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The place holder for the driver code to interact with the MLME.
//
//                                 devmgr
//                                   |
//                                   v
//          MLME  === channel ===   SME
//            |
//            v
//  +-------------------+
//  |   wlan-device.c   |
//  +-------------------+
//  | PHY ops | MAC ops |
//  +-------------------+
//       |         |
//       v         v
//     mvm/mac80211.c
//
// Note that the '*ctx' in this file may refer to:
//
//   - 'struct iwl_trans*' for PHY ops.
//   - 'struct iwl_mvm_vif*' for MAC ops.
//
//
// Sme_channel
//
//   The steps below briefly describe how the 'sme_channel' is used and transferred. In short,
//   the goal is to let SME and MLME have a channel to communicate with each other.
//
//   + After the devmgr (the device manager in wlanstack) detects a PHY device, the devmgr first
//     creates an SME instance in order to handle the MAC operation later. Then the devmgr
//     establishes a channel and passes one end to the SME instance.
//
//   + The devmgr requests the PHY device to create a MAC interface. In the request, the other end
//     of channel is passed to the driver.
//
//   + The driver's phy_create_iface() gets called, and saves the 'sme_channel' handle in the newly
//     created MAC context.
//
//   + Once the MAC device is added, its mac_start() will be called. Then it will transfer the
//     'sme_channel' handle back to the MLME.
//
//   + Now, both sides of channel (SME and MLME) can talk now.
//

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/wlan-device.h"

#include <stdio.h>
#include <string.h>
#include <zircon/status.h>

#include <ddk/device.h>
#include <ddk/driver.h>

#include "garnet/lib/wlan/protocol/include/wlan/protocol/ieee80211.h"
#include "garnet/lib/wlan/protocol/include/wlan/protocol/mac.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/iwl-debug.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/mvm/mvm.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/mvm/time-event.h"

////////////////////////////////////  Helper Functions  ////////////////////////////////////////////

//
// Given a NVM data structure, and return the list of bands.
//
// Returns:
//   size_t: # of bands enabled in the NVM data.
//   bands[]: contains the list of enabled bands.
//
size_t compose_band_list(const struct iwl_nvm_data* nvm_data,
                         wlan_info_band_t bands[WLAN_INFO_BAND_COUNT]) {
  size_t bands_count = 0;

  if (nvm_data->sku_cap_band_24ghz_enable) {
    bands[bands_count++] = WLAN_INFO_BAND_2GHZ;
  }
  if (nvm_data->sku_cap_band_52ghz_enable) {
    bands[bands_count++] = WLAN_INFO_BAND_5GHZ;
  }
  ZX_ASSERT(bands_count <= WLAN_INFO_BAND_COUNT);

  return bands_count;
}

//
// Given a NVM data, copy the band and channel info into the 'wlan_info_band_info_t' structure.
//
// - 'bands_count' is the number of bands in 'bands[]'.
// - 'band_infos[]' must have at least bands_count for this function to write.
//
void fill_band_infos(const struct iwl_nvm_data* nvm_data, const wlan_info_band_t* bands,
                     size_t bands_count, wlan_info_band_info_t* band_infos) {
  ZX_ASSERT(bands_count <= ARRAY_SIZE(nvm_data->bands));

  for (size_t band_idx = 0; band_idx < bands_count; ++band_idx) {
    wlan_info_band_t band_id = bands[band_idx];
    const struct ieee80211_supported_band* sband = &nvm_data->bands[band_id];  // source
    wlan_info_band_info_t* band_info = &band_infos[band_idx];                  // destination

    band_info->band = band_id;
    band_info->ht_supported = nvm_data->sku_cap_11n_enable;
    // TODO(43517): Better handling of driver features bits/flags
    band_info->ht_caps.ht_capability_info =
        IEEE80211_HT_CAPS_CHAN_WIDTH | IEEE80211_HT_CAPS_SMPS_DYNAMIC;
    band_info->ht_caps.ampdu_params = (3 << IEEE80211_AMPDU_RX_LEN_SHIFT) |  // (64K - 1) bytes
                                      (6 << IEEE80211_AMPDU_DENSITY_SHIFT);  // 8 us
    // TODO(36683): band_info->ht_caps->supported_mcs_set =
    // TODO(36684): band_info->vht_caps =

    ZX_ASSERT(sband->n_bitrates <= (int)ARRAY_SIZE(band_info->rates));
    for (int rate_idx = 0; rate_idx < sband->n_bitrates; ++rate_idx) {
      band_info->rates[rate_idx] = cfg_rates_to_80211(sband->bitrates[rate_idx]);
    }

    // Fill the channel list of this band.
    wlan_info_channel_list_t* ch_list = &band_info->supported_channels;
    switch (band_info->band) {
      case WLAN_INFO_BAND_2GHZ:
        ch_list->base_freq = 2407;
        break;
      case WLAN_INFO_BAND_5GHZ:
        ch_list->base_freq = 5000;
        break;
      default:
        ZX_ASSERT(0);  // Unknown band ID.
        break;
    }
    ZX_ASSERT(sband->n_channels <= (int)ARRAY_SIZE(ch_list->channels));
    for (int ch_idx = 0; ch_idx < sband->n_channels; ++ch_idx) {
      ch_list->channels[ch_idx] = sband->channels[ch_idx].ch_num;
    }
  }
}

/////////////////////////////////////       MAC       //////////////////////////////////////////////

static zx_status_t mac_query(void* ctx, uint32_t options, wlanmac_info_t* info) {
  struct iwl_mvm_vif* mvmvif = ctx;

  if (!ctx || !info) {
    return ZX_ERR_INVALID_ARGS;
  }
  memset(info, 0, sizeof(*info));

  ZX_ASSERT(mvmvif->mvm);
  ZX_ASSERT(mvmvif->mvm->nvm_data);
  struct iwl_nvm_data* nvm_data = mvmvif->mvm->nvm_data;

  memcpy(info->ifc_info.mac_addr, nvm_data->hw_addr, sizeof(info->ifc_info.mac_addr));
  info->ifc_info.mac_role = mvmvif->mac_role;
  // TODO(43517): Better handling of driver features bits/flags
  info->ifc_info.driver_features = WLAN_INFO_DRIVER_FEATURE_SCAN_OFFLOAD;
  info->ifc_info.supported_phys = WLAN_INFO_PHY_TYPE_DSSS | WLAN_INFO_PHY_TYPE_CCK |
                                  WLAN_INFO_PHY_TYPE_OFDM | WLAN_INFO_PHY_TYPE_HT;
  info->ifc_info.caps = WLAN_INFO_HARDWARE_CAPABILITY_SHORT_PREAMBLE |
                        WLAN_INFO_HARDWARE_CAPABILITY_SPECTRUM_MGMT |
                        WLAN_INFO_HARDWARE_CAPABILITY_SHORT_SLOT_TIME;

  // Determine how many bands this adapter supports.
  wlan_info_band_t bands[WLAN_INFO_BAND_COUNT];
  info->ifc_info.bands_count = compose_band_list(nvm_data, bands);

  fill_band_infos(nvm_data, bands, info->ifc_info.bands_count, info->ifc_info.bands);

  return ZX_OK;
}

static zx_status_t mac_start(void* ctx, const wlanmac_ifc_protocol_t* ifc,
                             zx_handle_t* out_sme_channel) {
  struct iwl_mvm_vif* mvmvif = ctx;

  if (!ctx || !ifc || !out_sme_channel) {
    return ZX_ERR_INVALID_ARGS;
  }

  // Clear the output result first.
  *out_sme_channel = ZX_HANDLE_INVALID;

  // The SME channel assigned in phy_create_iface() is gone.
  if (mvmvif->sme_channel == ZX_HANDLE_INVALID) {
    IWL_ERR(mvmvif, "Invalid SME channel. The interface might have been bound already.\n");
    return ZX_ERR_ALREADY_BOUND;
  }

  // Transfer the handle to MLME. Also invalid the copy we hold to indicate that this interface has
  // been bound.
  *out_sme_channel = mvmvif->sme_channel;
  mvmvif->sme_channel = ZX_HANDLE_INVALID;

  mvmvif->ifc = *ifc;

  zx_status_t ret = iwl_mvm_mac_add_interface(mvmvif);
  if (ret != ZX_OK) {
    IWL_ERR(mvmvif, "Cannot add MAC interface: %s\n", zx_status_get_string(ret));
    return ret;
  }

  return ret;
}

static void mac_stop(void* ctx) {
  struct iwl_mvm_vif* mvmvif = ctx;

  zx_status_t ret = iwl_mvm_mac_remove_interface(mvmvif);
  if (ret != ZX_OK) {
    IWL_ERR(mvmvif, "Cannot remove MAC interface: %s\n", zx_status_get_string(ret));
  }
}

static zx_status_t mac_queue_tx(void* ctx, uint32_t options, wlan_tx_packet_t* pkt) {
  IWL_ERR(ctx, "%s() needs porting ... see fxbug.dev/36742\n", __func__);
  return ZX_ERR_NOT_SUPPORTED;
}

// This function will ensure the mvmvif->phy_ctxt is valid (either get a free one from pool
// or use the assigned one).
//
static zx_status_t mac_ensure_phyctxt_valid(struct iwl_mvm_vif* mvmvif) {
  if (!mvmvif->phy_ctxt) {
    // Add PHY context with default value.
    uint16_t phy_ctxt_id;
    zx_status_t ret = iwl_mvm_add_chanctx(mvmvif->mvm, &default_channel, &phy_ctxt_id);
    if (ret != ZX_OK) {
      IWL_ERR(mvmvif, "Cannot add channel context: %s\n", zx_status_get_string(ret));
      return ret;
    }
    mvmvif->phy_ctxt = &mvmvif->mvm->phy_ctxts[phy_ctxt_id];
  }

  return ZX_OK;
}

static zx_status_t mac_set_channel(void* ctx, uint32_t options, const wlan_channel_t* chan) {
  struct iwl_mvm_vif* mvmvif = ctx;
  zx_status_t ret;

  // Before we do anything, ensure the PHY context had been assigned to the mvmvif.
  ret = mac_ensure_phyctxt_valid(mvmvif);
  if (ret != ZX_OK) {
    IWL_ERR(mvmvif, "Cannot get an available chanctx: %s\n", zx_status_get_string(ret));
    return ret;
  }

  mvmvif->phy_ctxt->chandef = *chan;

  ret = iwl_mvm_change_chanctx(mvmvif->mvm, mvmvif->phy_ctxt->id, chan);
  if (ret != ZX_OK) {
    IWL_ERR(mvmvif, "Cannot change chanctx: %s\n", zx_status_get_string(ret));
    return ret;
  }

  ret = iwl_mvm_assign_vif_chanctx(mvmvif, chan);
  if (ret != ZX_OK) {
    IWL_ERR(mvmvif, "Cannot assign VIF chanctx: %s\n", zx_status_get_string(ret));
    return ret;
  }

  return ret;
}

static struct iwl_mvm_sta* alloc_ap_mvm_sta(const uint8_t bssid[]) {
  struct iwl_mvm_sta* mvm_sta = calloc(1, sizeof(struct iwl_mvm_sta));
  if (!mvm_sta) {
    return NULL;
  }

  for (size_t i = 0; i < ARRAY_SIZE(mvm_sta->txq); i++) {
    mvm_sta->txq[i] = calloc(1, sizeof(struct iwl_mvm_txq));
  }
  memcpy(mvm_sta->addr, bssid, ETH_ALEN);

  return mvm_sta;
}

static void free_ap_mvm_sta(struct iwl_mvm_sta* mvm_sta) {
  if (!mvm_sta) {
    return;
  }

  for (size_t i = 0; i < ARRAY_SIZE(mvm_sta->txq); i++) {
    free(mvm_sta->txq[i]);
  }
  free(mvm_sta);
}

static zx_status_t mac_configure_bss(void* ctx, uint32_t options, const wlan_bss_config_t* config) {
  struct iwl_mvm_vif* mvmvif = ctx;

  IWL_INFO(mvmvif, "mac_configure_bss(bssid=%02x:%02x:%02x:%02x:%02x:%02x, type=%d, remote=%d)\n",
           config->bssid[0], config->bssid[1], config->bssid[2], config->bssid[3], config->bssid[4],
           config->bssid[5], config->bss_type, config->remote);

  if (config->bss_type != WLAN_BSS_TYPE_INFRASTRUCTURE) {
    IWL_ERR(mvmvif, "invalid bss_type requested: %d\n", config->bss_type);
    return ZX_ERR_INVALID_ARGS;
  }

  if (mvmvif->ap_sta_id != IWL_MVM_INVALID_STA) {
    IWL_ERR(mvmvif, "The AP sta ID has been set already. ap_sta_id=%d\n", mvmvif->ap_sta_id);
    return ZX_ERR_ALREADY_EXISTS;
  }
  // Note that 'ap_sta_id' is unset and later will be set in iwl_mvm_add_sta().

  // Add AP into the STA table in the firmware.
  struct iwl_mvm_sta* mvm_sta = alloc_ap_mvm_sta(config->bssid);
  if (!mvm_sta) {
    IWL_ERR(mvmvif, "cannot allocate MVM STA for AP.\n");
    return ZX_ERR_NO_RESOURCES;
  }

  zx_status_t ret = iwl_mvm_mac_sta_state(mvmvif, mvm_sta, IWL_STA_NOTEXIST, IWL_STA_NONE);
  if (ret != ZX_OK) {
    IWL_ERR(mvmvif, "cannot set MVM STA state: %s\n", zx_status_get_string(ret));
    goto exit;
  }

  // Simulates the behavior of iwl_mvm_bss_info_changed_station().
  mtx_lock(&mvmvif->mvm->mutex);
  memcpy(mvmvif->bss_conf.bssid, config->bssid, ETH_ALEN);
  memcpy(mvmvif->bssid, config->bssid, ETH_ALEN);
  ret = iwl_mvm_mac_ctxt_changed(mvmvif, false, mvmvif->bssid);
  if (ret != ZX_OK) {
    IWL_ERR(mvmvif, "cannot set BSSID: %s\n", zx_status_get_string(ret));
    goto unlock;
  }
  ret = iwl_mvm_mac_ctxt_changed(mvmvif, false, NULL);
  if (ret != ZX_OK) {
    IWL_ERR(mvmvif, "cannot clear BSSID: %s\n", zx_status_get_string(ret));
    goto unlock;
  }
  mtx_unlock(&mvmvif->mvm->mutex);

  // Ask the firmware to pay attention for beacon.
  iwl_mvm_mac_mgd_prepare_tx(mvmvif->mvm, mvmvif, IWL_MVM_TE_SESSION_PROTECTION_MAX_TIME_MS);

  // Allocate a Tx queue for this station.
  mtx_lock(&mvmvif->mvm->mutex);
  ret = iwl_mvm_sta_alloc_queue(mvmvif->mvm, mvm_sta, IEEE80211_AC_BE, IWL_MAX_TID_COUNT);
  if (ret != ZX_OK) {
    IWL_ERR(mvmvif, "cannot allocate queue for STA: %s\n", zx_status_get_string(ret));
  }

unlock:
  mtx_unlock(&mvmvif->mvm->mutex);

exit:
  free_ap_mvm_sta(mvm_sta);
  return ret;
}

static zx_status_t mac_enable_beaconing(void* ctx, uint32_t options,
                                        const wlan_bcn_config_t* bcn_cfg) {
  IWL_ERR(ctx, "%s() needs porting ... see fxbug.dev/36742\n", __func__);
  return ZX_ERR_NOT_SUPPORTED;
}

static zx_status_t mac_configure_beacon(void* ctx, uint32_t options, const wlan_tx_packet_t* pkt) {
  IWL_ERR(ctx, "%s() needs porting ... see fxbug.dev/36742\n", __func__);
  return ZX_ERR_NOT_SUPPORTED;
}

static zx_status_t mac_set_key(void* ctx, uint32_t options, const wlan_key_config_t* key_config) {
  IWL_ERR(ctx, "%s() needs porting ... see fxbug.dev/36742\n", __func__);
  return ZX_ERR_NOT_SUPPORTED;
}

static zx_status_t mac_configure_assoc(void* ctx, uint32_t options,
                                       const wlan_assoc_ctx_t* assoc_ctx) {
  IWL_ERR(ctx, "%s() needs porting ... see fxbug.dev/36742\n", __func__);
  return ZX_ERR_NOT_SUPPORTED;
}

static zx_status_t mac_clear_assoc(void* ctx, uint32_t options, const uint8_t* peer_addr,
                                   size_t peer_addr_size) {
  IWL_ERR(ctx, "%s() needs porting ... see fxbug.dev/36742\n", __func__);
  return ZX_ERR_NOT_SUPPORTED;
}

static zx_status_t mac_start_hw_scan(void* ctx, const wlan_hw_scan_config_t* scan_config) {
  struct iwl_mvm_vif* mvmvif = ctx;

  if (scan_config->scan_type != WLAN_HW_SCAN_TYPE_PASSIVE) {
    IWL_ERR(ctx, "Unsupported scan type: %d\n", scan_config->scan_type);
    return ZX_ERR_NOT_SUPPORTED;
  }

  return iwl_mvm_mac_hw_scan(mvmvif, scan_config);
}

wlanmac_protocol_ops_t wlanmac_ops = {
    .query = mac_query,
    .start = mac_start,
    .stop = mac_stop,
    .queue_tx = mac_queue_tx,
    .set_channel = mac_set_channel,
    .configure_bss = mac_configure_bss,
    .enable_beaconing = mac_enable_beaconing,
    .configure_beacon = mac_configure_beacon,
    .set_key = mac_set_key,
    .configure_assoc = mac_configure_assoc,
    .clear_assoc = mac_clear_assoc,
    .start_hw_scan = mac_start_hw_scan,
};

static void mac_unbind(void* ctx) {
  struct iwl_mvm_vif* mvmvif = ctx;

  if (!mvmvif->zxdev) {
    return;
  }

  device_unbind_reply(mvmvif->zxdev);
  mvmvif->zxdev = NULL;
}

static void mac_release(void* ctx) {
  struct iwl_mvm_vif* mvmvif = ctx;

  // Close the SME channel if it is NOT transferred to MLME yet.
  if (mvmvif->sme_channel != ZX_HANDLE_INVALID) {
    zx_handle_close(mvmvif->sme_channel);
    mvmvif->sme_channel = ZX_HANDLE_INVALID;
  }

  free(mvmvif);
}

zx_protocol_device_t device_mac_ops = {
    .version = DEVICE_OPS_VERSION,
    .unbind = mac_unbind,
    .release = mac_release,
};

/////////////////////////////////////       PHY       //////////////////////////////////////////////

static zx_status_t phy_query(void* ctx, wlanphy_impl_info_t* info) {
  struct iwl_trans* iwl_trans = ctx;
  struct iwl_mvm* mvm = iwl_trans_get_mvm(iwl_trans);
  if (!mvm || !info) {
    return ZX_ERR_INVALID_ARGS;
  }

  struct iwl_nvm_data* nvm_data = mvm->nvm_data;
  ZX_ASSERT(nvm_data);

  memset(info, 0, sizeof(*info));

  memcpy(info->wlan_info.mac_addr, nvm_data->hw_addr, sizeof(info->wlan_info.mac_addr));

  // TODO(fxbug.dev/36677): supports AP role
  info->wlan_info.mac_role = WLAN_INFO_MAC_ROLE_CLIENT;

  // TODO(43517): Better handling of driver features bits/flags
  info->wlan_info.supported_phys =
      WLAN_INFO_PHY_TYPE_DSSS | WLAN_INFO_PHY_TYPE_CCK | WLAN_INFO_PHY_TYPE_OFDM;
  // TODO(fxbug.dev/36683): supports HT (802.11n): WLAN_INFO_PHY_TYPE_HT
  // TODO(fxbug.dev/36684): suuports VHT (802.11ac): WLAN_INFO_PHY_TYPE_VHT

  info->wlan_info.driver_features = WLAN_INFO_DRIVER_FEATURE_SCAN_OFFLOAD;

  // TODO(43517): Better handling of driver features bits/flags
  info->wlan_info.caps = WLAN_INFO_HARDWARE_CAPABILITY_SHORT_PREAMBLE |
                         WLAN_INFO_HARDWARE_CAPABILITY_SPECTRUM_MGMT |
                         WLAN_INFO_HARDWARE_CAPABILITY_SHORT_SLOT_TIME;

  // Determine how many bands this adapter supports.
  wlan_info_band_t bands[WLAN_INFO_BAND_COUNT];
  info->wlan_info.bands_count = compose_band_list(nvm_data, bands);

  fill_band_infos(nvm_data, bands, info->wlan_info.bands_count, info->wlan_info.bands);

  return ZX_OK;
}

// This function is working with a PHY context ('ctx') to create a MAC interface.
static zx_status_t phy_create_iface(void* ctx, const wlanphy_impl_create_iface_req_t* req,
                                    uint16_t* out_iface_id) {
  struct iwl_trans* iwl_trans = ctx;
  struct iwl_mvm* mvm = iwl_trans_get_mvm(iwl_trans);
  zx_status_t ret = ZX_OK;

  if (!req) {
    IWL_ERR(mvm, "req is not given\n");
    return ZX_ERR_INVALID_ARGS;
  }

  if (req->sme_channel == ZX_HANDLE_INVALID) {
    IWL_ERR(mvm, "the given sme channel is invalid\n");
    return ZX_ERR_INVALID_ARGS;
  }

  if (!mvm) {
    IWL_ERR(mvm, "cannot obtain MVM from ctx=%p while creating interface\n", ctx);
    return ZX_ERR_INVALID_ARGS;
  }

  if (!out_iface_id) {
    IWL_ERR(mvm, "out_iface_id pointer is not given\n");
    return ZX_ERR_INVALID_ARGS;
  }

  mtx_lock(&mvm->mutex);

  // Find the first empty mvmvif slot.
  int idx;
  ret = iwl_mvm_find_free_mvmvif_slot(mvm, &idx);
  if (ret != ZX_OK) {
    IWL_ERR(mvm, "cannot find an empty slot for new MAC interface\n");
    goto unlock;
  }

  // Allocate a MAC context. This will be initialized once iwl_mvm_mac_add_interface() is called.
  // Note that once the 'mvmvif' is saved in the device ctx by device_add() below, it will live
  // as long as the device instance, and will be freed in mac_release().
  struct iwl_mvm_vif* mvmvif = calloc(1, sizeof(struct iwl_mvm_vif));
  if (!mvmvif) {
    ret = ZX_ERR_NO_MEMORY;
    goto unlock;
  }

  // Set default values into the mvmvif
  mvmvif->bss_conf.beacon_int = 100;
  mvmvif->bss_conf.dtim_period = 3;

  // Add MAC interface
  device_add_args_t mac_args = {
      .version = DEVICE_ADD_ARGS_VERSION,
      .name = "iwlwifi-wlanmac",
      .ctx = mvmvif,
      .ops = &device_mac_ops,
      .proto_id = ZX_PROTOCOL_WLANMAC,
      .proto_ops = &wlanmac_ops,
  };

  // Add this MAC device into the tree. The parent device is the PHY device.
  ret = device_add(iwl_trans->zxdev, &mac_args, &mvmvif->zxdev);
  if (ret == ZX_OK) {
    mvmvif->mvm = mvm;
    mvmvif->mac_role = req->role;
    mvmvif->sme_channel = req->sme_channel;
    ret = iwl_mvm_bind_mvmvif(mvm, idx, mvmvif);
    if (ret != ZX_OK) {
      IWL_ERR(ctx, "Cannot assign the new mvmvif to MVM: %s\n", zx_status_get_string(ret));
      // The allocated mvmvif instance will be freed at mac_release().
      goto unlock;
    }
    *out_iface_id = idx;

    // Only start FW MVM for the first device. The 'vif_count' will be increased in
    // iwl_mvm_mac_add_interface().
    if (mvm->vif_count == 0) {
      ret = __iwl_mvm_mac_start(mvm);
      if (ret != ZX_OK) {
        IWL_ERR(ctx, "Cannot start MVM MAC: %s\n", zx_status_get_string(ret));

        // If we fail to start the FW MVM, we shall unbind the mvmvif from the mvm. For the mvmvif
        // instance, it will be released in mac_release().
        iwl_mvm_unbind_mvmvif(mvm, idx);
        goto unlock;
      }
    }
  }

unlock:
  mtx_unlock(&mvm->mutex);

  return ret;
}

// This function is working with a PHY context ('ctx') to delete a MAC interface ('id').
// The 'id' is the value assigned by phy_create_iface().
static zx_status_t phy_destroy_iface(void* ctx, uint16_t id) {
  struct iwl_trans* iwl_trans = ctx;
  struct iwl_mvm* mvm = iwl_trans_get_mvm(iwl_trans);
  zx_status_t ret = ZX_OK;

  if (!mvm) {
    IWL_ERR(mvm, "cannot obtain MVM from ctx=%p while destroying interface (%d)\n", ctx, id);
    return ZX_ERR_INVALID_ARGS;
  }

  mtx_lock(&mvm->mutex);

  if (id >= MAX_NUM_MVMVIF) {
    IWL_ERR(mvm, "the interface id (%d) is invalid\n", id);
    ret = ZX_ERR_INVALID_ARGS;
    goto unlock;
  }

  struct iwl_mvm_vif* mvmvif = mvm->mvmvif[id];
  if (!mvmvif) {
    IWL_ERR(mvm, "the interface id (%d) has no MAC context\n", id);
    ret = ZX_ERR_NOT_FOUND;
    goto unlock;
  }

  // Only remove the device if it has been added and not removed yet.
  if (mvmvif->zxdev) {
    device_async_remove(mvmvif->zxdev);
  }

  // Unlink the 'mvmvif' from the 'mvm'. The zxdev will be removed in mac_unbind(),
  // and the memory of 'mvmvif' will be freed in mac_release().
  iwl_mvm_unbind_mvmvif(mvm, id);

  // the last MAC interface. stop the MVM to save power. 'vif_count' had been decreased in
  // iwl_mvm_mac_remove_interface().
  if (mvm->vif_count == 0) {
    __iwl_mvm_mac_stop(mvm);
  }

unlock:
  mtx_unlock(&mvm->mutex);

  return ret;
}

static zx_status_t phy_set_country(void* ctx, const wlanphy_country_t* country) {
  IWL_ERR(ctx, "%s() needs porting ...\n", __func__);
  return ZX_ERR_NOT_SUPPORTED;
}

static zx_status_t phy_get_country(void* ctx, wlanphy_country_t* out_country) {
  if (out_country == NULL) {
    return ZX_ERR_INVALID_ARGS;
  }

  IWL_ERR(ctx, "%s() needs porting ...\n", __func__);
  return ZX_ERR_NOT_SUPPORTED;
}

// PHY interface
wlanphy_impl_protocol_ops_t wlanphy_ops = {
    .query = phy_query,
    .create_iface = phy_create_iface,
    .destroy_iface = phy_destroy_iface,
    .set_country = phy_set_country,
    .get_country = phy_get_country,
};
