// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_BACKOFF_BACKOFF_H_
#define PERIDOT_BIN_LEDGER_BACKOFF_BACKOFF_H_

#include "lib/fxl/macros.h"
#include "lib/fxl/time/time_delta.h"

namespace backoff {

// Interface for a backoff policy.
class Backoff {
 public:
  Backoff() {}
  virtual ~Backoff() {}

  virtual fxl::TimeDelta GetNext() = 0;
  virtual void Reset() = 0;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(Backoff);
};

}  // namespace backoff

#endif  // PERIDOT_BIN_LEDGER_BACKOFF_BACKOFF_H_
