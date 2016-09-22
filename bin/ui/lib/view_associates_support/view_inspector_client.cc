// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mojo/ui/associates/view_inspector_client.h"

#include <algorithm>

#include "base/bind.h"
#include "base/logging.h"

namespace mojo {
namespace ui {

ViewInspectorClient::ViewInspectorClient(
    mojo::InterfaceHandle<mojo::ui::ViewInspector> view_inspector)
    : view_inspector_(
          mojo::ui::ViewInspectorPtr::Create(view_inspector.Pass())) {
  DCHECK(view_inspector_);
}

ViewInspectorClient::~ViewInspectorClient() {}

void ViewInspectorClient::ResolveHits(
    mojo::gfx::composition::HitTestResultPtr hit_test_result,
    const ResolvedHitsCallback& callback) {
  DCHECK(hit_test_result);

  scoped_ptr<ResolvedHits> resolved_hits(
      new ResolvedHits(hit_test_result.Pass()));

  if (resolved_hits->result()->root) {
    mojo::Array<mojo::gfx::composition::SceneTokenPtr> missing_scene_tokens;
    ResolveSceneHit(resolved_hits->result()->root.get(), resolved_hits.get(),
                    &missing_scene_tokens);
    if (missing_scene_tokens.size()) {
      // TODO(jeffbrown): Ideally we would set the capacity of the array
      // here since we know it upfront but mojo::Array doesn't support this.
      mojo::Array<uint32_t> missing_scene_token_values;
      for (const auto& token : missing_scene_tokens.storage())
        missing_scene_token_values.push_back(token->value);
      view_inspector_->ResolveScenes(
          missing_scene_tokens.Pass(),
          base::Bind(&ViewInspectorClient::OnScenesResolved,
                     base::Unretained(this), base::Passed(resolved_hits.Pass()),
                     base::Passed(missing_scene_token_values.Pass()),
                     callback));
      return;
    }
  }

  callback.Run(resolved_hits.Pass());
}

void ViewInspectorClient::ResolveSceneHit(
    const mojo::gfx::composition::SceneHit* scene_hit,
    ResolvedHits* resolved_hits,
    mojo::Array<mojo::gfx::composition::SceneTokenPtr>* missing_scene_tokens) {
  DCHECK(scene_hit);
  DCHECK(scene_hit->scene_token);
  DCHECK(resolved_hits);
  DCHECK(missing_scene_tokens);

  const uint32_t scene_token_value = scene_hit->scene_token->value;
  if (resolved_hits->map().find(scene_token_value) ==
      resolved_hits->map().end()) {
    auto it = resolved_scene_cache_.find(scene_hit->scene_token->value);
    if (it != resolved_scene_cache_.end()) {
      if (it->second)
        resolved_hits->AddMapping(scene_token_value, it->second->Clone());
    } else {
      if (std::none_of(
              missing_scene_tokens->storage().begin(),
              missing_scene_tokens->storage().end(),
              [scene_token_value](
                  const mojo::gfx::composition::SceneTokenPtr& needle) {
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
    scoped_ptr<ResolvedHits> resolved_hits,
    mojo::Array<uint32_t> missing_scene_token_values,
    const ResolvedHitsCallback& callback,
    mojo::Array<mojo::ui::ViewTokenPtr> view_tokens) {
  DCHECK(resolved_hits);
  DCHECK(missing_scene_token_values);
  DCHECK(view_tokens);
  DCHECK(missing_scene_token_values.size() == view_tokens.size());

  for (size_t i = 0; i < view_tokens.size(); i++) {
    const uint32_t scene_token_value = missing_scene_token_values[i];
    resolved_scene_cache_.emplace(scene_token_value, view_tokens[i].Clone());
    if (view_tokens[i])
      resolved_hits->AddMapping(scene_token_value, view_tokens[i].Pass());
  }

  callback.Run(resolved_hits.Pass());
}

}  // namespace ui
}  // namespace mojo
