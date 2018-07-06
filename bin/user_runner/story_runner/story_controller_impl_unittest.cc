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

#include "peridot/bin/user_runner/story_runner/story_controller_impl.h"

#include <fuchsia/modular/cpp/fidl.h>

#include "gtest/gtest.h"

using fuchsia::modular::Intent;
using fuchsia::modular::IntentParameter;
using fuchsia::modular::IntentParameterData;

namespace modular {
namespace {

IntentParameter CreateLinkNameParam(std::string name, std::string link) {
  IntentParameter param;
  param.name = name;
  param.data.set_link_name(link);
  return param;
}

IntentParameter CreateLinkPathParam(std::string name, std::string link) {
  IntentParameter param;
  param.name = name;
  LinkPath path;
  path.module_path->push_back(link);
  param.data.set_link_path(std::move(path));
  return param;
}

IntentParameter CreateJsonParam(std::string name, std::string json) {
  IntentParameter param;
  param.name = name;
  param.data.set_json(json);
  return param;
}

TEST(StoryControllerImplTest, ShouldRestartModuleForNewIntent) {
  Intent one;
  Intent two;

  // Handler differs.
  one.handler = "handler1";
  two.handler = "handler2";
  EXPECT_TRUE(ShouldRestartModuleForNewIntent(one, two));
  two.handler = "handler1";
  EXPECT_FALSE(ShouldRestartModuleForNewIntent(one, two));

  // Action name differs.
  one.action = "name1";
  two.action = "name2";
  EXPECT_TRUE(ShouldRestartModuleForNewIntent(one, two));
  two.action = "name1";
  EXPECT_FALSE(ShouldRestartModuleForNewIntent(one, two));

  // Param count differs.
  one.parameters->push_back(CreateLinkNameParam("param1", "link1"));
  EXPECT_TRUE(ShouldRestartModuleForNewIntent(one, two));

  // Param link mapping differs.
  two.parameters->push_back(CreateLinkNameParam("param1", "link2"));
  EXPECT_TRUE(ShouldRestartModuleForNewIntent(one, two));
  two.parameters->clear();
  two.parameters->push_back(CreateLinkPathParam("param1", "link1"));
  EXPECT_TRUE(ShouldRestartModuleForNewIntent(one, two));

  // Now they are the same.
  two.parameters->clear();
  two.parameters->push_back(CreateLinkNameParam("param1", "link1"));
  EXPECT_FALSE(ShouldRestartModuleForNewIntent(one, two));

  // Different JSON values are OK.
  one.parameters->push_back(CreateJsonParam("param2", "json1"));
  two.parameters->push_back(CreateJsonParam("param2", "json2"));
  EXPECT_FALSE(ShouldRestartModuleForNewIntent(one, two));
}

}
}  // namespace modular
