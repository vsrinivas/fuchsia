// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_SCENIC_SYSTEM_H_
#define GARNET_LIB_UI_SCENIC_SYSTEM_H_

#include "garnet/lib/ui/scenic/command_dispatcher.h"
#include "lib/fxl/memory/ref_counted.h"
// TODO(MZ-453): Don't support GetDisplayInfo in scenic fidl API.
#include "lib/ui/scenic/fidl/scenic.fidl.h"

namespace app {
class ApplicationContext;
}  // namespace app

namespace fxl {
class TaskRunner;
}  // namespace fxl

namespace scenic {

class Clock;
class Session;

// Provides the capabilities that a System needs to do its job, without directly
// exposing the system's host (typically a Scenic, except for testing).
class SystemContext final {
 public:
  explicit SystemContext(app::ApplicationContext* app_context,
                         fxl::TaskRunner* task_runner,
                         Clock* clock);
  SystemContext(SystemContext&& context);

  app::ApplicationContext* app_context() const { return app_context_; }
  fxl::TaskRunner* task_runner() const { return task_runner_; }
  Clock* clock() const { return clock_; }

 private:
  app::ApplicationContext* app_context_;
  fxl::TaskRunner* task_runner_;
  Clock* clock_;
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
    kDummySystem = 2,
    kMaxSystems = 3,
  };

  using OnInitializedCallback = std::function<void(System* system)>;

  // If |initialized_after_construction| is false, the System must call
  // SetToInitialized() after initialization is complete.
  explicit System(SystemContext context,
                  bool initialized_after_construction = true);
  virtual ~System();

  virtual std::unique_ptr<CommandDispatcher> CreateCommandDispatcher(
      CommandDispatcherContext context) = 0;

  SystemContext* context() { return &context_; }
  fxl::TaskRunner* task_runner() { return context_.task_runner(); }
  Clock* clock() { return context_.clock(); }

  bool initialized() { return initialized_; };

  void set_on_initialized_callback(OnInitializedCallback callback) {
    FXL_DCHECK(!on_initialized_callback_);
    on_initialized_callback_ = callback;
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
      const ui::Scenic::GetDisplayInfoCallback& callback) = 0;
};

}  // namespace scenic

#endif  // GARNET_LIB_UI_SCENIC_SYSTEM_H_
