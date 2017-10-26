// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_APP_LEDGER_REPOSITORY_DEBUG_IMPL_H_
#define PERIDOT_BIN_LEDGER_APP_LEDGER_REPOSITORY_DEBUG_IMPL_H_

#include "peridot/bin/ledger/fidl/debug.fidl.h"

namespace ledger {

// An entry point for debugging the Ledger.
// An implementation of the |LedgerRepositoryDebug| FIDL interface used by the
// Ledger dashboard to expose Ledger internals for debugging.
class LedgerRepositoryDebugImpl : public LedgerRepositoryDebug {
 public:
  LedgerRepositoryDebugImpl() {}

 private:
  // LedgerRepositoryDebug:
  void GetInt(const GetIntCallback& callback) override;
};

}  // namespace ledger

#endif
