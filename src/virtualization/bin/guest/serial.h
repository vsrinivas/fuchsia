// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_BIN_GUEST_SERIAL_H_
#define SRC_VIRTUALIZATION_BIN_GUEST_SERIAL_H_

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/zx/socket.h>

#include <memory>

#include "src/lib/fsl/socket/socket_drainer.h"

class InputReader;
class OutputWriter;

// Reads output from the given socket and writes it to stdout.
class OutputWriter : public fsl::SocketDrainer::Client {
 public:
  explicit OutputWriter(async::Loop* loop);

  // Start marshalling data between the socket and stdout.
  void Start(zx::socket socket);

 private:
  // |fsl::SocketDrainer::Client|
  void OnDataAvailable(const void* data, size_t num_bytes) override;
  void OnDataComplete() override;

  async::Loop* loop_;
  fsl::SocketDrainer socket_drainer_{this};
};

// Reads/writes from the terminal to/from the given socket.
class GuestConsole {
 public:
  explicit GuestConsole(async::Loop* loop);
  GuestConsole(GuestConsole&& o) noexcept;

  ~GuestConsole();

  void Start(zx::socket socket);

 private:
  async::Loop* loop_;
  std::unique_ptr<InputReader> input_reader_;
  std::unique_ptr<OutputWriter> output_writer_;
};

zx_status_t handle_serial(uint32_t env_id, uint32_t cid, async::Loop* loop,
                          sys::ComponentContext* context);

#endif  // SRC_VIRTUALIZATION_BIN_GUEST_SERIAL_H_
