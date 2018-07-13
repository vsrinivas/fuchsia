// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "server.h"

#include <array>
#include <cstdlib>
#include <limits>
#include <string>
#include <vector>

#include <lib/async/cpp/task.h>

#include "lib/fxl/logging.h"
#include "lib/fxl/strings/string_printf.h"

#include "garnet/lib/debugger_utils/util.h"

namespace inferior_control {

Server::Server(zx::job job_for_search, zx::job job_for_launch)
    : job_for_search_(std::move(job_for_search)),
      job_for_launch_(std::move(job_for_launch)),
      exception_port_(message_loop_.dispatcher()),
      run_status_(true) {}

Server::~Server() {}

void Server::SetCurrentThread(Thread* thread) {
  if (!thread)
    current_thread_.reset();
  else
    current_thread_ = thread->AsWeakPtr();
}

void Server::QuitMessageLoop(bool status) {
  run_status_ = status;
  message_loop_.Quit();
}

void Server::PostQuitMessageLoop(bool status) {
  run_status_ = status;
  async::PostTask(message_loop_.dispatcher(), [this] { message_loop_.Quit(); });
}

ServerWithIO::ServerWithIO(zx::job job_for_search, zx::job job_for_launch)
    : Server(std::move(job_for_search), std::move(job_for_launch)),
      client_sock_(-1) {}

ServerWithIO::~ServerWithIO() {
  // This will invoke the IOLoop destructor which will clean up and join the
  // I/O threads. This is done now because |message_loop_| and |client_sock_|
  // must outlive |io_loop_|. The former is handled by virtue of being in the
  // baseclass. The latter is handled here.
  io_loop_.reset();
}

}  // namespace inferior_control
