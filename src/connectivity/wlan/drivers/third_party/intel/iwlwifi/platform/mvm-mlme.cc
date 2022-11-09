// Copyright 2021 The Fuchsia Authors. All rights reserved.
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
//  |    mvm-mlme.cc    |
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
//   The steps below briefly describe how the 'mlme_channel' is used and transferred. In short,
//   the goal is to let SME and MLME have a channel to communicate with each other.
//
//   + After the devmgr (the device manager in wlanstack) detects a PHY device, the devmgr first
//     creates an SME instance in order to handle the MAC operation later. Then the devmgr
//     establishes a channel and passes one end to the SME instance.
//
//   + The devmgr requests the PHY device to create a MAC interface. In the request, the other end
//     of channel is passed to the driver.
//
//   + The driver's phy_create_iface() gets called, and saves the 'mlme_channel' handle in the newly
//     created MAC context.
//
//   + Once the MAC device is added, its mac_start() will be called. Then it will transfer the
//     'mlme_channel' handle back to the MLME.
//
//   + Now, both sides of channel (SME and MLME) can talk now.
//

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/platform/mvm-mlme.h"

#include <fidl/fuchsia.wlan.ieee80211/cpp/wire.h>
#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>
#include <stdio.h>
#include <string.h>
#include <zircon/status.h>

#include <algorithm>
#include <iterator>

#include <wlan/common/ieee80211.h>

#include "banjo/common.h"
#include "banjo/softmac.h"

extern "C" {
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/iwl-debug.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/mvm/mvm.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/mvm/sta.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/mvm/time-event.h"
}  // extern "C"

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/platform/ieee80211.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/platform/rcu.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/platform/scoped_utils.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/platform/wlan-softmac-device.h"

// Interface create waiting for delete to complete.
#define IWLWIFI_IF_DELETE_TIMEOUT (ZX_MSEC(100))

////////////////////////////////////  Helper Functions  ////////////////////////////////////////////

//
// Given a NVM data structure, and return the list of bands.
//
// Returns:
//   size_t: # of bands enabled in the NVM data.
//   bands[]: contains the list of enabled bands.
//
size_t compose_band_list(const struct iwl_nvm_data* nvm_data,
                         wlan_common_wire::WlanBand bands[fuchsia_wlan_common_MAX_BANDS]) {
  size_t bands_count = 0;

  if (nvm_data->sku_cap_band_24ghz_enable) {
    bands[bands_count++] = wlan_common_wire::WlanBand::kTwoGhz;
  }
  if (nvm_data->sku_cap_band_52ghz_enable) {
    bands[bands_count++] = wlan_common_wire::WlanBand::kFiveGhz;
  }
  ZX_ASSERT(bands_count <= wlan_common_wire::kMaxBands);

  return bands_count;
}

//
// Given a NVM data, copy the band and channel info into the 'wlan_softmac_band_capability_t'
// structure.
//
// - 'bands_count' is the number of bands in 'bands[]'.
// - 'band_infos[]' must have at least bands_count for this function to write.
//
void fill_band_cap_list(const struct iwl_nvm_data* nvm_data,
                        const wlan_common_wire::WlanBand* bands, size_t band_caps_count,
                        wlan_softmac_wire::WlanSoftmacBandCapability* band_cap_list) {
  ZX_ASSERT(band_caps_count <= std::size(nvm_data->bands));

  for (size_t band_idx = 0; band_idx < band_caps_count; ++band_idx) {
    wlan_common_wire::WlanBand band_id = bands[band_idx];
    const struct ieee80211_supported_band* sband =
        &nvm_data->bands[fidl::ToUnderlying(band_id)];  // source
    wlan_softmac_wire::WlanSoftmacBandCapability* band_cap =
        &band_cap_list[band_idx];  // destination

    band_cap->band = band_id;
    band_cap->ht_supported = sband->ht_cap.ht_supported;
    struct ieee80211_ht_cap_packed* ht_caps =
        reinterpret_cast<struct ieee80211_ht_cap_packed*>(band_cap->ht_caps.bytes.data());
    ht_caps->ht_capability_info = sband->ht_cap.cap;
    ht_caps->ampdu_params =
        (sband->ht_cap.ampdu_factor << IEEE80211_AMPDU_RX_LEN_SHIFT) |   // (64K - 1) bytes
        (sband->ht_cap.ampdu_density << IEEE80211_AMPDU_DENSITY_SHIFT);  // 8 us
    memcpy(&ht_caps->supported_mcs_set, &sband->ht_cap.mcs, sizeof(struct ieee80211_mcs_info));
    // TODO(fxbug.dev/36684): band_info->vht_caps =

    ZX_ASSERT(sband->n_bitrates <= std::size(band_cap->basic_rate_list));
    for (size_t rate_idx = 0; rate_idx < sband->n_bitrates; ++rate_idx) {
      band_cap->basic_rate_list[rate_idx] = cfg_rates_to_80211(sband->bitrates[rate_idx]);
    }
    band_cap->basic_rate_count = sband->n_bitrates;

    // Fill the channel list of this band.
    ZX_ASSERT(sband->n_channels <= std::size(band_cap->operating_channel_list));
    uint8_t* ch_list = band_cap->operating_channel_list.begin();
    for (size_t ch_idx = 0; ch_idx < sband->n_channels; ++ch_idx) {
      ch_list[ch_idx] = sband->channels[ch_idx].ch_num;
    }
    band_cap->operating_channel_count = static_cast<uint8_t>(sband->n_channels);
  }
}

/////////////////////////////////////       MAC       //////////////////////////////////////////////

