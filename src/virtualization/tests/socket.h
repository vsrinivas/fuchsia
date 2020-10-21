// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_TESTS_SOCKET_H_
#define SRC_VIRTUALIZATION_TESTS_SOCKET_H_

#include <lib/zx/socket.h>

#include <string>

class SocketInterface {
 public:
  SocketInterface() = default;
  virtual ~SocketInterface() = default;

  // Sends a message to the socket
  //
  // Blocks until the entire message is written to the socket, or the given
  // timeout has occurred. A non-blocking send can be performed by setting
  // deadline to ZX_TIME_INFINITE_PAST.
  //
  // If an error or timeout occurs, it is unspecified how much of "message"
  // has been tranmitted to the socket.
  virtual zx_status_t Send(zx::time deadline, const std::string& message) = 0;

  // Read one or more bytes from the socket, putting the result in
  // |result|.
  //
  // A non-blocking receive can be performed by setting deadline to
  // ZX_TIME_INFINITE_PAST. |result| must always be non-null.
  //
  // Returns when one or more bytes has been received, or the given timeout has
  // passed.
  virtual zx_status_t Receive(zx::time deadline, std::string* result) = 0;

  // Waits for the socket interface to be closed, or a |deadline| is reached.
  virtual zx_status_t WaitForClosed(zx::time deadline) = 0;
};

class ZxSocket : public SocketInterface {
 public:
  explicit ZxSocket(zx::socket socket);

  // |SocketInterface::Send|
  zx_status_t Send(zx::time deadline, const std::string& message) override;

  // |SocketInterface::Receive|
  zx_status_t Receive(zx::time deadline, std::string* result) override;

  // |SocketInterface::WaitForClosed|
  zx_status_t WaitForClosed(zx::time deadline) override;

 private:
  zx::socket socket_;
};

// Receive all data currently waiting on the socket.
//
// This call is non-blocking: it will only receive data already waiting.
//
// If |result| is non-null, the data read from the socket is stored.
// Otherwise, it is discarded.
//
// Returns ZX_OK if at least one byte was received, otherwise the error
// returned from the socket.
zx_status_t DrainSocket(SocketInterface* socket, std::string* result);

#endif  // SRC_VIRTUALIZATION_TESTS_SOCKET_H_
