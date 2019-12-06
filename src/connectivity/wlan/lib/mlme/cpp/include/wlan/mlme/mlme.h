// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_LIB_MLME_CPP_INCLUDE_WLAN_MLME_MLME_H_
#define SRC_CONNECTIVITY_WLAN_LIB_MLME_CPP_INCLUDE_WLAN_MLME_MLME_H_

#include <fuchsia/wlan/stats/cpp/fidl.h>
#include <zircon/types.h>

#include <memory>

#include <fbl/span.h>
#include <wlan/common/bitfield.h>
#include <wlan/mlme/mac_frame.h>
#include <wlan/protocol/mac.h>

namespace wlan {

enum class ObjectSubtype : uint8_t {
  kTimer = 0,
};

enum class ObjectTarget : uint8_t {
  kClientMlme = 0,
  kApMlme = 2,
  kMinstrel = 3,
  kHwmp = 4,
};

// An ObjectId is used as an id in a PortKey. Therefore, only the lower 56 bits
// may be used.
class ObjectId : public common::BitField<uint64_t> {
 public:
  constexpr explicit ObjectId(uint64_t id) : common::BitField<uint64_t>(id) {}
  constexpr ObjectId() = default;

  // ObjectSubtype
  WLAN_BIT_FIELD(subtype, 0, 4)
  // ObjectTarget
  WLAN_BIT_FIELD(target, 4, 4)

  // For objects with a MAC address
  WLAN_BIT_FIELD(mac, 8, 48)
};

class DeviceInterface;
class Packet;
class BaseMlmeMsg;

// Mlme is the Mac sub-Layer Management Entity for the wlan driver.
class Mlme {
 public:
  virtual ~Mlme() {}
  virtual zx_status_t Init() = 0;

  // Temporary function to support processing inbound MLME messages in C++ and Rust.
  virtual zx_status_t HandleEncodedMlmeMsg(fbl::Span<const uint8_t> msg) = 0;

  virtual zx_status_t HandleMlmeMsg(const BaseMlmeMsg& msg) = 0;
  virtual zx_status_t HandleFramePacket(std::unique_ptr<Packet> pkt) = 0;
  virtual zx_status_t HandleTimeout(const ObjectId id) = 0;
  // Called when the hardware reports an indication such as Pre-TBTT.
  virtual void HwIndication(uint32_t ind) {}
  virtual void HwScanComplete(uint8_t result_code) {}
  virtual ::fuchsia::wlan::stats::MlmeStats GetMlmeStats() const { return {}; }
  virtual void ResetMlmeStats() {}
};

}  // namespace wlan

#endif  // SRC_CONNECTIVITY_WLAN_LIB_MLME_CPP_INCLUDE_WLAN_MLME_MLME_H_
