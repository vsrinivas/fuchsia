// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/app/ledger_factory_impl.h"

#include "apps/ledger/src/app/ledger_impl.h"
#include "apps/ledger/src/convert/convert.h"
#include "apps/ledger/src/storage/impl/ledger_storage_impl.h"
#include "mojo/public/cpp/application/connect.h"

namespace ledger {

size_t LedgerFactoryImpl::ArrayHash::operator()(
    const IdentityPtr& identity) const {
  size_t user_hash = 5381;
  for (uint8_t b : identity->user_id.storage()) {
    user_hash = ((user_hash << 5) + user_hash) ^ b;
  }

  size_t app_hash = 5387;
  for (uint8_t b : identity->app_id.storage()) {
    app_hash = ((app_hash << 5) + app_hash) ^ b;
  }

  return ((user_hash << 5) + user_hash) ^ app_hash;
}

size_t LedgerFactoryImpl::ArrayEquals::operator()(
    const IdentityPtr& identity1,
    const IdentityPtr& identity2) const {
  return identity1->user_id.Equals(identity2->user_id) &&
         identity1->app_id.Equals(identity2->app_id);
}

LedgerFactoryImpl::LedgerFactoryImpl(ftl::RefPtr<ftl::TaskRunner> task_runner,
                                     const std::string& base_storage_dir)
    : task_runner_(task_runner), base_storage_dir_(base_storage_dir) {}

LedgerFactoryImpl::~LedgerFactoryImpl() {}

// GetLedger(Identity identity) => (Status status, Ledger? ledger);
void LedgerFactoryImpl::GetLedger(IdentityPtr identity,
                                  mojo::InterfaceRequest<Ledger> ledger_request,
                                  const GetLedgerCallback& callback) {
  if (identity->user_id.size() == 0 || identity->app_id.size() == 0) {
    callback.Run(Status::AUTHENTICATION_ERROR);
    return;
  }

  // If we have the ledger manager ready, just bind to its impl.
  auto it = ledger_managers_.find(identity);
  if (it != ledger_managers_.end()) {
    it->second->BindLedger(std::move(ledger_request));
    callback.Run(Status::OK);
    return;
  }

  storage::LedgerStorageImpl::Identity storage_identity;
  storage_identity.user_id = convert::ToString(identity->user_id);
  storage_identity.app_id = convert::ToString(identity->app_id);
  std::unique_ptr<storage::LedgerStorage> ledger_storage(
      new storage::LedgerStorageImpl(task_runner_, base_storage_dir_,
                                     storage_identity));

  auto ret = ledger_managers_.insert(std::make_pair(
      std::move(identity),
      std::make_unique<LedgerManager>(std::move(ledger_storage))));
  ret.first->second->BindLedger(std::move(ledger_request));
  callback.Run(Status::OK);
}

}  // namespace ledger
