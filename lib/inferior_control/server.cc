// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "server.h"

#include <array>
#include <cstdlib>
#include <limits>
#include <string>
#include <vector>

#include "lib/fxl/logging.h"
#include "lib/fxl/strings/string_printf.h"

#include "garnet/lib/debugger_utils/util.h"

namespace debugserver {

Server::Server()
    : client_sock_(-1),
      run_status_(true) {}

Server::~Server() {
  // This will invoke the IOLoop destructor which will clean up and join the I/O
  // threads.
  io_loop_.reset();
}

void Server::SetCurrentThread(Thread* thread) {
  if (!thread)
    current_thread_.reset();
  else
    current_thread_ = thread->AsWeakPtr();
}

void Server::QuitMessageLoop(bool status) {
  run_status_ = status;
  message_loop_.QuitNow();
}

void Server::PostQuitMessageLoop(bool status) {
  run_status_ = status;
  message_loop_.PostQuitTask();
}

}  // namespace debugserver
