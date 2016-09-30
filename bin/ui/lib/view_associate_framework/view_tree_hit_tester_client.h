// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MOZART_LIB_VIEW_ASSOCIATE_FRAMEWORK_VIEW_TREE_HIT_TESTER_CLIENT_H_
#define APPS_MOZART_LIB_VIEW_ASSOCIATE_FRAMEWORK_VIEW_TREE_HIT_TESTER_CLIENT_H_

#include <queue>

#include "apps/mozart/lib/view_associate_framework/view_inspector_client.h"
#include "apps/mozart/services/composition/interfaces/hit_tests.mojom.h"
#include "apps/mozart/services/views/interfaces/view_trees.mojom.h"
#include "lib/ftl/functional/closure.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/memory/ref_counted.h"
#include "lib/ftl/memory/ref_ptr.h"
#include "lib/ftl/memory/weak_ptr.h"

namespace mozart {

// Holds a hit tester for a view tree and keeps it up to date as the
// hit tester is invalidated.
class ViewTreeHitTesterClient
    : public ftl::RefCountedThreadSafe<ViewTreeHitTesterClient> {
 public:
  ViewTreeHitTesterClient(
      const ftl::RefPtr<ViewInspectorClient>& view_inspector_client,
      ViewTreeTokenPtr view_tree_token);

  // Performs a hit test for the specified point then invokes the callback.
  // Note: May invoke the callback immediately if no remote calls were required.
  void HitTest(mojo::PointFPtr point, const ResolvedHitsCallback& callback);

  // Sets a callback to invoke when the hit tester changes.
  void set_hit_tester_changed_callback(const ftl::Closure& callback) {
    hit_tester_changed_callback_ = callback;
  }

 private:
  FRIEND_REF_COUNTED_THREAD_SAFE(ViewTreeHitTesterClient);
  ~ViewTreeHitTesterClient();

  void OnHitTestResult(HitTestResultPtr result);

  void UpdateHitTester();
  void ReleaseHitTester();
  void OnHitTesterInvalidated(bool renderer_changed);
  void OnHitTesterDied();

  ftl::RefPtr<ViewInspectorClient> view_inspector_client_;
  ViewTreeTokenPtr view_tree_token_;
  HitTesterPtr hit_tester_;

  std::queue<ResolvedHitsCallback> pending_callbacks_;
  ftl::Closure hit_tester_changed_callback_;

  ftl::WeakPtrFactory<ViewTreeHitTesterClient> weak_factory_;

  FTL_DISALLOW_COPY_AND_ASSIGN(ViewTreeHitTesterClient);
};

}  // namespace mozart

#endif  // APPS_MOZART_LIB_VIEW_ASSOCIATE_FRAMEWORK_VIEW_TREE_HIT_TESTER_CLIENT_H_
