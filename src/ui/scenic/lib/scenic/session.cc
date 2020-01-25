// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/scenic/session.h"

#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/async/time.h>

#include <trace/event.h>

namespace {

#define SESSION_TRACE_ID(session_id, count) (((uint64_t)(session_id) << 32) | (count))

}  // anonymous namespace

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
  FXL_DCHECK(!binding_.channel() || binding_.is_bound());
}

Session::~Session() { reporter_->Reset(); }

void Session::set_binding_error_handler(fit::function<void(zx_status_t)> error_handler) {
  binding_.set_error_handler(std::move(error_handler));
}

void Session::SetFrameScheduler(
    const std::shared_ptr<scheduling::FrameScheduler>& frame_scheduler) {
  FXL_DCHECK(frame_scheduler_.expired()) << "Error: FrameScheduler already set";
  frame_scheduler_ = frame_scheduler;

  // Initialize OnFramePresented callback.
  // Check validity because it's not always set in tests.
  if (frame_scheduler) {
    frame_scheduler->SetOnUpdateFailedCallbackForSession(id_,
                                                         [weak = weak_factory_.GetWeakPtr()]() {
                                                           if (weak) {
                                                             // Called to initiate a session close
                                                             // when an update fails. Requests the
                                                             // destruction of client fidl session
                                                             // from scenic, which then triggers the
                                                             // actual destruction of this object.
                                                             weak->destroy_session_func_();
                                                           }
                                                         });

    frame_scheduler->SetOnFramePresentedCallbackForSession(
        id_,
        [weak = weak_factory_.GetWeakPtr()](fuchsia::scenic::scheduling::FramePresentedInfo info) {
          if (!weak)
            return;
          // Update and set num_presents_allowed before ultimately calling into the client provided
          // callback.
          weak->num_presents_allowed_ += (info.presentation_infos.size());
          FXL_DCHECK(weak->num_presents_allowed_ <=
                     scheduling::FrameScheduler::kMaxPresentsInFlight);
          info.num_presents_allowed = weak->num_presents_allowed_;
          weak->binding_.events().OnFramePresented(std::move(info));
        });
  }
}

void Session::Enqueue(std::vector<fuchsia::ui::scenic::Command> cmds) {
  TRACE_DURATION("gfx", "scenic_impl::Session::Enqueue", "session_id", id(), "num_commands",
                 cmds.size());
  for (auto& cmd : cmds) {
    // TODO(SCN-710): This dispatch is far from optimal in terms of performance.
    // We need to benchmark it to figure out whether it matters.
    System::TypeId type_id = SystemTypeForCmd(cmd);
    auto dispatcher = type_id != System::TypeId::kInvalid ? dispatchers_[type_id].get() : nullptr;
    if (!dispatcher) {
      reporter_->EnqueueEvent(std::move(cmd));
    } else if (type_id == System::TypeId::kInput) {
      dispatcher->DispatchCommand(std::move(cmd));
    } else {
      commands_pending_present_.emplace_back(std::move(cmd));
    }
  }
}

bool Session::VerifyPresentType(PresentType present_type) {
  if (present_type_ == PresentType::UNSET) {
    present_type_ = present_type;
  }
  return present_type_ == present_type;
}

void Session::Present(uint64_t presentation_time, std::vector<zx::event> acquire_fences,
                      std::vector<zx::event> release_fences,
                      scheduling::OnPresentedCallback callback) {
  TRACE_DURATION("gfx", "scenic_impl::Session::Present");

  if (!VerifyPresentType(PresentType::PRESENT1)) {
    reporter_->ERROR() << "Client cannot use Present() and Present2() in the same Session";
    destroy_session_func_();
    return;
  }

  TRACE_FLOW_END("gfx", "Session::Present", next_present_trace_id_);
  next_present_trace_id_++;

  if (--num_presents_allowed_ < 0) {
    reporter_->ERROR() << "Present() called with no more present calls allowed.";
  }

  auto present_callback = [weak = weak_factory_.GetWeakPtr(),
                           callback = std::move(callback)](fuchsia::images::PresentationInfo info) {
    if (!weak)
      return;
    ++(weak->num_presents_allowed_);
    FXL_DCHECK(weak->num_presents_allowed_ <= scheduling::FrameScheduler::kMaxPresentsInFlight);
    callback(info);
  };

  SchedulePresentRequest(zx::time(presentation_time), std::move(acquire_fences),
                         std::move(release_fences), std::move(present_callback));
}

