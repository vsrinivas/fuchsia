// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MODULAR_LIB_TESTING_LEDGER_REPOSITORY_FOR_TESTING_H_
#define SRC_MODULAR_LIB_TESTING_LEDGER_REPOSITORY_FOR_TESTING_H_

#include <fuchsia/ledger/cpp/fidl.h>
#include <fuchsia/ledger/internal/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/sys/cpp/component_context.h>

#include <string>

#include "src/lib/fxl/macros.h"
#include "src/lib/fxl/memory/weak_ptr.h"
#include "src/modular/lib/fidl/app_client.h"
#include "src/modular/lib/scoped_tmpfs/scoped_tmpfs.h"

namespace modular_testing {

// LedgerRepositoryForTesting spins up a ledger instance and acquires a ledger
// repository meant to be used for testing, particularly in gtest unittests.
class LedgerRepositoryForTesting {
 public:
  LedgerRepositoryForTesting();
  ~LedgerRepositoryForTesting();

  fuchsia::ledger::internal::LedgerRepository* ledger_repository();

  // Terminates the ledger repository app.
  void Terminate(fit::function<void()> callback);

 private:
  std::unique_ptr<sys::ComponentContext> component_context_;
  scoped_tmpfs::ScopedTmpFS tmp_fs_;
  std::unique_ptr<modular::AppClient<fuchsia::ledger::internal::LedgerController>>
      ledger_app_client_;
  fuchsia::ledger::internal::LedgerRepositoryFactoryPtr ledger_repo_factory_;
  fuchsia::ledger::internal::LedgerRepositoryPtr ledger_repo_;

  fxl::WeakPtrFactory<LedgerRepositoryForTesting> weak_ptr_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(LedgerRepositoryForTesting);
};

}  // namespace modular_testing

#endif  // SRC_MODULAR_LIB_TESTING_LEDGER_REPOSITORY_FOR_TESTING_H_
