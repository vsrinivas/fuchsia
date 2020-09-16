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
#include "src/ui/lib/escher/flib/fence_queue.h"
#include "src/ui/scenic/lib/gfx/engine/session.h"
#include "src/ui/scenic/lib/scenic/event_reporter.h"
#include "src/ui/scenic/lib/scenic/forward_declarations.h"
#include "src/ui/scenic/lib/scenic/scenic.h"
#include "src/ui/scenic/lib/scenic/system.h"
#include "src/ui/scenic/lib/scenic/util/error_reporter.h"
#include "src/ui/scenic/lib/scheduling/frame_scheduler.h"
#include "src/ui/scenic/lib/scheduling/id.h"
#include "src/ui/scenic/lib/scheduling/present1_helper.h"
#include "src/ui/scenic/lib/scheduling/present2_helper.h"

namespace scenic_impl {

class Session final : public fuchsia::ui::scenic::Session {
 public:
  Session(SessionId id, fidl::InterfaceRequest<fuchsia::ui::scenic::Session> session_request,
          fidl::InterfaceHandle<fuchsia::ui::scenic::SessionListener> listener,
          std::function<void()> destroy_session_function);
  ~Session() override;

  void SetCommandDispatchers(
      std::unordered_map<System::TypeId, CommandDispatcherUniquePtr> dispatchers);

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

  // |fuchsia::ui::scenic::Session|
  void RegisterBufferCollection(
      uint32_t buffer_collection_id,
      fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token) override;

  // |fuchsia::ui::scenic::Session|
  void DeregisterBufferCollection(uint32_t buffer_collection_id) override;

  void SetFrameScheduler(const std::shared_ptr<scheduling::FrameScheduler>& frame_scheduler);

  void OnPresented(const std::map<scheduling::PresentId, zx::time>& latched_times,
                   scheduling::PresentTimestamps present_times);

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
    void ReportError(syslog::LogSeverity severity, std::string error_string) override;

    // Called when the owning session is destroyed.
    void Reset();

    void set_event_callback(fit::function<void(fuchsia::ui::scenic::Event)> callback) {
      event_callback_ = std::move(callback);
    }

    // For tests.  Called by ReportError().
    void set_error_callback(fit::function<void(std::string)> callback) {
      error_callback_ = std::move(callback);
    }

    // Post an asynchronous task to call FlushEvents.
    void PostFlushTask();

   private:
    // Parses |buffered_gfx_events_| to check if there is anything queued that contradicts, i.e.
    // ViewAttachedToSceneEvent and ViewDetachedFromSceneEvent pairs. If there is a contradiction,
    // removes the contradicting events.
    void FilterRedundantGfxEvents();
    void FlushEvents();

    Session* session_ = nullptr;

    // Callbacks for testing.
    fit::function<void(fuchsia::ui::scenic::Event)> event_callback_;
    fit::function<void(std::string)> error_callback_;

    // Holds events from EnqueueEvent() until they are flushed by FlushEvents().
    std::vector<fuchsia::ui::scenic::Event> buffered_events_;

    fxl::WeakPtrFactory<EventAndErrorReporter> weak_factory_;
  };

  // Helper method to schedule Present1 and Present2 calls.
  void SchedulePresentRequest(scheduling::PresentId present_id,
                              zx::time requested_presentation_time,
                              std::vector<zx::event> acquire_fences);

  // Gets the future presentation times from the frame scheduler (indirectly),
  // and invokes |callback|.
  void InvokeFuturePresentationTimesCallback(zx_duration_t requested_prediction_span,
                                             RequestPresentationTimesCallback callback);

  const SessionId id_;
  fidl::InterfacePtr<fuchsia::ui::scenic::SessionListener> listener_;

  std::unordered_map<System::TypeId, CommandDispatcherUniquePtr> dispatchers_;

  std::weak_ptr<scheduling::FrameScheduler> frame_scheduler_;

  std::vector<fuchsia::ui::scenic::Command> commands_pending_present_;

  std::variant<std::monostate, scheduling::Present1Helper, scheduling::Present2Helper>
      present_helper_;

  zx::time last_scheduled_presentation_time_ = zx::time(0);

  int64_t num_presents_allowed_ = scheduling::FrameScheduler::kMaxPresentsInFlight;

  // A flow event trace id for following |Session::Present| calls from client
  // to scenic.  This will be incremented each |Session::Present| call.  By
  // convention, the scenic implementation side will also contain its own
  // trace id that begins at 0, and is incremented each |Session::Present|
  // call.
  uint64_t next_present_trace_id_ = 0;

  std::shared_ptr<EventAndErrorReporter> reporter_;

  fidl::Binding<fuchsia::ui::scenic::Session> binding_;

  // Function to kill this session so that it is properly cleaned up.
  std::function<void()> destroy_session_func_;

  std::shared_ptr<escher::FenceQueue> fence_queue_ = std::make_shared<escher::FenceQueue>();

  fxl::WeakPtrFactory<Session> weak_factory_;

  FXL_DISALLOW_COPY_ASSIGN_AND_MOVE(Session);
};

}  // namespace scenic_impl

#endif  // SRC_UI_SCENIC_LIB_SCENIC_SESSION_H_
