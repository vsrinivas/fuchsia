// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_LIB_TESTING_LEDGER_REPOSITORY_FOR_TESTING_H_
#define PERIDOT_LIB_TESTING_LEDGER_REPOSITORY_FOR_TESTING_H_

#include <string>

#include <fuchsia/ledger/cpp/fidl.h>
#include <fuchsia/ledger/internal/cpp/fidl.h>

#include "lib/app/cpp/startup_context.h"
#include "lib/fidl/cpp/binding.h"
#include "lib/fxl/files/scoped_temp_dir.h"
#include "lib/fxl/macros.h"
#include "peridot/lib/fidl/app_client.h"

namespace modular {
namespace testing {

// LedgerRepoistoryForTesting spins up a ledger instance and acquires a ledger
// repository meant to be used for testing, particularly in gtest unittests.
class LedgerRepositoryForTesting {
 public:
  LedgerRepositoryForTesting();
  ~LedgerRepositoryForTesting();

  fuchsia::ledger::internal::LedgerRepository* ledger_repository();

  // Terminates the ledger repository app.
  void Terminate(std::function<void()> done);

 private:
  std::unique_ptr<fuchsia::sys::StartupContext> startup_context_;
  files::ScopedTempDir tmp_dir_;
  std::unique_ptr<AppClient<fuchsia::ledger::internal::LedgerController>>
      ledger_app_client_;
  fuchsia::ledger::internal::LedgerRepositoryFactoryPtr ledger_repo_factory_;
  fuchsia::ledger::internal::LedgerRepositoryPtr ledger_repo_;

  FXL_DISALLOW_COPY_AND_ASSIGN(LedgerRepositoryForTesting);
};

}  // namespace testing
}  // namespace modular

#endif  // PERIDOT_LIB_TESTING_LEDGER_REPOSITORY_FOR_TESTING_H_