void Session::Present2(fuchsia::ui::scenic::Present2Args args, Present2Callback callback) {
  TRACE_DURATION("gfx", "scenic_impl::Session::Present2");

  if (!VerifyPresentType(PresentType::PRESENT2)) {
    reporter_->ERROR() << "Client cannot use Present() and Present2() in the same Session";
    destroy_session_func_();
    return;
  }

  TRACE_FLOW_END("gfx", "Session::Present", next_present_trace_id_);
  next_present_trace_id_++;

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

  // After decrementing |num_presents_allowed_|, fire the immediate callback.
  InvokeFuturePresentationTimesCallback(args.requested_prediction_span(), std::move(callback));

  // Schedule update: flush commands with present count to track in gfx session
  scheduling::Present2Info present2_info(id_);
  zx::time present_received_time = zx::time(async_now(async_get_default_dispatcher()));
  present2_info.SetPresentReceivedTime(present_received_time);

  SchedulePresentRequest(zx::time(args.requested_presentation_time()),
                         std::move(*args.mutable_acquire_fences()),
                         std::move(*args.mutable_release_fences()), std::move(present2_info));
}

void Session::ProcessQueuedPresents() {
  if (presents_to_schedule_.empty() || fence_listener_) {
    // The queue is either already being processed or there is nothing in the queue to process.
    return;
  }

  // Handle the first present on the queue.
  fence_listener_ = std::make_unique<escher::FenceSetListener>(
      std::move(presents_to_schedule_.front().acquire_fences));
  presents_to_schedule_.front().acquire_fences.clear();
  fence_listener_->WaitReadyAsync([this] {
    TRACE_DURATION("gfx", "scenic_impl::Session::ProcessQueuedPresents");
    TRACE_FLOW_END("gfx", "wait for acquire fences", ++queue_processing_id_end_);

    // Lambda won't fire if the object is destroyed, but the session can be killed inside of
    // SchedulePresent, so we need to guard against that.
    auto weak = weak_factory_.GetWeakPtr();
    ScheduleNextPresent();

    if (weak) {
      // Keep going until all queued presents have been scheduled.
      fence_listener_.reset();
      ProcessQueuedPresents();
    }
  });
}

void Session::SchedulePresentRequest(
    zx::time requested_presentation_time, std::vector<zx::event> acquire_fences,
    std::vector<zx::event> release_fences,
    std::variant<scheduling::OnPresentedCallback, scheduling::Present2Info> presentation_info) {
  const scheduling::PresentId present_id = next_present_id_++;

  {
    // Logic verifying client requests presents in-order.
    zx::time last_scheduled_presentation_time = last_scheduled_presentation_time_;
    if (!presents_to_schedule_.empty()) {
      last_scheduled_presentation_time =
          std::max(last_scheduled_presentation_time,
                   presents_to_schedule_.back().requested_presentation_time);
    }

    if (requested_presentation_time < last_scheduled_presentation_time) {
      reporter_->ERROR() << "scenic_impl::gfx::Session: Present called with out-of-order "
                            "presentation time. "
                         << "requested presentation time=" << requested_presentation_time
                         << ", last scheduled presentation time="
                         << last_scheduled_presentation_time << ".";
      destroy_session_func_();
      return;
    }
  }

  // Push present to the back of the queue of presents.
  PresentRequest request{.present_id = present_id,
                         .requested_presentation_time = requested_presentation_time,
                         .acquire_fences = std::move(acquire_fences),
                         .release_fences = std::move(release_fences),
                         .commands = std::move(commands_pending_present_),
                         .present_information = std::move(presentation_info)};
  presents_to_schedule_.emplace_back(std::move(request));
  commands_pending_present_.clear();

  TRACE_FLOW_BEGIN("gfx", "wait for acquire fences", ++queue_processing_id_begin_);
  ProcessQueuedPresents();
}

