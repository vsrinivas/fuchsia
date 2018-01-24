// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_LIB_BACKOFF_TESTING_TEST_BACKOFF_H_
#define PERIDOT_LIB_BACKOFF_TESTING_TEST_BACKOFF_H_

#include "lib/fxl/functional/closure.h"
#include "lib/fxl/time/time_delta.h"
#include "peridot/lib/backoff/backoff.h"

namespace backoff {

// Implementation of backoff that always returns a time delta of zero, and keeps
// track of method calls.
class TestBackoff : public Backoff {
 public:
  TestBackoff();
  ~TestBackoff() override;

  // Backoff:
  fxl::TimeDelta GetNext() override;
  void Reset() override;

  // Set a function to be notified when GetNext() is called.
  void SetOnGetNext(fxl::Closure on_get_next);

  fxl::TimeDelta backoff_to_return = fxl::TimeDelta::FromSeconds(0);

  // Number of times GetNext() was called.
  int get_next_count = 0;

  // Number of times Reset() was called.
  int reset_count = 0;

 private:
  fxl::Closure on_get_next_;
  FXL_DISALLOW_COPY_AND_ASSIGN(TestBackoff);
};

}  // namespace backoff

#endif  // PERIDOT_LIB_BACKOFF_TESTING_TEST_BACKOFF_H_
