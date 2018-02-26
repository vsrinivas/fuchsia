// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_MOZART_MOZART_H_
#define GARNET_LIB_UI_MOZART_MOZART_H_

#include "garnet/lib/ui/mozart/session.h"
#include "garnet/lib/ui/mozart/system.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/fxl/functional/closure.h"
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
  SystemT* RegisterSystem(Args... args);

  // Called by Session when it needs to close itself.
  void CloseSession(Session* session);

  // |ui_mozart::Mozart|
  void CreateSession(
      ::f1dl::InterfaceRequest<ui_mozart::Session> session,
      ::f1dl::InterfaceHandle<ui_mozart::SessionListener> listener) override;

  app::ApplicationContext* app_context() const { return app_context_; }
  fxl::TaskRunner* task_runner() const { return task_runner_; }
  Clock* clock() const { return clock_; }

  size_t num_sessions() { return session_bindings_.size(); }

 private:
  app::ApplicationContext* const app_context_;
  fxl::TaskRunner* const task_runner_;
  Clock* clock_;

  f1dl::BindingSet<ui_mozart::Session, std::unique_ptr<Session>>
      session_bindings_;
  f1dl::BindingSet<ui_mozart::Mozart> mozart_bindings_;

  // Registered systems, indexed by their TypeId. These slots could be null,
  // indicating the System is not available or supported.
  std::array<std::unique_ptr<System>, System::TypeId::kMaxSystems> systems_;

  // List of systems that are waiting to be initialized; we can't create
  // sessions until this is empty.
  std::set<System*> uninitialized_systems_;

  // Closures that will be run when all systems are initialized.
  std::vector<fxl::Closure> run_after_all_systems_initialized_;

  void CreateSessionImmediately(
      ::f1dl::InterfaceRequest<ui_mozart::Session> session_request,
      ::f1dl::InterfaceHandle<ui_mozart::SessionListener> listener);

  // If a System is not initially initialized, this method will be called when
  // it is ready.
  void OnSystemInitialized(System* system);

  void GetDisplayInfo(
      const ui_mozart::Mozart::GetDisplayInfoCallback& callback) override;

  size_t next_session_id_ = 1;

  FXL_DISALLOW_COPY_AND_ASSIGN(Mozart);
};

template <typename SystemT, typename... Args>
SystemT* Mozart::RegisterSystem(Args... args) {
  FXL_DCHECK(systems_[SystemT::kTypeId] == nullptr)
      << "System of type: " << SystemT::kTypeId << "was already registered.";

  SystemT* system =
      new SystemT(SystemContext(app_context_, task_runner_, clock_), args...);
  systems_[SystemT::kTypeId] = std::unique_ptr<System>(system);

  // Listen for System to be initialized if it isn't already.
  if (!system->initialized()) {
    uninitialized_systems_.insert(system);
    system->set_on_initialized_callback(
        [this](System* system) { Mozart::OnSystemInitialized(system); });
  }
  return system;
}

}  // namespace mz

#endif  // GARNET_LIB_UI_MOZART_MOZART_H_
