// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_GFX_ENGINE_SESSION_HANDLER_H_
#define GARNET_LIB_UI_GFX_ENGINE_SESSION_HANDLER_H_

#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fidl/cpp/interface_ptr_set.h>
#include <lib/inspect/inspect.h>

#include "garnet/lib/ui/gfx/engine/engine.h"
#include "garnet/lib/ui/gfx/engine/session.h"
#include "garnet/lib/ui/scenic/command_dispatcher.h"
#include "garnet/lib/ui/scenic/event_reporter.h"
#include "garnet/lib/ui/scenic/util/error_reporter.h"

namespace scenic_impl {
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
  SessionHandler(CommandDispatcherContext context,
                 SessionContext session_context, EventReporter* event_reporter,
                 ErrorReporter* error_reporter,
                 inspect::Object inspect_object = inspect::Object());
  ~SessionHandler() = default;

  scenic_impl::gfx::Session* session() const { return session_.get(); }

  // Called to initiate a session crash when an update fails.
  // Requests the destruction of client fidl session, which
  // then triggers the actual destruction of the SessionHandler
  void KillSession();

 protected:
  // |fuchsia::ui::scenic::Session / scenic::TempSessionDelegate|
  void Present(uint64_t presentation_time,
               ::std::vector<zx::event> acquire_fences,
               ::std::vector<zx::event> release_fences,
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

  // |fuchsia::ui::scenic::Session / scenic::TempSessionDelegate|
  void SetDebugName(const std::string& debug_name) override {
    session_->SetDebugName(debug_name);
  }

  // |scenic::CommandDispatcher|
  void DispatchCommand(fuchsia::ui::scenic::Command command) override;

 private:
  std::unique_ptr<Session> session_;

  // TODO(SCN-710): We reallocate this everytime we std::move it into
  // ScheduleUpdate().  The bug has some ideas about how to do better.
  std::vector<::fuchsia::ui::gfx::Command> buffered_commands_;
};

}  // namespace gfx
}  // namespace scenic_impl

#endif  // GARNET_LIB_UI_GFX_ENGINE_SESSION_HANDLER_H_
