// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "server.h"

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdlib>
#include <string>
#include <vector>

#include "lib/ftl/logging.h"

#include "util.h"

namespace debugserver {
namespace {

void LogWithErrno(const std::string& message) {
  FTL_LOG(ERROR) << message << " (errno = " << errno << ", \""
                 << strerror(errno) << "\")";
}

// Verifies that the given command is formatted correctly and that the checksum
// is correct. Returns false verification fails. Otherwise returns true, and
// returns a pointer to the beginning of the packet data and the size of the
// packet data in the out parameters. A GDB Remote Protocol packet is defined
// as:
//
//   $<packet-data>#<2-digit checksum>
//
bool VerifyPacket(const uint8_t* packet,
                  size_t packet_size,
                  const uint8_t** out_packet_data,
                  size_t* out_packet_data_size) {
  FTL_DCHECK(packet);
  FTL_DCHECK(out_packet_data);
  FTL_DCHECK(out_packet_data_size);

  // The packet should contain at least 4 bytes ($, #, 2-digit checksum).
  if (packet_size < 4)
    return false;

  // TODO(armansito): Not all packets need to start with '$', e.g. notifications
  // and acknowledgments. Handle those here as well.
  if (packet[0] != '$') {
    FTL_LOG(ERROR) << "Packet does not start with \"$\"";
    return false;
  }

  const uint8_t* pound = nullptr;
  for (size_t i = 1; i < packet_size; ++i) {
    if (packet[i] != '#')
      continue;

    pound = packet + i;
  }

  if (!pound) {
    FTL_LOG(ERROR) << "Packet does not contain \"#\"";
    return false;
  }

  const uint8_t* packet_data = packet + 1;
  size_t packet_data_size = pound - packet_data;

  // Extract the packet checksum

  // First check if the packet contains the 2 digit checksum. The difference
  // between the payload size and the full packet size should exactly match the
  // number of required characters (i.e. '$', '#', and checksum).
  if (packet_size - packet_data_size != 4) {
    FTL_LOG(ERROR) << "Packet does not contain 2 digit checksum";
    return false;
  }

  // TODO(armansito): Ignore the checksum if we're in no-acknowledgment mode.

  uint8_t received_checksum;
  if (!util::DecodeByteString(pound + 1, &received_checksum)) {
    FTL_LOG(ERROR) << "Malformed packet checksum received";
    return false;
  }

  // Compute the checksum over packet payload
  uint8_t local_checksum = 0;
  for (size_t i = 0; i < packet_data_size; ++i) {
    local_checksum += packet_data[i];
  }

  if (local_checksum != received_checksum) {
    FTL_LOG(ERROR) << "Bad checksum: computed = " << local_checksum
                   << ", received = " << received_checksum;
    return false;
  }

  *out_packet_data = packet_data;
  *out_packet_data_size = packet_data_size;

  return true;
}

}  // namespace

Server::Server(uint16_t port)
    : port_(port), client_sock_(-1), server_sock_(-1), run_status_(true) {}

bool Server::Run() {
  // Listen for an incoming connection.
  if (!Listen())
    return false;

  // |client_sock_| should be ready to be consumed now.
  FTL_DCHECK(client_sock_.is_valid());

  // Start the main loop after posting a task to wait for the first incoming
  // command packet.
  PostReadTask();
  message_loop_.Run();

  return run_status_;
}

bool Server::Listen() {
  FTL_DCHECK(!server_sock_.is_valid());
  FTL_DCHECK(!client_sock_.is_valid());

  ftl::UniqueFD server_sock(socket(AF_INET, SOCK_STREAM, 0));
  if (!server_sock.is_valid()) {
    LogWithErrno("Failed to open socket");
    return false;
  }

  // Bind to a local address for listening.
  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(port_);

  if (bind(server_sock.get(), (struct sockaddr*)&addr, sizeof(addr)) < 0) {
    LogWithErrno("Failed to bind socket");
    return false;
  }

  if (listen(server_sock.get(), 1) < 0) {
    LogWithErrno("Listen failed");
    return false;
  }

  FTL_LOG(INFO) << "Waiting for a connection on port " << port_ << "...";

  // Reuse |addr| here for the destination address.
  socklen_t addrlen = sizeof(addr);
  ftl::UniqueFD client_sock(
      accept(server_sock.get(), (struct sockaddr*)&addr, &addrlen));
  if (!client_sock.is_valid()) {
    LogWithErrno("Accept failed");
    return false;
  }

  FTL_LOG(INFO) << "Client connected";

  server_sock_ = std::move(server_sock);
  client_sock_ = std::move(client_sock);

  return true;
}

bool Server::SendAck(bool ack) {
  // TODO(armansito): Don't send anything if we're in no-acknowledgment mode. We
  // currently don't support this mode.

  uint8_t payload = ack ? '+' : '-';
  ssize_t bytes_written = write(client_sock_.get(), &payload, 1);

  return bytes_written > 0;
}

void Server::PostReadTask() {
  message_loop_.task_runner()->PostTask([this] {
    ssize_t bytes_read =
        read(client_sock_.get(), in_buffer_.data(), kMaxBufferSize);

    // If the remote end closed the TCP connection, then we're done.
    // TODO(armansito): Note that this is not the default (or only) behavior
    // that a stub usually supports and there are ways for gdbserver to stay up
    // even when a client (gdb, lldb, etc) closes the connection. We will worry
    // about supporting those later.
    if (bytes_read == 0) {
      FTL_LOG(INFO) << "Client closed connection";
      QuitMessageLoop(true);
      return;
    }

    // If there was an error
    if (bytes_read < 0) {
      LogWithErrno("Error occured while waiting for command");
      QuitMessageLoop(false);
      return;
    }

    const uint8_t* packet_data = nullptr;
    size_t packet_size = 0;
    bool verified =
        VerifyPacket(in_buffer_.data(), bytes_read, &packet_data, &packet_size);

    // Send acknowledgment back (this blocks)
    if (!SendAck(verified)) {
      LogWithErrno("Failed to send acknowledgment");
      QuitMessageLoop(false);
      return;
    }

    // Wait for the next command if we requested retransmission
    if (!verified) {
      PostReadTask();
      return;
    }

    // Route the packet data to the command handler.
    auto callback = [this](const uint8_t* rsp, size_t rsp_size) {
      // Send the response if there is one.
      PostWriteTask(rsp, rsp_size);

      // Wait for the next command.
      PostReadTask();
    };

    // If the command is handled, then |callback| will be called at some point,
    // so we're done.
    if (command_handler_.HandleCommand(packet_data, packet_size, callback))
      return;

    // If the command wasn't handled, that's because we do not support it, so we
    // respond with an empty response and continue.
    callback(nullptr, 0);
  });
}

void Server::PostWriteTask(const uint8_t* rsp, size_t rsp_bytes) {
  FTL_DCHECK(!rsp == !rsp_bytes);
  FTL_DCHECK(rsp_bytes + 4 < kMaxBufferSize);

  // Copy the data to capture it in the closure.
  std::vector<uint8_t> packet_data(rsp, rsp + rsp_bytes);
  message_loop_.task_runner()->PostTask([this, packet_data] {
    int index = 0;
    out_buffer_[index++] = '$';
    memcpy(out_buffer_.data() + index, packet_data.data(), packet_data.size());
    index += packet_data.size();
    out_buffer_[index++] = '#';

    uint8_t checksum = 0;
    for (uint8_t byte : packet_data)
      checksum += byte;

    util::EncodeByteString(checksum, out_buffer_.data() + index);
    index += 2;

    ssize_t bytes_written =
        write(client_sock_.get(), out_buffer_.data(), index);
    if (bytes_written != index) {
      LogWithErrno("Failed to send response");
      QuitMessageLoop(false);
    }
  });
}

void Server::QuitMessageLoop(bool status) {
  run_status_ = status;
  message_loop_.QuitNow();
}

}  // namespace debugserver
