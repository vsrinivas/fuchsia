// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/fidl/cpp/bindings/interface_ptr_set.h"
#include "lib/fxl/tasks/task_runner.h"

#include "garnet/bin/ui/scene_manager/engine/engine.h"
#include "garnet/bin/ui/scene_manager/engine/event_reporter.h"
#include "garnet/bin/ui/scene_manager/engine/session.h"
#include "garnet/bin/ui/scene_manager/util/error_reporter.h"
#include "lib/ui/scenic/fidl/session.fidl.h"

namespace scene_manager {

class SceneManagerImpl;

// Implements the Session FIDL interface.  For now, does nothing but buffer
// operations from Enqueue() before passing them all to |session_| when
// Commit() is called.  Eventually, this class may do more work if performance
// profiling suggests to.
class SessionHandler : public scenic::Session,
                       public EventReporter,
                       private ErrorReporter {
 public:
  SessionHandler(Engine* engine,
                 SessionId session_id,
                 ::fidl::InterfaceRequest<scenic::Session> request,
                 ::fidl::InterfaceHandle<scenic::SessionListener> listener);
  ~SessionHandler() override;

  scene_manager::Session* session() const { return session_.get(); }

  // Flushes enqueued session events to the session listener as a batch.
  void SendEvents(::fidl::Array<scenic::EventPtr> events) override;

 protected:
  // scenic::Session interface methods.
  void Enqueue(::fidl::Array<scenic::OpPtr> ops) override;
  void Present(uint64_t presentation_time,
               ::fidl::Array<zx::event> acquire_fences,
               ::fidl::Array<zx::event> release_fences,
               const PresentCallback& callback) override;

  void HitTest(uint32_t node_id,
               scenic::vec3Ptr ray_origin,
               scenic::vec3Ptr ray_direction,
               const HitTestCallback& callback) override;

 private:
  friend class Engine;

  // Customize behavior of ErrorReporter::ReportError().
  void ReportError(fxl::LogSeverity severity,
                   std::string error_string) override;

  // Called by |binding_| when the connection closes. Must be invoked within
  // the SessionHandler MessageLoop.
  void BeginTearDown();

  // Called only by Engine. Use BeginTearDown() instead when you need to
  // teardown from within SessionHandler.
  void TearDown();

  Engine* const engine_;
  scene_manager::SessionPtr session_;

  ::fidl::BindingSet<scenic::Session> bindings_;
  ::fidl::InterfacePtr<scenic::SessionListener> listener_;

  ::fidl::Array<scenic::OpPtr> buffered_ops_;
};

}  // namespace scene_manager
