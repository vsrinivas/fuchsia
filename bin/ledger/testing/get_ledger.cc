// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/testing/get_ledger.h"

#include <utility>

#include <fuchsia/ledger/cloud/firebase/cpp/fidl.h>
#include <fuchsia/ledger/internal/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/fit/function.h>

#include "lib/callback/capture.h"
#include "lib/fxl/functional/make_copyable.h"
#include "lib/fxl/logging.h"
#include "lib/svc/cpp/services.h"
#include "peridot/bin/ledger/fidl/include/types.h"
#include "peridot/lib/convert/convert.h"

namespace test {
void GetLedger(
    fuchsia::sys::StartupContext* context,
    fidl::InterfaceRequest<fuchsia::sys::ComponentController>
        controller_request,
    cloud_provider::CloudProviderPtr cloud_provider, std::string ledger_name,
    std::string ledger_repository_path, fit::function<void()> error_handler,
    fit::function<void(ledger::Status, ledger::LedgerPtr)> callback) {
  auto repository_factory =
      std::make_unique<ledger_internal::LedgerRepositoryFactoryPtr>();
  fuchsia::sys::Services child_services;
  fuchsia::sys::LaunchInfo launch_info;
  launch_info.url = "ledger";
  launch_info.directory_request = child_services.NewRequest();
  launch_info.arguments.push_back("--no_minfs_wait");
  launch_info.arguments.push_back("--disable_reporting");

  context->launcher()->CreateComponent(std::move(launch_info),
                                       std::move(controller_request));
  child_services.ConnectToService(repository_factory->NewRequest());

  auto repository_factory_ptr = repository_factory->get();
  auto repository = std::make_unique<ledger_internal::LedgerRepositoryPtr>();
  auto request = repository->NewRequest();

  repository_factory_ptr->GetRepository(
      ledger_repository_path, std::move(cloud_provider), std::move(request),
      fxl::MakeCopyable(
          [repository_factory = std::move(repository_factory),
           repository = std::move(repository),
           ledger_name = std::move(ledger_name),
           ledger_repository_path = std::move(ledger_repository_path),
           error_handler = std::move(error_handler),
           callback = std::move(callback)](ledger::Status status) mutable {
            if (status != ledger::Status::OK) {
              FXL_LOG(ERROR) << "Failure while getting repository.";
              callback(status, nullptr);
              return;
            }

            auto repository_ptr = repository->get();
            auto ledger = std::make_unique<ledger::LedgerPtr>();
            auto request = ledger->NewRequest();
            repository_ptr->GetLedger(
                convert::ToArray(ledger_name), std::move(request),
                fxl::MakeCopyable([repository = std::move(repository),
                                   ledger = std::move(ledger),
                                   error_handler = std::move(error_handler),
                                   callback = std::move(callback)](
                                      ledger::Status status) mutable {
                  if (status != ledger::Status::OK) {
                    FXL_LOG(ERROR) << "Failure while getting ledger.";
                    callback(status, nullptr);
                    return;
                  }
                  ledger->set_error_handler(
                      [error_handler = std::move(error_handler)] {
                        FXL_LOG(ERROR)
                            << "The ledger connection was closed, quitting.";
                        error_handler();
                      });
                  callback(ledger::Status::OK, std::move(*ledger));
                }));
          }));
}

void GetPageEnsureInitialized(
    ledger::LedgerPtr* ledger, ledger::PageIdPtr requested_id,
    fit::function<void()> error_handler,
    fit::function<void(ledger::Status, ledger::PagePtr, ledger::PageId)>
        callback) {
  auto page = std::make_unique<ledger::PagePtr>();
  auto request = page->NewRequest();
  (*ledger)->GetPage(
      std::move(requested_id), std::move(request),
      fxl::MakeCopyable(
          [page = std::move(page), error_handler = std::move(error_handler),
           callback = std::move(callback)](ledger::Status status) mutable {
            if (status != ledger::Status::OK) {
              FXL_LOG(ERROR) << "Failure while getting a page.";
              callback(status, nullptr, {});
              return;
            }

            page->set_error_handler([error_handler = std::move(error_handler)] {
              FXL_LOG(ERROR) << "The page connection was closed, quitting.";
              error_handler();
            });

            auto page_ptr = (*page).get();
            page_ptr->GetId(fxl::MakeCopyable(
                [page = std::move(page),
                 callback = std::move(callback)](ledger::PageId page_id) {
                  callback(ledger::Status::OK, std::move(*page),
                           std::move(page_id));
                }));
          }));
}

void KillLedgerProcess(fuchsia::sys::ComponentControllerPtr* controller) {
  (*controller)->Kill();
  auto channel = controller->Unbind().TakeChannel();
  zx_signals_t observed;
  channel.wait_one(ZX_CHANNEL_PEER_CLOSED, zx::deadline_after(zx::sec(5)),
                   &observed);
}

}  // namespace test
