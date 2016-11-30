// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_APP_LEDGER_REPOSITORY_IMPL_H_
#define APPS_LEDGER_SRC_APP_LEDGER_REPOSITORY_IMPL_H_

#include "apps/ledger/services/ledger.fidl.h"
#include "apps/ledger/src/app/ledger_manager.h"
#include "apps/ledger/src/callback/auto_cleanable.h"
#include "apps/ledger/src/convert/convert.h"
#include "apps/ledger/src/environment/environment.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/tasks/task_runner.h"

namespace ledger {

class LedgerRepositoryImpl : public LedgerRepository {
 public:
  LedgerRepositoryImpl(ftl::RefPtr<ftl::TaskRunner> task_runner,
                       const std::string& base_storage_dir,
                       ledger::Environment* environment);
  ~LedgerRepositoryImpl() override;

  void set_on_empty(const ftl::Closure& on_empty_callback) {
    on_empty_callback_ = on_empty_callback;
  }

  void BindRepository(
      fidl::InterfaceRequest<LedgerRepository> repository_request);

 private:
  // LedgerRepository:
  void GetLedger(fidl::Array<uint8_t> ledger_name,
                 fidl::InterfaceRequest<Ledger> ledger_request,
                 const GetLedgerCallback& callback) override;

  void CheckEmpty();

  ftl::RefPtr<ftl::TaskRunner> task_runner_;
  const std::string base_storage_dir_;
  ledger::Environment* const environment_;
  callback::AutoCleanableMap<std::string,
                             LedgerManager,
                             convert::StringViewComparator>
      ledger_managers_;
  fidl::BindingSet<LedgerRepository> bindings_;
  ftl::Closure on_empty_callback_;

  FTL_DISALLOW_COPY_AND_ASSIGN(LedgerRepositoryImpl);
};

}  // namespace ledger

#endif  // APPS_LEDGER_SRC_APP_LEDGER_REPOSITORY_IMPL_H_
