// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_NETCONNECTOR_LISTENER_H_
#define GARNET_BIN_NETCONNECTOR_LISTENER_H_

#include <memory>
#include <thread>

#include <lib/async/dispatcher.h>
#include <lib/fit/function.h>

#include "garnet/bin/netconnector/ip_port.h"
#include "lib/fxl/files/unique_fd.h"
#include "lib/fxl/macros.h"

namespace netconnector {

// Listens on the specified port for incoming connections.
//
// Listener is not thread-safe. All methods calls must be serialized. Callbacks
// will be called on the same thread on which the listener was constructed.
class Listener {
 public:
  Listener();

  ~Listener();

  // Starts listening on |port|. |new_connection_callback| is called when a new
  // connection is requested.
  void Start(IpPort port,
             fit::function<void(fxl::UniqueFD)> new_connection_callback);

  // Stops the listener.
  void Stop();

 private:
  static constexpr int kListenerQueueDepth = 3;

  void Worker();

  async_dispatcher_t* dispatcher_;
  fit::function<void(fxl::UniqueFD)> new_connection_callback_;
  fxl::UniqueFD socket_fd_;
  std::thread worker_thread_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Listener);
};

}  // namespace netconnector

#endif  // GARNET_BIN_NETCONNECTOR_LISTENER_H_
