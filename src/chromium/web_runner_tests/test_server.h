// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CHROMIUM_WEB_RUNNER_TESTS_TEST_SERVER_H_
#define SRC_CHROMIUM_WEB_RUNNER_TESTS_TEST_SERVER_H_

#include <lib/fit/defer.h>

#include <array>
#include <string>
#include <thread>
#include <vector>

#include <src/lib/files/unique_fd.h>

namespace web_runner_tests {

// This is a simple TCP server that binds to a random port on localhost and
// serves a single connection.
class TestServer {
 public:
  // This attempts to find an available port the server to.
  bool FindAndBindPort();

  // Closes the bound socket file descriptor, cancelling any pending |Accept|.
  void Close();

  // This accepts exactly one incoming connection.
  bool Accept();

  // This reads data from the currently open connection into the provided
  // |buf|. On success, this resizes |buf| to the number of bytes read.
  bool Read(std::string* buf);

  // Writes data from |buf| into the currently open connection.
  bool Write(const std::string& buf);

  // Writes message content into the currently open connection, preceeded by an
  // appropriate HTTP response header.
  bool WriteContent(const std::string& content);

  // Port number in use.
  int port() const { return port_; }

  // Runs a |serve| routine on its own thread, with proper cleanup to prevent
  // deadlock. |serve| must terminate after |Accept()| returns false.
  // The return value is a deferred action which, when destroyed, closes
  // the server (i.e. RAII semantics).
  template <typename T>
  auto ServeAsync(T serve) {
    auto server = std::thread(std::move(serve));
    // Close must be signaled before the thread goes out of scope so that any
    // blocking |Accept| calls terminate so that |serve| can terminate.
    return fit::defer([this, server = std::move(server)]() mutable {
      Close();
      server.join();
    });
  }

 private:
  fxl::UniqueFD conn_;
  fxl::UniqueFD socket_;
  std::array<fxl::UniqueFD, 2> close_;
  int port_ = -1;
};

}  // namespace web_runner_tests

#endif  // SRC_CHROMIUM_WEB_RUNNER_TESTS_TEST_SERVER_H_
