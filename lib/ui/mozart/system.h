// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_MOZART_SYSTEM_H_
#define GARNET_LIB_UI_MOZART_SYSTEM_H_

#include "garnet/lib/ui/mozart/command_dispatcher.h"
#include "lib/fxl/memory/ref_counted.h"

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

class System : public fxl::RefCountedThreadSafe<System> {
 public:
  enum TypeId {
    kScenic = 0,
    kDummySystem = 1,
    kMaxSystems = 2,
  };

  explicit System(SystemContext context);
  virtual ~System();

  virtual std::unique_ptr<CommandDispatcher> CreateCommandDispatcher(
      CommandDispatcherContext context) = 0;

  SystemContext* context() { return &context_; }

 private:
  SystemContext context_;

  FXL_DISALLOW_COPY_AND_ASSIGN(System);
};

}  // namespace mz

#endif  // GARNET_LIB_UI_MOZART_SYSTEM_H_
