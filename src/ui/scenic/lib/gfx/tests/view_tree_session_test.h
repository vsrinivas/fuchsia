// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_GFX_TESTS_VIEW_TREE_SESSION_TEST_H_
#define SRC_UI_SCENIC_LIB_GFX_TESTS_VIEW_TREE_SESSION_TEST_H_

#include "src/ui/scenic/lib/gfx/engine/view_tree_updater.h"
#include "src/ui/scenic/lib/gfx/tests/session_test.h"

namespace scenic_impl {
namespace gfx {
namespace test {

// This test fixture enables ViewTree updates.
//
// Users need to register all the session they create, and call
// StageAndUpdateViewTree() to update the ViewTree stored in SceneGraph every
// time after they create pending changes to the ViewTree.
//
class ViewTreeSessionTest : public SessionTest {
 public:
  ViewTreeSessionTest() = default;

  // |SessionTest::SetUp|
  void SetUp() override;

  std::unique_ptr<Session> CreateAndRegisterSession();

  // Register new created session. Only updates of registered session will be
  // applied to the ViewTree.
  void RegisterSession(Session* session);

  // Apply all the staged ViewTree updates to ViewTree stored in |scene_graph|,
  // and clear all the staged ViewTree updates.
  void StageAndUpdateViewTree(SceneGraph* scene_graph);

 private:
  std::vector<fxl::WeakPtr<Session>> sessions_;
};

}  // namespace test
}  // namespace gfx
}  // namespace scenic_impl

#endif  // SRC_UI_SCENIC_LIB_GFX_TESTS_VIEW_TREE_SESSION_TEST_H_
