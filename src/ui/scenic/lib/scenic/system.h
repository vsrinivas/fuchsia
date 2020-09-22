// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_SCENIC_SYSTEM_H_
#define SRC_UI_SCENIC_LIB_SCENIC_SYSTEM_H_

// TODO(fxbug.dev/23687): Don't support GetDisplayInfo in scenic fidl API.
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <lib/fit/function.h>

#include <unordered_map>

#include "lib/inspect/cpp/inspect.h"
#include "src/lib/fxl/macros.h"
#include "src/lib/fxl/memory/ref_counted.h"
#include "src/ui/scenic/lib/scenic/command_dispatcher.h"
#include "src/ui/scenic/lib/scenic/event_reporter.h"
#include "src/ui/scenic/lib/scenic/util/error_reporter.h"
#include "src/ui/scenic/lib/scheduling/frame_scheduler.h"
#include "src/ui/scenic/lib/scheduling/id.h"

namespace sys {
class ComponentContext;
}  // namespace sys

namespace scenic_impl {

class Clock;

// Provides the capabilities that a System needs to do its job, without directly
// exposing the system's host (typically a Scenic, except for testing).
class SystemContext final {
 public:
  explicit SystemContext(sys::ComponentContext* app_context, inspect::Node inspect_object,
                         fit::closure quit_callback);
  SystemContext(SystemContext&& context);

  sys::ComponentContext* app_context() const { return app_context_; }
  inspect::Node* inspect_node() { return &inspect_node_; }

  // Calls quit on the associated message loop.
  void Quit() { quit_callback_(); }

 private:
  sys::ComponentContext* const app_context_;
  fit::closure quit_callback_;
  inspect::Node inspect_node_;
};

// Systems are a composable way to add functionality to Scenic. A System creates
// CommandDispatcher objects, which handle a subset of the Commands that a
// Scenic Session can support. A Scenic Session creates multiple
// CommandDispatchers, one per unique System, which handle different subsets of
// Commands.
//
// Systems are not expected to be thread-safe; they are only created, used, and
// destroyed on the main Scenic thread.
class System {
 public:
  enum TypeId {
    kGfx = 0,
    kSketchy = 1,
    kReserved = 2,
    kInput = 3,
    kA11yInput = 4,
    kDummySystem = 5,
    kMaxSystems = 6,
    kInvalid = kMaxSystems,
  };

  explicit System(SystemContext context);
  virtual ~System();

  virtual CommandDispatcherUniquePtr CreateCommandDispatcher(
      scheduling::SessionId session_id, std::shared_ptr<EventReporter> event_reporter,
      std::shared_ptr<ErrorReporter> error_reporter) = 0;

  // Performs updates up to the corresponding PresentId for Sessions managed by this System.
  // Mirrors the functionality of scheduling::SessionUpdater::UpdateSessions.
  virtual scheduling::SessionUpdater::UpdateResults UpdateSessions(
      const std::unordered_map<scheduling::SessionId, scheduling::PresentId>& sessions_to_update,
      uint64_t frame_trace_id) {
    return {};
  };

  SystemContext* context() { return &context_; }

 private:
  SystemContext context_;

  FXL_DISALLOW_COPY_AND_ASSIGN(System);
};

// Return the system type that knows how to handle the specified command.
// Used by Session to choose a CommandDispatcher.
inline System::TypeId SystemTypeForCmd(const fuchsia::ui::scenic::Command& command) {
  switch (command.Which()) {
    case fuchsia::ui::scenic::Command::Tag::kGfx:
      return System::TypeId::kGfx;
    case fuchsia::ui::scenic::Command::Tag::kInput:
      return System::TypeId::kInput;
    default:
      return System::TypeId::kInvalid;
  }
}

}  // namespace scenic_impl

#endif  // SRC_UI_SCENIC_LIB_SCENIC_SYSTEM_H_
