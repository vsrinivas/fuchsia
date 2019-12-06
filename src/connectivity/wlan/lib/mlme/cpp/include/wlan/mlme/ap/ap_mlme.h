// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_LIB_MLME_CPP_INCLUDE_WLAN_MLME_AP_AP_MLME_H_
#define SRC_CONNECTIVITY_WLAN_LIB_MLME_CPP_INCLUDE_WLAN_MLME_AP_AP_MLME_H_

#include <fuchsia/wlan/mlme/cpp/fidl.h>
#include <zircon/types.h>

#include <memory>

#include <wlan/mlme/mlme.h>
#include <wlan/mlme/packet.h>
#include <wlan/mlme/rust_utils.h>
#include <wlan/mlme/timer_manager.h>
#include <wlan/protocol/mac.h>

namespace wlan {

class BaseMlmeMsg;

// ApMlme is an MLME which operates in AP role. It is not thread-safe.
class ApMlme : public Mlme {
 public:
  explicit ApMlme(DeviceInterface* device);

  // Mlme interface methods.
  zx_status_t Init() override;
  zx_status_t HandleEncodedMlmeMsg(fbl::Span<const uint8_t> msg) override;
  zx_status_t HandleMlmeMsg(const BaseMlmeMsg& msg) override;
  zx_status_t HandleFramePacket(std::unique_ptr<Packet> pkt) override;
  zx_status_t HandleTimeout(const ObjectId id) override;
  void HwIndication(uint32_t ind) override;

 private:
  DeviceInterface* const device_;
  ApStation rust_ap_;
  std::unique_ptr<TimerManager<std::tuple<>>> timer_mgr_;
};

}  // namespace wlan

#endif  // SRC_CONNECTIVITY_WLAN_LIB_MLME_CPP_INCLUDE_WLAN_MLME_AP_AP_MLME_H_
