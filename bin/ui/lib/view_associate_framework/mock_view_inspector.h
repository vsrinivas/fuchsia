// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MOZART_LIB_VIEW_ASSOCIATE_FRAMEWORK_MOCK_VIEW_INSPECTOR_H_
#define APPS_MOZART_LIB_VIEW_ASSOCIATE_FRAMEWORK_MOCK_VIEW_INSPECTOR_H_

#include <unordered_map>

#include "apps/mozart/services/views/interfaces/view_associates.mojom.h"
#include "lib/ftl/macros.h"
#include "mojo/public/cpp/bindings/binding_set.h"

namespace mozart {

class MockViewInspector : public ViewInspector {
 public:
  MockViewInspector();
  ~MockViewInspector() override;

  // Sets the hit tester to use for a particular view tree.
  // Setting to null removes the hit tester.
  // Does not take ownership; the hit tester must outlive this object.
  void SetHitTester(uint32_t view_tree_token_value, HitTester* hit_tester);

  // Closes all hit tester bindings without invoking the changed callbacks.
  void CloseHitTesterBindings();

  // Adds a mapping from scene token to view token.
  // Setting to null removes the scene mapping.
  void SetSceneMapping(uint32_t scene_token_value, ViewTokenPtr view_token);

  // Returns how often |GetHitTester| was called.
  uint32_t hit_tester_lookups() const { return hit_tester_lookups_; }

  // Returns how often |ResolveScenes| was called.
  uint32_t scene_lookups() const { return scene_lookups_; }

  // |ViewInspector|
  void GetHitTester(ViewTreeTokenPtr view_tree_token,
                    mojo::InterfaceRequest<HitTester> hit_tester_request,
                    const GetHitTesterCallback& callback) override;
  void ResolveScenes(mojo::Array<SceneTokenPtr> scene_tokens,
                     const ResolveScenesCallback& callback) override;

 private:
  std::unordered_map<uint32_t, HitTester*> hit_testers_;
  mojo::BindingSet<HitTester> hit_tester_bindings_;
  std::unordered_multimap<uint32_t, GetHitTesterCallback> hit_tester_callbacks_;

  std::unordered_map<uint32_t, ViewTokenPtr> scene_mappings_;

  uint32_t hit_tester_lookups_ = 0u;
  uint32_t scene_lookups_ = 0u;

  FTL_DISALLOW_COPY_AND_ASSIGN(MockViewInspector);
};

}  // namespace mozart

#endif  // APPS_MOZART_LIB_VIEW_ASSOCIATE_FRAMEWORK_MOCK_VIEW_INSPECTOR_H_
