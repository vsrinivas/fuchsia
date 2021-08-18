// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_RUN_TEST_COMPONENT_OUTPUT_COLLECTOR_H_
#define GARNET_BIN_RUN_TEST_COMPONENT_OUTPUT_COLLECTOR_H_

#include <lib/async/cpp/wait.h>
#include <lib/async/dispatcher.h>
#include <lib/fpromise/bridge.h>
#include <lib/fpromise/promise.h>
#include <lib/zx/socket.h>
#include <zircon/assert.h>

#include <cstdio>
#include <memory>
#include <vector>

#include <fbl/unique_fd.h>

namespace run {

// This class captures the output from the socket and then buffers that output by newline.
// This can be used to pass file decriptors to launched components and capture their standard
// output.
class OutputCollector {
 public:
  static std::unique_ptr<OutputCollector> Create();

  using OutputCallBack = fit::function<void(std::string s)>;

  zx::socket TakeServer() {
    ZX_ASSERT_MSG(server_socket_.is_valid(), "This function should be called only once");
    return std::move(server_socket_);
  }
  explicit OutputCollector(zx::socket log_socket, zx::socket server_socket);
  ~OutputCollector();

  void CollectOutput(OutputCallBack callback, async_dispatcher_t *dispatcher);

  /// Signals when this class is done processing output socket.
  fpromise::promise<> SignalWhenDone();

 private:
  void Close();
  void Handler(async_dispatcher_t *, async::WaitBase *, zx_status_t, const zx_packet_signal_t *);

  std::vector<uint8_t> buf_;
  OutputCallBack callback_;
  zx::socket server_socket_;
  zx::socket log_socket_;
  async::WaitMethod<OutputCollector, &OutputCollector::Handler> wait_;
  std::unique_ptr<OutputCollector> self_;
  std::vector<fpromise::bridge<>> done_signals_;
};

}  // namespace run

#endif  // GARNET_BIN_RUN_TEST_COMPONENT_OUTPUT_COLLECTOR_H_
