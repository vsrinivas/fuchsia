// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_SCENIC_ENGINE_SESSION_HANDLER_H_
#define GARNET_LIB_UI_SCENIC_ENGINE_SESSION_HANDLER_H_

#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/fidl/cpp/bindings/interface_ptr_set.h"
#include "lib/fxl/tasks/task_runner.h"

#include "garnet/lib/ui/mozart/command_dispatcher.h"
#include "garnet/lib/ui/mozart/event_reporter.h"
#include "garnet/lib/ui/mozart/util/error_reporter.h"
#include "garnet/lib/ui/scenic/engine/engine.h"
#include "garnet/lib/ui/scenic/engine/session.h"
#include "lib/ui/mozart/fidl/events.fidl.h"

namespace scene_manager {

class SceneManagerImpl;

// Implements the Session FIDL interface.  For now, does nothing but buffer
// operations from Enqueue() before passing them all to |session_| when
// Commit() is called.  Eventually, this class may do more work if performance
// profiling suggests to.
class SessionHandler : public mz::TempSessionDelegate, public EventReporter {
 public:
  SessionHandler(mz::CommandDispatcherContext context,
                 Engine* engine,
                 SessionId session_id,
                 mz::EventReporter* event_reporter,
                 mz::ErrorReporter* error_reporter);
  virtual ~SessionHandler();

  scene_manager::Session* session() const { return session_.get(); }

  // Flushes enqueued session events to the session listener as a batch.
  void SendEvents(::f1dl::Array<scenic::EventPtr> events) override;

 protected:
  // |ui_mozart::Session|
  void Enqueue(::f1dl::Array<ui_mozart::CommandPtr> commands) override;
  void Present(uint64_t presentation_time,
               ::f1dl::Array<zx::event> acquire_fences,
               ::f1dl::Array<zx::event> release_fences,
               const ui_mozart::Session::PresentCallback& callback) override;

  void HitTest(uint32_t node_id,
               scenic::vec3Ptr ray_origin,
               scenic::vec3Ptr ray_direction,
               const ui_mozart::Session::HitTestCallback& callback) override;

  void HitTestDeviceRay(
      scenic::vec3Ptr ray_origin,
      scenic::vec3Ptr ray_direction,
      const ui_mozart::Session::HitTestCallback& clback) override;

  bool ApplyCommand(const ui_mozart::CommandPtr& command) override;

 private:
  friend class Engine;

  // Called by |binding_| when the connection closes. Must be invoked within
  // the SessionHandler MessageLoop.
  void BeginTearDown();

  // Called only by Engine. Use BeginTearDown() instead when you need to
  // teardown from within SessionHandler.
  void TearDown();

  Engine* const engine_;

  mz::EventReporter* const event_reporter_;
  mz::ErrorReporter* const error_reporter_;
  scene_manager::SessionPtr session_;

  ::f1dl::Array<scenic::OpPtr> buffered_ops_;
};

}  // namespace scene_manager

#endif  // GARNET_LIB_UI_SCENIC_ENGINE_SESSION_HANDLER_H_
