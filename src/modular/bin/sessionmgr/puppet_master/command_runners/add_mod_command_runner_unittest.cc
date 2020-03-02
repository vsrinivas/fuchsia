// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/bin/sessionmgr/puppet_master/command_runners/add_mod_command_runner.h"

#include <lib/fidl/cpp/clone.h>
#include <lib/gtest/test_loop_fixture.h>

#include "gtest/gtest.h"
#include "src/lib/fsl/types/type_converters.h"
#include "src/lib/fsl/vmo/strings.h"
#include "src/modular/lib/ledger_client/page_id.h"
#include "src/modular/lib/testing/entity_resolver_fake.h"
#include "src/modular/lib/testing/test_with_session_storage.h"

namespace modular {
namespace {

class AddModCommandRunnerTest : public modular_testing::TestWithSessionStorage {
 public:
  void SetUp() override {
    modular_testing::TestWithSessionStorage::SetUp();
    session_storage_ = MakeSessionStorage("page");
    story_id_ = CreateStory(session_storage_.get()).value_or("");
    story_storage_ = GetStoryStorage(session_storage_.get(), story_id_);
    runner_ = MakeRunner();
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

    std::map<fidl::StringPtr, const fuchsia::modular::IntentParameterData*> old_params;
    if (old_intent.parameters) {
      for (const auto& entry : *old_intent.parameters) {
        old_params[entry.name] = &entry.data;
      }
    }

    std::map<fidl::StringPtr, const fuchsia::modular::IntentParameterData*> new_params;
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
          break;
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
        case fuchsia::modular::IntentParameterData::Tag::Invalid:
          break;
      }
    }
    return true;
  }

  std::unique_ptr<AddModCommandRunner> MakeRunner() {
    return std::make_unique<AddModCommandRunner>();
  }

  fuchsia::modular::StoryCommand MakeAddModCommand(const std::string& mod_name,
                                                   const std::string& parent_mod_name,
                                                   float surface_emphasis,
                                                   const fuchsia::modular::Intent& intent) {
    fuchsia::modular::AddMod add_mod;
    add_mod.mod_name_transitional = mod_name;
    if (!parent_mod_name.empty()) {
      add_mod.surface_parent_mod_name.emplace({parent_mod_name});
    }
    add_mod.surface_relation.emphasis = surface_emphasis;
    intent.Clone(&add_mod.intent);
    fuchsia::modular::StoryCommand command;
    command.set_add_mod(std::move(add_mod));
    return command;
  }

  fuchsia::modular::Intent CreateEmptyIntent(const std::string& action,
                                             const std::string& handler = "") {
    fuchsia::modular::Intent intent;
    intent.action = "intent_action";
    if (!handler.empty()) {
      intent.handler = "mod_url";
    }
    return intent;
  }

  void AddEntityRefParameter(fuchsia::modular::Intent* intent, const std::string& name,
                             const std::string& reference) {
    fuchsia::modular::IntentParameter parameter;
    parameter.name = name;
    parameter.data.set_entity_reference(reference);
    if (!intent->parameters.has_value()) {
      intent->parameters.emplace();
    }
    intent->parameters->push_back(std::move(parameter));
  }

  void AddEntityTypeParameter(fuchsia::modular::Intent* intent, const std::string& name,
                              std::vector<std::string> types) {
    fuchsia::modular::IntentParameter parameter;
    parameter.name = name;
    parameter.data.set_entity_type(types);
    if (!intent->parameters.has_value()) {
      intent->parameters.emplace();
    }
    intent->parameters->push_back(std::move(parameter));
  }

  void AddParameterWithoutName(fuchsia::modular::Intent* intent) {
    fuchsia::modular::IntentParameter parameter;
    fsl::SizedVmo vmo;
    FXL_CHECK(fsl::VmoFromString("10", &vmo));
    parameter.data.set_json(std::move(vmo).ToTransport());
    if (!intent->parameters.has_value()) {
      intent->parameters.emplace();
    }
    intent->parameters->push_back(std::move(parameter));
  }

  void AddJsonParameter(fuchsia::modular::Intent* intent, const std::string& name,
                        const std::string& json) {
    fuchsia::modular::IntentParameter parameter;
    parameter.name = name;
    fsl::SizedVmo vmo;
    FXL_CHECK(fsl::VmoFromString(json, &vmo));
    parameter.data.set_json(std::move(vmo).ToTransport());
    if (!intent->parameters.has_value()) {
      intent->parameters.emplace();
    }
    intent->parameters->push_back(std::move(parameter));
  }

