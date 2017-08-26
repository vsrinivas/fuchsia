// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>

#include "garnet/drivers/bluetooth/lib/common/byte_buffer.h"
#include "garnet/drivers/bluetooth/lib/common/cancelable_callback.h"
#include "garnet/drivers/bluetooth/lib/common/packet_view.h"
#include "garnet/drivers/bluetooth/lib/hci/connection.h"
#include "garnet/drivers/bluetooth/lib/l2cap/l2cap.h"
#include "garnet/drivers/bluetooth/lib/l2cap/sdu.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/memory/ref_ptr.h"
#include "lib/fxl/synchronization/thread_checker.h"
#include "lib/fxl/tasks/task_runner.h"

namespace bluetooth {
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
  SignalingChannel(std::unique_ptr<Channel> chan, hci::Connection::Role role);
  virtual ~SignalingChannel();

  bool is_open() const { return is_open_; }

  // Local signaling MTU (i.e. MTU_sig, per spec)
  uint16_t mtu() const { return mtu_; }
  void set_mtu(uint16_t mtu) { mtu_ = mtu; }

 protected:
  // Sends out a single signaling packet using the given parameters.
  bool SendPacket(CommandCode code,
                  uint8_t identifier,
                  const common::ByteBuffer& data);

  // Called when a new signaling packet has been received. Returns false if
  // |packet| is rejected. Otherwise returns true and sends a response packet.
  //
  // This method is thread-safe in that a SignalingChannel cannot be deleted
  // while this is running. SendPacket() can be called safely from this method.
  virtual bool HandlePacket(const SignalingPacket& packet) = 0;

  // Sends out a command reject packet with the given parameters.
  bool SendCommandReject(uint8_t identifier,
                         RejectReason reason,
                         const common::ByteBuffer& data);

  // Returns true if called on this SignalingChannel's creation thread. Mainly
  // intended for debug assertions.
  bool IsCreationThreadCurrent() const {
    return thread_checker_.IsCreationThreadCurrent();
  }

  // Returns the logicak link that signaling channel is operating on.
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
      CommandCode code,
      uint8_t identifier,
      const common::ByteBuffer& data);

  // Channel callbacks:
  void OnChannelClosed();
  void OnRxBFrame(const SDU& sdu);

  void ProcessLeSigSdu(const SDU& sdu);
  void ProcessBrEdrSigSdu(const SDU& sdu);
  void ProcessPacket(const SignalingPacket& packet);

  bool is_open_;
  std::unique_ptr<Channel> chan_;
  hci::Connection::Role role_;
  uint16_t mtu_;

  // Used to cancel OnRxBFrame()
  common::CancelableCallbackFactory<void(const SDU& sdu)> rx_cb_factory_;

  // Task runner on which to process received signaling packets.
  fxl::RefPtr<fxl::TaskRunner> task_runner_;

  fxl::ThreadChecker thread_checker_;

  FXL_DISALLOW_COPY_AND_ASSIGN(SignalingChannel);
};

}  // namespace internal
}  // namespace l2cap
}  // namespace bluetooth
