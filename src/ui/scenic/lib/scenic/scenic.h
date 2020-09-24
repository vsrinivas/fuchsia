// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_SCENIC_SCENIC_H_
#define SRC_UI_SCENIC_LIB_SCENIC_SCENIC_H_

#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <fuchsia/ui/scenic/internal/cpp/fidl.h>
#include <lib/async/default.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fit/function.h>

#include <array>
#include <memory>
#include <unordered_map>
#include <vector>

#include "lib/inspect/cpp/inspect.h"
#include "src/lib/fxl/macros.h"
#include "src/ui/scenic/lib/scenic/session.h"
#include "src/ui/scenic/lib/scenic/system.h"
#include "src/ui/scenic/lib/scenic/take_screenshot_delegate_deprecated.h"
#include "src/ui/scenic/lib/scheduling/frame_scheduler.h"
#include "src/ui/scenic/lib/scheduling/id.h"

namespace scenic_impl {

// A Scenic instance has two main areas of responsibility:
//   - manage Session lifecycles
//   - provide a host environment for Services
class Scenic final : public fuchsia::ui::scenic::Scenic, public scheduling::SessionUpdater {
 public:
  // TODO(fxbug.dev/23686): Remove when we get rid of Scenic.GetDisplayInfo().
  class GetDisplayInfoDelegateDeprecated {
   public:
    virtual ~GetDisplayInfoDelegateDeprecated() = default;
    virtual void GetDisplayInfo(fuchsia::ui::scenic::Scenic::GetDisplayInfoCallback callback) = 0;
    virtual void GetDisplayOwnershipEvent(
        fuchsia::ui::scenic::Scenic::GetDisplayOwnershipEventCallback callback) = 0;
  };

  explicit Scenic(sys::ComponentContext* app_context, inspect::Node inspect_node,
                  fit::closure quit_callback);
  ~Scenic();

  // |fuchsia::ui::scenic::Scenic|
  void GetDisplayInfo(fuchsia::ui::scenic::Scenic::GetDisplayInfoCallback callback) override;
  // |fuchsia::ui::scenic::Scenic|
  void TakeScreenshot(fuchsia::ui::scenic::Scenic::TakeScreenshotCallback callback) override;
  // |fuchsia::ui::scenic::Scenic|
  void GetDisplayOwnershipEvent(
      fuchsia::ui::scenic::Scenic::GetDisplayOwnershipEventCallback callback) override;

  // |scheduling::SessionUpdater|
  UpdateResults UpdateSessions(
      const std::unordered_map<scheduling::SessionId, scheduling::PresentId>& sessions_to_update,
      uint64_t trace_id) override;
  // |scheduling::SessionUpdater|
  void OnFramePresented(
      const std::unordered_map<scheduling::SessionId, std::map<scheduling::PresentId, zx::time>>&
          latched_times,
      scheduling::PresentTimestamps present_times) override;

  // Register a delegate class for implementing top-level Scenic operations (e.g., GetDisplayInfo).
  // This delegate must outlive the Scenic instance.
  void SetDisplayInfoDelegate(GetDisplayInfoDelegateDeprecated* delegate) {
    FX_DCHECK(!display_delegate_);
    display_delegate_ = delegate;
  }

  void SetScreenshotDelegate(TakeScreenshotDelegateDeprecated* delegate) {
    FX_DCHECK(!screenshot_delegate_);
    screenshot_delegate_ = delegate;
  }

  // Create and register a new system of the specified type.  At most one System
  // with a given TypeId may be registered.
  template <typename SystemT, typename... Args>
  std::shared_ptr<SystemT> RegisterSystem(Args&&... args);

  // Called by Session when it needs to close itself.
  void CloseSession(scheduling::SessionId session_id);

  // |fuchsia::ui::scenic::Scenic|
  void CreateSession(fidl::InterfaceRequest<fuchsia::ui::scenic::Session> session,
                     fidl::InterfaceHandle<fuchsia::ui::scenic::SessionListener> listener) override;

  // |fuchsia::ui::scenic::Scenic|
  void CreateSession2(fidl::InterfaceRequest<fuchsia::ui::scenic::Session> session,
                      fidl::InterfaceHandle<fuchsia::ui::scenic::SessionListener> listener,
                      fidl::InterfaceRequest<fuchsia::ui::views::Focuser> view_focuser) override;

  sys::ComponentContext* app_context() const { return app_context_; }
  inspect::Node* inspect_node() { return &inspect_node_; }

  size_t num_sessions();

  void SetInitialized(fxl::WeakPtr<gfx::ViewFocuserRegistry> view_focuser_registry = nullptr);

  void SetFrameScheduler(const std::shared_ptr<scheduling::FrameScheduler>& frame_scheduler);

  void InitializeSnapshotService(std::unique_ptr<fuchsia::ui::scenic::internal::Snapshot> snapshot);

  fuchsia::ui::scenic::internal::Snapshot* snapshot() { return snapshot_.get(); }

 private:
  void CreateSessionImmediately(
      fidl::InterfaceRequest<fuchsia::ui::scenic::Session> session_request,
      fidl::InterfaceHandle<fuchsia::ui::scenic::SessionListener> listener,
      fidl::InterfaceRequest<fuchsia::ui::views::Focuser> view_focuser);

  // If a System is not initially initialized, this method will be called when
  // it is ready.
  void OnSystemInitialized(System* system);

  void RunAfterInitialized(fit::closure closure);

  sys::ComponentContext* const app_context_;
  fit::closure quit_callback_;
  inspect::Node inspect_node_;

  // Registered systems, mapped to their TypeId.
  std::unordered_map<System::TypeId, std::shared_ptr<System>> systems_;

  bool initialized_ = false;
  // Closures that will be run when all systems are initialized.
  std::vector<fit::closure> run_after_initialized_;

  std::shared_ptr<scheduling::FrameScheduler> frame_scheduler_;

  // Session bindings rely on setup of systems_; order matters.
  std::unordered_map<scheduling::SessionId, std::unique_ptr<scenic_impl::Session>> sessions_;
  fidl::BindingSet<fuchsia::ui::scenic::Scenic> scenic_bindings_;
  fidl::BindingSet<fuchsia::ui::scenic::internal::Snapshot> snapshot_bindings_;

  GetDisplayInfoDelegateDeprecated* display_delegate_ = nullptr;
  TakeScreenshotDelegateDeprecated* screenshot_delegate_ = nullptr;

  fxl::WeakPtr<gfx::ViewFocuserRegistry> view_focuser_registry_;

 protected:
  std::unique_ptr<fuchsia::ui::scenic::internal::Snapshot> snapshot_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Scenic);
};

template <typename SystemT, typename... Args>
std::shared_ptr<SystemT> Scenic::RegisterSystem(Args&&... args) {
  FX_DCHECK(systems_.find(SystemT::kTypeId) == systems_.end())
      << "System of type: " << SystemT::kTypeId << "was already registered.";

  SystemT* system =
      new SystemT(SystemContext(app_context_, inspect_node_.CreateChild(SystemT::kName),
                                quit_callback_.share()),
                  std::forward<Args>(args)...);
  systems_[SystemT::kTypeId] = std::shared_ptr<System>(system);
  return std::static_pointer_cast<SystemT>(systems_[SystemT::kTypeId]);
}

}  // namespace scenic_impl

#endif  // SRC_UI_SCENIC_LIB_SCENIC_SCENIC_H_
