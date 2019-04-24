// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_SCENIC_SYSTEM_H_
#define GARNET_LIB_UI_SCENIC_SYSTEM_H_

// TODO(SCN-453): Don't support GetDisplayInfo in scenic fidl API.
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <lib/fit/function.h>
#include <lib/inspect/inspect.h>

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
                         inspect::Object inspect_object,
                         fit::closure quit_callback);
  SystemContext(SystemContext&& context);

  sys::ComponentContext* app_context() const { return app_context_; }
  inspect::Object* inspect_object() { return &inspect_object_; }

  // Calls quit on the associated message loop.
  void Quit() { quit_callback_(); }

 private:
  sys::ComponentContext* const app_context_;
  fit::closure quit_callback_;
  inspect::Object inspect_object_;
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

  using OnInitializedCallback = fit::function<void(System* system)>;

  // If |initialized_after_construction| is false, the System must call
  // SetToInitialized() after initialization is complete.
  explicit System(SystemContext context,
                  bool initialized_after_construction = true);
  virtual ~System();

  virtual CommandDispatcherUniquePtr CreateCommandDispatcher(
      CommandDispatcherContext context) = 0;

  SystemContext* context() { return &context_; }

  bool initialized() { return initialized_; };

  void set_on_initialized_callback(OnInitializedCallback callback) {
    FXL_DCHECK(!on_initialized_callback_);
    on_initialized_callback_ = std::move(callback);
  }

 protected:
  // TODO(SCN-906): Remove/refactor this under-used deferred-init logic.
  bool initialized_ = true;

  // Marks this system as initialized and invokes callback if it's set.
  void SetToInitialized();

 private:
  OnInitializedCallback on_initialized_callback_;

  SystemContext context_;

  FXL_DISALLOW_COPY_AND_ASSIGN(System);
};

// TODO(SCN-452): Remove when we get rid of Scenic.GetDisplayInfo().
class TempSystemDelegate : public System {
 public:
  explicit TempSystemDelegate(SystemContext context,
                              bool initialized_after_construction);
  virtual void GetDisplayInfo(
      fuchsia::ui::scenic::Scenic::GetDisplayInfoCallback callback) = 0;
  virtual void TakeScreenshot(
      fuchsia::ui::scenic::Scenic::TakeScreenshotCallback callback) = 0;
  virtual void GetDisplayOwnershipEvent(
      fuchsia::ui::scenic::Scenic::GetDisplayOwnershipEventCallback
          callback) = 0;
};

// Return the system type that knows how to handle the specified command.
// Used by Session to choose a CommandDispatcher.
inline System::TypeId SystemTypeForCmd(
    const fuchsia::ui::scenic::Command& command) {
  switch (command.Which()) {
    case fuchsia::ui::scenic::Command::Tag::kGfx:
      return System::TypeId::kGfx;
    case fuchsia::ui::scenic::Command::Tag::kInput:
      // TODO(SCN-1124): Provide a way to route input to a11y_input here when
      // applicable.
      return System::TypeId::kInput;
    case fuchsia::ui::scenic::Command::Tag::kVectorial:
      return System::TypeId::kVectorial;
    default:
      return System::TypeId::kInvalid;
  }
}

}  // namespace scenic_impl

#endif  // GARNET_LIB_UI_SCENIC_SYSTEM_H_
