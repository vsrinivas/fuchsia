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
#include "lib/fxl/macros.h"
#include "lib/fxl/tasks/task_runner.h"

namespace modular {
namespace testing {

// LedgerRepoistoryForTesting spins up a ledger instance and acquires a ledger
// repository meant to be used for testing, particularly in gtest unittests.
class LedgerRepositoryForTesting {
 public:
  explicit LedgerRepositoryForTesting(const std::string& repository_name);
  ~LedgerRepositoryForTesting();

  ledger::LedgerRepository* ledger_repository();

  // Erases the repository. Must be done before destroying the
  // LedgerRepositoryForTesting instance, otherwise the repository stays there
  // and may be reopened on a later run of the test.
  void Reset(std::function<void()> done);

  // Terminates the ledger repository app.
  void Terminate(std::function<void()> done);

 private:
  const std::string repository_path_;
  std::unique_ptr<AppClient<ledger::LedgerController>>
      ledger_app_client_;
  ledger::LedgerRepositoryFactoryPtr ledger_repo_factory_;
  ledger::LedgerRepositoryPtr ledger_repo_;

  FXL_DISALLOW_COPY_AND_ASSIGN(LedgerRepositoryForTesting);
};

}  // namespace testing
}  // namespace modular

#endif  // APPS_MODULAR_LIB_TESTING_LEDGER_REPOSITORY_FOR_TESTING_H_
