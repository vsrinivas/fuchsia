// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_SHELL_CONSOLE_SCOPED_INTERPRETER_H_
#define SRC_DEVELOPER_SHELL_CONSOLE_SCOPED_INTERPRETER_H_

#include <unistd.h>
#include <zircon/status.h>

#include <condition_variable>
#include <mutex>
#include <thread>

#include "src/developer/shell/interpreter/src/server.h"

namespace shell::console {

// This class creates a separate thread running an interpreter with a managed lifetime.
// If a client wants to communicate with that interpreter, it should call |client()|.
class ScopedInterpreter {
 public:
  ScopedInterpreter() {
    zx_handle_t client_ch;
    zx_handle_t server_ch;
    zx_channel_create(0, &client_ch, &server_ch);
    server_loop_ = nullptr;

    // In the long run, we want to talk with a server component.  There is currently (components v1)
    // no way to make sure that the server component gets the correct permissions from this program.
    // So, we start a server in a separate thread, and talk with that.
    // std::thread t();
    server_thread_ = std::thread([this, server_ch]() {
      shell::interpreter::server::Server server;
      {
        std::unique_lock<std::mutex> lck(mutex_);
        server_loop_ = server.loop();
        condvar_.notify_all();
      }
      zx_status_t result = server.IncomingConnection(server_ch);
      if (result != ZX_OK) {
        fprintf(stderr, "Unable to start interpreter: %s\n", zx_status_get_string(result));
        exit(1);
      }
      server.Run();
    });

    zx::channel client_channel(client_ch);
    client_ = std::make_unique<llcpp::fuchsia::shell::Shell::SyncClient>(std::move(client_channel));
  }

  ~ScopedInterpreter() {
    async::Loop* sl;
    {
      // Wait for server thread startup to finish, if it hasn't.
      std::unique_lock<std::mutex> lck(mutex_);
      // Loop is for spurious wakeups.
      while ((sl = server_loop_) == nullptr) {
        condvar_.wait(lck);
      }
    }
    // Shutdown the loop and wait for the server thread to notice.
    if (sl != nullptr) {
      sl->Quit();
      server_thread_.join();
    }
  }

  // Returns a pointer to the SyncClient that is valid during the lifetime of this object.
  llcpp::fuchsia::shell::Shell::SyncClient* client() { return client_.get(); }

 private:
  async::Loop* server_loop_ = nullptr;
  std::thread server_thread_;
  std::unique_ptr<llcpp::fuchsia::shell::Shell::SyncClient> client_;
  std::mutex mutex_;
  std::condition_variable condvar_;
};

}  // namespace shell::console

#endif  // SRC_DEVELOPER_SHELL_CONSOLE_SCOPED_INTERPRETER_H_
