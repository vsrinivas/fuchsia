// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_ABAX_LEDGER_FACTORY_IMPL_H_
#define APPS_LEDGER_ABAX_LEDGER_FACTORY_IMPL_H_

#include <memory>
#include <string>

#include "apps/ledger/api/ledger.mojom.h"
#include "lib/ftl/macros.h"
#include "mojo/public/cpp/bindings/strong_binding.h"

namespace ledger {

class LedgerFactoryImpl : public LedgerFactory {
 public:
  LedgerFactoryImpl(mojo::InterfaceRequest<LedgerFactory> request);
  ~LedgerFactoryImpl() override;

 private:
  // LedgerFactory:
  void GetLedger(IdentityPtr identity,
                 const GetLedgerCallback& callback) override;

  std::string GetLedgerPath(IdentityPtr identity);

  mojo::StrongBinding<LedgerFactory> binding_;

  FTL_DISALLOW_COPY_AND_ASSIGN(LedgerFactoryImpl);
};

}  // namespace ledger

#endif  // APPS_LEDGER_ABAX_LEDGER_FACTORY_IMPL_H_
