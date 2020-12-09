// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/developer/debug/zxdb/debug_adapter/server.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

#include "src/developer/debug/shared/logging/logging.h"

namespace zxdb {

Err DebugAdapterServer::Init() {
  main_loop_ = debug_ipc::MessageLoop::Current();
  server_socket_.reset(socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP));
  if (!server_socket_.is_valid()) {
    return Err("Could not create socket.");
  }

  {
    // Set SO_REUSEPORT so that subsequent binds succeeds.
    int opt = 1;
    setsockopt(server_socket_.get(), SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
  }

  {
    // Bind to local address.
    struct sockaddr_in6 addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin6_family = AF_INET6;
    addr.sin6_addr = in6addr_any;
    addr.sin6_port = htons(port_);
    if (bind(server_socket_.get(), reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
      return Err("Could not bind socket to port: %d", port_);
    }
  }

  if (listen(server_socket_.get(), 1) < 0) {
    return Err("Failed to listen on server socket");
  }
  ListenConnection();
  return Err();
}

void DebugAdapterServer::ListenConnection() {
  FX_DCHECK(!background_thread_.get());  // Duplicate ListenConnection() call.

  // Create the background thread to listen to incoming connections.
  background_thread_ = std::make_unique<std::thread>([this]() { ListenBackgroundThread(); });
}

void DebugAdapterServer::ListenBackgroundThread() {
  // Wait for one connection.
  // TODO(puneetha) : Replace FX_LOGS with call to console output.
  FX_LOGS(INFO) << "Waiting on port " << port_ << " for debug adapter connection.\r\n";
  fbl::unique_fd client;
  while (!Accept(client)) {
    if (background_thread_exit_) {
      return;
    }
  }
  FX_LOGS(INFO) << "Debug Adapter connection established.\r\n";

  main_loop_->PostTask(FROM_HERE, [this, client = std::move(client)]() mutable {
    ConnectionResolvedMainThread(std::move(client));
  });
}

bool DebugAdapterServer::Accept(fbl::unique_fd& client) {
  sockaddr_in6 addr;
  memset(&addr, 0, sizeof(addr));

  socklen_t addrlen = sizeof(addr);
  client =
      fbl::unique_fd(accept(server_socket_.get(), reinterpret_cast<sockaddr*>(&addr), &addrlen));
  if (!client.is_valid()) {
    return false;
  }

  if (fcntl(client.get(), F_SETFL, O_NONBLOCK) < 0) {
    FX_LOGS(ERROR) << "Couldn't make port nonblocking.";
    return false;
  }
  return true;
}

void DebugAdapterServer::ConnectionResolvedMainThread(fbl::unique_fd client) {
  background_thread_->join();
  background_thread_.reset();

  for (auto& observer : observers_) {
    observer.ClientConnected();
  }

  buffer_ = std::make_unique<debug_ipc::BufferedFD>();
  if (!buffer_->Init(std::move(client))) {
    FX_LOGS(ERROR) << "Failed to initialize debug adapter buffer";
    return;
  }

  context_ = std::make_unique<DebugAdapterContext>(session_, &buffer_->stream());
  buffer_->set_data_available_callback(
      [context = context_.get()]() { context->OnStreamReadable(); });

  // Reset the client connection on error.
  buffer_->set_error_callback([this]() {
    FX_LOGS(INFO) << "Connection lost.";
    OnConnectionError();
  });
}

void DebugAdapterServer::OnConnectionError() {
  ResetClientConnection();
  for (auto& observer : observers_) {
    observer.ClientDisconnected();
  }
  ListenConnection();
}

void DebugAdapterServer::ResetClientConnection() {
  context_.reset();
  buffer_.reset();
}

DebugAdapterServer::~DebugAdapterServer() {
  ResetClientConnection();
  server_socket_.reset();
  if (background_thread_) {
    background_thread_exit_ = true;
    background_thread_->join();
    background_thread_.reset();
  }
}

}  // namespace zxdb
