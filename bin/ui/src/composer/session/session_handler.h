// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/mozart/services/scene/session.fidl.h"
#include "apps/mozart/src/composer/session/session.h"
#include "apps/mozart/src/composer/util/error_reporter.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/fidl/cpp/bindings/interface_ptr_set.h"
#include "lib/ftl/tasks/task_runner.h"

namespace mozart {
namespace composer {

class ComposerImpl;

// Implements the Session FIDL interface.  For now, does nothing but buffer
// operations from Enqueue() before passing them all to |session_| when Commit()
// is called.  Eventually, this class may do more work if performance profiling
// suggests to.
class SessionHandler : public mozart2::Session, private ErrorReporter {
 public:
  SessionHandler(ComposerImpl* composer,
                 SessionId session_id,
                 ::fidl::InterfaceRequest<mozart2::Session> request,
                 ::fidl::InterfaceHandle<mozart2::SessionListener> listener);
  ~SessionHandler() override;

  composer::Session* session() const { return session_.get(); }

 protected:
  // mozart2::Session interface methods.
  void Enqueue(::fidl::Array<mozart2::OpPtr> ops) override;
  void Present(::fidl::Array<mx::event> wait_events,
               ::fidl::Array<mx::event> signal_events) override;
  void Connect(
      ::fidl::InterfaceRequest<mozart2::Session> session,
      ::fidl::InterfaceHandle<mozart2::SessionListener> listener) override;

 private:
  friend class ComposerImpl;

  // Called by |binding_| when the connection closes, or by the SessionHandler
  // itself when there is a validation error while applying operations.  Must be
  // invoked within the SessionHandler MessageLoop.
  void BeginTeardown();

  // Customize behavior of ErrorReporter::ReportError().
  void ReportError(ftl::LogSeverity severity,
                   std::string error_string) override;

  void TearDown();

  ComposerImpl* const composer_;
  composer::SessionPtr session_;

  ::fidl::BindingSet<mozart2::Session> bindings_;
  ::fidl::InterfacePtrSet<mozart2::SessionListener> listeners_;

  ::fidl::Array<mozart2::OpPtr> buffered_ops_;
};

}  // namespace composer
}  // namespace mozart
