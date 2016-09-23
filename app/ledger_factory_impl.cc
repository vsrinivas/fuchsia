// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/app/ledger_factory_impl.h"

#include "apps/ledger/app/ledger_impl.h"
#include "apps/ledger/glue/crypto/base64.h"
#include "mojo/public/cpp/application/connect.h"

namespace ledger {

LedgerFactoryImpl::LedgerFactoryImpl(
    mojo::InterfaceRequest<LedgerFactory> request,
    storage::LedgerStorage* storage)
    : binding_(this, std::move(request)), storage_(storage) {}

LedgerFactoryImpl::~LedgerFactoryImpl() {}

// GetLedger(Identity identity) => (Status status, Ledger? ledger);
void LedgerFactoryImpl::GetLedger(IdentityPtr identity,
                                  const GetLedgerCallback& callback) {
  LedgerPtr ledger;
  if (identity->user_id.size() == 0) {
    // User identity cannot be empty.
    callback.Run(Status::AUTHENTICATION_ERROR, nullptr);
  } else {
    std::unique_ptr<storage::ApplicationStorage> app_storage =
        storage_->CreateApplicationStorage(
            GetIdentityString(std::move(identity)));
    if (app_storage) {
      new LedgerImpl(GetProxy(&ledger), std::move(app_storage));
      callback.Run(Status::OK, std::move(ledger));
    } else {
      callback.Run(Status::AUTHENTICATION_ERROR, std::move(ledger));
    }
  }
}

std::string LedgerFactoryImpl::GetIdentityString(IdentityPtr identity) {
  std::string identity_string(
      reinterpret_cast<const char*>(identity->user_id.data()),
      identity->user_id.size());
  std::string ledger_name;
  glue::Base64Encode(identity_string, &ledger_name);
  return ledger_name;
}

}  // namespace ledger
