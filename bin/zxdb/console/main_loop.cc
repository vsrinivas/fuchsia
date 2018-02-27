// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/console/main_loop.h"

#include <string>
#include <errno.h>
#include <unistd.h>

#include "garnet/bin/zxdb/client/agent_connection.h"
#include "garnet/bin/zxdb/client/err.h"
#include "garnet/bin/zxdb/console/console.h"
#include "garnet/public/lib/fxl/logging.h"

namespace zxdb {

MainLoop::MainLoop() = default;
MainLoop::~MainLoop() = default;

void MainLoop::Run() {
  PlatformRun();
}

void MainLoop::StartWatchingConnection(AgentConnection* connection) {
  size_t this_id = next_connection_id_;
  next_connection_id_++;

  connections_[this_id] = connection;
  PlatformStartWatchingConnection(this_id, connection);

  // See comment in the header file for this function.
  connection->OnNativeHandleReadable();
}

void MainLoop::StopWatchingConnection(AgentConnection* connection) {
  for (auto it = connections_.begin(); it != connections_.end(); ++it) {
    if (it->second == connection) {
      connections_.erase(it);
      PlatformStopWatchingConnection(it->first, connection);
      return;
    }
  }
  FXL_NOTREACHED();
}

void MainLoop::OnStdinReadable() {
  constexpr size_t kBufSize = 64;  // Don't expect much data at once.
  char buf[kBufSize];
  ssize_t bytes_read = 0;
  while ((bytes_read = read(STDIN_FILENO, &buf, kBufSize)) > 0) {
    for (ssize_t i = 0; i < bytes_read; i++) {
      if (Console::get()->OnInput(buf[i]) == Console::Result::kQuit) {
        should_quit_ = true;
        return;
      }
    }
  }
}

AgentConnection* MainLoop::ConnectionFromID(size_t connection_id) {
  auto found = connections_.find(connection_id);
  if (found == connections_.end())
    return nullptr;
  return found->second;
}

}  // namespace zxdb
