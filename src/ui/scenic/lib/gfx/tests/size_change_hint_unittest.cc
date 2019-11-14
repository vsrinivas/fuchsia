// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/eventpair.h>

#include "lib/ui/scenic/cpp/commands.h"
#include "src/ui/scenic/lib/gfx/tests/session_test.h"
#include "sdk/lib/ui/scenic/cpp/view_token_pair.h"

namespace scenic_impl {
namespace gfx {
namespace test {

class SizeChangeHintTest : public SessionTest {
 public:
  SizeChangeHintTest() {}

  void TearDown() override {
    SessionTest::TearDown();
    view_linker_.reset();
  }

  SessionContext CreateSessionContext() override {
    SessionContext session_context = SessionTest::CreateSessionContext();

    FXL_DCHECK(!view_linker_);

    view_linker_ = std::make_unique<ViewLinker>();
    session_context.view_linker = view_linker_.get();

    return session_context;
  }

  std::unique_ptr<ViewLinker> view_linker_;
};

TEST_F(SizeChangeHintTest, SendingSizeChangeEventWorks) {
  zx::eventpair source;
  zx::eventpair destination;
  ASSERT_EQ(ZX_OK, zx::eventpair::create(0, &source, &destination));

  // Export an entity node.
  const ResourceId node_id = 1;
  const ResourceId view_holder_id = 2;
  const ResourceId view_id = 3;
  ASSERT_TRUE(Apply(scenic::NewCreateEntityNodeCmd(node_id)));

  auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();
  EXPECT_TRUE(Apply(scenic::NewCreateViewHolderCmd(
      view_holder_id, std::move(view_holder_token), "MyViewHolder")));
  EXPECT_TRUE(Apply(
      scenic::NewCreateViewCmd(view_id, std::move(view_token), "MyView")));

  // Run the message loop to flush out View-related events that we don't
  // care about.
  RunLoopUntilIdle();
  ClearEvents();

  Apply(scenic::NewAddChildCmd(node_id, view_holder_id));

  const ResourceId root_node_id = 4;
  const ResourceId child_1_id = 5;
  const ResourceId child_2_id = 6;
  ASSERT_TRUE(Apply(scenic::NewCreateEntityNodeCmd(root_node_id)));
  ASSERT_TRUE(Apply(scenic::NewCreateEntityNodeCmd(child_1_id)));
  ASSERT_TRUE(Apply(scenic::NewCreateEntityNodeCmd(child_2_id)));

  Apply(scenic::NewAddChildCmd(view_id, root_node_id));
  Apply(scenic::NewAddChildCmd(root_node_id, child_1_id));
  Apply(scenic::NewAddChildCmd(root_node_id, child_2_id));

  EXPECT_TRUE(
      Apply(scenic::NewSetEventMaskCmd(child_1_id, fuchsia::ui::gfx::kSizeChangeHintEventMask)));

  EXPECT_TRUE(Apply(scenic::NewSendSizeChangeHintCmdHACK(node_id, 3.14f, 3.14f)));

  // Run the message loop until we get an event.
  RunLoopUntilIdle();

  // Verify that we got an SizeChangeHintEvent.
  EXPECT_EQ(1u, events().size());
  auto& event = std::move(events()[0]);
  EXPECT_EQ(fuchsia::ui::scenic::Event::Tag::kGfx, event.Which());
  EXPECT_EQ(::fuchsia::ui::gfx::Event::Tag::kSizeChangeHint, event.gfx().Which());
  ASSERT_EQ(child_1_id, event.gfx().size_change_hint().node_id);
  EXPECT_EQ(3.14f, event.gfx().size_change_hint().width_change_factor);
  EXPECT_EQ(3.14f, event.gfx().size_change_hint().height_change_factor);
}

}  // namespace test
}  // namespace gfx
}  // namespace scenic_impl
