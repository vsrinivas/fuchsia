// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/app_driver/cpp/module_driver.h"
#include "lib/entity/fidl/entity_reference_factory.fidl.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/functional/make_copyable.h"
#include "lib/fxl/tasks/task_runner.h"
#include "lib/fxl/time/time_delta.h"
#include "lib/module/fidl/module.fidl.h"
#include "lib/story/fidl/chain.fidl.h"
#include "lib/ui/views/fidl/view_token.fidl.h"
#include "peridot/lib/testing/reporting.h"
#include "peridot/lib/testing/testing.h"

using modular::testing::TestPoint;

namespace modular {
namespace {

class TestApp {
 public:
  TestApp(
      ModuleHost* module_host,
      fidl::InterfaceRequest<mozart::ViewProvider> /*view_provider_request*/,
      fidl::InterfaceRequest<app::ServiceProvider> /*outgoing_services*/)
      : module_context_(module_host->module_context()) {
    module_context_->GetComponentContext(component_context_.NewRequest());
    component_context_->GetEntityResolver(entity_resolver_.NewRequest());
    testing::Init(module_host->application_context(), __FILE__);

    initialized_.Pass();

    // Verify that the three nouns we got passed from chain_test_module appear
    // in Links we have access to, and that their contents are correct.
    VerifyLinkOne();
  }

  // Called from ModuleDriver.
  void Terminate(const std::function<void()>& done) {
    stopped_.Pass();
    testing::Done(done);
  }

 private:
  TestPoint link_one_correct_{"Link one value is correct."};
  void VerifyLinkOne() {
    module_context_->GetLink("one", link_one_.NewRequest());
    link_one_->GetEntity([this](const fidl::String& entity_reference) {
      if (!entity_reference) {
        VerifyLinkTwo();
        return;
      }
      EntityPtr entity;
      entity_resolver_->ResolveEntity(entity_reference, entity.NewRequest());
      entity->GetData("myType",
                      fxl::MakeCopyable([this, entity = std::move(entity)](
                                            const fidl::String& content) {
                        if (content == "1337") {
                          link_one_correct_.Pass();
                        }

                        VerifyLinkTwo();
                      }));
    });
  }

  TestPoint link_two_correct_{"Link two value is correct."};
  void VerifyLinkTwo() {
    module_context_->GetLink("two", link_two_.NewRequest());
    link_two_->Get(nullptr, [this](const fidl::String& content) {
      if (content == "12345") {
        link_two_correct_.Pass();
      }

      VerifyLinkThree();
    });
  }

  TestPoint link_three_correct_{"Link three value is correct."};
  void VerifyLinkThree() {
    module_context_->GetLink("three", link_three_.NewRequest());
    link_three_->Get(nullptr, [this](const fidl::String& content) {
      if (content == "67890") {
        link_three_correct_.Pass();
      }

      module_context_->Done();
    });
  }

  ComponentContextPtr component_context_;
  EntityResolverPtr entity_resolver_;
  ModuleContext* module_context_;
  ModuleControllerPtr child_module_;
  mozart::ViewOwnerPtr child_view_;

  LinkPtr link_one_;
  LinkPtr link_two_;
  LinkPtr link_three_;

  fidl::String entity_one_reference_;

  TestPoint initialized_{"Child module initialized"};
  TestPoint stopped_{"Child module stopped"};
};

}  // namespace
}  // namespace modular

int main(int /*argc*/, const char** /*argv*/) {
  fsl::MessageLoop loop;
  auto app_context = app::ApplicationContext::CreateFromStartupInfo();
  modular::ModuleDriver<modular::TestApp> driver(app_context.get(),
                                                 [&loop] { loop.QuitNow(); });
  loop.Run();
  return 0;
}
