// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_APP_LEDGER_REPOSITORY_FACTORY_IMPL_H_
#define APPS_LEDGER_SRC_APP_LEDGER_REPOSITORY_FACTORY_IMPL_H_

#include <memory>
#include <string>

#include "apps/ledger/services/ledger.fidl.h"
#include "apps/ledger/src/app/auto_cleanable.h"
#include "apps/ledger/src/app/ledger_repository_impl.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/tasks/task_runner.h"

namespace ledger {

class LedgerRepositoryFactoryImpl : public LedgerRepositoryFactory {
 public:
  // |task_runner| executes asynchronous tasks for the created ledgers
  LedgerRepositoryFactoryImpl(ftl::RefPtr<ftl::TaskRunner> task_runner);
  ~LedgerRepositoryFactoryImpl() override;

 private:
  // LedgerRepositoryFactory:
  void GetRepository(
      const fidl::String& repository_path,
      fidl::InterfaceRequest<LedgerRepository> repository_request,
      const GetRepositoryCallback& callback) override;

  ftl::RefPtr<ftl::TaskRunner> task_runner_;
  AutoCleanableMap<std::string, LedgerRepositoryImpl> repositories_;

  FTL_DISALLOW_COPY_AND_ASSIGN(LedgerRepositoryFactoryImpl);
};

}  // namespace ledger

#endif  // APPS_LEDGER_SRC_APP_LEDGER_REPOSITORY_FACTORY_IMPL_H_
