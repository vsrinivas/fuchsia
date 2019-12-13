// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_LIB_TIMEKEEPER_TEST_CLOCK_H_
#define SRC_LEDGER_LIB_TIMEKEEPER_TEST_CLOCK_H_

#include "src/ledger/lib/timekeeper/clock.h"

namespace ledger {

// Implementation of |Clock| that returned a pre-set time.
class TestClock : public Clock {
 public:
  TestClock();
  ~TestClock() override;

  template <zx_clock_t kClockId>
  void Set(zx::basic_time<kClockId> time) {
    current_time_ = time.get();
  }

 private:
  zx_status_t GetTime(zx_clock_t clock_id, zx_time_t* time) const override;
  zx_time_t GetMonotonicTime() const override;

  zx_time_t current_time_;
};

}  // namespace ledger

#endif  // SRC_LEDGER_LIB_TIMEKEEPER_TEST_CLOCK_H_