  void AddInvalidParameter(fuchsia::modular::Intent* intent, const std::string& name) {
    // This parameter has no data union field set, hence it's invalid.
    fuchsia::modular::IntentParameterData data;
    fuchsia::modular::IntentParameter parameter;
    parameter.name = name;
    parameter.data = std::move(data);
    if (!intent->parameters.has_value()) {
      intent->parameters.emplace();
    }
    intent->parameters->push_back(std::move(parameter));
  }

  // Initializes a parent mod for the mod created during the test. The goal of
  // this mod is to test parameters of type link_name and as the
  // surface_relation_parent_mod.
  void InitParentMod(const std::string& mod_name, const std::string& param_name,
                     const std::string& param_value, const std::string& link_path_name) {
    fuchsia::modular::ModuleData module_data;
    module_data.mutable_module_path()->push_back(mod_name);
    module_data.set_intent(fuchsia::modular::Intent{});
    AddJsonParameter(module_data.mutable_intent(), param_name, param_value);

    fuchsia::modular::ModuleParameterMapEntry parameter_entry;
    auto link_path = MakeLinkPath(link_path_name);
    fidl::Clone(module_data.module_path(), &link_path.module_path);
    parameter_entry.name = param_name;
    fidl::Clone(link_path, &parameter_entry.link_path);
    module_data.mutable_parameter_map()->entries.push_back(std::move(parameter_entry));

    SetLinkValue(story_storage_.get(), link_path, param_value);
    WriteModuleData(story_storage_.get(), std::move(module_data));
  }

  std::unique_ptr<AddModCommandRunner> runner_;
  std::unique_ptr<SessionStorage> session_storage_;
  std::unique_ptr<StoryStorage> story_storage_;
  std::string story_id_;
};

TEST_F(AddModCommandRunnerTest, ExecuteIntentWithIntentHandler) {
  // Set up command
  auto intent = CreateEmptyIntent("intent_action", "mod_url");
  intent.parameters.emplace();
  auto command = MakeAddModCommand("mod", "parent_mod", 0.5, intent);

  // Run the command and assert results.
  bool done = false;
  runner_->Execute(story_id_, story_storage_.get(), std::move(command),
                   [&](fuchsia::modular::ExecuteResult result) {
                     ASSERT_EQ(fuchsia::modular::ExecuteStatus::OK, result.status);
                     done = true;
                   });
  RunLoopUntil([&] { return done; });

  done = false;
  std::vector<std::string> full_path{"parent_mod", "mod"};
  story_storage_->ReadModuleData(std::move(full_path))
      ->Then([&](fuchsia::modular::ModuleDataPtr module_data) {
        EXPECT_EQ("mod_url", module_data->module_url());
        EXPECT_EQ(full_path, module_data->module_path());
        EXPECT_FALSE(module_data->module_deleted());
        EXPECT_EQ(fuchsia::modular::ModuleSource::EXTERNAL, module_data->module_source());
        EXPECT_EQ(0.5, module_data->surface_relation().emphasis);
        EXPECT_TRUE(AreIntentsEqual(intent, module_data->intent()));
        EXPECT_EQ(0u, module_data->parameter_map().entries.size());
        done = true;
      });

  RunLoopUntil([&] { return done; });
}

// Explicitly leave surface_parent_mod_name as null when providing the Intent.
// We should tolerate this, and initialize it to a zero-length vector
// internally.
TEST_F(AddModCommandRunnerTest, ExecuteIntentWithIntentHandler_NoParent) {
  auto intent = CreateEmptyIntent("intent_action", "mod_url");
  intent.parameters.emplace();
  auto command = MakeAddModCommand("mod", "" /* parent mod is null */, 0.5, intent);

  // Run the command and assert results.
  bool done = false;
  runner_->Execute(story_id_, story_storage_.get(), std::move(command),
                   [&](fuchsia::modular::ExecuteResult result) {
                     ASSERT_EQ(fuchsia::modular::ExecuteStatus::OK, result.status);
                     done = true;
                   });
  RunLoopUntil([&] { return done; });

  done = false;
  std::vector<std::string> full_path{"mod"};
  story_storage_->ReadModuleData(std::move(full_path))
      ->Then([&](fuchsia::modular::ModuleDataPtr module_data) {
        EXPECT_EQ("mod_url", module_data->module_url());
        EXPECT_EQ(full_path, module_data->module_path());
        EXPECT_FALSE(module_data->module_deleted());
        EXPECT_EQ(fuchsia::modular::ModuleSource::EXTERNAL, module_data->module_source());
        EXPECT_EQ(0.5, module_data->surface_relation().emphasis);
        EXPECT_TRUE(AreIntentsEqual(intent, module_data->intent()));
        EXPECT_EQ(0u, module_data->parameter_map().entries.size());
        done = true;
      });

  RunLoopUntil([&] { return done; });
}

