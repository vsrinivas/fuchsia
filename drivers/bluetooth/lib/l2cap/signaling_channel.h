// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_DRIVERS_BLUETOOTH_LIB_L2CAP_SIGNALING_CHANNEL_H_
#define GARNET_DRIVERS_BLUETOOTH_LIB_L2CAP_SIGNALING_CHANNEL_H_

#include <memory>

#include "garnet/drivers/bluetooth/lib/common/byte_buffer.h"
#include "garnet/drivers/bluetooth/lib/common/packet_view.h"
#include "garnet/drivers/bluetooth/lib/hci/connection.h"
#include "garnet/drivers/bluetooth/lib/l2cap/l2cap_defs.h"
#include "garnet/drivers/bluetooth/lib/l2cap/scoped_channel.h"
#include "garnet/drivers/bluetooth/lib/l2cap/sdu.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/synchronization/thread_checker.h"

namespace btlib {
namespace l2cap {

class Channel;

namespace internal {

using SignalingPacket = common::PacketView<CommandHeader>;
using MutableSignalingPacket = common::MutablePacketView<CommandHeader>;

// SignalingChannel is an abstract class that handles the common operations
// involved in LE and BR/EDR signaling channels.
//
// TODO(armansito): Implement flow control (RTX/ERTX timers).
class SignalingChannel {
 public:
  SignalingChannel(fbl::RefPtr<Channel> chan, hci::Connection::Role role);
  virtual ~SignalingChannel();

  bool is_open() const { return is_open_; }

  // Local signaling MTU (i.e. MTU_sig, per spec)
  uint16_t mtu() const { return mtu_; }
  void set_mtu(uint16_t mtu) { mtu_ = mtu; }

 protected:
  using PacketDispatchCallback =
      fit::function<void(const SignalingPacket& packet)>;

  // Sends out a single signaling packet using the given parameters.
  bool SendPacket(CommandCode code, uint8_t identifier,
                  const common::ByteBuffer& data);

  // Called when a frame is received to decode into L2CAP signaling command
  // packets. The derived implementation should invoke |cb| for each packet with
  // a valid payload length, send a Command Reject packet for each packet with
  // an intact ID in its header but invalid payload length, and drop any other
  // incoming data.
  virtual void DecodeRxUnit(const SDU& sdu,
                            const PacketDispatchCallback& cb) = 0;

  // Called when a new signaling packet has been received. Returns false if
  // |packet| is rejected. Otherwise returns true and sends a response packet.
  //
  // This method is thread-safe in that a SignalingChannel cannot be deleted
  // while this is running. SendPacket() can be called safely from this method.
  virtual bool HandlePacket(const SignalingPacket& packet) = 0;

  // Sends out a command reject packet with the given parameters.
  bool SendCommandReject(uint8_t identifier, RejectReason reason,
                         const common::ByteBuffer& data);

  // Returns true if called on this SignalingChannel's creation thread. Mainly
  // intended for debug assertions.
  bool IsCreationThreadCurrent() const {
    return thread_checker_.IsCreationThreadCurrent();
  }

  // Returns the logical link that signaling channel is operating on.
  hci::Connection::Role role() const { return role_; }

 private:
  // Sends out the given signaling packet directly via |chan_| after running
  // debug-mode assertions for validity. Packet must correspond to exactly one
  // C-frame payload.
  //
  // This method is not thread-safe (i.e. requires external locking).
  //
  // TODO(armansito): This should be generalized for ACL-U to allow multiple
  // signaling commands in a single C-frame.
  bool Send(std::unique_ptr<const common::ByteBuffer> packet);

  // Builds a signaling packet with the given parameters and payload. The
  // backing buffer is slab allocated.
  std::unique_ptr<common::ByteBuffer> BuildPacket(
      CommandCode code, uint8_t identifier, const common::ByteBuffer& data);

  // Channel callbacks:
  void OnChannelClosed();
  void OnRxBFrame(const SDU& sdu);

  // Invoke the abstract packet handler |HandlePacket| for well-formed command
  // packets and send responses for command packets that exceed this host's MTU
  // or can't be handled by this host.
  void CheckAndDispatchPacket(const SignalingPacket& packet);

  // Destroy all other members prior to this so they can use this for checking.
  fxl::ThreadChecker thread_checker_;

  bool is_open_;
  l2cap::ScopedChannel chan_;
  hci::Connection::Role role_;
  uint16_t mtu_;

  fxl::WeakPtrFactory<SignalingChannel> weak_ptr_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(SignalingChannel);
};

}  // namespace internal
}  // namespace l2cap
}  // namespace btlib

#endif  // GARNET_DRIVERS_BLUETOOTH_LIB_L2CAP_SIGNALING_CHANNEL_H_
