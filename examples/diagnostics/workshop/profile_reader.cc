// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/examples/diagnostics/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/cpp/interface_ptr.h>
#include <lib/fidl/cpp/synchronous_interface_ptr.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/syslog/cpp/log_settings.h>
#include <lib/syslog/cpp/macros.h>
#include <unistd.h>

#include <iostream>
#include <vector>

#include <src/lib/fxl/command_line.h>

int main(int argc, const char** argv) {
  syslog::SetTags({"workshop", "reader"});

  if (argc < 3) {
    FX_LOGS(FATAL) << "required args: "
                   << " --key <some_key> [--key <some_key>]";
  }

  std::vector<std::string> keys;
  for (int i = 1; i < argc; i += 2) {
    if (std::string("--key") != argv[i] || i + 1 >= argc) {
      FX_LOGS(FATAL) << "required args: "
                     << " --key <some_key> [--key <some_key>]";
    }
    keys.push_back(argv[i + 1]);
  }

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  std::shared_ptr<sys::ServiceDirectory> svc = sys::ServiceDirectory::CreateFromNamespace();
  auto store = svc->Connect<fuchsia::examples::diagnostics::ProfileStore>();
  fidl::SynchronousInterfacePtr<fuchsia::examples::diagnostics::ProfileReader> profile;
  for (auto& key : keys) {
    FX_LOGS(INFO) << "Get profile for key: " << key;
    store->OpenReader(key, profile.NewRequest());
    std::string name;
    int64_t balance;
    auto status = profile->GetName(&name);
    if (status != ZX_OK) {
      FX_LOGS(INFO) << "Cannot find profile for key: " << key;
    } else {
      profile->GetBalance(&balance);
      FX_LOGS(INFO) << "\nRead\nName: " << name << "\nBalance: " << balance;
    }
  }
  // to make sure that logs are propagated
  sleep(3);
  return 0;
}
