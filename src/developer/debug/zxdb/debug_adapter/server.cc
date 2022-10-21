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

DebugAdapterServer::DebugAdapterServer(Session* session, uint16_t port)
    : session_(session), port_(port) {
  int fd[2] = {};
  pipe(fd);
  exit_pipe_[0] = fbl::unique_fd(fd[0]);
  exit_pipe_[1] = fbl::unique_fd(fd[1]);
}

Err DebugAdapterServer::Init() {
  main_loop_ = debug::MessageLoop::Current();
  server_socket_.reset(socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP));
  if (!server_socket_.is_valid()) {
    return Err("Could not create socket.");
  }

  {
    // Bind to local address.
    struct sockaddr_in6 addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin6_family = AF_INET6;
    addr.sin6_addr = in6addr_any;
    addr.sin6_port = htons(port_);
    const int value = 1;
    if (setsockopt(server_socket_.get(), SOL_SOCKET, SO_REUSEADDR, &value, sizeof(value)) < 0) {
      return Err("Could not set SO_REUSEADDR");
    }
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
  LOGS(Info) << "Waiting on port " << port_ << " for debug adapter connection.";
  fbl::unique_fd client;
  while (!Accept(client)) {
    if (background_thread_exit_) {
      return;
    }
  }
  LOGS(Info) << "Debug Adapter connection established.";

  main_loop_->PostTask(FROM_HERE, [this, client = std::move(client)]() mutable {
    ConnectionResolvedMainThread(std::move(client));
  });
}

bool DebugAdapterServer::Accept(fbl::unique_fd& client) {
  // Wait on server_socket fd and exit_event fd until new connection is received or thread exit is
  // requested.
  fd_set read_set;
  FD_ZERO(&read_set);
  FD_SET(server_socket_.get(), &read_set);
  FD_SET(exit_pipe_[0].get(), &read_set);
  int nfds =
      server_socket_.get() > exit_pipe_[0].get() ? server_socket_.get() : exit_pipe_[0].get();
  auto status = select(nfds + 1, &read_set, NULL, NULL, NULL);
  if (status <= 0) {
    // An error or timeout occurred.
    return false;
  }

  if (FD_ISSET(exit_pipe_[0].get(), &read_set)) {
    // Thread exit requested.
    return false;
  }

  // Accept the new connection.
  sockaddr_in6 addr;
  memset(&addr, 0, sizeof(addr));

  socklen_t addrlen = sizeof(addr);
  client =
      fbl::unique_fd(accept(server_socket_.get(), reinterpret_cast<sockaddr*>(&addr), &addrlen));
  if (!client.is_valid()) {
    LOGS(Error) << "Accept failed.";
    return false;
  }

  if (fcntl(client.get(), F_SETFL, O_NONBLOCK) < 0) {
    LOGS(Error) << "Couldn't make port nonblocking.";
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

  buffer_ = std::make_unique<debug::BufferedFD>(std::move(client));
  if (!buffer_->Start()) {
    LOGS(Error) << "Failed to initialize debug adapter buffer";
    return;
  }

  context_ = std::make_unique<DebugAdapterContext>(session_, &buffer_->stream());
  buffer_->set_data_available_callback(
      [context = context_.get()]() { context->OnStreamReadable(); });
  context_->set_destroy_connection_callback([this]() { OnDisconnect(); });

  // Reset the client connection on error.
  buffer_->set_error_callback([this]() {
    LOGS(Info) << "Connection lost.";
    OnDisconnect();
  });
}

void DebugAdapterServer::OnDisconnect() {
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

  if (background_thread_) {
    background_thread_exit_ = true;
    // Write to exit_event to unblock select().
    int ret;
    do {
      int val = 1;
      ret = write(exit_pipe_[1].get(), &val, sizeof(val));
    } while (ret < 0 && errno == EINTR);
    // Wait for background thread to exit.
    background_thread_->join();
    background_thread_.reset();
  }
}

}  // namespace zxdb
