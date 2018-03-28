// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_SCENIC_COMMAND_DISPATCHER_H_
#define GARNET_LIB_UI_SCENIC_COMMAND_DISPATCHER_H_

#include <fuchsia/cpp/ui.h>
#include "garnet/lib/ui/scenic/forward_declarations.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/memory/ref_counted.h"

namespace scenic {

// Provides the capabilities that a CommandDispatcher needs to do its job,
// without directly exposing the Session.
class CommandDispatcherContext final {
 public:
  explicit CommandDispatcherContext(Scenic* scenic, Session* session);
  CommandDispatcherContext(CommandDispatcherContext&& context);

  Scenic* scenic() { return scenic_; }
  Session* session() { return session_; }
  SessionId session_id() { return session_id_; }

 private:
  Scenic* const scenic_;
  Session* const session_;
  const SessionId session_id_;
};

class CommandDispatcher {
 public:
  explicit CommandDispatcher(CommandDispatcherContext context);
  virtual ~CommandDispatcher();

  virtual bool ApplyCommand(const ui::Command& command) = 0;

  CommandDispatcherContext* context() { return &context_; }

 private:
  CommandDispatcherContext context_;
  FXL_DISALLOW_COPY_AND_ASSIGN(CommandDispatcher);
};

// TODO(MZ-421): Remove this once view manager is another Scenic system.
class TempSessionDelegate : public CommandDispatcher {
 public:
  explicit TempSessionDelegate(CommandDispatcherContext context);

  virtual void Enqueue(::fidl::VectorPtr<ui::Command> ops) = 0;
  virtual void Present(uint64_t presentation_time,
                       ::fidl::VectorPtr<zx::event> acquire_fences,
                       ::fidl::VectorPtr<zx::event> release_fences,
                       ui::Session::PresentCallback callback) = 0;

  virtual void HitTest(uint32_t node_id,
                       ::gfx::vec3 ray_origin,
                       ::gfx::vec3 ray_direction,
                       ui::Session::HitTestCallback callback) = 0;

  virtual void HitTestDeviceRay(::gfx::vec3 ray_origin,
                                ::gfx::vec3 ray_direction,
                                ui::Session::HitTestCallback callback) = 0;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(TempSessionDelegate);
};

}  // namespace scenic

#endif  // GARNET_LIB_UI_SCENIC_COMMAND_DISPATCHER_H_
