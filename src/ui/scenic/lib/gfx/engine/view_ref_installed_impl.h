// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_GFX_ENGINE_VIEW_REF_INSTALLED_IMPL_H_
#define SRC_UI_SCENIC_LIB_GFX_ENGINE_VIEW_REF_INSTALLED_IMPL_H_

#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/async/cpp/wait.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/component_context.h>
#include <zircon/types.h>

#include <set>
#include <unordered_map>
#include <vector>

namespace scenic_impl::gfx {

// Class that implements the ViewRefInstalled service.
// This is not thread safe and should only be run from the same thread as the default dispatcher.
class ViewRefInstalledImpl : public fuchsia::ui::views::ViewRefInstalled {
 public:
  ViewRefInstalledImpl(fit::function<bool(zx_koid_t)> is_installed)
      : is_installed_(std::move(is_installed)) {}
  ~ViewRefInstalledImpl() = default;

  // Publish the ViewRefInstalled service.
  void Publish(sys::ComponentContext* app_context);

  // |fuchsia::ui::views::ViewRefInstalled|
  void Watch(fuchsia::ui::views::ViewRef view_ref,
             fuchsia::ui::views::ViewRefInstalled::WatchCallback callback);

  // Should be called by the ViewTree whenever a new ViewRef is installed.
  void OnViewRefInstalled(zx_koid_t view_ref_koid);

 private:
  struct ViewRefInvalidationWaiter {
    ViewRefInvalidationWaiter(fuchsia::ui::views::ViewRef&& view_ref)
        : waiter(view_ref.reference.get(), ZX_EVENTPAIR_PEER_CLOSED),
          view_ref(std::move(view_ref)) {}
    ~ViewRefInvalidationWaiter() { waiter.Cancel(); }

    async::WaitOnce waiter;
    fuchsia::ui::views::ViewRef view_ref;  // Keep a reference in case this is the last ViewRef.
  };

  // Fired by |invalidation_waiters_| when a ViewRef signals ZX_ERR_PEER_CLOSED.
  void OnViewRefInvalidated(zx_koid_t view_ref_koid, zx_status_t status,
                            const zx_packet_signal* signal);

  // Clean up after a ViewRef is installed/invalidated.
  void CleanUp(zx_koid_t view_ref_koid);

  const fit::function<bool(zx_koid_t)> is_installed_;

  // All callbacks from Watch() calls that have yet to complete.
  std::unordered_map<zx_koid_t, std::vector<fuchsia::ui::views::ViewRefInstalled::WatchCallback>>
      pending_callbacks_;

  // Waiters that tracks when ViewRefs gets invalidated.
  // We keep a single waiter per watched ViewRef.
  std::unordered_map<zx_koid_t, ViewRefInvalidationWaiter> invalidation_waiters_;

  fidl::BindingSet<fuchsia::ui::views::ViewRefInstalled> bindings_;
};

}  // namespace scenic_impl::gfx

#endif  // SRC_UI_SCENIC_LIB_GFX_ENGINE_VIEW_REF_INSTALLED_IMPL_H_
