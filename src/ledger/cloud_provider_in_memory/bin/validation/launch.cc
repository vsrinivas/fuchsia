// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/ledger/cloud/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/sys/cpp/service_directory.h>

#include <iostream>

#include "src/ledger/bin/tests/cloud_provider/launcher/validation_tests_launcher.h"
#include "src/ledger/lib/convert/convert.h"
#include "src/lib/fxl/logging.h"
#include "third_party/abseil-cpp/absl/strings/string_view.h"

namespace {
constexpr absl::string_view kCloudProviderUrl =
    "fuchsia-pkg://fuchsia.com/cloud_provider_in_memory#meta/"
    "cloud_provider_in_memory.cmx";
}  // namespace

int main(int argc, char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  std::unique_ptr<sys::ComponentContext> component_context = sys::ComponentContext::Create();
  fuchsia::sys::LauncherPtr component_launcher;
  component_context->svc()->Connect(component_launcher.NewRequest());
  cloud_provider::ValidationTestsLauncher launcher(
      loop.dispatcher(), component_context.get(),
      [component_launcher = std::move(component_launcher)](auto request) {
        fuchsia::sys::LaunchInfo launch_info;
        launch_info.url = convert::ToString(kCloudProviderUrl);
        auto cloud_provider_services =
            sys::ServiceDirectory::CreateWithRequest(&launch_info.directory_request);

        fuchsia::sys::ComponentControllerPtr controller;
        component_launcher->CreateComponent(std::move(launch_info), controller.NewRequest());
        cloud_provider_services->Connect(std::move(request),
                                         fuchsia::ledger::cloud::CloudProvider::Name_);
        return controller;
      });

  int32_t return_code = -1;
  async::PostTask(loop.dispatcher(), [&launcher, &return_code, &loop] {
    launcher.Run({}, [&return_code, &loop](int32_t result) {
      return_code = result;
      loop.Quit();
    });
  });
  loop.Run();
  return return_code;
}
