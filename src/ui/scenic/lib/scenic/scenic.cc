// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/scenic/scenic.h"

#include <lib/async/default.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/macros.h>

namespace scenic_impl {

Scenic::Scenic(sys::ComponentContext* app_context, inspect::Node inspect_node,
               fit::closure quit_callback)
    : app_context_(app_context),
      quit_callback_(std::move(quit_callback)),
      inspect_node_(std::move(inspect_node)) {
  FX_DCHECK(app_context_);

  app_context->outgoing()->AddPublicService(scenic_bindings_.GetHandler(this));

  // Scenic relies on having a valid default dispatcher. A hard check here means
  // we don't have to be defensive everywhere else.
  FX_CHECK(async_get_default_dispatcher());
}

Scenic::~Scenic() = default;

void Scenic::SetInitialized(fxl::WeakPtr<gfx::ViewFocuserRegistry> view_focuser_registry) {
  view_focuser_registry_ = view_focuser_registry;

  initialized_ = true;
  for (auto& closure : run_after_initialized_) {
    closure();
  }
  run_after_initialized_.clear();
}

void Scenic::SetFrameScheduler(const std::shared_ptr<scheduling::FrameScheduler>& frame_scheduler) {
  FX_DCHECK(!frame_scheduler_) << "Error: FrameScheduler already set";
  FX_DCHECK(frame_scheduler) << "Error: No FrameScheduler provided";
  frame_scheduler_ = frame_scheduler;
}

void Scenic::CloseSession(scheduling::SessionId session_id) {
  sessions_.erase(session_id);

  if (frame_scheduler_) {
    frame_scheduler_->RemoveSession(session_id);
  }
  if (view_focuser_registry_) {
    view_focuser_registry_->UnregisterViewFocuser(session_id);
  }
}

void Scenic::RunAfterInitialized(fit::closure closure) {
  if (initialized_) {
    closure();
  } else {
    run_after_initialized_.push_back(std::move(closure));
  }
}

scheduling::SessionUpdater::UpdateResults Scenic::UpdateSessions(
    const std::unordered_map<scheduling::SessionId, scheduling::PresentId>& sessions_to_update,
    uint64_t trace_id) {
  scheduling::SessionUpdater::UpdateResults results;
  for (auto& [type_id, system] : systems_) {
    auto temp_result = system->UpdateSessions(sessions_to_update, trace_id);
    for (auto session_id : temp_result.sessions_with_failed_updates) {
      CloseSession(session_id);
    }
    results.sessions_with_failed_updates.insert(temp_result.sessions_with_failed_updates.begin(),
                                                temp_result.sessions_with_failed_updates.end());
  }
  return results;
}

void Scenic::OnFramePresented(
    const std::unordered_map<scheduling::SessionId, std::map<scheduling::PresentId, zx::time>>&
        latched_times,
    scheduling::PresentTimestamps present_times) {
  for (const auto& [session_id, latched_map] : latched_times) {
    const auto session_it = sessions_.find(session_id);
    if (session_it != sessions_.end()) {
      session_it->second->OnPresented(latched_map, present_times);
    }
  }
}

void Scenic::CreateSession(fidl::InterfaceRequest<fuchsia::ui::scenic::Session> session_request,
                           fidl::InterfaceHandle<fuchsia::ui::scenic::SessionListener> listener) {
  RunAfterInitialized([this, session_request = std::move(session_request),
                       listener = std::move(listener)]() mutable {
    CreateSessionImmediately(std::move(session_request), std::move(listener),
                             fidl::InterfaceRequest<fuchsia::ui::views::Focuser>(/*invalid*/));
  });
}

void Scenic::CreateSession2(fidl::InterfaceRequest<fuchsia::ui::scenic::Session> session_request,
                            fidl::InterfaceHandle<fuchsia::ui::scenic::SessionListener> listener,
                            fidl::InterfaceRequest<fuchsia::ui::views::Focuser> view_focuser) {
  RunAfterInitialized([this, session_request = std::move(session_request),
                       listener = std::move(listener),
                       view_focuser = std::move(view_focuser)]() mutable {
    CreateSessionImmediately(std::move(session_request), std::move(listener),
                             std::move(view_focuser));
  });
}

void Scenic::CreateSessionImmediately(
    fidl::InterfaceRequest<fuchsia::ui::scenic::Session> session_request,
    fidl::InterfaceHandle<fuchsia::ui::scenic::SessionListener> listener,
    fidl::InterfaceRequest<fuchsia::ui::views::Focuser> view_focuser) {
  const SessionId session_id = scheduling::GetNextSessionId();
  auto destroy_session_function = [this, session_id](auto...) { CloseSession(session_id); };

  auto session = std::make_unique<scenic_impl::Session>(
      session_id, std::move(session_request), std::move(listener), destroy_session_function);
  FX_DCHECK(session_id == session->id());

  session->SetFrameScheduler(frame_scheduler_);

  session->set_binding_error_handler(destroy_session_function);

  // Give each installed System an opportunity to install a CommandDispatcher in
  // the newly-created Session.
  std::unordered_map<System::TypeId, CommandDispatcherUniquePtr> dispatchers;
  for (auto& [type_id, system] : systems_) {
    dispatchers[type_id] = system->CreateCommandDispatcher(session_id, session->event_reporter(),
                                                           session->error_reporter());
  }
  session->SetCommandDispatchers(std::move(dispatchers));

  FX_CHECK(sessions_.find(session_id) == sessions_.end());
  sessions_[session_id] = std::move(session);

  if (view_focuser && view_focuser_registry_) {
    view_focuser_registry_->RegisterViewFocuser(session_id, std::move(view_focuser));
  } else if (!view_focuser) {
    FX_VLOGS(2) << "Invalid fuchsia.ui.views.Focuser request.";
  } else if (!view_focuser_registry_) {
    FX_LOGS(ERROR) << "Failed to register fuchsia.ui.views.Focuser request.";
  }
}

void Scenic::GetDisplayInfo(fuchsia::ui::scenic::Scenic::GetDisplayInfoCallback callback) {
  RunAfterInitialized([this, callback = std::move(callback)]() mutable {
    // TODO(fxbug.dev/23686): This code assumes that, once all systems have been initialized, that
    // there will be a proper delegate for Scenic API functions. Attached to the bug to remove this
    // delegate class completely. If the delegate becomes a permanent fixture of the system,
    // switch to SCN-1506, as we need a more formal mechanism for delayed execution and
    // initialization order logic.
    FX_DCHECK(display_delegate_);
    display_delegate_->GetDisplayInfo(std::move(callback));
  });
}

void Scenic::TakeScreenshot(fuchsia::ui::scenic::Scenic::TakeScreenshotCallback callback) {
  RunAfterInitialized([this, callback = std::move(callback)]() mutable {
    // TODO(fxbug.dev/23686): This code assumes that, once all systems have been initialized, that
    // there will be a proper delegate for Scenic API functions. Attached to the bug to remove this
    // delegate class completely. If the delegate becomes a permanent fixture of the system,
    // switch to SCN-1506, as we need a more formal mechanism for delayed execution and
    // initialization order logic.
    FX_DCHECK(screenshot_delegate_);
    screenshot_delegate_->TakeScreenshot(std::move(callback));
  });
}

void Scenic::GetDisplayOwnershipEvent(
    fuchsia::ui::scenic::Scenic::GetDisplayOwnershipEventCallback callback) {
  RunAfterInitialized([this, callback = std::move(callback)]() mutable {
    // TODO(fxbug.dev/23686): This code assumes that, once all systems have been initialized, that
    // there will be a proper delegate for Scenic API functions. Attached to the bug to remove this
    // delegate class completely. If the delegate becomes a permanent fixture of the system,
    // switch to SCN-1506, as we need a more formal mechanism for delayed execution and
    // initialization order logic.
    FX_DCHECK(display_delegate_);
    display_delegate_->GetDisplayOwnershipEvent(std::move(callback));
  });
}

void Scenic::InitializeSnapshotService(
    std::unique_ptr<fuchsia::ui::scenic::internal::Snapshot> snapshot) {
  snapshot_ = std::move(snapshot);
  app_context_->outgoing()->AddPublicService(snapshot_bindings_.GetHandler(snapshot_.get()));
}

size_t Scenic::num_sessions() {
  int num_sessions = 0;
  for (auto&& elem : sessions_) {
    if (elem.second->is_bound())
      ++num_sessions;
  }
  return num_sessions;
}

}  // namespace scenic_impl
