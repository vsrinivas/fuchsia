// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/user_runner/puppet_master/command_runners/update_mod_command_runner.h"

#include <lib/async/cpp/future.h>
#include <lib/fsl/vmo/strings.h>

#include "gtest/gtest.h"
#include "peridot/lib/testing/test_with_session_storage.h"

namespace modular {
namespace {

class UpdateModCommandRunnerTest : public testing::TestWithSessionStorage {
 protected:
  std::unique_ptr<UpdateModCommandRunner> MakeRunner() {
    return std::make_unique<UpdateModCommandRunner>();
  }

  fuchsia::modular::ModuleData InitModuleData(
      fidl::VectorPtr<fidl::StringPtr> module_path) {
    fuchsia::modular::ModuleParameterMap parameter_map;
    fuchsia::modular::ModuleData module_data;
    module_data.module_path = std::move(module_path);
    module_data.intent = fuchsia::modular::Intent::New();
    module_data.parameter_map = std::move(parameter_map);
    return module_data;
  }

  void AddJsonParamAndCreateLink(fuchsia::modular::ModuleData* module_data,
                                 StoryStorage* const story_storage,
                                 const std::string& parameter_name,
                                 const std::string& link_path,
                                 const std::string& json_value) {
    fuchsia::modular::IntentParameter parameter;
    parameter.name = parameter_name;
    fsl::SizedVmo vmo;
    FXL_CHECK(fsl::VmoFromString(json_value, &vmo));
    parameter.data.set_json(std::move(vmo).ToTransport());
    fuchsia::modular::ModuleParameterMapEntry parameter_entry;
    parameter_entry.name = parameter_name;
    parameter_entry.link_path = MakeLinkPath(link_path);
    module_data->parameter_map.entries.push_back(std::move(parameter_entry));
    module_data->intent->parameters.push_back(std::move(parameter));
    SetLinkValue(story_storage, link_path, json_value);
  }

