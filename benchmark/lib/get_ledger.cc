// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/benchmark/lib/get_ledger.h"

#include <utility>

#include "application/lib/app/connect.h"
#include "apps/ledger/benchmark/lib/convert.h"
#include "apps/ledger/benchmark/lib/logging.h"
#include "apps/ledger/services/internal/internal.fidl.h"

namespace benchmark {

ledger::LedgerPtr GetLedger(app::ApplicationContext* context,
                            app::ApplicationControllerPtr* controller,
                            std::string ledger_name,
                            std::string ledger_repository_path) {
  ledger::LedgerRepositoryFactoryPtr repository_factory;
  app::ServiceProviderPtr child_services;
  auto launch_info = app::ApplicationLaunchInfo::New();
  launch_info->url = "file:///system/apps/ledger";
  launch_info->services = child_services.NewRequest();
  context->launcher()->CreateApplication(std::move(launch_info),
                                         controller->NewRequest());
  app::ConnectToService(child_services.get(), repository_factory.NewRequest());

  ledger::LedgerRepositoryPtr repository;
  repository_factory->GetRepository(ledger_repository_path,
                                    repository.NewRequest(),
                                    QuitOnErrorCallback("GetRepository"));

  ledger::LedgerPtr ledger;
  repository->GetLedger(ToArray(ledger_name), ledger.NewRequest(),
                        QuitOnErrorCallback("GetLedger"));
  return ledger;
}

}  // namespace benchmark
