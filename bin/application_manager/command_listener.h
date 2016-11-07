// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MODULAR_SRC_APPLICATION_MANAGER_COMMAND_LISTENER_H_
#define APPS_MODULAR_SRC_APPLICATION_MANAGER_COMMAND_LISTENER_H_

#include <mx/channel.h>

#include "apps/modular/services/application/application_launcher.fidl.h"
#include "lib/ftl/macros.h"
#include "lib/mtl/tasks/message_loop.h"
#include "lib/mtl/tasks/message_loop_handler.h"

namespace modular {

// This class listens on the given handle for commands to drive the application
// manager. For example, mxsh sends commands the user types that begin with
// "file:" to this class to run the corresponding applications.
class CommandListener : mtl::MessageLoopHandler {
 public:
  explicit CommandListener(ApplicationLauncher* launcher,
                           mx::channel command_channel);
  ~CommandListener();

 private:
  // |mtl::MessageLoopHandler|:
  void OnHandleReady(mx_handle_t handle, mx_signals_t pending) override;

  void ExecuteCommand(std::string command);
  void Close();

  ApplicationLauncher* const launcher_;
  mtl::MessageLoop* const message_loop_;

  mx::channel command_channel_;
  mtl::MessageLoop::HandlerKey handler_key_{};

  FTL_DISALLOW_COPY_AND_ASSIGN(CommandListener);
};

}  // namespace mojo

#endif  // APPS_MODULAR_SRC_APPLICATION_MANAGER_COMMAND_LISTENER_H_
