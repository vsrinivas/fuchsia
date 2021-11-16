// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_LIB_MLME_CPP_INCLUDE_WLAN_MLME_MLME_H_
#define SRC_CONNECTIVITY_WLAN_LIB_MLME_CPP_INCLUDE_WLAN_MLME_MLME_H_

#include <fuchsia/wlan/stats/cpp/fidl.h>
#include <lib/stdcompat/span.h>
#include <zircon/types.h>

#include <memory>

#include <wlan/common/bitfield.h>
#include <wlan/mlme/mac_frame.h>

namespace wlan {

class DeviceInterface;
class Packet;

// Mlme is the Mac sub-Layer Management Entity for the wlan driver.
// TODO(fxbug.dev/29063): Merge client and ap MLME into a single C++ representation.
class Mlme {
 public:
  virtual ~Mlme() {}
  virtual zx_status_t Init() = 0;

  virtual zx_status_t StopMainLoop() = 0;
  virtual zx_status_t QueueEthFrameTx(std::unique_ptr<Packet> pkt) = 0;
  // Called when the hardware reports an indication such as Pre-TBTT.
  virtual ::fuchsia::wlan::stats::MlmeStats GetMlmeStats() const { return {}; }
  virtual void ResetMlmeStats() {}
};

}  // namespace wlan

#endif  // SRC_CONNECTIVITY_WLAN_LIB_MLME_CPP_INCLUDE_WLAN_MLME_MLME_H_
