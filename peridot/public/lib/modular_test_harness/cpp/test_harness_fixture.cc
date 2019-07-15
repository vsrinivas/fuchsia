// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/modular_test_harness/cpp/test_harness_fixture.h>

namespace modular {
namespace testing {

TestHarnessFixture::TestHarnessFixture()
    : test_harness_launcher_(real_services()->Connect<fuchsia::sys::Launcher>()) {}

void AddModToStory(const fuchsia::modular::testing::TestHarnessPtr& test_harness,
                   std::string story_name, std::string mod_name, fuchsia::modular::Intent intent) {
  fuchsia::modular::AddMod add_mod;
  add_mod.mod_name_transitional = {mod_name};
  add_mod.intent = std::move(intent);

  fuchsia::modular::StoryCommand cmd;
  cmd.set_add_mod(std::move(add_mod));

  std::vector<fuchsia::modular::StoryCommand> cmds;
  cmds.push_back(std::move(cmd));

  // Connect to PuppetMaster and ComponentContext.
  fuchsia::modular::PuppetMasterPtr puppet_master;
  fuchsia::modular::testing::ModularService svc;
  svc.set_puppet_master(puppet_master.NewRequest());
  test_harness->ConnectToModularService(std::move(svc));

  // Create a story
  fuchsia::modular::StoryPuppetMasterPtr story_master;
  puppet_master->ControlStory(story_name, story_master.NewRequest());

  // Add the initial module to the story
  story_master->Enqueue(std::move(cmds));
  story_master->Execute([&](fuchsia::modular::ExecuteResult result) {});
}

}  // namespace testing
}  // namespace modular
