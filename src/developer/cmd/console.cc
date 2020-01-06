// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/cmd/console.h"

#include <fcntl.h>
#include <lib/fit/bridge.h>
#include <poll.h>
#include <unistd.h>
#include <zircon/assert.h>

#include <utility>

namespace cmd {

Console::Client::~Client() = default;

Console::Console(Client* client, async_dispatcher_t* dispatcher, int input_fd)
    : client_(client), waiter_(dispatcher), input_fd_(input_fd) {}

Console::~Console() = default;

void Console::Init(std::string prompt) {
  fcntl(input_fd_, F_SETFL, fcntl(input_fd_, F_GETFL, 0) | O_NONBLOCK);
  line_input_.Init([this](const std::string& line) { OnAccept(line); }, std::move(prompt));
  line_input_.SetEofCallback([this] { OnError(ZX_ERR_PEER_CLOSED); });
  line_input_.SetAutocompleteCallback([this](const std::string& line) {
    Autocomplete autocomplete(line);
    client_->OnConsoleAutocomplete(&autocomplete);
    return autocomplete.TakeCompletions();
  });
}

void Console::GetNextCommand() {
  ZX_DEBUG_ASSERT(!should_read_);
  line_input_.Show();
  should_read_ = true;
  WaitForInputAsynchronously();
}

void Console::WaitForInputAsynchronously() {
  waiter_.Wait(
      [this](zx_status_t status, uint32_t observed) {
        ZX_DEBUG_ASSERT(should_read_);
        if (status != ZX_OK) {
          OnError(status);
          return;
        }
        for (;;) {
          char ch = 0;
          ssize_t rv = read(input_fd_, &ch, 1);
          if (rv < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
              WaitForInputAsynchronously();
              return;
            }
            OnError(ZX_ERR_IO);
            return;
          }
          if (rv == 0) {
            OnError(ZX_ERR_PEER_CLOSED);
            return;
          }
          line_input_.OnInput(ch);
          if (!should_read_) {
            return;
          }
        }
      },
      input_fd_, POLLIN);
}

void Console::OnAccept(const std::string& line) {
  ZX_DEBUG_ASSERT(should_read_);
  line_input_.AddToHistory(line);
  Command command;
  command.Parse(line);

  zx_status_t status = client_->OnConsoleCommand(std::move(command));
  ZX_DEBUG_ASSERT(status == ZX_ERR_STOP || status == ZX_ERR_NEXT || status == ZX_ERR_ASYNC);
  if (status != ZX_ERR_NEXT) {
    line_input_.Hide();
    should_read_ = false;
  }
}

void Console::OnError(zx_status_t status) {
  ZX_DEBUG_ASSERT(should_read_);
  line_input_.Hide();
  should_read_ = false;
  client_->OnConsoleError(status);
}

}  // namespace cmd
