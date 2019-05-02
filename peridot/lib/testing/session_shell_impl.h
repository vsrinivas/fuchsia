// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_LIB_TESTING_SESSION_SHELL_IMPL_H_
#define PERIDOT_LIB_TESTING_SESSION_SHELL_IMPL_H_

#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <fuchsia/ui/viewsv1token/cpp/fidl.h>
#include <lib/component/cpp/startup_context.h>
#include <lib/fidl/cpp/binding_set.h>
#include <src/lib/fxl/logging.h>
#include <src/lib/fxl/macros.h>
#include <src/lib/fxl/time/time_delta.h>

namespace modular {
namespace testing {

// An implementation of the fuchsia.modular.SessionShell FIDL service, to be
// used in session shell components in integration tests. Usually used through
// SessionShellBase.
class SessionShellImpl : fuchsia::modular::SessionShell {
 public:
  SessionShellImpl();
  ~SessionShellImpl() override;

  using ViewId = fuchsia::modular::ViewIdentifier;

  // Produces a handler function that can be used in the outgoing service
  // provider.
  fidl::InterfaceRequestHandler<fuchsia::modular::SessionShell> GetHandler();

  // Whenever SessionShell.AttachView() is called, the supplied callback is
  // invoked with the view ID. The ImportToken is dropped.
  void set_on_attach_view(fit::function<void(ViewId view_id)> callback) {
    on_attach_view_ = std::move(callback);
  }

  // Whenever SessionShell.DetachView() is called, the supplied callback is
  // invoked with the view ID. The return callback of DetachView() is invoked
  // asynchronously after a delay that can be configured by the client with
  // set_detach_delay().
  void set_on_detach_view(fit::function<void(ViewId view_id)> callback) {
    on_detach_view_ = std::move(callback);
  }

  // Configures the delay after which the return callback of DetachView() is
  // invoked. Used to test the timeout behavior of sessionmgr.
  void set_detach_delay(zx::duration detach_delay) {
    detach_delay_ = std::move(detach_delay);
  }

 private:
  // |SessionShell|
  void AttachView(fuchsia::modular::ViewIdentifier view_id,
                  fidl::InterfaceHandle<fuchsia::ui::viewsv1token::ViewOwner>
                      view_owner) override;

  // |SessionShell|
  void DetachView(fuchsia::modular::ViewIdentifier view_id,
                  fit::function<void()> done) override;

  fidl::BindingSet<fuchsia::modular::SessionShell> bindings_;
  fit::function<void(ViewId view_id)> on_attach_view_{[](ViewId) {}};
  fit::function<void(ViewId view_id)> on_detach_view_{[](ViewId) {}};
  zx::duration detach_delay_{};

  FXL_DISALLOW_COPY_AND_ASSIGN(SessionShellImpl);
};

}  // namespace testing
}  // namespace modular

#endif  // PERIDOT_LIB_TESTING_SESSION_SHELL_IMPL_H_
