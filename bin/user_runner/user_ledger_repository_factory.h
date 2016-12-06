// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// UserLedgerRepositoryFactory encapsulates a LedgerRepositoryFactory for a particular
// user.

#ifndef APPS_MODULAR_SRC_USER_RUNNER_USER_LEDGER_REPOSITORY_FACTORY_H_
#define APPS_MODULAR_SRC_USER_RUNNER_USER_LEDGER_REPOSITORY_FACTORY_H_

#include <string>

#include "apps/ledger/services/ledger.fidl.h"
#include "lib/fidl/cpp/bindings/array.h"
#include "lib/ftl/macros.h"

namespace modular {

// This class works around the absence of a Dup() method in LedgerRepository. If
// we need another connection to the ledger repository of the same user, we just
// open a ledger repository of the same user on the ledger repository factory.
class UserLedgerRepositoryFactory {
 public:
  explicit UserLedgerRepositoryFactory(
      const std::string& user_repository_path,
      ledger::LedgerRepositoryFactoryPtr ledger_repository_factory);

  ledger::LedgerRepositoryPtr Clone();

 private:
  std::string user_repository_path_;
  ledger::LedgerRepositoryFactoryPtr ledger_repository_factory_;

  FTL_DISALLOW_COPY_AND_ASSIGN(UserLedgerRepositoryFactory);
};

}  // namespace modular

#endif  // APPS_MODULAR_SRC_USER_RUNNER_USER_LEDGER_REPOSITORY_FACTORY_H_
