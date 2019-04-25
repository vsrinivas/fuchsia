// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_TESTING_LEDGER_MEMORY_USAGE_H_
#define SRC_LEDGER_BIN_TESTING_LEDGER_MEMORY_USAGE_H_

#include <lib/fit/function.h>
#include <lib/zx/object.h>
#include <lib/zx/process.h>

namespace ledger {

// Allows estimating Ledger's memory usage. Assumes there is a single ledger
// process running throughout the lifetime of a |LedgerMemoryEstimator| object.
class LedgerMemoryEstimator {
 public:
  LedgerMemoryEstimator();
  ~LedgerMemoryEstimator();

  // Initializes the |LedgerMemoryEstimator|. This must be called before any
  // call to |GetLedgerMemoryUsage|. Ledger must already be running before
  // |Init()| is called.
  bool Init();

  // Updates |memory| to store the memory usage, in bytes, of the Ledger binary.
  // This only includes the private bytes, not the shared memory. Returns true
  // on success; false otherwise.
  // Note that a successfull call to |Init()| must be made before calling this
  // method.
  bool GetLedgerMemoryUsage(uint64_t* memory);

 private:
  zx::process ledger_task_;
};

}  // namespace ledger

#endif  // SRC_LEDGER_BIN_TESTING_LEDGER_MEMORY_USAGE_H_
