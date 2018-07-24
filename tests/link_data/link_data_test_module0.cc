// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// A Module that serves as the recipe in the example story, i.e. that
// creates other Modules in the story.

#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/ui/viewsv1/cpp/fidl.h>
#include <lib/app_driver/cpp/module_driver.h>
#include <lib/async-loop/cpp/loop.h>

#include "peridot/lib/rapidjson/rapidjson.h"
#include "peridot/lib/testing/reporting.h"
#include "peridot/lib/testing/testing.h"
#include "peridot/tests/common/defs.h"
#include "peridot/tests/link_data/defs.h"

using modular::testing::Signal;

namespace {

// Implementation of the fuchsia::modular::LinkWatcher service that forwards the
// value of one fuchsia::modular::Link instance to a second
// fuchsia::modular::Link instance whenever it changes.
class LinkForwarder : fuchsia::modular::LinkWatcher {
 public:
  LinkForwarder(fuchsia::modular::Link* const src,
                fuchsia::modular::Link* const dst)
      : src_binding_(this), src_(src), dst_(dst) {
    src_->Watch(src_binding_.NewBinding());
  }

  // |fuchsia::modular::LinkWatcher|
  void Notify(fidl::StringPtr json) override { dst_->Set(nullptr, json); }

 private:
  fidl::Binding<fuchsia::modular::LinkWatcher> src_binding_;
  fuchsia::modular::Link* const src_;
  fuchsia::modular::Link* const dst_;

  FXL_DISALLOW_COPY_AND_ASSIGN(LinkForwarder);
};

// Cf. README.md for what this test does and how.
class TestApp {
 public:
  TestApp(modular::ModuleHost* const module_host,
          fidl::InterfaceRequest<
              fuchsia::ui::viewsv1::ViewProvider> /*view_provider_request*/)
      : module_host_(module_host),
        module_context_(module_host_->module_context()) {
    modular::testing::Init(module_host->startup_context(), __FILE__);
    Signal("module0_init");

    Start();
  }

  void Start() {
    // Read null link data, and send its value back to the test user shell to
    // verify its expected value. Nb. the user shell does this only for the
    // first invocation. Therefore, it would be wrong to verify this with a
    // TestPoint.
    module_context_->GetLink(nullptr, link_.NewRequest());
    link_->Get(nullptr, [this](fidl::StringPtr value) {
      if (value == kRootJson1) {
        Signal(std::string("module0_link") + ":" + kRootJson1);
      }

      StartModules();
    });
  }

  void StartModules() {
    module_context_->GetLink(kModule1Link, module1_link_.NewRequest());
    module_context_->GetLink(kModule2Link, module2_link_.NewRequest());

    fuchsia::modular::IntentParameterData parameter_data;
    parameter_data.set_link_name(kModule1Link);

    fuchsia::modular::IntentParameter parameter;
    parameter.name = kLink;
    parameter.data = std::move(parameter_data);

    fuchsia::modular::Intent intent;
    intent.handler = kModule1Url;
    intent.parameters.push_back(std::move(parameter));

    module_context_->StartModule("module1", std::move(intent),
                                 module1_.NewRequest(), nullptr,
                                 [](fuchsia::modular::StartModuleStatus) {});

    parameter_data = fuchsia::modular::IntentParameterData();
    parameter_data.set_link_name(kModule2Link);

    parameter = fuchsia::modular::IntentParameter();
    parameter.name = kLink;
    parameter.data = std::move(parameter_data);

    intent = fuchsia::modular::Intent();
    intent.handler = kModule2Url;
    intent.parameters.push_back(std::move(parameter));

    module_context_->StartModule("module2", std::move(intent),
                                 module2_.NewRequest(), nullptr,
                                 [](fuchsia::modular::StartModuleStatus) {});

    connections_.emplace_back(
        new LinkForwarder(module1_link_.get(), module2_link_.get()));
  }

  // Called from ModuleDriver.
  void Terminate(const std::function<void()>& done) {
    Signal("module1_stop");
    modular::testing::Done(done);
  }

 private:
  modular::ModuleHost* const module_host_;
  fuchsia::modular::ModuleContext* const module_context_;

  fuchsia::modular::LinkPtr link_;

  fuchsia::modular::ModuleControllerPtr module1_;
  fuchsia::modular::LinkPtr module1_link_;

  fuchsia::modular::ModuleControllerPtr module2_;
  fuchsia::modular::LinkPtr module2_link_;

  std::vector<std::unique_ptr<LinkForwarder>> connections_;

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
