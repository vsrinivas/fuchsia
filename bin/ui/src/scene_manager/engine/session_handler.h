// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/mozart/services/scene/session.fidl.h"
#include "apps/mozart/src/scene_manager/engine/session.h"
#include "apps/mozart/src/scene_manager/util/error_reporter.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/fidl/cpp/bindings/interface_ptr_set.h"
#include "lib/ftl/tasks/task_runner.h"

namespace scene_manager {

class SceneManagerImpl;

// Implements the Session FIDL interface.  For now, does nothing but buffer
// operations from Enqueue() before passing them all to |session_| when Commit()
// is called.  Eventually, this class may do more work if performance profiling
// suggests to.
class SessionHandler : public mozart2::Session, private ErrorReporter {
 public:
  SessionHandler(Engine* engine,
                 SessionId session_id,
                 ::fidl::InterfaceRequest<mozart2::Session> request,
                 ::fidl::InterfaceHandle<mozart2::SessionListener> listener);
  ~SessionHandler() override;

  scene_manager::Session* session() const { return session_.get(); }

  // Enqueues a session event for delivery.
  void EnqueueEvent(mozart2::EventPtr event);

  // Flushes enqueued session events to the session listener as a batch.
  void FlushEvents(uint64_t presentation_time);

 protected:
  // mozart2::Session interface methods.
  void Enqueue(::fidl::Array<mozart2::OpPtr> ops) override;
  void Present(uint64_t presentation_time,
               ::fidl::Array<mx::event> acquire_fences,
               ::fidl::Array<mx::event> release_fences,
               const PresentCallback& callback) override;

  void HitTest(uint32_t node_id,
               mozart2::vec3Ptr ray_origin,
               mozart2::vec3Ptr ray_direction,
               const HitTestCallback& callback) override;

 private:
  friend class Engine;

  // Customize behavior of ErrorReporter::ReportError().
  void ReportError(ftl::LogSeverity severity,
                   std::string error_string) override;

  // Called by |binding_| when the connection closes. Must be invoked within
  // the SessionHandler MessageLoop.
  void BeginTearDown();

  // Called only by Engine. Use BeginTearDown() instead when you need to
  // teardown from within SessionHandler.
  void TearDown();

  Engine* const engine_;
  scene_manager::SessionPtr session_;

  ::fidl::BindingSet<mozart2::Session> bindings_;
  ::fidl::InterfacePtr<mozart2::SessionListener> listener_;

  ::fidl::Array<mozart2::OpPtr> buffered_ops_;
  ::fidl::Array<mozart2::EventPtr> buffered_events_;
};

}  // namespace scene_manager
