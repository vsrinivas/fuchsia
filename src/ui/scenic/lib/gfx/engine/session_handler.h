// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_GFX_ENGINE_SESSION_HANDLER_H_
#define SRC_UI_SCENIC_LIB_GFX_ENGINE_SESSION_HANDLER_H_

#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fidl/cpp/interface_ptr_set.h>

#include "src/lib/inspect_deprecated/inspect.h"
#include "src/ui/scenic/lib/gfx/engine/engine.h"
#include "src/ui/scenic/lib/gfx/engine/session.h"
#include "src/ui/scenic/lib/scenic/command_dispatcher.h"
#include "src/ui/scenic/lib/scenic/event_reporter.h"
#include "src/ui/scenic/lib/scenic/util/error_reporter.h"

namespace scenic_impl {
namespace gfx {

// Implements the Session FIDL interface.  For now, does nothing but buffer
// operations from Enqueue() before passing them all to |session_| when
// Commit() is called.  Eventually, this class may do more work if performance
// profiling suggests to.
//
// TODO(SCN-709): Unify SessionHandler and Session.
class SessionHandler : public TempSessionDelegate {
 public:
  SessionHandler(CommandDispatcherContext context, SessionContext session_context,
                 std::shared_ptr<EventReporter> event_reporter,
                 std::shared_ptr<ErrorReporter> error_reporter,
                 inspect_deprecated::Node inspect_node = inspect_deprecated::Node());
  // TODO(SCN-1485): along with ~Session(), this ensures that the contents are
  // properly removed from the scene-graph.  However, it doesn't trigger another
  // frame to show the updated scene-graph.
  ~SessionHandler() = default;

  scenic_impl::gfx::Session* session() const { return session_.get(); }

  // |CommandDispatcher|
  void SetDebugName(const std::string& debug_name) override;

  // Called to initiate a session crash when an update fails.
  // Requests the destruction of client fidl session, which
  // then triggers the actual destruction of the SessionHandler
  void KillSession() override;

 protected:
  // |fuchsia::ui::scenic::Session / scenic::TempSessionDelegate|
  void Present(uint64_t presentation_time, std::vector<zx::event> acquire_fences,
               std::vector<zx::event> release_fences,
               fuchsia::ui::scenic::Session::PresentCallback callback) override;

  // |fuchsia::ui::scenic::Session / scenic::TempSessionDelegate|
  void Present2(zx_time_t requested_presentation_time, std::vector<zx::event> acquire_fences,
                std::vector<zx::event> release_fences) override;

  // |scenic::TempSessionDelegate|
  void GetFuturePresentationInfos(
      zx::duration requested_prediction_span,
      scheduling::FrameScheduler::GetFuturePresentationInfosCallback callback) override;

  // |scenic::CommandDispatcher|
  void DispatchCommand(fuchsia::ui::scenic::Command command) override;

  // |scenic::CommandDispatcher|
  void SetOnFramePresentedCallback(OnFramePresentedCallback callback) override;

 private:
  std::unique_ptr<Session> session_;

  // TODO(SCN-710): We reallocate this everytime we std::move it into
  // ScheduleUpdate().  The bug has some ideas about how to do better.
  std::vector<::fuchsia::ui::gfx::Command> buffered_commands_;
};

}  // namespace gfx
}  // namespace scenic_impl

#endif  // SRC_UI_SCENIC_LIB_GFX_ENGINE_SESSION_HANDLER_H_
