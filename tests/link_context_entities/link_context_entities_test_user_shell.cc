// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <sstream>
#include <string>

#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/ui/viewsv1token/cpp/fidl.h>
#include <lib/component/cpp/startup_context.h>
#include <lib/context/cpp/context_helper.h>
#include <lib/context/cpp/formatting.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fxl/logging.h>
#include <lib/fxl/macros.h>

#include "peridot/lib/fidl/array_to_string.h"
#include "peridot/lib/rapidjson/rapidjson.h"
#include "peridot/lib/testing/component_base.h"
#include "peridot/public/lib/integration_testing/cpp/reporting.h"
#include "peridot/public/lib/integration_testing/cpp/testing.h"
#include "peridot/tests/common/defs.h"
#include "peridot/tests/link_context_entities/defs.h"

using modular::testing::TestPoint;

namespace {

const char kStoryName[] = "story";

// A context reader watcher implementation.
class ContextListenerImpl : fuchsia::modular::ContextListener {
 public:
  ContextListenerImpl() : binding_(this) {
    handler_ = [](const fuchsia::modular::ContextValue&) {};
  }

  ~ContextListenerImpl() override = default;

  // Registers itself a watcher on the given story provider. Only one story
  // provider can be watched at a time.
  void Listen(fuchsia::modular::ContextReader* const context_reader) {
    // Subscribe to all entity values.
    fuchsia::modular::ContextSelector selector;
    selector.type = fuchsia::modular::ContextValueType::ENTITY;

    fuchsia::modular::ContextQuery query;
    modular::AddToContextQuery(&query, "all", std::move(selector));

    context_reader->Subscribe(std::move(query), binding_.NewBinding());
    binding_.set_error_handler([](zx_status_t status) {
      FXL_LOG(ERROR) << "Lost fuchsia::modular::ContextListener connection to "
                        "fuchsia::modular::ContextReader.";
    });
  }

  using Handler = std::function<void(const fuchsia::modular::ContextValue&)>;

  void Handle(const Handler& handler) { handler_ = handler; }

  // Deregisters itself from the watched story provider.
  void Reset() { binding_.Unbind(); }

 private:
  // |fuchsia::modular::ContextListener|
  void OnContextUpdate(fuchsia::modular::ContextUpdate update) override {
    FXL_LOG(INFO) << "ContextListenerImpl::OnUpdate()";
    if (auto values = modular::TakeContextValue(&update, "all")) {
      for (const auto& value : **values) {
        FXL_LOG(INFO) << "ContextListenerImpl::OnUpdate() " << value;
        handler_(value);
      }
    };
  }

  fidl::Binding<fuchsia::modular::ContextListener> binding_;
  Handler handler_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ContextListenerImpl);
};

// Cf. README.md for what this test does and how.
class TestApp : public modular::testing::ComponentBase<void> {
 public:
  TestApp(component::StartupContext* const startup_context)
      : ComponentBase(startup_context) {
    TestInit(__FILE__);

    puppet_master_ =
        startup_context
            ->ConnectToEnvironmentService<fuchsia::modular::PuppetMaster>();
    user_shell_context_ =
        startup_context
            ->ConnectToEnvironmentService<fuchsia::modular::UserShellContext>();
    user_shell_context_->GetStoryProvider(story_provider_.NewRequest());

    fuchsia::modular::IntelligenceServicesPtr intelligence_services;
    user_shell_context_->GetIntelligenceServices(
        intelligence_services.NewRequest());
    intelligence_services->GetContextReader(context_reader_.NewRequest());
    context_listener_.Listen(context_reader_.get());
    context_reader_.set_error_handler([](zx_status_t status) {
      FXL_LOG(ERROR) << "Lost fuchsia::modular::ContextReader connection.";
    });

    CreateStory();
  }

  ~TestApp() override = default;

 private:
  TestPoint create_story_{"CreateStory()"};

