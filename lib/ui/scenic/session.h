// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_SCENIC_SESSION_H_
#define GARNET_LIB_UI_SCENIC_SESSION_H_

#include <array>
#include <memory>
#include <string>

#include <fuchsia/ui/gfx/cpp/fidl.h>
#include <lib/fit/function.h>

#include "garnet/lib/ui/scenic/event_reporter.h"
#include "garnet/lib/ui/scenic/forward_declarations.h"
#include "garnet/lib/ui/scenic/scenic.h"
#include "garnet/lib/ui/scenic/system.h"
#include "garnet/lib/ui/scenic/util/error_reporter.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/memory/weak_ptr.h"

namespace scenic {

class CommandDispatcher;
class Scenic;

using SessionId = uint64_t;

class Session final : public fuchsia::ui::scenic::Session,
                      public EventReporter,
                      public ErrorReporter {
 public:
  Session(
      Scenic* owner, SessionId id,
      ::fidl::InterfaceHandle<fuchsia::ui::scenic::SessionListener> listener);
  ~Session() override;

  void SetCommandDispatchers(std::array<std::unique_ptr<CommandDispatcher>,
                                        System::TypeId::kMaxSystems>
                                 dispatchers);

  // |fuchsia::ui::scenic::Session|
  void Enqueue(::fidl::VectorPtr<fuchsia::ui::scenic::Command> cmds) override;

  // |fuchsia::ui::scenic::Session|
  void Present(uint64_t presentation_time,
               ::fidl::VectorPtr<zx::event> acquire_fences,
               ::fidl::VectorPtr<zx::event> release_fences,
               PresentCallback callback) override;

  // |fuchsia::ui::scenic::Session|
  // TODO(MZ-422): Remove this after it's removed from session.fidl.
  void HitTest(uint32_t node_id, ::fuchsia::ui::gfx::vec3 ray_origin,
               ::fuchsia::ui::gfx::vec3 ray_direction,
               HitTestCallback callback) override;

  // |fuchsia::ui::scenic::Session|
  // TODO(MZ-422): Remove this after it's removed from session.fidl.
  void HitTestDeviceRay(::fuchsia::ui::gfx::vec3 ray_origin,
                        ::fuchsia::ui::gfx::vec3 ray_direction,
                        HitTestCallback callback) override;

  // |EventReporter|
  // Enqueues the event and manages scheduling of call to FlushEvents().
  void EnqueueEvent(fuchsia::ui::scenic::Event event) override;

  // |ErrorReporter|
  // Customize behavior of ErrorReporter::ReportError().
  void ReportError(fxl::LogSeverity severity,
                   std::string error_string) override;

  SessionId id() const { return id_; }

  ErrorReporter* error_reporter() { return this; }

  // For tests.  See FlushEvents() below.
  void set_event_callback(
      fit::function<void(fuchsia::ui::scenic::Event)> callback) {
    event_callback_ = std::move(callback);
  }

  // For tests.  Called by ReportError().
  void set_error_callback(fit::function<void(std::string)> callback) {
    error_callback_ = std::move(callback);
  }

 private:
  // Flush any/all events that were enqueued via EnqueueEvent(), sending them
  // to |listener_|.  If |listener_| is null but |event_callback_| isn't, then
  // invoke the callback for each event.
  void FlushEvents();

  Scenic* const scenic_;
  const SessionId id_;
  ::fidl::InterfacePtr<fuchsia::ui::scenic::SessionListener> listener_;

  std::array<std::unique_ptr<CommandDispatcher>, System::TypeId::kMaxSystems>
      dispatchers_;

  // Holds events from EnqueueEvent() until they are flushed by FlushEvents().
  fidl::VectorPtr<fuchsia::ui::scenic::Event> buffered_events_;

  // Callbacks for testing.
  fit::function<void(fuchsia::ui::scenic::Event)> event_callback_;
  fit::function<void(std::string)> error_callback_;

  fxl::WeakPtrFactory<Session> weak_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Session);
};

}  // namespace scenic

#endif  // GARNET_LIB_UI_SCENIC_SESSION_H_
