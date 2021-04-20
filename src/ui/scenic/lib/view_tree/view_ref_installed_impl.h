// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_VIEW_TREE_VIEW_REF_INSTALLED_IMPL_H_
#define SRC_UI_SCENIC_LIB_VIEW_TREE_VIEW_REF_INSTALLED_IMPL_H_

#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/async/cpp/wait.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/component_context.h>
#include <zircon/types.h>

#include <set>
#include <unordered_map>
#include <vector>

#include "src/ui/scenic/lib/view_tree/snapshot_types.h"

namespace view_tree {

// Class that implements the ViewRefInstalled service.
class ViewRefInstalledImpl : public fuchsia::ui::views::ViewRefInstalled {
 public:
  // Publish the ViewRefInstalled service. Separate from constructor for easier testing.
  void Publish(sys::ComponentContext* app_context);

  // |fuchsia::ui::views::ViewRefInstalled|
  void Watch(fuchsia::ui::views::ViewRef view_ref,
             fuchsia::ui::views::ViewRefInstalled::WatchCallback callback) override;

  // Called whenever a new snapshot of the ViewTree is generated.
  // When this happens we look through it to check if any of the waited on views have been installed
  // or any installed views have been removed entirely.
  void OnNewViewTreeSnapshot(std::shared_ptr<const Snapshot> snapshot);

 private:
  // Struct to track for when a view ref gets invalidated.
  struct ViewRefInvalidationWaiter {
    explicit ViewRefInvalidationWaiter(fuchsia::ui::views::ViewRef&& view_ref)
        : waiter(view_ref.reference.get(), ZX_EVENTPAIR_PEER_CLOSED),
          view_ref(std::move(view_ref)) {}
    ~ViewRefInvalidationWaiter() { waiter.Cancel(); }

    async::WaitOnce waiter;
    fuchsia::ui::views::ViewRef view_ref;  // Keep a reference in case this is the last ViewRef.
  };

  // Tracks uninstalled views with Watch() calls waiting on them.
  struct WatchedView {
    explicit WatchedView(fuchsia::ui::views::ViewRef&& view_ref)
        : invalidation_waiter(std::move(view_ref)) {}

    // Waiters that tracks when ViewRefs gets invalidated.
    // We keep a single waiter per watched ViewRef.
    ViewRefInvalidationWaiter invalidation_waiter;

    // All pending callbacks from Watch() calls for this ViewRef.
    std::vector<fuchsia::ui::views::ViewRefInstalled::WatchCallback> callbacks;
  };

  // Called when |view_ref_koid| is observed in the ViewTree for the first time.
  void OnViewRefInstalled(zx_koid_t view_ref_koid);

  // Fired by |invalidation_waiters_| when a ViewRef signals ZX_ERR_PEER_CLOSED.
  void OnViewRefInvalidated(zx_koid_t view_ref_koid, zx_status_t status,
                            const zx_packet_signal* signal);

  fidl::BindingSet<fuchsia::ui::views::ViewRefInstalled> bindings_;

  // All views currently being Watch()ed.
  std::unordered_map<zx_koid_t, WatchedView> watched_views_;

  // The set of active views (i.e. extant in the latest snapshot, either in view_tree or
  // unconnected_views) that have at some point been installed in the view tree.
  std::unordered_set<zx_koid_t> installed_views_;
};

}  // namespace view_tree

#endif  // SRC_UI_SCENIC_LIB_VIEW_TREE_VIEW_REF_INSTALLED_IMPL_H_
