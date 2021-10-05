// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_LIB_MLME_CPP_INCLUDE_WLAN_MLME_DISPATCHER_H_
#define SRC_CONNECTIVITY_WLAN_LIB_MLME_CPP_INCLUDE_WLAN_MLME_DISPATCHER_H_

#include <fuchsia/wlan/mlme/cpp/fidl.h>
#include <fuchsia/wlan/stats/cpp/fidl.h>
#include <zircon/types.h>

#include <memory>

#include <wlan/common/stats.h>
#include <wlan/mlme/mac_frame.h>
#include <wlan/mlme/mlme.h>
#include <wlan/mlme/service.h>
#include <wlan/protocol/mac.h>

namespace wlan {

// The Dispatcher converts Packets, forwarded by the Device, into concrete
// frames, such as management frames, or service messages.

class Dispatcher {
 public:
  explicit Dispatcher(DeviceInterface* device, std::unique_ptr<Mlme> mlme);
  ~Dispatcher();

  zx_status_t HandlePacket(std::unique_ptr<Packet>);
  zx_status_t HandlePortPacket(uint64_t key);
  zx_status_t HandleAnyMlmeMessage(cpp20::span<uint8_t> span);

  // Called when the hardware reports an indication such as Pre-TBTT.
  void HwIndication(uint32_t ind);
  void HwScanComplete(uint8_t result_code);
  void ResetStats();
  ::fuchsia::wlan::mlme::StatsQueryResponse GetStatsToFidl() const;

 private:
  template <typename Message>
  zx_status_t HandleMinstrelPeerList(uint64_t ordinal, zx_txid_t txid) const;
  zx_status_t HandleMinstrelTxStats(cpp20::span<uint8_t> span, uint64_t ordinal,
                                    zx_txid_t txid) const;
  template <typename T>
  zx_status_t SendServiceMessage(uint64_t ordinal, T* msg) const;

  DeviceInterface* device_;
  // The MLME that will handle requests for this dispatcher. This field will be
  // set upon querying the underlying DeviceInterface, based on the role of the
  // device (e.g., Client or AP).
  std::unique_ptr<Mlme> mlme_ = nullptr;
  common::WlanStats<common::DispatcherStats, ::fuchsia::wlan::stats::DispatcherStats> stats_;
};

}  // namespace wlan

#endif  // SRC_CONNECTIVITY_WLAN_LIB_MLME_CPP_INCLUDE_WLAN_MLME_DISPATCHER_H_
