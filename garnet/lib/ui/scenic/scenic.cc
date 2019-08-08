// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/scenic/scenic.h"

#include <lib/async/default.h>
#include <lib/sys/cpp/component_context.h>

#include "src/lib/fxl/logging.h"

namespace scenic_impl {

Scenic::Scenic(sys::ComponentContext* app_context, inspect_deprecated::Node inspect_node,
               fit::closure quit_callback)
    : app_context_(app_context),
      quit_callback_(std::move(quit_callback)),
      inspect_node_(std::move(inspect_node)) {
  FXL_DCHECK(app_context_);

  app_context->outgoing()->AddPublicService(scenic_bindings_.GetHandler(this));

  // Scenic relies on having a valid default dispatcher. A hard check here means
  // we don't have to be defensive everywhere else.
  FXL_CHECK(async_get_default_dispatcher());
}

Scenic::~Scenic() = default;

void Scenic::SetInitialized() {
  initialized_ = true;
  for (auto& closure : run_after_initialized_) {
    closure();
  }
  run_after_initialized_.clear();
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

void Scenic::RunAfterInitialized(fit::closure closure) {
  if (initialized_) {
    closure();
  } else {
    run_after_initialized_.push_back(std::move(closure));
  }
}

void Scenic::CreateSession(::fidl::InterfaceRequest<fuchsia::ui::scenic::Session> session_request,
                           ::fidl::InterfaceHandle<fuchsia::ui::scenic::SessionListener> listener) {
  RunAfterInitialized([this, session_request = std::move(session_request),
                       listener = std::move(listener)]() mutable {
    CreateSessionImmediately(std::move(session_request), std::move(listener));
  });
}

void Scenic::CreateSessionImmediately(
    ::fidl::InterfaceRequest<fuchsia::ui::scenic::Session> session_request,
    ::fidl::InterfaceHandle<fuchsia::ui::scenic::SessionListener> listener) {
  auto session = std::make_unique<Session>(next_session_id_++, std::move(listener));

  // Give each installed System an opportunity to install a CommandDispatcher in
  // the newly-created Session.
  std::array<CommandDispatcherUniquePtr, System::TypeId::kMaxSystems> dispatchers;
  for (size_t i = 0; i < System::TypeId::kMaxSystems; ++i) {
    if (auto& system = systems_[i]) {
      dispatchers[i] =
          system->CreateCommandDispatcher(CommandDispatcherContext(this, session.get()));
    }
  }
  session->SetCommandDispatchers(std::move(dispatchers));

  session_bindings_.AddBinding(std::move(session), std::move(session_request));
}

void Scenic::GetDisplayInfo(fuchsia::ui::scenic::Scenic::GetDisplayInfoCallback callback) {
  RunAfterInitialized([this, callback = std::move(callback)]() mutable {
    // TODO(SCN-452): This code assumes that, once all systems have been initialized, that there
    // will be a proper delegate for Scenic API functions. Attached to the bug to remove this
    // delegate class completely. If the delegate becomes a permanent fixture of the system, switch
    // to SCN-1506, as we need a more formal mechanism for delayed execution and initialization
    // order logic.
    FXL_DCHECK(delegate_);
    delegate_->GetDisplayInfo(std::move(callback));
  });
}

void Scenic::TakeScreenshot(fuchsia::ui::scenic::Scenic::TakeScreenshotCallback callback) {
  RunAfterInitialized([this, callback = std::move(callback)]() mutable {
    // TODO(SCN-452): This code assumes that, once all systems have been initialized, that there
    // will be a proper delegate for Scenic API functions. Attached to the bug to remove this
    // delegate class completely. If the delegate becomes a permanent fixture of the system, switch
    // to SCN-1506, as we need a more formal mechanism for delayed execution and initialization
    // order logic.
    FXL_DCHECK(delegate_);
    delegate_->TakeScreenshot(std::move(callback));
  });
}

void Scenic::GetDisplayOwnershipEvent(
    fuchsia::ui::scenic::Scenic::GetDisplayOwnershipEventCallback callback) {
  RunAfterInitialized([this, callback = std::move(callback)]() mutable {
    // TODO(SCN-452): This code assumes that, once all systems have been initialized, that there
    // will be a proper delegate for Scenic API functions. Attached to the bug to remove this
    // delegate class completely. If the delegate becomes a permanent fixture of the system, switch
    // to SCN-1506, as we need a more formal mechanism for delayed execution and initialization
    // order logic.
    FXL_DCHECK(delegate_);
    delegate_->GetDisplayOwnershipEvent(std::move(callback));
  });
}

}  // namespace scenic_impl
