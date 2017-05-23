// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/benchmark/lib/get_ledger.h"

#include <utility>

#include "application/lib/app/connect.h"
#include "apps/ledger/benchmark/lib/convert.h"
#include "apps/ledger/benchmark/lib/logging.h"
#include "apps/ledger/services/internal/internal.fidl.h"
#include "lib/ftl/functional/make_copyable.h"
#include "lib/ftl/logging.h"
#include "lib/mtl/tasks/message_loop.h"

namespace benchmark {

ledger::LedgerPtr GetLedger(app::ApplicationContext* context,
                            app::ApplicationControllerPtr* controller,
                            std::string ledger_name,
                            std::string ledger_repository_path,
                            bool sync,
                            std::string server_id,
                            ledger::LedgerControllerPtr* ledger_controller) {
  ledger::LedgerRepositoryFactoryPtr repository_factory;
  app::ServiceProviderPtr child_services;
  auto launch_info = app::ApplicationLaunchInfo::New();
  launch_info->url = "file:///system/apps/ledger";
  launch_info->services = child_services.NewRequest();
  launch_info->arguments.push_back("--no_minfs_wait");
  launch_info->arguments.push_back("--no_persisted_config");

  context->launcher()->CreateApplication(std::move(launch_info),
                                         controller->NewRequest());
  app::ConnectToService(child_services.get(), repository_factory.NewRequest());
  if (ledger_controller) {
    app::ConnectToService(child_services.get(),
                          ledger_controller->NewRequest());
  }
  ledger::LedgerRepositoryPtr repository;
  fidl::String fidl_server_id = sync ? fidl::String(server_id) : fidl::String();
  repository_factory->GetRepository(
      ledger_repository_path, std::move(fidl_server_id), nullptr,
      repository.NewRequest(), QuitOnErrorCallback("GetRepository"));

  ledger::LedgerPtr ledger;
  repository->GetLedger(ToArray(ledger_name), ledger.NewRequest(),
                        QuitOnErrorCallback("GetLedger"));
  ledger.set_connection_error_handler([] {
    FTL_LOG(ERROR) << "The ledger connection was closed, quitting.";
    mtl::MessageLoop::GetCurrent()->PostQuitTask();
  });
  return ledger;
}

void GetPageEnsureInitialized(
    ledger::Ledger* ledger,
    fidl::Array<uint8_t> id,
    std::function<void(ledger::PagePtr page, fidl::Array<uint8_t> id)>
        callback) {
  ledger::PagePtr page;
  ledger->GetPage(std::move(id), page.NewRequest(),
                  benchmark::QuitOnErrorCallback("GetRootPage"));
  page.set_connection_error_handler([] {
    FTL_LOG(ERROR) << "The root page connection was closed, quitting.";
    mtl::MessageLoop::GetCurrent()->PostQuitTask();
  });

  ledger::Page* page_ptr = page.get();
  page_ptr->GetId(ftl::MakeCopyable([
    page = std::move(page), callback = std::move(callback)
  ](auto id) mutable { callback(std::move(page), std::move(id)); }));
}

}  // namespace benchmark
