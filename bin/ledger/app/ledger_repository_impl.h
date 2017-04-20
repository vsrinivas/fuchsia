// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_APP_LEDGER_REPOSITORY_IMPL_H_
#define APPS_LEDGER_SRC_APP_LEDGER_REPOSITORY_IMPL_H_

#include "apps/ledger/services/internal/internal.fidl.h"
#include "apps/ledger/services/public/ledger.fidl.h"
#include "apps/ledger/src/app/ledger_manager.h"
#include "apps/ledger/src/callback/auto_cleanable.h"
#include "apps/ledger/src/cloud_sync/public/user_config.h"
#include "apps/ledger/src/convert/convert.h"
#include "apps/ledger/src/environment/environment.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/ftl/macros.h"

namespace ledger {

class LedgerRepositoryImpl : public LedgerRepository {
 public:
  LedgerRepositoryImpl(const std::string& base_storage_dir,
                       Environment* environment,
                       cloud_sync::UserConfig user_config);
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
  void Duplicate(fidl::InterfaceRequest<LedgerRepository> request,
                 const DuplicateCallback& callback) override;

  void CheckEmpty();

  const std::string base_storage_dir_;
  Environment* const environment_;
  const cloud_sync::UserConfig user_config_;
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
