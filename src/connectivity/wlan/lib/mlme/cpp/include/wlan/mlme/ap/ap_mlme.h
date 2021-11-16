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

namespace wlan {

class BaseMlmeMsg;

// ApMlme is an MLME which operates in AP role. It is not thread-safe.
// TODO(fxbug.dev/29063): Merge client and ap MLME into a single C++ representation.
class ApMlme : public Mlme {
 public:
  explicit ApMlme(DeviceInterface* device, bool run_as_test = false);

  // Mlme interface methods.
  zx_status_t Init() override;
  zx_status_t StopMainLoop() override;
  zx_status_t QueueEthFrameTx(std::unique_ptr<Packet> pkt) override;

  // Testing methods. Use only if run_as_test is true.
  void AdvanceFakeTime(int64_t nanos);
  void RunUntilStalled();

 private:
  DeviceInterface* const device_;
  ApStation rust_ap_;
  bool run_as_test_;
};

}  // namespace wlan

#endif  // SRC_CONNECTIVITY_WLAN_LIB_MLME_CPP_INCLUDE_WLAN_MLME_AP_AP_MLME_H_
