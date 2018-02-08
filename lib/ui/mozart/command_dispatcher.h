// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_MOZART_COMMAND_DISPATCHER_H_
#define GARNET_LIB_UI_MOZART_COMMAND_DISPATCHER_H_

#include "lib/fxl/macros.h"
#include "lib/fxl/memory/ref_counted.h"
#include "lib/ui/mozart/fidl/commands.fidl.h"

namespace mz {

class Mozart;
class Session;

// Provides the capabilities that a CommandDispatcher needs to do its job,
// without directly exposing the Session.
class CommandDispatcherContext final {
 public:
  explicit CommandDispatcherContext(Mozart* mozart, Session* session);
  CommandDispatcherContext(CommandDispatcherContext&& context);

 private:
  Mozart* mozart_;
  Session* session_;
};

class CommandDispatcher {
 public:
  explicit CommandDispatcher(CommandDispatcherContext context);
  virtual ~CommandDispatcher();

  virtual bool ApplyCommand(const ui_mozart::CommandPtr& command) = 0;

  CommandDispatcherContext* context() { return &context_; }

 private:
  CommandDispatcherContext context_;
  FXL_DISALLOW_COPY_AND_ASSIGN(CommandDispatcher);
};

}  // namespace mz

#endif  // GARNET_LIB_UI_MOZART_COMMAND_DISPATCHER_H_
