// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/user_runner/puppet_master/command_runners/add_mod_command_runner.h"

#include <lib/fidl/cpp/clone.h>
#include <lib/fsl/types/type_converters.h>
#include <lib/fsl/vmo/strings.h>
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
 public:
  void SetUp() override {
    testing::TestWithSessionStorage::SetUp();
    session_storage_ = MakeSessionStorage("page");
    story_id_ = CreateStory(session_storage_.get());
    story_storage_ = GetStoryStorage(session_storage_.get(), story_id_);
    fake_module_resolver_->Connect(module_resolver_.NewRequest());
    fake_entity_resolver_->Connect(entity_resolver_.NewRequest());
    runner_ = MakeRunner(module_resolver_.get(), entity_resolver_.get());
  }

 protected:
  // This method compares intents field by field, where fuchsia::mem::Buffers
  // are compared via their contents.
  bool AreIntentsEqual(const fuchsia::modular::Intent& old_intent,
                       const fuchsia::modular::Intent& new_intent) {
    if (old_intent.handler != new_intent.handler) {
      return false;
    }

    if (old_intent.action != new_intent.action) {
      return false;
    }

    std::map<fidl::StringPtr, const fuchsia::modular::IntentParameterData*>
        old_params;
    if (old_intent.parameters) {
      for (const auto& entry : *old_intent.parameters) {
        old_params[entry.name] = &entry.data;
      }
    }

    std::map<fidl::StringPtr, const fuchsia::modular::IntentParameterData*>
        new_params;
    if (new_intent.parameters) {
      for (const auto& entry : *new_intent.parameters) {
        new_params[entry.name] = &entry.data;
      }
    }

    if (new_params.size() != old_params.size()) {
      return false;
    }

    for (const auto& entry : new_params) {
      const auto& name = entry.first;
      if (old_params.count(name) == 0) {
        return false;
      }

      const auto& new_param = *entry.second;
      const auto& old_param = *old_params[name];

      // If a parameter type changed, or a link mapping changed, we
      // need to relaunch.
      if (old_param.Which() != new_param.Which()) {
        return false;
      }

      switch (old_param.Which()) {
        case fuchsia::modular::IntentParameterData::Tag::kEntityType:
          if (old_param.entity_type() != new_param.entity_type()) {
            return false;
          }
          break;
        case fuchsia::modular::IntentParameterData::Tag::kEntityReference:
          if (old_param.entity_reference() != new_param.entity_reference()) {
            return false;
          }
        case fuchsia::modular::IntentParameterData::Tag::kJson: {
          std::string old_string;
          std::string new_string;
          if (old_param.json().size == 0 && new_param.json().size == 0) {
            break;
          }

          FXL_CHECK(fsl::StringFromVmo(old_param.json(), &old_string));
          FXL_CHECK(fsl::StringFromVmo(new_param.json(), &new_string));
          if (old_string != new_string) {
            return false;
          }
          break;
        }
        case fuchsia::modular::IntentParameterData::Tag::kLinkName:
          if (old_param.link_name() != new_param.link_name()) {
            return false;
          }
          break;
        case fuchsia::modular::IntentParameterData::Tag::kLinkPath:
          if (old_param.link_path() != new_param.link_path()) {
            return false;
          }
          break;
        case fuchsia::modular::IntentParameterData::Tag::Invalid:
          break;
      }
    }
    return true;
  }

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

  void AddParameterWithoutName(fuchsia::modular::Intent* intent) {
    fuchsia::modular::IntentParameter parameter;
    fsl::SizedVmo vmo;
    FXL_CHECK(fsl::VmoFromString("10", &vmo));
    parameter.data.set_json(std::move(vmo).ToTransport());
    intent->parameters.push_back(std::move(parameter));
  }

  void AddJsonParameter(fuchsia::modular::Intent* intent,
                        const std::string& name, const std::string& json) {
    fuchsia::modular::IntentParameter parameter;
    parameter.name = name;
    fsl::SizedVmo vmo;
    FXL_CHECK(fsl::VmoFromString(json, &vmo));
    parameter.data.set_json(std::move(vmo).ToTransport());
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

  void AddInvalidParameter(fuchsia::modular::Intent* intent,
                           const std::string& name) {
    // This parameter has no data union field set, hence it's invalid.
    fuchsia::modular::IntentParameterData data;
    fuchsia::modular::IntentParameter parameter;
    parameter.name = name;
    parameter.data = std::move(data);
    intent->parameters.push_back(std::move(parameter));
  }

  // Initializes a parent mod for the mod created during the test. The goal of
  // this mod is to test parameters of type link_name and as the
  // surface_relation_parent_mod.
  void InitParentMod(const std::string& mod_name, const std::string& param_name,
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

    SetLinkValue(story_storage_.get(), link_path, param_value);
    WriteModuleData(story_storage_.get(), std::move(module_data));
  }

  std::unique_ptr<AddModCommandRunner> runner_;
  std::unique_ptr<SessionStorage> session_storage_;
  std::unique_ptr<StoryStorage> story_storage_;
  fuchsia::modular::ModuleResolverPtr module_resolver_;
  fuchsia::modular::EntityResolverPtr entity_resolver_;
  std::string story_id_;
  std::unique_ptr<ModuleResolverFake> fake_module_resolver_ =
      std::make_unique<ModuleResolverFake>();
  std::unique_ptr<EntityResolverFake> fake_entity_resolver_ =
      std::make_unique<EntityResolverFake>();
};

