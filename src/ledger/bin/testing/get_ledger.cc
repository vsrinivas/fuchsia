// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/testing/get_ledger.h"

#include <fcntl.h>
#include <fuchsia/ledger/internal/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/fit/defer.h>
#include <lib/fit/function.h>

#include <utility>

#include "src/ledger/bin/app/flags.h"
#include "src/ledger/bin/fidl/include/types.h"
#include "src/ledger/bin/platform/fd.h"
#include "src/ledger/bin/storage/public/types.h"
#include "src/ledger/lib/convert/convert.h"
#include "src/ledger/lib/files/detached_path.h"
#include "src/ledger/lib/files/unique_fd.h"
#include "src/ledger/lib/logging/logging.h"

namespace ledger {
namespace {
// Converts a status returned by Ledger via FIDL to a Status.
// The convention is that kernel errors (zx_status_t) are negative, while
// positive values are reserved for user-space.
Status ToLedgerStatus(zx_status_t status) {
  if (status == ZX_OK) {
    return Status::OK;
  }
  if (status > 0) {
    return static_cast<Status>(status);
  }
  return Status::INTERNAL_ERROR;
}
}  // namespace

Status GetLedger(sys::ComponentContext* context,
                 fidl::InterfaceRequest<fuchsia::sys::ComponentController> controller_request,
                 cloud_provider::CloudProviderPtr cloud_provider, std::string user_id,
                 std::string ledger_name, const DetachedPath& ledger_repository_path,
                 fit::function<void()> error_handler, LedgerPtr* ledger,
                 storage::GarbageCollectionPolicy gc_policy,
                 fit::function<void(fit::closure)>* close_repository) {
  unique_fd dir(
      openat(ledger_repository_path.root_fd(), ledger_repository_path.path().c_str(), O_RDONLY));
  if (!dir.is_valid()) {
    LEDGER_LOG(ERROR) << "Unable to open directory at " << ledger_repository_path.path()
                      << ". errno: " << errno;
    return Status::IO_ERROR;
  }

  ledger_internal::LedgerRepositoryFactoryPtr repository_factory;

  fidl::InterfaceHandle<fuchsia::io::Directory> child_directory;
  fuchsia::sys::LaunchInfo launch_info;
  launch_info.url = "fuchsia-pkg://fuchsia.com/ledger#meta/ledger.cmx";
  launch_info.directory_request = child_directory.NewRequest().TakeChannel();
  AppendGarbageCollectionPolicyFlags(gc_policy, &launch_info);
  launch_info.arguments->push_back("--verbose=" +
                                   std::to_string(-static_cast<int>(GetLogSeverity())));
  fuchsia::sys::LauncherPtr launcher;
  context->svc()->Connect(launcher.NewRequest());
  launcher->CreateComponent(std::move(launch_info), std::move(controller_request));
  sys::ServiceDirectory child_services(std::move(child_directory));
  child_services.Connect(repository_factory.NewRequest());

  fuchsia::ledger::internal::LedgerRepositorySyncPtr repository;

  repository_factory->GetRepository(CloneChannelFromFileDescriptor(dir.get()),
                                    std::move(cloud_provider), std::move(user_id),
                                    repository.NewRequest());

  (*ledger).set_error_handler([error_handler = std::move(error_handler)](zx_status_t status) {
    LEDGER_LOG(ERROR) << "The ledger connection was closed, quitting.";
    error_handler();
  });
  repository->GetLedger(convert::ToArray(ledger_name), ledger->NewRequest());

  Status status = ToLedgerStatus(repository->Sync());

  fuchsia::ledger::internal::LedgerRepositoryPtr async_repository;
  async_repository.Bind(repository.Unbind());

  if (close_repository) {
    *close_repository = [async_repository = std::move(async_repository)](fit::closure cb) mutable {
      async_repository->Close();
      async_repository.set_error_handler([cb = std::move(cb)](zx_status_t status) { cb(); });
    };
  }

  return status;
}

void KillLedgerProcess(fuchsia::sys::ComponentControllerPtr* controller) {
  (*controller)->Kill();
  auto channel = controller->Unbind().TakeChannel();
  zx_signals_t observed;
  channel.wait_one(ZX_CHANNEL_PEER_CLOSED, zx::deadline_after(zx::sec(5)), &observed);
}

}  // namespace ledger
