// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/netconnector/listener.h"

#include <arpa/inet.h>
#include <errno.h>
#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <sys/socket.h>

#include "garnet/bin/netconnector/ip_port.h"
#include "lib/fxl/files/unique_fd.h"
#include "lib/fxl/functional/make_copyable.h"
#include "lib/fxl/logging.h"

namespace netconnector {

Listener::Listener() : dispatcher_(async_get_default_dispatcher()) {}

Listener::~Listener() { Stop(); }

void Listener::Start(
    IpPort port, fit::function<void(fxl::UniqueFD)> new_connection_callback) {
  FXL_DCHECK(!socket_fd_.is_valid()) << "Started when already listening";

  socket_fd_ = fxl::UniqueFD(socket(AF_INET, SOCK_STREAM, 0));

  if (!socket_fd_.is_valid()) {
    FXL_LOG(ERROR) << "Failed to open socket for listening, errno " << errno;
    return;
  }

  struct sockaddr_in listener_address;
  listener_address.sin_family = AF_INET;
  listener_address.sin_addr.s_addr = INADDR_ANY;
  listener_address.sin_port = port.as_in_port_t();

  if (bind(socket_fd_.get(), (struct sockaddr*)&listener_address,
           sizeof(listener_address)) < 0) {
    FXL_LOG(ERROR) << "Failed to bind listening socket, errno " << errno;
    socket_fd_.reset();
    return;
  }

  if (listen(socket_fd_.get(), kListenerQueueDepth) < 0) {
    FXL_LOG(ERROR) << "Failed to listen on listening socket, errno " << errno;
    socket_fd_.reset();
    return;
  }

  new_connection_callback_ = std::move(new_connection_callback);

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

    fxl::UniqueFD connection_fd(accept(socket_fd_.get(),
                                       (struct sockaddr*)&connection_address,
                                       &connection_address_size));

    if (!socket_fd_.is_valid()) {
      // Stopping.
      break;
    }

    if (!connection_fd.is_valid()) {
      FXL_LOG(ERROR) << "Failed to accept on listening socket, errno " << errno;
      break;
    }

    async::PostTask(
        dispatcher_,
        fxl::MakeCopyable([this, fd = std::move(connection_fd)]() mutable {
          new_connection_callback_(std::move(fd));
        }));
  }
}

}  // namespace netconnector