TEST_F(AddModCommandRunnerTest, ExecuteIntentWithIntentHandler) {
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
  runner_->Execute(story_id_, story_storage_.get(), std::move(command),
                   [&](fuchsia::modular::ExecuteResult result) {
                     EXPECT_EQ(fuchsia::modular::ExecuteStatus::OK,
                               result.status);
                     done = true;
                   });
  RunLoopUntil([&] { return done; });

  done = false;
  fidl::VectorPtr<fidl::StringPtr> full_path{{"parent_mod", "mod"}};
  story_storage_->ReadModuleData(std::move(full_path))
      ->Then([&](fuchsia::modular::ModuleDataPtr module_data) {
        EXPECT_EQ("mod_url", module_data->module_url);
        EXPECT_EQ(full_path, module_data->module_path);
        EXPECT_FALSE(module_data->module_stopped);
        EXPECT_EQ(fuchsia::modular::ModuleSource::EXTERNAL,
                  module_data->module_source);
        EXPECT_EQ(0.5, module_data->surface_relation->emphasis);
        EXPECT_TRUE(AreIntentsEqual(intent, *module_data->intent));
        EXPECT_EQ(*manifest, *module_data->module_manifest);
        EXPECT_EQ(0u, module_data->parameter_map.entries->size());
        done = true;
      });

  RunLoopUntil([&] { return done; });
}

TEST_F(AddModCommandRunnerTest, ExecuteIntentThatNeedsResolution) {
  auto reference =
      fake_entity_resolver_->AddEntity({{"entity_type1", "entity_data"}});

  InitParentMod("parent_mod", "param", R"({"@type": "baz"})",
                "parent_link_name");

  // Set up command
  fuchsia::modular::Intent intent;
  intent.action = "intent_action";
  AddJsonParameter(&intent, "param_json", R"({"@type": "foo"})");
  AddEntityRefParameter(&intent, "param_ref", reference);
  AddEntityTypeParameter(&intent, "param_type", {"entity_type2"});
  SetLinkValue(story_storage_.get(), "link_path1", R"({"@type": "bar"})");
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
  runner_->Execute(story_id_, story_storage_.get(), std::move(command),
                   [&](fuchsia::modular::ExecuteResult result) {
                     EXPECT_EQ(fuchsia::modular::ExecuteStatus::OK,
                               result.status);
                     done = true;
                   });
  RunLoopUntil([&] { return done; });

  done = false;
  fidl::VectorPtr<fidl::StringPtr> full_path{{"parent_mod", "mod"}};
  fuchsia::modular::ModuleDataPtr module_data;
  story_storage_->ReadModuleData(std::move(full_path))
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
  EXPECT_TRUE(AreIntentsEqual(intent, *module_data->intent));
  EXPECT_EQ(*manifest, *module_data->module_manifest);
  EXPECT_EQ(5u, module_data->parameter_map.entries->size());

  // Verify parameters
  auto& link_path1 = module_data->parameter_map.entries->at(0).link_path;
  EXPECT_EQ("param_json", module_data->parameter_map.entries->at(0).name);
  EXPECT_EQ(full_path, link_path1.module_path);
  EXPECT_EQ("param_json", link_path1.link_name);
  EXPECT_EQ(R"({"@type": "foo"})",
            GetLinkValue(story_storage_.get(), link_path1));

  auto& link_path2 = module_data->parameter_map.entries->at(1).link_path;
  EXPECT_EQ("param_ref", module_data->parameter_map.entries->at(1).name);
  EXPECT_EQ(full_path, link_path2.module_path);
  EXPECT_EQ("param_ref", link_path2.link_name);
  EXPECT_EQ(R"({"@entityRef":")" + *reference + R"("})",
            GetLinkValue(story_storage_.get(), link_path2));

  auto& link_path3 = module_data->parameter_map.entries->at(2).link_path;
  EXPECT_EQ("param_type", module_data->parameter_map.entries->at(2).name);
  EXPECT_EQ(full_path, link_path3.module_path);
  EXPECT_EQ("param_type", link_path3.link_name);
  EXPECT_EQ("null", GetLinkValue(story_storage_.get(), link_path3));

  auto& link_path4 = module_data->parameter_map.entries->at(3).link_path;
  EXPECT_EQ("param_linkpath", module_data->parameter_map.entries->at(3).name);
  EXPECT_TRUE(link_path4.module_path->empty());
  EXPECT_EQ("link_path1", link_path4.link_name);
  EXPECT_EQ(R"({"@type": "bar"})",
            GetLinkValue(story_storage_.get(), link_path4));

  auto& link_path5 = module_data->parameter_map.entries->at(4).link_path;
  EXPECT_EQ("param_linkname", module_data->parameter_map.entries->at(4).name);
  EXPECT_EQ(1u, link_path5.module_path->size());
  EXPECT_EQ("parent_mod", link_path5.module_path->at(0));
  EXPECT_EQ("parent_link_name", link_path5.link_name);
  EXPECT_EQ(R"({"@type": "baz"})",
            GetLinkValue(story_storage_.get(), link_path5));
}

