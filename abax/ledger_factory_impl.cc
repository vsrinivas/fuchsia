// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/abax/ledger_factory_impl.h"

#include "apps/ledger/abax/ledger_impl.h"
#include "apps/ledger/convert/convert.h"
#include "apps/ledger/glue/crypto/base64.h"
#include "mojo/public/cpp/application/connect.h"

namespace ledger {

LedgerFactoryImpl::LedgerFactoryImpl(
    mojo::InterfaceRequest<LedgerFactory> request)
    : binding_(this, std::move(request)) {}

LedgerFactoryImpl::~LedgerFactoryImpl() {}

// GetLedger(Identity identity) => (Status status, Ledger? ledger);
void LedgerFactoryImpl::GetLedger(IdentityPtr identity,
                                  const GetLedgerCallback& callback) {
  LedgerPtr ledger;
  if (identity->user_id.size() == 0) {
    // User identity cannot be empty.
    callback.Run(Status::AUTHENTICATION_ERROR, nullptr);
  } else {
    Status status = (new LedgerImpl(GetProxy(&ledger)))->Init();
    if (status != Status::OK) {
      ledger = nullptr;
    }
    callback.Run(status, std::move(ledger));
  }
}

}  // namespace ledger
