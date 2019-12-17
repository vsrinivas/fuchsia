// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_PLATFORM_LEDGER_MEMORY_ESTIMATOR_H_
#define SRC_LEDGER_BIN_PLATFORM_LEDGER_MEMORY_ESTIMATOR_H_

#include <lib/fit/function.h>

namespace ledger {

// Allows estimating Ledger's memory usage. Assumes there is a single ledger process running
// throughout the lifetime of a |LedgerMemoryEstimator| object.
class LedgerMemoryEstimator {
 public:
  LedgerMemoryEstimator() = default;
  virtual ~LedgerMemoryEstimator() = default;

  // Updates |memory| to store the memory usage, in bytes, of the Ledger binary. This only includes
  // the private bytes, not the shared memory. Returns true on success; false otherwise.
  // Note that a successfull call to |Init()| must be made before calling this method.
  virtual bool GetLedgerMemoryUsage(uint64_t* memory) = 0;

  // Updates |memory| to store the memory usage, in bytes, of the current process. Returns true on
  // success; false otherwise.
  virtual bool GetCurrentProcessMemoryUsage(uint64_t* memory) = 0;
};

}  // namespace ledger

#endif  // SRC_LEDGER_BIN_PLATFORM_LEDGER_MEMORY_ESTIMATOR_H_
