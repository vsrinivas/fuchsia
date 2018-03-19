// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_SCENIC_COMMAND_DISPATCHER_H_
#define GARNET_LIB_UI_SCENIC_COMMAND_DISPATCHER_H_

#include "lib/fxl/macros.h"
#include "lib/fxl/memory/ref_counted.h"
#include "lib/ui/scenic/fidl/commands.fidl.h"
// TODO(MZ-469): Remove this once Scenic's session is factored away.
#include "lib/ui/scenic/fidl/session.fidl.h"

namespace scenic {

class Scenic;
class Session;

// Provides the capabilities that a CommandDispatcher needs to do its job,
// without directly exposing the Session.
class CommandDispatcherContext final {
 public:
  explicit CommandDispatcherContext(Scenic* scenic, Session* session);
  CommandDispatcherContext(CommandDispatcherContext&& context);

  Scenic* scenic() { return scenic_; }
  Session* session() { return session_; }

 private:
  Scenic* scenic_;
  Session* session_;
};

class CommandDispatcher {
 public:
  explicit CommandDispatcher(CommandDispatcherContext context);
  virtual ~CommandDispatcher();

  virtual bool ApplyCommand(const ui::CommandPtr& command) = 0;

  CommandDispatcherContext* context() { return &context_; }

 private:
  CommandDispatcherContext context_;
  FXL_DISALLOW_COPY_AND_ASSIGN(CommandDispatcher);
};

// TODO(MZ-421): Remove this once view manager is another Scenic system.
class TempSessionDelegate : public CommandDispatcher {
 public:
  explicit TempSessionDelegate(CommandDispatcherContext context);

  virtual void Enqueue(::f1dl::VectorPtr<ui::CommandPtr> ops) = 0;
  virtual void Present(uint64_t presentation_time,
                       ::f1dl::VectorPtr<zx::event> acquire_fences,
                       ::f1dl::VectorPtr<zx::event> release_fences,
                       const ui::Session::PresentCallback& callback) = 0;

  virtual void HitTest(uint32_t node_id,
                       ui::gfx::vec3Ptr ray_origin,
                       ui::gfx::vec3Ptr ray_direction,
                       const ui::Session::HitTestCallback& callback) = 0;

  virtual void HitTestDeviceRay(ui::gfx::vec3Ptr ray_origin,
                                ui::gfx::vec3Ptr ray_direction,
                                const ui::Session::HitTestCallback& clback) = 0;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(TempSessionDelegate);
};

}  // namespace scenic

#endif  // GARNET_LIB_UI_SCENIC_COMMAND_DISPATCHER_H_
