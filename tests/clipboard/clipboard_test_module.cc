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

// The url of the clipboard agent under test.
constexpr char kClipboardAgentUrl[] = "file:///system/bin/agents/clipboard";

// A module that tests the ClipboardAgent.
class ClipboardTestApp {
 public:
  TestPoint initialized_{"Clipboard module initialized"};
  TestPoint successful_peek_{"Clipboard pushed and peeked value"};

  ClipboardTestApp(
      modular::ModuleHost* const module_host,
      fidl::InterfaceRequest<mozart::ViewProvider> /*view_provider_request*/,
      fidl::InterfaceRequest<app::ServiceProvider> /*outgoing_services*/)
      : module_host_(module_host) {
    modular::testing::Init(module_host->application_context(), __FILE__);
    initialized_.Pass();

    SetUp();

    const std::string expected_value = "hello there";
    clipboard_->Push(expected_value);
    clipboard_->Peek([this, expected_value](const fidl::String& text) {
      if (expected_value == text) {
        successful_peek_.Pass();
      }
      module_host_->module_context()->Done();
    });
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
    component_context_->ConnectToAgent(kClipboardAgentUrl,
                                       agent_services.NewRequest(),
                                       agent_controller_.NewRequest());
    ConnectToService(agent_services.get(), clipboard_.NewRequest());
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
