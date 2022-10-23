// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This component launches the codelab examples. The example contains directories for each part of
// the codelab, and this component accepts a command line argument to choose which part to launch.
//
// In addition to launching the codelab, this component also launches the fizzbuzz component that
// the codelab depends on.

#include <fuchsia/component/cpp/fidl.h>
#include <fuchsia/component/decl/cpp/fidl.h>
#include <fuchsia/examples/inspect/cpp/fidl.h>
#include <fuchsia/io/cpp/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/log_settings.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/result.h>

#include <thread>

void Usage(char* name) {
  FX_LOGS(ERROR)
      << "Usage: " << (name ? name : "")
      << "<string> [string...]\n string: Strings provided on the command line to reverse";
  exit(1);
}

int main(int argc, char** argv) {
  syslog::SetTags({"inspect_cpp_codelab", "client"});

  // If no url is specified, print the usage information and exit.
  if (argc < 2) {
    Usage(argc >= 1 ? argv[0] : nullptr);
  }

  fidl::SynchronousInterfacePtr<fuchsia::component::Realm> realm;
  std::string realm_service = std::string("/svc/") + fuchsia::component::Realm::Name_;
  auto status = zx::make_result(
      fdio_service_connect(realm_service.c_str(), realm.NewRequest().TakeChannel().get()));
  if (status.is_error()) {
    FX_LOGS(ERROR) << "Unable to connect to realm: " << status.status_string();
    return status.status_value();
  }

  fuchsia::io::DirectorySyncPtr exposed_dir;
  fuchsia::component::Realm_OpenExposedDir_Result result;
  status = zx::make_result(realm->OpenExposedDir(
      fuchsia::component::decl::ChildRef{.name = "reverser"}, exposed_dir.NewRequest(), &result));

  fuchsia::examples::inspect::ReverserSyncPtr reverser;
  status = zx::make_result(exposed_dir->Open(
      fuchsia::io::OpenFlags::RIGHT_READABLE | fuchsia::io::OpenFlags::RIGHT_WRITABLE,
      fuchsia::io::MODE_TYPE_SERVICE, fuchsia::examples::inspect::Reverser::Name_,
      fidl::InterfaceRequest<fuchsia::io::Node>(reverser.NewRequest().TakeChannel())));
  if (status.is_error()) {
    FX_LOGS(ERROR) << "Unable to connect to reverser: " << status.status_string();
    return status.status_value();
  }

  // Start FizzBuzz. For the purposes of the codelab.
  fuchsia::component::BinderSyncPtr binder;
  fuchsia::io::DirectorySyncPtr fizzbuzz_exposed_dir;
  fuchsia::component::Realm_OpenExposedDir_Result result_open_fizzbuzz;
  status = zx::make_result(
      realm->OpenExposedDir(fuchsia::component::decl::ChildRef{.name = "fizzbuzz"},
                            fizzbuzz_exposed_dir.NewRequest(), &result_open_fizzbuzz));
  status = zx::make_result(fizzbuzz_exposed_dir->Open(
      fuchsia::io::OpenFlags::RIGHT_READABLE | fuchsia::io::OpenFlags::RIGHT_WRITABLE,
      fuchsia::io::MODE_TYPE_SERVICE, fuchsia::component::Binder::Name_,
      fidl::InterfaceRequest<fuchsia::io::Node>(binder.NewRequest().TakeChannel())));
  if (status.is_error()) {
    FX_LOGS(ERROR) << "Unable to connect to Binder (for FizzBuzz): " << status.status_string();
    return status.status_value();
  }

  // [START reverse_loop]
  // Repeatedly send strings to be reversed to the other component.
  for (int i = 1; i < argc; i++) {
    FX_LOGS(INFO) << "Input: " << argv[i];

    std::string output;
    status = zx::make_result(reverser->Reverse(argv[i], &output));
    if (status.is_error()) {
      FX_LOGS(ERROR) << "Error: Failed to reverse string.";
      return status.status_value();
    }

    FX_LOGS(INFO) << "Output: " << output;
  }
  // [END reverse_loop]

  FX_LOGS(INFO) << "Done reversing! Please use `ffx component stop`";

  while (true) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }

  return 0;
}
