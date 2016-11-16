// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MOTERM_COMMAND_H_
#define APPS_MOTERM_COMMAND_H_

#include <launchpad/launchpad.h>
#include <mx/process.h>
#include <mx/socket.h>
#include <mxio/util.h>

#include <functional>

#include "apps/modular/services/application/application_launcher.fidl.h"
#include "lib/ftl/files/unique_fd.h"
#include "lib/ftl/functional/closure.h"
#include "lib/mtl/tasks/message_loop.h"
#include "lib/mtl/tasks/message_loop_handler.h"

namespace moterm {

class Command : mtl::MessageLoopHandler {
 public:
  using ReceiveCallback =
      std::function<void(const void* bytes, size_t num_bytes)>;

  Command();
  ~Command();

  bool Start(modular::ApplicationLauncher* launcher,
             std::vector<std::string> command,
             ReceiveCallback receive_callback,
             ftl::Closure termination_callback);

  void SendData(const void* bytes, size_t num_bytes);

 private:
  // |mtl::MessageLoopHandler|:
  void OnHandleReady(mx_handle_t handle, mx_signals_t pending);

  ReceiveCallback receive_callback_;
  mx::socket stdin_;
  mx::socket stdout_;
  mx::socket stderr_;

  mtl::MessageLoop::HandlerKey out_key_;
  mtl::MessageLoop::HandlerKey err_key_;
  modular::ApplicationControllerPtr application_controller_;
};

}  // namespace moterm

#endif  // APPS_MOTERM_COMMAND_H_
