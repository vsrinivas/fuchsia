// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_SCENIC_SYSTEM_H_
#define GARNET_LIB_UI_SCENIC_SYSTEM_H_

// TODO(MZ-453): Don't support GetDisplayInfo in scenic fidl API.
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <lib/fit/function.h>

#include "garnet/lib/ui/scenic/command_dispatcher.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/memory/ref_counted.h"

namespace component {
class StartupContext;
}  // namespace component

namespace scenic {

class Clock;
class Session;

// Provides the capabilities that a System needs to do its job, without directly
// exposing the system's host (typically a Scenic, except for testing).
class SystemContext final {
 public:
  explicit SystemContext(component::StartupContext* app_context,
                         fit::closure quit_callback);
  SystemContext(SystemContext&& context);

  component::StartupContext* app_context() const { return app_context_; }

  // Calls quit on the associated message loop.
  void Quit() { quit_callback_(); }

 private:
  component::StartupContext* const app_context_;
  fit::closure quit_callback_;
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
    kViews = 1,
    kSketchy = 2,
    kVectorial = 3,
    kDummySystem = 4,
    kMaxSystems = 5,
    kInvalid = kMaxSystems,
  };

  using OnInitializedCallback = fit::function<void(System* system)>;

  // If |initialized_after_construction| is false, the System must call
  // SetToInitialized() after initialization is complete.
  explicit System(SystemContext context,
                  bool initialized_after_construction = true);
  virtual ~System();

  virtual std::unique_ptr<CommandDispatcher> CreateCommandDispatcher(
      CommandDispatcherContext context) = 0;

  SystemContext* context() { return &context_; }

  bool initialized() { return initialized_; };

  void set_on_initialized_callback(OnInitializedCallback callback) {
    FXL_DCHECK(!on_initialized_callback_);
    on_initialized_callback_ = std::move(callback);
  }

 protected:
  bool initialized_ = true;

  // Marks this system as initialized and invokes callback if it's set.
  void SetToInitialized();

 private:
  OnInitializedCallback on_initialized_callback_;

  SystemContext context_;

  FXL_DISALLOW_COPY_AND_ASSIGN(System);
};

// TODO(MZ-452): Remove when we get rid of Scenic.GetDisplayInfo().
class TempSystemDelegate : public System {
 public:
  explicit TempSystemDelegate(SystemContext context,
                              bool initialized_after_construction);
  virtual void GetDisplayInfo(
      fuchsia::ui::scenic::Scenic::GetDisplayInfoCallback callback) = 0;
  virtual void TakeScreenshot(
      fuchsia::ui::scenic::Scenic::TakeScreenshotCallback callback) = 0;
  virtual void GetDisplayOwnershipEvent(
      fuchsia::ui::scenic::Scenic::GetDisplayOwnershipEventCallback callback) = 0;
};

// Return the system type that knows how to handle the specified command.
// Used by Session to choose a CommandDispatcher.
inline System::TypeId SystemTypeForCmd(
    const fuchsia::ui::scenic::Command& command) {
  switch (command.Which()) {
    case fuchsia::ui::scenic::Command::Tag::kGfx:
      return System::TypeId::kGfx;
    case fuchsia::ui::scenic::Command::Tag::kViews:
      return System::TypeId::kViews;
    case fuchsia::ui::scenic::Command::Tag::kVectorial:
      return System::TypeId::kVectorial;
    default:
      return System::TypeId::kInvalid;
  }
}

}  // namespace scenic

#endif  // GARNET_LIB_UI_SCENIC_SYSTEM_H_
