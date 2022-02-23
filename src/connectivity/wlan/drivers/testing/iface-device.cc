// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "iface-device.h"

#include <fuchsia/hardware/wlan/phyinfo/c/banjo.h>
#include <fuchsia/wlan/common/c/banjo.h>
#include <fuchsia/wlan/ieee80211/c/banjo.h>
#include <fuchsia/wlan/internal/c/banjo.h>
#include <lib/ddk/debug.h>
#include <stdio.h>
#include <zircon/assert.h>

namespace wlan {
namespace testing {

#define DEV(c) static_cast<IfaceDevice*>(c)
static zx_protocol_device_t wlan_softmac_test_device_ops = {
    .version = DEVICE_OPS_VERSION,
    .unbind = [](void* ctx) { DEV(ctx)->Unbind(); },
    .release = [](void* ctx) { DEV(ctx)->Release(); },
};

static wlan_softmac_protocol_ops_t wlan_softmac_test_protocol_ops = {
    .query = [](void* ctx, wlan_softmac_info_t* info) -> zx_status_t {
      return DEV(ctx)->Query(info);
    },
    .query_discovery_support =
        [](void* ctx, discovery_support_t* support) {
          return DEV(ctx)->QueryDiscoverySupport(support);
        },
    .query_mac_sublayer_support =
        [](void* ctx, mac_sublayer_support_t* support) {
          return DEV(ctx)->QueryMacSublayerSupport(support);
        },
    .query_security_support =
        [](void* ctx, security_support_t* support) {
          return DEV(ctx)->QuerySecuritySupport(support);
        },
    .query_spectrum_management_support =
        [](void* ctx, spectrum_management_support_t* support) {
          return DEV(ctx)->QuerySpectrumManagementSupport(support);
        },
    .start = [](void* ctx, const wlan_softmac_ifc_protocol_t* ifc, zx_handle_t* out_mlme_channel)
        -> zx_status_t { return DEV(ctx)->Start(ifc, out_mlme_channel); },
    .stop = [](void* ctx) { DEV(ctx)->Stop(); },
    .queue_tx = [](void* ctx, const wlan_tx_packet_t* pkt,
                   bool* out_enqueue_pending) -> zx_status_t {
      *out_enqueue_pending = false;
      return ZX_OK;
    },
    .set_channel = [](void* ctx, const wlan_channel_t* channel) -> zx_status_t {
      return DEV(ctx)->SetChannel(channel);
    },
    .configure_bss = [](void* ctx, const bss_config_t* config) -> zx_status_t { return ZX_OK; },
    .enable_beaconing = [](void* ctx, const wlan_bcn_config_t* bcn_cfg) -> zx_status_t {
      return ZX_OK;
    },
    .configure_beacon = [](void* ctx, const wlan_tx_packet_t* pkt) -> zx_status_t { return ZX_OK; },
    .set_key = [](void* ctx, const wlan_key_config_t* key_config) -> zx_status_t { return ZX_OK; },
    .configure_assoc = [](void* ctx, const wlan_assoc_ctx_t* assoc_ctx) -> zx_status_t {
      return ZX_OK;
    },
    .clear_assoc = [](void* ctx, const uint8_t[fuchsia_wlan_ieee80211_MAC_ADDR_LEN])
        -> zx_status_t { return ZX_OK; },
};
#undef DEV

IfaceDevice::IfaceDevice(zx_device_t* device, wlan_mac_role_t role)
    : parent_(device), role_(role) {}

zx_status_t IfaceDevice::Bind() {
  zxlogf(INFO, "wlan::testing::IfaceDevice::Bind()");

  device_add_args_t args = {};
  args.version = DEVICE_ADD_ARGS_VERSION;
  args.name = "wlan-softmac-test";
  args.ctx = this;
  args.ops = &wlan_softmac_test_device_ops;
  args.proto_id = ZX_PROTOCOL_WLAN_SOFTMAC;
  args.proto_ops = &wlan_softmac_test_protocol_ops;

  zx_status_t status = device_add(parent_, &args, &zxdev_);
  if (status != ZX_OK) {
    zxlogf(INFO, "wlan-test: could not add test device: %d", status);
  }
  return status;
}

void IfaceDevice::Unbind() {
  zxlogf(INFO, "wlan::testing::IfaceDevice::Unbind()");
  device_unbind_reply(zxdev_);
}

void IfaceDevice::Release() {
  zxlogf(INFO, "wlan::testing::IfaceDevice::Release()");
  delete this;
}

zx_status_t IfaceDevice::Query(wlan_softmac_info_t* info) {
  zxlogf(INFO, "wlan::testing::IfaceDevice::Query()");
  *info = {};

  static uint8_t mac[fuchsia_wlan_ieee80211_MAC_ADDR_LEN] = {0x02, 0x02, 0x02, 0x03, 0x03, 0x03};
  std::memcpy(info->sta_addr, mac, fuchsia_wlan_ieee80211_MAC_ADDR_LEN);

  // Fill out a minimal set of wlan device capabilities
  size_t count = 0;
  for (auto phy : {WLAN_PHY_TYPE_DSSS, WLAN_PHY_TYPE_HR, WLAN_PHY_TYPE_OFDM, WLAN_PHY_TYPE_ERP,
                   WLAN_PHY_TYPE_HT}) {
    ZX_DEBUG_ASSERT(count < fuchsia_wlan_common_MAX_SUPPORTED_PHY_TYPES);
    info->supported_phys_list[count] = phy;
    ++count;
  }
  info->supported_phys_count = count;

  mac_sublayer_support_t mac_sublayer;
  QueryMacSublayerSupport(&mac_sublayer);
  if (mac_sublayer.device.is_synthetic) {
    info->driver_features |= WLAN_INFO_DRIVER_FEATURE_SYNTH;
  }
  info->mac_role = role_;
  info->hardware_capability = 0;
  info->band_cap_count = 2;
  // clang-format off
    info->band_cap_list[0] = {
        .band = WLAN_BAND_TWO_GHZ,
        .basic_rate_count = 12,
        .basic_rate_list = {2, 4, 11, 22, 12, 18, 24, 36, 48, 72, 96, 108},
        .ht_supported = false,
        .ht_caps = {},
        .vht_supported = false,
        .vht_caps = {},
        .operating_channel_count = 11,
        .operating_channel_list = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11},
    };
    info->band_cap_list[1] = {
        .band = WLAN_BAND_FIVE_GHZ,
        .basic_rate_count = 8,
        .basic_rate_list = {12, 18, 24, 36, 48, 72, 96, 108},
        .ht_supported = false,
        .ht_caps = {},
        .vht_supported = false,
        .vht_caps = {},
        .operating_channel_count = 13,
        .operating_channel_list = {36, 40, 44, 48, 52, 56, 60, 64, 149, 153, 157, 161, 165},
    };
  // clang-format on

