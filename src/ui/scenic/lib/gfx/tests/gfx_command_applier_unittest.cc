// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/gfx/engine/gfx_command_applier.h"

#include "gtest/gtest.h"
#include "lib/ui/scenic/cpp/commands.h"
#include "src/ui/scenic/lib/gfx/tests/session_test.h"

namespace scenic_impl {
namespace gfx {
namespace test {

using GfxCommandApplierTest = SessionTest;

TEST_F(GfxCommandApplierTest, NewCreateEntityNodeCmd) {
  CommandContext empty_command_context;

  // Valid id passes
  EXPECT_TRUE(GfxCommandApplier::ApplyCommand(session(), &empty_command_context,
                                              scenic::NewCreateEntityNodeCmd(/*id*/ 1)));

  // Invalid id fails
  EXPECT_FALSE(GfxCommandApplier::ApplyCommand(session(), &empty_command_context,
                                               scenic::NewCreateEntityNodeCmd(/*id*/ 0)));
}

TEST_F(GfxCommandApplierTest, EraseResource) {
  CommandContext empty_command_context;
  EXPECT_TRUE(GfxCommandApplier::ApplyCommand(session(), &empty_command_context,
                                              scenic::NewCreateEntityNodeCmd(/*id*/ 3)));
  // Erasing non-existent resource fails
  EXPECT_FALSE(GfxCommandApplier::ApplyCommand(session(), &empty_command_context,
                                               scenic::NewReleaseResourceCmd(/*id*/ 2)));

  // Erasing existing resource passes
  EXPECT_TRUE(GfxCommandApplier::ApplyCommand(session(), &empty_command_context,
                                              scenic::NewReleaseResourceCmd(/*id*/ 3)));
}

TEST_F(GfxCommandApplierTest, SeparateSessionsAreIndependent) {
  auto session2 = CreateSession();

  CommandContext empty_command_context;

  EXPECT_TRUE(GfxCommandApplier::ApplyCommand(session(), &empty_command_context,
                                              scenic::NewCreateEntityNodeCmd(/*id*/ 3)));
  EXPECT_FALSE(GfxCommandApplier::ApplyCommand(session2.get(), &empty_command_context,
                                               scenic::NewReleaseResourceCmd(/*id*/ 3)));
  EXPECT_TRUE(GfxCommandApplier::ApplyCommand(session2.get(), &empty_command_context,
                                              scenic::NewCreateEntityNodeCmd(/*id*/ 3)));
  EXPECT_TRUE(GfxCommandApplier::ApplyCommand(session(), &empty_command_context,
                                              scenic::NewReleaseResourceCmd(/*id*/ 3)));
  EXPECT_TRUE(GfxCommandApplier::ApplyCommand(session2.get(), &empty_command_context,
                                              scenic::NewReleaseResourceCmd(/*id*/ 3)));
}

TEST_F(GfxCommandApplierTest, NansWillFailToBeApplied) {
  const ResourceId id = 1;
  EXPECT_FALSE(Apply(scenic::NewCreateRoundedRectangleCmd(id, NAN, 40.f, 2.f, 4.f, 6.f, 8.f)));
  EXPECT_FALSE(Apply(scenic::NewCreateRectangleCmd(id, NAN, 8.f)));
  EXPECT_FALSE(Apply(scenic::NewCreateCircleCmd(id, NAN)));
}

}  // namespace test
}  // namespace gfx
}  // namespace scenic_impl
