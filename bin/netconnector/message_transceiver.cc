// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/netconnector/message_transceiver.h"

#include <errno.h>
#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>

#include "lib/fxl/logging.h"

namespace netconnector {

MessageTransceiver::MessageTransceiver(fxl::UniqueFD socket_fd)
    : socket_fd_(std::move(socket_fd)),
      dispatcher_(async_get_default_dispatcher()),
      receive_buffer_(kRecvBufferSize) {
  FXL_DCHECK(socket_fd_.is_valid());
  FXL_DCHECK(dispatcher_);

  message_relay_.SetMessageReceivedCallback(
      [this](std::vector<uint8_t> message) {
        SendMessage(std::move(message));
      });

  message_relay_.SetChannelClosedCallback([this]() { CloseConnection(); });

  SendVersionPacket();

  WaitToReceive();
}

MessageTransceiver::~MessageTransceiver() { CancelWaiters(); }

void MessageTransceiver::SetChannel(zx::channel channel) {
  FXL_DCHECK(channel);

  if (!socket_fd_.is_valid()) {
    return;
  }

  if (version_ != kNullVersion) {
    message_relay_.SetChannel(std::move(channel));
  } else {
    // Version exchange hasn't occurred yet. Postpone setting the channel on the
    // relay until it does, because we don't want messages sent over the network
    // until the version of the remote party is known.
    channel_.swap(channel);
  }
}

void MessageTransceiver::SendServiceName(const std::string& service_name) {
  if (!socket_fd_.is_valid()) {
    FXL_LOG(WARNING) << "SendServiceName called with closed connection";
    return;
  }

  PostSendTask([this, service_name = service_name]() {
    SendPacket(PacketType::kServiceName, service_name.data(),
               service_name.size());
  });
}

void MessageTransceiver::SendMessage(std::vector<uint8_t> message) {
  if (!socket_fd_.is_valid()) {
    FXL_LOG(WARNING) << "SendMessage called with closed connection";
    return;
  }

  PostSendTask([this, m = std::move(message)]() {
    SendPacket(PacketType::kMessage, m.data(), m.size());
  });
}

void MessageTransceiver::CloseConnection() {
  if (socket_fd_.is_valid()) {
    CancelWaiters();
    socket_fd_.reset();
    async::PostTask(dispatcher_, [this]() {
      channel_.reset();
      message_relay_.CloseChannel();
      OnConnectionClosed();
    });
  }
}

void MessageTransceiver::OnMessageReceived(std::vector<uint8_t> message) {
  message_relay_.SendMessage(std::move(message));
}

void MessageTransceiver::OnConnectionClosed() {}

void MessageTransceiver::SendVersionPacket() {
  PostSendTask([this]() {
    uint32_t version = htonl(kVersion);
    SendPacket(PacketType::kVersion, &version, sizeof(version));
  });
}

void MessageTransceiver::PostSendTask(fit::closure task) {
  FXL_DCHECK(socket_fd_.is_valid()) << "PostSendTask with invalid socket.";
  send_tasks_.push(std::move(task));
  if (send_tasks_.size() == 1) {
    MaybeWaitToSend();
  }
}

void MessageTransceiver::MaybeWaitToSend() {
  if (send_tasks_.empty()) {
    return;
  }

  if (!fd_send_waiter_.Wait(
          [this](zx_status_t status, uint32_t events) {
            FXL_DCHECK(!send_tasks_.empty());
            auto task = std::move(send_tasks_.front());
            send_tasks_.pop();
            task();
          },
          socket_fd_.get(), POLLOUT)) {
    // Wait failed because the fd is no longer valid. We need to clear
    // |send_tasks_| before we proceeed, because a non-empty send_tasks_
    // implies the need to cancel the wait.
    std::queue<fit::closure> doomed;
    send_tasks_.swap(doomed);
    CloseConnection();
  }
}

void MessageTransceiver::SendPacket(PacketType type, const void* payload,
                                    size_t payload_size) {
  FXL_DCHECK(payload_size == 0 || payload != nullptr);

  PacketHeader packet_header;

  packet_header.sentinel_ = kSentinel;
  packet_header.type_ = type;
  packet_header.channel_ = 0;
  packet_header.payload_size_ = htonl(payload_size);

  int result = send(socket_fd_.get(), &packet_header, sizeof(packet_header), 0);
  if (result == -1) {
    FXL_LOG(ERROR) << "Failed to send, errno " << errno;
    CloseConnection();
    return;
  }

  FXL_DCHECK(result == static_cast<int>(sizeof(packet_header)));

  if (payload_size == 0) {
    MaybeWaitToSend();
    return;
  }

  result = send(socket_fd_.get(), payload, payload_size, 0);
  if (result == -1) {
    FXL_LOG(ERROR) << "Failed to send, errno " << errno;
    CloseConnection();
    return;
  }

  FXL_DCHECK(result == static_cast<int>(payload_size));
  MaybeWaitToSend();
}

void MessageTransceiver::WaitToReceive() {
  fd_recv_waiter_waiting_ = true;
  if (!fd_recv_waiter_.Wait(
          [this](zx_status_t status, uint32_t events) {
            fd_recv_waiter_waiting_ = false;
            ReceiveMessage();
          },
          socket_fd_.get(), POLLIN)) {
    fd_recv_waiter_waiting_ = false;
    CloseConnection();
  }
}

void MessageTransceiver::ReceiveMessage() {
  int result =
      recv(socket_fd_.get(), receive_buffer_.data(), receive_buffer_.size(), 0);
  if (result == -1) {
    // If we got EIO and socket_fd_ isn't valid, recv failed because the
    // socket was closed locally.
    if (errno != EIO || socket_fd_.is_valid()) {
      FXL_LOG(ERROR) << "Failed to receive, errno " << errno;
    }

    CloseConnection();
    return;
  }

  if (result == 0) {
    // The remote party closed the connection.
    CloseConnection();
    return;
  }

  ParseReceivedBytes(result);
  WaitToReceive();
}

// Determines whether the indicated field in the packet header has been
// received.
#define PacketHeaderFieldReceived(field)                        \
  (receive_packet_offset_ >=                                    \
   (reinterpret_cast<uint8_t*>(&receive_packet_header_.field) - \
    reinterpret_cast<uint8_t*>(&receive_packet_header_)) +      \
       sizeof(receive_packet_header_.field))

void MessageTransceiver::ParseReceivedBytes(size_t byte_count) {
  uint8_t* bytes = receive_buffer_.data();

  while (byte_count != 0) {
    if (receive_packet_offset_ < sizeof(receive_packet_header_)) {
      bool header_complete =
          CopyReceivedBytes(&bytes, &byte_count,
                            reinterpret_cast<uint8_t*>(&receive_packet_header_),
                            sizeof(receive_packet_header_), 0);

      if (PacketHeaderFieldReceived(sentinel_) &&
          receive_packet_header_.sentinel_ != kSentinel) {
        FXL_LOG(ERROR) << "Received bad packet sentinel "
                       << receive_packet_header_.sentinel_;
        CloseConnection();
        return;
      }

      if (PacketHeaderFieldReceived(type_) &&
          receive_packet_header_.type_ > PacketType::kMax) {
        FXL_LOG(ERROR) << "Received bad packet type "
                       << static_cast<uint8_t>(receive_packet_header_.type_);
        CloseConnection();
        return;
      }

      // If we ever use channel_, we'll need to make sure we fix its byte
      // order exactly once. For now, 0 is 0 regardless of byte order.
      if (PacketHeaderFieldReceived(channel_) &&
          receive_packet_header_.channel_ != 0) {
        FXL_LOG(ERROR) << "Received bad channel id "
                       << receive_packet_header_.channel_;
        CloseConnection();
        return;
      }

      if (header_complete) {
        receive_packet_header_.payload_size_ =
            ntohl(receive_packet_header_.payload_size_);
        if (receive_packet_header_.payload_size_ > kMaxPayloadSize) {
          FXL_LOG(ERROR) << "Received bad payload size "
                         << receive_packet_header_.payload_size_;
          CloseConnection();
          return;
        }

        receive_packet_payload_.resize(receive_packet_header_.payload_size_);
      }
    }

    if (CopyReceivedBytes(&bytes, &byte_count, receive_packet_payload_.data(),
                          receive_packet_payload_.size(),
                          sizeof(PacketHeader))) {
      // Packet complete.
      receive_packet_offset_ = 0;
      OnReceivedPacketComplete();
    }
  }
}

bool MessageTransceiver::CopyReceivedBytes(uint8_t** bytes, size_t* byte_count,
                                           uint8_t* dest, size_t dest_size,
                                           size_t dest_packet_offset) {
  FXL_DCHECK(bytes != nullptr);
  FXL_DCHECK(*bytes != nullptr);
  FXL_DCHECK(byte_count != nullptr);
  FXL_DCHECK(dest != nullptr);
  FXL_DCHECK(dest_size != 0);
  FXL_DCHECK(dest_packet_offset <= receive_packet_offset_);
  FXL_DCHECK(receive_packet_offset_ < dest_packet_offset + dest_size);

  size_t dest_offset = receive_packet_offset_ - dest_packet_offset;
  size_t bytes_to_copy = std::min(*byte_count, dest_size - dest_offset);

  if (bytes_to_copy != 0) {
    std::memcpy(dest + dest_offset, *bytes, bytes_to_copy);

    *byte_count -= bytes_to_copy;
    *bytes += bytes_to_copy;
    dest_offset += bytes_to_copy;
    receive_packet_offset_ += bytes_to_copy;
  }

  return dest_offset == dest_size;
}

void MessageTransceiver::OnReceivedPacketComplete() {
  switch (receive_packet_header_.type_) {
    case PacketType::kVersion:
      if (version_ != kNullVersion) {
        FXL_LOG(ERROR) << "Version packet received out of order";
        CloseConnection();
        return;
      }

      if (receive_packet_header_.payload_size_ != sizeof(uint32_t)) {
        FXL_LOG(ERROR) << "Version packet has bad payload size "
                       << receive_packet_header_.payload_size_;
        CloseConnection();
        return;
      }

      version_ = ParsePayloadUint32();

      if (version_ < kMinSupportedVersion) {
        FXL_LOG(ERROR) << "Unsupported version " << version_;
        CloseConnection();
        return;
      }

      async::PostTask(dispatcher_, [this, version = version_]() {
        OnVersionReceived(version);
        if (socket_fd_.is_valid() && channel_) {
          // We've postponed setting the channel on the relay until now, because
          // we don't want messages sent over the network until the version of
          // the remote party is known.
          message_relay_.SetChannel(std::move(channel_));
        }
      });

      if (version_ > kVersion) {
        version_ = kVersion;
      }
      break;

    case PacketType::kServiceName:
      if (version_ == kNullVersion) {
        FXL_LOG(ERROR) << "Service name packet received when version "
                          "packet was expected";
        CloseConnection();
        return;
      }

      if (receive_packet_header_.payload_size_ == 0 ||
          receive_packet_header_.payload_size_ > kMaxServiceNameLength) {
        FXL_LOG(ERROR) << "Service name packet has bad payload size "
                       << receive_packet_header_.payload_size_;
        CloseConnection();
        return;
      }

      async::PostTask(dispatcher_, [this, service_name = ParsePayloadString()]() {
        OnServiceNameReceived(service_name);
      });
      break;

    case PacketType::kMessage:
      if (version_ == kNullVersion) {
        FXL_LOG(ERROR) << "Message packet received when version "
                          "packet was expected";
        CloseConnection();
        return;
      }

      async::PostTask(
          dispatcher_,
          [this, payload = std::move(receive_packet_payload_)]() mutable {
            OnMessageReceived(std::move(payload));
          });
      break;

    default:
      FXL_CHECK(false);  // ParseReceivedBytes shouldn't have let this through.
      break;
  }
}

uint32_t MessageTransceiver::ParsePayloadUint32() {
  uint32_t net_byte_order_result;
  FXL_DCHECK(receive_packet_payload_.size() == sizeof(net_byte_order_result));
  std::memcpy(&net_byte_order_result, receive_packet_payload_.data(),
              sizeof(net_byte_order_result));
  return ntohl(net_byte_order_result);
}

std::string MessageTransceiver::ParsePayloadString() {
  return std::string(reinterpret_cast<char*>(receive_packet_payload_.data()),
                     receive_packet_payload_.size());
}

void MessageTransceiver::CancelWaiters() {
  if (!send_tasks_.empty()) {
    fd_send_waiter_.Cancel();
    std::queue<fit::closure> doomed;
    send_tasks_.swap(doomed);
  }

  if (fd_recv_waiter_waiting_) {
    fd_recv_waiter_.Cancel();
    fd_recv_waiter_waiting_ = false;
  }
}

}  // namespace netconnector
