// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_MOZART_SYSTEM_H_
#define GARNET_LIB_UI_MOZART_SYSTEM_H_

#include "garnet/lib/ui/mozart/command_dispatcher.h"
#include "lib/fxl/memory/ref_counted.h"
// TODO(MZ-453): Don't support GetDisplayInfo in mozart fidl API.
#include "lib/ui/mozart/fidl/mozart.fidl.h"

namespace app {
class ApplicationContext;
}  // namespace app

namespace fxl {
class TaskRunner;
}  // namespace fxl

namespace mz {

class Clock;
class Session;

// Provides the capabilities that a System needs to do its job, without directly
// exposing the system's host (typically a Mozart, except for testing).
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

// Systems are a composable way to add functionality to Mozart. A System creates
// CommandDispatcher objects, which handle a subset of the Commands that a
// Mozart Session can support. A Mozart Session creates multiple
// CommandDispatchers, one per unique System, which handle different subsets of
// Commands.
//
// Systems are not expected to be thread-safe; they are only created, used, and
// destroyed on the main Mozart thread.
class System {
 public:
  enum TypeId {
    kScenic = 0,
    kDummySystem = 1,
    kMaxSystems = 2,
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

// TODO(MZ-452): Remove when we get rid of Mozart.GetDisplayInfo().
class TempSystemDelegate : public System {
 public:
  explicit TempSystemDelegate(SystemContext context,
                              bool initialized_after_construction);
  virtual void GetDisplayInfo(
      const ui_mozart::Mozart::GetDisplayInfoCallback& callback) = 0;
};

}  // namespace mz

#endif  // GARNET_LIB_UI_MOZART_SYSTEM_H_
