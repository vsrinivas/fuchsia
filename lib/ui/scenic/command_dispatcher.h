// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_SCENIC_COMMAND_DISPATCHER_H_
#define GARNET_LIB_UI_SCENIC_COMMAND_DISPATCHER_H_

#include <fuchsia/ui/scenic/cpp/fidl.h>

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

  // TODO(SCN-808): can/should we avoid exposing any/all of these?
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

  virtual void DispatchCommand(fuchsia::ui::scenic::Command command) = 0;

  CommandDispatcherContext* context() { return &context_; }

 private:
  CommandDispatcherContext context_;
  FXL_DISALLOW_COPY_AND_ASSIGN(CommandDispatcher);
};

// TODO(SCN-421): Remove this once view manager is another Scenic system.
class TempSessionDelegate : public CommandDispatcher {
 public:
  explicit TempSessionDelegate(CommandDispatcherContext context);

  virtual void Present(
      uint64_t presentation_time, ::fidl::VectorPtr<zx::event> acquire_fences,
      ::fidl::VectorPtr<zx::event> release_fences,
      fuchsia::ui::scenic::Session::PresentCallback callback) = 0;

  virtual void HitTest(
      uint32_t node_id, ::fuchsia::ui::gfx::vec3 ray_origin,
      ::fuchsia::ui::gfx::vec3 ray_direction,
      fuchsia::ui::scenic::Session::HitTestCallback callback) = 0;

  virtual void HitTestDeviceRay(
      ::fuchsia::ui::gfx::vec3 ray_origin,
      ::fuchsia::ui::gfx::vec3 ray_direction,
      fuchsia::ui::scenic::Session::HitTestCallback callback) = 0;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(TempSessionDelegate);
};

}  // namespace scenic

#endif  // GARNET_LIB_UI_SCENIC_COMMAND_DISPATCHER_H_
