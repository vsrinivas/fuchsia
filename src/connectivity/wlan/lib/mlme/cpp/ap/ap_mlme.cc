// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/wlan/common/c/banjo.h>
#include <fuchsia/wlan/internal/c/banjo.h>
#include <zircon/errors.h>
#include <zircon/status.h>

#include <memory>

#include <wlan/common/logging.h>
#include <wlan/mlme/ap/ap_mlme.h>
#include <wlan/mlme/device_interface.h>
#include <wlan/mlme/rust_utils.h>

namespace wlan {

namespace wlan_mlme = ::fuchsia::wlan::mlme;

#define MLME(m) static_cast<ApMlme*>(m)
ApMlme::ApMlme(DeviceInterface* device, bool run_as_test)
    : device_(device), rust_ap_(nullptr, delete_ap_sta), run_as_test_(run_as_test) {}

zx_status_t ApMlme::StopMainLoop() {
  stop_ap_sta(rust_ap_.get());
  return ZX_OK;
}

zx_status_t ApMlme::Init() {
  debugfn();

  // Repeated calls to Init are a no-op.
  if (rust_ap_ != nullptr) {
    return ZX_OK;
  }

  auto rust_device = rust_device_interface_t{
      .device = static_cast<void*>(this),
      .start = [](void* mlme, const rust_wlan_softmac_ifc_protocol_copy_t* ifc,
                  zx_handle_t* out_sme_channel) -> zx_status_t {
        zx::channel channel;
        zx_status_t result = MLME(mlme)->device_->Start(ifc, &channel);
        *out_sme_channel = channel.release();
        return result;
      },
      .deliver_eth_frame = [](void* mlme, const uint8_t* data, size_t len) -> zx_status_t {
        return MLME(mlme)->device_->DeliverEthernet({data, len});
      },
      .queue_tx = [](void* mlme, uint32_t options, mlme_out_buf_t buf,
                     wlan_tx_info_t tx_info) -> zx_status_t {
        auto pkt = FromRustOutBuf(buf);
        return MLME(mlme)->device_->QueueTx(std::move(pkt), tx_info);
      },
      .set_eth_status = [](void* mlme, uint32_t status) { MLME(mlme)->device_->SetStatus(status); },
      .get_wlan_channel = [](void* mlme) -> wlan_channel_t {
        return MLME(mlme)->device_->GetState()->channel();
      },
      .set_wlan_channel = [](void* mlme, wlan_channel_t channel) -> zx_status_t {
        return MLME(mlme)->device_->SetChannel(channel);
      },
      .set_key = [](void* mlme, wlan_key_config_t* key) -> zx_status_t {
        return MLME(mlme)->device_->SetKey(key);
      },
      .get_wlan_softmac_info = [](void* mlme) -> wlan_softmac_info_t {
        return MLME(mlme)->device_->GetWlanSoftmacInfo();
      },
      .get_discovery_support = [](void* mlme) -> discovery_support_t {
        return MLME(mlme)->device_->GetDiscoverySupport();
      },
      .get_mac_sublayer_support = [](void* mlme) -> mac_sublayer_support_t {
        return MLME(mlme)->device_->GetMacSublayerSupport();
      },
      .get_security_support = [](void* mlme) -> security_support_t {
        return MLME(mlme)->device_->GetSecuritySupport();
      },
      .get_spectrum_management_support = [](void* mlme) -> spectrum_management_support_t {
        return MLME(mlme)->device_->GetSpectrumManagementSupport();
      },
      .configure_bss = [](void* mlme, bss_config_t* cfg) -> zx_status_t {
        return MLME(mlme)->device_->ConfigureBss(cfg);
      },
      .enable_beaconing = [](void* mlme, mlme_out_buf_t buf, size_t tim_ele_offset,
                             uint16_t beacon_interval) -> zx_status_t {
        auto pkt = FromRustOutBuf(buf);
        wlan_bcn_config_t bcn_cfg = {
            .packet_template =
                {
                    .mac_frame_buffer = pkt->data(),
                    .mac_frame_size = pkt->size(),
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
        return MLME(mlme)->device_->ClearAssoc(*addr);
      },
  };
  auto bssid = device_->GetState()->address();
  if (run_as_test_) {
    rust_ap_ = ApStation(start_ap_sta_for_test(rust_device, rust_buffer_provider, &bssid.byte),
                         delete_ap_sta);
  } else {
    rust_ap_ =
        ApStation(start_ap_sta(rust_device, rust_buffer_provider, &bssid.byte), delete_ap_sta);
  }

  if (rust_ap_ == nullptr) {
    return ZX_ERR_BAD_STATE;
  }
  return ZX_OK;
}

zx_status_t ApMlme::QueueEthFrameTx(std::unique_ptr<Packet> pkt) {
  if (auto eth_frame = EthFrameView::CheckType(pkt.get()).CheckLength()) {
    ap_sta_queue_eth_frame_tx(rust_ap_.get(), AsWlanSpan({pkt->data(), pkt->len()}));
  }
  return ZX_OK;
}

void ApMlme::AdvanceFakeTime(int64_t nanos) { ap_mlme_advance_fake_time(rust_ap_.get(), nanos); }

void ApMlme::RunUntilStalled() { ap_mlme_run_until_stalled(rust_ap_.get()); }
}  // namespace wlan
