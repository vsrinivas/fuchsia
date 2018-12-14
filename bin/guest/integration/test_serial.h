// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_GUEST_INTEGRATION_TEST_SERIAL_H_
#define GARNET_BIN_GUEST_INTEGRATION_TEST_SERIAL_H_

#include <string>

#include <lib/zx/socket.h>

class TestSerial {
 public:
  zx_status_t Start(zx::socket socket);

  // Executes a command and waits for a response. Uses a header and a footer to
  // ensure the command finished executing and to capture output. Blocks on the
  // serial socket being writable and readable at various points and on the
  // command completing.
  zx_status_t ExecuteBlocking(const std::string& command,
                              std::string* result = nullptr);

 private:
  zx_status_t SendBlocking(const std::string& message);

  zx_status_t WaitForMarker(const std::string& marker,
                            std::string* result = nullptr);

  zx_status_t WaitForAny();

  zx::socket socket_;
  std::string buffer_;
};

#endif  // GARNET_BIN_GUEST_INTEGRATION_TEST_SERIAL_H_
