// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_PLATFORM_FUCHSIA_LEDGER_MEMORY_ESTIMATOR_H_
#define SRC_LEDGER_BIN_PLATFORM_FUCHSIA_LEDGER_MEMORY_ESTIMATOR_H_

#include <lib/fit/function.h>
#include <lib/zx/process.h>

#include "src/ledger/bin/platform/ledger_memory_estimator.h"

namespace ledger {

class FuchsiaLedgerMemoryEstimator : public LedgerMemoryEstimator {
 public:
  FuchsiaLedgerMemoryEstimator() = default;
  ~FuchsiaLedgerMemoryEstimator() override = default;

  // LedgerMemoryEstimator:
  bool GetLedgerMemoryUsage(uint64_t* memory) override;
  bool GetCurrentProcessMemoryUsage(uint64_t* memory) override;
};

}  // namespace ledger

#endif  // SRC_LEDGER_BIN_PLATFORM_FUCHSIA_LEDGER_MEMORY_ESTIMATOR_H_
