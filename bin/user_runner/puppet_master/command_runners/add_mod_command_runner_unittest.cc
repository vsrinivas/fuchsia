// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/user_runner/puppet_master/command_runners/add_mod_command_runner.h"

#include <lib/fidl/cpp/clone.h>
#include <lib/fsl/types/type_converters.h>
#include <lib/fxl/type_converter.h>
#include <lib/gtest/test_loop_fixture.h>

#include "gtest/gtest.h"
#include "peridot/lib/ledger_client/page_id.h"
#include "peridot/lib/testing/entity_resolver_fake.h"
#include "peridot/lib/testing/module_resolver_fake.h"
#include "peridot/lib/testing/test_with_session_storage.h"

namespace modular {
namespace {

class AddModCommandRunnerTest : public testing::TestWithSessionStorage {
 protected:
  std::unique_ptr<AddModCommandRunner> MakeRunner(
      fuchsia::modular::ModuleResolver* const module_resolver,
      fuchsia::modular::EntityResolver* const entity_resolver) {
    return std::make_unique<AddModCommandRunner>(module_resolver,
                                                 entity_resolver);
  }

  fuchsia::modular::StoryCommand MakeAddModCommand(
      const std::string& mod_name, const std::string& parent_mod_name,
      float surface_emphasis, const fuchsia::modular::Intent& intent) {
    fuchsia::modular::AddMod add_mod;
    add_mod.mod_name.reset({mod_name});
    add_mod.surface_parent_mod_name.reset({parent_mod_name});
    add_mod.surface_relation.emphasis = surface_emphasis;
    intent.Clone(&add_mod.intent);
    fuchsia::modular::StoryCommand command;
    command.set_add_mod(std::move(add_mod));
    return command;
  }

  void AddEntityRefParameter(fuchsia::modular::Intent* intent,
                             const std::string& name,
                             const std::string& reference) {
    fuchsia::modular::IntentParameter parameter;
    parameter.name = name;
    parameter.data.set_entity_reference(reference);
    intent->parameters.push_back(std::move(parameter));
  }

  void AddEntityTypeParameter(fuchsia::modular::Intent* intent,
                              const std::string& name,
                              std::vector<std::string> types) {
    fuchsia::modular::IntentParameter parameter;
    parameter.name = name;
    parameter.data.set_entity_type(
        fxl::To<fidl::VectorPtr<fidl::StringPtr>>(std::move(types)));
    intent->parameters.push_back(std::move(parameter));
  }

  void AddJsonParameter(fuchsia::modular::Intent* intent,
                        const std::string& name, const std::string& json) {
    fuchsia::modular::IntentParameter parameter;
    parameter.name = name;
    parameter.data.set_json(json);
    intent->parameters.push_back(std::move(parameter));
  }

  void AddLinkNameParameter(fuchsia::modular::Intent* intent,
                            const std::string& name,
                            const std::string& link_name) {
    fuchsia::modular::IntentParameter parameter;
    parameter.name = name;
    parameter.data.set_link_name(link_name);
    intent->parameters.push_back(std::move(parameter));
  }

  void AddLinkPathParameter(fuchsia::modular::Intent* intent,
                            const std::string& name,
                            const std::string& link_path) {
    fuchsia::modular::IntentParameter parameter;
    parameter.name = name;
    parameter.data.set_link_path(MakeLinkPath(link_path));
    intent->parameters.push_back(std::move(parameter));
  }

