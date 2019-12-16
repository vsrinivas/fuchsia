// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/ledger/cpp/fidl.h>
#include <fuchsia/ledger/internal/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sys/cpp/component_context.h>

#include <cstdlib>

#include "src/ledger/bin/app/flags.h"
#include "src/ledger/bin/platform/fd.h"
#include "src/ledger/bin/platform/platform.h"
#include "src/ledger/bin/platform/scoped_tmp_dir.h"
#include "src/ledger/lib/convert/convert.h"
#include "third_party/abseil-cpp/absl/strings/string_view.h"

namespace {

constexpr char kLedgerBinaryPath[] = "fuchsia-pkg://fuchsia.com/ledger#meta/ledger.cmx";
constexpr absl::string_view kLedgerName = "test ledger instance";

}  // namespace

// Exposes a public service that serves an in-memory Ledger.
int main(int argc, char const *argv[]) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  std::unique_ptr<sys::ComponentContext> context(sys::ComponentContext::Create());

  // Get a repository factory.
  fidl::InterfaceHandle<fuchsia::io::Directory> child_directory;
  fuchsia::sys::LaunchInfo launch_info;
  launch_info.url = kLedgerBinaryPath;
  launch_info.directory_request = child_directory.NewRequest().TakeChannel();
  // This instance exists to allow tests built outside of peridot (ie. clients of ledger) to get
  // access to a Ledger instance backed by memfs. We want this instance to use the default garbage
  // collection policy because we are testing the clients, not Ledger itself.
  ledger::AppendGarbageCollectionPolicyFlags(ledger::kDefaultGarbageCollectionPolicy, &launch_info);
  fuchsia::sys::ComponentControllerPtr controller;
  fuchsia::sys::LauncherPtr launcher;
  context->svc()->Connect(launcher.NewRequest());
  launcher->CreateComponent(std::move(launch_info), controller.NewRequest());
  fuchsia::ledger::internal::LedgerRepositoryFactoryPtr repository_factory;
  sys::ServiceDirectory child_services(std::move(child_directory));
  child_services.Connect(repository_factory.NewRequest());

  // Create a temporary location.
  std::unique_ptr<ledger::Platform> platform = ledger::MakePlatform();
  std::unique_ptr<ledger::ScopedTmpLocation> tmp_location =
      platform->file_system()->CreateScopedTmpLocation();
  zx::channel memfs_channel =
      ledger::CloneChannelFromFileDescriptor(tmp_location->path().root_fd());

  // Get a repository.
  fuchsia::ledger::internal::LedgerRepositorySyncPtr repository;
  repository_factory->GetRepository(std::move(memfs_channel), nullptr, "", repository.NewRequest());

  // Serve the repository.
  context->outgoing()->AddPublicService<fuchsia::ledger::Ledger>(
      [&repository](fidl::InterfaceRequest<fuchsia::ledger::Ledger> request) {
        repository->GetLedger(convert::ToArray(kLedgerName), std::move(request));
      });
  loop.Run();
  return EXIT_SUCCESS;
}
