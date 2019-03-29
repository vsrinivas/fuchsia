// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>

#include <fuchsia/ledger/cloud/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/task.h>
#include <src/lib/fxl/logging.h>
#include <src/lib/fxl/strings/string_view.h>

#include "lib/component/cpp/startup_context.h"
#include "lib/svc/cpp/services.h"
#include "src/ledger/bin/tests/cloud_provider/launcher/validation_tests_launcher.h"

namespace {
constexpr fxl::StringView kCloudProviderUrl =
    "fuchsia-pkg://fuchsia.com/cloud_provider_in_memory#meta/"
    "cloud_provider_in_memory.cmx";
}

int main(int argc, char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  std::unique_ptr<component::StartupContext> startup_context =
      component::StartupContext::CreateFromStartupInfo();

  cloud_provider::ValidationTestsLauncher launcher(
      startup_context.get(), [&startup_context](auto request) {
        component::Services cloud_provider_services;
        fuchsia::sys::LaunchInfo launch_info;
        launch_info.url = kCloudProviderUrl.ToString();
        launch_info.directory_request = cloud_provider_services.NewRequest();
        startup_context->launcher()->CreateComponent(std::move(launch_info),
                                                     nullptr);
        cloud_provider_services.ConnectToService(
            std::move(request), fuchsia::ledger::cloud::CloudProvider::Name_);
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
