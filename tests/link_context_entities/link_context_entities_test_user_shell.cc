// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <sstream>
#include <string>

#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/ui/views_v1_token/cpp/fidl.h>
#include <lib/component/cpp/startup_context.h>
#include <lib/context/cpp/context_helper.h>
#include <lib/context/cpp/formatting.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fxl/logging.h>
#include <lib/fxl/macros.h>

#include "peridot/lib/fidl/array_to_string.h"
#include "peridot/lib/rapidjson/rapidjson.h"
#include "peridot/lib/testing/component_base.h"
#include "peridot/lib/testing/reporting.h"
#include "peridot/lib/testing/testing.h"
#include "peridot/tests/common/defs.h"
#include "peridot/tests/link_context_entities/defs.h"

using modular::testing::TestPoint;

namespace {

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
    binding_.set_error_handler([] {
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
    const auto& values = modular::TakeContextValue(&update, "all");
    for (const auto& value : *values.second) {
      FXL_LOG(INFO) << "ContextListenerImpl::OnUpdate() " << value;
      handler_(value);
    }
  }

  fidl::Binding<fuchsia::modular::ContextListener> binding_;
  Handler handler_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ContextListenerImpl);
};

// Cf. README.md for what this test does and how.
class TestApp
    : public modular::testing::ComponentBase<fuchsia::modular::UserShell> {
 public:
  TestApp(component::StartupContext* const startup_context)
      : ComponentBase(startup_context) {
    TestInit(__FILE__);
  }

  ~TestApp() override = default;

 private:
  TestPoint initialize_{"Initialize()"};

  // |fuchsia::modular::UserShell|
  void Initialize(fidl::InterfaceHandle<fuchsia::modular::UserShellContext>
                      user_shell_context) override {
    initialize_.Pass();

    user_shell_context_.Bind(std::move(user_shell_context));

    user_shell_context_->GetStoryProvider(story_provider_.NewRequest());

    fuchsia::modular::IntelligenceServicesPtr intelligence_services;
    user_shell_context_->GetIntelligenceServices(
        intelligence_services.NewRequest());
    intelligence_services->GetContextReader(context_reader_.NewRequest());
    context_listener_.Listen(context_reader_.get());
    context_reader_.set_error_handler([] {
      FXL_LOG(ERROR) << "Lost fuchsia::modular::ContextReader connection.";
    });

    CreateStory();
  }

  TestPoint create_story_{"CreateStory()"};

  void CreateStory() {
    story_provider_->CreateStory(kModuleUrl, [this](fidl::StringPtr story_id) {
      story_id_ = story_id;
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

    story_provider_->GetController(story_id_, story_controller_.NewRequest());

    // Start and show the new story.
    fidl::InterfaceHandle<fuchsia::ui::views_v1_token::ViewOwner> story_view;
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

    if (value.meta.story->id != story_id_ ||
        value.meta.entity->type.is_null() ||
        value.meta.entity->type->size() != 1) {
      FXL_LOG(ERROR) << "fuchsia::modular::ContextValue metadata is incorrect: "
                     << value;
      return;
    }

    modular::JsonDoc doc;
    doc.Parse(value.content);

    if (doc.HasParseError()) {
      FXL_LOG(ERROR) << "JSON Parse Error";
      Logout();
      return;
    }

    if (!doc.IsObject()) {
      FXL_LOG(ERROR) << "JSON not an Object";
      Logout();
      return;
    }

    if (!doc.HasMember("value")) {
      FXL_LOG(ERROR) << "JSON missing 'value'";
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

  fidl::StringPtr story_id_;
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
