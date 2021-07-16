// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_TESTS_SOCKET_LOGGER_H_
#define SRC_VIRTUALIZATION_TESTS_SOCKET_LOGGER_H_

#include <lib/async-loop/cpp/loop.h>
#include <lib/zx/socket.h>

#include <optional>

#include <src/lib/fsl/socket/socket_drainer.h>

#include "logger.h"

class LogClient;

// Read data from the given socket, marshalling it to the logger.
class SocketLogger {
 public:
  ~SocketLogger();

  // Log all data received on the given socket to the given logger.
  //
  // Caller maintains ownership of `dispatcher` and `logger`.
  SocketLogger(Logger* logger, zx::socket socket);

 private:
  std::unique_ptr<LogClient> client_;
  std::optional<fsl::SocketDrainer> drainer_;
  async::Loop loop_ = async::Loop(&kAsyncLoopConfigNeverAttachToThread);
};

#endif  // SRC_VIRTUALIZATION_TESTS_SOCKET_LOGGER_H_
