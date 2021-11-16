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

namespace wlan {

class DeviceInterface;
class Packet;
class BaseMlmeMsg;
class ChannelScheduler;
class Scanner;
class Station;

wlan_client_mlme_config_t ClientMlmeDefaultConfig();

// ClientMlme is a MLME which operates in non-AP role. It is not thread-safe.
// TODO(fxbug.dev/29063): Merge client and ap MLME into a single C++ representation.
class ClientMlme : public Mlme {
 public:
  ClientMlme(DeviceInterface* device, wlan_client_mlme_config_t config, bool run_as_test = false);
  ~ClientMlme();

  // Mlme interface methods.
  zx_status_t Init() override;
  zx_status_t StopMainLoop() override;
  zx_status_t QueueEthFrameTx(std::unique_ptr<Packet> pkt) override;

  // Testing methods. Use only if run_as_test is true.
  void AdvanceFakeTime(int64_t nanos);
  void RunUntilStalled();

  // Visible for tests only
  bool OnChannel();

 private:
  DeviceInterface* const device_;
  RustClientMlme rust_mlme_;
  wlan_client_mlme_config_t config_;
  bool run_as_test_;
};

}  // namespace wlan

#endif  // SRC_CONNECTIVITY_WLAN_LIB_MLME_CPP_INCLUDE_WLAN_MLME_CLIENT_CLIENT_MLME_H_