void Session::ScheduleNextPresent() {
  FXL_DCHECK(!presents_to_schedule_.empty());

  auto& present_request = presents_to_schedule_.front();
  FXL_DCHECK(present_request.acquire_fences.empty());
  TRACE_DURATION("gfx", "scenic_impl::Session::ScheduleNextPresent", "session_id", id_,
                 "requested time", present_request.requested_presentation_time.get());
  TRACE_FLOW_BEGIN("gfx", "scheduled_update", SESSION_TRACE_ID(id_, present_request.present_id));

  for (auto& cmd : present_request.commands) {
    System::TypeId type_id = SystemTypeForCmd(cmd);
    auto dispatcher = type_id != System::TypeId::kInvalid ? dispatchers_[type_id].get() : nullptr;
    FXL_DCHECK(dispatcher);
    dispatcher->DispatchCommand(std::move(cmd));
  }

  bool present_success = false;
  if (auto present_callback =
          std::get_if<scheduling::OnPresentedCallback>(&present_request.present_information)) {
    // TODO(SCN-469): Move Present logic into Session.
    present_success = GetGfxSession()->ScheduleUpdateForPresent(
        present_request.requested_presentation_time, std::move(present_request.release_fences),
        std::move(*present_callback));
  } else {
    FXL_DCHECK(
        std::holds_alternative<scheduling::Present2Info>(present_request.present_information));
    present_success = GetGfxSession()->ScheduleUpdateForPresent2(
        present_request.requested_presentation_time, std::move(present_request.release_fences),
        scheduling::Present2Info(id_));
  }

  // Pop it off the queue before continuing.
  presents_to_schedule_.pop_front();

  if (!present_success) {
    destroy_session_func_();
    return;
  }
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

  if (auto locked_frame_scheduler = frame_scheduler_.lock()) {
    locked_frame_scheduler->GetFuturePresentationInfos(
        zx::duration(requested_prediction_span),
        [weak = weak_factory_.GetWeakPtr(), callback = std::move(callback)](
            std::vector<fuchsia::scenic::scheduling::PresentationInfo> presentation_infos) {
          callback({std::move(presentation_infos), weak ? weak->num_presents_allowed_ : 0});
        });
  }
}

void Session::SetCommandDispatchers(
    std::array<CommandDispatcherUniquePtr, System::TypeId::kMaxSystems> dispatchers) {
  for (size_t i = 0; i < System::TypeId::kMaxSystems; ++i) {
    dispatchers_[i] = std::move(dispatchers[i]);
  }
}

void Session::SetDebugName(std::string debug_name) {
  TRACE_DURATION("gfx", "scenic_impl::Session::SetDebugName", "debug name", debug_name);
  for (auto& dispatcher : dispatchers_) {
    if (dispatcher)
      dispatcher->SetDebugName(debug_name);
  }
}

Session::EventAndErrorReporter::EventAndErrorReporter(Session* session)
    : session_(session), weak_factory_(this) {
  FXL_DCHECK(session_);
}

void Session::EventAndErrorReporter::Reset() { session_ = nullptr; }

void Session::EventAndErrorReporter::PostFlushTask() {
  FXL_DCHECK(session_);
  TRACE_DURATION("gfx", "scenic_impl::Session::EventAndErrorReporter::PostFlushTask");

  // If this is the first EnqueueEvent() since the last FlushEvent(), post a
  // task to ensure that FlushEvents() is called.
  if (buffered_events_.empty()) {
    async::PostTask(async_get_default_dispatcher(),
                    [shared_this = session_->reporter_] { shared_this->FlushEvents(); });
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
  // Force an immediate flush, preserving event order.
  fuchsia::ui::scenic::Event scenic_event;
  scenic_event.set_input(std::move(event));
  buffered_events_.push_back(std::move(scenic_event));

  FlushEvents();
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

void Session::EventAndErrorReporter::ReportError(fxl::LogSeverity severity,
                                                 std::string error_string) {
  // TODO(SCN-1265): Come up with a better solution to avoid children
  // calling into us during destruction.
  if (!session_) {
    FXL_LOG(ERROR) << "Reporting Scenic Session error after session destroyed: " << error_string;
    return;
  }

  TRACE_DURATION("gfx", "scenic_impl::Session::EventAndErrorReporter::ReportError");

  switch (severity) {
    case fxl::LOG_INFO:
      FXL_LOG(INFO) << error_string;
      return;
    case fxl::LOG_WARNING:
      FXL_LOG(WARNING) << error_string;
      return;
    case fxl::LOG_ERROR:
      FXL_LOG(ERROR) << "Scenic session error (session_id: " << session_->id()
                     << "): " << error_string;

      if (error_callback_) {
        error_callback_(error_string);
      }

      if (session_->listener_) {
        session_->listener_->OnScenicError(std::move(error_string));
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

gfx::Session* Session::GetGfxSession() {
  auto& dispatcher = dispatchers_[System::TypeId::kGfx];
  return dispatcher ? static_cast<gfx::Session*>(dispatcher.get()) : nullptr;
}

}  // namespace scenic_impl
