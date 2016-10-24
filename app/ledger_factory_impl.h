// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_APP_LEDGER_FACTORY_IMPL_H_
#define APPS_LEDGER_APP_LEDGER_FACTORY_IMPL_H_

#include <memory>
#include <string>
#include <unordered_map>

#include "apps/ledger/api/ledger.mojom.h"
#include "apps/ledger/app/ledger_manager.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/tasks/task_runner.h"
#include "mojo/public/interfaces/application/shell.mojom.h"

namespace ledger {

class LedgerFactoryImpl : public LedgerFactory {
 public:
  // |task_runner| executes asynchronous tasks for the created ledgers
  // |base_storage_dir| is the base directory where disk storage for the created
  // ledgers is hosted in separate subdirectories
  LedgerFactoryImpl(ftl::RefPtr<ftl::TaskRunner> task_runner,
                    const std::string& base_storage_dir);
  ~LedgerFactoryImpl() override;

 private:
  // LedgerFactory:
  void GetLedger(IdentityPtr identity,
                 const GetLedgerCallback& callback) override;

  ftl::RefPtr<ftl::TaskRunner> task_runner_;
  const std::string base_storage_dir_;

  struct ArrayHash {
    size_t operator()(const IdentityPtr& identity) const;
  };

  struct ArrayEquals {
    size_t operator()(const IdentityPtr& identity1,
                      const IdentityPtr& identity2) const;
  };

  std::unordered_map<IdentityPtr,
                     std::unique_ptr<LedgerManager>,
                     ArrayHash,
                     ArrayEquals>
      ledger_managers_;

  FTL_DISALLOW_COPY_AND_ASSIGN(LedgerFactoryImpl);
};

}  // namespace ledger

#endif  // APPS_LEDGER_APP_LEDGER_FACTORY_IMPL_H_
