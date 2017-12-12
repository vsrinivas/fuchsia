// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/app/cpp/connect.h"
#include "lib/app_driver/cpp/module_driver.h"
#include "lib/clipboard/fidl/clipboard.fidl.h"
#include "lib/component/fidl/component_context.fidl.h"
#include "peridot/lib/testing/reporting.h"
#include "peridot/lib/testing/testing.h"

using modular::testing::TestPoint;

namespace {

// The path to the clipboard agent under test.
constexpr char kClipboardAgentPath[] = "file:///system/bin/agents/clipboard";

// A module that tests the ClipboardAgent.
class ClipboardTestApp {
 public:
  TestPoint initialized_{"Clipboard module initialized"};
  TestPoint first_peek_{"First peek returns empty string"};
  TestPoint peek_after_push_{"Peek after push returns pushed item"};
  ClipboardTestApp(
      modular::ModuleHost* const module_host,
      fidl::InterfaceRequest<mozart::ViewProvider> /*view_provider_request*/,
      fidl::InterfaceRequest<app::ServiceProvider> /*outgoing_services*/)
      : module_host_(module_host) {
    modular::testing::Init(module_host->application_context(), __FILE__);
    initialized_.Pass();

    SetUp();

    ExpectPeekReturnsValue("", &first_peek_);
    TestPeekAfterPush();
  }

  TestPoint stopped_{"Clipboard module stopped"};
  void Terminate(const std::function<void()>& done) {
    stopped_.Pass();
    modular::testing::Done(done);
  }

 private:
  void SetUp() {
    module_host_->module_context()->GetComponentContext(
        component_context_.NewRequest());

    app::ServiceProviderPtr agent_services;
    component_context_->ConnectToAgent(kClipboardAgentPath,
                                       agent_services.NewRequest(),
                                       agent_controller_.NewRequest());
    ConnectToService(agent_services.get(), clipboard_.NewRequest());
  }

  // Verifies that a call to |Peek()| returns |expected_value|
  // |test_point| if successful. |completed| is called once peek has returned
  // a value, regardless of the value itself.
  void ExpectPeekReturnsValue(const std::string& expected_value,
                              TestPoint* test_point,
                              const std::function<void()>& completed = [] {}) {
    clipboard_->Peek([this, expected_value, test_point,
                      completed](const ::fidl::String& text) {
      if (text == expected_value) {
        test_point->Pass();
      }
      completed();
    });
  }

  // Tests that |Peek()| returns the value previously passed to |Push()|.
  void TestPeekAfterPush() {
    std::string expected_value = "hello there";
    clipboard_->Push(expected_value);
    ExpectPeekReturnsValue(expected_value, &peek_after_push_,
                           [this] { module_host_->module_context()->Done(); });
  }

  modular::ModuleHost* const module_host_;
  modular::AgentControllerPtr agent_controller_;
  modular::ClipboardPtr clipboard_;
  modular::ComponentContextPtr component_context_;
};

}  // namespace

int main(int /*argc*/, const char** /*argv*/) {
  fsl::MessageLoop loop;
  auto app_context = app::ApplicationContext::CreateFromStartupInfo();
  modular::ModuleDriver<ClipboardTestApp> driver(app_context.get(),
                                                 [&loop] { loop.QuitNow(); });
  loop.Run();
  return 0;
}
