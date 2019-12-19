// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/cmd/app.h"

#include <unistd.h>
#include <zircon/status.h>

#include <memory>
#include <utility>

namespace cmd {

App::App(async_dispatcher_t* dispatcher)
    : console_(this, dispatcher, STDIN_FILENO), executor_(dispatcher) {}

App::~App() = default;

void App::Init(QuitCallback quit_callback) {
  quit_callback_ = std::move(quit_callback);
  console_.Init("% ");
  console_.GetNextCommand();
}

zx_status_t App::OnConsoleCommand(Command command) {
  zx_status_t status =
      executor_.Execute(std::move(command), [this]() { console_.GetNextCommand(); });
  if (status == ZX_ERR_STOP) {
    auto quit_callback = std::move(quit_callback_);
    quit_callback();
    return status;
  }
  if (status != ZX_ERR_NEXT && status != ZX_ERR_ASYNC) {
    fprintf(stderr, "error: Failed to execute command: %d (%s)\n", status,
            zx_status_get_string(status));
    return ZX_ERR_NEXT;
  }
  return status;
}

void App::OnConsoleError(zx_status_t status) {
  fprintf(stderr, "error: Failed to read console: %d (%s)\n", status, zx_status_get_string(status));
  auto quit_callback = std::move(quit_callback_);
  quit_callback();
}

}  // namespace cmd
