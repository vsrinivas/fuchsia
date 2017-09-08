// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <sstream>
#include <string>

#include "application/lib/app/connect.h"
#include "application/services/service_provider.fidl.h"
#include "apps/maxwell/services/context/context_publisher.fidl.h"
#include "apps/maxwell/services/context/context_reader.fidl.h"
#include "apps/modular/lib/fidl/array_to_string.h"
#include "apps/modular/lib/fidl/single_service_app.h"
#include "apps/modular/lib/rapidjson/rapidjson.h"
#include "apps/modular/lib/testing/component_base.h"
#include "apps/modular/lib/testing/reporting.h"
#include "apps/modular/lib/testing/testing.h"
#include "apps/modular/services/user/focus.fidl.h"
#include "apps/modular/services/user/user_shell.fidl.h"
#include "lib/ui/views/fidl/view_manager.fidl.h"
#include "lib/ui/views/fidl/view_provider.fidl.h"
#include "apps/test_runner/services/test_runner.fidl.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/ftl/command_line.h"
#include "lib/ftl/functional/make_copyable.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/tasks/task_runner.h"
#include "lib/ftl/time/time_delta.h"
#include "lib/mtl/tasks/message_loop.h"

namespace {

constexpr char kModuleUrl[] =
    "file:///system/apps/modular_tests/context_link_module";
constexpr char kTopic[] = "context_link_test";
constexpr char kLink[] = "context_link";

// A context reader watcher implementation.
class ContextListenerForTopicsImpl : maxwell::ContextListenerForTopics {
 public:
  ContextListenerForTopicsImpl() : binding_(this) {
    handler_ = [](fidl::String, fidl::String) {};
  }

  ~ContextListenerForTopicsImpl() override = default;

  // Registers itself a watcher on the given story provider. Only one story
  // provider can be watched at a time.
  void Listen(maxwell::ContextReader* const context_reader) {
    auto query = maxwell::ContextQueryForTopics::New();
    query->topics.resize(0);
    context_reader->SubscribeToTopics(std::move(query), binding_.NewBinding());
    binding_.set_connection_error_handler(
        [] { FTL_LOG(WARNING) << "Lost connection to ContextReader."; });
  }

  using Handler = std::function<void(fidl::String, fidl::String)>;

  void Handle(const Handler& handler) { handler_ = handler; }

  // Deregisters itself from the watched story provider.
  void Reset() { binding_.Close(); }

 private:
  // |ContextListenerForTopics|
  void OnUpdate(maxwell::ContextUpdateForTopicsPtr update) override {
    const auto& values = update->values;
    for (auto i = values.cbegin(); i != values.cend(); ++i) {
      handler_(i.GetKey(), i.GetValue());
    }
  }

  fidl::Binding<maxwell::ContextListenerForTopics> binding_;
  Handler handler_;
  FTL_DISALLOW_COPY_AND_ASSIGN(ContextListenerForTopicsImpl);
};

// Tests the context links machinery. We start a module that writes a context
// link and listen for the expected context topic to show up.
class TestApp : modular::testing::ComponentBase<modular::UserShell> {
 public:
  // The app instance must be dynamic, because it needs to do several things
  // after its own constructor is invoked. It accomplishes that by being able to
  // call delete this. Cf. Terminate().
  static void New() {
    new TestApp;  // will delete itself in Terminate().
  }

 private:
  TestApp() { TestInit(__FILE__); }

  ~TestApp() override = default;

  using TestPoint = modular::testing::TestPoint;

  TestPoint initialize_{"Initialize()"};

  // |UserShell|
  void Initialize(fidl::InterfaceHandle<modular::UserShellContext>
                      user_shell_context) override {
    initialize_.Pass();

    user_shell_context_.Bind(std::move(user_shell_context));

    user_shell_context_->GetStoryProvider(story_provider_.NewRequest());

    // HACK(mesch): If done here, context reader connection just closes.
    // user_shell_context_->GetContextReader(context_reader_.NewRequest());
    // context_listener_.Listen(context_reader_.get());

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

    user_shell_context_->GetContextReader(context_reader_.NewRequest());
    context_listener_.Listen(context_reader_.get());
    context_listener_.Handle(
        [this](const fidl::String& key, const fidl::String& value) {
          GetContextTopic(key, value);
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

  void GetContextTopic(const fidl::String& topic, const fidl::String& value) {
    // The context link topic is derived from the story id in which it was
    // published.
    std::ostringstream expected_topic;
    expected_topic << "/story/id/" << story_id_ << "/link/" << kTopic;
    if (topic != expected_topic.str()) {
      return;
    }

    FTL_LOG(INFO) << "Context value for topic " << kTopic << " is: " << value;

    modular::JsonDoc doc;
    doc.Parse(value);

    if (doc.HasParseError()) {
      FTL_LOG(ERROR) << "JSON Parse Error";
      Logout();
      return;
    }

    if (!doc.IsObject()) {
      FTL_LOG(ERROR) << "JSON not an Object";
      Logout();
      return;
    }

    if (!doc.HasMember("@source")) {
      FTL_LOG(ERROR) << "JSON missing @source";
      Logout();
      return;
    }

    if (!doc["@source"].IsObject()) {
      FTL_LOG(ERROR) << "JSON @source not an Object";
      Logout();
      return;
    }

    if (!doc["@source"].HasMember("link_name")) {
      FTL_LOG(ERROR) << "JSON @source missing link_name";
      Logout();
      return;
    }

    if (!doc["@source"]["link_name"].IsString()) {
      FTL_LOG(ERROR) << "JSON @source link_name not a string";
      Logout();
      return;
    }

    // HACK(mesch): Comparing GetString() to kLink always fails.
    const std::string link_name{doc["@source"]["link_name"].GetString()};
    if (link_name != std::string{kLink}) {
      FTL_LOG(ERROR) << "JSON @source wrong link_name " << link_name;
      Logout();
      return;
    }

    if (!doc.HasMember("link_value")) {
      FTL_LOG(ERROR) << "JSON missing property link_value (set by module)";
      Logout();
      return;
    }

    if (!doc["link_value"].IsString()) {
      FTL_LOG(ERROR) << "JSON link_value (set by module) not a String";
      Logout();
      return;
    }

    const std::string link_value{doc["link_value"].GetString()};
    if (link_value != std::string{"1"} && link_value != std::string{"2"}) {
      FTL_LOG(ERROR) << "JSON link_value (set by module) wrong: " << link_value;
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
        context_listener_.Handle(
            [this](const fidl::String& key, const fidl::String& value) {});

        Logout();
      }
    }
  }

  void Logout() { user_shell_context_->Logout(); }

  TestPoint terminate_{"Terminate()"};

  // |UserShell|
  void Terminate() override {
    terminate_.Pass();
    DeleteAndQuit();
  }

  modular::UserShellContextPtr user_shell_context_;
  modular::StoryProviderPtr story_provider_;

  fidl::String story_id_;
  modular::StoryControllerPtr story_controller_;

  maxwell::ContextReaderPtr context_reader_;
  ContextListenerForTopicsImpl context_listener_;

  FTL_DISALLOW_COPY_AND_ASSIGN(TestApp);
};

}  // namespace

int main(int /*argc*/, const char** /*argv*/) {
  mtl::MessageLoop loop;
  TestApp::New();
  loop.Run();
  return 0;
}
