// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/view_manager/input/view_tree_hit_tester_client.h"

#include <utility>

#include "apps/mozart/lib/tests/mocks/mock_hit_tester.h"
#include "apps/mozart/lib/tests/mocks/mock_view_inspector.h"
#include "apps/mozart/src/view_manager/tests/test_helpers.h"
#include "lib/fidl/cpp/application/application_test_base.h"
#include "lib/ftl/memory/ref_ptr.h"
#include "lib/mtl/tasks/message_loop.h"
#include "third_party/gtest/include/gtest/gtest.h"

namespace test {

class ViewTreeHitTesterClientTest : public fidl::test::ApplicationTestBase {
 public:
  ViewTreeHitTesterClientTest() : view_inspector_binding_(&view_inspector_) {}
  ~ViewTreeHitTesterClientTest() override {}

  void SetUp() override {
    fidl::test::ApplicationTestBase::SetUp();

    fidl::InterfaceHandle<ViewInspector> view_inspector;
    view_inspector_binding_.Bind(&view_inspector);
    view_inspector_client_ =
        ftl::MakeRefCounted<ViewInspectorClient>(view_inspector.Pass());

    view_tree_token_ = mozart::ViewTreeToken::New();
    view_tree_token_->value = 1u;
    view_tree_hit_tester_client_ =
        ftl::MakeRefCounted<ViewTreeHitTesterClient>(
            view_inspector_client_, view_tree_token_.Clone());
  }

 protected:
  void HitTest(mozart::PointFPtr point,
               scoped_ptr<ResolvedHits>* resolved_hits) {
    base::RunLoop loop;
    view_tree_hit_tester_client_->HitTest(
        point.Pass(), base::Bind(&Capture<scoped_ptr<ResolvedHits>>,
                                 loop.QuitClosure(), resolved_hits));
    loop.Run();
  }

  mozart::MockViewInspector view_inspector_;
  fidl::Binding<ViewInspector> view_inspector_binding_;
  ftl::RefPtr<ViewInspectorClient> view_inspector_client_;

  mozart::ViewTreeTokenPtr view_tree_token_;
  ftl::RefPtr<ViewTreeHitTesterClient> view_tree_hit_tester_client_;

 private:
  DISALLOW_COPY_AND_ASSIGN(ViewTreeHitTesterClientTest);
};

TEST_F(ViewTreeHitTesterClientTest, NoHitTester) {
  scoped_ptr<ResolvedHits> resolved_hits;
  HitTest(MakePointF(0.f, 0.f), &resolved_hits);
  EXPECT_EQ(nullptr, resolved_hits.get());
}

TEST_F(ViewTreeHitTesterClientTest, HaveHitTester) {
  auto scene_token_1 = MakeDummySceneToken(1u);
  auto view_token_11 = MakeDummyViewToken(11u);
  auto transform_111 = MakeDummyTransform(111);
  auto transform_222 = MakeDummyTransform(222);
  auto transform_333 = MakeDummyTransform(333);
  view_inspector_.SetSceneMapping(scene_token_1->value, view_token_11.Clone());

  mozart::MockHitTester hit_tester;
  view_inspector_.SetHitTester(view_tree_token_->value, &hit_tester);

  // Simple hit test with the first hit tester.
  hit_tester.SetNextResult(
      MakePointF(2.f, 5.f),
      MakeSimpleHitTestResult(scene_token_1.Clone(), transform_111.Clone()));
  scoped_ptr<ResolvedHits> resolved_hits;
  HitTest(MakePointF(2.f, 5.f), &resolved_hits);
  ASSERT_NE(nullptr, resolved_hits.get());
  EXPECT_NE(nullptr, resolved_hits->result());
  EXPECT_EQ(1u, resolved_hits->map().size());
  EXPECT_TRUE(
      view_token_11.Equals(resolved_hits->map().at(scene_token_1->value)));
  EXPECT_TRUE(transform_111.Equals(
      resolved_hits->result()->root->hits[0]->get_node()->transform));
  EXPECT_EQ(1u, view_inspector_.hit_tester_lookups());
  EXPECT_EQ(1u, view_inspector_.scene_lookups());

  // Do it again, should reuse the same hit tester.
  hit_tester.SetNextResult(
      MakePointF(3.f, 4.f),
      MakeSimpleHitTestResult(scene_token_1.Clone(), transform_222.Clone()));
  HitTest(MakePointF(3.f, 4.f), &resolved_hits);
  ASSERT_NE(nullptr, resolved_hits.get());
  EXPECT_NE(nullptr, resolved_hits->result());
  EXPECT_EQ(1u, resolved_hits->map().size());
  EXPECT_TRUE(
      view_token_11.Equals(resolved_hits->map().at(scene_token_1->value)));
  EXPECT_TRUE(transform_222.Equals(
      resolved_hits->result()->root->hits[0]->get_node()->transform));
  EXPECT_EQ(1u, view_inspector_.hit_tester_lookups());
  EXPECT_EQ(1u, view_inspector_.scene_lookups());

  // Replace the hit tester, ensuring that another lookup occurs.
  mozart::MockHitTester hit_tester_2;
  {
    base::RunLoop loop;
    view_tree_hit_tester_client_->set_hit_tester_changed_callback(
        loop.QuitClosure());
    view_inspector_.SetHitTester(view_tree_token_->value, &hit_tester_2);
    loop.Run();
  }

  // Try to use the new hit tester.
  hit_tester_2.SetNextResult(
      MakePointF(7.f, 8.f),
      MakeSimpleHitTestResult(scene_token_1.Clone(), transform_333.Clone()));
  HitTest(MakePointF(7.f, 8.f), &resolved_hits);
  ASSERT_NE(nullptr, resolved_hits.get());
  EXPECT_NE(nullptr, resolved_hits->result());
  EXPECT_EQ(1u, resolved_hits->map().size());
  EXPECT_TRUE(
      view_token_11.Equals(resolved_hits->map().at(scene_token_1->value)));
  EXPECT_TRUE(transform_333.Equals(
      resolved_hits->result()->root->hits[0]->get_node()->transform));
  EXPECT_EQ(2u, view_inspector_.hit_tester_lookups());
  EXPECT_EQ(1u, view_inspector_.scene_lookups());

  // Cause the hit tester to be closed.
  {
    base::RunLoop loop;
    view_tree_hit_tester_client_->set_hit_tester_changed_callback(
        loop.QuitClosure());
    view_inspector_.CloseHitTesterBindings();
    loop.Run();
  }

  // Hit testing should not work anymore.
  HitTest(MakePointF(0.f, 0.f), &resolved_hits);
  EXPECT_EQ(nullptr, resolved_hits.get());
}

}  // namespace test
