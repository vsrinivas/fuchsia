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
    zx_handle_t client_ch;
    zx_handle_t server_ch;
    zx_channel_create(0, &client_ch, &server_ch);
    zx_status_t result = server_.IncomingConnection(server_ch);
    if (result != ZX_OK) {
      fprintf(stderr, "Unable to start interpreter: %s\n", zx_status_get_string(result));
      exit(1);
    }
    loop_.StartThread("shell worker");

    zx::channel client_channel(client_ch);
    client_ = std::make_unique<llcpp::fuchsia::shell::Shell::SyncClient>(std::move(client_channel));
  }

  ~ScopedInterpreter() {
    client_->Shutdown();
    loop_.Shutdown();
    loop_.JoinThreads();
  }

  // Returns a pointer to the SyncClient that is valid during the lifetime of this object.
  llcpp::fuchsia::shell::Shell::SyncClient* client() { return client_.get(); }

 private:
  async::Loop loop_;
  shell::interpreter::server::Server server_;
  std::unique_ptr<llcpp::fuchsia::shell::Shell::SyncClient> client_;
};

}  // namespace shell::console

#endif  // SRC_DEVELOPER_SHELL_CONSOLE_SCOPED_INTERPRETER_H_
