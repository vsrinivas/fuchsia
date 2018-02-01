// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/testing/get_ledger.h"

#include <zx/time.h>

#include <utility>

#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/functional/make_copyable.h"
#include "lib/fxl/logging.h"
#include "lib/svc/cpp/services.h"
#include "peridot/bin/cloud_provider_firebase/fidl/factory.fidl.h"
#include "peridot/bin/ledger/fidl/internal.fidl.h"
#include "peridot/lib/callback/capture.h"
#include "peridot/lib/callback/synchronous_task.h"
#include "peridot/lib/convert/convert.h"

namespace test {
namespace {
constexpr zx::duration kTimeout = zx::sec(10);
}  // namespace

ledger::Status GetLedger(fsl::MessageLoop* loop,
                         app::ApplicationContext* context,
                         app::ApplicationControllerPtr* controller,
                         cloud_provider::CloudProviderPtr cloud_provider,
                         std::string ledger_name,
                         std::string ledger_repository_path,
                         ledger::LedgerPtr* ledger_ptr) {
  ledger::LedgerRepositoryFactoryPtr repository_factory;
  app::Services child_services;
  auto launch_info = app::ApplicationLaunchInfo::New();
  launch_info->url = "ledger";
  launch_info->service_request = child_services.NewRequest();
  launch_info->arguments.push_back("--no_minfs_wait");
  launch_info->arguments.push_back("--no_statistics_reporting_for_testing");

  context->launcher()->CreateApplication(std::move(launch_info),
                                         controller->NewRequest());
  child_services.ConnectToService(repository_factory.NewRequest());
  ledger::LedgerRepositoryPtr repository;

  ledger::Status status = ledger::Status::UNKNOWN_ERROR;

  repository_factory->GetRepository(
      ledger_repository_path, std::move(cloud_provider),
      repository.NewRequest(), callback::Capture([] {}, &status));
  if (!repository_factory.WaitForResponseUntil(zx::deadline_after(kTimeout))) {
    FXL_LOG(ERROR) << "Unable to get repository.";
    return ledger::Status::INTERNAL_ERROR;
  }
  if (status != ledger::Status::OK) {
    FXL_LOG(ERROR) << "Failure while getting repository.";
    return status;
  }

  repository->GetLedger(convert::ToArray(ledger_name), ledger_ptr->NewRequest(),
                        callback::Capture([] {}, &status));
  if (!repository.WaitForResponseUntil(zx::deadline_after(kTimeout))) {
    FXL_LOG(ERROR) << "Unable to get ledger.";
    return ledger::Status::INTERNAL_ERROR;
  }
  if (status != ledger::Status::OK) {
    FXL_LOG(ERROR) << "Failure while getting ledger.";
    return status;
  }
  ledger_ptr->set_error_handler([loop] {
    FXL_LOG(ERROR) << "The ledger connection was closed, quitting.";
    loop->PostQuitTask();
  });

  return status;
}

ledger::Status GetPageEnsureInitialized(fsl::MessageLoop* loop,
                                        ledger::LedgerPtr* ledger,
                                        fidl::Array<uint8_t> requested_id,
                                        ledger::PagePtr* page,
                                        fidl::Array<uint8_t>* page_id) {
  ledger::Status status;
  (*ledger)->GetPage(std::move(requested_id), page->NewRequest(),
                     callback::Capture([] {}, &status));
  if (!ledger->WaitForResponseUntil(zx::deadline_after(kTimeout))) {
    FXL_LOG(ERROR) << "Unable to get page.";
    return ledger::Status::INTERNAL_ERROR;
  }
  if (status != ledger::Status::OK) {
    return status;
  }

  page->set_error_handler([loop] {
    FXL_LOG(ERROR) << "The page connection was closed, quitting.";
    loop->PostQuitTask();
  });

  (*page)->GetId(callback::Capture([] {}, page_id));
  page->WaitForResponseUntil(zx::deadline_after(kTimeout));
  return status;
}

}  // namespace test
