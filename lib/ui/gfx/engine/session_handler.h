// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_GFX_ENGINE_SESSION_HANDLER_H_
#define GARNET_LIB_UI_GFX_ENGINE_SESSION_HANDLER_H_

#include "lib/fidl/cpp/binding_set.h"
#include "lib/fidl/cpp/interface_ptr_set.h"
#include "lib/fxl/tasks/task_runner.h"

#include <fuchsia/ui/scenic/cpp/fidl.h>
#include "garnet/lib/ui/gfx/engine/engine.h"
#include "garnet/lib/ui/gfx/engine/session.h"
#include "garnet/lib/ui/scenic/command_dispatcher.h"
#include "garnet/lib/ui/scenic/event_reporter.h"
#include "garnet/lib/ui/scenic/util/error_reporter.h"

namespace scenic {
namespace gfx {

class SceneManagerImpl;

// Implements the Session FIDL interface.  For now, does nothing but buffer
// operations from Enqueue() before passing them all to |session_| when
// Commit() is called.  Eventually, this class may do more work if performance
// profiling suggests to.
//
// TODO(SCN-709): Unify SessionHandler and Session.
class SessionHandler : public TempSessionDelegate {
 public:
  SessionHandler(CommandDispatcherContext context, Engine* engine,
                 SessionId session_id, EventReporter* event_reporter,
                 ErrorReporter* error_reporter);
  virtual ~SessionHandler();

  scenic::gfx::Session* session() const { return session_.get(); }

 protected:
  // |fuchsia::ui::scenic::Session / scenic::TempSessionDelegate|
  void Present(uint64_t presentation_time,
               ::fidl::VectorPtr<zx::event> acquire_fences,
               ::fidl::VectorPtr<zx::event> release_fences,
               fuchsia::ui::scenic::Session::PresentCallback callback) override;

  // |fuchsia::ui::scenic::Session / scenic::TempSessionDelegate|
  void HitTest(uint32_t node_id, ::fuchsia::ui::gfx::vec3 ray_origin,
               ::fuchsia::ui::gfx::vec3 ray_direction,
               fuchsia::ui::scenic::Session::HitTestCallback callback) override;

  // |fuchsia::ui::scenic::Session / scenic::TempSessionDelegate|
  void HitTestDeviceRay(
      ::fuchsia::ui::gfx::vec3 ray_origin,
      ::fuchsia::ui::gfx::vec3 ray_direction,
      fuchsia::ui::scenic::Session::HitTestCallback callback) override;

  // |scenic::CommandDispatcher|
  void DispatchCommand(fuchsia::ui::scenic::Command command) override;

 private:
  friend class SessionManager;

  // Called by |binding_| when the connection closes. Must be invoked within
  // the SessionHandler MessageLoop.
  void BeginTearDown();

  // Called only by Engine. Use BeginTearDown() instead when you need to
  // teardown from within SessionHandler.
  void TearDown();

  SessionManager* const session_manager_;

  EventReporter* const event_reporter_;
  ErrorReporter* const error_reporter_;
  scenic::gfx::SessionPtr session_;

  // TODO(SCN-710): We reallocate this everytime we std::move it into
  // ScheduleUpdate().  The bug has some ideas about how to do better.
  std::vector<::fuchsia::ui::gfx::Command> buffered_commands_;
};

}  // namespace gfx
}  // namespace scenic

#endif  // GARNET_LIB_UI_GFX_ENGINE_SESSION_HANDLER_H_
