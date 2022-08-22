// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/feedback/cpp/fidl.h>
#include <fuchsia/io/cpp/fidl.h>
#include <fuchsia/logger/cpp/fidl.h>
#include <fuchsia/memorypressure/cpp/fidl.h>
#include <fuchsia/posix/socket/cpp/fidl.h>
#include <fuchsia/scheduler/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <fuchsia/sysmem/cpp/fidl.h>
#include <fuchsia/tracing/provider/cpp/fidl.h>
#include <fuchsia/ui/app/cpp/fidl.h>
#include <fuchsia/ui/input/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <fuchsia/vulkan/loader/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/component/cpp/testing/realm_builder.h>
#include <lib/sys/component/cpp/testing/realm_builder_types.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/clock.h>
#include <lib/zx/time.h>
#include <zircon/status.h>
#include <zircon/types.h>
#include <zircon/utc.h>

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

#include <gtest/gtest.h>
#include <test/inputsynthesis/cpp/fidl.h>
#include <test/text/cpp/fidl.h>

#include "src/lib/testing/loop_fixture/real_loop_fixture.h"
#include "src/ui/testing/ui_test_manager/ui_test_manager.h"

namespace {

// Types imported for the realm_builder library.
using component_testing::ChildRef;
using component_testing::LocalComponent;
using component_testing::LocalComponentHandles;
using component_testing::ParentRef;
using component_testing::Protocol;
using component_testing::Realm;
using component_testing::Route;

// Max timeout in failure cases.
// Set this as low as you can that still works across all test platforms.
constexpr zx::duration kTimeout = zx::min(5);

// `ResponseListener` is a local test protocol that our test Flutter app uses to let us know
// what text is being entered into its only text field.
//
// The text field contents are reported on almost every change, so if you are entering a long
// text, you will see calls corresponding to successive additions of characters, not just the
// end result.
class TestResponseListenerServer : public test::text::ResponseListener, public LocalComponent {
 public:
  explicit TestResponseListenerServer(async_dispatcher_t* dispatcher) : dispatcher_(dispatcher) {}

  TestResponseListenerServer(const TestResponseListenerServer&) = delete;
  TestResponseListenerServer& operator=(const TestResponseListenerServer&) = delete;

  // `test.text.ResponseListener/Respond`.
  void Respond(test::text::Response response,
               test::text::ResponseListener::RespondCallback callback) override {
    FX_VLOGS(1) << "Flutter app sent: '" << response.text() << "'";
    response_ = response.text();
    callback();
  }

  // Starts this server.
  void Start(std::unique_ptr<LocalComponentHandles> handles) override {
    handles_ = std::move(handles);

    ASSERT_EQ(ZX_OK,
              handles_->outgoing()->AddPublicService(bindings_.GetHandler(this, dispatcher_)));
  }

  // Returns true if the last response received matches `expected`.  If a match is found,
  // the match is consumed, so a next call to HasResponse starts from scratch.
  bool HasResponse(const std::string& expected) {
    bool match = response_.has_value() && response_.value() == expected;
    if (match) {
      response_ = std::nullopt;
    }
    return match;
  }

 private:
  // Not owned.
  async_dispatcher_t* dispatcher_ = nullptr;
  fidl::BindingSet<test::text::ResponseListener> bindings_;
  std::unique_ptr<LocalComponentHandles> handles_;
  std::optional<std::string> response_;
};

constexpr auto kResponseListener = "test_text_response_listener";
constexpr auto kTextInputFlutter = "text_input_flutter";
static constexpr auto kTextInputFlutterUrl = "#meta/text-input-flutter-realm.cm";

constexpr auto kMemoryPressureProvider = "memory_pressure_provider";
constexpr auto kMemoryPressureProviderUrl = "#meta/memory_monitor.cm";

constexpr auto kNetstack = "netstack";
constexpr auto kNetstackUrl = "#meta/netstack.cm";

class TextInputTest : public gtest::RealLoopFixture {
 protected:
  TextInputTest()
      : test_response_listener_(std::make_unique<TestResponseListenerServer>(dispatcher())) {}