  return ZX_OK;
}

void IfaceDevice::QueryDiscoverySupport(discovery_support_t* out_support) {
  *out_support = {};
  // This test device does not set any discovery features.
}

void IfaceDevice::QueryMacSublayerSupport(mac_sublayer_support_t* out_support) {
  *out_support = {};
  out_support->device.is_synthetic = true;
}

void IfaceDevice::QuerySecuritySupport(security_support_t* out_support) {
  *out_support = {};
  // This test device does not set any security features.
}

void IfaceDevice::QuerySpectrumManagementSupport(spectrum_management_support_t* out_support) {
  *out_support = {};
  // This test device does not set any spectrum management features.
}

void IfaceDevice::Stop() {
  zxlogf(INFO, "wlan::testing::IfaceDevice::Stop()");
  std::lock_guard<std::mutex> lock(lock_);
  ifc_.ops = nullptr;
  ifc_.ctx = nullptr;
}

zx_status_t IfaceDevice::Start(const wlan_softmac_ifc_protocol_t* ifc,
                               zx_handle_t* out_mlme_channel) {
  zxlogf(INFO, "wlan::testing::IfaceDevice::Start()");
  std::lock_guard<std::mutex> lock(lock_);
  *out_mlme_channel = ZX_HANDLE_INVALID;
  if (ifc_.ops != nullptr) {
    return ZX_ERR_ALREADY_BOUND;
  } else {
    ifc_ = *ifc;
  }
  return ZX_OK;
}

zx_status_t IfaceDevice::SetChannel(const wlan_channel_t* channel) {
  zxlogf(INFO, "wlan::testing::IfaceDevice::SetChannel()  channel=%u", channel->primary);
  return ZX_OK;
}

}  // namespace testing
}  // namespace wlan
