// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/ui/viewsv1/cpp/fidl.h>
#include <fuchsia/ui/viewsv1token/cpp/fidl.h>
#include <lib/app_driver/cpp/module_driver.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fsl/vmo/strings.h>
#include <lib/fxl/functional/make_copyable.h>
#include <lib/fxl/time/time_delta.h>

#include "peridot/lib/testing/reporting.h"
#include "peridot/lib/testing/testing.h"
#include "peridot/tests/chain/defs.h"
#include "peridot/tests/common/defs.h"

using modular::testing::Signal;
using modular::testing::TestPoint;

namespace {

// Cf. README.md for what this test does and how.
class TestApp {
 public:
  TestPoint initialized_{"Child module initialized"};

  TestApp(modular::ModuleHost* module_host,
          fidl::InterfaceRequest<
              fuchsia::ui::viewsv1::ViewProvider> /*view_provider_request*/)
      : module_context_(module_host->module_context()) {
    module_context_->GetComponentContext(component_context_.NewRequest());
    component_context_->GetEntityResolver(entity_resolver_.NewRequest());
    modular::testing::Init(module_host->startup_context(), __FILE__);

    initialized_.Pass();

    // Verify that the three nouns we got passed from chain_test_module appear
    // in Links we have access to, and that their contents are correct.
    VerifyLinkOne();
  }

  TestPoint stopped_{"Child module stopped"};

  // Called from ModuleDriver.
  void Terminate(const std::function<void()>& done) {
    stopped_.Pass();
    modular::testing::Done(done);
  }

 private:
  TestPoint link_one_correct_{"Link one value is correct."};

  void VerifyLinkOne() {
    module_context_->GetLink("one", link_one_.NewRequest());
    link_one_->GetEntity([this](const fidl::StringPtr& entity_reference) {
      FXL_LOG(INFO) << "*******************: " << entity_reference;
      if (!entity_reference) {
        VerifyLinkTwo();
        return;
      }
      entity_resolver_->ResolveEntity(entity_reference, entity_.NewRequest());
      entity_->GetData(
          "myType", [this](std::unique_ptr<fuchsia::mem::Buffer> content) {
            std::string content_string;
            FXL_CHECK(fsl::StringFromVmo(*content, &content_string));
            FXL_LOG(INFO) << "*******************: " << content_string;
            if (content_string == "1337") {
              link_one_correct_.Pass();
            }

            VerifyLinkTwo();
          });
    });
  }

  TestPoint link_two_correct_{"Link two value is correct."};

  void VerifyLinkTwo() {
    module_context_->GetLink("two", link_two_.NewRequest());
    link_two_->Get(nullptr,
                   [this](std::unique_ptr<fuchsia::mem::Buffer> content) {
                     std::string content_string;
                     FXL_CHECK(fsl::StringFromVmo(*content, &content_string));
                     if (content_string == "12345") {
                       link_two_correct_.Pass();
                     }

                     VerifyLinkThree();
                   });
  }

  TestPoint link_three_correct_{"Link three value is correct."};

  void VerifyLinkThree() {
    module_context_->GetLink("three", link_three_.NewRequest());
    link_three_->Get(nullptr,
                     [this](std::unique_ptr<fuchsia::mem::Buffer> content) {
                       std::string content_string;
                       FXL_CHECK(fsl::StringFromVmo(*content, &content_string));
                       if (content_string == "67890") {
                         link_three_correct_.Pass();
                       }

                       VerifyDefaultLink();
                     });
  }

  TestPoint default_link_correct_{"Default Link value is correct."};

  void VerifyDefaultLink() {
    // Check that we did get a default link as specified by the
    // fuchsia::modular::Intent.
    module_context_->GetLink(nullptr, default_link_.NewRequest());
    default_link_->Get(
        nullptr, [this](std::unique_ptr<fuchsia::mem::Buffer> content) {
          std::string content_string;
          FXL_CHECK(fsl::StringFromVmo(*content, &content_string));
          if (content_string == "1337") {
            default_link_correct_.Pass();
          }

          Signal(modular::testing::kTestShutdown);
        });
  }

  fuchsia::modular::ComponentContextPtr component_context_;
  fuchsia::modular::EntityResolverPtr entity_resolver_;
  fuchsia::modular::ModuleContext* module_context_;
  fuchsia::modular::ModuleControllerPtr child_module_;
  fuchsia::ui::viewsv1token::ViewOwnerPtr child_view_;

  fuchsia::modular::LinkPtr link_one_;
  fuchsia::modular::LinkPtr link_two_;
  fuchsia::modular::LinkPtr link_three_;
  fuchsia::modular::LinkPtr default_link_;

  fuchsia::modular::EntityPtr entity_;

  fidl::StringPtr entity_one_reference_;

  FXL_DISALLOW_COPY_AND_ASSIGN(TestApp);
};

}  // namespace

int main(int /*argc*/, const char** /*argv*/) {
  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  auto context = component::StartupContext::CreateFromStartupInfo();
  modular::ModuleDriver<TestApp> driver(context.get(),
                                        [&loop] { loop.Quit(); });
  loop.Run();
  return 0;
}
