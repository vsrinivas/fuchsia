// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MODULAR_LIB_TESTING_LEDGER_REPOSITORY_FOR_TESTING_H_
#define APPS_MODULAR_LIB_TESTING_LEDGER_REPOSITORY_FOR_TESTING_H_

#include <string>

#include "apps/ledger/services/internal/internal.fidl.h"
#include "apps/ledger/services/public/ledger.fidl.h"
#include "apps/modular/lib/fidl/app_client.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/tasks/task_runner.h"

namespace modular {
namespace testing {

// LedgerRepoistoryForTesting spins up a ledger instance and acquires a ledger
// repository meant to be used for testing, particularly in gtest unittests.
class LedgerRepositoryForTesting {
 public:
  LedgerRepositoryForTesting(const std::string& repository_path);
  ledger::LedgerRepository* ledger_repository();
  void Reset(ftl::Closure done);

  // Calling this method a second time will return the same singleton instance
  // regardless of whether a different |respository_path| was given.
  static LedgerRepositoryForTesting* GetSingleton(
      const std::string& repository_path);

 private:
  std::string repository_path_;
  std::unique_ptr<AppClient<ledger::LedgerRepositoryFactory>>
      ledger_app_client_;
  ledger::LedgerRepositoryPtr ledger_repo_;

  FTL_DISALLOW_COPY_AND_ASSIGN(LedgerRepositoryForTesting);
};

}  // namespace testing
}  // namespace modular

#endif  // APPS_MODULAR_LIB_TESTING_LEDGER_REPOSITORY_FOR_TESTING_H_
