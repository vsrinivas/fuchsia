// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/resources/view.h"
#include "garnet/lib/ui/gfx/resources/view_holder.h"

#include <lib/async/cpp/task.h>
#include <lib/zx/eventpair.h>

#include "garnet/lib/ui/gfx/tests/session_test.h"
#include "garnet/lib/ui/gfx/tests/util.h"
#include "lib/ui/scenic/fidl_helpers.h"

namespace scenic {
namespace gfx {
namespace test {

using ViewTest = SessionTest;

TEST_F(ViewTest, CreateViewWithBadTokenDies) {
  EXPECT_DEATH_IF_SUPPORTED(
      Apply(scenic::NewCreateViewCmd(1, zx::eventpair(), "")), "");
  EXPECT_DEATH_IF_SUPPORTED(
      Apply(scenic::NewCreateViewHolderCmd(2, zx::eventpair(), "")), "");
}

TEST_F(ViewTest, Children) {
  zx::eventpair view_holder_token, view_token;
  EXPECT_EQ(ZX_OK, zx::eventpair::create(0, &view_holder_token, &view_token));

  const scenic::ResourceId view_id = 1;
  EXPECT_TRUE(Apply(
      scenic_lib::NewCreateViewCmd(view_id, std::move(view_token), "Test")));
  EXPECT_ERROR_COUNT(0);

  const scenic::ResourceId node1_id = 2;
  EXPECT_TRUE(Apply(scenic_lib::NewCreateEntityNodeCmd(node1_id)));
  EXPECT_ERROR_COUNT(0);

  const scenic::ResourceId node2_id = 3;
  EXPECT_TRUE(Apply(scenic_lib::NewCreateEntityNodeCmd(node2_id)));
  EXPECT_ERROR_COUNT(0);

  auto view = FindResource<View>(view_id);
  auto node1 = FindResource<Node>(node1_id);
  auto node2 = FindResource<Node>(node2_id);
  EXPECT_TRUE(view);
  EXPECT_TRUE(node1);
  EXPECT_TRUE(node2);

  EXPECT_TRUE(Apply(scenic_lib::NewAddChildCmd(view_id, node1_id)));
  EXPECT_ERROR_COUNT(0);

  const std::unordered_set<NodePtr>& children = view->children();
  auto child_iter = children.begin();
  std::equal_to<ResourcePtr> equal_to;
  std::hash<ResourcePtr> hash;
  EXPECT_EQ(children.size(), 1u);
  EXPECT_EQ(*child_iter, node1);
  EXPECT_TRUE(equal_to(*child_iter, node1));
  EXPECT_EQ(hash(*child_iter), hash(node1));

  EXPECT_TRUE(Apply(scenic_lib::NewAddChildCmd(view_id, node2_id)));
  EXPECT_ERROR_COUNT(0);

  child_iter = children.begin();  // Iterator was invalidated before.
  EXPECT_EQ(children.size(), 2u);
  if (*child_iter == node1) {
    child_iter++;
  }
  EXPECT_EQ(*child_iter, node2);
  EXPECT_TRUE(equal_to(*child_iter, node2));
  EXPECT_EQ(hash(*child_iter), hash(node2));
}

TEST_F(ViewTest, ExportsViewHolderViaCmd) {
  zx::eventpair view_holder_token, view_token;
  EXPECT_EQ(ZX_OK, zx::eventpair::create(0, &view_holder_token, &view_token));

  const scenic::ResourceId view_holder_id = 1;
  EXPECT_TRUE(Apply(scenic_lib::NewCreateViewHolderCmd(
      view_holder_id, std::move(view_holder_token), "Test")));
  EXPECT_ERROR_COUNT(0);

  auto view_holder = FindResource<ViewHolder>(view_holder_id);
  EXPECT_TRUE(view_holder);
  EXPECT_EQ(nullptr, view_holder->view());
  EXPECT_EQ(1u, session_->GetMappedResourceCount());
  EXPECT_EQ(1u, engine_->view_linker()->ExportCount());
  EXPECT_EQ(1u, engine_->view_linker()->UnresolvedExportCount());
  EXPECT_EQ(0u, engine_->view_linker()->ImportCount());
  EXPECT_EQ(0u, engine_->view_linker()->UnresolvedImportCount());
}

TEST_F(ViewTest, ImportsViewViaCmd) {
  zx::eventpair view_holder_token, view_token;
  EXPECT_EQ(ZX_OK, zx::eventpair::create(0, &view_holder_token, &view_token));

  const scenic::ResourceId view_id = 1;
  EXPECT_TRUE(Apply(
      scenic_lib::NewCreateViewCmd(view_id, std::move(view_token), "Test")));
  EXPECT_ERROR_COUNT(0);

  auto view = FindResource<View>(view_id);
  EXPECT_TRUE(view);
  EXPECT_EQ(nullptr, view->view_holder());
  EXPECT_EQ(1u, session_->GetMappedResourceCount());
  EXPECT_EQ(0u, engine_->view_linker()->ExportCount());
  EXPECT_EQ(0u, engine_->view_linker()->UnresolvedExportCount());
  EXPECT_EQ(1u, engine_->view_linker()->ImportCount());
  EXPECT_EQ(1u, engine_->view_linker()->UnresolvedImportCount());
}

TEST_F(ViewTest, PairedViewAndHolderAreLinked) {
  zx::eventpair view_holder_token, view_token;
  EXPECT_EQ(ZX_OK, zx::eventpair::create(0, &view_holder_token, &view_token));

  const scenic::ResourceId view_holder_id = 1u;
  EXPECT_TRUE(Apply(scenic_lib::NewCreateViewHolderCmd(
      view_holder_id, std::move(view_holder_token), "Holder [Test]")));
  EXPECT_ERROR_COUNT(0);

  auto view_holder = FindResource<ViewHolder>(view_holder_id);
  EXPECT_TRUE(view_holder);
  EXPECT_EQ(nullptr, view_holder->view());
  EXPECT_EQ(1u, session_->GetMappedResourceCount());
  EXPECT_EQ(1u, engine_->view_linker()->ExportCount());
  EXPECT_EQ(1u, engine_->view_linker()->UnresolvedExportCount());
  EXPECT_EQ(0u, engine_->view_linker()->ImportCount());
  EXPECT_EQ(0u, engine_->view_linker()->UnresolvedImportCount());

  const scenic::ResourceId view_id = 2u;
  EXPECT_TRUE(Apply(
      scenic_lib::NewCreateViewCmd(view_id, std::move(view_token), "Test")));
  EXPECT_ERROR_COUNT(0);

  auto view = FindResource<View>(view_id);
  EXPECT_TRUE(view);
  EXPECT_EQ(view.get(), view_holder->view());
  EXPECT_EQ(view_holder.get(), view->view_holder());
  EXPECT_EQ(2u, session_->GetMappedResourceCount());
  EXPECT_EQ(1u, engine_->view_linker()->ExportCount());
  EXPECT_EQ(0u, engine_->view_linker()->UnresolvedExportCount());
  EXPECT_EQ(1u, engine_->view_linker()->ImportCount());
  EXPECT_EQ(0u, engine_->view_linker()->UnresolvedImportCount());
}

TEST_F(ViewTest, ExportViewHolderWithDeadHandleFails) {
  zx::eventpair view_holder_token_out, view_token;
  {
    zx::eventpair view_holder_token;
    EXPECT_EQ(ZX_OK, zx::eventpair::create(0, &view_holder_token, &view_token));
    view_holder_token_out = zx::eventpair{view_holder_token.get()};
    // view_holder_token dies now.
  }

  const scenic::ResourceId view_holder_id = 1;
  EXPECT_FALSE(Apply(scenic_lib::NewCreateViewHolderCmd(
      view_holder_id, std::move(view_holder_token_out), "Test")));
  EXPECT_ERROR_COUNT(1);  // Dead handles cause a session error.

  auto view_holder = FindResource<ViewHolder>(view_holder_id);
  EXPECT_FALSE(view_holder);
  EXPECT_EQ(0u, session_->GetMappedResourceCount());
  EXPECT_EQ(0u, engine_->view_linker()->ExportCount());
  EXPECT_EQ(0u, engine_->view_linker()->UnresolvedExportCount());
  EXPECT_EQ(0u, engine_->view_linker()->ImportCount());
  EXPECT_EQ(0u, engine_->view_linker()->UnresolvedImportCount());
}

}  // namespace test
}  // namespace gfx
}  // namespace scenic
