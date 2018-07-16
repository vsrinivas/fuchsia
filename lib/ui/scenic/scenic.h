// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_SCENIC_SCENIC_H_
#define GARNET_LIB_UI_SCENIC_SCENIC_H_

#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <lib/fit/function.h>
#include <set>

#include "garnet/lib/ui/scenic/session.h"
#include "garnet/lib/ui/scenic/system.h"
#include "lib/fidl/cpp/binding_set.h"
#include "lib/fxl/macros.h"

namespace scenic {

class Clock;

// A Scenic instance has two main areas of responsibility:
//   - manage Session lifecycles
//   - provide a host environment for Services
class Scenic : public fuchsia::ui::scenic::Scenic {
 public:
  explicit Scenic(component::StartupContext* app_context,
                  fit::closure quit_callback);
  ~Scenic();

  // Create and register a new system of the specified type.  At most one System
  // with a given TypeId may be registered.
  template <typename SystemT, typename... Args>
  SystemT* RegisterSystem(Args... args);

  // Called by Session when it needs to close itself.
  void CloseSession(Session* session);

  // |fuchsia::ui::scenic::Scenic|
  void CreateSession(
      ::fidl::InterfaceRequest<fuchsia::ui::scenic::Session> session,
      ::fidl::InterfaceHandle<fuchsia::ui::scenic::SessionListener> listener)
      override;

  component::StartupContext* app_context() const { return app_context_; }

  size_t num_sessions() { return session_bindings_.size(); }

 private:
  component::StartupContext* const app_context_;
  fit::closure quit_callback_;

  fidl::BindingSet<fuchsia::ui::scenic::Session, std::unique_ptr<Session>>
      session_bindings_;
  fidl::BindingSet<fuchsia::ui::scenic::Scenic> scenic_bindings_;

  // Registered systems, indexed by their TypeId. These slots could be null,
  // indicating the System is not available or supported.
  std::array<std::unique_ptr<System>, System::TypeId::kMaxSystems> systems_;

  // List of systems that are waiting to be initialized; we can't create
  // sessions until this is empty.
  std::set<System*> uninitialized_systems_;

  // Closures that will be run when all systems are initialized.
  std::vector<fit::closure> run_after_all_systems_initialized_;

  void CreateSessionImmediately(
      ::fidl::InterfaceRequest<fuchsia::ui::scenic::Session> session_request,
      ::fidl::InterfaceHandle<fuchsia::ui::scenic::SessionListener> listener);

  // If a System is not initially initialized, this method will be called when
  // it is ready.
  void OnSystemInitialized(System* system);

  void GetDisplayInfo(
      fuchsia::ui::scenic::Scenic::GetDisplayInfoCallback callback) override;
  void TakeScreenshot(
      fuchsia::ui::scenic::Scenic::TakeScreenshotCallback callback) override;

  void GetDisplayOwnershipEvent(
      fuchsia::ui::scenic::Scenic::GetDisplayOwnershipEventCallback callback) override;

  size_t next_session_id_ = 1;

  FXL_DISALLOW_COPY_AND_ASSIGN(Scenic);
};

template <typename SystemT, typename... Args>
SystemT* Scenic::RegisterSystem(Args... args) {
  FXL_DCHECK(systems_[SystemT::kTypeId] == nullptr)
      << "System of type: " << SystemT::kTypeId << "was already registered.";

  SystemT* system =
      new SystemT(SystemContext(app_context_, quit_callback_.share()), args...);
  systems_[SystemT::kTypeId] = std::unique_ptr<System>(system);

  // Listen for System to be initialized if it isn't already.
  if (!system->initialized()) {
    uninitialized_systems_.insert(system);
    system->set_on_initialized_callback(
        [this](System* system) { Scenic::OnSystemInitialized(system); });
  }
  return system;
}

}  // namespace scenic

#endif  // GARNET_LIB_UI_SCENIC_SCENIC_H_
