// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_LIB_BACKOFF_BACKOFF_H_
#define SRC_LEDGER_LIB_BACKOFF_BACKOFF_H_

#include <lib/zx/time.h>

namespace ledger {

// Interface for a backoff policy.
class Backoff {
 public:
  Backoff() {}
  Backoff(const Backoff&) = delete;
  Backoff& operator=(const Backoff&) = delete;
  virtual ~Backoff() {}

  virtual zx::duration GetNext() = 0;
  virtual void Reset() = 0;
};

}  // namespace ledger

#endif  // SRC_LEDGER_LIB_BACKOFF_BACKOFF_H_