TEST_F(AddModCommandRunnerTest, ExecuteNoModulesFound) {
  fuchsia::modular::Intent intent;
  fuchsia::modular::AddMod add_mod;
  intent.Clone(&add_mod.intent);
  add_mod.intent.action = "intent_action";
  fuchsia::modular::StoryCommand command;
  command.set_add_mod(std::move(add_mod));

  bool done{};
  runner_->Execute(story_id_, story_storage_.get(), std::move(command),
                   [&](fuchsia::modular::ExecuteResult result) {
                     EXPECT_EQ(
                         fuchsia::modular::ExecuteStatus::NO_MODULES_FOUND,
                         result.status);
                     EXPECT_EQ("Resolution of intent gave zero results.",
                               result.error_message);
                     done = true;
                   });
  RunLoopUntil([&] { return done; });
}

TEST_F(AddModCommandRunnerTest, ExecuteInvalidParameter) {
  // Set up command
  fuchsia::modular::Intent intent;
  intent.action = "intent_action";
  intent.handler = "mod_url";
  AddInvalidParameter(&intent, "invalid_param");
  auto command = MakeAddModCommand("mod", "parent_mod", 0.5, intent);

  auto manifest = fuchsia::modular::ModuleManifest::New();
  manifest->action = "intent_action";
  manifest->binary = "mod_url";

  // Set up fake module resolver.
  fuchsia::modular::ModuleManifestPtr manifest_out;
  fidl::Clone(manifest, &manifest_out);
  fake_module_resolver_->SetManifest(std::move(manifest_out));

  // Run the command and assert results.
  bool done{};
  runner_->Execute(story_id_, story_storage_.get(), std::move(command),
                   [&](fuchsia::modular::ExecuteResult result) {
                     EXPECT_EQ(fuchsia::modular::ExecuteStatus::INVALID_COMMAND,
                               result.status);
                     EXPECT_EQ(
                         "Invalid data for parameter with name: "
                         "invalid_param",
                         result.error_message);
                     done = true;
                   });
  RunLoopUntil([&] { return done; });
}

TEST_F(AddModCommandRunnerTest, ExecuteInvalidParameterWithResulution) {
  // Set up command
  fuchsia::modular::Intent intent;
  intent.action = "intent_action";
  AddInvalidParameter(&intent, "invalid_param");
  auto command = MakeAddModCommand("mod", "parent_mod", 0.5, intent);

  // Set up fake module resolver.
  auto manifest = fuchsia::modular::ModuleManifest::New();
  manifest->action = "intent_action";
  manifest->binary = "mod_url";
  fuchsia::modular::FindModulesResult result;
  result.module_id = "mod_url";
  fidl::Clone(manifest, &result.manifest);

  fake_module_resolver_->AddFindModulesResult(std::move(result));

  // Run the command and assert results.
  bool done{};
  runner_->Execute(story_id_, story_storage_.get(), std::move(command),
                   [&](fuchsia::modular::ExecuteResult result) {
                     EXPECT_EQ(fuchsia::modular::ExecuteStatus::INVALID_COMMAND,
                               result.status);
                     EXPECT_EQ(
                         "Invalid data for parameter with name: "
                         "invalid_param",
                         result.error_message);
                     done = true;
                   });
  RunLoopUntil([&] { return done; });
}

