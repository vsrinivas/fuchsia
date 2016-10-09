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

Server::Server(uint16_t port)
    : port_(port),
      client_sock_(-1),
      server_sock_(-1),
      command_handler_(this),
      run_status_(true) {}

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

void Server::SetCurrentThread(Thread* thread) {
  if (!thread)
    current_thread_.reset();
  else
    current_thread_ = thread->AsWeakPtr();
}

bool Server::Listen() {
  FTL_DCHECK(!server_sock_.is_valid());
  FTL_DCHECK(!client_sock_.is_valid());

  ftl::UniqueFD server_sock(socket(AF_INET, SOCK_STREAM, 0));
  if (!server_sock.is_valid()) {
    util::LogErrorWithErrno("Failed to open socket");
    return false;
  }

  // Bind to a local address for listening.
  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(port_);

  if (bind(server_sock.get(), (struct sockaddr*)&addr, sizeof(addr)) < 0) {
    util::LogErrorWithErrno("Failed to bind socket");
    return false;
  }

  if (listen(server_sock.get(), 1) < 0) {
    util::LogErrorWithErrno("Listen failed");
    return false;
  }

  FTL_LOG(INFO) << "Waiting for a connection on port " << port_ << "...";

  // Reuse |addr| here for the destination address.
  socklen_t addrlen = sizeof(addr);
  ftl::UniqueFD client_sock(
      accept(server_sock.get(), (struct sockaddr*)&addr, &addrlen));
  if (!client_sock.is_valid()) {
    util::LogErrorWithErrno("Accept failed");
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
      util::LogErrorWithErrno("Error occured while waiting for command");
      QuitMessageLoop(false);
      return;
    }

    const uint8_t* packet_data = nullptr;
    size_t packet_size = 0;
    bool verified = util::VerifyPacket(in_buffer_.data(), bytes_read,
                                       &packet_data, &packet_size);

    // Send acknowledgment back (this blocks)
    if (!SendAck(verified)) {
      util::LogErrorWithErrno("Failed to send acknowledgment");
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
    FTL_LOG(ERROR) << "Command not supported: "
                   << std::string((const char*)packet_data, packet_size);
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
      util::LogErrorWithErrno("Failed to send response");
      QuitMessageLoop(false);
    }
  });
}

void Server::QuitMessageLoop(bool status) {
  run_status_ = status;
  message_loop_.QuitNow();
}

}  // namespace debugserver
