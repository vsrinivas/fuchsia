// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/app/ledger_factory_impl.h"

#include "apps/ledger/app/ledger_impl.h"
#include "apps/ledger/glue/crypto/base64.h"
#include "apps/ledger/storage/impl/ledger_storage_impl.h"
#include "mojo/public/cpp/application/connect.h"

namespace ledger {

size_t LedgerFactoryImpl::ArrayHash::operator()(
    const mojo::Array<uint8_t>& array) const {
  size_t result = 5381;
  for (uint8_t b : array.storage())
    result = ((result << 5) + result) ^ b;
  return result;
}

size_t LedgerFactoryImpl::ArrayEquals::operator()(
    const mojo::Array<uint8_t>& array_1,
    const mojo::Array<uint8_t>& array_2) const {
  return array_1.Equals(array_2);
}

LedgerFactoryImpl::LedgerFactoryImpl(ftl::RefPtr<ftl::TaskRunner> task_runner,
                                     const std::string& base_storage_dir)
    : task_runner_(task_runner), base_storage_dir_(base_storage_dir) {}

LedgerFactoryImpl::~LedgerFactoryImpl() {}

// GetLedger(Identity identity) => (Status status, Ledger? ledger);
void LedgerFactoryImpl::GetLedger(IdentityPtr identity,
                                  const GetLedgerCallback& callback) {
  LedgerPtr ledger;
  if (identity->user_id.size() == 0) {
    // User identity cannot be empty.
    callback.Run(Status::AUTHENTICATION_ERROR, nullptr);
    return;
  }

  // If we have the ledger manager ready, just bind to its impl.
  auto it = ledger_managers_.find(identity->user_id);
  if (it != ledger_managers_.end()) {
    callback.Run(Status::OK, it->second->GetLedgerPtr());
    return;
  }

  // If not, create one.
  std::unique_ptr<storage::LedgerStorage> ledger_storage(
      new storage::LedgerStorageImpl(task_runner_, base_storage_dir_,
                                     GetIdentityString(identity)));

  auto ret = ledger_managers_.insert(std::make_pair(
      std::move(identity->user_id),
      std::make_unique<LedgerManager>(std::move(ledger_storage))));
  callback.Run(Status::OK, ret.first->second->GetLedgerPtr());
}

std::string LedgerFactoryImpl::GetIdentityString(const IdentityPtr& identity) {
  std::string identity_string(
      reinterpret_cast<const char*>(identity->user_id.data()),
      identity->user_id.size());
  std::string ledger_name;
  glue::Base64Encode(identity_string, &ledger_name);
  return ledger_name;
}

}  // namespace ledger
