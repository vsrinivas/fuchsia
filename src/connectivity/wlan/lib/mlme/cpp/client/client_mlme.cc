// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/wlan/common/c/banjo.h>
#include <fuchsia/wlan/internal/c/banjo.h>
#include <fuchsia/wlan/mlme/cpp/fidl.h>
#include <lib/zx/time.h>
#include <zircon/assert.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

#include <cinttypes>
#include <cstring>
#include <memory>
#include <sstream>

#include <wlan/common/bitfield.h>
#include <wlan/common/channel.h>
#include <wlan/common/logging.h>
#include <wlan/mlme/client/client_mlme.h>
#include <wlan/mlme/device_interface.h>
#include <wlan/mlme/mac_frame.h>
#include <wlan/mlme/packet.h>
#include <wlan/mlme/wlan.h>

#include "src/connectivity/wlan/lib/mlme/rust/c-binding/bindings.h"

namespace wlan {

namespace wlan_mlme = ::fuchsia::wlan::mlme;
namespace wlan_stats = ::fuchsia::wlan::stats;

#define DEVICE(c) static_cast<DeviceInterface*>(c)

// TODO(fxbug.dev/45464): Move this to C++ once tests are ported.
wlan_client_mlme_config_t ClientMlmeDefaultConfig() {
  return wlan_client_mlme_config_t{
      .ensure_on_channel_time = zx::msec(500).get(),
  };
}

ClientMlme::ClientMlme(DeviceInterface* device, wlan_client_mlme_config_t config, bool run_as_test)
    : device_(device),
      rust_mlme_(nullptr, delete_client_mlme),
      config_(config),
      run_as_test_(run_as_test) {
  debugfn();
}

ClientMlme::~ClientMlme() = default;

zx_status_t ClientMlme::Init() {
  debugfn();

  // Initialize Rust dependencies
  auto rust_device = rust_device_interface_t{
      .device = static_cast<void*>(this->device_),
      .start = [](void* device, const rust_wlan_softmac_ifc_protocol_copy_t* ifc,
                  zx_handle_t* out_sme_channel) -> zx_status_t {
        zx::channel channel;
        zx_status_t result = DEVICE(device)->Start(ifc, &channel);
        *out_sme_channel = channel.release();
        return result;
      },
      .deliver_eth_frame = [](void* device, const uint8_t* data, size_t len) -> zx_status_t {
        return DEVICE(device)->DeliverEthernet({data, len});
      },
      .queue_tx = [](void* device, uint32_t options, mlme_out_buf_t buf,
                     wlan_tx_info_t tx_info) -> zx_status_t {
        auto pkt = FromRustOutBuf(buf);
        return DEVICE(device)->QueueTx(std::move(pkt), tx_info);
      },
      .set_eth_status = [](void* device, uint32_t status) { DEVICE(device)->SetStatus(status); },
      .get_wlan_channel = [](void* device) -> wlan_channel_t {
        return DEVICE(device)->GetState()->channel();
      },
      .set_wlan_channel = [](void* device, wlan_channel_t channel) -> zx_status_t {
        return DEVICE(device)->SetChannel(channel);
      },
      .set_key = [](void* device, wlan_key_config_t* key) -> zx_status_t {
        return DEVICE(device)->SetKey(key);
      },
      .start_passive_scan = [](void* device,
                               const wlan_softmac_passive_scan_args_t* passive_scan_args,
                               uint64_t* out_scan_id) -> zx_status_t {
        return DEVICE(device)->StartPassiveScan(passive_scan_args, out_scan_id);
      },
      .start_active_scan = [](void* device, const wlan_softmac_active_scan_args_t* active_scan_args,
                              uint64_t* out_scan_id) -> zx_status_t {
        return DEVICE(device)->StartActiveScan(active_scan_args, out_scan_id);
      },
      .get_wlan_softmac_info = [](void* device) -> wlan_softmac_info_t {
        return DEVICE(device)->GetWlanSoftmacInfo();
      },
      .get_discovery_support = [](void* device) -> discovery_support_t {
        return DEVICE(device)->GetDiscoverySupport();
      },
      .get_mac_sublayer_support = [](void* device) -> mac_sublayer_support_t {
        return DEVICE(device)->GetMacSublayerSupport();
      },
      .get_security_support = [](void* device) -> security_support_t {
        return DEVICE(device)->GetSecuritySupport();
      },
      .get_spectrum_management_support = [](void* device) -> spectrum_management_support_t {
        return DEVICE(device)->GetSpectrumManagementSupport();
      },
      .configure_bss = [](void* device, bss_config_t* cfg) -> zx_status_t {
        return DEVICE(device)->ConfigureBss(cfg);
      },
      .enable_beaconing = [](void* device, mlme_out_buf_t buf, size_t tim_ele_offset,
                             uint16_t beacon_interval) -> zx_status_t {
        // The client never needs to enable beaconing.
        return ZX_ERR_NOT_SUPPORTED;
      },
      .disable_beaconing = [](void* device) -> zx_status_t {
        // The client never needs to disable beaconing.
        return ZX_ERR_NOT_SUPPORTED;
      },
      .configure_beacon = [](void* device, mlme_out_buf_t buf) -> zx_status_t {
        // The client never needs to enable beaconing.
        return ZX_ERR_NOT_SUPPORTED;
      },
      .set_link_status = [](void* device,
                            uint8_t status) { return DEVICE(device)->SetStatus(status); },
      .configure_assoc = [](void* device, wlan_assoc_ctx_t* assoc_ctx) -> zx_status_t {
        return DEVICE(device)->ConfigureAssoc(assoc_ctx);
      },
      .clear_assoc = [](void* device, const uint8_t(*addr)[6]) -> zx_status_t {
        return DEVICE(device)->ClearAssoc(*addr);
      },
  };
  if (run_as_test_) {
    rust_mlme_ = RustClientMlme(
        start_client_mlme_for_test(config_, rust_device, rust_buffer_provider), delete_client_mlme);
  } else {
    rust_mlme_ = RustClientMlme(start_client_mlme(config_, rust_device, rust_buffer_provider),
                                delete_client_mlme);
  }

  if (rust_mlme_ == nullptr) {
    return ZX_ERR_BAD_STATE;
  }
  return ZX_OK;
}

void ClientMlme::AdvanceFakeTime(int64_t nanos) {
  client_mlme_advance_fake_time(rust_mlme_.get(), nanos);
}

void ClientMlme::RunUntilStalled() { client_mlme_run_until_stalled(rust_mlme_.get()); }

zx_status_t ClientMlme::StopMainLoop() {
  stop_client_mlme(rust_mlme_.get());
  return ZX_OK;
}

zx_status_t ClientMlme::QueueEthFrameTx(std::unique_ptr<Packet> pkt) {
  client_mlme_queue_eth_frame_tx(rust_mlme_.get(), AsWlanSpan({pkt->data(), pkt->len()}));
  return ZX_OK;
}

bool ClientMlme::OnChannel() { ZX_ASSERT(false); }
}  // namespace wlan