  void CreateStory() {
    puppet_master_->ControlStory(kStoryName, story_puppet_master_.NewRequest());

    fidl::VectorPtr<fuchsia::modular::StoryCommand> commands;
    fuchsia::modular::AddMod add_mod;
    add_mod.mod_name.push_back("root");
    add_mod.intent.handler = kModuleUrl;
    add_mod.intent.action = kModuleAction;
    add_mod.surface_parent_mod_name.resize(0);

    fuchsia::modular::StoryCommand command;
    command.set_add_mod(std::move(add_mod));
    commands.push_back(std::move(command));

    story_puppet_master_->Enqueue(std::move(commands));
    story_puppet_master_->Execute(
        [this](fuchsia::modular::ExecuteResult result) {
          create_story_.Pass();
          StartStory();
        });
  }

  TestPoint start_story_enter_{"StartStory() Enter"};
  TestPoint start_story_exit_{"StartStory() Exit"};

  void StartStory() {
    start_story_enter_.Pass();

    context_listener_.Handle(
        [this](const fuchsia::modular::ContextValue& value) {
          ProcessContextValue(value);
        });

    story_provider_->GetController(kStoryName, story_controller_.NewRequest());
    fidl::InterfaceHandle<fuchsia::ui::viewsv1token::ViewOwner> story_view;
    story_controller_->Start(story_view.NewRequest());

    start_story_exit_.Pass();
  }

  TestPoint get_context_topic_1_{"GetContextTopic() value=1"};
  int get_context_topic_1_called_{};
  TestPoint get_context_topic_2_{"GetContextTopic() value=2"};
  int get_context_topic_2_called_{};

  void ProcessContextValue(const fuchsia::modular::ContextValue& value) {
    // The context value has metadata that is derived from the story id in
    // which it was published.
    if (!value.meta.story || !value.meta.link || !value.meta.entity) {
      FXL_LOG(ERROR) << "fuchsia::modular::ContextValue missing metadata: "
                     << value;
      return;
    }

    if (value.meta.story->id != kStoryName ||
        value.meta.entity->type.is_null() ||
        value.meta.entity->type->size() != 1) {
      FXL_LOG(ERROR) << "fuchsia::modular::ContextValue metadata is incorrect: "
                     << value;
      return;
    }

    modular::JsonDoc doc;
    doc.Parse(value.content);

    if (doc.HasParseError()) {
      FXL_LOG(ERROR) << "JSON Parse Error" << value.content;
      Logout();
      return;
    }

    if (!doc.IsObject()) {
      FXL_LOG(ERROR) << "JSON not an Object" << value.content;
      Logout();
      return;
    }

    if (!doc.HasMember("value")) {
      FXL_LOG(ERROR) << "JSON missing 'value': " << value.content;
      Logout();
      return;
    }

    const std::string value_property{doc["value"].GetString()};
    const std::string type{value.meta.entity->type->at(0)};
    if (value_property != "value1" && value_property != "value2") {
      FXL_LOG(ERROR) << "JSON 'value' property (set by module) wrong: "
                     << value_property;
      Logout();
      return;
    }

    if (value_property == "value1" && type == "type1" &&
        value.meta.link->name == "link1") {
      if (++get_context_topic_1_called_ == 1) {
        get_context_topic_1_.Pass();
      }
    } else if (value_property == "value2" && type == "type2" &&
               value.meta.link->name == "link2") {
      if (++get_context_topic_2_called_ == 1) {
        get_context_topic_2_.Pass();
      }
    }

    if (get_context_topic_1_called_ > 0 && get_context_topic_2_called_ > 0) {
      context_listener_.Reset();
      context_listener_.Handle(
          [this](const fuchsia::modular::ContextValue&) {});
      Logout();
    }
  }

  void Logout() { user_shell_context_->Logout(); }

  fuchsia::modular::UserShellContextPtr user_shell_context_;
  fuchsia::modular::StoryProviderPtr story_provider_;

  fuchsia::modular::PuppetMasterPtr puppet_master_;
  fuchsia::modular::StoryPuppetMasterPtr story_puppet_master_;

  fuchsia::modular::StoryControllerPtr story_controller_;

  fuchsia::modular::ContextReaderPtr context_reader_;
  ContextListenerImpl context_listener_;

  FXL_DISALLOW_COPY_AND_ASSIGN(TestApp);
};

}  // namespace

int main(int /*argc*/, const char** /*argv*/) {
  modular::testing::ComponentMain<TestApp>();
  return 0;
}
