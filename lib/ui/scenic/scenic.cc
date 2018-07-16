// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/scenic/scenic.h"

#include "lib/component/cpp/startup_context.h"

namespace scenic {

Scenic::Scenic(component::StartupContext* app_context,
               fit::closure quit_callback)
    : app_context_(app_context), quit_callback_(std::move(quit_callback)) {
  FXL_DCHECK(app_context_);

  app_context->outgoing().AddPublicService(scenic_bindings_.GetHandler(this));
}

Scenic::~Scenic() = default;

void Scenic::OnSystemInitialized(System* system) {
  size_t num_erased = uninitialized_systems_.erase(system);
  FXL_CHECK(num_erased == 1);

  if (uninitialized_systems_.empty()) {
    for (auto& closure : run_after_all_systems_initialized_) {
      closure();
    }
    run_after_all_systems_initialized_.clear();
  }
}

void Scenic::CloseSession(Session* session) {
  for (auto& binding : session_bindings_.bindings()) {
    // It's possible that this is called by BindingSet::CloseAndCheckForEmpty.
    // In that case, binding could be empty, so check for that.
    if (binding && binding->impl().get() == session) {
      binding->Unbind();
      return;
    }
  }
}

void Scenic::CreateSession(
    ::fidl::InterfaceRequest<fuchsia::ui::scenic::Session> session_request,
    ::fidl::InterfaceHandle<fuchsia::ui::scenic::SessionListener> listener) {
  if (uninitialized_systems_.empty()) {
    CreateSessionImmediately(std::move(session_request), std::move(listener));
  } else {
    run_after_all_systems_initialized_.push_back(
        [this, session_request = std::move(session_request),
         listener = std::move(listener)]() mutable {
          CreateSessionImmediately(std::move(session_request),
                                   std::move(listener));
        });
  }
}

void Scenic::CreateSessionImmediately(
    ::fidl::InterfaceRequest<fuchsia::ui::scenic::Session> session_request,
    ::fidl::InterfaceHandle<fuchsia::ui::scenic::SessionListener> listener) {
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

void Scenic::GetDisplayInfo(
    fuchsia::ui::scenic::Scenic::GetDisplayInfoCallback callback) {
  FXL_DCHECK(systems_[System::kGfx]);
  TempSystemDelegate* delegate =
      reinterpret_cast<TempSystemDelegate*>(systems_[System::kGfx].get());
  delegate->GetDisplayInfo(std::move(callback));
}

void Scenic::TakeScreenshot(
    fuchsia::ui::scenic::Scenic::TakeScreenshotCallback callback) {
  FXL_DCHECK(systems_[System::kGfx]);
  TempSystemDelegate* delegate =
      reinterpret_cast<TempSystemDelegate*>(systems_[System::kGfx].get());
  delegate->TakeScreenshot(std::move(callback));
}

void Scenic::GetDisplayOwnershipEvent(
    fuchsia::ui::scenic::Scenic::GetDisplayOwnershipEventCallback callback) {
  FXL_DCHECK(systems_[System::kGfx]);
  TempSystemDelegate* delegate =
      reinterpret_cast<TempSystemDelegate*>(systems_[System::kGfx].get());
  delegate->GetDisplayOwnershipEvent(std::move(callback));
}

}  // namespace scenic
