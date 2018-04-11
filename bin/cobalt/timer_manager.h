// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_COBALT_TIMER_H
#define GARNET_BIN_COBALT_TIMER_H

#include <stdlib.h>

#include <chrono>
#include <mutex>
#include <string>
#include <queue>
#include <unordered_map>

#include <fuchsia/cpp/cobalt.h>

namespace cobalt {

// How often the expiry thread should run.
const std::chrono::seconds kExpiryThreadFrequency(60);

// Used to store all necessary values for a Timer to be able to create an
// Observation.
struct TimerVal {
  uint32_t metric_id;
  uint32_t encoding_id;
  uint64_t start_timestamp;
  uint64_t end_timestamp;
  std::chrono::system_clock::time_point ttl;
  std::string part_name;
  // The remaining fields of a multipart obervation.
  std::vector<ObservationValue> observation;
};

// Class which stores partial timer values as they are encountered. Once both
// the start and end value of the timer have been encountered the timer's values
// are returned as a TimerVal.
class TimerManager {
 public:
  // Constructs a TimerManager Object
  TimerManager();

  // The destructor will stop the expiry thread which deletes expired timers.
  ~TimerManager();

  // Checks if the given TimerVal is initialized or empty.
  static bool isInitialized(const TimerVal& timer_val);

  // Checks if the given TimerVal contains a multipart observation.
  static bool isMultipart(const TimerVal& timer_val);

  // Checks that the arguments could be valid timer arguments.
  static bool isValidTimerArguments(fidl::StringPtr timer_id,
                                    uint32_t timeout_s);

  // Starts an expiry thread which deletes expired timers in the background.
  void Start();

  // Returns a TimerVal if there is a valid timer with the timer_id. If no
  // valid timer exists it creates a new timer with the start data. If a timer
  // with the same timer_id and different start timestamp exists it returns an
  // error.
  cobalt::Status GetTimerValWithStart(uint32_t metric_id, uint32_t encoding_id,
    const std::string& timer_id, uint64_t timestamp, uint32_t timeout_s,
    TimerVal* timer_val_ptr);

  // Returns a TimerVal if there is a valid timer with the timer_id. If no
  // valid timer exists it creates a new timer with the end data. If a timer
  // with the same timer_id and different end timestamp exists it returns an
  // error.
  cobalt::Status GetTimerValWithEnd(const std::string& timer_id,
    uint64_t timestamp, uint32_t timeout_s, TimerVal* timer_val_ptr);

  // Returns a TimerVal if there is a valid timer with the timer_id. If no
  // valid timer exists it creates a new timer with the end data. If a timer
  // with the same timer_id and different end timestamp exists it returns an
  // error.
  cobalt::Status GetTimerValWithEnd(const std::string& timer_id,
    uint64_t timestamp, uint32_t timeout_s, const std::string& part_name,
    fidl::VectorPtr<ObservationValue> observation, TimerVal* timer_val_ptr);

 private:
  // Runs DeleteExpiredTimers once every given period
  void DeleteExpiredTimersEvery(std::chrono::seconds frequency);

  // Deletes all data associated with the timer_id. Does not use locking.
  void DeleteTimer(const std::string& timer_id);

  // Map from a timer's ttl to its timer_id.
  std::map<std::chrono::seconds, std::string> timer_ttl_;
  // Map from timer_id to the TimerVal values associated with it.
  std::unordered_map<std::string, TimerVal> timer_values_;
  // Lock which ensures the maps' integrity in a multithreaded environment.
  std::mutex mutex_lock_;
};

}  // namespace cobalt

#endif  // GARNET_BIN_COBALT_TIMER_H