zx_status_t mac_query(void* ctx, wlan_softmac_wire::WlanSoftmacInfo* info_out,
                      fidl::AnyArena& arena) {
  if (ctx == nullptr || info_out == nullptr) {
    IWL_ERR(mvmvif, "Empty parameter.");
    return ZX_ERR_INVALID_ARGS;
  }

  const auto mvmvif = reinterpret_cast<struct iwl_mvm_vif*>(ctx);
  auto builder = wlan_softmac_wire::WlanSoftmacInfo::Builder(arena);

  // The minimal set of wlan device capabilities, also stored as static array since it also back a
  // VectorView in wlan_softmac_wire::WlanSoftmacInfo.
  constexpr size_t kPhySize = 5;

  if (kPhySize > wlan_common_wire::kMaxSupportedPhyTypes) {
    IWL_ERR(mvmvif,
            "The phy type array size here is too large to return, please check. kPhySize: %zu, "
            "kMaxSupportedPhyTypes: %hhu",
            kPhySize, wlan_common_wire::kMaxSupportedPhyTypes);
    return ZX_ERR_OUT_OF_RANGE;
  }

  ZX_ASSERT(mvmvif->mvm);
  ZX_ASSERT(mvmvif->mvm->nvm_data);

  struct iwl_nvm_data* nvm_data = mvmvif->mvm->nvm_data;
  fidl::Array<uint8_t, wlan_ieee80211_wire::kMacAddrLen> sta_addr;
  memcpy(sta_addr.begin(), nvm_data->hw_addr, wlan_ieee80211_wire::kMacAddrLen);
  builder.sta_addr(sta_addr);
  switch (mvmvif->mac_role) {
    case WLAN_MAC_ROLE_CLIENT:
      builder.mac_role(wlan_common_wire::WlanMacRole::kClient);
      break;
    case WLAN_MAC_ROLE_AP:
      builder.mac_role(wlan_common_wire::WlanMacRole::kAp);
      break;
    case WLAN_MAC_ROLE_MESH:
      builder.mac_role(wlan_common_wire::WlanMacRole::kMesh);
      break;
    default:
      IWL_ERR(mvmvif, "Mac role not supported. The MAC role in mvmvif: %u", mvmvif->mac_role);
      return ZX_ERR_BAD_STATE;
  }

  std::vector<wlan_common_wire::WlanPhyType> phy_vec;
  phy_vec.push_back(wlan_common_wire::WlanPhyType::kDsss);
  phy_vec.push_back(wlan_common_wire::WlanPhyType::kHr);
  phy_vec.push_back(wlan_common_wire::WlanPhyType::kOfdm);
  phy_vec.push_back(wlan_common_wire::WlanPhyType::kErp);
  phy_vec.push_back(wlan_common_wire::WlanPhyType::kHt);

  builder.supported_phys(fidl::VectorView<wlan_common_wire::WlanPhyType>(arena, phy_vec));

  builder.hardware_capability(
      (uint32_t)wlan_common_wire::WlanSoftmacHardwareCapabilityBit::kShortPreamble |
      (uint32_t)wlan_common_wire::WlanSoftmacHardwareCapabilityBit::kSpectrumMgmt |
      (uint32_t)wlan_common_wire::WlanSoftmacHardwareCapabilityBit::kShortSlotTime);

  // Determine how many bands this adapter supports.
  wlan_common_wire::WlanBand bands[fuchsia_wlan_common_MAX_BANDS];
  wlan_softmac_wire::WlanSoftmacBandCapability band_caps_buffer[wlan_common_wire::kMaxBands];
  size_t band_caps_count = compose_band_list(nvm_data, bands);

  fill_band_cap_list(nvm_data, bands, band_caps_count, band_caps_buffer);
  auto band_caps_vec = std::vector<wlan_softmac_wire::WlanSoftmacBandCapability>(
      band_caps_buffer, band_caps_buffer + band_caps_count);
  builder.band_caps(
      fidl::VectorView<wlan_softmac_wire::WlanSoftmacBandCapability>(arena, band_caps_vec));
  *info_out = builder.Build();
  return ZX_OK;
}

void mac_query_discovery_support(wlan_common_wire::DiscoverySupport* out_resp) {
  // TODO(fxbug.dev/43517): Better handling of driver features
  out_resp->scan_offload.supported = true;
}

void mac_query_mac_sublayer_support(wlan_common_wire::MacSublayerSupport* out_resp) {
  *out_resp = {};
  out_resp->data_plane.data_plane_type = wlan_common_wire::DataPlaneType::kEthernetDevice;
  out_resp->device.mac_implementation_type = wlan_common_wire::MacImplementationType::kSoftmac;
}

void mac_query_security_support(wlan_common_wire::SecuritySupport* out_resp) {
  *out_resp = {};
  // TODO(43517): Better handling of driver features
  out_resp->mfp.supported = true;
  out_resp->sae.sme_handler_supported = true;
}

void mac_query_spectrum_management_support(wlan_common_wire::SpectrumManagementSupport* out_resp) {
  *out_resp = {};
  // TODO(43517): Better handling of driver features
  out_resp->dfs.supported = true;
}

zx_status_t mac_start(void* ctx, void* ifc_dev, zx_handle_t* out_mlme_channel) {
  const auto mvmvif = reinterpret_cast<struct iwl_mvm_vif*>(ctx);

  if (!ctx || !ifc_dev || !out_mlme_channel) {
    return ZX_ERR_INVALID_ARGS;
  }

  // Clear the output result first.
  *out_mlme_channel = ZX_HANDLE_INVALID;

  // The SME channel assigned in phy_create_iface() is gone.
  if (mvmvif->mlme_channel == ZX_HANDLE_INVALID) {
    IWL_ERR(mvmvif, "Invalid SME channel. The interface might have been bound already.\n");
    return ZX_ERR_ALREADY_BOUND;
  }

  // Transfer the handle to MLME. Also invalid the copy we hold to indicate that this interface has
  // been bound.
  *out_mlme_channel = mvmvif->mlme_channel;
  mvmvif->mlme_channel = ZX_HANDLE_INVALID;

  iwl_rcu_store(mvmvif->ifc.ctx, ifc_dev);
  iwl_rcu_store(mvmvif->ifc.recv, &mac_ifc_recv);
  iwl_rcu_store(mvmvif->ifc.scan_complete, &mac_ifc_scan_complete);

  zx_status_t ret = iwl_mvm_mac_add_interface(mvmvif);
  if (ret != ZX_OK) {
    IWL_ERR(mvmvif, "Cannot add MAC interface: %s\n", zx_status_get_string(ret));
    return ret;
  }

  return ret;
}

