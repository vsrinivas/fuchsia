// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/mozart/services/scene/session.fidl.h"
#include "apps/mozart/src/scene/session/session.h"
#include "apps/mozart/src/scene/util/error_reporter.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/fidl/cpp/bindings/interface_ptr_set.h"
#include "lib/ftl/tasks/task_runner.h"

namespace mozart {
namespace scene {

class SceneManagerImpl;

// Implements the Session FIDL interface.  For now, does nothing but buffer
// operations from Enqueue() before passing them all to |session_| when Commit()
// is called.  Eventually, this class may do more work if performance profiling
// suggests to.
class SessionHandler : public mozart2::Session, private ErrorReporter {
 public:
  SessionHandler(SceneManagerImpl* scene_manager,
                 SessionId session_id,
                 ::fidl::InterfaceRequest<mozart2::Session> request,
                 ::fidl::InterfaceHandle<mozart2::SessionListener> listener);
  ~SessionHandler() override;

  scene::Session* session() const { return session_.get(); }

 protected:
  // mozart2::Session interface methods.
  void Enqueue(::fidl::Array<mozart2::OpPtr> ops) override;
  void Present(uint64_t presentation_time,
               ::fidl::Array<mx::event> acquire_fences,
               ::fidl::Array<mx::event> release_fences,
               const PresentCallback& callback) override;
  void Connect(
      ::fidl::InterfaceRequest<mozart2::Session> session,
      ::fidl::InterfaceHandle<mozart2::SessionListener> listener) override;
  void HitTest(uint32_t node_id,
               mozart2::vec3Ptr ray_origin,
               mozart2::vec3Ptr ray_direction,
               const HitTestCallback& callback) override;

 private:
  friend class SceneManagerImpl;

  // Called by |binding_| when the connection closes, or by the SessionHandler
  // itself when there is a validation error while applying operations.  Must be
  // invoked within the SessionHandler MessageLoop.
  void BeginTeardown();

  // Customize behavior of ErrorReporter::ReportError().
  void ReportError(ftl::LogSeverity severity,
                   std::string error_string) override;

  void TearDown();

  SceneManagerImpl* const scene_manager_;
  scene::SessionPtr session_;

  ::fidl::BindingSet<mozart2::Session> bindings_;
  ::fidl::InterfacePtrSet<mozart2::SessionListener> listeners_;

  ::fidl::Array<mozart2::OpPtr> buffered_ops_;
};

}  // namespace scene
}  // namespace mozart
