// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/lib/view_associate_framework/view_inspector_client.h"

#include <algorithm>

#include "lib/ftl/functional/closure.h"
#include "lib/ftl/functional/make_copyable.h"
#include "lib/ftl/logging.h"

namespace mozart {

ViewInspectorClient::ViewInspectorClient(
    mojo::InterfaceHandle<ViewInspector> view_inspector)
    : view_inspector_(ViewInspectorPtr::Create(view_inspector.Pass())) {
  FTL_DCHECK(view_inspector_);
}

ViewInspectorClient::~ViewInspectorClient() {}

void ViewInspectorClient::ResolveHits(HitTestResultPtr hit_test_result,
                                      const ResolvedHitsCallback& callback) {
  FTL_DCHECK(hit_test_result);

  std::unique_ptr<ResolvedHits> resolved_hits(
      new ResolvedHits(hit_test_result.Pass()));

  if (resolved_hits->result()->root) {
    mojo::Array<SceneTokenPtr> missing_scene_tokens;
    ResolveSceneHit(resolved_hits->result()->root.get(), resolved_hits.get(),
                    &missing_scene_tokens);
    if (missing_scene_tokens.size()) {
      // TODO(jeffbrown): Ideally we would set the capacity of the array
      // here since we know it upfront but mojo::Array doesn't support this.
      mojo::Array<uint32_t> missing_scene_token_values;
      for (const auto& token : missing_scene_tokens.storage())
        missing_scene_token_values.push_back(token->value);

      std::function<void(mojo::Array<ViewTokenPtr>)> resolved_scenes =
          ftl::MakeCopyable([
            this, hits = std::move(resolved_hits),
            token_values = std::move(missing_scene_token_values), callback
          ](mojo::Array<ViewTokenPtr> view_tokens) mutable {
            OnScenesResolved(std::move(hits), std::move(token_values), callback,
                             view_tokens.Pass());
          });
      view_inspector_->ResolveScenes(missing_scene_tokens.Pass(),
                                     resolved_scenes);
      return;
    }
  }

  callback(std::move(resolved_hits));
}

void ViewInspectorClient::ResolveSceneHit(
    const SceneHit* scene_hit,
    ResolvedHits* resolved_hits,
    mojo::Array<SceneTokenPtr>* missing_scene_tokens) {
  FTL_DCHECK(scene_hit);
  FTL_DCHECK(scene_hit->scene_token);
  FTL_DCHECK(resolved_hits);
  FTL_DCHECK(missing_scene_tokens);

  const uint32_t scene_token_value = scene_hit->scene_token->value;
  if (resolved_hits->map().find(scene_token_value) ==
      resolved_hits->map().end()) {
    auto it = resolved_scene_cache_.find(scene_hit->scene_token->value);
    if (it != resolved_scene_cache_.end()) {
      if (it->second)
        resolved_hits->AddMapping(scene_token_value, it->second->Clone());
    } else {
      if (std::none_of(missing_scene_tokens->storage().begin(),
                       missing_scene_tokens->storage().end(),
                       [scene_token_value](const SceneTokenPtr& needle) {
                         return needle->value == scene_token_value;
                       }))
        missing_scene_tokens->push_back(scene_hit->scene_token.Clone());
    }
  }

  for (const auto& hit : scene_hit->hits.storage()) {
    if (hit->is_scene()) {
      ResolveSceneHit(hit->get_scene().get(), resolved_hits,
                      missing_scene_tokens);
    }
  }
}

void ViewInspectorClient::OnScenesResolved(
    std::unique_ptr<ResolvedHits> resolved_hits,
    mojo::Array<uint32_t> missing_scene_token_values,
    const ResolvedHitsCallback& callback,
    mojo::Array<ViewTokenPtr> view_tokens) {
  FTL_DCHECK(resolved_hits);
  FTL_DCHECK(missing_scene_token_values);
  FTL_DCHECK(view_tokens);
  FTL_DCHECK(missing_scene_token_values.size() == view_tokens.size());

  for (size_t i = 0; i < view_tokens.size(); i++) {
    const uint32_t scene_token_value = missing_scene_token_values[i];
    resolved_scene_cache_.emplace(scene_token_value, view_tokens[i].Clone());
    if (view_tokens[i])
      resolved_hits->AddMapping(scene_token_value, view_tokens[i].Pass());
  }

  callback(std::move(resolved_hits));
}

}  // namespace mozart
