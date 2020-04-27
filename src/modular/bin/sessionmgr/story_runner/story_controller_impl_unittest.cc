// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

////////////////////////////////////////////////////////////////////////////
// NOTE: This is an incomplete test of StoryControllerImpl. We are closer now to
// being able to construct a StoryControllerImpl without a StoryProviderImpl,
// but not yet.
//
// Fow now this only tests one public function in story_controller_impl.cc
// (ShouldRestartModuleForNewIntent).
////////////////////////////////////////////////////////////////////////////

#include "src/modular/bin/sessionmgr/story_runner/story_controller_impl.h"

#include <fuchsia/modular/cpp/fidl.h>

#include <gtest/gtest.h>

#include "src/lib/fsl/vmo/strings.h"

using fuchsia::modular::Intent;

namespace modular {
namespace {

TEST(StoryControllerImplTest, ShouldRestartModuleForNewIntent) {
  Intent one;
  Intent two;

  // Handler differs.
  one.handler = "handler1";
  two.handler = "handler2";
  EXPECT_TRUE(ShouldRestartModuleForNewIntent(one, two));
  two.handler = "handler1";
  EXPECT_FALSE(ShouldRestartModuleForNewIntent(one, two));
}

}  // namespace
}  // namespace modular
