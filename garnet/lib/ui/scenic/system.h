// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_SCENIC_SYSTEM_H_
#define GARNET_LIB_UI_SCENIC_SYSTEM_H_

// TODO(SCN-453): Don't support GetDisplayInfo in scenic fidl API.
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <lib/fit/function.h>
#include <lib/inspect_deprecated/inspect.h>

#include "garnet/lib/ui/scenic/command_dispatcher.h"
#include "src/lib/fxl/macros.h"
#include "src/lib/fxl/memory/ref_counted.h"

namespace sys {
class ComponentContext;
}  // namespace sys

namespace scenic_impl {

class Clock;
class Session;

// Provides the capabilities that a System needs to do its job, without directly
// exposing the system's host (typically a Scenic, except for testing).
class SystemContext final {
 public:
  explicit SystemContext(sys::ComponentContext* app_context,
                         inspect_deprecated::Node inspect_object, fit::closure quit_callback);
  SystemContext(SystemContext&& context);

  sys::ComponentContext* app_context() const { return app_context_; }
  inspect_deprecated::Node* inspect_node() { return &inspect_node_; }

  // Calls quit on the associated message loop.
  void Quit() { quit_callback_(); }

 private:
  sys::ComponentContext* const app_context_;
  fit::closure quit_callback_;
  inspect_deprecated::Node inspect_node_;
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
    kVectorial = 2,
    kInput = 3,
    kA11yInput = 4,
    kDummySystem = 5,
    kMaxSystems = 6,
    kInvalid = kMaxSystems,
  };

  explicit System(SystemContext context);
  virtual ~System();

  virtual CommandDispatcherUniquePtr CreateCommandDispatcher(CommandDispatcherContext context) = 0;

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
    case fuchsia::ui::scenic::Command::Tag::kVectorial:
      return System::TypeId::kVectorial;
    default:
      return System::TypeId::kInvalid;
  }
}

}  // namespace scenic_impl

#endif  // GARNET_LIB_UI_SCENIC_SYSTEM_H_
