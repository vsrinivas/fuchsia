// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>
#include <thread>

#include "lib/ftl/files/unique_fd.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/tasks/task_runner.h"

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
  void Start(uint32_t port,
             std::function<void(ftl::UniqueFD)> new_connection_callback);

  // Stops the listener.
  void Stop();

 private:
  static constexpr int kListenerQueueDepth = 3;

  void Worker();

  ftl::RefPtr<ftl::TaskRunner> task_runner_;
  std::function<void(ftl::UniqueFD)> new_connection_callback_;
  ftl::UniqueFD socket_fd_;
  std::thread worker_thread_;

  FTL_DISALLOW_COPY_AND_ASSIGN(Listener);
};

}  // namespace netconnector
