// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/eventpair.h>

#include "garnet/lib/ui/gfx/tests/session_test.h"
#include "lib/ui/scenic/cpp/commands.h"

namespace scenic_impl {
namespace gfx {
namespace test {

class SizeChangeHintTest : public SessionTest {
 public:
  SizeChangeHintTest() {}

  std::unique_ptr<SessionForTest> CreateSession() override {
    SessionContext session_context = CreateBarebonesSessionContext();

    resource_linker_ = std::make_unique<ResourceLinker>();
    session_context.resource_linker = resource_linker_.get();

    return std::make_unique<SessionForTest>(1, std::move(session_context), this,
                                            error_reporter());
  }

  std::unique_ptr<ResourceLinker> resource_linker_;
};

TEST_F(SizeChangeHintTest, SendingSizeChangeEventWorks) {
  zx::eventpair source;
  zx::eventpair destination;
  ASSERT_EQ(ZX_OK, zx::eventpair::create(0, &source, &destination));

  // Export an entity node.
  ResourceId node_id = 1;
  ResourceId import_node_id = 2;
  ASSERT_TRUE(Apply(scenic::NewCreateEntityNodeCmd(node_id)));
  EXPECT_TRUE(Apply(scenic::NewExportResourceCmd(node_id, std::move(source))));
  EXPECT_TRUE(Apply(scenic::NewImportResourceCmd(
      import_node_id, ::fuchsia::ui::gfx::ImportSpec::NODE,
      std::move(destination))));

  ResourceId root_node_id = 3;
  ResourceId child_1_id = 4;
  ResourceId child_2_id = 5;
  ASSERT_TRUE(Apply(scenic::NewCreateEntityNodeCmd(root_node_id)));
  ASSERT_TRUE(Apply(scenic::NewCreateEntityNodeCmd(child_1_id)));
  ASSERT_TRUE(Apply(scenic::NewCreateEntityNodeCmd(child_2_id)));

  Apply(scenic::NewAddChildCmd(import_node_id, root_node_id));
  Apply(scenic::NewAddChildCmd(root_node_id, child_1_id));
  Apply(scenic::NewAddChildCmd(root_node_id, child_2_id));

  EXPECT_TRUE(Apply(scenic::NewSetEventMaskCmd(
      child_1_id, fuchsia::ui::gfx::kSizeChangeHintEventMask)));

  EXPECT_TRUE(
      Apply(scenic::NewSendSizeChangeHintCmdHACK(node_id, 3.14f, 3.14f)));

  // Run the message loop until we get an event.
  RunLoopUntilIdle();

  // Verify that we got an SizeChangeHintEvent.
  EXPECT_EQ(1u, events_.size());
  fuchsia::ui::scenic::Event event = std::move(events_[0]);
  EXPECT_EQ(fuchsia::ui::scenic::Event::Tag::kGfx, event.Which());
  EXPECT_EQ(::fuchsia::ui::gfx::Event::Tag::kSizeChangeHint,
            event.gfx().Which());
  ASSERT_EQ(child_1_id, event.gfx().size_change_hint().node_id);
  EXPECT_EQ(3.14f, event.gfx().size_change_hint().width_change_factor);
  EXPECT_EQ(3.14f, event.gfx().size_change_hint().height_change_factor);
}

}  // namespace test
}  // namespace gfx
}  // namespace scenic_impl
