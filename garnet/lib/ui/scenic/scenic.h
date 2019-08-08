// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_SCENIC_SCENIC_H_
#define GARNET_LIB_UI_SCENIC_SCENIC_H_

#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <lib/async/default.h>
#include <lib/fit/function.h>
#include <lib/inspect_deprecated/inspect.h>

#include <set>

#include "garnet/lib/ui/scenic/session.h"
#include "garnet/lib/ui/scenic/system.h"
#include "lib/fidl/cpp/binding_set.h"
#include "src/lib/fxl/macros.h"

namespace scenic_impl {

class Clock;

// TODO(SCN-452): Remove when we get rid of Scenic.GetDisplayInfo().
class TempScenicDelegate {
 public:
  virtual void GetDisplayInfo(fuchsia::ui::scenic::Scenic::GetDisplayInfoCallback callback) = 0;
  virtual void TakeScreenshot(fuchsia::ui::scenic::Scenic::TakeScreenshotCallback callback) = 0;
  virtual void GetDisplayOwnershipEvent(
      fuchsia::ui::scenic::Scenic::GetDisplayOwnershipEventCallback callback) = 0;
};

// A Scenic instance has two main areas of responsibility:
//   - manage Session lifecycles
//   - provide a host environment for Services
class Scenic : public fuchsia::ui::scenic::Scenic {
 public:
  explicit Scenic(sys::ComponentContext* app_context, inspect_deprecated::Node inspect_node,
                  fit::closure quit_callback);
  ~Scenic();

  // [fuchsia::ui::scenic::Scenic]
  void GetDisplayInfo(fuchsia::ui::scenic::Scenic::GetDisplayInfoCallback callback) override;
  // [fuchsia::ui::scenic::Scenic]
  void TakeScreenshot(fuchsia::ui::scenic::Scenic::TakeScreenshotCallback callback) override;
  // [fuchsia::ui::scenic::Scenic]
  void GetDisplayOwnershipEvent(
      fuchsia::ui::scenic::Scenic::GetDisplayOwnershipEventCallback callback) override;

  // Register a delegate class for implementing top-level Scenic operations (e.g., GetDisplayInfo).
  // This delegate must outlive the Scenic instance.
  void SetDelegate(TempScenicDelegate* delegate) {
    FXL_DCHECK(!delegate_);
    delegate_ = delegate;
  }

  // Create and register a new system of the specified type.  At most one System
  // with a given TypeId may be registered.
  template <typename SystemT, typename... Args>
  SystemT* RegisterSystem(Args&&... args);

  // Called by Session when it needs to close itself.
  void CloseSession(Session* session);

  // |fuchsia::ui::scenic::Scenic|
  void CreateSession(
      ::fidl::InterfaceRequest<fuchsia::ui::scenic::Session> session,
      ::fidl::InterfaceHandle<fuchsia::ui::scenic::SessionListener> listener) override;

  sys::ComponentContext* app_context() const { return app_context_; }
  inspect_deprecated::Node* inspect_node() { return &inspect_node_; }
  void Quit() { quit_callback_(); }

  size_t num_sessions() {
    int num_sessions = 0;
    for (auto& binding : session_bindings_.bindings()) {
      if (binding->is_bound()) {
        num_sessions++;
      }
    }
    return num_sessions;
  }

  void SetInitialized();

 private:
  void CreateSessionImmediately(
      ::fidl::InterfaceRequest<fuchsia::ui::scenic::Session> session_request,
      ::fidl::InterfaceHandle<fuchsia::ui::scenic::SessionListener> listener);

  // If a System is not initially initialized, this method will be called when
  // it is ready.
  void OnSystemInitialized(System* system);

  void RunAfterInitialized(fit::closure closure);

  sys::ComponentContext* const app_context_;
  fit::closure quit_callback_;
  inspect_deprecated::Node inspect_node_;

  // Registered systems, indexed by their TypeId. These slots could be null,
  // indicating the System is not available or supported.
  std::array<std::unique_ptr<System>, System::TypeId::kMaxSystems> systems_;

  bool initialized_ = false;
  // Closures that will be run when all systems are initialized.
  std::vector<fit::closure> run_after_initialized_;

  // Session bindings rely on setup of systems_; order matters.
  fidl::BindingSet<fuchsia::ui::scenic::Session, std::unique_ptr<Session>> session_bindings_;
  fidl::BindingSet<fuchsia::ui::scenic::Scenic> scenic_bindings_;

  size_t next_session_id_ = 1;

  TempScenicDelegate* delegate_ = nullptr;

  FXL_DISALLOW_COPY_AND_ASSIGN(Scenic);
};

template <typename SystemT, typename... Args>
SystemT* Scenic::RegisterSystem(Args&&... args) {
  FXL_DCHECK(systems_[SystemT::kTypeId] == nullptr)
      << "System of type: " << SystemT::kTypeId << "was already registered.";

  SystemT* system =
      new SystemT(SystemContext(app_context_, inspect_node_.CreateChild(SystemT::kName),
                                quit_callback_.share()),
                  std::forward<Args>(args)...);
  systems_[SystemT::kTypeId] = std::unique_ptr<System>(system);
  return system;
}

}  // namespace scenic_impl

#endif  // GARNET_LIB_UI_SCENIC_SCENIC_H_
