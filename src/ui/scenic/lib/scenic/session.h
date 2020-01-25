// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_SCENIC_SESSION_H_
#define SRC_UI_SCENIC_LIB_SCENIC_SESSION_H_

#include <fuchsia/ui/gfx/cpp/fidl.h>
#include <lib/fit/function.h>

#include <array>
#include <memory>
#include <string>
#include <variant>

#include "src/lib/fxl/macros.h"
#include "src/lib/fxl/memory/weak_ptr.h"
#include "src/ui/lib/escher/flib/fence_set_listener.h"
#include "src/ui/scenic/lib/gfx/engine/session.h"
#include "src/ui/scenic/lib/scenic/event_reporter.h"
#include "src/ui/scenic/lib/scenic/forward_declarations.h"
#include "src/ui/scenic/lib/scenic/scenic.h"
#include "src/ui/scenic/lib/scenic/system.h"
#include "src/ui/scenic/lib/scenic/util/error_reporter.h"
#include "src/ui/scenic/lib/scheduling/frame_scheduler.h"
#include "src/ui/scenic/lib/scheduling/id.h"

namespace scenic_impl {

class Scenic;

class Session final : public fuchsia::ui::scenic::Session {
 public:
  Session(SessionId id, fidl::InterfaceRequest<fuchsia::ui::scenic::Session> session_request,
          fidl::InterfaceHandle<fuchsia::ui::scenic::SessionListener> listener,
          std::function<void()> destroy_session_function);
  ~Session() override;

  void SetCommandDispatchers(
      std::array<CommandDispatcherUniquePtr, System::TypeId::kMaxSystems> dispatchers);

  // |fuchsia::ui::scenic::Session|
  void Enqueue(std::vector<fuchsia::ui::scenic::Command> cmds) override;

  // |fuchsia::ui::scenic::Session|
  void Present(uint64_t presentation_time, std::vector<zx::event> acquire_fences,
               std::vector<zx::event> release_fences,
               scheduling::OnPresentedCallback callback) override;

  // |fuchsia::ui::scenic::Session|
  void Present2(fuchsia::ui::scenic::Present2Args args, Present2Callback callback) override;

  // |fuchsia::ui::scenic::Session|
  void RequestPresentationTimes(zx_duration_t requested_prediction_span,
                                RequestPresentationTimesCallback callback) override;

  // |fuchsia::ui::scenic::Session|
  void SetDebugName(std::string debug_name) override;

  void SetFrameScheduler(const std::shared_ptr<scheduling::FrameScheduler>& frame_scheduler);

  SessionId id() const { return id_; }

  std::shared_ptr<ErrorReporter> error_reporter() const { return reporter_; }
  std::shared_ptr<EventReporter> event_reporter() const { return reporter_; }

  // For tests.  See FlushEvents() below.
  void set_event_callback(fit::function<void(fuchsia::ui::scenic::Event)> callback) {
    reporter_->set_event_callback(std::move(callback));
  }

  // For tests.  Called by ReportError().
  void set_error_callback(fit::function<void(std::string)> callback) {
    reporter_->set_error_callback(std::move(callback));
  }

  bool is_bound() { return binding_.is_bound(); }

  void set_binding_error_handler(fit::function<void(zx_status_t)> error_handler);

 private:
  // Helper class which manages the reporting of events and errors to Scenic clients.
  // NOTE: this object is not only reffed by the owning Session; it is also reffed by
  // shared_ptr<EventReporter/ErrorReporter> that are obtained via CommandDispatcherContext.
  // Therefore, the owning Session cannot be strongly reffed by this, or else a reference-cycle
  // would result.
  class EventAndErrorReporter : public EventReporter, public ErrorReporter {
   public:
    explicit EventAndErrorReporter(Session* session);
    virtual ~EventAndErrorReporter() = default;

    // |EventReporter|
    // Enqueues the gfx/cmd event and schedules call to FlushEvents().
    void EnqueueEvent(fuchsia::ui::gfx::Event event) override;
    void EnqueueEvent(fuchsia::ui::scenic::Command event) override;

    // |EventReporter|
    // Enqueues the input event and immediately calls FlushEvents().
    void EnqueueEvent(fuchsia::ui::input::InputEvent event) override;

    // |EventReporter|
    EventReporterWeakPtr GetWeakPtr() override { return weak_factory_.GetWeakPtr(); }

    // |ErrorReporter|
    // Customize behavior of ErrorReporter::ReportError().
    void ReportError(fxl::LogSeverity severity, std::string error_string) override;

    // Called when the owning session is destroyed.
    void Reset();

    void set_event_callback(fit::function<void(fuchsia::ui::scenic::Event)> callback) {
      event_callback_ = std::move(callback);
    }

