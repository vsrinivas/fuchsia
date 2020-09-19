// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_LIB_MLME_CPP_INCLUDE_WLAN_MLME_CLIENT_CLIENT_MLME_H_
#define SRC_CONNECTIVITY_WLAN_LIB_MLME_CPP_INCLUDE_WLAN_MLME_CLIENT_CLIENT_MLME_H_

#include <fuchsia/wlan/stats/cpp/fidl.h>
#include <zircon/types.h>

#include <memory>
#include <optional>

#include <wlan/mlme/mlme.h>
#include <wlan/mlme/rust_utils.h>
#include <wlan/mlme/service.h>
#include <wlan/mlme/timer_manager.h>
#include <wlan/protocol/mac.h>

namespace wlan {

class DeviceInterface;
class Packet;
class BaseMlmeMsg;
class ChannelScheduler;
class Scanner;
class Station;

// ClientMlme is a MLME which operates in non-AP role. It is not thread-safe.
class ClientMlme : public Mlme {
 public:
  explicit ClientMlme(DeviceInterface* device);
  ClientMlme(DeviceInterface* device, wlan_client_mlme_config_t config);
  ~ClientMlme();

  // Mlme interface methods.
  zx_status_t Init() override;
  zx_status_t HandleEncodedMlmeMsg(fbl::Span<const uint8_t> msg) override;
  zx_status_t HandleMlmeMsg(const BaseMlmeMsg& msg) override;
  zx_status_t HandleFramePacket(std::unique_ptr<Packet> pkt) override;
  zx_status_t HandleTimeout(const ObjectId id) override;
  void HwScanComplete(uint8_t code) final;

  // Visible for tests only
  bool OnChannel();

 private:
  DeviceInterface* const device_;
  std::unique_ptr<TimerManager<>> timer_mgr_;
  RustClientMlme rust_mlme_;
  wlan_client_mlme_config_t config_;
};

}  // namespace wlan

#endif  // SRC_CONNECTIVITY_WLAN_LIB_MLME_CPP_INCLUDE_WLAN_MLME_CLIENT_CLIENT_MLME_H_