  void AddEntityReferenceParamAndCreateLink(
      fuchsia::modular::ModuleData* module_data,
      StoryStorage* const story_storage, const std::string& parameter_name,
      const std::string& link_path, const std::string& reference) {
    fuchsia::modular::IntentParameter parameter;
    parameter.name = parameter_name;
    parameter.data.set_entity_reference(reference);
    fuchsia::modular::ModuleParameterMapEntry parameter_entry;
    parameter_entry.name = parameter_name;
    parameter_entry.link_path = MakeLinkPath(link_path);
    module_data->parameter_map.entries.push_back(std::move(parameter_entry));
    module_data->intent->parameters.push_back(std::move(parameter));

    std::stringstream stream;
    stream << "{\"@entityRef\":\"" << reference << "\"}";
    SetLinkValue(story_storage, link_path, stream.str());
  }
};

// Verifies that an UpdateCommandMod updates the right links associated with the
// parameters.
TEST_F(UpdateModCommandRunnerTest, Execute) {
  auto storage = MakeSessionStorage("page");
  auto runner = MakeRunner();
  auto story_id = CreateStory(storage.get());
  auto story_storage = GetStoryStorage(storage.get(), story_id);
  bool done{};

  fidl::VectorPtr<fidl::StringPtr> path;
  path.push_back("mod");

  // Create a module with base parameters that will be updated.
  auto module_data = InitModuleData(path.Clone());
  AddEntityReferenceParamAndCreateLink(&module_data, story_storage.get(),
                                       "param1", "link1", "reference");
  AddJsonParamAndCreateLink(&module_data, story_storage.get(), "param2",
                            "link2", "10");
  WriteModuleData(story_storage.get(), std::move(module_data));

  // Update module parameters.
  fuchsia::modular::IntentParameter parameter1;
  parameter1.name = "param1";
  parameter1.data.set_entity_reference("reference2");
  fuchsia::modular::IntentParameter parameter2;
  parameter2.name = "param2";
  fsl::SizedVmo vmo;
  FXL_CHECK(fsl::VmoFromString("20", &vmo));
  parameter2.data.set_json(std::move(vmo).ToTransport());

  fuchsia::modular::UpdateMod update_mod;
  update_mod.mod_name = path.Clone();
  update_mod.parameters.push_back(std::move(parameter1));
  update_mod.parameters.push_back(std::move(parameter2));
  fuchsia::modular::StoryCommand command;
  command.set_update_mod(std::move(update_mod));

  runner->Execute(story_id, story_storage.get(), std::move(command),
                  [&](fuchsia::modular::ExecuteResult result) {
                    EXPECT_EQ(fuchsia::modular::ExecuteStatus::OK,
                              result.status);
                    done = true;
                  });
  RunLoopUntil([&] { return done; });

  // Verify links were updated.
  auto link1_value = GetLinkValue(story_storage.get(), "link1");
  EXPECT_EQ(link1_value, "{\"@entityRef\":\"reference2\"}");
  auto link2_value = GetLinkValue(story_storage.get(), "link2");
  EXPECT_EQ(link2_value, "20");
}

// Sets a parameter of invalid type in the command.
TEST_F(UpdateModCommandRunnerTest, ExecuteUnsupportedParameterType) {
  auto storage = MakeSessionStorage("page");
  auto runner = MakeRunner();
  auto story_id = CreateStory(storage.get());
  auto story_storage = GetStoryStorage(storage.get(), story_id);
  bool done{};

  fidl::VectorPtr<fidl::StringPtr> path;
  path.push_back("mod");

  // Create a module with base parameters that will be updated.
  auto module_data = InitModuleData(path.Clone());
  AddEntityReferenceParamAndCreateLink(&module_data, story_storage.get(),
                                       "param1", "link1", "reference");
  AddJsonParamAndCreateLink(&module_data, story_storage.get(), "param2",
                            "link2", "10");
  WriteModuleData(story_storage.get(), std::move(module_data));

  // Update module parameters.
  fuchsia::modular::IntentParameter parameter1;
  parameter1.name = "param1";
  parameter1.data.set_link_name("name2");

  fuchsia::modular::UpdateMod update_mod;
  update_mod.mod_name = path.Clone();
  update_mod.parameters.push_back(std::move(parameter1));
  fuchsia::modular::StoryCommand command;
  command.set_update_mod(std::move(update_mod));

  runner->Execute(story_id, story_storage.get(), std::move(command),
                  [&](fuchsia::modular::ExecuteResult result) {
                    EXPECT_EQ(fuchsia::modular::ExecuteStatus::INVALID_COMMAND,
                              result.status);
                    done = true;
                  });
  RunLoopUntil([&] { return done; });

  // Verify nothing changed.
  auto link1_value = GetLinkValue(story_storage.get(), "link1");
  EXPECT_EQ(link1_value, "{\"@entityRef\":\"reference\"}");
  auto link2_value = GetLinkValue(story_storage.get(), "link2");
  EXPECT_EQ(link2_value, "10");
}

TEST_F(UpdateModCommandRunnerTest, ExecuteNoModuleData) {
  auto storage = MakeSessionStorage("page");
  auto runner = MakeRunner();
  auto story_id = CreateStory(storage.get());
  auto story_storage = GetStoryStorage(storage.get(), story_id);
  bool done{};

  fidl::VectorPtr<fidl::StringPtr> path;
  path.push_back("mod");

  fuchsia::modular::UpdateMod update_mod;
  update_mod.mod_name = path.Clone();
  fuchsia::modular::StoryCommand command;
  command.set_update_mod(std::move(update_mod));

  runner->Execute(story_id, story_storage.get(), std::move(command),
                  [&](fuchsia::modular::ExecuteResult result) {
                    EXPECT_EQ(fuchsia::modular::ExecuteStatus::INVALID_COMMAND,
                              result.status);
                    EXPECT_EQ("No module data", result.error_message);
                    done = true;
                  });

  RunLoopUntil([&] { return done; });
}

}  // namespace
}  // namespace modular
