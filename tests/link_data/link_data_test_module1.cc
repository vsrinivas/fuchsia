// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/ui/viewsv1/cpp/fidl.h>
#include <lib/app_driver/cpp/module_driver.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fsl/vmo/strings.h>

#include "peridot/lib/rapidjson/rapidjson.h"
#include "peridot/public/lib/integration_testing/cpp/reporting.h"
#include "peridot/public/lib/integration_testing/cpp/testing.h"
#include "peridot/tests/common/defs.h"
#include "peridot/tests/link_data/defs.h"

using modular::testing::Put;
using modular::testing::Signal;

namespace {

// Cf. README.md for what this test does and how.
class TestModule {
 public:
  TestModule(modular::ModuleHost* const module_host,
             fidl::InterfaceRequest<
                 fuchsia::ui::app::ViewProvider> /*view_provider_request*/)
      : module_host_(module_host) {
    modular::testing::Init(module_host->startup_context(), __FILE__);
    Signal("module1_init");

    path_.push_back(kCount);

    Start();
  }

  TestModule(modular::ModuleHost* const module_host,
             fidl::InterfaceRequest<
                 fuchsia::ui::viewsv1::ViewProvider> /*view_provider_request*/)
      : TestModule(
            module_host,
            fidl::InterfaceRequest<fuchsia::ui::app::ViewProvider>(nullptr)) {}

  void Start() {
    module_host_->module_context()->GetLink(kModule1Link, link_.NewRequest());
    Loop();
  }

  void Loop() {
    link_->Get(
        path_.Clone(), [this](std::unique_ptr<fuchsia::mem::Buffer> content) {
          std::string content_string;
          if (content) {
            FXL_CHECK(fsl::StringFromVmo(*content, &content_string));
          }

          rapidjson::Document doc;
          doc.Parse(content_string);
          if (doc.IsInt()) {
            doc.SetInt(doc.GetInt() + 1);
          } else {
            doc.SetInt(0);
          }
          fsl::SizedVmo vmo;
          FXL_CHECK(fsl::VmoFromString(modular::JsonValueToString(doc), &vmo));
          link_->Set(path_.Clone(), std::move(vmo).ToTransport());
          link_->Sync([this] { Loop(); });
        });
  }

  // Called from ModuleDriver.
  void Terminate(const std::function<void()>& done) {
    modular::testing::GetStore()->Put("module1_stop", "", [] {});
    modular::testing::Done(done);
  }

 private:
  modular::ModuleHost* const module_host_;
  fuchsia::modular::LinkPtr link_;

  fidl::VectorPtr<fidl::StringPtr> path_;

  FXL_DISALLOW_COPY_AND_ASSIGN(TestModule);
};

}  // namespace

int main(int /*argc*/, const char** /*argv*/) {
  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  auto context = component::StartupContext::CreateFromStartupInfo();
  modular::ModuleDriver<TestModule> driver(context.get(),
                                           [&loop] { loop.Quit(); });
  loop.Run();
  return 0;
}
