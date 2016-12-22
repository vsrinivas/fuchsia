// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/netconnector/src/message_transceiver.h"

#include <errno.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>

#include "lib/ftl/logging.h"
#include "lib/mtl/tasks/message_loop.h"
#include "lib/mtl/threading/create_thread.h"

namespace netconnector {

MessageTransciever::MessageTransciever(ftl::UniqueFD socket_fd)
    : socket_fd_(std::move(socket_fd)),
      task_runner_(mtl::MessageLoop::GetCurrent()->task_runner()),
      receive_buffer_(kRecvBufferSize) {
  FTL_DCHECK(socket_fd_.is_valid());
  FTL_DCHECK(task_runner_);

  message_relay_.SetMessageReceivedCallback([this](
      std::vector<uint8_t> message) { SendMessage(std::move(message)); });

  message_relay_.SetChannelClosedCallback([this]() { CloseConnection(); });

  receive_thread_ = std::thread([this]() { ReceiveWorker(); });
  send_thread_ = mtl::CreateThread(&send_task_runner_);

  SendVersionPacket();
}

MessageTransciever::~MessageTransciever() {
  send_task_runner_->PostTask(
      []() { mtl::MessageLoop::GetCurrent()->QuitNow(); });

  if (receive_thread_.joinable()) {
    receive_thread_.join();
  }

  if (send_thread_.joinable()) {
    send_thread_.join();
  }
}

void MessageTransciever::SetChannel(mx::channel channel) {
  FTL_DCHECK(channel);

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

void MessageTransciever::SendServiceName(const std::string& service_name) {
  if (!socket_fd_.is_valid()) {
    FTL_LOG(WARNING) << "SendServiceName called with closed connection";
    return;
  }

  send_task_runner_->PostTask([ this, service_name = service_name ]() {
    SendPacket(PacketType::kServiceName, service_name.data(),
               service_name.size());
  });
}

void MessageTransciever::SendMessage(std::vector<uint8_t> message) {
  if (!socket_fd_.is_valid()) {
    FTL_LOG(WARNING) << "SendMessage called with closed connection";
    return;
  }

  send_task_runner_->PostTask([ this, m = std::move(message) ]() {
    SendPacket(PacketType::kMessage, m.data(), m.size());
  });
}

void MessageTransciever::CloseConnection() {
  if (socket_fd_.is_valid()) {
    socket_fd_.reset();
    task_runner_->PostTask([this]() {
      channel_.reset();
      message_relay_.CloseChannel();
      OnConnectionClosed();
    });
  }
}

void MessageTransciever::OnMessageReceived(std::vector<uint8_t> message) {
  message_relay_.SendMessage(std::move(message));
}

void MessageTransciever::OnConnectionClosed() {}

void MessageTransciever::SendVersionPacket() {
  send_task_runner_->PostTask([this]() {
    uint32_t version = htonl(kVersion);
    SendPacket(PacketType::kVersion, &version, sizeof(version));
  });
}

void MessageTransciever::SendPacket(PacketType type,
                                    const void* payload,
                                    size_t payload_size) {
  FTL_DCHECK(payload_size == 0 || payload != nullptr);

  PacketHeader packet_header;

  packet_header.sentinel_ = kSentinel;
  packet_header.type_ = type;
  packet_header.channel_ = 0;
  packet_header.payload_size_ = htonl(payload_size);

  int result = send(socket_fd_.get(), &packet_header, sizeof(packet_header),
                    payload_size == 0 ? 0 : MSG_MORE);
  if (result == -1) {
    FTL_LOG(ERROR) << "Failed to send, errno " << errno;
    CloseConnection();
    return;
  }

  FTL_DCHECK(result == static_cast<int>(sizeof(packet_header)));

  if (payload_size == 0) {
    return;
  }

  result = send(socket_fd_.get(), payload, payload_size, 0);
  if (result == -1) {
    FTL_LOG(ERROR) << "Failed to send, errno " << errno;
    CloseConnection();
    return;
  }

  FTL_DCHECK(result == static_cast<int>(payload_size));
}

void MessageTransciever::ReceiveWorker() {
  while (true) {
    int result = recv(socket_fd_.get(), receive_buffer_.data(),
                      receive_buffer_.size(), 0);
    if (result == -1) {
      // If we got EIO and socket_fd_ isn't valid, recv failed because the
      // socket was closed locally.
      if (errno != EIO || socket_fd_.is_valid()) {
        FTL_LOG(ERROR) << "Failed to receive, errno " << errno;
      }
      CloseConnection();
      break;
    }

    if (result == 0) {
      // The remote party closed the connection.
      CloseConnection();
      break;
    }

    ParseReceivedBytes(result);
  }
}

// Determines whether the indicated field in the packet header has been
// received.
#define PacketHeaderFieldReceived(field)                        \
  (receive_packet_offset_ >=                                    \
   (reinterpret_cast<uint8_t*>(&receive_packet_header_.field) - \
    reinterpret_cast<uint8_t*>(&receive_packet_header_)) +      \
       sizeof(receive_packet_header_.field))

void MessageTransciever::ParseReceivedBytes(size_t byte_count) {
  uint8_t* bytes = receive_buffer_.data();

  while (byte_count != 0) {
    if (receive_packet_offset_ < sizeof(receive_packet_header_)) {
      bool header_complete =
          CopyReceivedBytes(&bytes, &byte_count,
                            reinterpret_cast<uint8_t*>(&receive_packet_header_),
                            sizeof(receive_packet_header_), 0);

      if (PacketHeaderFieldReceived(sentinel_) &&
          receive_packet_header_.sentinel_ != kSentinel) {
        FTL_LOG(ERROR) << "Received bad packet sentinel "
                       << receive_packet_header_.sentinel_;
        CloseConnection();
        return;
      }

      if (PacketHeaderFieldReceived(type_) &&
          receive_packet_header_.type_ > PacketType::kMax) {
        FTL_LOG(ERROR) << "Received bad packet type "
                       << static_cast<uint8_t>(receive_packet_header_.type_);
        CloseConnection();
        return;
      }

      // If we ever use channel_, we'll need to make sure we fix its byte
      // order exactly once. For now, 0 is 0 regardless of byte order.
      if (PacketHeaderFieldReceived(channel_) &&
          receive_packet_header_.channel_ != 0) {
        FTL_LOG(ERROR) << "Received bad channel id "
                       << receive_packet_header_.channel_;
        CloseConnection();
        return;
      }

      if (header_complete) {
        receive_packet_header_.payload_size_ =
            ntohl(receive_packet_header_.payload_size_);
        if (receive_packet_header_.payload_size_ > kMaxPayloadSize) {
          FTL_LOG(ERROR) << "Received bad payload size "
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

bool MessageTransciever::CopyReceivedBytes(uint8_t** bytes,
                                           size_t* byte_count,
                                           uint8_t* dest,
                                           size_t dest_size,
                                           size_t dest_packet_offset) {
  FTL_DCHECK(bytes != nullptr);
  FTL_DCHECK(*bytes != nullptr);
  FTL_DCHECK(byte_count != nullptr);
  FTL_DCHECK(dest != nullptr);
  FTL_DCHECK(dest_size != 0);
  FTL_DCHECK(dest_packet_offset <= receive_packet_offset_);
  FTL_DCHECK(receive_packet_offset_ < dest_packet_offset + dest_size);

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

void MessageTransciever::OnReceivedPacketComplete() {
  switch (receive_packet_header_.type_) {
    case PacketType::kVersion:
      if (version_ != kNullVersion) {
        FTL_LOG(ERROR) << "Version packet received out of order";
        CloseConnection();
        return;
      }

      if (receive_packet_header_.payload_size_ != sizeof(uint32_t)) {
        FTL_LOG(ERROR) << "Version packet has bad payload size "
                       << receive_packet_header_.payload_size_;
        CloseConnection();
        return;
      }

      version_ = ParsePayloadUint32();

      if (version_ < kMinSupportedVersion) {
        FTL_LOG(ERROR) << "Unsupported version " << version_;
        CloseConnection();
        return;
      }

      task_runner_->PostTask([ this, version = version_ ]() {
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
        FTL_LOG(ERROR) << "Service name packet received when version "
                          "packet was expected";
        CloseConnection();
        return;
      }

      if (receive_packet_header_.payload_size_ == 0 ||
          receive_packet_header_.payload_size_ > kMaxServiceNameLength) {
        FTL_LOG(ERROR) << "Service name packet has bad payload size "
                       << receive_packet_header_.payload_size_;
        CloseConnection();
        return;
      }

      task_runner_->PostTask([ this, service_name = ParsePayloadString() ]() {
        OnServiceNameReceived(service_name);
      });
      break;

    case PacketType::kMessage:
      if (version_ == kNullVersion) {
        FTL_LOG(ERROR) << "Message packet received when version "
                          "packet was expected";
        CloseConnection();
        return;
      }

      task_runner_->PostTask([
        this, payload = std::move(receive_packet_payload_)
      ]() mutable { OnMessageReceived(std::move(payload)); });
      break;

    default:
      FTL_CHECK(false);  // ParseReceivedBytes shouldn't have let this through.
      break;
  }
}

uint32_t MessageTransciever::ParsePayloadUint32() {
  uint32_t net_byte_order_result;
  FTL_DCHECK(receive_packet_payload_.size() == sizeof(net_byte_order_result));
  std::memcpy(&net_byte_order_result, receive_packet_payload_.data(),
              sizeof(net_byte_order_result));
  return ntohl(net_byte_order_result);
}

std::string MessageTransciever::ParsePayloadString() {
  return std::string(reinterpret_cast<char*>(receive_packet_payload_.data()),
                     receive_packet_payload_.size());
}

}  // namespace netconnector
