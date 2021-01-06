// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "iface-device.h"

#include <stdio.h>

#include <ddk/debug.h>
#include <ddk/hw/wlan/wlaninfo.h>

namespace wlan {
namespace testing {

#define DEV(c) static_cast<IfaceDevice*>(c)
static zx_protocol_device_t wlanmac_test_device_ops = {
    .version = DEVICE_OPS_VERSION,
    .unbind = [](void* ctx) { DEV(ctx)->Unbind(); },
    .release = [](void* ctx) { DEV(ctx)->Release(); },
};

static wlanmac_protocol_ops_t wlanmac_test_protocol_ops = {
    .query = [](void* ctx, uint32_t options, wlanmac_info_t* info) -> zx_status_t {
      return DEV(ctx)->Query(options, info);
    },
    .start = [](void* ctx, const wlanmac_ifc_protocol_t* ifc, zx_handle_t* out_sme_channel)
        -> zx_status_t { return DEV(ctx)->Start(ifc, out_sme_channel); },
    .stop = [](void* ctx) { DEV(ctx)->Stop(); },
    .queue_tx = [](void* ctx, uint32_t options, wlan_tx_packet_t* pkt) -> zx_status_t {
      return ZX_OK;
    },
    .set_channel = [](void* ctx, uint32_t options, const wlan_channel_t* chan) -> zx_status_t {
      return DEV(ctx)->SetChannel(options, chan);
    },
    .configure_bss = [](void* ctx, uint32_t options,
                        const wlan_bss_config_t* config) -> zx_status_t { return ZX_OK; },
    .enable_beaconing = [](void* ctx, uint32_t options,
                           const wlan_bcn_config_t* bcn_cfg) -> zx_status_t { return ZX_OK; },
    .configure_beacon = [](void* ctx, uint32_t options,
                           const wlan_tx_packet_t* pkt) -> zx_status_t { return ZX_OK; },
    .set_key = [](void* ctx, uint32_t options, const wlan_key_config_t* key_config) -> zx_status_t {
      return ZX_OK;
    },
    .configure_assoc = [](void* ctx, uint32_t options,
                          const wlan_assoc_ctx_t* assoc_ctx) -> zx_status_t { return ZX_OK; },
    .clear_assoc = [](void* ctx, uint32_t options, const uint8_t*, size_t) -> zx_status_t {
      return ZX_OK;
    },
};
#undef DEV

IfaceDevice::IfaceDevice(zx_device_t* device, wlan_info_mac_role_t role)
    : parent_(device), role_(role) {}

zx_status_t IfaceDevice::Bind() {
  zxlogf(INFO, "wlan::testing::IfaceDevice::Bind()");

  device_add_args_t args = {};
  args.version = DEVICE_ADD_ARGS_VERSION;
  args.name = "wlanmac-test";
  args.ctx = this;
  args.ops = &wlanmac_test_device_ops;
  args.proto_id = ZX_PROTOCOL_WLANMAC;
  args.proto_ops = &wlanmac_test_protocol_ops;

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

zx_status_t IfaceDevice::Query(uint32_t options, wlanmac_info_t* info) {
  zxlogf(INFO, "wlan::testing::IfaceDevice::Query()");
  memset(info, 0, sizeof(*info));

  static uint8_t mac[ETH_MAC_SIZE] = {0x02, 0x02, 0x02, 0x03, 0x03, 0x03};
  std::memcpy(info->mac_addr, mac, ETH_MAC_SIZE);

  // Fill out a minimal set of wlan device capabilities
  info->supported_phys = WLAN_INFO_PHY_TYPE_DSSS | WLAN_INFO_PHY_TYPE_CCK |
                         WLAN_INFO_PHY_TYPE_OFDM | WLAN_INFO_PHY_TYPE_HT;
  info->driver_features = WLAN_INFO_DRIVER_FEATURE_SYNTH;
  info->mac_role = role_;
  info->caps = 0;
  info->bands_count = 2;
  // clang-format off
    info->bands[0] = {
        .band = WLAN_INFO_BAND_2GHZ,
        .ht_supported = false,
        .ht_caps = {},
        .vht_supported = false,
        .vht_caps = {},
        .rates = {2, 4, 11, 22, 12, 18, 24, 36, 48, 72, 96, 108},
        .supported_channels =
            {
                .base_freq = 2417,
                .channels = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11},
           },
    };
    info->bands[1] = {
        .band = WLAN_INFO_BAND_5GHZ,
        .ht_supported = false,
        .ht_caps = {},
        .vht_supported = false,
        .vht_caps = {},
        .rates = {12, 18, 24, 36, 48, 72, 96, 108},
        .supported_channels =
            {
                .base_freq = 5000,
                .channels = {36, 40, 44, 48, 52, 56, 60, 64, 149, 153, 157, 161, 165},
            },
    };
  // clang-format on

  return ZX_OK;
}

void IfaceDevice::Stop() {
  zxlogf(INFO, "wlan::testing::IfaceDevice::Stop()");
  std::lock_guard<std::mutex> lock(lock_);
  ifc_.ops = nullptr;
  ifc_.ctx = nullptr;
}

zx_status_t IfaceDevice::Start(const wlanmac_ifc_protocol_t* ifc, zx_handle_t* out_sme_channel) {
  zxlogf(INFO, "wlan::testing::IfaceDevice::Start()");
  std::lock_guard<std::mutex> lock(lock_);
  *out_sme_channel = ZX_HANDLE_INVALID;
  if (ifc_.ops != nullptr) {
    return ZX_ERR_ALREADY_BOUND;
  } else {
    ifc_ = *ifc;
  }
  return ZX_OK;
}

zx_status_t IfaceDevice::SetChannel(uint32_t options, const wlan_channel_t* chan) {
  zxlogf(INFO, "wlan::testing::IfaceDevice::SetChannel()  chan=%u", chan->primary);
  return ZX_OK;
}

}  // namespace testing
}  // namespace wlan
