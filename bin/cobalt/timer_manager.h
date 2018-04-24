// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_COBALT_TIMER_H
#define GARNET_BIN_COBALT_TIMER_H

#include <stdlib.h>

#include <chrono>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <unordered_map>

#include <fuchsia/cpp/cobalt.h>
#include <lib/fxl/logging.h>
#include <lib/zx/time.h>

#include "garnet/lib/wlan/mlme/include/wlan/mlme/clock.h"

namespace cobalt {

const uint32_t kMaxTimerTimeout = 300;

// Used to store all necessary values for a Timer to be able to create an
// Observation.
struct TimerVal {
  // The metric_id of the observation we will create.
  uint32_t metric_id;
  // The encoding_id used in the observation we will create.
  uint32_t encoding_id;
  // When the timer starts.
  int64_t start_timestamp;
  // When the timer ends.
  int64_t end_timestamp;
  // The time at which the timer is expired.
  zx::time expiry_time;
  // The name of the timer field/part if it is a multipart obervation.
  std::string part_name;
  // The remaining fields of a multipart obervation.
  fidl::VectorPtr<ObservationValue> observation;

  // Stores the start-related arguments in the given TimerVal.
  void AddStart(uint32_t metric_id, uint32_t encoding_id, int64_t timestamp);

  // Stores the end-related arguments in the given TimerVal.
  void AddEnd(int64_t timestamp, const std::string& part_name,
              fidl::VectorPtr<ObservationValue> observation);
};

// Stores partial timer values as they are encountered. Once both the start and
// end value of the timer have been encountered the timer's values are returned
// as a TimerVal.
class TimerManager {
 public:
  // Constructs a TimerManager Object
  TimerManager();

  ~TimerManager();

  // Checks if the given TimerVal contains all the information it needs to send
  // an observation. That means it was populated by both StartTimer and EndTimer
  // calls.
  static bool isReady(const std::unique_ptr<TimerVal>& timer_val_ptr);

  // Checks if the given TimerVal contains a multipart observation.
  static bool isMultipart(const std::unique_ptr<TimerVal>& timer_val_ptr);

  // Checks that the arguments are valid timer arguments.
  static bool isValidTimerArguments(fidl::StringPtr timer_id, int64_t timestamp,
                                    uint32_t timeout_s);

  // Populates the TimerVal parameter with the timer's values if there is a
  // valid timer with the timer_id. If no valid timer exists it creates a new
  // timer with the start data and resets the TimerVal ptr. If a timer with the
  // same timer_id and different start timestamp exists it returns
  // FAILED_PRECONDITION. If timer_ID or timeout_s is invalid, returns
  // INVALID_ARGUMENTS.
  cobalt::Status GetTimerValWithStart(uint32_t metric_id, uint32_t encoding_id,
                                      const std::string& timer_id,
                                      int64_t timestamp, uint32_t timeout_s,
                                      std::unique_ptr<TimerVal>* timer_val_ptr);

  // Populates the TimerVal parameter with the timer's values if there is a
  // valid timer with the timer_id. If no valid timer exists it creates a new
  // timer with the end data and resets the TimerVal ptr. If a timer with the
  // same timer_id and different end timestamp exists it returns an error.
  cobalt::Status GetTimerValWithEnd(const std::string& timer_id,
                                    int64_t timestamp, uint32_t timeout_s,
                                    std::unique_ptr<TimerVal>* timer_val_ptr);

  // Populates the TimerVal parameter with the timer's values if there is a
  // valid timer with the timer_id. If no valid timer exists it creates a new
  // timer with the end data and resets the TimerVal ptr. If a timer with the
  // same timer_id and different end timestamp exists it returns an error.
  cobalt::Status GetTimerValWithEnd(
      const std::string& timer_id, int64_t timestamp, uint32_t timeout_s,
      const std::string& part_name,
      fidl::VectorPtr<ObservationValue> observation,
      std::unique_ptr<TimerVal>* timer_val_ptr);

  // Tests can use private methods for setup.
  friend class TimerManagerTests;

 private:
  // Used for testing.
  void SetClockForTesting(std::shared_ptr<wlan::Clock> clock) {
    clock_ = clock;
  }

  // Map from timer_id to the TimerVal values associated with it.
  std::unordered_map<std::string, std::unique_ptr<TimerVal>> timer_values_;
  // The clock is abstracted so that friend tests can set a non-system clock.
  std::shared_ptr<wlan::Clock> clock_;
};

}  // namespace cobalt

#endif  // GARNET_BIN_COBALT_TIMER_H
