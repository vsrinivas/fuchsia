// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/ui/views_v1/cpp/fidl.h>
#include <lib/app_driver/cpp/module_driver.h>
#include <lib/async-loop/cpp/loop.h>

#include "peridot/lib/rapidjson/rapidjson.h"
#include "peridot/lib/testing/reporting.h"
#include "peridot/lib/testing/testing.h"
#include "peridot/tests/common/defs.h"
#include "peridot/tests/link_data/defs.h"

using modular::testing::Put;
using modular::testing::Signal;

namespace {

// Cf. README.md for what this test does and how.
class TestApp {
 public:
  TestApp(modular::ModuleHost* const module_host,
          fidl::InterfaceRequest<
              fuchsia::ui::views_v1::ViewProvider> /*view_provider_request*/)
      : module_host_(module_host) {
    modular::testing::Init(module_host->startup_context(), __FILE__);
    Signal("module1_init");

    path_.push_back(kCount);

    Start();
  }

  void Start() {
    module_host_->module_context()->GetLink("link", link_.NewRequest());
    Loop();
  }

  void Loop() {
    link_->Get(path_.Clone(), [this](fidl::StringPtr value) {
      if (!value.is_null()) {
        Put("module1_link", value);
      }
      rapidjson::Document doc;
      doc.Parse(value.get().c_str());
      if (doc.IsInt()) {
        doc.SetInt(doc.GetInt() + 1);
      } else {
        doc.SetInt(0);
      }
      link_->Set(path_.Clone(), modular::JsonValueToString(doc));
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

  FXL_DISALLOW_COPY_AND_ASSIGN(TestApp);
};

}  // namespace

int main(int /*argc*/, const char** /*argv*/) {
  async::Loop loop(&kAsyncLoopConfigMakeDefault);
  auto context = component::StartupContext::CreateFromStartupInfo();
  modular::ModuleDriver<TestApp> driver(context.get(),
                                        [&loop] { loop.Quit(); });
  loop.Run();
  return 0;
}
