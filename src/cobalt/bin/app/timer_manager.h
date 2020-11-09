// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_COBALT_BIN_APP_TIMER_MANAGER_H_
#define SRC_COBALT_BIN_APP_TIMER_MANAGER_H_

#include <fuchsia/cobalt/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/time.h>
#include <stdlib.h>

#include <chrono>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <unordered_map>

namespace cobalt {

// Used to store all necessary values for a Timer to be able to log an Event.
struct TimerVal {
  // The metric_id of the event we will create.
  uint32_t metric_id;
  // The encoding_id used in the event we will log.
  uint32_t encoding_id;
  // The event_code for the event.
  uint32_t event_code;
  // The component for the event.
  std::string component;
  // When the timer starts.
  int64_t start_timestamp;
  // When the timer ends.
  int64_t end_timestamp;
  // The time at which the timer is expired.
  zx::time expiry_time;
  // Task which will delete the timer once it is expired.
  async::TaskClosure expiry_task;
  // The name of the timer field/part if it is a multipart obervation.
  std::string part_name;

  // Stores the start-related arguments in the given TimerVal.
  void AddStart(uint32_t metric_id, uint32_t event_code, const std::string& component,
                uint32_t encoding_id, int64_t timestamp);

  // Stores the end-related arguments in the given TimerVal.
  void AddEnd(int64_t timestamp, const std::string& part_name);
};

// Stores partial timer values as they are encountered. Once both the start and
// end value of the timer have been encountered the timer's values are returned
// as a TimerVal.
class TimerManager {
 public:
  // Constructs a TimerManager Object. Uses the given dispatcher to
  // schedule tasks which delete timer data once it has expired.
  TimerManager(async_dispatcher_t* dispatcher);

  // In some situations (e.g. fuzzing) the currently active dispatcher may change over time. Rather
  // than constructing a new TimerManager each time, we just update the dispatcher.
  void UpdateDispatcher(async_dispatcher_t* dispatcher) { dispatcher_ = dispatcher; };

  ~TimerManager();

  // Checks if the given TimerVal contains all the information it needs to log
  // an event. That means it was populated by both StartTimer and EndTimer
  // calls.
  static bool isReady(const std::unique_ptr<TimerVal>& timer_val_ptr);

  // Checks that the arguments are valid timer arguments.
  static bool isValidTimerArguments(fidl::StringPtr timer_id, int64_t timestamp,
                                    uint32_t timeout_s);

  // Populates the TimerVal parameter with the timer's values if there is a
  // valid timer with the timer_id. If no valid timer exists it creates a new
  // timer with the start data and resets the TimerVal ptr. If a timer with the
  // same timer_id and different start timestamp exists it returns
  // FAILED_PRECONDITION. If timer_ID or timeout_s is invalid, returns
  // INVALID_ARGUMENTS.
  fuchsia::cobalt::Status GetTimerValWithStart(
      uint32_t metric_id, uint32_t event_code, const std::string& component,
      uint32_t encoding_id, const std::string& timer_id, int64_t timestamp,
      uint32_t timeout_s, std::unique_ptr<TimerVal>* timer_val_ptr);

  // Populates the TimerVal parameter with the timer's values if there is a
  // valid timer with the timer_id. If no valid timer exists it creates a new
  // timer with the end data and resets the TimerVal ptr. If a timer with the
  // same timer_id and different end timestamp exists it returns an error.
  fuchsia::cobalt::Status GetTimerValWithEnd(
      const std::string& timer_id, int64_t timestamp, uint32_t timeout_s,
      std::unique_ptr<TimerVal>* timer_val_ptr);

  // Populates the TimerVal parameter with the timer's values if there is a
  // valid timer with the timer_id. If no valid timer exists it creates a new
  // timer with the end data and resets the TimerVal ptr. If a timer with the
  // same timer_id and different end timestamp exists it returns an error.
  fuchsia::cobalt::Status GetTimerValWithEnd(
      const std::string& timer_id, int64_t timestamp, uint32_t timeout_s,
      const std::string& part_name, std::unique_ptr<TimerVal>* timer_val_ptr);

 private:
  // Schedules a task which will delete the timer entries associated with
  // timer_id when it expires.
  // timeout_s : the timer timer_id will be deleted after timeout_s seconds.
  // timer_val_ptr : the task will be stored in the given object until it is
  //                 executed or cancelled. Deleting the TimerVal will cancel
  //                 the task.
  void ScheduleExpiryTask(const std::string& timer_id, uint32_t timeout_s,
                          std::unique_ptr<TimerVal>* timer_val_ptr);

  // Copies the data found in the iterator entry to the TimerVal pointer
  // provided. It then deletes the data associated with it from the map, which
  // includes cancelling the pending expiry task.
  void MoveTimerToTimerVal(
      std::unordered_map<std::string, std::unique_ptr<TimerVal>>::iterator* timer_val_iter,
      std::unique_ptr<TimerVal>* timer_val_ptr);

  // Map from timer_id to the TimerVal values associated with it.
  std::unordered_map<std::string, std::unique_ptr<TimerVal>> timer_values_;
  // Async dispatcher used for deleting expired timer entries.
  async_dispatcher_t* dispatcher_;  // not owned.
};

}  // namespace cobalt

#endif  // SRC_COBALT_BIN_APP_TIMER_MANAGER_H_
