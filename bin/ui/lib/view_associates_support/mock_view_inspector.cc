// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/lib/view_associates_support/mock_view_inspector.h"

#include "lib/ftl/logging.h"

namespace mojo {
namespace ui {

MockViewInspector::MockViewInspector() {}

MockViewInspector::~MockViewInspector() {
  for (const auto& pair : hit_tester_callbacks_)
    pair.second.Run(false);
}

void MockViewInspector::SetHitTester(
    uint32_t view_tree_token_value,
    mojo::gfx::composition::HitTester* hit_tester) {
  FTL_DCHECK(view_tree_token_value);

  hit_testers_.erase(hit_testers_.find(view_tree_token_value),
                     hit_testers_.end());
  if (hit_tester)
    hit_testers_.emplace(view_tree_token_value, hit_tester);

  auto pair = hit_tester_callbacks_.equal_range(view_tree_token_value);
  for (auto it = pair.first; it != pair.second; ++it)
    it->second.Run(hit_tester != nullptr);
  hit_tester_callbacks_.erase(pair.first, pair.second);
}

void MockViewInspector::CloseHitTesterBindings() {
  hit_tester_bindings_.CloseAllBindings();
}

void MockViewInspector::SetSceneMapping(uint32_t scene_token_value,
                                        mojo::ui::ViewTokenPtr view_token) {
  FTL_DCHECK(scene_token_value);

  if (view_token)
    scene_mappings_.emplace(scene_token_value, view_token.Pass());
  else
    scene_mappings_.erase(scene_mappings_.find(scene_token_value),
                          scene_mappings_.end());
}

void MockViewInspector::GetHitTester(
    mojo::ui::ViewTreeTokenPtr view_tree_token,
    mojo::InterfaceRequest<mojo::gfx::composition::HitTester>
        hit_tester_request,
    const GetHitTesterCallback& callback) {
  FTL_DCHECK(view_tree_token);
  FTL_DCHECK(view_tree_token->value);
  FTL_DCHECK(hit_tester_request.is_pending());

  hit_tester_lookups_++;

  auto it = hit_testers_.find(view_tree_token->value);
  if (it == hit_testers_.end()) {
    callback.Run(false);
    return;
  }

  hit_tester_bindings_.AddBinding(it->second, hit_tester_request.Pass());
  hit_tester_callbacks_.emplace(view_tree_token->value, callback);
}

void MockViewInspector::ResolveScenes(
    mojo::Array<mojo::gfx::composition::SceneTokenPtr> scene_tokens,
    const ResolveScenesCallback& callback) {
  FTL_DCHECK(!scene_tokens.is_null());

  scene_lookups_++;

  mojo::Array<mojo::ui::ViewTokenPtr> view_tokens;
  view_tokens.resize(scene_tokens.size());
  for (size_t i = 0; i < scene_tokens.size(); i++) {
    FTL_DCHECK(scene_tokens[i]);
    FTL_DCHECK(scene_tokens[i]->value);
    auto it = scene_mappings_.find(scene_tokens[i]->value);
    if (it != scene_mappings_.end())
      view_tokens[i] = it->second.Clone();
  }

  callback.Run(view_tokens.Pass());
}

}  // namespace ui
}  // namespace mojo
