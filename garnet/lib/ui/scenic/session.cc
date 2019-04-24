// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/scenic/session.h"

#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <trace/event.h>

namespace scenic_impl {

Session::Session(
    SessionId id,
    ::fidl::InterfaceHandle<fuchsia::ui::scenic::SessionListener> listener)
    : id_(id), listener_(listener.Bind()), weak_factory_(this) {}

void Session::Enqueue(::std::vector<fuchsia::ui::scenic::Command> cmds) {
  // TODO(SCN-1265): Come up with a better solution to avoid children
  // calling into us during destruction.
  if (!valid_)
    return;
  for (auto& cmd : cmds) {
    // TODO(SCN-710): This dispatch is far from optimal in terms of performance.
    // We need to benchmark it to figure out whether it matters.
    System::TypeId type_id = SystemTypeForCmd(cmd);
    auto dispatcher = type_id != System::TypeId::kInvalid
                          ? dispatchers_[type_id].get()
                          : nullptr;
    if (dispatcher) {
      dispatcher->DispatchCommand(std::move(cmd));
    } else {
      EnqueueEvent(std::move(cmd));
    }
  }
}

void Session::Present(uint64_t presentation_time,
                      ::std::vector<zx::event> acquire_fences,
                      ::std::vector<zx::event> release_fences,
                      PresentCallback callback) {
  TRACE_DURATION("gfx", "scenic_impl::Session::Present");
  TRACE_FLOW_END("gfx", "Session::Present", next_present_trace_id_);
  next_present_trace_id_++;
  // TODO(SCN-1265): Come up with a better solution to avoid children
  // calling into us during destruction.
  if (!valid_)
    return;
  // TODO(SCN-469): Move Present logic into Session.
  auto& dispatcher = dispatchers_[System::TypeId::kGfx];
  FXL_DCHECK(dispatcher);
  TempSessionDelegate* delegate =
      static_cast<TempSessionDelegate*>(dispatcher.get());
  delegate->Present(presentation_time, std::move(acquire_fences),
                    std::move(release_fences), std::move(callback));
}

void Session::SetCommandDispatchers(
    std::array<CommandDispatcherUniquePtr, System::TypeId::kMaxSystems>
        dispatchers) {
  for (size_t i = 0; i < System::TypeId::kMaxSystems; ++i) {
    dispatchers_[i] = std::move(dispatchers[i]);
  }
}

void Session::HitTest(uint32_t node_id, ::fuchsia::ui::gfx::vec3 ray_origin,
                      ::fuchsia::ui::gfx::vec3 ray_direction,
                      HitTestCallback callback) {
  // TODO(SCN-1265): Come up with a better solution to avoid children
  // calling into us during destruction.
  if (!valid_)
    return;
  auto& dispatcher = dispatchers_[System::TypeId::kGfx];
  FXL_DCHECK(dispatcher);
  TempSessionDelegate* delegate =
      static_cast<TempSessionDelegate*>(dispatcher.get());
  delegate->HitTest(node_id, std::move(ray_origin), std::move(ray_direction),
                    std::move(callback));
}

void Session::SetDebugName(std::string debug_name) {
  // TODO(SCN-1265): Come up with a better solution to avoid children
  // calling into us during destruction.
  if (!valid_)
    return;
  auto& dispatcher = dispatchers_[System::TypeId::kGfx];
  FXL_DCHECK(dispatcher);
  TempSessionDelegate* delegate =
      static_cast<TempSessionDelegate*>(dispatcher.get());
  delegate->SetDebugName(debug_name);
}

void Session::HitTestDeviceRay(::fuchsia::ui::gfx::vec3 ray_origin,
                               ::fuchsia::ui::gfx::vec3 ray_direction,
                               HitTestCallback callback) {
  auto& dispatcher = dispatchers_[System::TypeId::kGfx];
  FXL_DCHECK(dispatcher);
  TempSessionDelegate* delegate =
      reinterpret_cast<TempSessionDelegate*>(dispatcher.get());
  delegate->HitTestDeviceRay(std::move(ray_origin), std::move(ray_direction),
                             std::move(callback));
}

void Session::PostFlushTask() {
  // If this is the first EnqueueEvent() since the last FlushEvent(), post a
  // task to ensure that FlushEvents() is called.
  if (buffered_events_.empty()) {
    async::PostTask(async_get_default_dispatcher(),
                    [weak = weak_factory_.GetWeakPtr()] {
                      if (weak) {
                        weak->FlushEvents();
                      }
                    });
  }
}

void Session::EnqueueEvent(fuchsia::ui::gfx::Event event) {
  // TODO(SCN-1265): Come up with a better solution to avoid children
  // calling into us during destruction.
  if (!valid_)
    return;
  PostFlushTask();

  fuchsia::ui::scenic::Event scenic_event;
  scenic_event.set_gfx(std::move(event));
  buffered_events_.push_back(std::move(scenic_event));
}

void Session::EnqueueEvent(fuchsia::ui::scenic::Command unhandled_command) {
  // TODO(SCN-1265): Come up with a better solution to avoid children
  // calling into us during destruction.
  if (!valid_)
    return;
  PostFlushTask();

  fuchsia::ui::scenic::Event scenic_event;
  scenic_event.set_unhandled(std::move(unhandled_command));
  buffered_events_.push_back(std::move(scenic_event));
}

void Session::EnqueueEvent(fuchsia::ui::input::InputEvent event) {
  // TODO(SCN-1265): Come up with a better solution to avoid
  // children calling
  // into us during destruction.
  if (!valid_)
    return;

  // Force an immediate flush, preserving event order.
  fuchsia::ui::scenic::Event scenic_event;
  scenic_event.set_input(std::move(event));
  buffered_events_.push_back(std::move(scenic_event));

  FlushEvents();
}

void Session::FlushEvents() {
  if (!buffered_events_.empty()) {
    if (listener_) {
      listener_->OnScenicEvent(std::move(buffered_events_));
    } else if (event_callback_) {
      // Only use the callback if there is no listener.  It is difficult to do
      // better because we std::move the argument into OnEvent().
      for (auto& evt : buffered_events_) {
        event_callback_(std::move(evt));
      }
    }
    buffered_events_.clear();
  }
}

void Session::ReportError(fxl::LogSeverity severity, std::string error_string) {
  // TODO(SCN-1265): Come up with a better solution to avoid children
  // calling into us during destruction.
  if (!valid_)
    return;

  switch (severity) {
    case fxl::LOG_INFO:
      FXL_LOG(INFO) << error_string;
      return;
    case fxl::LOG_WARNING:
      FXL_LOG(WARNING) << error_string;
      return;
    case fxl::LOG_ERROR:
      FXL_LOG(ERROR) << error_string;
      if (listener_) {
        listener_->OnScenicError(std::move(error_string));
      } else if (error_callback_) {
        // Only use the callback if there is no listener.  It is difficult to do
        // better because we std::move the argument into OnEvent().
        error_callback_(std::move(error_string));
      }
      return;
    case fxl::LOG_FATAL:
      FXL_LOG(FATAL) << error_string;
      return;
    default:
      // Invalid severity.
      FXL_DCHECK(false);
  }
}

}  // namespace scenic_impl
