// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_DRIVERS_BLUETOOTH_LIB_L2CAP_BREDR_SIGNALING_CHANNEL_H_
#define GARNET_DRIVERS_BLUETOOTH_LIB_L2CAP_BREDR_SIGNALING_CHANNEL_H_

#include "garnet/drivers/bluetooth/lib/l2cap/signaling_channel.h"

namespace btlib {
namespace l2cap {
namespace internal {

class BrEdrSignalingChannel final : public SignalingChannel {
 public:
  BrEdrSignalingChannel(fbl::RefPtr<Channel> chan, hci::Connection::Role role);
  ~BrEdrSignalingChannel() override = default;

 private:
  // SignalingChannel overrides
  void DecodeRxUnit(const SDU& sdu, const PacketDispatchCallback& cb) override;
  bool HandlePacket(const SignalingPacket& packet) override;
};

}  // namespace internal
}  // namespace l2cap
}  // namespace btlib

#endif  // GARNET_DRIVERS_BLUETOOTH_LIB_L2CAP_BREDR_SIGNALING_CHANNEL_H_
