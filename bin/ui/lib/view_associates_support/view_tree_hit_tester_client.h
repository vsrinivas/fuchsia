// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MOJO_UI_ASSOCIATES_VIEW_TREE_HIT_TESTER_CLIENT_H_
#define MOJO_UI_ASSOCIATES_VIEW_TREE_HIT_TESTER_CLIENT_H_

#include <queue>

#include "base/callback.h"
#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "mojo/services/gfx/composition/interfaces/hit_tests.mojom.h"
#include "mojo/services/ui/views/interfaces/view_trees.mojom.h"
#include "mojo/ui/associates/view_inspector_client.h"

namespace mojo {
namespace ui {

// Holds a hit tester for a view tree and keeps it up to date as the
// hit tester is invalidated.
class ViewTreeHitTesterClient
    : public base::RefCounted<ViewTreeHitTesterClient> {
 public:
  ViewTreeHitTesterClient(
      const scoped_refptr<ViewInspectorClient>& view_inspector_client,
      mojo::ui::ViewTreeTokenPtr view_tree_token);

  // Performs a hit test for the specified point then invokes the callback.
  // Note: May invoke the callback immediately if no remote calls were required.
  void HitTest(mojo::PointFPtr point, const ResolvedHitsCallback& callback);

  // Sets a callback to invoke when the hit tester changes.
  void set_hit_tester_changed_callback(const base::Closure& callback) {
    hit_tester_changed_callback_ = callback;
  }

 private:
  friend class base::RefCounted<ViewTreeHitTesterClient>;
  ~ViewTreeHitTesterClient();

  void OnHitTestResult(mojo::gfx::composition::HitTestResultPtr result);

  void UpdateHitTester();
  void ReleaseHitTester();
  void OnHitTesterInvalidated(bool renderer_changed);
  void OnHitTesterDied();

  scoped_refptr<ViewInspectorClient> view_inspector_client_;
  mojo::ui::ViewTreeTokenPtr view_tree_token_;
  mojo::gfx::composition::HitTesterPtr hit_tester_;

  std::queue<ResolvedHitsCallback> pending_callbacks_;
  base::Closure hit_tester_changed_callback_;

  base::WeakPtrFactory<ViewTreeHitTesterClient> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(ViewTreeHitTesterClient);
};

}  // namespace ui
}  // namespace mojo

#endif  // MOJO_UI_ASSOCIATES_VIEW_TREE_HIT_TESTER_CLIENT_H_
