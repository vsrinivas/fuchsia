// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/netconnector/src/listener.h"

#include <arpa/inet.h>
#include <errno.h>
#include <sys/socket.h>

#include "apps/netconnector/src/ip_port.h"
#include "lib/ftl/files/unique_fd.h"
#include "lib/ftl/functional/make_copyable.h"
#include "lib/ftl/logging.h"
#include "lib/mtl/tasks/message_loop.h"

namespace netconnector {

Listener::Listener()
    : task_runner_(mtl::MessageLoop::GetCurrent()->task_runner()) {}

Listener::~Listener() {
  Stop();
}

void Listener::Start(
    IpPort port,
    std::function<void(ftl::UniqueFD)> new_connection_callback) {
  FTL_DCHECK(!socket_fd_.is_valid()) << "Started when already listening";

  socket_fd_ = ftl::UniqueFD(socket(AF_INET, SOCK_STREAM, 0));

  if (!socket_fd_.is_valid()) {
    FTL_LOG(ERROR) << "Failed to open socket for listening, errno " << errno;
    return;
  }

  struct sockaddr_in listener_address;
  listener_address.sin_family = AF_INET;
  listener_address.sin_addr.s_addr = INADDR_ANY;
  listener_address.sin_port = port.as_in_port_t();

  if (bind(socket_fd_.get(), (struct sockaddr*)&listener_address,
           sizeof(listener_address)) < 0) {
    FTL_LOG(ERROR) << "Failed to bind listening socket, errno " << errno;
    socket_fd_.reset();
    return;
  }

  if (listen(socket_fd_.get(), kListenerQueueDepth) < 0) {
    FTL_LOG(ERROR) << "Failed to listen on listening socket, errno " << errno;
    socket_fd_.reset();
    return;
  }

  new_connection_callback_ = new_connection_callback;

  worker_thread_ = std::thread([this]() { Worker(); });
}

void Listener::Stop() {
  if (!socket_fd_.is_valid()) {
    return;
  }

  socket_fd_.reset();

  if (worker_thread_.joinable()) {
    worker_thread_.join();
  }
}

void Listener::Worker() {
  while (socket_fd_.is_valid()) {
    struct sockaddr_in connection_address;
    socklen_t connection_address_size = sizeof(connection_address);

    ftl::UniqueFD connection_fd(accept(socket_fd_.get(),
                                       (struct sockaddr*)&connection_address,
                                       &connection_address_size));

    if (!socket_fd_.is_valid()) {
      // Stopping.
      break;
    }

    if (!connection_fd.is_valid()) {
      FTL_LOG(ERROR) << "Failed to accept on listening socket, errno " << errno;
      break;
    }

    task_runner_->PostTask(
        ftl::MakeCopyable([ this, fd = std::move(connection_fd) ]() mutable {
          new_connection_callback_(std::move(fd));
        }));
  }
}

}  // namespace netconnector
