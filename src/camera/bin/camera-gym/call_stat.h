// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_CAMERA_BIN_CAMERA_GYM_CALL_STAT_H_
#define SRC_CAMERA_BIN_CAMERA_GYM_CALL_STAT_H_

#include <inttypes.h>
#include <lib/zx/time.h>
#include <zircon/time.h>

namespace camera {

class CallStat {
 public:
  zx_time_t call_time() const { return call_time_; }
  uint32_t call_counter() const { return call_counter_; }
  uint32_t error_counter() const { return error_counter_; }

  void SetCallTime() { call_time_ = zx_clock_get_monotonic(); }
  void IncCallCounter() { call_counter_++; }
  void IncErrorCounter() { error_counter_++; }

  void Enter() {
    SetCallTime();
    IncCallCounter();
  }

 private:
  zx_time_t call_time_ = 0;
  uint32_t call_counter_ = 0;
  uint32_t error_counter_ = 0;
};

}  // namespace camera

#endif  // SRC_CAMERA_BIN_CAMERA_GYM_CALL_STAT_H_
