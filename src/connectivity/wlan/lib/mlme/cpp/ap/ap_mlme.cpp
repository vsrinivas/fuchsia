// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <wlan/common/logging.h>
#include <wlan/mlme/ap/ap_mlme.h>
#include <wlan/mlme/service.h>
#include <wlan/protocol/mac.h>
#include <zircon/status.h>

namespace wlan {

namespace wlan_mlme = ::fuchsia::wlan::mlme;

ApMlme::ApMlme(DeviceInterface* device) : device_(device) {}

ApMlme::~ApMlme() {
  // Ensure the BSS is correctly stopped and terminated when destroying the
  // MLME.
  if (bss_ != nullptr && bss_->IsStarted()) {
    bss_->Stop();
  }
}

zx_status_t ApMlme::Init() {
  debugfn();
  return ZX_OK;
}

zx_status_t ApMlme::HandleTimeout(const ObjectId id) {
  debugfn();

  switch (id.target()) {
    case to_enum_type(ObjectTarget::kBss): {
      return bss_->HandleTimeout();
    }
    default:
      ZX_DEBUG_ASSERT(false);
      break;
  }

  return ZX_OK;
}

zx_status_t ApMlme::HandleMlmeMsg(const BaseMlmeMsg& msg) {
  if (auto start_req = msg.As<wlan_mlme::StartRequest>()) {
    return HandleMlmeStartReq(*start_req);
  } else if (auto stop_req = msg.As<wlan_mlme::StopRequest>()) {
    return HandleMlmeStopReq(*stop_req);
  }
  return bss_->HandleMlmeMsg(msg);
}

zx_status_t ApMlme::HandleFramePacket(fbl::unique_ptr<Packet> pkt) {
  if (bss_ != nullptr) {
    bss_->HandleAnyFrame(std::move(pkt));
  }
  return ZX_OK;
}

zx_status_t ApMlme::HandleMlmeStartReq(
    const MlmeMsg<wlan_mlme::StartRequest>& req) {
  debugfn();

  // Only one BSS can be started at a time.
  if (bss_ != nullptr) {
    debugf("BSS %s already running but received MLME-START.request\n",
           device_->GetState()->address().ToString().c_str());
    return service::SendStartConfirm(
        device_, wlan_mlme::StartResultCodes::BSS_ALREADY_STARTED_OR_JOINED);
  }

  ObjectId timer_id;
  timer_id.set_subtype(to_enum_type(ObjectSubtype::kTimer));
  timer_id.set_target(to_enum_type(ObjectTarget::kBss));
  fbl::unique_ptr<Timer> timer;
  zx_status_t status =
      device_->GetTimer(ToPortKey(PortKeyType::kMlme, timer_id.val()), &timer);
  if (status != ZX_OK) {
    errorf("Could not create bss timer: %s\n", zx_status_get_string(status));
    return service::SendStartConfirm(
        device_, wlan_mlme::StartResultCodes::INTERNAL_ERROR);
  }

  // Configure BSS in driver.
  auto& bssid = device_->GetState()->address();
  wlan_bss_config_t cfg{
      .bss_type = WLAN_BSS_TYPE_INFRASTRUCTURE,
      .remote = false,
  };
  bssid.CopyTo(cfg.bssid);
  device_->ConfigureBss(&cfg);

  // Create and start BSS.
  auto bcn_sender = std::make_unique<BeaconSender>(device_);
  bss_.reset(
      new InfraBss(device_, std::move(bcn_sender), bssid, std::move(timer)));
  bss_->Start(req);

  return service::SendStartConfirm(device_,
                                   wlan_mlme::StartResultCodes::SUCCESS);
}

zx_status_t ApMlme::HandleMlmeStopReq(
    const MlmeMsg<wlan_mlme::StopRequest>& req) {
  debugfn();

  if (bss_ == nullptr) {
    errorf("received MLME-STOP.request but no BSS is running on device: %s\n",
           device_->GetState()->address().ToString().c_str());
    return ZX_OK;
  }

  // Stop and destroy BSS.
  bss_->Stop();
  bss_.reset();

  return ZX_OK;
}

void ApMlme::HwIndication(uint32_t ind) {
  if (ind == WLAN_INDICATION_PRE_TBTT) {
    bss_->OnPreTbtt();
  } else if (ind == WLAN_INDICATION_BCN_TX_COMPLETE) {
    bss_->OnBcnTxComplete();
  }
}

HtConfig ApMlme::Ht() const { return bss_->Ht(); }

const Span<const SupportedRate> ApMlme::Rates() const { return bss_->Rates(); }

}  // namespace wlan