TEST_F(AddModCommandRunnerTest, ExecuteNullHandlerAndParameter) {
  // Set up command
  fuchsia::modular::Intent intent;
  intent.action = "intent_action";
  AddParameterWithoutName(&intent);
  auto command = MakeAddModCommand("mod", "parent_mod", 0.5, intent);

  // Run the command and assert results.
  bool done{};
  runner_->Execute(story_id_, story_storage_.get(), std::move(command),
                   [&](fuchsia::modular::ExecuteResult result) {
                     EXPECT_EQ(fuchsia::modular::ExecuteStatus::INVALID_COMMAND,
                               result.status);
                     EXPECT_EQ(
                         "A null-named module parameter is not allowed "
                         "when using fuchsia::modular::Intent.action.",
                         result.error_message);
                     done = true;
                   });
  RunLoopUntil([&] { return done; });
}

TEST_F(AddModCommandRunnerTest, ExecuteNoLinkPathForLinkName) {
  // Set up command
  fuchsia::modular::Intent intent;
  intent.action = "intent_action";
  AddLinkNameParameter(&intent, "invalid_param", "parent_linkname");
  auto command = MakeAddModCommand("mod", "parent_mod", 0.5, intent);

  // Set up fake module resolver.
  auto manifest = fuchsia::modular::ModuleManifest::New();
  manifest->action = "intent_action";
  manifest->binary = "mod_url";
  fuchsia::modular::FindModulesResult result;
  result.module_id = "mod_url";
  fidl::Clone(manifest, &result.manifest);
  fake_module_resolver_->AddFindModulesResult(std::move(result));

  // Run the command and assert results.
  bool done{};
  runner_->Execute(story_id_, story_storage_.get(), std::move(command),
                   [&](fuchsia::modular::ExecuteResult result) {
                     EXPECT_EQ(fuchsia::modular::ExecuteStatus::INVALID_COMMAND,
                               result.status);
                     EXPECT_EQ(
                         "No link path found for parameter with name "
                         "invalid_param",
                         result.error_message);
                     done = true;
                   });
  RunLoopUntil([&] { return done; });
}

TEST_F(AddModCommandRunnerTest, ExecuteInvalidJsonLinkPathParam) {
  // Set up command
  fuchsia::modular::Intent intent;
  intent.action = "intent_action";
  AddLinkPathParameter(&intent, "invalid_param", "invalid_linkpath");
  auto command = MakeAddModCommand("mod", "parent_mod", 0.5, intent);

  // Set up fake module resolver.
  auto manifest = fuchsia::modular::ModuleManifest::New();
  manifest->action = "intent_action";
  manifest->binary = "mod_url";
  fuchsia::modular::FindModulesResult result;
  result.module_id = "mod_url";
  fidl::Clone(manifest, &result.manifest);
  fake_module_resolver_->AddFindModulesResult(std::move(result));

  // Run the command and assert results.
  bool done{};
  runner_->Execute(story_id_, story_storage_.get(), std::move(command),
                   [&](fuchsia::modular::ExecuteResult result) {
                     EXPECT_EQ(fuchsia::modular::ExecuteStatus::INTERNAL_ERROR,
                               result.status);
                     EXPECT_EQ(
                         "Mal-formed JSON read from link for parameter: "
                         "invalid_param",
                         result.error_message);
                     done = true;
                   });
  RunLoopUntil([&] { return done; });
}

TEST_F(AddModCommandRunnerTest, ExecuteInvalidJsonParam) {
  // Set up command
  fuchsia::modular::Intent intent;
  intent.action = "intent_action";
  AddJsonParameter(&intent, "invalid_param", "x}");
  auto command = MakeAddModCommand("mod", "parent_mod", 0.5, intent);

  // Set up fake module resolver.
  auto manifest = fuchsia::modular::ModuleManifest::New();
  manifest->action = "intent_action";
  manifest->binary = "mod_url";
  fuchsia::modular::FindModulesResult result;
  result.module_id = "mod_url";
  fidl::Clone(manifest, &result.manifest);
  fake_module_resolver_->AddFindModulesResult(std::move(result));

  // Run the command and assert results.
  bool done{};
  runner_->Execute(story_id_, story_storage_.get(), std::move(command),
                   [&](fuchsia::modular::ExecuteResult result) {
                     EXPECT_EQ(fuchsia::modular::ExecuteStatus::INVALID_COMMAND,
                               result.status);
                     EXPECT_EQ("Mal-formed JSON in parameter: invalid_param",
                               result.error_message);
                     done = true;
                   });
  RunLoopUntil([&] { return done; });
}

}  // namespace
}  // namespace modular
