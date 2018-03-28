// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_LIB_TESTING_LEDGER_REPOSITORY_FOR_TESTING_H_
#define PERIDOT_LIB_TESTING_LEDGER_REPOSITORY_FOR_TESTING_H_

#include <string>

#include <fuchsia/cpp/ledger.h>
#include <fuchsia/cpp/ledger_internal.h>
#include "lib/app/cpp/application_context.h"
#include "lib/fidl/cpp/binding.h"
#include "lib/fxl/files/scoped_temp_dir.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/tasks/task_runner.h"
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

  // Terminates the ledger repository app.
  void Terminate(std::function<void()> done);

 private:
  std::unique_ptr<component::ApplicationContext> application_context_;
  files::ScopedTempDir tmp_dir_;
  std::unique_ptr<AppClient<ledger::LedgerController>> ledger_app_client_;
  ledger_internal::LedgerRepositoryFactoryPtr ledger_repo_factory_;
  ledger_internal::LedgerRepositoryPtr ledger_repo_;

  FXL_DISALLOW_COPY_AND_ASSIGN(LedgerRepositoryForTesting);
};

}  // namespace testing
}  // namespace modular

#endif  // PERIDOT_LIB_TESTING_LEDGER_REPOSITORY_FOR_TESTING_H_
