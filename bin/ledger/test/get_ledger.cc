// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/test/get_ledger.h"

#include <utility>

#include "lib/app/cpp/connect.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/functional/make_copyable.h"
#include "lib/fxl/logging.h"
#include "peridot/bin/cloud_provider_firebase/fidl/factory.fidl.h"
#include "peridot/bin/ledger/callback/capture.h"
#include "peridot/bin/ledger/callback/synchronous_task.h"
#include "peridot/bin/ledger/convert/convert.h"
#include "peridot/bin/ledger/fidl/internal.fidl.h"
#include "peridot/bin/ledger/test/test_with_message_loop.h"

namespace test {
namespace {
constexpr fxl::TimeDelta kTimeout = fxl::TimeDelta::FromSeconds(10);
}  // namespace

ledger::Status GetLedger(fsl::MessageLoop* loop,
                         app::ApplicationContext* context,
                         app::ApplicationControllerPtr* controller,
                         cloud_provider::CloudProviderPtr cloud_provider,
                         std::string ledger_name,
                         std::string ledger_repository_path,
                         ledger::LedgerPtr* ledger_ptr,
                         Erase erase) {
  ledger::LedgerRepositoryFactoryPtr repository_factory;
  app::ServiceProviderPtr child_services;
  auto launch_info = app::ApplicationLaunchInfo::New();
  launch_info->url = "ledger";
  launch_info->services = child_services.NewRequest();
  launch_info->arguments.push_back("--no_minfs_wait");
  launch_info->arguments.push_back("--no_statistics_reporting_for_testing");

  context->launcher()->CreateApplication(std::move(launch_info),
                                         controller->NewRequest());
  app::ConnectToService(child_services.get(), repository_factory.NewRequest());
  ledger::LedgerRepositoryPtr repository;

  ledger::Status status = ledger::Status::UNKNOWN_ERROR;
  if (erase == Erase::ERASE_CLOUD) {
    cloud_provider::Status cloud_provider_status;
    cloud_provider->EraseAllData(
        callback::Capture([] {}, &cloud_provider_status));
    if (!repository_factory.WaitForIncomingResponseWithTimeout(kTimeout) ||
        cloud_provider_status != cloud_provider::Status::OK) {
      FXL_LOG(ERROR) << "Unable to erase repository.";
      return ledger::Status::INTERNAL_ERROR;
    }
  }

  repository_factory->GetRepository(
      ledger_repository_path, std::move(cloud_provider),
      repository.NewRequest(), callback::Capture([] {}, &status));
  if (!repository_factory.WaitForIncomingResponseWithTimeout(kTimeout)) {
    FXL_LOG(ERROR) << "Unable to get repository.";
    return ledger::Status::INTERNAL_ERROR;
  }
  if (status != ledger::Status::OK) {
    FXL_LOG(ERROR) << "Failure while getting repository.";
    return status;
  }

  repository->GetLedger(convert::ToArray(ledger_name), ledger_ptr->NewRequest(),
                        callback::Capture([] {}, &status));
  if (!repository.WaitForIncomingResponseWithTimeout(kTimeout)) {
    FXL_LOG(ERROR) << "Unable to get ledger.";
    return ledger::Status::INTERNAL_ERROR;
  }
  if (status != ledger::Status::OK) {
    FXL_LOG(ERROR) << "Failure while getting ledger.";
    return status;
  }
  ledger_ptr->set_connection_error_handler([loop] {
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
  if (!ledger->WaitForIncomingResponseWithTimeout(kTimeout)) {
    FXL_LOG(ERROR) << "Unable to get page.";
    return ledger::Status::INTERNAL_ERROR;
  }
  if (status != ledger::Status::OK) {
    return status;
  }

  page->set_connection_error_handler([loop] {
    FXL_LOG(ERROR) << "The page connection was closed, quitting.";
    loop->PostQuitTask();
  });

  (*page)->GetId(callback::Capture([] {}, page_id));
  page->WaitForIncomingResponseWithTimeout(kTimeout);
  return status;
}

}  // namespace test
