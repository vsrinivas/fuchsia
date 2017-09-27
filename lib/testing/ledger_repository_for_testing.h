// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_LIB_TESTING_LEDGER_REPOSITORY_FOR_TESTING_H_
#define PERIDOT_LIB_TESTING_LEDGER_REPOSITORY_FOR_TESTING_H_

#include <string>

#include "lib/app/cpp/application_context.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fxl/files/scoped_temp_dir.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/tasks/task_runner.h"
#include "lib/ledger/fidl/ledger.fidl.h"
#include "peridot/bin/ledger/fidl/internal.fidl.h"
#include "peridot/lib/fidl/app_client.h"

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
  std::unique_ptr<app::ApplicationContext> application_context_;
  files::ScopedTempDir tmp_dir_;
  std::unique_ptr<AppClient<ledger::LedgerController>> ledger_app_client_;
  ledger::LedgerRepositoryFactoryPtr ledger_repo_factory_;
  ledger::LedgerRepositoryPtr ledger_repo_;

  FXL_DISALLOW_COPY_AND_ASSIGN(LedgerRepositoryForTesting);
};

}  // namespace testing
}  // namespace modular

#endif  // PERIDOT_LIB_TESTING_LEDGER_REPOSITORY_FOR_TESTING_H_