TEST_F(AddModCommandRunnerTest, ExecuteNoModulesFound) {
  fuchsia::modular::Intent intent;
  fuchsia::modular::AddMod add_mod;
  intent.Clone(&add_mod.intent);
  add_mod.mod_name.push_back("mymod");
  add_mod.intent.action = "intent_action";
  fuchsia::modular::StoryCommand command;
  command.set_add_mod(std::move(add_mod));

  bool done{};
  runner_->Execute(story_id_, story_storage_.get(), std::move(command),
                   [&](fuchsia::modular::ExecuteResult result) {
                     EXPECT_EQ(fuchsia::modular::ExecuteStatus::NO_MODULES_FOUND, result.status);
                     EXPECT_EQ("Module resolution via Intent.action is deprecated.",
                               result.error_message);
                     done = true;
                   });
  RunLoopUntil([&] { return done; });
}

TEST_F(AddModCommandRunnerTest, UpdatesModIfItExists) {
  // Set up command
  auto intent = CreateEmptyIntent("intent_action", "mod_url");
  AddJsonParameter(&intent, "param_json", R"({"@type": "foo"})");
  auto command = MakeAddModCommand("mod", "parent_mod", 0.5, intent);

  // Add a mod to begin with.
  bool done{};
  runner_->Execute(story_id_, story_storage_.get(), std::move(command),
                   [&](fuchsia::modular::ExecuteResult result) {
                     ASSERT_EQ(fuchsia::modular::ExecuteStatus::OK, result.status);
                     done = true;
                   });
  RunLoopUntil([&] { return done; });

  // Get the link path for the param of the mod we created.
  fuchsia::modular::LinkPath link_path;
  std::vector<std::string> full_path{"parent_mod", "mod"};
  story_storage_->ReadModuleData(full_path)->Then([&](fuchsia::modular::ModuleDataPtr result) {
    for (auto& entry : result->parameter_map().entries) {
      if (entry.name == "param_json") {
        fidl::Clone(entry.link_path, &link_path);
      }
    }
  });

  // Create AddMod intent for a mod with the same name as the one we created
  // previously.
  auto intent2 = CreateEmptyIntent("intent_action", "mod_url");
  AddJsonParameter(&intent2, "param_json", R"({"@type": "baz"})");
  fuchsia::modular::AddMod add_mod;
  add_mod.mod_name = {"parent_mod", "mod"};
  intent2.Clone(&add_mod.intent);
  fuchsia::modular::StoryCommand command2;
  command2.set_add_mod(std::move(add_mod));

  done = false;
  runner_->Execute(story_id_, story_storage_.get(), std::move(command2),
                   [&](fuchsia::modular::ExecuteResult result) {
                     ASSERT_EQ(fuchsia::modular::ExecuteStatus::OK, result.status);
                     done = true;
                   });
  RunLoopUntil([&] { return done; });

  done = false;
  // Verify we updated an existing link.
  story_storage_->GetLinkValue(link_path)->Then(
      [&](StoryStorage::Status status, fidl::StringPtr v) {
        EXPECT_EQ(R"({"@type": "baz"})", *v);
        done = true;
      });
  RunLoopUntil([&] { return done; });
}

TEST_F(AddModCommandRunnerTest, AcceptsModNameTransitional) {
  // Set up command
  auto intent = CreateEmptyIntent("intent_action", "mod_url");
  AddJsonParameter(&intent, "param_json", R"({"@type": "foo"})");
  auto command = MakeAddModCommand("mod", "parent_mod", 0.5, intent);

  // Keep only `mod_name_transitional`
  command.add_mod().mod_name.clear();

  // Add a mod to begin with.
  bool done{};
  runner_->Execute(story_id_, story_storage_.get(), std::move(command),
                   [&](fuchsia::modular::ExecuteResult result) {
                     ASSERT_EQ(fuchsia::modular::ExecuteStatus::OK, result.status);
                     done = true;
                   });
  RunLoopUntil([&] { return done; });
}

}  // namespace
}  // namespace modular
