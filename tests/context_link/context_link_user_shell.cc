// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <sstream>
#include <string>

#include "lib/app/cpp/application_context.h"
#include "lib/app/cpp/connect.h"
#include "lib/app/fidl/service_provider.fidl.h"
#include "lib/app_driver/cpp/app_driver.h"
#include "lib/context/cpp/formatting.h"
#include "lib/context/fidl/context_reader.fidl.h"
#include "lib/context/fidl/context_writer.fidl.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/command_line.h"
#include "lib/fxl/functional/make_copyable.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/tasks/task_runner.h"
#include "lib/fxl/time/time_delta.h"
#include "lib/test_runner/fidl/test_runner.fidl.h"
#include "lib/ui/views/fidl/view_manager.fidl.h"
#include "lib/ui/views/fidl/view_provider.fidl.h"
#include "lib/user/fidl/focus.fidl.h"
#include "lib/user/fidl/user_shell.fidl.h"
#include "peridot/lib/fidl/array_to_string.h"
#include "peridot/lib/fidl/single_service_app.h"
#include "peridot/lib/rapidjson/rapidjson.h"
#include "peridot/lib/testing/component_base.h"
#include "peridot/lib/testing/reporting.h"
#include "peridot/lib/testing/testing.h"

namespace {

constexpr char kModuleUrl[] =
    "file:///system/test/modular_tests/context_link_module";
constexpr char kTopic[] = "link/context_link_test";
constexpr char kLink[] = "context_link";

// A context reader watcher implementation.
class ContextListenerImpl : maxwell::ContextListener {
 public:
  ContextListenerImpl() : binding_(this) {
    handler_ = [](const maxwell::ContextValuePtr&) {};
  }

  ~ContextListenerImpl() override = default;

  // Registers itself a watcher on the given story provider. Only one story
  // provider can be watched at a time.
  void Listen(maxwell::ContextReader* const context_reader) {
    // Subscribe to all entity values.
    auto selector = maxwell::ContextSelector::New();
    selector->type = maxwell::ContextValueType::ENTITY;

    auto query = maxwell::ContextQuery::New();
    query->selector["all"] = std::move(selector);

    context_reader->Subscribe(std::move(query), binding_.NewBinding());
    binding_.set_connection_error_handler([] {
      FXL_LOG(ERROR) << "Lost ContextListener connection to ContextReader.";
    });
  }

  using Handler = std::function<void(const maxwell::ContextValuePtr&)>;

  void Handle(const Handler& handler) { handler_ = handler; }

  // Deregisters itself from the watched story provider.
  void Reset() { binding_.Close(); }

 private:
  // |ContextListener|
  void OnContextUpdate(maxwell::ContextUpdatePtr update) override {
    FXL_VLOG(4) << "ContextListenerImpl::OnUpdate()";
    const auto& values = update->values["all"];
    for (const auto& value : values) {
      FXL_VLOG(4) << "ContextListenerImpl::OnUpdate() " << value;
      handler_(value);
    }
  }

  fidl::Binding<maxwell::ContextListener> binding_;
  Handler handler_;
  FXL_DISALLOW_COPY_AND_ASSIGN(ContextListenerImpl);
};

// Tests the context links machinery. We start a module that writes a context
// link and listen for the expected context topic to show up.
class TestApp : public modular::testing::ComponentBase<modular::UserShell> {
 public:
  TestApp(app::ApplicationContext* const application_context)
      : ComponentBase(application_context) { TestInit(__FILE__); }

  ~TestApp() override = default;

 private:
  using TestPoint = modular::testing::TestPoint;

  TestPoint initialize_{"Initialize()"};

  // |UserShell|
  void Initialize(fidl::InterfaceHandle<modular::UserShellContext>
                      user_shell_context) override {
    initialize_.Pass();

    user_shell_context_.Bind(std::move(user_shell_context));

    user_shell_context_->GetStoryProvider(story_provider_.NewRequest());

    user_shell_context_->GetContextReader(context_reader_.NewRequest());
    context_listener_.Listen(context_reader_.get());
    context_reader_.set_connection_error_handler(
        [] { FXL_LOG(ERROR) << "Lost ContextReader connection."; });

    CreateStory();
  }

  TestPoint create_story_{"CreateStory()"};

  void CreateStory() {
    story_provider_->CreateStory(kModuleUrl,
                                 [this](const fidl::String& story_id) {
                                   story_id_ = story_id;
                                   create_story_.Pass();
                                   StartStory();
                                 });
  }

