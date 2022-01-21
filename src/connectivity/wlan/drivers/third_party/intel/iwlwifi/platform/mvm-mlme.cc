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
#include <fuchsia/hardware/wlan/softmac/c/banjo.h>
#include <fuchsia/wlan/internal/c/banjo.h>
#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>
#include <stdio.h>
#include <string.h>
#include <zircon/status.h>

#include <algorithm>
#include <iterator>

#include <wlan/common/ieee80211.h>

extern "C" {
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/iwl-debug.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/mvm/mvm.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/mvm/sta.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/mvm/time-event.h"
}  // extern "C"

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/platform/ieee80211.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/platform/rcu.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/platform/scoped_utils.h"

namespace {

// IEEE 802.11-2016 3.2 (c.f. "vendor organizationally unique identifier")
constexpr uint8_t kIeeeOui[] = {0x00, 0x0F, 0xAC};

}  // namespace

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
    bands[bands_count++] = WLAN_INFO_BAND_TWO_GHZ;
  }
  if (nvm_data->sku_cap_band_52ghz_enable) {
    bands[bands_count++] = WLAN_INFO_BAND_FIVE_GHZ;
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
  ZX_ASSERT(bands_count <= std::size(nvm_data->bands));

  for (size_t band_idx = 0; band_idx < bands_count; ++band_idx) {
    wlan_info_band_t band_id = bands[band_idx];
    const struct ieee80211_supported_band* sband = &nvm_data->bands[band_id];  // source
    wlan_info_band_info_t* band_info = &band_infos[band_idx];                  // destination

    band_info->band = band_id;
    band_info->ht_supported = sband->ht_cap.ht_supported;
    band_info->ht_caps.ht_capability_info = sband->ht_cap.cap;
    band_info->ht_caps.ampdu_params =
        (sband->ht_cap.ampdu_factor << IEEE80211_AMPDU_RX_LEN_SHIFT) |   // (64K - 1) bytes
        (sband->ht_cap.ampdu_density << IEEE80211_AMPDU_DENSITY_SHIFT);  // 8 us
    memcpy(&band_info->ht_caps.supported_mcs_set, &sband->ht_cap.mcs,
           sizeof(struct ieee80211_mcs_info));
    // TODO(36684): band_info->vht_caps =

    ZX_ASSERT(sband->n_bitrates <= static_cast<int>(std::size(band_info->rates)));
    for (int rate_idx = 0; rate_idx < sband->n_bitrates; ++rate_idx) {
      band_info->rates[rate_idx] = cfg_rates_to_80211(sband->bitrates[rate_idx]);
    }

    // Fill the channel list of this band.
    wlan_info_channel_list_t* ch_list = &band_info->supported_channels;
    switch (band_info->band) {
      case WLAN_INFO_BAND_TWO_GHZ:
        ch_list->base_freq = 2407;
        break;
      case WLAN_INFO_BAND_FIVE_GHZ:
        ch_list->base_freq = 5000;
        break;
      default:
        ZX_ASSERT(0);  // Unknown band ID.
        break;
    }
    ZX_ASSERT(sband->n_channels <= static_cast<int>(std::size(ch_list->channels)));
    for (int ch_idx = 0; ch_idx < sband->n_channels; ++ch_idx) {
      ch_list->channels[ch_idx] = sband->channels[ch_idx].ch_num;
    }
  }
}

/////////////////////////////////////       MAC       //////////////////////////////////////////////

zx_status_t mac_query(void* ctx, uint32_t options, wlan_softmac_info_t* info) {
  const auto mvmvif = reinterpret_cast<struct iwl_mvm_vif*>(ctx);

  if (!ctx || !info) {
    return ZX_ERR_INVALID_ARGS;
  }
  memset(info, 0, sizeof(*info));

  ZX_ASSERT(mvmvif->mvm);
  ZX_ASSERT(mvmvif->mvm->nvm_data);
  struct iwl_nvm_data* nvm_data = mvmvif->mvm->nvm_data;

  memcpy(info->sta_addr, nvm_data->hw_addr, sizeof(info->sta_addr));
  info->mac_role = mvmvif->mac_role;
  // TODO(43517): Better handling of driver features bits/flags
  info->driver_features = WLAN_INFO_DRIVER_FEATURE_SCAN_OFFLOAD;
  info->supported_phys = WLAN_INFO_PHY_TYPE_DSSS | WLAN_INFO_PHY_TYPE_HR | WLAN_INFO_PHY_TYPE_OFDM |
                         WLAN_INFO_PHY_TYPE_HT;
  info->caps = WLAN_INFO_HARDWARE_CAPABILITY_SHORT_PREAMBLE |
               WLAN_INFO_HARDWARE_CAPABILITY_SPECTRUM_MGMT |
               WLAN_INFO_HARDWARE_CAPABILITY_SHORT_SLOT_TIME;

  // Determine how many bands this adapter supports.
  wlan_info_band_t bands[WLAN_INFO_BAND_COUNT];
  info->bands_count = compose_band_list(nvm_data, bands);

  fill_band_infos(nvm_data, bands, info->bands_count, info->bands);

  return ZX_OK;
}

