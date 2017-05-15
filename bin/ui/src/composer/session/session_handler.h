// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/mozart/services/composer/session.fidl.h"
#include "apps/mozart/src/composer/session/session.h"
#include "apps/mozart/src/composer/util/error_reporter.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/ftl/tasks/task_runner.h"

namespace mozart {
namespace composer {

class ComposerImpl;

// Implements the Session FIDL interface.  For now, does nothing but buffer
// operations from Enqueue() before passing them all to |session_| when Commit()
// is called.  Eventually, this class may do more work if performance profiling
// suggests to.
class SessionHandler final : public mozart2::Session, private ErrorReporter {
 public:
  SessionHandler(ComposerImpl* composer,
                 SessionId session_id,
                 ::fidl::InterfaceRequest<mozart2::Session> request);
  ~SessionHandler() override;

 private:
  // mozart2::Session interface methods.
  void Enqueue(::fidl::Array<mozart2::OpPtr> ops) override;
  void Present(::fidl::Array<mx::event> wait_events,
               ::fidl::Array<mx::event> signal_events) override;

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
  ::fidl::Binding<mozart2::Session> binding_;

  ::fidl::Array<mozart2::OpPtr> buffered_ops_;
};

}  // namespace composer
}  // namespace mozart