  void SetUp() override {
    // Post a "just in case" quit task, if the test hangs.
    async::PostDelayedTask(
        dispatcher(),
        [] { FX_LOGS(FATAL) << "\n\n>> Test did not complete in time, terminating.  <<\n\n"; },
        kTimeout);

    ui_testing::UITestRealm::Config config;
    config.use_flatland = true;
    config.scene_owner = ui_testing::UITestRealm::SceneOwnerType::SCENE_MANAGER;
    config.use_input = true;
    config.accessibility_owner = ui_testing::UITestRealm::AccessibilityOwnerType::FAKE;
    config.ui_to_client_services = {
        fuchsia::ui::scenic::Scenic::Name_, fuchsia::ui::composition::Flatland::Name_,
        fuchsia::ui::composition::Allocator::Name_, fuchsia::ui::input::ImeService::Name_,
        fuchsia::ui::input3::Keyboard::Name_};
    ui_test_manager_ = std::make_unique<ui_testing::UITestManager>(std::move(config));

    FX_LOGS(INFO) << "Building realm";
    realm_ = std::make_unique<Realm>(ui_test_manager_->AddSubrealm());

    realm_->AddLocalChild(kResponseListener, test_response_listener_.get());
    realm_->AddChild(kTextInputFlutter, kTextInputFlutterUrl);
    realm_->AddChild(kMemoryPressureProvider, kMemoryPressureProviderUrl);
    realm_->AddRoute(Route{.capabilities =
                               {
                                   Protocol{fuchsia::logger::LogSink::Name_},
                                   Protocol{fuchsia::scheduler::ProfileProvider::Name_},
                               },
                           .source = ParentRef(),
                           .targets = {ChildRef{kMemoryPressureProvider}}});
    realm_->AddChild(kNetstack, kNetstackUrl);
    realm_->AddRoute(Route{.capabilities = {Protocol{fuchsia::ui::app::ViewProvider::Name_}},
                           .source = ChildRef{kTextInputFlutter},
                           .targets = {ParentRef()}});
    realm_->AddRoute(Route{.capabilities =
                               {
                                   Protocol{fuchsia::ui::composition::Flatland::Name_},
                                   Protocol{fuchsia::ui::composition::Allocator::Name_},
                                   Protocol{fuchsia::ui::input::ImeService::Name_},
                                   Protocol{fuchsia::ui::input3::Keyboard::Name_},
                                   Protocol{fuchsia::ui::scenic::Scenic::Name_},
                                   // Redirect logging output for the test realm to
                                   // the host console output.
                                   Protocol{fuchsia::logger::LogSink::Name_},
                                   Protocol{fuchsia::scheduler::ProfileProvider::Name_},
                                   Protocol{fuchsia::sysmem::Allocator::Name_},
                                   Protocol{fuchsia::tracing::provider::Registry::Name_},
                                   Protocol{fuchsia::vulkan::loader::Loader::Name_},
                                   Protocol{fuchsia::feedback::CrashReporter::Name_},
                               },
                           .source = ParentRef(),
                           .targets = {ChildRef{kTextInputFlutter}}});
    realm_->AddRoute(Route{.capabilities = {Protocol{fuchsia::memorypressure::Provider::Name_}},
                           .source = ChildRef{kMemoryPressureProvider},
                           .targets = {ChildRef{kTextInputFlutter}}});
    realm_->AddRoute(Route{.capabilities = {Protocol{fuchsia::posix::socket::Provider::Name_}},
                           .source = ChildRef{kNetstack},
                           .targets = {ChildRef{kTextInputFlutter}}});
    realm_->AddRoute(Route{.capabilities =
                               {
                                   Protocol{test::text::ResponseListener::Name_},
                               },
                           .source = ChildRef{kResponseListener},
                           .targets = {ChildRef{kTextInputFlutter}}});

    ui_test_manager_->BuildRealm();
    realm_exposed_services_ = ui_test_manager_->CloneExposedServicesDirectory();

    // Initialize scene, and attach client view.
    ui_test_manager_->InitializeScene();
    FX_LOGS(INFO) << "Wait for client view to render";
    RunLoopUntil([this]() { return ui_test_manager_->ClientViewIsRendering(); });
  }

  sys::ServiceDirectory* realm_exposed_services() { return realm_exposed_services_.get(); }

  std::unique_ptr<ui_testing::UITestManager> ui_test_manager_;
  std::unique_ptr<sys::ServiceDirectory> realm_exposed_services_;
  std::unique_ptr<Realm> realm_;

  std::unique_ptr<TestResponseListenerServer> test_response_listener_;
};

TEST_F(TextInputTest, FlutterTextFieldEntry) {
  FX_LOGS(INFO) << "Wait for the initial text response";
  RunLoopUntil([&] { return test_response_listener_->HasResponse(""); });

  // If the child has rendered, this means the flutter app is alive. Yay!
  //
  // Now, send it some text. test_response_listener_ will eventually contain
  // the entire response.

  auto input_synthesis = realm_exposed_services()->Connect<test::inputsynthesis::Text>();

  FX_LOGS(INFO) << "Sending a text message";
  bool done = false;
  input_synthesis->Send("Hello\nworld!", [&done]() { done = true; });
  RunLoopUntil([&] { return done; });

  FX_LOGS(INFO) << "Message was sent";

  // Sadly, we can only wait until test timeout if this fails.
  RunLoopUntil([&] { return test_response_listener_->HasResponse("Hello\nworld!"); });

  FX_LOGS(INFO) << "Done";
}

}  // namespace
