// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_BACKOFF_TESTING_TEST_BACKOFF_H_
#define LIB_BACKOFF_TESTING_TEST_BACKOFF_H_

#include <lib/fit/function.h>

#include "lib/backoff/backoff.h"

namespace backoff {

class TestBackoff : public Backoff {
 public:
  TestBackoff();
  ~TestBackoff() override;

  // Backoff:
  zx::duration GetNext() override;
  void Reset() override;

  void SetOnGetNext(fit::closure on_get_next);

  zx::duration backoff_to_return = zx::sec(0);

  int get_next_count = 0;
  int reset_count = 0;

 private:
  fit::closure on_get_next_;
  FXL_DISALLOW_COPY_AND_ASSIGN(TestBackoff);
};

}  // namespace backoff

#endif  // LIB_BACKOFF_TESTING_TEST_BACKOFF_H_
