// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_BACKOFF_TESTING_TEST_BACKOFF_H_
#define SRC_LIB_BACKOFF_TESTING_TEST_BACKOFF_H_

#include <lib/fit/function.h>

#include "src/lib/backoff/backoff.h"

namespace backoff {

class TestBackoff : public Backoff {
 public:
  // Backoff with GetNext returning kDefaultBackoffDuration.
  TestBackoff();
  // Backoff with GetNext returning |duration|.
  TestBackoff(zx::duration duration);
  ~TestBackoff() override;

  // Backoff:
  zx::duration GetNext() override;
  void Reset() override;

  void SetOnGetNext(fit::closure on_get_next);

  static constexpr zx::duration kDefaultBackoffDuration = zx::sec(1);
  int get_next_count = 0;
  int reset_count = 0;

 private:
  zx::duration backoff_to_return_;
  fit::closure on_get_next_;
  FXL_DISALLOW_COPY_AND_ASSIGN(TestBackoff);
};

}  // namespace backoff

#endif  // SRC_LIB_BACKOFF_TESTING_TEST_BACKOFF_H_
