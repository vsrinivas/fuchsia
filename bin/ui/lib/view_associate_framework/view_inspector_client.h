// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MOZART_LIB_VIEW_ASSOCIATE_FRAMEWORK_VIEW_INSPECTOR_CLIENT_H_
#define APPS_MOZART_LIB_VIEW_ASSOCIATE_FRAMEWORK_VIEW_INSPECTOR_CLIENT_H_

#include <queue>
#include <vector>

#include "apps/mozart/lib/view_associate_framework/resolved_hits.h"
#include "apps/mozart/services/views/interfaces/view_associates.mojom.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/memory/ref_counted.h"

namespace mozart {

// Provides facilities for using a |ViewInspector|, including caching.
class ViewInspectorClient
    : public ftl::RefCountedThreadSafe<ViewInspectorClient> {
 public:
  ViewInspectorClient(mojo::InterfaceHandle<ViewInspector> view_inspector);

  ViewInspector* view_inspector() { return view_inspector_.get(); }

  // Resolves all of the scene tokens referenced in the hit test result
  // then invokes the callback.
  // Note: May invoke the callback immediately if no remote calls were required.
  void ResolveHits(HitTestResultPtr hit_test_result,
                   const ResolvedHitsCallback& callback);

 private:
  FRIEND_REF_COUNTED_THREAD_SAFE(ViewInspectorClient);
  ~ViewInspectorClient();

  void ResolveSceneHit(const SceneHit* scene_hit,
                       ResolvedHits* resolved_hits,
                       mojo::Array<SceneTokenPtr>* missing_scene_tokens);
  void OnScenesResolved(std::unique_ptr<ResolvedHits> resolved_hits,
                        mojo::Array<uint32_t> missing_scene_token_values,
                        const ResolvedHitsCallback& callback,
                        mojo::Array<ViewTokenPtr> view_tokens);

  ViewInspectorPtr view_inspector_;

  // TODO(jeffbrown): Decide how this should be pruned.
  SceneTokenValueToViewTokenMap resolved_scene_cache_;

  std::queue<ResolvedHitsCallback> resolutions_;

  FTL_DISALLOW_COPY_AND_ASSIGN(ViewInspectorClient);
};

}  // namespace mozart

#endif  // APPS_MOZART_LIB_VIEW_ASSOCIATE_FRAMEWORK_VIEW_INSPECTOR_CLIENT_H_
