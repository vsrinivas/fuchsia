// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_NETCONNECTOR_MESSAGE_TRANSCEIVER_H_
#define GARNET_BIN_NETCONNECTOR_MESSAGE_TRANSCEIVER_H_

#include <queue>
#include <vector>

#include <lib/async/dispatcher.h>
#include <lib/fit/function.h>
#include <lib/zx/channel.h>

#include "lib/fsl/tasks/fd_waiter.h"
#include "lib/fxl/files/unique_fd.h"
#include "lib/fxl/synchronization/thread_annotations.h"
#include "lib/netconnector/cpp/message_relay.h"

namespace netconnector {

/*

All packets conform to the following format:

    sentinel     (1 byte, 0xcc)
    type         (1 byte)
    channel      (2 bytes, 0x0000)
    payload size (4 bytes)
    payload      (<payload size> bytes)

The sentinel is just a sanity check, and the channel isn't used (always zeros).
All integers are in big-endian order.

Here are the types:

    version        (0x00) indicates the version of the sender
    service name   (0x01) indicates the name of the desired service
    message        (0x02) contains a message

A version packet has a 4-byte payload specifying the version of the sender.
Version packets are sent by both sides upon connection establishment. The format
of subsequent traffic on the connection must conform to the minimum of the two
version numbers. If either party isn't backward-compatible to that version, it
must close the connection.

A service name packet's payload consists of a string identifying the desired
service. The requestor sends a service name packet after the version packets
are exchanged. If the remote party doesn't recognize the service name,
it must close the connection.

A message packet contains a message intended for the requestor/service.

If either party receives a malformed packet, it must close the connection.

*/

// Abstract base class that shuttles data-only messages between a channel and
// a TCP socket.
//
// MessageTransceiver is not thread-safe. All methods calls must be serialized.
// All overridables will be called on the same thread on which the transceiver
// was constructed.
class MessageTransceiver {
 public:
  virtual ~MessageTransceiver();

 protected:
  MessageTransceiver(fxl::UniqueFD socket_fd);

  // Sets the channel that the transceiver should use to forward messages.
  void SetChannel(zx::channel channel);

  // Sends a service name.
  void SendServiceName(const std::string& service_name);

  // Sends a message.
  void SendMessage(std::vector<uint8_t> message);

  // Closes the connection.
  void CloseConnection();

  // Called when a version is received.
  virtual void OnVersionReceived(uint32_t version) = 0;

  // Called when a service name is received.
  virtual void OnServiceNameReceived(const std::string& service_name) = 0;

  // Called when a message is received. The default implementation puts the
  // message on the channel supplied by SetChannel.
  virtual void OnMessageReceived(std::vector<uint8_t> message);

  // Called when the connection closes. The default implementation does nothing.
  virtual void OnConnectionClosed();

 private:
  enum class PacketType : uint8_t {
    kVersion = 0,
    kServiceName = 1,
    kMessage = 2,
    kMax = 2
  };

  struct __attribute__((packed)) PacketHeader {
    uint8_t sentinel_;
    PacketType type_;
    uint16_t channel_;
    uint32_t payload_size_;
  };

  static const size_t kRecvBufferSize = 2048;
  static const uint8_t kSentinel = 0xcc;
  // TODO(dalesat): Make this larger when zx::channel messages can be larger.
  static const uint32_t kMaxPayloadSize = 65536;
  static const uint32_t kVersion = 1;
  static const uint32_t kNullVersion = 0;
  static const uint32_t kMinSupportedVersion = 1;
  static const size_t kMaxServiceNameLength = 1024;

  // Sends a version packet.
  void SendVersionPacket();

  // Queues up a task that calls |SendPacket| to be run when the socket is
  // ready.
  void PostSendTask(fit::closure task);

  // Waits (using |fd_send_waiter_|) for the socket to be ready to send if there
  // are send tasks pending.
  void MaybeWaitToSend();

  // Sends a packet. Must be called in the send thread.
  void SendPacket(PacketType type, const void* payload, size_t payload_size);

  // Waits (using |fd_recv_waiter_|) for an inbound message.
  void WaitToReceive();

  // Receives a message.
  void ReceiveMessage();

  // Parses |byte_count| received bytes from |receive_buffer_|.
  void ParseReceivedBytes(size_t byte_count);

  // Called when a complete packet has been received.
  void OnReceivedPacketComplete();

  // Cancels any waiters that are currently waiting.
  void CancelWaiters();

  // Copies received bytes.
  // |*bytes| points to the received bytes and is increased to reflect the
  //     number of bytes actually copied.
  // |*byte_count| indicates the number of bytes available to copy and is
  //     decreased to reflect the number of bytes actually copied.
  // |dest| is the destination buffer.
  // |dest_size| is the size of the destination buffer.
  // |dest_packet_offset| indicates where dest occurs logically in the packet.
  // Returns true if and only if dest is filled to its end.
  // |receive_packet_offset_| indicates where we are in the packet and must be
  // at least |dest_packet_offset| and less than the sum of |dest_packet_offset|
  // and |dest_size|. |receive_packet_offset_| is also increased to reflect the
  // number of bytes actually copied.
  bool CopyReceivedBytes(uint8_t** bytes, size_t* byte_count, uint8_t* dest,
                         size_t dest_size, size_t dest_packet_offset);

  // Parses a uint32 out of receive_buffer_.
  uint32_t ParsePayloadUint32();

  // Parses string out of receive_buffer_.
  std::string ParsePayloadString();

  fxl::UniqueFD socket_fd_;
  async_dispatcher_t* dispatcher_;
  zx::channel channel_;
  MessageRelay message_relay_;

  uint32_t version_ = kNullVersion;

  fsl::FDWaiter fd_recv_waiter_;
  bool fd_recv_waiter_waiting_ = false;
  std::vector<uint8_t> receive_buffer_;
  size_t receive_packet_offset_ = 0;
  PacketHeader receive_packet_header_;
  std::vector<uint8_t> receive_packet_payload_;

  // In general, |fd_send_waiter_| is waiting if and only if |send_tasks_| isn't
  // empty. The only exception to this is in the code that actually does the
  // sending (the waiter callback, |SendPacket| and the send tasks).
  fsl::FDWaiter fd_send_waiter_;
  std::queue<fit::closure> send_tasks_;

  FXL_DISALLOW_COPY_AND_ASSIGN(MessageTransceiver);
};

}  // namespace netconnector

#endif  // GARNET_BIN_NETCONNECTOR_MESSAGE_TRANSCEIVER_H_