    // For tests.  Called by ReportError().
    void set_error_callback(fit::function<void(std::string)> callback) {
      error_callback_ = std::move(callback);
    }

    void FlushEvents();

    // Post an asynchronous task to call FlushEvents.
    void PostFlushTask();

   private:
    Session* session_ = nullptr;

    // Callbacks for testing.
    fit::function<void(fuchsia::ui::scenic::Event)> event_callback_;
    fit::function<void(std::string)> error_callback_;

    // Holds events from EnqueueEvent() until they are flushed by FlushEvents().
    std::vector<fuchsia::ui::scenic::Event> buffered_events_;

    fxl::WeakPtrFactory<EventAndErrorReporter> weak_factory_;
  };

  // TODO(44000): Remove when Present1 is removed.
  enum PresentType { UNSET = 0, PRESENT1 = 1, PRESENT2 = 2 };

  // Used to store the data associated with a Present call until all its acquire fences have been
  // reached.
  struct PresentRequest {
    uint64_t present_id;
    zx::time requested_presentation_time;
    std::vector<zx::event> acquire_fences;
    std::vector<zx::event> release_fences;
    std::vector<fuchsia::ui::scenic::Command> commands;
    // Holds either Present1's |fuchsia::ui::scenic::PresentCallback| or Present2's |Present2Info|.
    std::variant<scheduling::OnPresentedCallback, scheduling::Present2Info> present_information;
  };

  // Verifies the client is only using Present() or Present2(). It is an error for a client
  // to use both calls in the same Session. If both are called, the session should be shut down.
  // TODO(44000) remove check when Present() is deprecated and removed from the fidl.
  bool VerifyPresentType(PresentType present_type);

  // Helper method to schedule Present1 and Present2 calls.
  void SchedulePresentRequest(
      zx::time requested_presentation_time, std::vector<zx::event> acquire_fences,
      std::vector<zx::event> release_fences,
      std::variant<scheduling::OnPresentedCallback, scheduling::Present2Info> presentation_info);

  // Waits for the acquire fences for each Present call and schedules them in turn as the fences are
  // signalled.
  void ProcessQueuedPresents();

  // Schedules the next Present on the queue.
  void ScheduleNextPresent();

  // Flush any/all events that were enqueued via EnqueueEvent(), sending them
  // to |listener_|.  If |listener_| is null but |event_callback_| isn't, then
  // invoke the callback for each event.
  void FlushEvents() { reporter_->FlushEvents(); }

  // Gets the future presentation times from the frame scheduler (indirectly),
  // and invokes |callback|.
  void InvokeFuturePresentationTimesCallback(zx_duration_t requested_prediction_span,
                                             RequestPresentationTimesCallback callback);

  gfx::Session* GetGfxSession();

  const SessionId id_;
  fidl::InterfacePtr<fuchsia::ui::scenic::SessionListener> listener_;

  std::array<CommandDispatcherUniquePtr, System::TypeId::kMaxSystems> dispatchers_;

  std::weak_ptr<scheduling::FrameScheduler> frame_scheduler_;

  // The unique ID used for a given present.
  scheduling::PresentId next_present_id_ = 0;
  std::deque<PresentRequest> presents_to_schedule_;

  std::vector<fuchsia::ui::scenic::Command> commands_pending_present_;

  zx::time last_scheduled_presentation_time_ = zx::time(0);

  int64_t num_presents_allowed_ = scheduling::FrameScheduler::kMaxPresentsInFlight;

  // Tracks if the client is using Present1 or Present2 while the Present1 API is being deprecated.
  // No client should use both Present commands in the same session.
  PresentType present_type_ = PresentType::UNSET;

  // A flow event trace id for following |Session::Present| calls from client
  // to scenic.  This will be incremented each |Session::Present| call.  By
  // convention, the scenic implementation side will also contain its own
  // trace id that begins at 0, and is incremented each |Session::Present|
  // call.
  uint64_t next_present_trace_id_ = 0;

  // Flow event trace ids for the Present acquire fence queue. Since they're handled in order they
  // can simply be incremented as each one is handled.
  uint64_t queue_processing_id_begin_ = 0;
  uint64_t queue_processing_id_end_ = 0;

  std::shared_ptr<EventAndErrorReporter> reporter_;

  fidl::Binding<fuchsia::ui::scenic::Session> binding_;

  // Function to kill this session so that it is properly cleaned up.
  std::function<void()> destroy_session_func_;

  std::unique_ptr<escher::FenceSetListener> fence_listener_;

  fxl::WeakPtrFactory<Session> weak_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Session);
};

}  // namespace scenic_impl

#endif  // SRC_UI_SCENIC_LIB_SCENIC_SESSION_H_
