// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_BACKOFF_TEST_TEST_BACKOFF_H_
#define APPS_LEDGER_SRC_BACKOFF_TEST_TEST_BACKOFF_H_

#include "apps/ledger/src/backoff/backoff.h"
#include "lib/ftl/functional/closure.h"
#include "lib/ftl/time/time_delta.h"

namespace backoff {

namespace test {

class TestBackoff : public Backoff {
 public:
  TestBackoff();
  ~TestBackoff() override;

  // Backoff:
  ftl::TimeDelta GetNext() override;
  void Reset() override;

  void SetOnGetNext(ftl::Closure on_get_next);

  ftl::TimeDelta backoff_to_return = ftl::TimeDelta::FromSeconds(0);

  int get_next_count = 0;
  int reset_count = 0;

 private:
  ftl::Closure on_get_next_;
  FTL_DISALLOW_COPY_AND_ASSIGN(TestBackoff);
};

}  // namespace test

}  // namespace backoff

#endif  // APPS_LEDGER_SRC_BACKOFF_TEST_TEST_BACKOFF_H_
