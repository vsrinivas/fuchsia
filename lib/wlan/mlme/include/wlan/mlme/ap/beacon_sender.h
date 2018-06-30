// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <wlan/mlme/device_interface.h>
#include <wlan/mlme/frame_handler.h>

#include <fuchsia/wlan/mlme/cpp/fidl.h>

#include <zircon/types.h>

namespace wlan {

class BssInterface;
class PsCfg;
template<typename T>
class MlmeMsg;

// Configures the driver to send Beacon frames periodically.
class BeaconSender : public FrameHandler {
   public:
    BeaconSender(DeviceInterface* device);
    ~BeaconSender();

    void Start(BssInterface* bss, const PsCfg& ps_cfg,
               const MlmeMsg<::fuchsia::wlan::mlme::StartRequest>& req);
    void Stop();
    zx_status_t UpdateBeacon(const PsCfg& ps_cfg);
    zx_status_t HandleProbeRequest(const MgmtFrame<ProbeRequest>& frame) override;

   private:
    zx_status_t SendProbeResponse(const MgmtFrame<ProbeRequest>& frame);
    zx_status_t WriteSsid(ElementWriter* w);
    zx_status_t WriteSupportedRates(ElementWriter* w);
    zx_status_t WriteDsssParamSet(ElementWriter* w);
    zx_status_t WriteTim(ElementWriter* w, const PsCfg& ps_cfg);
    zx_status_t WriteCountry(ElementWriter* w);
    zx_status_t WriteExtendedSupportedRates(ElementWriter* w);
    zx_status_t WriteHtCapabilities(ElementWriter* w);
    zx_status_t WriteHtOperation(ElementWriter* w);
    bool IsStarted();

    DeviceInterface* const device_;
    ::fuchsia::wlan::mlme::StartRequest req_;
    BssInterface* bss_ = nullptr;
    // Buffer to write the Partial Virtual Bitmap to which was derived from a
    // Traffic Indication Map.
    uint8_t pvb_[TimElement::kMaxLenBmp];
};

}  // namespace wlan
