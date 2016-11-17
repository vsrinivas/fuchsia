// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MODULAR_SRC_APPLICATION_MANAGER_COMMAND_LISTENER_H_
#define APPS_MODULAR_SRC_APPLICATION_MANAGER_COMMAND_LISTENER_H_

#include <mx/channel.h>

#include "apps/modular/src/application_manager/application_environment_impl.h"
#include "lib/ftl/macros.h"
#include "lib/mtl/tasks/message_loop.h"
#include "lib/mtl/tasks/message_loop_handler.h"

namespace modular {

// This class listens on the given handle for commands to drive the application
// manager. For example, mxsh sends commands the user types that begin with
// "@" to this class to run the corresponding applications.
//
// Currently supported commands are:
//   @<scope> <uri> <args> : run application with specified uri in scope.
//   @<scope>? : display information about the specified scope
//
// Scopes are names for environments.
class CommandListener : mtl::MessageLoopHandler {
 public:
  explicit CommandListener(ApplicationEnvironmentImpl* root_environment,
                           mx::channel command_channel);
  ~CommandListener();

 private:
  // |mtl::MessageLoopHandler|:
  void OnHandleReady(mx_handle_t handle, mx_signals_t pending) override;

  void ExecuteCommand(std::string command);
  ApplicationEnvironmentImpl* FindEnvironment(ftl::StringView scope);
  void Usage();
  void Close();

  ApplicationEnvironmentImpl* const root_environment_;
  mtl::MessageLoop* const message_loop_;

  mx::channel command_channel_;
  mtl::MessageLoop::HandlerKey handler_key_{};

  FTL_DISALLOW_COPY_AND_ASSIGN(CommandListener);
};

}  // namespace modular

#endif  // APPS_MODULAR_SRC_APPLICATION_MANAGER_COMMAND_LISTENER_H_
