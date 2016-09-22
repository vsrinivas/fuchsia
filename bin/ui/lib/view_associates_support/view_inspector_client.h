// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MOJO_UI_ASSOCIATES_VIEW_INSPECTOR_CLIENT_H_
#define MOJO_UI_ASSOCIATES_VIEW_INSPECTOR_CLIENT_H_

#include <queue>
#include <vector>

#include "apps/mozart/lib/view_associates_support/resolved_hits.h"
#include "apps/mozart/services/views/interfaces/view_associates.mojom.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/memory/ref_counted.h"

namespace mojo {
namespace ui {

// Provides facilities for using a |ViewInspector|, including caching.
class ViewInspectorClient
    : public ftl::RefCountedThreadSafe<ViewInspectorClient> {
 public:
  ViewInspectorClient(
      mojo::InterfaceHandle<mojo::ui::ViewInspector> view_inspector);

  mojo::ui::ViewInspector* view_inspector() { return view_inspector_.get(); }

  // Resolves all of the scene tokens referenced in the hit test result
  // then invokes the callback.
  // Note: May invoke the callback immediately if no remote calls were required.
  void ResolveHits(mojo::gfx::composition::HitTestResultPtr hit_test_result,
                   const ResolvedHitsCallback& callback);

 private:
  FRIEND_REF_COUNTED_THREAD_SAFE(ViewInspectorClient);
  ~ViewInspectorClient();

  void ResolveSceneHit(
      const mojo::gfx::composition::SceneHit* scene_hit,
      ResolvedHits* resolved_hits,
      mojo::Array<mojo::gfx::composition::SceneTokenPtr>* missing_scene_tokens);
  void OnScenesResolved(std::unique_ptr<ResolvedHits> resolved_hits,
                        mojo::Array<uint32_t> missing_scene_token_values,
                        const ResolvedHitsCallback& callback,
                        mojo::Array<mojo::ui::ViewTokenPtr> view_tokens);

  mojo::ui::ViewInspectorPtr view_inspector_;

  // TODO(jeffbrown): Decide how this should be pruned.
  SceneTokenValueToViewTokenMap resolved_scene_cache_;

  std::queue<ResolvedHitsCallback> resolutions_;

  FTL_DISALLOW_COPY_AND_ASSIGN(ViewInspectorClient);
};

}  // namespace ui
}  // namespace mojo

#endif  // MOJO_UI_ASSOCIATES_VIEW_INSPECTOR_CLIENT_H_
