// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/testing/get_ledger.h"

#include <fcntl.h>
#include <fuchsia/ledger/internal/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/callback/capture.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/fit/function.h>
#include <lib/fsl/io/fd.h>
#include <lib/svc/cpp/services.h>
#include <src/lib/fxl/logging.h>

#include <utility>

#include "peridot/lib/convert/convert.h"
#include "src/ledger/bin/fidl/include/types.h"
#include "src/ledger/bin/filesystem/detached_path.h"
#include "src/lib/files/unique_fd.h"

namespace ledger {
namespace {
// Converts a status returned by Ledger via FIDL to a ledger::Status.
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
                 fidl::InterfaceRequest<fuchsia::sys::ComponentController>
                     controller_request,
                 cloud_provider::CloudProviderPtr cloud_provider,
                 std::string user_id, std::string ledger_name,
                 const DetachedPath& ledger_repository_path,
                 fit::function<void()> error_handler, LedgerPtr* ledger) {
  fxl::UniqueFD dir(openat(ledger_repository_path.root_fd(),
                           ledger_repository_path.path().c_str(), O_RDONLY));
  if (!dir.is_valid()) {
    FXL_LOG(ERROR) << "Unable to open directory at "
                   << ledger_repository_path.path() << ". errno: " << errno;
    return Status::IO_ERROR;
  }

  ledger_internal::LedgerRepositoryFactoryPtr repository_factory;

  component::Services child_services;
  fuchsia::sys::LaunchInfo launch_info;
  launch_info.url = "fuchsia-pkg://fuchsia.com/ledger#meta/ledger.cmx";
  launch_info.directory_request = child_services.NewRequest();
  launch_info.arguments.push_back("--disable_reporting");
  fuchsia::sys::LauncherPtr launcher;
  context->svc()->Connect(launcher.NewRequest());
  launcher->CreateComponent(std::move(launch_info),
                            std::move(controller_request));
  child_services.ConnectToService(repository_factory.NewRequest());

  fuchsia::ledger::internal::LedgerRepositorySyncPtr repository;

  repository_factory->GetRepository(
      fsl::CloneChannelFromFileDescriptor(dir.get()), std::move(cloud_provider),
      std::move(user_id), repository.NewRequest());

  (*ledger).set_error_handler(
      [error_handler = std::move(error_handler)](zx_status_t status) {
        FXL_LOG(ERROR) << "The ledger connection was closed, quitting.";
        error_handler();
      });
  repository->GetLedger(convert::ToArray(ledger_name), ledger->NewRequest());
  return ToLedgerStatus(repository->Sync());
}

void KillLedgerProcess(fuchsia::sys::ComponentControllerPtr* controller) {
  (*controller)->Kill();
  auto channel = controller->Unbind().TakeChannel();
  zx_signals_t observed;
  channel.wait_one(ZX_CHANNEL_PEER_CLOSED, zx::deadline_after(zx::sec(5)),
                   &observed);
}

}  // namespace ledger