  TestPoint start_story_enter_{"StartStory() Enter"};
  TestPoint start_story_exit_{"StartStory() Exit"};

  void StartStory() {
    start_story_enter_.Pass();

    context_listener_.Handle([this](const maxwell::ContextValuePtr& value) {
      GetContextTopic(value);
    });

    story_provider_->GetController(story_id_, story_controller_.NewRequest());

    // Start and show the new story.
    fidl::InterfaceHandle<mozart::ViewOwner> story_view;
    story_controller_->Start(story_view.NewRequest());

    start_story_exit_.Pass();
  }

  TestPoint get_context_topic_1_{"GetContextTopic() value=1"};
  int get_context_topic_1_called_{};
  TestPoint get_context_topic_2_{"GetContextTopic() value=2"};
  int get_context_topic_2_called_{};

  void GetContextTopic(const maxwell::ContextValuePtr& value) {
    // The context link value has metadata that is derived from the story id in
    // which it was published.
    if (!value->meta || !value->meta->story || !value->meta->entity) {
      FXL_LOG(ERROR) << "ContextValue missing story or entity metadata: "
                     << value;
      return;
    }

    if (value->meta->story->id != story_id_ ||
        value->meta->entity->topic != kTopic) {
      FXL_LOG(ERROR) << "ContextValue metadata is incorrect: " << value;
      return;
    }

    FXL_LOG(INFO) << "Context value for topic " << kTopic << " is: " << value;

    modular::JsonDoc doc;
    doc.Parse(value->content);

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

    if (!doc.HasMember("@source")) {
      FXL_LOG(ERROR) << "JSON missing @source";
      Logout();
      return;
    }

    if (!doc["@source"].IsObject()) {
      FXL_LOG(ERROR) << "JSON @source not an Object";
      Logout();
      return;
    }

    if (!doc["@source"].HasMember("link_name")) {
      FXL_LOG(ERROR) << "JSON @source missing link_name";
      Logout();
      return;
    }

    if (!doc["@source"]["link_name"].IsString()) {
      FXL_LOG(ERROR) << "JSON @source link_name not a string";
      Logout();
      return;
    }

    // HACK(mesch): Comparing GetString() to kLink always fails.
    const std::string link_name{doc["@source"]["link_name"].GetString()};
    if (link_name != std::string{kLink}) {
      FXL_LOG(ERROR) << "JSON @source wrong link_name " << link_name;
      Logout();
      return;
    }

    if (!doc.HasMember("link_value")) {
      FXL_LOG(ERROR) << "JSON missing property link_value (set by module)";
      Logout();
      return;
    }

    if (!doc["link_value"].IsString()) {
      FXL_LOG(ERROR) << "JSON link_value (set by module) not a String";
      Logout();
      return;
    }

    const std::string link_value{doc["link_value"].GetString()};
    if (link_value != std::string{"1"} && link_value != std::string{"2"}) {
      FXL_LOG(ERROR) << "JSON link_value (set by module) wrong: " << link_value;
      Logout();
      return;
    }

    if (link_value == std::string{"1"}) {
      if (++get_context_topic_1_called_ == 1) {
        get_context_topic_1_.Pass();
      }

    } else {
      if (++get_context_topic_2_called_ == 1) {
        get_context_topic_2_.Pass();

        context_listener_.Reset();
        context_listener_.Handle([this](const maxwell::ContextValuePtr&) {});

        Logout();
      }
    }
  }

  void Logout() { user_shell_context_->Logout(); }

  modular::UserShellContextPtr user_shell_context_;
  modular::StoryProviderPtr story_provider_;

  fidl::String story_id_;
  modular::StoryControllerPtr story_controller_;

  maxwell::ContextReaderPtr context_reader_;
  ContextListenerImpl context_listener_;

  FXL_DISALLOW_COPY_AND_ASSIGN(TestApp);
};

}  // namespace

int main(int /*argc*/, const char** /*argv*/) {
  fsl::MessageLoop loop;

  auto app_context = app::ApplicationContext::CreateFromStartupInfo();
  modular::AppDriver<TestApp> driver(
      app_context->outgoing_services(),
      std::make_unique<TestApp>(app_context.get()),
      [&loop] { loop.QuitNow(); });

  loop.Run();
  return 0;
}