zx_status_t mac_start(void* ctx, const wlan_softmac_ifc_protocol_t* ifc,
                      zx_handle_t* out_mlme_channel) {
  const auto mvmvif = reinterpret_cast<struct iwl_mvm_vif*>(ctx);

  if (!ctx || !ifc || !out_mlme_channel) {
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

  mvmvif->ifc = *ifc;

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

zx_status_t mac_queue_tx(void* ctx, uint32_t options, const wlan_tx_packet_t* tx_packet) {
  const auto mvmvif = reinterpret_cast<struct iwl_mvm_vif*>(ctx);

  if (tx_packet->mac_frame_size > WLAN_MSDU_MAX_LEN) {
    IWL_ERR(mvmvif, "Frame size is to large (%lu). expect less than %lu.\n",
            tx_packet->mac_frame_size, WLAN_MSDU_MAX_LEN);
    return ZX_ERR_INVALID_ARGS;
  }

  ieee80211_mac_packet packet = {};
  packet.common_header =
      reinterpret_cast<const ieee80211_frame_header*>(tx_packet->mac_frame_buffer);
  packet.header_size = ieee80211_get_header_len(packet.common_header);
  if (packet.header_size > tx_packet->mac_frame_size) {
    IWL_ERR(mvmvif, "TX packet header size %zu too large for data size %zu\n", packet.header_size,
            tx_packet->mac_frame_size);
    return ZX_ERR_INVALID_ARGS;
  }

  packet.body = tx_packet->mac_frame_buffer + packet.header_size;
  packet.body_size = tx_packet->mac_frame_size - packet.header_size;

  auto lock = std::lock_guard(mvmvif->mvm->mutex);
  return iwl_mvm_mac_tx(mvmvif, &packet);
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
zx_status_t mac_set_channel(struct iwl_mvm_vif* mvmvif, uint32_t options,
                            const wlan_channel_t* channel) {
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
zx_status_t mac_configure_bss(struct iwl_mvm_vif* mvmvif, uint32_t options,
                              const bss_config_t* config) {
  zx_status_t ret = ZX_OK;

  IWL_INFO(mvmvif, "mac_configure_bss(bssid=%02x:%02x:%02x:%02x:%02x:%02x, type=%d, remote=%d)\n",
           config->bssid[0], config->bssid[1], config->bssid[2], config->bssid[3], config->bssid[4],
           config->bssid[5], config->bss_type, config->remote);

  if (config->bss_type != BSS_TYPE_INFRASTRUCTURE) {
    IWL_ERR(mvmvif, "invalid bss_type requested: %d\n", config->bss_type);
    return ZX_ERR_INVALID_ARGS;
  }

  {
    // Copy the BSSID info.
    auto lock = std::lock_guard(mvmvif->mvm->mutex);
    memcpy(mvmvif->bss_conf.bssid, config->bssid, ETH_ALEN);
    memcpy(mvmvif->bssid, config->bssid, ETH_ALEN);

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

zx_status_t mac_enable_beaconing(void* ctx, uint32_t options, const wlan_bcn_config_t* bcn_cfg) {
  IWL_ERR(ctx, "%s() needs porting ... see fxbug.dev/36742\n", __func__);
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t mac_configure_beacon(void* ctx, uint32_t options,
                                 const wlan_tx_packet_t* packet_template) {
  IWL_ERR(ctx, "%s() needs porting ... see fxbug.dev/36742\n", __func__);
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t mac_set_key(struct iwl_mvm_vif* mvmvif, struct iwl_mvm_sta* mvmsta, uint32_t options,
                        const wlan_key_config_t* key_config) {
  zx_status_t status = ZX_OK;
  iwl_mvm* mvm = mvmvif->mvm;

  if (key_config->key_len > WLAN_MAX_KEY_LEN) {
    IWL_ERR(mvm, "unreasonable key length: %d bytes. expect smaller than or equal to %lu bytes.\n",
            key_config->key_len, WLAN_MAX_KEY_LEN);
    return ZX_ERR_INVALID_ARGS;
  }

  if (mvm->trans->cfg->gen2 || iwl_mvm_has_new_tx_api(mvm)) {
    // The new firmwares (for starting with the 22000 series) have different packet generation
    // requirements than mentioned below.
    return ZX_ERR_NOT_SUPPORTED;
  }

  if (!std::equal(key_config->cipher_oui,
                  key_config->cipher_oui + std::size(key_config->cipher_oui), kIeeeOui,
                  kIeeeOui + std::size(kIeeeOui))) {
    // IEEE 802.11-2016 9.4.2.25.2
    // The standard ciphers all live in the IEEE space.
    return ZX_ERR_NOT_SUPPORTED;
  }

  switch (static_cast<fuchsia_wlan_ieee80211::wire::CipherSuiteType>(key_config->cipher_type)) {
    case fuchsia_wlan_ieee80211::wire::CipherSuiteType::kCcmp128:
      // Note: the Linux iwlwifi driver requests IEEE80211_KEY_FLAG_PUT_IV_SPACE from the mac80211
      // stack.  We will apply equivalent functionality manually to Incoming packets from Fuchsia.
      break;
    default:
      // Additional porting required for other types.
      return ZX_ERR_NOT_SUPPORTED;
  }

  auto key_conf = reinterpret_cast<iwl_mvm_sta_key_conf*>(
      malloc(sizeof(iwl_mvm_sta_key_conf) + key_config->key_len));
  memset(key_conf, 0, sizeof(*key_conf) + key_config->key_len);
  key_conf->cipher_type = key_config->cipher_type;
  key_conf->key_type = key_config->key_type;
  key_conf->keyidx = key_config->key_idx;
  key_conf->keylen = key_config->key_len;
  key_conf->rx_seq = key_config->rsc;
  memcpy(key_conf->key, key_config->key, key_conf->keylen);

  if ((status = iwl_mvm_mac_set_key(mvmvif, mvmsta, key_conf)) != ZX_OK) {
    free(key_conf);
    IWL_ERR(mvmvif, "iwl_mvm_mac_set_key() failed: %s\n", zx_status_get_string(status));
    return status;
  }

  if (key_conf->key_type == WLAN_KEY_TYPE_PAIRWISE) {
    // Save the pairwise key, for use in the TX path.  Group keys are receive-only and do not need
    // to be saved.
    free(mvmsta->key_conf);
    mvmsta->key_conf = key_conf;
  } else {
    free(key_conf);
  }

  return ZX_OK;
}

// Set the association result to the firmware.
//
// The current mac context is set by mac_configure_bss() with default values.
//   TODO(fxbug.dev/36683): supports HT (802.11n)
//   TODO(fxbug.dev/36684): supports VHT (802.11ac)
//
zx_status_t mac_configure_assoc(struct iwl_mvm_vif* mvmvif, uint32_t options,
                                const wlan_assoc_ctx_t* assoc_ctx) {
  zx_status_t ret = ZX_OK;

  IWL_INFO(ctx, "Associating ...\n");

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

zx_status_t mac_clear_assoc(struct iwl_mvm_vif* mvmvif, uint32_t options,
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

zx_status_t mac_start_passive_scan(void* ctx,
                                   const wlan_softmac_passive_scan_args_t* passive_scan_args,
                                   uint64_t* out_scan_id) {
  const auto mvmvif = reinterpret_cast<struct iwl_mvm_vif*>(ctx);
  return iwl_mvm_mac_hw_scan_passive(mvmvif, passive_scan_args, out_scan_id);
}

zx_status_t mac_start_active_scan(void* ctx,
                                  const wlan_softmac_active_scan_args_t* active_scan_args,
                                  uint64_t* out_scan_id) {
  return ZX_ERR_NOT_SUPPORTED;
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

  free(mvmvif);
}

/////////////////////////////////////       PHY       //////////////////////////////////////////////

zx_status_t phy_query(void* ctx, wlanphy_impl_info_t* info) {
  const auto iwl_trans = reinterpret_cast<struct iwl_trans*>(ctx);
  struct iwl_mvm* mvm = iwl_trans_get_mvm(iwl_trans);
  if (!mvm || !info) {
    return ZX_ERR_INVALID_ARGS;
  }

  struct iwl_nvm_data* nvm_data = mvm->nvm_data;
  ZX_ASSERT(nvm_data);

  memset(info, 0, sizeof(*info));

  // TODO(fxbug.dev/36677): supports AP role
  info->supported_mac_roles = WLAN_INFO_MAC_ROLE_CLIENT;

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

  {
    auto lock = std::lock_guard(mvm->mutex);

    if (id >= MAX_NUM_MVMVIF) {
      IWL_ERR(mvm, "the interface id (%d) is invalid\n", id);
      return ZX_ERR_INVALID_ARGS;
    }

    mvmvif = mvm->mvmvif[id];
    if (!mvmvif) {
      IWL_ERR(mvm, "the interface id (%d) has no MAC context\n", id);
      return ZX_ERR_NOT_FOUND;
    }

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
  if (out_country == nullptr) {
    return ZX_ERR_INVALID_ARGS;
  }

  IWL_ERR(ctx, "%s() needs porting ...\n", __func__);
  return ZX_ERR_NOT_SUPPORTED;
}
