// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_GUEST_CLI_SERIAL_H_
#define GARNET_BIN_GUEST_CLI_SERIAL_H_

#include <memory>

#include <lib/async-loop/cpp/loop.h>
#include <zx/socket.h>

class InputReader;
class OutputWriter;

class SerialConsole {
 public:
  SerialConsole(async::Loop* loop);
  SerialConsole(SerialConsole&& o);

  ~SerialConsole();

  void Start(zx::socket socket);

 private:
  async::Loop* loop_;
  std::unique_ptr<InputReader> input_reader_;
  std::unique_ptr<OutputWriter> output_writer_;
};

void handle_serial(uint32_t env_id, uint32_t cid);

#endif  // GARNET_BIN_GUEST_CLI_SERIAL_H_
