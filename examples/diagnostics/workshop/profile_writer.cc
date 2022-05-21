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

#include <cstdlib>
#include <iostream>

#include <src/lib/fxl/command_line.h>

void required_args() {
  FX_LOGS(FATAL) << "required args: "
                 << " --key <some_key> [--name <some_name>] [--balance value] [--delete]";
}

int main(int argc, const char **argv) {
  syslog::SetTags({"workshop", "writer"});

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  if (argc < 3 || argv[1] != std::string("--key")) {
    required_args();
  }

  std::string key = argv[2];
  std::shared_ptr<sys::ServiceDirectory> svc = sys::ServiceDirectory::CreateFromNamespace();
  fidl::SynchronousInterfacePtr<fuchsia::examples::diagnostics::ProfileStore> store;
  svc->Connect(store.NewRequest());
  fidl::SynchronousInterfacePtr<fuchsia::examples::diagnostics::Profile> profile;
  store->CreateOrOpen(key, profile.NewRequest());

  for (int i = 3; i < argc; i += 2) {
    if (argv[i] == std::string("--name")) {
      if (i + 1 >= argc) {
        required_args();
      }
      FX_LOGS(INFO) << "set name for " << key;
      profile->SetName(argv[i + 1]);
    } else if (argv[i] == std::string("--balance")) {
      if (i + 1 >= argc) {
        required_args();
      }
      std::string::size_type sz;
      int64_t balance = std::stol(argv[i + 1], &sz, 10);
      FX_LOGS(INFO) << "update balance for " << key;

      if (balance >= 0) {
        profile->AddBalance(balance);
      } else {
        bool success;
        profile->WithdrawBalance(balance, &success);
        if (!success) {
          FX_LOGS(INFO) << "cannot withdraw balance for: " << key;
        }
      }
    } else if (argv[i] == std::string("--delete")) {
      bool success;
      profile.Unbind();
      FX_LOGS(INFO) << "delete profile for " << key;

      store->Delete(key, &success);
      if (!success) {
        FX_LOGS(INFO) << "cannot delete key: " << key;
        return 1;
      }
      break;
    }
  }
  // to make sure that logs are propagated
  sleep(3);
  return 0;
}
