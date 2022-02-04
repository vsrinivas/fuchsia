// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_TESTS_GUEST_CONSOLE_H_
#define SRC_VIRTUALIZATION_TESTS_GUEST_CONSOLE_H_

#include <lib/zx/socket.h>

#include <memory>
#include <string>

#include "src/virtualization/tests/socket.h"

class GuestConsole {
 public:
  explicit GuestConsole(std::unique_ptr<SocketInterface> socket);

  // Initialize the socket, attempting to reach a state where the socket appears
  // 'stable' with respect to output.
  //
  // This skips over initial startup noise (such as boot logs, etc) that may be
  // present on the socket interface. However, no guarantee can be made on the
  // state of the guest and whether it is actually finished starting up services
  // or even started listening for input on the serial line.
  //
  // Aborts with the error ZX_ERR_TIMEOUT if `deadline` is reached
  // prior to the system reaching its shell.
  zx_status_t Start(zx::time deadline);

  // Executes a command and waits for a response. Uses a header and a footer to
  // ensure the command finished executing and to capture output. Blocks on the
  // serial socket being writable and readable at various points and on the
  // command completing.
  zx_status_t ExecuteBlocking(const std::string& command, const std::string& prompt,
                              zx::time deadline, std::string* result = nullptr);

  // Sends a message to the guest's serial. Blocks until the entire message is
  // written to the socket but doesn't wait for a response.
  zx_status_t SendBlocking(const std::string& message, zx::time deadline);

  // Waits for a marker string to be read from the guest's serial, or until an
  // internal timeout passes. Optionally fills |result| with everything read up
  // to (but excluding) |marker|.
  //
  // Returns ZX_OK if the string was read before the timeout expired, and
  // an error status in other cases.
  //
  // The class keeps an internal buffer of unread serial data. This function
  // will consume all of the buffer up to the first occurance of |marker|.
  // For example, if the underlying socket has the following data on it:
  //
  //   "xxxmarkeryyy"
  //
  // then a call `WaitForMarker("marker", &result)` will return `xxx` in
  // |result|, and consume the buffer so that only `yyy` remains.
  zx_status_t WaitForMarker(const std::string& marker, zx::time deadline,
                            std::string* result = nullptr);

  // Waits for the socket interface to be closed, or a deadline is reached.
  zx_status_t WaitForSocketClosed(zx::time deadline);

  // Repeatedly execute a command until the desired output is seen. This
  // essentially retries the command using ExecuteBlocking at the specified rate
  // until it either succeeds and the output is matched, or the overall deadline
  // expires and this returns with an error.
  //
  // This is intended to be used when a guest is starting up and it is ambiguous
  // whether it is even listening for input yet.
  zx_status_t RepeatCommandTillSuccess(const std::string& command, const std::string& prompt,
                                       const std::string& success, zx::time deadline,
                                       zx::duration repeat_rate);

 private:
  // Waits for something to be written to the socket and drains it.
  zx_status_t WaitForAny(zx::time deadline);

  // Read all pending data from the socket. Non-blocking.
  zx_status_t Drain();

  std::unique_ptr<SocketInterface> socket_;
  std::string buffer_;
};

#endif  // SRC_VIRTUALIZATION_TESTS_GUEST_CONSOLE_H_
