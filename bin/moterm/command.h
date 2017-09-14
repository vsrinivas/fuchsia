// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MOTERM_COMMAND_H_
#define APPS_MOTERM_COMMAND_H_

#include <launchpad/launchpad.h>
#include <zx/process.h>
#include <zx/socket.h>
#include <fdio/util.h>

#include <functional>
#include <vector>

#include "lib/fxl/files/unique_fd.h"
#include "lib/fxl/functional/closure.h"
#include "lib/fsl/io/redirection.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fsl/tasks/message_loop_handler.h"

namespace moterm {

class Command : fsl::MessageLoopHandler {
 public:
  using ReceiveCallback =
      std::function<void(const void* bytes, size_t num_bytes)>;

  Command();
  ~Command();

  bool Start(std::vector<std::string> command,
             std::vector<fsl::StartupHandle> startup_handles,
             ReceiveCallback receive_callback,
             fxl::Closure termination_callback);

  void SendData(const void* bytes, size_t num_bytes);

 private:
  // |fsl::MessageLoopHandler|:
  void OnHandleReady(zx_handle_t handle, zx_signals_t pending, uint64_t count);

  fxl::Closure termination_callback_;
  ReceiveCallback receive_callback_;
  zx::socket stdin_;
  zx::socket stdout_;
  zx::socket stderr_;

  fsl::MessageLoop::HandlerKey termination_key_ = 0;
  fsl::MessageLoop::HandlerKey out_key_ = 0;
  fsl::MessageLoop::HandlerKey err_key_ = 0;
  zx::process process_;
};

}  // namespace moterm

#endif  // APPS_MOTERM_COMMAND_H_
