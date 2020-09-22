// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/scenic/session.h"

#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/async/time.h>
#include <lib/trace/event.h>

namespace scenic_impl {

Session::Session(SessionId id, fidl::InterfaceRequest<fuchsia::ui::scenic::Session> session_request,
                 fidl::InterfaceHandle<fuchsia::ui::scenic::SessionListener> listener,
                 std::function<void()> destroy_session_function)
    : id_(id),
      listener_(listener.Bind()),
      reporter_(std::make_shared<EventAndErrorReporter>(this)),
      binding_(this, std::move(session_request)),
      destroy_session_func_(std::move(destroy_session_function)),
      weak_factory_(this) {
  FX_DCHECK(!binding_.channel() || binding_.is_bound());
  static_assert(!std::is_move_constructible<Session>::value);
}

Session::~Session() { reporter_->Reset(); }

void Session::set_binding_error_handler(fit::function<void(zx_status_t)> error_handler) {
  binding_.set_error_handler(std::move(error_handler));
}

void Session::SetFrameScheduler(
    const std::shared_ptr<scheduling::FrameScheduler>& frame_scheduler) {
  FX_DCHECK(frame_scheduler_.expired()) << "Error: FrameScheduler already set";
  frame_scheduler_ = frame_scheduler;
}

void Session::Enqueue(std::vector<fuchsia::ui::scenic::Command> cmds) {
  TRACE_DURATION("gfx", "scenic_impl::Session::Enqueue", "session_id", id(), "num_commands",
                 cmds.size());
  for (auto& cmd : cmds) {
    // TODO(fxbug.dev/23932): This dispatch is far from optimal in terms of performance.
    // We need to benchmark it to figure out whether it matters.
    const System::TypeId type_id = SystemTypeForCmd(cmd);
    const auto dispatcher_it = dispatchers_.find(type_id);
    if (dispatcher_it == dispatchers_.end()) {
      reporter_->EnqueueEvent(std::move(cmd));
    } else if (type_id == System::TypeId::kInput) {
      // Input handles commands immediately and doesn't care about present calls.
      dispatcher_it->second->DispatchCommand(std::move(cmd), /*present_id=*/0);
    } else {
      commands_pending_present_.emplace_back(std::move(cmd));
    }
  }
}

void Session::Present(uint64_t presentation_time, std::vector<zx::event> acquire_fences,
                      std::vector<zx::event> release_fences,
                      scheduling::OnPresentedCallback callback) {
  TRACE_DURATION("gfx", "scenic_impl::Session::Present");

  if (std::holds_alternative<std::monostate>(present_helper_)) {
    present_helper_.emplace<scheduling::Present1Helper>();
  } else if (!std::holds_alternative<scheduling::Present1Helper>(present_helper_)) {
    reporter_->ERROR() << "Client cannot use Present() and Present2() in the same Session";
    destroy_session_func_();
    return;
  }

  // Logic verifying client requests presents in-order.
  const zx::time requested_presentation_time = zx::time(presentation_time);
  if (requested_presentation_time < last_scheduled_presentation_time_) {
    reporter_->ERROR() << "scenic_impl::Session: Present called with out-of-order "
                          "presentation time. "
                       << "requested presentation time=" << requested_presentation_time
                       << ", last scheduled presentation time=" << last_scheduled_presentation_time_
                       << ".";
    destroy_session_func_();
    return;
  }
  last_scheduled_presentation_time_ = requested_presentation_time;

  if (--num_presents_allowed_ < 0) {
    reporter_->ERROR() << "Present() called with no more present calls allowed.";
  }

  TRACE_FLOW_END("gfx", "Session::Present", next_present_trace_id_);
  next_present_trace_id_++;

  // TODO(fxbug.dev/56290): Handle the missing frame scheduler case.
  if (auto scheduler = frame_scheduler_.lock()) {
    // TODO(fxbug.dev/47308): Delete |present_information| argument from signature entirely.
    const scheduling::PresentId present_id = scheduler->RegisterPresent(
        id_, /*present_information*/ [](auto...) {}, std::move(release_fences));
    std::get<scheduling::Present1Helper>(present_helper_)
        .RegisterPresent(present_id, std::move(callback));
    SchedulePresentRequest(present_id, requested_presentation_time, std::move(acquire_fences));
  }
}

void Session::Present2(fuchsia::ui::scenic::Present2Args args, Present2Callback callback) {
  if (std::holds_alternative<std::monostate>(present_helper_)) {
    present_helper_.emplace<scheduling::Present2Helper>(
        /*on_frame_presented_event*/
        // Safe to capture |this| because the Session is guaranteed to outlive |present_helper_|,
        // Session is non-movable and Present2Helper does not fire closures after destruction.
        [this](fuchsia::scenic::scheduling::FramePresentedInfo info) {
          binding_.events().OnFramePresented(std::move(info));
        });
  } else if (!std::holds_alternative<scheduling::Present2Helper>(present_helper_)) {
    reporter_->ERROR() << "Client cannot use Present() and Present2() in the same Session";
    destroy_session_func_();
    return;
  }

  // Kill the Session if they have not set any of the Present2Args fields.
  if (!args.has_requested_presentation_time() || !args.has_release_fences() ||
      !args.has_acquire_fences() || !args.has_requested_prediction_span()) {
    reporter_->ERROR() << "One or more fields not set in Present2Args table";
    destroy_session_func_();
    return;
  }

  // Kill the Session if they have no more presents left.
  if (--num_presents_allowed_ < 0) {
    reporter_->ERROR()
        << "Present2() called with no more present calls allowed. Terminating session.";
    destroy_session_func_();
    return;
  }

  // Logic verifying client requests presents in-order.
  const zx::time requested_presentation_time = zx::time(args.requested_presentation_time());
  if (requested_presentation_time < last_scheduled_presentation_time_) {
    reporter_->ERROR() << "scenic_impl::Session: Present called with out-of-order "
                          "presentation time. "
                       << "requested presentation time=" << requested_presentation_time
                       << ", last scheduled presentation time=" << last_scheduled_presentation_time_
                       << ".";
    destroy_session_func_();
    return;
  }
  last_scheduled_presentation_time_ = requested_presentation_time;

  // Output requested presentation time in milliseconds.
  TRACE_DURATION("gfx", "scenic_impl::Session::Present2", "requested_presentation_time",
                 requested_presentation_time.get() / 1'000'000);
  TRACE_FLOW_END("gfx", "Session::Present", next_present_trace_id_);
  next_present_trace_id_++;

  // TODO(fxbug.dev/56290): Handle the missing frame scheduler case.
  if (auto scheduler = frame_scheduler_.lock()) {
    // TODO(fxbug.dev/47308): Delete |present_information| argument from signature entirely.
    const scheduling::PresentId present_id = scheduler->RegisterPresent(
        id_, /*present_information*/ [](auto...) {}, std::move(*args.mutable_release_fences()));
    std::get<scheduling::Present2Helper>(present_helper_)
        .RegisterPresent(present_id, /*present_received_time*/ zx::time(
                             async_now(async_get_default_dispatcher())));

    InvokeFuturePresentationTimesCallback(args.requested_prediction_span(), std::move(callback));
    SchedulePresentRequest(present_id, requested_presentation_time,
                           std::move(*args.mutable_acquire_fences()));
  }
}

void Session::SchedulePresentRequest(scheduling::PresentId present_id,
                                     zx::time requested_presentation_time,
                                     std::vector<zx::event> acquire_fences) {
  TRACE_DURATION("gfx", "scenic_impl::Sesssion::SchedulePresentRequest");
  TRACE_FLOW_BEGIN("gfx", "wait_for_fences", SESSION_TRACE_ID(id_, present_id));

  // Safe to capture |this| because the Session is guaranteed to outlive |fence_queue_|,
  // Session is non-movable and FenceQueue does not fire closures after destruction.
  fence_queue_->QueueTask(
      [this, present_id, requested_presentation_time,
       commands = std::move(commands_pending_present_)]() mutable {
        if (auto scheduler = frame_scheduler_.lock()) {
          TRACE_DURATION("gfx", "scenic_impl::Session::ScheduleNextPresent", "session_id", id_,
                         "requested_presentation_time",
                         requested_presentation_time.get() / 1'000'000);
          TRACE_FLOW_END("gfx", "wait_for_fences", SESSION_TRACE_ID(id_, present_id));

          for (auto& cmd : commands) {
            dispatchers_.at(SystemTypeForCmd(cmd))->DispatchCommand(std::move(cmd), present_id);
          }

          scheduler->ScheduleUpdateForSession(requested_presentation_time, {id_, present_id});
        } else {
          // TODO(fxbug.dev/56290): Handle the missing frame scheduler case.
          FX_LOGS(WARNING) << "FrameScheduler is missing.";
        }
      },
      std::move(acquire_fences));

  commands_pending_present_.clear();
}

void Session::RequestPresentationTimes(zx_duration_t requested_prediction_span,
                                       RequestPresentationTimesCallback callback) {
  TRACE_DURATION("gfx", "scenic_impl::Session::RequestPresentationTimes");
  InvokeFuturePresentationTimesCallback(requested_prediction_span, std::move(callback));
}

void Session::InvokeFuturePresentationTimesCallback(zx_duration_t requested_prediction_span,
                                                    RequestPresentationTimesCallback callback) {
  if (!callback)
    return;
  // TODO(fxbug.dev/56290): Handle the missing frame scheduler case.
  if (auto locked_frame_scheduler = frame_scheduler_.lock()) {
    locked_frame_scheduler->GetFuturePresentationInfos(
        zx::duration(requested_prediction_span),
        [weak = weak_factory_.GetWeakPtr(), callback = std::move(callback)](
            std::vector<fuchsia::scenic::scheduling::PresentationInfo> presentation_infos) {
          if (weak)
            callback({std::move(presentation_infos), weak->num_presents_allowed_});
        });
  }
}

void Session::OnPresented(const std::map<scheduling::PresentId, zx::time>& latched_times,
                          scheduling::PresentTimestamps present_times) {
  FX_DCHECK(!latched_times.empty());
  num_presents_allowed_ += latched_times.size();
  FX_DCHECK(num_presents_allowed_ <= scheduling::FrameScheduler::kMaxPresentsInFlight);

  if (auto* present2_helper = std::get_if<scheduling::Present2Helper>(&present_helper_)) {
    present2_helper->OnPresented(latched_times, present_times, num_presents_allowed_);
  } else if (auto* present1_helper = std::get_if<scheduling::Present1Helper>(&present_helper_)) {
    present1_helper->OnPresented(latched_times, present_times);
  } else {
    // Should never be reached.
    FX_CHECK(false);
  }
}

void Session::SetCommandDispatchers(
    std::unordered_map<System::TypeId, CommandDispatcherUniquePtr> dispatchers) {
  FX_DCHECK(dispatchers_.empty()) << "dispatchers should only be set once.";
  dispatchers_ = std::move(dispatchers);
}

void Session::SetDebugName(std::string debug_name) {
  TRACE_DURATION("gfx", "scenic_impl::Session::SetDebugName", "debug name", debug_name);
  for (auto& [type_id, dispatcher] : dispatchers_) {
    dispatcher->SetDebugName(debug_name);
  }
}

void Session::RegisterBufferCollection(
    uint32_t buffer_collection_id,
    fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token) {
  auto dispatcher = dispatchers_[System::TypeId::kGfx].get();
  FX_DCHECK(dispatcher);
  auto gfx_session = static_cast<gfx::Session*>(dispatcher);

  gfx_session->RegisterBufferCollection(buffer_collection_id, std::move(token));
}

void Session::DeregisterBufferCollection(uint32_t buffer_collection_id) {
  auto dispatcher = dispatchers_[System::TypeId::kGfx].get();
  FX_DCHECK(dispatcher);
  auto gfx_session = static_cast<gfx::Session*>(dispatcher);

  gfx_session->DeregisterBufferCollection(buffer_collection_id);
}

Session::EventAndErrorReporter::EventAndErrorReporter(Session* session)
    : session_(session), weak_factory_(this) {
  FX_DCHECK(session_);
}

void Session::EventAndErrorReporter::Reset() { session_ = nullptr; }

void Session::EventAndErrorReporter::PostFlushTask() {
  FX_DCHECK(session_);
  TRACE_DURATION("gfx", "scenic_impl::Session::EventAndErrorReporter::PostFlushTask");

  // If this is the first EnqueueEvent() since the last FlushEvent(), post a
  // task to ensure that FlushEvents() is called.
  if (buffered_events_.empty()) {
    async::PostTask(async_get_default_dispatcher(), [weak = weak_factory_.GetWeakPtr()] {
      if (!weak)
        return;
      weak->FilterRedundantGfxEvents();
      weak->FlushEvents();
    });
  }
}

void Session::EventAndErrorReporter::EnqueueEvent(fuchsia::ui::gfx::Event event) {
  if (!session_)
    return;

  TRACE_DURATION("gfx", "scenic_impl::Session::EventAndErrorReporter::EnqueueEvent", "event_type",
                 "gfx::Event");
  PostFlushTask();

  fuchsia::ui::scenic::Event scenic_event;
  scenic_event.set_gfx(std::move(event));
  buffered_events_.push_back(std::move(scenic_event));
}

void Session::EventAndErrorReporter::EnqueueEvent(fuchsia::ui::scenic::Command unhandled_command) {
  if (!session_)
    return;

  TRACE_DURATION("gfx", "scenic_impl::Session::EventAndErrorReporter::EnqueueEvent", "event_type",
                 "UnhandledCommand");
  PostFlushTask();

  fuchsia::ui::scenic::Event scenic_event;
  scenic_event.set_unhandled(std::move(unhandled_command));
  buffered_events_.push_back(std::move(scenic_event));
}

void Session::EventAndErrorReporter::EnqueueEvent(fuchsia::ui::input::InputEvent event) {
  if (!session_)
    return;

  TRACE_DURATION("gfx", "scenic_impl::Session::EventAndErrorReporter::EnqueueEvent", "event_type",
                 "input::InputEvent");
  // Send input event immediately.
  fuchsia::ui::scenic::Event scenic_event;
  scenic_event.set_input(std::move(event));

  FilterRedundantGfxEvents();
  buffered_events_.push_back(std::move(scenic_event));
  FlushEvents();
}

void Session::EventAndErrorReporter::FilterRedundantGfxEvents() {
  if (buffered_events_.empty())
    return;

  struct EventCounts {
    uint32_t view_attached_to_scene = 0;
    uint32_t view_detached_from_scene = 0;
  };
  std::map</*view_id=*/uint32_t, EventCounts> event_counts;
  for (const auto& event : buffered_events_) {
    if (event.is_gfx()) {
      switch (event.gfx().Which()) {
        case fuchsia::ui::gfx::Event::kViewAttachedToScene:
          event_counts[event.gfx().view_attached_to_scene().view_id].view_attached_to_scene++;
          break;
        case fuchsia::ui::gfx::Event::kViewDetachedFromScene:
          event_counts[event.gfx().view_detached_from_scene().view_id].view_detached_from_scene++;
          break;
        default:
          break;
      }
    }
  }
  if (event_counts.empty())
    return;

  auto is_view_event = [](uint32_t view_id, const fuchsia::ui::scenic::Event& event) {
    return event.is_gfx() && ((event.gfx().is_view_detached_from_scene() &&
                               (view_id == event.gfx().view_detached_from_scene().view_id)) ||
                              (event.gfx().is_view_attached_to_scene() &&
                               (view_id == event.gfx().view_attached_to_scene().view_id)));
  };
  for (auto [view_id, event_count] : event_counts) {
    auto matching_view_event = std::bind(is_view_event, view_id, std::placeholders::_1);
    // We expect that multiple attach or detach events aren't fired in a row. Then, remove all
    // attach/detach events if we have balanced counts. Otherwise, remove all except last.
    if (event_count.view_attached_to_scene == event_count.view_detached_from_scene) {
      buffered_events_.erase(
          std::remove_if(buffered_events_.begin(), buffered_events_.end(), matching_view_event),
          buffered_events_.end());
    } else if (event_count.view_attached_to_scene && event_count.view_detached_from_scene) {
      auto last_event =
          std::find_if(buffered_events_.rbegin(), buffered_events_.rend(), matching_view_event);
      buffered_events_.erase(
          buffered_events_.rend().base(),
          std::remove_if(std::next(last_event), buffered_events_.rend(), matching_view_event)
              .base());
    }
  }
}

void Session::EventAndErrorReporter::FlushEvents() {
  if (!session_)
    return;

  TRACE_DURATION("gfx", "scenic_impl::Session::EventAndErrorReporter::FlushEvents");
  if (!buffered_events_.empty()) {
    if (session_->listener_) {
      session_->listener_->OnScenicEvent(std::move(buffered_events_));
    } else if (event_callback_) {
      // Only use the callback if there is no listener.  It is difficult to do
      // better because we std::move the argument into OnScenicEvent().
      for (auto& evt : buffered_events_) {
        event_callback_(std::move(evt));
      }
    }
    buffered_events_.clear();
  }
}

void Session::EventAndErrorReporter::ReportError(syslog::LogSeverity severity,
                                                 std::string error_string) {
  // TODO(fxbug.dev/24465): Come up with a better solution to avoid children
  // calling into us during destruction.
  if (!session_) {
    FX_LOGS(ERROR) << "Reporting Scenic Session error after session destroyed: " << error_string;
    return;
  }

  TRACE_DURATION("gfx", "scenic_impl::Session::EventAndErrorReporter::ReportError");

  switch (severity) {
    case syslog::LOG_INFO:
      FX_LOGS(INFO) << error_string;
      return;
    case syslog::LOG_WARNING:
      FX_LOGS(WARNING) << error_string;
      return;
    case syslog::LOG_ERROR:
      FX_LOGS(WARNING) << "Scenic session error (session_id: " << session_->id()
                       << "): " << error_string;

      if (error_callback_) {
        error_callback_(error_string);
      }

      if (session_->listener_) {
        session_->listener_->OnScenicError(std::move(error_string));
      }
      return;
    case syslog::LOG_FATAL:
      FX_LOGS(FATAL) << error_string;
      return;
    default:
      // Invalid severity.
      FX_DCHECK(false);
  }
}

}  // namespace scenic_impl
