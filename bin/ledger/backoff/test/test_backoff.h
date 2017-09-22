// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_BACKOFF_TEST_TEST_BACKOFF_H_
#define APPS_LEDGER_SRC_BACKOFF_TEST_TEST_BACKOFF_H_

#include "peridot/bin/ledger/backoff/backoff.h"
#include "lib/fxl/functional/closure.h"
#include "lib/fxl/time/time_delta.h"

namespace backoff {

namespace test {

class TestBackoff : public Backoff {
 public:
  TestBackoff();
  ~TestBackoff() override;

  // Backoff:
  fxl::TimeDelta GetNext() override;
  void Reset() override;

  void SetOnGetNext(fxl::Closure on_get_next);

  fxl::TimeDelta backoff_to_return = fxl::TimeDelta::FromSeconds(0);

  int get_next_count = 0;
  int reset_count = 0;

 private:
  fxl::Closure on_get_next_;
  FXL_DISALLOW_COPY_AND_ASSIGN(TestBackoff);
};

}  // namespace test

}  // namespace backoff

#endif  // APPS_LEDGER_SRC_BACKOFF_TEST_TEST_BACKOFF_H_
