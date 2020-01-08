// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/cmd/console.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <zircon/assert.h>

#include <utility>

namespace cmd {

Console::Client::~Client() = default;

Console::Console(Client* client, async_dispatcher_t* dispatcher, int input_fd)
    : client_(client),
      input_fd_(input_fd),
      input_waiter_(dispatcher),
      interrupt_waiter_(dispatcher) {}

Console::~Console() {
  if (tty_) {
    fdio_unsafe_release(tty_);
    tty_ = nullptr;
  }
}

void Console::Init(std::string prompt) {
  fcntl(input_fd_, F_SETFL, fcntl(input_fd_, F_GETFL, 0) | O_NONBLOCK);

  if (isatty(input_fd_)) {
    tty_ = fdio_unsafe_fd_to_io(input_fd_);
    WaitForInterruptAsynchronously();
  }

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
  input_waiter_.Wait(
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

void Console::WaitForInterruptAsynchronously() {
  interrupt_waiter_.Wait(
      [this](zx_status_t status, uint32_t observed) {
        if (status != ZX_OK) {
          return;
        }
        uint32_t events = 0;
        zx_status_t pty_status = ZX_OK;
        status = fuchsia_hardware_pty_DeviceReadEvents(fdio_unsafe_borrow_channel(tty_),
                                                       &pty_status, &events);
        WaitForInterruptAsynchronously();
        if (events & fuchsia_hardware_pty_EVENT_INTERRUPT) {
          if (should_read_) {
            line_input_.OnInput(line_input::SpecialCharacters::kKeyControlC);
          } else {
            client_->OnConsoleInterrupt();
          }
        }
      },
      input_fd_, POLLPRI);
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
