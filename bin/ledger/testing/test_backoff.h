// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_TESTING_TEST_BACKOFF_H_
#define PERIDOT_BIN_LEDGER_TESTING_TEST_BACKOFF_H_

#include <lib/backoff/backoff.h>

namespace ledger {

// Dummy implementation of a backoff policy.
// Counts the number of |GetNext| calls that happened.
// TODO(LE-583): Default to a non-zero backoff duration.
class TestBackoff : public backoff::Backoff {
 public:
  // Backoff with GetNext returning a duration of 0.
  TestBackoff();
  // Backoff with GetNext returning a duration of 0 and incrementing
  // |*get_next_count| (pointer must not be null).
  explicit TestBackoff(int* get_next_count);
  // Backoff with GetNext returning |duration| and incrementing
  // |*get_next_count| (pointer must not be null).
  explicit TestBackoff(int* get_next_count, zx::duration duration);
  ~TestBackoff() override;

  zx::duration GetNext() override;

  void Reset() override;
 private:
  int* get_next_count_;
  zx::duration duration_;
};

}  // namespace ledger

#endif  // PERIDOT_BIN_LEDGER_TESTING_TEST_BACKOFF_H_