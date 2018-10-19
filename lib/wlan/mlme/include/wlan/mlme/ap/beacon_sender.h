// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_WLAN_MLME_INCLUDE_WLAN_MLME_AP_BEACON_SENDER_H_
#define GARNET_LIB_WLAN_MLME_INCLUDE_WLAN_MLME_AP_BEACON_SENDER_H_

#include <wlan/mlme/device_interface.h>

#include <fuchsia/wlan/mlme/cpp/fidl.h>

#include <zircon/types.h>

namespace wlan {

class BssInterface;
class PsCfg;
template <typename T> class MlmeMsg;

// Configures the driver to send Beacon frames periodically.
class BeaconSender {
   public:
    explicit BeaconSender(DeviceInterface* device);
    ~BeaconSender();

    void Start(BssInterface* bss, const PsCfg& ps_cfg,
               const MlmeMsg<::fuchsia::wlan::mlme::StartRequest>& req);
    void Stop();
    zx_status_t UpdateBeacon(const PsCfg& ps_cfg);
    void SendProbeResponse(const MgmtFrameView<ProbeRequest>&);

   private:
    bool ShouldSendProbeResponse(const MgmtFrameView<ProbeRequest>&);

    zx_status_t BuildBeacon(const PsCfg& ps_cfg, MgmtFrame<Beacon>* frame, size_t* tim_ele_offset);

    bool IsStarted();

    DeviceInterface* const device_;
    ::fuchsia::wlan::mlme::StartRequest req_;
    BssInterface* bss_ = nullptr;
};

}  // namespace wlan

#endif  // GARNET_LIB_WLAN_MLME_INCLUDE_WLAN_MLME_AP_BEACON_SENDER_H_
