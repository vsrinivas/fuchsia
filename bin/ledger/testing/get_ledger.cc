// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/testing/get_ledger.h"

#include <fuchsia/ledger/cloud/firebase/cpp/fidl.h>
#include <fuchsia/ledger/internal/cpp/fidl.h>
#include <lib/async/cpp/task.h>

#include <utility>

#include "lib/callback/capture.h"
#include "lib/fxl/functional/make_copyable.h"
#include "lib/fxl/logging.h"
#include "lib/svc/cpp/services.h"
#include "peridot/bin/ledger/fidl/include/types.h"
#include "peridot/lib/convert/convert.h"

namespace test {
namespace {
ledger::Status GetLedger(fxl::Closure run_loop, fxl::Closure stop_loop,
                         fuchsia::sys::StartupContext* context,
                         fuchsia::sys::ComponentControllerPtr* controller,
                         cloud_provider::CloudProviderPtr cloud_provider,
                         std::string ledger_name,
                         std::string ledger_repository_path,
                         ledger::LedgerPtr* ledger_ptr) {
  ledger_internal::LedgerRepositoryFactoryPtr repository_factory;
  fuchsia::sys::Services child_services;
  fuchsia::sys::LaunchInfo launch_info;
  launch_info.url = "ledger";
  launch_info.directory_request = child_services.NewRequest();
  launch_info.arguments.push_back("--no_minfs_wait");
  launch_info.arguments.push_back("--disable_reporting");

  context->launcher()->CreateComponent(std::move(launch_info),
                                       controller->NewRequest());
  child_services.ConnectToService(repository_factory.NewRequest());
  ledger_internal::LedgerRepositoryPtr repository;

  ledger::Status status = ledger::Status::UNKNOWN_ERROR;

  repository_factory->GetRepository(
      ledger_repository_path, std::move(cloud_provider),
      repository.NewRequest(), callback::Capture(stop_loop, &status));
  run_loop();
  if (status != ledger::Status::OK) {
    FXL_LOG(ERROR) << "Failure while getting repository.";
    return status;
  }

  repository->GetLedger(convert::ToArray(ledger_name), ledger_ptr->NewRequest(),
                        callback::Capture(stop_loop, &status));
  run_loop();
  if (status != ledger::Status::OK) {
    FXL_LOG(ERROR) << "Failure while getting ledger.";
    return status;
  }
  ledger_ptr->set_error_handler([stop_loop = std::move(stop_loop)] {
    FXL_LOG(ERROR) << "The ledger connection was closed, quitting.";
    stop_loop();
  });

  return status;
}

ledger::Status GetPageEnsureInitialized(fxl::Closure run_loop,
                                        fxl::Closure stop_loop,
                                        ledger::LedgerPtr* ledger,
                                        ledger::PageIdPtr requested_id,
                                        ledger::PagePtr* page,
                                        ledger::PageId* page_id) {
  ledger::Status status;
  (*ledger)->GetPage(std::move(requested_id), page->NewRequest(),
                     callback::Capture(stop_loop, &status));
  run_loop();
  if (status != ledger::Status::OK) {
    return status;
  }

  page->set_error_handler([stop_loop] {
    FXL_LOG(ERROR) << "The page connection was closed, quitting.";
    stop_loop();
  });

  (*page)->GetId(callback::Capture(std::move(stop_loop), page_id));
  return status;
}
}  // namespace

ledger::Status GetLedger(async::Loop* loop,
                         fuchsia::sys::StartupContext* context,
                         fuchsia::sys::ComponentControllerPtr* controller,
                         cloud_provider::CloudProviderPtr cloud_provider,
                         std::string ledger_name,
                         std::string ledger_repository_path,
                         ledger::LedgerPtr* ledger_ptr) {
  return GetLedger([loop] { loop->Run(); }, [loop] { loop->Quit(); }, context,
                   controller, std::move(cloud_provider),
                   std::move(ledger_name), std::move(ledger_repository_path),
                   ledger_ptr);
}

ledger::Status GetLedger(fsl::MessageLoop* loop,
                         fuchsia::sys::StartupContext* context,
                         fuchsia::sys::ComponentControllerPtr* controller,
                         cloud_provider::CloudProviderPtr cloud_provider,
                         std::string ledger_name,
                         std::string ledger_repository_path,
                         ledger::LedgerPtr* ledger_ptr) {
  return GetLedger([loop] { loop->Run(); }, [loop] { loop->QuitNow(); },
                   context, controller, std::move(cloud_provider),
                   std::move(ledger_name), std::move(ledger_repository_path),
                   ledger_ptr);
}

ledger::Status GetPageEnsureInitialized(async::Loop* loop,
                                        ledger::LedgerPtr* ledger,
                                        ledger::PageIdPtr requested_id,
                                        ledger::PagePtr* page,
                                        ledger::PageId* page_id) {
  return GetPageEnsureInitialized([loop] { loop->Run(); },
                                  [loop] { loop->Quit(); }, ledger,
                                  std::move(requested_id), page, page_id);
}

ledger::Status GetPageEnsureInitialized(fsl::MessageLoop* loop,
                                        ledger::LedgerPtr* ledger,
                                        ledger::PageIdPtr requested_id,
                                        ledger::PagePtr* page,
                                        ledger::PageId* page_id) {
  return GetPageEnsureInitialized([loop] { loop->Run(); },
                                  [loop] { loop->QuitNow(); }, ledger,
                                  std::move(requested_id), page, page_id);
}

}  // namespace test
