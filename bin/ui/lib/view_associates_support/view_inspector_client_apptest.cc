// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mojo/ui/associates/view_inspector_client.h"

#include "base/bind.h"
#include "base/message_loop/message_loop.h"
#include "base/run_loop.h"
#include "mojo/public/cpp/application/application_test_base.h"
#include "mojo/ui/associates/mock_view_inspector.h"
#include "mojo/ui/associates/test_helpers.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace test {

class ViewInspectorClientTest : public mojo::test::ApplicationTestBase {
 public:
  ViewInspectorClientTest() : view_inspector_binding_(&view_inspector_) {}
  ~ViewInspectorClientTest() override {}

  void SetUp() override {
    mojo::test::ApplicationTestBase::SetUp();

    mojo::InterfaceHandle<mojo::ui::ViewInspector> view_inspector;
    view_inspector_binding_.Bind(&view_inspector);
    view_inspector_client_ =
        new mojo::ui::ViewInspectorClient(view_inspector.Pass());
  }

 protected:
  void ResolveHits(mojo::gfx::composition::HitTestResultPtr hit_test_result,
                   scoped_ptr<mojo::ui::ResolvedHits>* resolved_hits) {
    base::RunLoop loop;
    view_inspector_client_->ResolveHits(
        hit_test_result.Pass(),
        base::Bind(&Capture<scoped_ptr<mojo::ui::ResolvedHits>>,
                   loop.QuitClosure(), resolved_hits));
    loop.Run();
  }

  mojo::ui::MockViewInspector view_inspector_;
  mojo::Binding<mojo::ui::ViewInspector> view_inspector_binding_;
  scoped_refptr<mojo::ui::ViewInspectorClient> view_inspector_client_;

 private:
  DISALLOW_COPY_AND_ASSIGN(ViewInspectorClientTest);
};

TEST_F(ViewInspectorClientTest, EmptyResult) {
  auto hit_test_result = mojo::gfx::composition::HitTestResult::New();

  scoped_ptr<mojo::ui::ResolvedHits> resolved_hits;
  ResolveHits(hit_test_result.Pass(), &resolved_hits);
  ASSERT_NE(nullptr, resolved_hits.get());
  EXPECT_NE(nullptr, resolved_hits->result());
  EXPECT_TRUE(resolved_hits->map().empty());
  EXPECT_EQ(0u, view_inspector_.scene_lookups());
}

TEST_F(ViewInspectorClientTest, CachingNegativeResult) {
  auto scene_token_1 = MakeDummySceneToken(1u);

  // Initial lookup, should miss cache.
  scoped_ptr<mojo::ui::ResolvedHits> resolved_hits;
  ResolveHits(MakeSimpleHitTestResult(scene_token_1.Clone()), &resolved_hits);
  ASSERT_NE(nullptr, resolved_hits.get());
  EXPECT_NE(nullptr, resolved_hits->result());
  EXPECT_TRUE(resolved_hits->map().empty());
  EXPECT_EQ(1u, view_inspector_.scene_lookups());

  // Try again, ensure no further attempts to look up scene 1.
  ResolveHits(MakeSimpleHitTestResult(scene_token_1.Clone()), &resolved_hits);
  ASSERT_NE(nullptr, resolved_hits.get());
  EXPECT_NE(nullptr, resolved_hits->result());
  EXPECT_TRUE(resolved_hits->map().empty());
  EXPECT_EQ(1u, view_inspector_.scene_lookups());
}

TEST_F(ViewInspectorClientTest, CachingPositiveResult) {
  auto scene_token_1 = MakeDummySceneToken(1u);
  auto view_token_11 = MakeDummyViewToken(11u);
  view_inspector_.SetSceneMapping(scene_token_1->value, view_token_11.Clone());

  // Initial lookup, should hit cache.
  scoped_ptr<mojo::ui::ResolvedHits> resolved_hits;
  ResolveHits(MakeSimpleHitTestResult(scene_token_1.Clone()), &resolved_hits);
  ASSERT_NE(nullptr, resolved_hits.get());
  EXPECT_NE(nullptr, resolved_hits->result());
  EXPECT_EQ(1u, resolved_hits->map().size());
  EXPECT_TRUE(
      view_token_11.Equals(resolved_hits->map().at(scene_token_1->value)));
  EXPECT_EQ(1u, view_inspector_.scene_lookups());

  // Try again, ensure no further attempts to look up scene 1.
  ResolveHits(MakeSimpleHitTestResult(scene_token_1.Clone()), &resolved_hits);
  ASSERT_NE(nullptr, resolved_hits.get());
  EXPECT_NE(nullptr, resolved_hits->result());
  EXPECT_EQ(1u, resolved_hits->map().size());
  EXPECT_TRUE(
      view_token_11.Equals(resolved_hits->map().at(scene_token_1->value)));
  EXPECT_EQ(1u, view_inspector_.scene_lookups());
}

TEST_F(ViewInspectorClientTest, CompositeSceneGraph) {
  auto scene_token_1 = MakeDummySceneToken(1u);
  auto scene_token_2 = MakeDummySceneToken(2u);
  auto scene_token_3 = MakeDummySceneToken(3u);
  auto view_token_11 = MakeDummyViewToken(11u);
  auto view_token_33 = MakeDummyViewToken(33u);
  view_inspector_.SetSceneMapping(scene_token_1->value, view_token_11.Clone());
  view_inspector_.SetSceneMapping(scene_token_3->value, view_token_33.Clone());

  // Scene graph with hits in 3 scenes, only 2 of which are views.
  auto hit_test_result = mojo::gfx::composition::HitTestResult::New();
  hit_test_result->root = mojo::gfx::composition::SceneHit::New();
  hit_test_result->root->scene_token = scene_token_1.Clone();
  hit_test_result->root->hits.push_back(mojo::gfx::composition::Hit::New());
  hit_test_result->root->hits.at(0)->set_scene(
      mojo::gfx::composition::SceneHit::New());
  hit_test_result->root->hits.at(0)->get_scene()->scene_token =
      scene_token_2.Clone();
  hit_test_result->root->hits.at(0)->get_scene()->hits.push_back(
      mojo::gfx::composition::Hit::New());
  hit_test_result->root->hits.at(0)->get_scene()->hits.at(0)->set_node(
      mojo::gfx::composition::NodeHit::New());
  hit_test_result->root->hits.push_back(mojo::gfx::composition::Hit::New());
  hit_test_result->root->hits[1]->set_scene(
      mojo::gfx::composition::SceneHit::New());
  hit_test_result->root->hits[1]->get_scene()->scene_token =
      scene_token_3.Clone();
  hit_test_result->root->hits[1]->get_scene()->hits.push_back(
      mojo::gfx::composition::Hit::New());
  hit_test_result->root->hits[1]->get_scene()->hits.at(0)->set_node(
      mojo::gfx::composition::NodeHit::New());

  scoped_ptr<mojo::ui::ResolvedHits> resolved_hits;
  ResolveHits(hit_test_result.Pass(), &resolved_hits);
  EXPECT_NE(nullptr, resolved_hits->result());
  EXPECT_EQ(2u, resolved_hits->map().size());
  EXPECT_TRUE(
      view_token_11.Equals(resolved_hits->map().at(scene_token_1->value)));
  EXPECT_TRUE(
      view_token_33.Equals(resolved_hits->map().at(scene_token_3->value)));
  EXPECT_EQ(1u, view_inspector_.scene_lookups());
}

}  // namespace test
