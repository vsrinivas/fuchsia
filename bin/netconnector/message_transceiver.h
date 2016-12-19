// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <queue>
#include <thread>
#include <vector>

#include <mx/channel.h>

#include "apps/netconnector/lib/message_relay.h"
#include "lib/ftl/files/unique_fd.h"
#include "lib/ftl/synchronization/mutex.h"
#include "lib/ftl/synchronization/thread_annotations.h"
#include "lib/ftl/tasks/task_runner.h"

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

    version          (0x00) indicates the version of the sender
    responder name   (0x01) indicates the name of the desired responder
    message          (0x02) contains a message

A version packet has a 4-byte payload specifying the version of the sender.
Version packets are sent by both sides upon connection establishment. The format
of subsequent traffic on the connection must conform to the minimum of the two
version numbers. If either party isn't backward-compatible to that version, it
must close the connection.

A responder name packet's payload consists of a string identifying the
desired responder. The requestor sends a selector packet after the version
packets are exchanged. If the remote party doesn't recognize the responder name,
it must close the connection.

A message packet contains a message intended for the requestor/responder.

If either party receives a malformed packet, it must close the connection.

*/

// Abstract base class that shuttles data-only messages between a channel and
// a TCP socket.
//
// MessageTransciever is not thread-safe. All methods calls must be serialized.
// All overridables will be called on the same thread on which the transceiver
// was constructed.
class MessageTransciever {
 public:
  virtual ~MessageTransciever();

 protected:
  MessageTransciever(ftl::UniqueFD socket_fd);

  // Sets the channel that the transceiver should use to forward messages.
  void SetChannel(mx::channel channel);

  // Sends a responder name.
  void SendResponderName(const std::string& responder_name);

  // Sends a message.
  void SendMessage(std::vector<uint8_t> message);

  // Closes the connection.
  void CloseConnection();

  // Called when a version is received.
  virtual void OnVersionReceived(uint32_t version) = 0;

  // Called when a responder name is received.
  virtual void OnResponderNameReceived(std::string responder_name) = 0;

  // Called when a message is received. The default implementation puts the
  // message on the channel supplied by SetChannel.
  virtual void OnMessageReceived(std::vector<uint8_t> message);

  // Called when the connection closes. The default implementation does nothing.
  virtual void OnConnectionClosed();

 private:
  enum class PacketType : uint8_t {
    kVersion = 0,
    kResponderName = 1,
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
  // TODO(dalesat): Make this larger when mx::channel messages can be larger.
  static const uint32_t kMaxPayloadSize = 65536;
  static const uint32_t kVersion = 1;
  static const uint32_t kNullVersion = 0;
  static const uint32_t kMinSupportedVersion = 1;
  static const size_t kMaxResponderNameLength = 1024;

  // Sends a version packet.
  void SendVersionPacket();

  // Sends a packet. Must be called in the send thread.
  void SendPacket(PacketType type, const void* payload, size_t payload_size);

  // Worker for the receive thread.
  void ReceiveWorker();

  // Parses |byte_count| received bytes from |receive_buffer_|.
  void ParseReceivedBytes(size_t byte_count);

  // Called when a complete packet has been received.
  void OnReceivedPacketComplete();

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
  bool CopyReceivedBytes(uint8_t** bytes,
                         size_t* byte_count,
                         uint8_t* dest,
                         size_t dest_size,
                         size_t dest_packet_offset);

  // Parses a uint32 out of receive_buffer_.
  uint32_t ParsePayloadUint32();

  // Parses string out of receive_buffer_.
  std::string ParsePayloadString();

  ftl::UniqueFD socket_fd_;
  ftl::RefPtr<ftl::TaskRunner> task_runner_;
  mx::channel channel_;
  MessageRelay message_relay_;

  uint32_t version_ = kNullVersion;

  std::thread receive_thread_;
  std::vector<uint8_t> receive_buffer_;
  size_t receive_packet_offset_ = 0;
  PacketHeader receive_packet_header_;
  std::vector<uint8_t> receive_packet_payload_;

  std::thread send_thread_;
  ftl::RefPtr<ftl::TaskRunner> send_task_runner_;

  FTL_DISALLOW_COPY_AND_ASSIGN(MessageTransciever);
};

}  // namespace netconnector
