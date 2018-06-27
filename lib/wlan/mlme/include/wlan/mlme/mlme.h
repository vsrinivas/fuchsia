// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fuchsia/wlan/stats/cpp/fidl.h>
#include <wlan/common/bitfield.h>
#include <wlan/mlme/mac_frame.h>
#include <wlan/protocol/mac.h>
#include <zircon/types.h>

namespace wlan {

enum class ObjectSubtype : uint8_t {
    kTimer = 0,
};

enum class ObjectTarget : uint8_t {
    kScanner = 0,
    kStation = 1,
    kBss = 2,
};

// An ObjectId is used as an id in a PortKey. Therefore, only the lower 56 bits may be used.
class ObjectId : public common::BitField<uint64_t> {
   public:
    constexpr explicit ObjectId(uint64_t id) : common::BitField<uint64_t>(id) {}
    constexpr ObjectId() = default;

    // ObjectSubtype
    WLAN_BIT_FIELD(subtype, 0, 4);
    // ObjectTarget
    WLAN_BIT_FIELD(target, 4, 4);

    // For objects with a MAC address
    WLAN_BIT_FIELD(mac, 8, 48);
};

class DeviceInterface;
class Packet;
class BaseMlmeMsg;

// Mlme is the Mac sub-Layer Management Entity for the wlan driver.
class Mlme {
   public:
    virtual ~Mlme() {}
    virtual zx_status_t Init() = 0;

    virtual zx_status_t HandleMlmeMsg(const BaseMlmeMsg& msg) = 0;
    virtual zx_status_t HandleFramePacket(fbl::unique_ptr<Packet> pkt) = 0;
    // Called before a channel change happens.
    virtual zx_status_t PreChannelChange(wlan_channel_t chan) = 0;
    // Called after a channel change is complete. The DeviceState channel will reflect the channel,
    // whether it changed or not.
    virtual zx_status_t PostChannelChange() = 0;
    virtual zx_status_t HandleTimeout(const ObjectId id) = 0;
    // Called when the hardware reports an indication such as Pre-TBTT.
    virtual void HwIndication(uint32_t ind) {};
    virtual ::fuchsia::wlan::stats::MlmeStats GetMlmeStats() const { return {}; };
};

}  // namespace wlan
