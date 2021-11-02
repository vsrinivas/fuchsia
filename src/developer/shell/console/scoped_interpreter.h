// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_SHELL_CONSOLE_SCOPED_INTERPRETER_H_
#define SRC_DEVELOPER_SHELL_CONSOLE_SCOPED_INTERPRETER_H_

#include <lib/async-loop/default.h>
#include <stdio.h>
#include <zircon/status.h>

#include "src/developer/shell/interpreter/src/server.h"

namespace shell::console {

// This class creates a separate thread running an interpreter with a managed lifetime.
// If a client wants to communicate with that interpreter, it should call |client()|.
class ScopedInterpreter {
 public:
  ScopedInterpreter() : loop_(&kAsyncLoopConfigNoAttachToCurrentThread), server_(&loop_) {
    auto endpoints = fidl::CreateEndpoints<fuchsia_shell::Shell>();
    if (!endpoints.is_ok()) {
      fprintf(stderr, "Unable to create channels: %s\n", endpoints.status_string());
      exit(1);
    }

    zx_status_t result = server_.IncomingConnection(std::move(endpoints->server));
    if (result != ZX_OK) {
      fprintf(stderr, "Unable to start interpreter: %s\n", zx_status_get_string(result));
      exit(1);
    }
    loop_.StartThread("shell worker");

    client_ = fidl::BindSyncClient(std::move(endpoints->client));
  }

  ~ScopedInterpreter() {
    client_->Shutdown();
    loop_.Shutdown();
    loop_.JoinThreads();
  }

  // Returns a pointer to the SyncClient that is valid during the lifetime of this object.
  fidl::WireSyncClient<fuchsia_shell::Shell>* client() { return &client_; }

 private:
  async::Loop loop_;
  shell::interpreter::server::Server server_;
  fidl::WireSyncClient<fuchsia_shell::Shell> client_;
};

}  // namespace shell::console

#endif  // SRC_DEVELOPER_SHELL_CONSOLE_SCOPED_INTERPRETER_H_
