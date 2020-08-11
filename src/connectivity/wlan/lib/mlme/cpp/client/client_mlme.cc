// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

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
#include <wlan/mlme/mac_frame.h>
#include <wlan/mlme/packet.h>
#include <wlan/mlme/service.h>
#include <wlan/mlme/timer.h>
#include <wlan/mlme/timer_manager.h>
#include <wlan/mlme/wlan.h>

namespace wlan {

namespace wlan_mlme = ::fuchsia::wlan::mlme;
namespace wlan_stats = ::fuchsia::wlan::stats;

#define TIMER_MGR(c) static_cast<TimerManager<>*>(c)
#define DEVICE(c) static_cast<DeviceInterface*>(c)

wlan_client_mlme_config_t ClientMlmeDefaultConfig() {
  return wlan_client_mlme_config_t{
      .ensure_on_channel_time = zx::msec(500).get(),
  };
}

ClientMlme::ClientMlme(DeviceInterface* device) : ClientMlme(device, ClientMlmeDefaultConfig()) {
  debugfn();
}

ClientMlme::ClientMlme(DeviceInterface* device, wlan_client_mlme_config_t config)
    : device_(device), rust_mlme_(nullptr, client_mlme_delete), config_(config) {
  debugfn();
}

ClientMlme::~ClientMlme() = default;

zx_status_t ClientMlme::Init() {
  debugfn();

  std::unique_ptr<Timer> timer;
  ObjectId timer_id;
  timer_id.set_subtype(to_enum_type(ObjectSubtype::kTimer));
  timer_id.set_target(to_enum_type(ObjectTarget::kClientMlme));
  zx_status_t status = device_->GetTimer(ToPortKey(PortKeyType::kMlme, timer_id.val()), &timer);
  if (status != ZX_OK) {
    errorf("could not create channel scheduler timer: %d\n", status);
    return status;
  }
  timer_mgr_ = std::make_unique<TimerManager<>>(std::move(timer));

  // Initialize Rust dependencies
  auto rust_device = mlme_device_ops_t{
      .device = static_cast<void*>(this->device_),
      .deliver_eth_frame = [](void* device, const uint8_t* data, size_t len) -> zx_status_t {
        return DEVICE(device)->DeliverEthernet({data, len});
      },
      .send_wlan_frame = [](void* device, mlme_out_buf_t buf, uint32_t flags) -> zx_status_t {
        auto pkt = FromRustOutBuf(buf);
        return DEVICE(device)->SendWlan(std::move(pkt), flags);
      },
      .get_sme_channel = [](void* device) -> zx_handle_t {
        return DEVICE(device)->GetSmeChannelRef();
      },
      .get_wlan_channel = [](void* device) -> wlan_channel_t {
        return DEVICE(device)->GetState()->channel();
      },
      .set_wlan_channel = [](void* device, wlan_channel_t chan) -> zx_status_t {
        return DEVICE(device)->SetChannel(chan);
      },
      .set_key = [](void* device, wlan_key_config_t* key) -> zx_status_t {
        return DEVICE(device)->SetKey(key);
      },
      .start_hw_scan = [](void* device, const wlan_hw_scan_config_t* config) -> zx_status_t {
        return DEVICE(device)->StartHwScan(config);
      },
      .get_wlan_info = [](void* device) -> wlanmac_info_t { return DEVICE(device)->GetWlanInfo(); },
      .configure_bss = [](void* device, wlan_bss_config_t* cfg) -> zx_status_t {
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
        return DEVICE(device)->ClearAssoc(common::MacAddr(*addr));
      },
  };
  auto scheduler = wlan_scheduler_ops_t{
      .cookie = static_cast<void*>(this->timer_mgr_.get()),
      .now = [](void* cookie) -> zx_time_t { return TIMER_MGR(cookie)->Now().get(); },
      .schedule = [](void* cookie, int64_t deadline) -> wlan_scheduler_event_id_t {
        TimeoutId id = {};
        TIMER_MGR(cookie)->Schedule(zx::time(deadline), {}, &id);
        return {._0 = id.raw()};
      },
      .cancel = [](void* cookie,
                   wlan_scheduler_event_id_t id) { TIMER_MGR(cookie)->Cancel(TimeoutId(id._0)); },
  };
  rust_mlme_ = RustClientMlme(
      client_mlme_new(config_, rust_device, rust_buffer_provider, scheduler), client_mlme_delete);

  return status;
}

zx_status_t ClientMlme::HandleTimeout(const ObjectId id) {
  if (id.target() != to_enum_type(ObjectTarget::kClientMlme)) {
    ZX_DEBUG_ASSERT(0);
    return ZX_ERR_NOT_SUPPORTED;
  }

  auto status = timer_mgr_->HandleTimeout([&](auto now, auto target, auto timeout_id) {
    client_mlme_timeout_fired(rust_mlme_.get(), wlan_scheduler_event_id_t{._0 = timeout_id.raw()});
  });

  if (status != ZX_OK) {
    errorf("failed to rearm the timer after handling the timeout: %s",
           zx_status_get_string(status));
  }
  return status;
}

void ClientMlme::HwScanComplete(uint8_t result_code) {
  client_mlme_hw_scan_complete(rust_mlme_.get(), result_code);
}

zx_status_t ClientMlme::HandleEncodedMlmeMsg(fbl::Span<const uint8_t> msg) {
  debugfn();
  return client_mlme_handle_mlme_msg(rust_mlme_.get(), AsWlanSpan(msg));
}

zx_status_t ClientMlme::HandleMlmeMsg(const BaseMlmeMsg& msg) { return ZX_ERR_NOT_SUPPORTED; }

zx_status_t ClientMlme::HandleFramePacket(std::unique_ptr<Packet> pkt) {
  switch (pkt->peer()) {
    case Packet::Peer::kEthernet: {
      return client_mlme_handle_eth_frame(rust_mlme_.get(), AsWlanSpan({pkt->data(), pkt->len()}));
    }
    case Packet::Peer::kWlan: {
      auto frame_span = fbl::Span<uint8_t>{pkt->data(), pkt->len()};
      const wlan_rx_info_t* rx_info = nullptr;
      if (pkt->has_ctrl_data<wlan_rx_info_t>()) {
        rx_info = pkt->ctrl_data<wlan_rx_info_t>();
      }
      client_mlme_on_mac_frame(rust_mlme_.get(), AsWlanSpan(frame_span), rx_info);

      return ZX_OK;
    }
    default:
      errorf("unknown Packet peer: %u\n", pkt->peer());
      return ZX_ERR_INVALID_ARGS;
  }
}

bool ClientMlme::OnChannel() { return client_mlme_on_channel(rust_mlme_.get()); }
}  // namespace wlan