  // Initializes a parent mod for the mod created during the test. The goal of
  // this mod is to test parameters of type link_name and as the
  // surface_relation_parent_mod.
  void InitParentMod(StoryStorage* const story_storage,
                     const std::string& mod_name, const std::string& param_name,
                     const std::string& param_value,
                     const std::string& link_path_name) {
    fuchsia::modular::ModuleData module_data;
    module_data.module_path.push_back(mod_name);
    module_data.intent = fuchsia::modular::Intent::New();
    AddJsonParameter(module_data.intent.get(), param_name, param_value);

    fuchsia::modular::ModuleParameterMapEntry parameter_entry;
    auto link_path = MakeLinkPath(link_path_name);
    fidl::Clone(module_data.module_path, &link_path.module_path);
    parameter_entry.name = param_name;
    fidl::Clone(link_path, &parameter_entry.link_path);
    module_data.parameter_map.entries.push_back(std::move(parameter_entry));

    SetLinkValue(story_storage, link_path, param_value);
    WriteModuleData(story_storage, std::move(module_data));
  }

  std::unique_ptr<ModuleResolverFake> fake_module_resolver_ =
      std::make_unique<ModuleResolverFake>();
  std::unique_ptr<EntityResolverFake> fake_entity_resolver_ =
      std::make_unique<EntityResolverFake>();
};

TEST_F(AddModCommandRunnerTest, ExecuteIntentWithIntentHandler) {
  auto storage = MakeSessionStorage("page");
  auto story_id = CreateStory(storage.get());
  auto story_storage = GetStoryStorage(storage.get(), story_id);
  fuchsia::modular::ModuleResolverPtr module_resolver;
  fake_module_resolver_->Connect(module_resolver.NewRequest());
  fuchsia::modular::EntityResolverPtr entity_resolver;
  fake_entity_resolver_->Connect(entity_resolver.NewRequest());
  auto runner = MakeRunner(module_resolver.get(), entity_resolver.get());

  // Set up command
  fuchsia::modular::Intent intent;
  intent.action = "intent_action";
  intent.handler = "mod_url";
  intent.parameters.resize(0);
  auto command = MakeAddModCommand("mod", "parent_mod", 0.5, intent);

  auto manifest = fuchsia::modular::ModuleManifest::New();
  manifest->action = "intent_action";
  manifest->binary = "mod_url";

  // Set up fake module resolver and set validaiton of GetModuleManifest call.
  fuchsia::modular::ModuleManifestPtr manifest_out;
  fidl::Clone(manifest, &manifest_out);
  fake_module_resolver_->SetManifest(std::move(manifest_out));
  fake_module_resolver_->SetGetModuleManifestValidation(
      [&](const fidl::StringPtr& module_id) {
        EXPECT_EQ(intent.handler, module_id);
      });

  // Run the command and assert results.
  bool done{};
  runner->Execute(story_id, story_storage.get(), std::move(command),
                  [&](fuchsia::modular::ExecuteResult result) {
                    EXPECT_EQ(fuchsia::modular::ExecuteStatus::OK,
                              result.status);
                    done = true;
                  });
  RunLoopUntil([&] { return done; });

  done = false;
  fidl::VectorPtr<fidl::StringPtr> full_path{{"parent_mod", "mod"}};
  story_storage->ReadModuleData(std::move(full_path))
      ->Then([&](fuchsia::modular::ModuleDataPtr module_data) {
        EXPECT_EQ("mod_url", module_data->module_url);
        EXPECT_EQ(full_path, module_data->module_path);
        EXPECT_FALSE(module_data->module_stopped);
        EXPECT_EQ(fuchsia::modular::ModuleSource::EXTERNAL,
                  module_data->module_source);
        EXPECT_EQ(0.5, module_data->surface_relation->emphasis);
        EXPECT_EQ(intent, *module_data->intent);
        EXPECT_EQ(*manifest, *module_data->module_manifest);
        EXPECT_EQ(0u, module_data->parameter_map.entries->size());
        done = true;
      });

  RunLoopUntil([&] { return done; });
}

TEST_F(AddModCommandRunnerTest, ExecuteIntentThatNeedsResolution) {
  auto storage = MakeSessionStorage("page");
  auto story_id = CreateStory(storage.get());
  auto story_storage = GetStoryStorage(storage.get(), story_id);
  fuchsia::modular::ModuleResolverPtr module_resolver;
  fake_module_resolver_->Connect(module_resolver.NewRequest());
  fuchsia::modular::EntityResolverPtr entity_resolver;
  fake_entity_resolver_->Connect(entity_resolver.NewRequest());
  auto runner = MakeRunner(module_resolver.get(), entity_resolver.get());

  auto reference =
      fake_entity_resolver_->AddEntity({{"entity_type1", "entity_data"}});

  InitParentMod(story_storage.get(), "parent_mod", "param",
                R"({"@type": "baz"})", "parent_link_name");

  // Set up command
  fuchsia::modular::Intent intent;
  intent.action = "intent_action";
  AddJsonParameter(&intent, "param_json", R"({"@type": "foo"})");
  AddEntityRefParameter(&intent, "param_ref", reference);
  AddEntityTypeParameter(&intent, "param_type", {"entity_type2"});
  SetLinkValue(story_storage.get(), "link_path1", R"({"@type": "bar"})");
  AddLinkPathParameter(&intent, "param_linkpath", "link_path1");
  AddLinkNameParameter(&intent, "param_linkname", "parent_link_name");
  auto command = MakeAddModCommand("mod", "parent_mod", 0.5, intent);

  auto manifest = fuchsia::modular::ModuleManifest::New();
  manifest->action = "intent_action";
  manifest->binary = "mod_url";
  fuchsia::modular::FindModulesResult result;
  result.module_id = "mod_url";
  fidl::Clone(manifest, &result.manifest);

  fake_module_resolver_->AddFindModulesResult(std::move(result));
  // Since we are mocking module resolver and returning the expected |result|,
  // ensure that at least it's being called with the right parameters.
  fake_module_resolver_->SetFindModulesValidation(
      [&](const fuchsia::modular::FindModulesQuery& query) {
        EXPECT_EQ("intent_action", query.action);
        auto& constraints = query.parameter_constraints;
        EXPECT_EQ(5u, constraints->size());
        for (auto& constraint : *constraints) {
          EXPECT_EQ(1u, constraint.param_types->size());
        }
        EXPECT_EQ("param_json", constraints->at(0).param_name);
        EXPECT_EQ("foo", constraints->at(0).param_types->at(0));
        EXPECT_EQ("param_ref", constraints->at(1).param_name);
        EXPECT_EQ("entity_type1", constraints->at(1).param_types->at(0));
        EXPECT_EQ("param_ref", constraints->at(1).param_name);
        EXPECT_EQ("entity_type1", constraints->at(1).param_types->at(0));
        EXPECT_EQ("param_type", constraints->at(2).param_name);
        EXPECT_EQ("entity_type2", constraints->at(2).param_types->at(0));
        EXPECT_EQ("param_linkpath", constraints->at(3).param_name);
        EXPECT_EQ("bar", constraints->at(3).param_types->at(0));
        EXPECT_EQ("param_linkname", constraints->at(4).param_name);
        EXPECT_EQ("baz", constraints->at(4).param_types->at(0));
      });

  bool done{};
  runner->Execute(story_id, story_storage.get(), std::move(command),
                  [&](fuchsia::modular::ExecuteResult result) {
                    EXPECT_EQ(fuchsia::modular::ExecuteStatus::OK,
                              result.status);
                    done = true;
                  });
  RunLoopUntil([&] { return done; });

  done = false;
  fidl::VectorPtr<fidl::StringPtr> full_path{{"parent_mod", "mod"}};
  fuchsia::modular::ModuleDataPtr module_data;
  story_storage->ReadModuleData(std::move(full_path))
      ->Then([&](fuchsia::modular::ModuleDataPtr result) {
        module_data = std::move(result);
        done = true;
      });
  RunLoopUntil([&] { return done; });

  EXPECT_EQ("mod_url", module_data->module_url);
  EXPECT_EQ(full_path, module_data->module_path);
  EXPECT_FALSE(module_data->module_stopped);
  EXPECT_EQ(fuchsia::modular::ModuleSource::EXTERNAL,
            module_data->module_source);
  EXPECT_EQ(0.5, module_data->surface_relation->emphasis);
  EXPECT_EQ(intent, *module_data->intent);
  EXPECT_EQ(*manifest, *module_data->module_manifest);
  EXPECT_EQ(5u, module_data->parameter_map.entries->size());

  // Verify parameters
  auto& link_path1 = module_data->parameter_map.entries->at(0).link_path;
  EXPECT_EQ("param_json", module_data->parameter_map.entries->at(0).name);
  EXPECT_EQ(full_path, link_path1.module_path);
  EXPECT_EQ("param_json", link_path1.link_name);
  EXPECT_EQ(R"({"@type": "foo"})",
            GetLinkValue(story_storage.get(), link_path1));

  auto& link_path2 = module_data->parameter_map.entries->at(1).link_path;
  EXPECT_EQ("param_ref", module_data->parameter_map.entries->at(1).name);
  EXPECT_EQ(full_path, link_path2.module_path);
  EXPECT_EQ("param_ref", link_path2.link_name);
  EXPECT_EQ(R"({"@entityRef":")" + *reference + R"("})",
            GetLinkValue(story_storage.get(), link_path2));

  auto& link_path3 = module_data->parameter_map.entries->at(2).link_path;
  EXPECT_EQ("param_type", module_data->parameter_map.entries->at(2).name);
  EXPECT_EQ(full_path, link_path3.module_path);
  EXPECT_EQ("param_type", link_path3.link_name);
  EXPECT_EQ("null", GetLinkValue(story_storage.get(), link_path3));

  auto& link_path4 = module_data->parameter_map.entries->at(3).link_path;
  EXPECT_EQ("param_linkpath", module_data->parameter_map.entries->at(3).name);
  EXPECT_TRUE(link_path4.module_path->empty());
  EXPECT_EQ("link_path1", link_path4.link_name);
  EXPECT_EQ(R"({"@type": "bar"})",
            GetLinkValue(story_storage.get(), link_path4));

  auto& link_path5 = module_data->parameter_map.entries->at(4).link_path;
  EXPECT_EQ("param_linkname", module_data->parameter_map.entries->at(4).name);
  EXPECT_EQ(1u, link_path5.module_path->size());
  EXPECT_EQ("parent_mod", link_path5.module_path->at(0));
  EXPECT_EQ("parent_link_name", link_path5.link_name);
  EXPECT_EQ(R"({"@type": "baz"})",
            GetLinkValue(story_storage.get(), link_path5));
}

TEST_F(AddModCommandRunnerTest, ExecuteNoModulesFound) {
  auto storage = MakeSessionStorage("page");
  auto story_id = CreateStory(storage.get());
  auto story_storage = GetStoryStorage(storage.get(), story_id);

  fuchsia::modular::ModuleResolverPtr module_resolver;
  fake_module_resolver_->Connect(module_resolver.NewRequest());
  fuchsia::modular::EntityResolverPtr entity_resolver;
  fake_entity_resolver_->Connect(entity_resolver.NewRequest());
  auto runner = MakeRunner(module_resolver.get(), entity_resolver.get());

  fuchsia::modular::Intent intent;
  fuchsia::modular::AddMod add_mod;
  intent.Clone(&add_mod.intent);
  add_mod.intent.action = "intent_action";
  fuchsia::modular::StoryCommand command;
  command.set_add_mod(std::move(add_mod));

  bool done{};
  runner->Execute(story_id, story_storage.get(), std::move(command),
                  [&](fuchsia::modular::ExecuteResult result) {
                    EXPECT_EQ(fuchsia::modular::ExecuteStatus::NO_MODULES_FOUND,
                              result.status);
                    EXPECT_EQ("Resolution of intent gave zero results.",
                              result.error_message);
                    done = true;
                  });
  RunLoopUntil([&] { return done; });
}

}  // namespace
}  // namespace modular
