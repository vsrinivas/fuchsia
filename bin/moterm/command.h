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

#include "apps/modular/services/application/application_environment.fidl.h"
#include "lib/ftl/files/unique_fd.h"
#include "lib/mtl/tasks/message_loop.h"
#include "lib/mtl/tasks/message_loop_handler.h"

namespace moterm {

using CommandCallback =
    std::function<void(const void* bytes, size_t num_bytes)>;

class Command : mtl::MessageLoopHandler {
 public:
  explicit Command(CommandCallback callback);
  ~Command();
  bool Start(const char* name,
             int argc,
             const char* const* argv,
             fidl::InterfaceHandle<modular::ApplicationEnvironment> environment,
             fidl::InterfaceRequest<modular::ServiceProvider> services);

  void SendData(const void* bytes, size_t num_bytes);

 private:
  // |mtl::MessageLoopHandler|:
  void OnHandleReady(mx_handle_t handle, mx_signals_t pending);

  CommandCallback callback_;
  mx::socket stdin_;
  mx::socket stdout_;
  mx::socket stderr_;

  mtl::MessageLoop::HandlerKey out_key_;
  mtl::MessageLoop::HandlerKey err_key_;
  mx::process process_;
};

}  // namespace moterm

#endif  // APPS_MOTERM_COMMAND_H_