void mac_stop(struct iwl_mvm_vif* mvmvif) {
  zx_status_t ret = ZX_OK;

  if (mvmvif->phy_ctxt) {
    ret = iwl_mvm_remove_chanctx(mvmvif->mvm, mvmvif->phy_ctxt->id);
    if (ret != ZX_OK) {
      IWL_WARN(mvmvif, "Cannot remove chanctx: %s\n", zx_status_get_string(ret));
    }
  }

  ret = iwl_mvm_mac_remove_interface(mvmvif);
  if (ret != ZX_OK) {
    IWL_ERR(mvmvif, "Cannot remove MAC interface: %s\n", zx_status_get_string(ret));
  }
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

static zx_status_t remove_chanctx(struct iwl_mvm_vif* mvmvif) {
  zx_status_t ret;

  if (!mvmvif->phy_ctxt) {
    IWL_WARN(mvmvif, "PHY ctxt not set");
    return ZX_ERR_BAD_STATE;
  }
  // mvmvif->phy_ctxt will be cleared up in iwl_mvm_unassign_vif_chanctx(). So back up the phy
  // context ID and the chandef pointer for later use.

  auto phy_ctxt_id = mvmvif->phy_ctxt->id;
  auto chandef = &mvmvif->phy_ctxt->chandef;

  // Unbinding MAC and PHY contexts.
  ret = iwl_mvm_unassign_vif_chanctx(mvmvif);
  if (ret != ZX_OK) {
    IWL_ERR(mvmvif, "cannot unassign VIF channel context: %s\n", zx_status_get_string(ret));
    goto out;
  }

  ret = iwl_mvm_remove_chanctx(mvmvif->mvm, phy_ctxt_id);
  if (ret != ZX_OK) {
    IWL_ERR(mvmvif, "Cannot remove channel context: %s\n", zx_status_get_string(ret));
    goto out;
  }

  // Clear the chandef in mvm->phy_ctxts[] (was pointed by mvmvif->phy_ctxt->chandef) to indicate
  // this phy_ctxt is unused.
  memset(chandef, 0, sizeof(*chandef));

out:
  return ret;
}

// This is called right after SSID scan. The MLME tells this function the channel to tune in.
// This function configures the PHY context and binds the MAC to that PHY context.
zx_status_t mac_set_channel(struct iwl_mvm_vif* mvmvif, const wlan_channel_t* channel) {
  zx_status_t ret;

  IWL_INFO(mvmvif, "mac_set_channel(primary:%d, bandwidth:'%s', secondary:%d)\n", channel->primary,
           channel->cbw == CHANNEL_BANDWIDTH_CBW20        ? "20"
           : channel->cbw == CHANNEL_BANDWIDTH_CBW40      ? "40"
           : channel->cbw == CHANNEL_BANDWIDTH_CBW40BELOW ? "40-"
           : channel->cbw == CHANNEL_BANDWIDTH_CBW80      ? "80"
           : channel->cbw == CHANNEL_BANDWIDTH_CBW160     ? "160"
           : channel->cbw == CHANNEL_BANDWIDTH_CBW80P80   ? "80+80"
                                                          : "unknown",
           channel->secondary80);

  if (mvmvif->phy_ctxt && mvmvif->phy_ctxt->chandef.primary != 0) {
    // The PHY context is set (the RF is on a particular channel). Remove it first. Below code
    // will allocate a new one.
    ret = remove_chanctx(mvmvif);
    if (ret != ZX_OK) {
      IWL_ERR(mvmvif, "Cannot reset PHY context: %s\n", zx_status_get_string(ret));
      return ret;
    }
  }

  // Before we do anything, ensure the PHY context had been assigned to the mvmvif.
  ret = mac_ensure_phyctxt_valid(mvmvif);
  if (ret != ZX_OK) {
    IWL_ERR(mvmvif, "Cannot get an available chanctx: %s\n", zx_status_get_string(ret));
    return ret;
  }

  // Save the info.
  mvmvif->phy_ctxt->chandef = *channel;

  ret = iwl_mvm_change_chanctx(mvmvif->mvm, mvmvif->phy_ctxt->id, channel);
  if (ret != ZX_OK) {
    IWL_ERR(mvmvif, "Cannot change chanctx: %s\n", zx_status_get_string(ret));
    return ret;
  }

  ret = iwl_mvm_assign_vif_chanctx(mvmvif, channel);
  if (ret != ZX_OK) {
    IWL_ERR(mvmvif, "Cannot assign VIF chanctx: %s\n", zx_status_get_string(ret));
    return ret;
  }

  return ret;
}

// This is called after mac_set_channel(). The MAC (mvmvif) will be configured as a CLIENT role.
zx_status_t mac_configure_bss(struct iwl_mvm_vif* mvmvif,
                              const fuchsia_wlan_internal::wire::BssConfig* config) {
  zx_status_t ret = ZX_OK;

  IWL_INFO(mvmvif, "mac_configure_bss(bssid=" FMT_SSID ", type=%d, remote=%d)\n",
           FMT_SSID_BYTES(config->bssid.data(), sizeof(config->bssid)), config->bss_type,
           config->remote);

  if (config->bss_type != fuchsia_wlan_internal::BssType::kInfrastructure) {
    IWL_ERR(mvmvif, "invalid bss_type requested: %d\n", config->bss_type);
    return ZX_ERR_INVALID_ARGS;
  }

  {
    // Copy the BSSID info.
    auto lock = std::lock_guard(mvmvif->mvm->mutex);
    memcpy(mvmvif->bss_conf.bssid, config->bssid.data(), ETH_ALEN);
    memcpy(mvmvif->bssid, config->bssid.data(), ETH_ALEN);

    // Simulates the behavior of iwl_mvm_bss_info_changed_station().
    ret = iwl_mvm_mac_ctxt_changed(mvmvif, false, mvmvif->bssid);
    if (ret != ZX_OK) {
      IWL_ERR(mvmvif, "cannot set BSSID: %s\n", zx_status_get_string(ret));
      return ret;
    }
  }

  // Ask the firmware to pay attention for beacon.
  // Note that this would add TIME_EVENT as well.
  iwl_mvm_mac_mgd_prepare_tx(mvmvif->mvm, mvmvif, IWL_MVM_TE_SESSION_PROTECTION_MAX_TIME_MS);

  return ZX_OK;
}

// This function is to revert what mac_configure_bss() does.
zx_status_t mac_unconfigure_bss(struct iwl_mvm_vif* mvmvif) {
  zx_status_t ret = ZX_OK;

  {
    // To simulate the behavior that iwl_mvm_bss_info_changed_station() would do for disassocitaion.
    auto lock = std::lock_guard(mvmvif->mvm->mutex);
    memset(mvmvif->bss_conf.bssid, 0, ETH_ALEN);
    memset(mvmvif->bssid, 0, ETH_ALEN);
    // This will take the cleared BSSID from bss_conf and update the firmware.
    ret = iwl_mvm_mac_ctxt_changed(mvmvif, false, NULL);
    if (ret != ZX_OK) {
      IWL_ERR(mvm, "failed to update MAC (clear after unassoc)\n");
      return ret;
    }
  }

  return ZX_OK;
}

zx_status_t mac_enable_beaconing(void* ctx, const wlan_softmac_wire::WlanBcnConfig* bcn_cfg) {
  IWL_ERR(ctx, "%s() needs porting ... see fxbug.dev/36742\n", __func__);
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t mac_configure_beacon(void* ctx,
                                 const wlan_softmac_wire::WlanTxPacket* packet_template) {
  IWL_ERR(ctx, "%s() needs porting ... see fxbug.dev/36742\n", __func__);
  return ZX_ERR_NOT_SUPPORTED;
}

// Set the association result to the firmware.
//
// The current mac context is set by mac_configure_bss() with default values.
//   TODO(fxbug.dev/36684): supports VHT (802.11ac)
//
zx_status_t mac_configure_assoc(
    struct iwl_mvm_vif* mvmvif,
    const fuchsia_hardware_wlan_associnfo::wire::WlanAssocCtx* assoc_ctx) {
  zx_status_t ret = ZX_OK;
  IWL_INFO(ctx, "Associating ...\n");

  // TODO(fxbug.dev/86715): this RCU-unprotected access is safe as deletions from the map are
  // RCU-synchronized from API calls to mac_stop() in this same thread.
  struct iwl_mvm_sta* mvm_sta = mvmvif->mvm->fw_id_to_mac_id[mvmvif->ap_sta_id];
  if (!mvm_sta) {
    IWL_ERR(mvmvif, "sta info is not set before association.\n");
    ret = ZX_ERR_BAD_STATE;
    return ret;
  }

  // Save band info into interface struct for future usage.
  mvmvif->phy_ctxt->band = iwl_mvm_get_channel_band(assoc_ctx->channel.primary);
  switch (assoc_ctx->channel.cbw) {
    case fuchsia_wlan_common::ChannelBandwidth::kCbw20:
      mvm_sta->bw = CHANNEL_BANDWIDTH_CBW20;
      break;
    case fuchsia_wlan_common::ChannelBandwidth::kCbw40:
      mvm_sta->bw = CHANNEL_BANDWIDTH_CBW40;
      break;
    case fuchsia_wlan_common::ChannelBandwidth::kCbw40Below:
      mvm_sta->bw = CHANNEL_BANDWIDTH_CBW40BELOW;
      break;
    case fuchsia_wlan_common::ChannelBandwidth::kCbw80:
      mvm_sta->bw = CHANNEL_BANDWIDTH_CBW80;
      break;
    case fuchsia_wlan_common::ChannelBandwidth::kCbw160:
      mvm_sta->bw = CHANNEL_BANDWIDTH_CBW160;
      break;
    case fuchsia_wlan_common::ChannelBandwidth::kCbw80P80:
      mvm_sta->bw = CHANNEL_BANDWIDTH_CBW80P80;
      break;
    default:
      IWL_ERR(mvmvif, "Unknown channel bandwidth.");
      return ZX_ERR_INVALID_ARGS;
  }
  // Record the intersection of AP and station supported rate to mvm_sta.
  ZX_ASSERT(assoc_ctx->rates_cnt <= sizeof(mvm_sta->supp_rates));
  memcpy(mvm_sta->supp_rates, assoc_ctx->rates.data(), assoc_ctx->rates_cnt);

  // Copy HT related fields from fuchsia_hardware_wlan_associnfo::wire::WlanAssocCtx.
  mvm_sta->support_ht = assoc_ctx->has_ht_cap;
  if (assoc_ctx->has_ht_cap) {
    memcpy(&mvm_sta->ht_cap, assoc_ctx->ht_cap.bytes.data(), sizeof(ht_capabilities_t));
  }

  // Change the station states step by step.
  ret = iwl_mvm_mac_sta_state(mvmvif, mvm_sta, IWL_STA_NONE, IWL_STA_AUTH);
  if (ret != ZX_OK) {
    IWL_ERR(mvmvif, "cannot set state from NONE to AUTH: %s\n", zx_status_get_string(ret));
    return ret;
  }

  ret = iwl_mvm_mac_sta_state(mvmvif, mvm_sta, IWL_STA_AUTH, IWL_STA_ASSOC);
  if (ret != ZX_OK) {
    IWL_ERR(mvmvif, "cannot set state from AUTH to ASSOC: %s\n", zx_status_get_string(ret));
    return ret;
  }

  ret = iwl_mvm_mac_sta_state(mvmvif, mvm_sta, IWL_STA_ASSOC, IWL_STA_AUTHORIZED);
  if (ret != ZX_OK) {
    IWL_ERR(mvmvif, "cannot set state from ASSOC to AUTHORIZED: %s\n", zx_status_get_string(ret));
    return ret;
  }

  {
    auto lock = std::lock_guard(mvmvif->mvm->mutex);

    // Update the MAC context in the firmware.
    mvmvif->bss_conf.assoc = true;
    mvmvif->bss_conf.listen_interval = assoc_ctx->listen_interval;
    ret = iwl_mvm_mac_ctxt_changed(mvmvif, false, NULL);
    if (ret != ZX_OK) {
      IWL_ERR(mvmvif, "cannot update MAC context in the firmware: %s\n", zx_status_get_string(ret));
      return ret;
    }

    ret = iwl_mvm_remove_time_event(mvmvif, &mvmvif->time_event_data);
    if (ret != ZX_OK) {
      IWL_ERR(mvmvif, "cannot remove time event: %s\n", zx_status_get_string(ret));
      return ret;
    }
  }

  // Tell firmware to pass multicast packets to driver.
  iwl_mvm_configure_filter(mvmvif->mvm);

  // TODO(43218): support multiple interfaces. Need to port iwl_mvm_update_quotas() in mvm/quota.c.
  // TODO(56093): support low latency in struct iwl_time_quota_data.
  return ZX_OK;
}

zx_status_t mac_clear_assoc(struct iwl_mvm_vif* mvmvif,
                            const uint8_t peer_addr[fuchsia_wlan_ieee80211::wire::kMacAddrLen]) {
  IWL_INFO(ctx, "Disassociating ...\n");

  zx_status_t ret = ZX_OK;

  {
    auto lock = std::lock_guard(mvmvif->mvm->mutex);
    // Remove Time event (in case assoc failed)
    ret = iwl_mvm_remove_time_event(mvmvif, &mvmvif->time_event_data);
    if (ret != ZX_OK) {
      IWL_ERR(mvmvif, "cannot remove time event: %s\n", zx_status_get_string(ret));
    }
  }

  ret = mac_unconfigure_bss(mvmvif);
  if (ret != ZX_OK) {
    return ret;
  }

  return remove_chanctx(mvmvif);
}

zx_status_t mac_start_passive_scan(
    void* ctx, const wlan_softmac_wire::WlanSoftmacPassiveScanArgs* passive_scan_args,
    uint64_t* out_scan_id) {
  const auto mvmvif = reinterpret_cast<struct iwl_mvm_vif*>(ctx);
  if (!passive_scan_args->has_channels()) {
    IWL_ERR(mvmvif, "Required parameter missed: channels.");
    return ZX_ERR_INVALID_ARGS;
  }

  struct iwl_mvm_scan_req scan_req = {
      .channels_list = passive_scan_args->channels().data(),
      .channels_count = passive_scan_args->channels().count(),
      .ssids = NULL,
      .ssids_count = 0,
      .mac_header_buffer = NULL,
      .mac_header_size = 0,
      .ies_buffer = NULL,
      .ies_size = 0,
  };
  return iwl_mvm_mac_hw_scan(mvmvif, &scan_req, out_scan_id);
}

zx_status_t mac_start_active_scan(
    void* ctx, const wlan_softmac_wire::WlanSoftmacActiveScanArgs* active_scan_args,
    uint64_t* out_scan_id) {
  const auto mvmvif = reinterpret_cast<struct iwl_mvm_vif*>(ctx);
  zx_status_t ret = ZX_OK;
  if (!(active_scan_args->has_channels() && active_scan_args->has_mac_header() &&
        active_scan_args->has_ies() && active_scan_args->has_ssids())) {
    IWL_ERR(mvmvif, "WlanSoftmacActiveScanArgs missing fields: %s %s %s %s",
            active_scan_args->has_channels() ? "" : "channels",
            active_scan_args->has_mac_header() ? "" : "mac_header",
            active_scan_args->has_ies() ? "" : "ies", active_scan_args->has_ssids() ? "" : "ssids");
    return ZX_ERR_INVALID_ARGS;
  }
  struct iwl_mvm_scan_req scan_req = {
      .channels_list = active_scan_args->channels().data(),
      .channels_count = active_scan_args->channels().count(),
      .mac_header_buffer = active_scan_args->mac_header().data(),
      .mac_header_size = active_scan_args->mac_header().count(),
      .ies_buffer = active_scan_args->ies().data(),
      .ies_size = active_scan_args->ies().count(),
  };

  // If the ssid list in wlanmac_active_scan_args_t is empty, set scan_req for wildcard active scan.
  if (active_scan_args->ssids().count() == 0) {
    scan_req.ssids_count = 1;
    scan_req.ssids = (struct iwl_mvm_ssid*)calloc(1, sizeof(struct iwl_mvm_ssid));
    scan_req.ssids[0].ssid_len = 0;
  } else {
    scan_req.ssids_count = active_scan_args->ssids().count();
    scan_req.ssids =
        (struct iwl_mvm_ssid*)calloc(scan_req.ssids_count, sizeof(struct iwl_mvm_ssid));
    for (uint32_t i = 0; i < scan_req.ssids_count; ++i) {
      scan_req.ssids[i].ssid_len = (active_scan_args->ssids().data())[i].len;
      memcpy(scan_req.ssids[i].ssid_data, (active_scan_args->ssids().data())[i].data.data(),
             scan_req.ssids[i].ssid_len);
    }
  }

  ret = iwl_mvm_mac_hw_scan(mvmvif, &scan_req, out_scan_id);
  free(scan_req.ssids);
  return ret;
}

zx_status_t mac_init(void* ctx, struct iwl_trans* drvdata, zx_device_t* zxdev, uint16_t idx) {
  zx_status_t status = phy_start_iface(drvdata, zxdev, idx);
  if (status != ZX_OK) {
    // Freeing of resources allocated in phy_create_iface() will happen in mac_release().
    IWL_ERR(this, "%s() failed phy start: %s\n", __func__, zx_status_get_string(status));
  }
  return status;
}

void mac_unbind(void* ctx) {
  const auto mvmvif = reinterpret_cast<struct iwl_mvm_vif*>(ctx);

  if (!mvmvif->zxdev) {
    IWL_ERR(nullptr, "mac_unbind(): no zxdev\n");
    return;
  }

  mvmvif->zxdev = nullptr;
}

void mac_release(void* ctx) {
  const auto mvmvif = reinterpret_cast<struct iwl_mvm_vif*>(ctx);

  // Close the SME channel if it is NOT transferred to MLME yet.
  if (mvmvif->mlme_channel != ZX_HANDLE_INVALID) {
    zx_handle_close(mvmvif->mlme_channel);
    mvmvif->mlme_channel = ZX_HANDLE_INVALID;
  }

  mvmvif->mvm->if_delete_in_progress = false;
  sync_completion_signal(&mvmvif->mvm->wait_for_delete);
  iwl_rcu_free_sync(mvmvif->mvm->dev, mvmvif);
}

/////////////////////////////////////       PHY //////////////////////////////////////////////

zx_status_t phy_get_supported_mac_roles(
    void* ctx,
    fuchsia_wlan_common::WlanMacRole
        out_supported_mac_roles_list[fuchsia_wlan_common_MAX_SUPPORTED_MAC_ROLES],
    uint8_t* out_supported_mac_roles_count) {
  const auto iwl_trans = reinterpret_cast<struct iwl_trans*>(ctx);
  struct iwl_mvm* mvm = iwl_trans_get_mvm(iwl_trans);
  if (nullptr == mvm || nullptr == out_supported_mac_roles_list ||
      nullptr == out_supported_mac_roles_count) {
    return ZX_ERR_INVALID_ARGS;
  }

  struct iwl_nvm_data* nvm_data = mvm->nvm_data;
  ZX_ASSERT(nvm_data);

  // TODO(fxbug.dev/36677): supports AP role
  out_supported_mac_roles_list[0] = fuchsia_wlan_common::WlanMacRole::kClient;
  *out_supported_mac_roles_count = 1;
  return ZX_OK;
}

// This function is working with a PHY context ('ctx') to create a MAC interface.
zx_status_t phy_create_iface(void* ctx, const wlanphy_impl_create_iface_req_t* req,
                             uint16_t* out_iface_id) {
  const auto iwl_trans = reinterpret_cast<struct iwl_trans*>(ctx);
  struct iwl_mvm* mvm = iwl_trans_get_mvm(iwl_trans);
  struct iwl_mvm_vif* mvmvif = nullptr;
  zx_status_t ret = ZX_OK;

  if (!req) {
    IWL_ERR(mvm, "req is not given\n");
    return ZX_ERR_INVALID_ARGS;
  }

  if (req->mlme_channel == ZX_HANDLE_INVALID) {
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

  // wait for IF delete to complete for up to 50 msecs.
  if (mvm->if_delete_in_progress) {
    ret = sync_completion_wait(&mvm->wait_for_delete, IWLWIFI_IF_DELETE_TIMEOUT);
    if (ret != ZX_OK) {
      IWL_ERR(mvm, "IF delete is still in progress, create failed");
      return ret;
    }
  }

  auto lock = std::lock_guard(mvm->mutex);

  // Find the first empty mvmvif slot.
  int idx;
  ret = iwl_mvm_find_free_mvmvif_slot(mvm, &idx);
  if (ret != ZX_OK) {
    IWL_ERR(mvm, "cannot find an empty slot for new MAC interface\n");
    return ret;
  }

  // Allocate a MAC context. This will be initialized once iwl_mvm_mac_add_interface() is called.
  // Note that once the 'mvmvif' is saved in the device ctx by device_add() below, it will live
  // as long as the device instance, and will be freed in mac_release().
  mvmvif = reinterpret_cast<struct iwl_mvm_vif*>(calloc(1, sizeof(struct iwl_mvm_vif)));
  if (!mvmvif) {
    ret = ZX_ERR_NO_MEMORY;
    return ret;
  }

  // Set default values into the mvmvif
  mvmvif->bss_conf.beacon_int = 100;
  mvmvif->bss_conf.dtim_period = 3;

  mvmvif->mvm = mvm;
  mvmvif->mac_role = req->role;
  mvmvif->mlme_channel = req->mlme_channel;
  ret = iwl_mvm_bind_mvmvif(mvm, idx, mvmvif);
  if (ret != ZX_OK) {
    IWL_ERR(ctx, "Cannot assign the new mvmvif to MVM: %s\n", zx_status_get_string(ret));
    // The allocated mvmvif instance will be freed at mac_release().
    return ret;
  }

  *out_iface_id = idx;
  return ZX_OK;
}

// If there are failures post phy_create_iface() and before phy_start_iface()
// is successful, then this is the API to undo phy_create_iface().
void phy_create_iface_undo(struct iwl_trans* iwl_trans, uint16_t idx) {
  struct iwl_mvm* mvm = iwl_trans_get_mvm(iwl_trans);

  struct iwl_mvm_vif* mvmvif = nullptr;
  {
    // Unbind and free the mvmvif interface.
    auto lock = std::lock_guard(mvm->mutex);
    mvmvif = mvm->mvmvif[idx];
    iwl_mvm_unbind_mvmvif(mvm, idx);
  }

  free(mvmvif);
}

zx_status_t phy_start_iface(void* ctx, zx_device_t* zxdev, uint16_t idx) {
  const auto iwl_trans = reinterpret_cast<struct iwl_trans*>(ctx);
  struct iwl_mvm* mvm = iwl_trans_get_mvm(iwl_trans);
  zx_status_t ret = ZX_OK;

  if (idx >= MAX_NUM_MVMVIF) {
    IWL_ERR(mvm, "Interface index is too large (%d). expect less than %d\n", idx, MAX_NUM_MVMVIF);
    return ZX_ERR_INVALID_ARGS;
  }

  auto lock = std::lock_guard(mvm->mutex);
  struct iwl_mvm_vif* mvmvif = mvm->mvmvif[idx];
  mvmvif->zxdev = zxdev;

  // Only start FW MVM for the first device. The 'vif_count' will be increased in
  // iwl_mvm_mac_add_interface().
  if (mvm->vif_count == 0) {
    ret = __iwl_mvm_mac_start(mvm);
    if (ret != ZX_OK) {
      IWL_ERR(ctx, "Cannot start MVM MAC: %s\n", zx_status_get_string(ret));

      // If we fail to start the FW MVM, we shall unbind the mvmvif from the mvm. For the mvmvif
      // instance, it will be released in mac_release().
      // TODO: It does not look clean to have unbind happen here.
      iwl_mvm_unbind_mvmvif(mvm, idx);

      return ret;
    }

    // Once MVM is started, copy the MAC address to mvmvif.
    struct iwl_nvm_data* nvm_data = mvmvif->mvm->nvm_data;
    memcpy(mvmvif->addr, nvm_data->hw_addr, ETH_ALEN);
  }

  return ZX_OK;
}

// This function is working with a PHY context ('ctx') to delete a MAC interface ('id').
// The 'id' is the value assigned by phy_create_iface().
zx_status_t phy_destroy_iface(void* ctx, uint16_t id) {
  const auto iwl_trans = reinterpret_cast<struct iwl_trans*>(ctx);
  struct iwl_mvm* mvm = iwl_trans_get_mvm(iwl_trans);
  struct iwl_mvm_vif* mvmvif = nullptr;

  if (!mvm) {
    IWL_ERR(mvm, "cannot obtain MVM from ctx=%p while destroying interface (%d)\n", ctx, id);
    return ZX_ERR_INVALID_ARGS;
  }

  if (id >= MAX_NUM_MVMVIF) {
    IWL_ERR(mvm, "the interface id (%d) is invalid\n", id);
    return ZX_ERR_INVALID_ARGS;
  }

  {
    auto lock = std::lock_guard(mvm->mutex);
    mvmvif = mvm->mvmvif[id];
    if (!mvmvif) {
      IWL_ERR(mvm, "the interface id (%d) has no MAC context\n", id);
      return ZX_ERR_NOT_FOUND;
    }
    // Mark this interface as being deleted. This is used to prevent any OPs on the interface.
    mvmvif->delete_in_progress = true;
  }
  if (mvmvif->ap_sta_id != IWL_MVM_INVALID_STA) {
    // Client interface is in connected state. Clean it up before deleting the interface.
    // Attempting to take down the interface with the client connected causes FW to crash
    // sometimes.
    struct iwl_mvm_sta* mvm_sta = mvmvif->mvm->fw_id_to_mac_id[mvmvif->ap_sta_id];
    if (mvm_sta) {
      zx_status_t ret;

      IWL_INFO(mvmvif, "STA found during delete, needs to be cleaned up");
      mvmvif->bss_conf.assoc = false;

      // Below are to simulate the behavior of iwl_mvm_bss_info_changed_station().
      ret = iwl_mvm_mac_sta_state(mvmvif, mvm_sta, IWL_STA_AUTHORIZED, IWL_STA_ASSOC);
      if (ret != ZX_OK) {
        IWL_ERR(mvmvif, "cannot set state from AUTHORIZED to ASSOC: %s\n",
                zx_status_get_string(ret));
      }
      ret = iwl_mvm_mac_sta_state(mvmvif, mvm_sta, IWL_STA_ASSOC, IWL_STA_AUTH);
      if (ret != ZX_OK) {
        IWL_ERR(mvmvif, "cannot set state from ASSOC to AUTH: %s\n", zx_status_get_string(ret));
      }
      ret = iwl_mvm_mac_sta_state(mvmvif, mvm_sta, IWL_STA_AUTH, IWL_STA_NONE);
      if (ret != ZX_OK) {
        IWL_ERR(mvmvif, "cannot set state from AUTH to NONE: %s\n", zx_status_get_string(ret));
      }
      ret = iwl_mvm_mac_sta_state(mvmvif, mvm_sta, IWL_STA_NONE, IWL_STA_NOTEXIST);
      if (ret != ZX_OK) {
        IWL_ERR(mvmvif, "cannot set state from NONE to NOTEXIST: %s\n", zx_status_get_string(ret));
      }
      if (mac_clear_assoc(mvmvif, mvmvif->addr) != ZX_OK) {
        IWL_ERR(mvmvif, "Unable to clear assoc during iface destroy");
      }
    } else {
      IWL_WARN(mvmvif, "Sta id: %d set but mvm_sta not set", mvmvif->ap_sta_id);
    }
  }

  {
    auto lock = std::lock_guard(mvm->mutex);
    // attempt to stop any ongoing scans.
    iwl_mvm_scan_stop(mvm, IWL_MVM_SCAN_REGULAR, true);

    // To serialize IF delete and create.  phy_create_iface() waits until
    // this flag is cleared before proceeeding. This flag is cleared in mac_stop().
    mvm->if_delete_in_progress = true;

    // Unlink the 'mvmvif' from the 'mvm'. The zxdev will be removed in mac_unbind(),
    // and the memory of 'mvmvif' will be freed in mac_release().
    iwl_mvm_unbind_mvmvif(mvm, id);

    // the last MAC interface. stop the MVM to save power. 'vif_count' had been decreased in
    // iwl_mvm_mac_remove_interface().
    if (mvm->vif_count == 0) {
      __iwl_mvm_mac_stop(mvm);
    }
  }

  device_async_remove(mvmvif->zxdev);
  return ZX_OK;
}

zx_status_t phy_set_country(void* ctx, const wlanphy_country_t* country) {
  IWL_ERR(ctx, "%s() needs porting ...\n", __func__);
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t phy_get_country(void* ctx, wlanphy_country_t* out_country) {
  const auto iwl_trans = reinterpret_cast<struct iwl_trans*>(ctx);
  struct iwl_mvm* mvm = iwl_trans_get_mvm(iwl_trans);

  if (out_country == nullptr) {
    return ZX_ERR_INVALID_ARGS;
  }

  bool changed;
  mtx_lock(&mvm->mutex);
  zx_status_t ret = iwl_mvm_get_current_regdomain(mvm, &changed, out_country);
  mtx_unlock(&mvm->mutex);
  return ret;
}

void mac_ifc_recv(void* ctx, const wlan_rx_packet_t* rx_packet) {
  wlan_softmac_wire::WlanRxPacket fidl_rx_packet;
  // Unconst the buffer pointer.
  fidl_rx_packet.mac_frame = fidl::VectorView<uint8_t>::FromExternal(
      (uint8_t*)(rx_packet->mac_frame_buffer), rx_packet->mac_frame_size);

  auto& fidl_info = fidl_rx_packet.info;
  const auto& banjo_info = rx_packet->info;

  // TODO(fxbug.dev/109461): Seek a way to remove the conversion below.
  fidl_info.rx_flags = banjo_info.rx_flags;
  fidl_info.valid_fields = banjo_info.valid_fields;

  switch (banjo_info.phy) {
    case WLAN_PHY_TYPE_DSSS:
      fidl_info.phy = fuchsia_wlan_common::wire::WlanPhyType::kDsss;
      break;
    case WLAN_PHY_TYPE_HR:
      fidl_info.phy = fuchsia_wlan_common::wire::WlanPhyType::kHr;
      break;
    case WLAN_PHY_TYPE_OFDM:
      fidl_info.phy = fuchsia_wlan_common::wire::WlanPhyType::kOfdm;
      break;
    case WLAN_PHY_TYPE_ERP:
      fidl_info.phy = fuchsia_wlan_common::wire::WlanPhyType::kErp;
      break;
    case WLAN_PHY_TYPE_HT:
      fidl_info.phy = fuchsia_wlan_common::wire::WlanPhyType::kHt;
      break;
    case WLAN_PHY_TYPE_DMG:
      fidl_info.phy = fuchsia_wlan_common::wire::WlanPhyType::kDmg;
      break;
    case WLAN_PHY_TYPE_VHT:
      fidl_info.phy = fuchsia_wlan_common::wire::WlanPhyType::kVht;
      break;
    case WLAN_PHY_TYPE_TVHT:
      fidl_info.phy = fuchsia_wlan_common::wire::WlanPhyType::kTvht;
      break;
    case WLAN_PHY_TYPE_S1G:
      fidl_info.phy = fuchsia_wlan_common::wire::WlanPhyType::kS1G;
      break;
    case WLAN_PHY_TYPE_CDMG:
      fidl_info.phy = fuchsia_wlan_common::wire::WlanPhyType::kCdmg;
      break;
    case WLAN_PHY_TYPE_CMMG:
      fidl_info.phy = fuchsia_wlan_common::wire::WlanPhyType::kCmmg;
      break;
    case WLAN_PHY_TYPE_HE:
      fidl_info.phy = fuchsia_wlan_common::wire::WlanPhyType::kHe;
      break;
    default:
      IWL_ERR(nullptr, "Invalid phy type, dropping the packet.");
      return;
  }
  fidl_info.data_rate = banjo_info.data_rate;
  fidl_info.channel.primary = banjo_info.channel.primary;
  switch (banjo_info.channel.cbw) {
    case CHANNEL_BANDWIDTH_CBW20:
      fidl_info.channel.cbw = fuchsia_wlan_common::wire::ChannelBandwidth::kCbw20;
      break;
    case CHANNEL_BANDWIDTH_CBW40:
      fidl_info.channel.cbw = fuchsia_wlan_common::wire::ChannelBandwidth::kCbw40;
      break;
    case CHANNEL_BANDWIDTH_CBW40BELOW:
      fidl_info.channel.cbw = fuchsia_wlan_common::wire::ChannelBandwidth::kCbw40Below;
      break;
    case CHANNEL_BANDWIDTH_CBW80:
      fidl_info.channel.cbw = fuchsia_wlan_common::wire::ChannelBandwidth::kCbw80;
      break;
    case CHANNEL_BANDWIDTH_CBW160:
      fidl_info.channel.cbw = fuchsia_wlan_common::wire::ChannelBandwidth::kCbw160;
      break;
    case CHANNEL_BANDWIDTH_CBW80P80:
      fidl_info.channel.cbw = fuchsia_wlan_common::wire::ChannelBandwidth::kCbw80P80;
      break;
    default:
      IWL_ERR(nullptr, "Bandwidth is not supported, dropping the packet.");
      return;
  }
  fidl_info.channel.secondary80 = banjo_info.channel.secondary80;
  fidl_info.mcs = banjo_info.mcs;
  fidl_info.rssi_dbm = banjo_info.rssi_dbm;
  fidl_info.snr_dbh = banjo_info.snr_dbh;

  static_cast<wlan::iwlwifi::WlanSoftmacDevice*>(ctx)->Recv(&fidl_rx_packet);
}

void mac_ifc_scan_complete(void* ctx, const zx_status_t status, const uint64_t scan_id) {
  static_cast<wlan::iwlwifi::WlanSoftmacDevice*>(ctx)->ScanComplete(status, scan_id);
}
