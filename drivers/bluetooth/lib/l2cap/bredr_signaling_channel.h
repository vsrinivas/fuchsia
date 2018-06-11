// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_DRIVERS_BLUETOOTH_LIB_L2CAP_BREDR_SIGNALING_CHANNEL_H_
#define GARNET_DRIVERS_BLUETOOTH_LIB_L2CAP_BREDR_SIGNALING_CHANNEL_H_

#include <unordered_map>

#include "garnet/drivers/bluetooth/lib/l2cap/signaling_channel.h"

namespace btlib {
namespace l2cap {
namespace internal {

// Implements packet processing for the BR/EDR signaling channel (CID = 1).
// Callbacks will be run on the thread where packet reception occurs, which is
// the L2CAP thread in production.
class BrEdrSignalingChannel final : public SignalingChannel {
 public:
  // Called to indicate reception of some data payload.
  using DataCallback = fit::function<void(const common::ByteBuffer& data)>;

  BrEdrSignalingChannel(fbl::RefPtr<Channel> chan, hci::Connection::Role role);
  ~BrEdrSignalingChannel() override = default;

  // Test the link using an Echo Request command that can have an arbitrary
  // payload. The callback will be invoked with the remote's Echo Response
  // payload (if any) on the L2CAP thread. Returns false if the request failed
  // to send.
  bool TestLink(const common::ByteBuffer& data, DataCallback cb);

private:
  // Invoked upon response command reception that matches an outgoing request.
  using ResponseHandler = fit::function<void(const SignalingPacket& packet)>;

  // SignalingChannel overrides
  void DecodeRxUnit(const SDU& sdu, const PacketDispatchCallback& cb) override;
  bool HandlePacket(const SignalingPacket& packet) override;

  // Register a callback that will be invoked when a response-type command
  // packet (specified by |expected_code|) is received. Returns the identifier
  // to be included in the header of the outgoing request packet (or
  // kInvalidCommandId if all valid command identifiers are pending responses).
  // |handler| will be run on the L2CAP thread.
  //
  // TODO(xow): Add function to cancel a queued response.
  CommandId EnqueueResponse(CommandCode expected_code, ResponseHandler handler);

  // True if the code is for a supported ACL-U response-type signaling command.
  bool IsSupportedResponse(CommandCode code) const;

  // True if an outbound request-type command has registered a callback for its
  // response matching a particular |id|.
  bool IsCommandPending(CommandId id) const;

  // Called when a response-type command packet is received. Sends a Command
  // Reject if no ResponseHandler was registered for inbound packet's command
  // code and identifier.
  void OnRxResponse(const SignalingPacket& packet);

  // Stores response handlers for requests that have been sent.
  std::unordered_map<CommandId, std::pair<CommandCode, ResponseHandler>>
      pending_commands_;
};

}  // namespace internal
}  // namespace l2cap
}  // namespace btlib

#endif  // GARNET_DRIVERS_BLUETOOTH_LIB_L2CAP_BREDR_SIGNALING_CHANNEL_H_
