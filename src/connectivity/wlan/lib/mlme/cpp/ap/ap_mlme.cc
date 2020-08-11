// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/status.h>

#include <memory>

#include <wlan/common/logging.h>
#include <wlan/mlme/ap/ap_mlme.h>
#include <wlan/mlme/rust_utils.h>
#include <wlan/mlme/service.h>
#include <wlan/protocol/mac.h>

namespace wlan {

namespace wlan_mlme = ::fuchsia::wlan::mlme;

#define MLME(m) static_cast<ApMlme*>(m)
ApMlme::ApMlme(DeviceInterface* device) : device_(device), rust_ap_(nullptr, ap_sta_delete) {
  auto rust_device = mlme_device_ops_t{
      .device = static_cast<void*>(this),
      .deliver_eth_frame = [](void* mlme, const uint8_t* data, size_t len) -> zx_status_t {
        return MLME(mlme)->device_->DeliverEthernet({data, len});
      },
      .send_wlan_frame = [](void* mlme, mlme_out_buf_t buf, uint32_t flags) -> zx_status_t {
        return MLME(mlme)->device_->SendWlan(FromRustOutBuf(buf), flags);
      },
      .get_sme_channel = [](void* mlme) -> zx_handle_t {
        return MLME(mlme)->device_->GetSmeChannelRef();
      },
      .get_wlan_channel = [](void* mlme) -> wlan_channel_t {
        return MLME(mlme)->device_->GetState()->channel();
      },
      .set_wlan_channel = [](void* mlme, wlan_channel_t chan) -> zx_status_t {
        return MLME(mlme)->device_->SetChannel(chan);
      },
      .set_key = [](void* mlme, wlan_key_config_t* key) -> zx_status_t {
        return MLME(mlme)->device_->SetKey(key);
      },
      .get_wlan_info = [](void* mlme) -> wlanmac_info_t {
        return MLME(mlme)->device_->GetWlanInfo();
      },
      .configure_bss = [](void* mlme, wlan_bss_config_t* cfg) -> zx_status_t {
        return MLME(mlme)->device_->ConfigureBss(cfg);
      },
      .enable_beaconing = [](void* mlme, mlme_out_buf_t buf, size_t tim_ele_offset,
                             uint16_t beacon_interval) -> zx_status_t {
        auto pkt = FromRustOutBuf(buf);
        wlan_bcn_config_t bcn_cfg = {
            .tmpl =
                {
                    .packet_head =
                        {
                            .data_buffer = pkt->data(),
                            .data_size = pkt->size(),
                        },
                },
            .tim_ele_offset = tim_ele_offset,
            .beacon_interval = beacon_interval,
        };
        return MLME(mlme)->device_->EnableBeaconing(&bcn_cfg);
      },
      .disable_beaconing = [](void* mlme) -> zx_status_t {
        return MLME(mlme)->device_->EnableBeaconing(nullptr);
      },
      .configure_beacon = [](void* mlme, mlme_out_buf_t buf) -> zx_status_t {
        return MLME(mlme)->device_->ConfigureBeacon(FromRustOutBuf(buf));
      },
      .set_link_status = [](void* mlme, uint8_t status) -> zx_status_t {
        (void)mlme;
        (void)status;
        return ZX_ERR_NOT_SUPPORTED;
      },
      .configure_assoc = [](void* mlme, wlan_assoc_ctx_t* assoc_ctx) -> zx_status_t {
        return MLME(mlme)->device_->ConfigureAssoc(assoc_ctx);
      },
      .clear_assoc = [](void* mlme, const uint8_t(*addr)[6]) -> zx_status_t {
        return MLME(mlme)->device_->ClearAssoc(common::MacAddr(*addr));
      },
  };
  wlan_scheduler_ops_t scheduler = {
      .cookie = this,
      .now = [](void* cookie) -> zx_time_t { return MLME(cookie)->timer_mgr_->Now().get(); },
      .schedule = [](void* cookie, int64_t deadline) -> wlan_scheduler_event_id_t {
        TimeoutId id = {};
        MLME(cookie)->timer_mgr_->Schedule(zx::time(deadline), {}, &id);
        return {._0 = id.raw()};
      },
      .cancel =
          [](void* cookie, wlan_scheduler_event_id_t id) {
            MLME(cookie)->timer_mgr_->Cancel(TimeoutId(id._0));
          },
  };
  rust_ap_ =
      NewApStation(rust_device, rust_buffer_provider, scheduler, device_->GetState()->address());
}

zx_status_t ApMlme::Init() {
  debugfn();

  ObjectId timer_id;
  timer_id.set_subtype(to_enum_type(ObjectSubtype::kTimer));
  timer_id.set_target(to_enum_type(ObjectTarget::kApMlme));
  std::unique_ptr<Timer> timer;
  if (zx_status_t status = device_->GetTimer(ToPortKey(PortKeyType::kMlme, timer_id.val()), &timer);
      status != ZX_OK) {
    errorf("Could not create ap timer: %s\n", zx_status_get_string(status));
    return status;
  }
  timer_mgr_ = std::make_unique<TimerManager<std::tuple<>>>(std::move(timer));

  return ZX_OK;
}

zx_status_t ApMlme::HandleTimeout(const ObjectId id) {
  debugfn();
  if (id.target() != to_enum_type(ObjectTarget::kApMlme)) {
    ZX_DEBUG_ASSERT(false);
    return ZX_ERR_NOT_SUPPORTED;
  }

  return timer_mgr_->HandleTimeout([&](auto now, auto target, auto timeout_id) {
    ap_sta_timeout_fired(rust_ap_.get(), wlan_scheduler_event_id_t{._0 = timeout_id.raw()});
  });
}

zx_status_t ApMlme::HandleEncodedMlmeMsg(fbl::Span<const uint8_t> msg) {
  debugfn();
  return ap_sta_handle_mlme_msg(rust_ap_.get(), AsWlanSpan(msg));
}

zx_status_t ApMlme::HandleMlmeMsg(const BaseMlmeMsg& msg) {
  debugfn();

  // We don't handle MLME messages at this level.
  (void)msg;
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t ApMlme::HandleFramePacket(std::unique_ptr<Packet> pkt) {
  switch (pkt->peer()) {
    case Packet::Peer::kEthernet: {
      if (auto eth_frame = EthFrameView::CheckType(pkt.get()).CheckLength()) {
        return ap_sta_handle_eth_frame(rust_ap_.get(), AsWlanSpan({pkt->data(), pkt->len()}));
      }
      break;
    }
    case Packet::Peer::kWlan: {
      const wlan_rx_info_t* rx_info = nullptr;
      if (pkt->has_ctrl_data<wlan_rx_info_t>()) {
        rx_info = pkt->ctrl_data<wlan_rx_info_t>();
      }
      return ap_sta_handle_mac_frame(rust_ap_.get(),
                                     wlan_span_t{.data = pkt->data(), .size = pkt->len()}, rx_info);
    }
    default:
      errorf("unknown Packet peer: %u\n", pkt->peer());
      break;
  }
  return ZX_OK;
}

void ApMlme::HwIndication(uint32_t ind) {
  ap_sta_handle_hw_indication(rust_ap_.get(), static_cast<wlan_indication_t>(ind));
}

}  // namespace wlan
