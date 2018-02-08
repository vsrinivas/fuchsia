// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/mozart/mozart.h"

namespace mz {

Mozart::Mozart(app::ApplicationContext* app_context,
               fxl::TaskRunner* task_runner,
               Clock* clock)
    : app_context_(app_context), task_runner_(task_runner), clock_(clock) {
  FXL_DCHECK(app_context_);
  FXL_DCHECK(task_runner_);
  FXL_DCHECK(clock_);
}

Mozart::~Mozart() = default;

void Mozart::CreateSession(
    ::fidl::InterfaceRequest<ui_mozart::Session> session_request,
    ::fidl::InterfaceHandle<ui_mozart::SessionListener> listener) {
  auto session =
      std::make_unique<Session>(this, next_session_id_++, std::move(listener));

  // Give each installed System an opportunity to install a CommandDispatcher in
  // the newly-created Session.
  std::array<std::unique_ptr<CommandDispatcher>, System::TypeId::kMaxSystems>
      dispatchers;
  for (size_t i = 0; i < System::TypeId::kMaxSystems; ++i) {
    if (auto& system = systems_[i]) {
      dispatchers[i] = system->CreateCommandDispatcher(
          CommandDispatcherContext(this, session.get()));
    }
  }
  session->SetCommandDispatchers(std::move(dispatchers));

  session_bindings_.AddBinding(std::move(session), std::move(session_request));
}

void Mozart::CloseSession(Session* session) {
  for (auto& binding : session_bindings_.bindings()) {
    if (binding->impl().get() == session) {
      // A Session is only added to the bindings once, so we can return
      // immediately after finding one and unbinding it.
      binding->Unbind();
      return;
    }
  }
}

}  // namespace mz
