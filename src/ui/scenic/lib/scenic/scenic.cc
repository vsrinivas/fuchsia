// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/scenic/scenic.h"

#include <lib/async/default.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/macros.h>

#include "src/lib/fsl/handles/object_info.h"

namespace scenic_impl {

using fuchsia::ui::scenic::SessionEndpoints;

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

void Scenic::SetRegisterViewFocuser(
    fit::function<void(zx_koid_t, fidl::InterfaceRequest<fuchsia::ui::views::Focuser>)>
        register_view_focuser) {
  register_view_focuser_ = std::move(register_view_focuser);
}

void Scenic::SetViewRefFocusedRegisterFunction(
    fit::function<void(zx_koid_t, fidl::InterfaceRequest<fuchsia::ui::views::ViewRefFocused>)>
        vrf_register_function) {
  view_ref_focused_register_ = std::move(vrf_register_function);
}

void Scenic::SetFrameScheduler(const std::shared_ptr<scheduling::FrameScheduler>& frame_scheduler) {
  FX_DCHECK(!frame_scheduler_) << "Error: FrameScheduler already set";
  FX_DCHECK(frame_scheduler) << "Error: No FrameScheduler provided";
  frame_scheduler_ = frame_scheduler;
}

void Scenic::CloseSession(scheduling::SessionId session_id) {
  FX_LOGS(INFO) << "Scenic::CloseSession() session_id=" << session_id;

  sessions_.erase(session_id);

  if (frame_scheduler_) {
    frame_scheduler_->RemoveSession(session_id);
    // Schedule a final update to clean up any session leftovers from last frame.
    auto present_id = frame_scheduler_->RegisterPresent(session_id, /*release_fences*/ {});
    frame_scheduler_->ScheduleUpdateForSession(zx::time(0), {session_id, present_id},
                                               /*squashable*/ false);
  }
}

scheduling::SessionUpdater::UpdateResults Scenic::UpdateSessions(
    const std::unordered_map<scheduling::SessionId, scheduling::PresentId>& sessions_to_update,
    uint64_t trace_id) {
  scheduling::SessionUpdater::UpdateResults results;
  for (auto& [type_id, system] : systems_) {
    auto temp_result = system->UpdateSessions(
        sessions_to_update, trace_id,
        // Have to destroy the session *inside* GfxSystem to make sure the resulting ViewTree
        // updates are added before we commit updates to the ViewTree.
        /*destroy_session*/ [this](scheduling::SessionId session_id) { CloseSession(session_id); });
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
  SessionEndpoints endpoints;
  endpoints.set_session(std::move(session_request));
  endpoints.set_session_listener(std::move(listener));
  CreateSessionImmediately(std::move(endpoints));
}

void Scenic::CreateSession2(fidl::InterfaceRequest<fuchsia::ui::scenic::Session> session_request,
                            fidl::InterfaceHandle<fuchsia::ui::scenic::SessionListener> listener,
                            fidl::InterfaceRequest<fuchsia::ui::views::Focuser> view_focuser) {
  SessionEndpoints endpoints;
  endpoints.set_session(std::move(session_request));
  endpoints.set_session_listener(std::move(listener));
  endpoints.set_view_focuser(std::move(view_focuser));
  CreateSessionImmediately(std::move(endpoints));
}

void Scenic::CreateSessionT(SessionEndpoints endpoints, CreateSessionTCallback callback) {
  if (!endpoints.has_session()) {
    // We need explicit handling of the missing Session request here, because
    // CreateSessionImmediately will just make up a new one in endpoints.mutable_session().
    // We can't cleanly "just close" the Scenic channel to the client, though, because all Scenic
    // channels are bound to (and identified with) the singleton scenic_impl::Scenic object.
    FX_LOGS(ERROR) << "Request failed, request<fuchsia.ui.scenic.Session> is required but missing.";
    callback();
    return;
  }

  CreateSessionImmediately(std::move(endpoints));
  callback();  // acknowledge this request
}

void Scenic::CreateSessionImmediately(SessionEndpoints endpoints) {
  const SessionId session_id = scheduling::GetNextSessionId();

  zx_koid_t koid;
  zx_koid_t peer_koid;
  std::tie(koid, peer_koid) = fsl::GetKoids(endpoints.session().channel().get());
  FX_LOGS(INFO) << "Scenic::CreateSessionImmediately() session_id=" << session_id
                << " koid=" << koid << " peer_koid=" << peer_koid;

  auto destroy_session_function = [this, session_id](auto...) { CloseSession(session_id); };

  auto session = std::make_unique<scenic_impl::Session>(
      session_id, std::move(*endpoints.mutable_session()),
      std::move(*endpoints.mutable_session_listener()), destroy_session_function);
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

  std::vector<fit::function<void(zx_koid_t)>> on_view_created_callbacks;

  if (endpoints.has_view_focuser()) {
    if (endpoints.view_focuser() && register_view_focuser_) {
      on_view_created_callbacks.emplace_back(
          [this, focuser = std::move(*endpoints.mutable_view_focuser())](
              zx_koid_t view_ref_koid) mutable {
            FX_DCHECK(register_view_focuser_);
            register_view_focuser_(view_ref_koid, std::move(focuser));
          });
    } else if (!endpoints.view_focuser()) {
      FX_VLOGS(2) << "Invalid fuchsia.ui.views.Focuser request.";
    } else if (!register_view_focuser_) {
      FX_LOGS(ERROR) << "Failed to register fuchsia.ui.views.Focuser request.";
    }
  }

  if (endpoints.has_view_ref_focused()) {
    if (endpoints.view_ref_focused() && view_ref_focused_register_) {
      on_view_created_callbacks.emplace_back(
          [this, vrf = std::move(*endpoints.mutable_view_ref_focused())](
              zx_koid_t view_ref_koid) mutable {
            FX_DCHECK(view_ref_focused_register_);
            view_ref_focused_register_(view_ref_koid, std::move(vrf));
          });
    } else if (!endpoints.view_ref_focused()) {
      FX_VLOGS(2) << "Invalid fuchsia.ui.views.ViewRefFocused request.";
    } else if (!view_ref_focused_register_) {
      FX_LOGS(ERROR) << "Failed to register fuchsia.ui.views.ViewRefFocused request.";
    }
  }

  if (endpoints.has_touch_source()) {
    if (register_touch_source_) {
      on_view_created_callbacks.emplace_back(
          [this, touch_source = std::move(*endpoints.mutable_touch_source())](
              zx_koid_t view_ref_koid) mutable {
            register_touch_source_(std::move(touch_source), view_ref_koid);
          });
    } else if (!register_touch_source_) {
      FX_LOGS(ERROR) << "Failed to register fuchsia.ui.pointer.TouchSource request.";
    }
  }

  if (endpoints.has_mouse_source()) {
    if (register_mouse_source_) {
      on_view_created_callbacks.emplace_back(
          [this, mouse_source = std::move(*endpoints.mutable_mouse_source())](
              zx_koid_t view_ref_koid) mutable {
            register_mouse_source_(std::move(mouse_source), view_ref_koid);
          });
    } else if (!register_mouse_source_) {
      FX_LOGS(ERROR) << "Failed to register fuchsia.ui.pointer.MouseSource request.";
    }
  }

  {
    const auto it = dispatchers.find(System::kGfx);
    if (it != dispatchers.end()) {
      it->second->SetOnViewCreated(
          [callbacks = std::move(on_view_created_callbacks)](zx_koid_t view_ref_koid) {
            for (auto& callback : callbacks) {
              callback(view_ref_koid);
            }
          });
    }
  }
  session->SetCommandDispatchers(std::move(dispatchers));
  FX_CHECK(sessions_.find(session_id) == sessions_.end());
  sessions_[session_id] = std::move(session);
}

void Scenic::GetDisplayInfo(fuchsia::ui::scenic::Scenic::GetDisplayInfoCallback callback) {
  // TODO(fxbug.dev/23686): This code assumes that, once all systems have been initialized, that
  // there will be a proper delegate for Scenic API functions. Attached to the bug to remove this
  // delegate class completely. If the delegate becomes a permanent fixture of the system,
  // switch to fxbug.dev/24689, as we need a more formal mechanism for delayed execution and
  // initialization order logic.
  FX_DCHECK(display_delegate_);
  display_delegate_->GetDisplayInfo(std::move(callback));
}

void Scenic::TakeScreenshot(fuchsia::ui::scenic::Scenic::TakeScreenshotCallback callback) {
  // TODO(fxbug.dev/23686): This code assumes that, once all systems have been initialized, that
  // there will be a proper delegate for Scenic API functions. Attached to the bug to remove this
  // delegate class completely. If the delegate becomes a permanent fixture of the system,
  // switch to fxbug.dev/24689, as we need a more formal mechanism for delayed execution and
  // initialization order logic.
  FX_DCHECK(screenshot_delegate_);
  screenshot_delegate_->TakeScreenshot(std::move(callback));
}

void Scenic::GetDisplayOwnershipEvent(
    fuchsia::ui::scenic::Scenic::GetDisplayOwnershipEventCallback callback) {
  // TODO(fxbug.dev/23686): This code assumes that, once all systems have been initialized, that
  // there will be a proper delegate for Scenic API functions. Attached to the bug to remove this
  // delegate class completely. If the delegate becomes a permanent fixture of the system,
  // switch to fxbug.dev/24689, as we need a more formal mechanism for delayed execution and
  // initialization order logic.
  FX_DCHECK(display_delegate_);
  display_delegate_->GetDisplayOwnershipEvent(std::move(callback));
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
