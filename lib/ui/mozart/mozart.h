// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_MOZART_MOZART_H_
#define GARNET_LIB_UI_MOZART_MOZART_H_

#include "garnet/lib/ui/mozart/session.h"
#include "garnet/lib/ui/mozart/system.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/fxl/macros.h"
#include "lib/ui/mozart/fidl/mozart.fidl.h"

namespace mz {

class Clock;

// A Mozart instance has two main areas of responsibility:
//   - manage Session lifecycles
//   - provide a host environment for Services
class Mozart : public ui_mozart::Mozart {
 public:
  Mozart(app::ApplicationContext* app_context,
         fxl::TaskRunner* task_runner,
         Clock* clock);
  ~Mozart();

  // Create and register a new system of the specified type.  At most one System
  // with a given TypeId may be registered.
  template <typename SystemT, typename... Args>
  void RegisterSystem(Args... args);

  // Called by Session when it needs to close itself.
  void CloseSession(Session* session);

  // ui_mozart::Mozart FIDL API.
  void CreateSession(
      ::fidl::InterfaceRequest<ui_mozart::Session> session,
      ::fidl::InterfaceHandle<ui_mozart::SessionListener> listener) override;

  app::ApplicationContext* app_context() const { return app_context_; }
  fxl::TaskRunner* task_runner() const { return task_runner_; }
  Clock* clock() const { return clock_; }

 private:
  app::ApplicationContext* const app_context_;
  fxl::TaskRunner* const task_runner_;
  Clock* clock_;

  fidl::BindingSet<ui_mozart::Session, std::unique_ptr<Session>>
      session_bindings_;

  std::array<std::unique_ptr<System>, System::TypeId::kMaxSystems> systems_;

  size_t next_session_id_ = 1;

  FXL_DISALLOW_COPY_AND_ASSIGN(Mozart);
};

template <typename SystemT, typename... Args>
void Mozart::RegisterSystem(Args... args) {
  FXL_DCHECK(systems_[SystemT::kTypeId] == nullptr)
      << "System of type: " << SystemT::kTypeId << "was already registered.";
  systems_[SystemT::kTypeId] = std::unique_ptr<System>(
      new SystemT(SystemContext(app_context_, task_runner_, clock_), args...));
}

}  // namespace mz

#endif  // GARNET_LIB_UI_MOZART_MOZART_H_
