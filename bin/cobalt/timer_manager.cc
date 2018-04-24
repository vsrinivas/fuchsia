// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/cobalt/timer_manager.h"

#include <thread>

namespace cobalt {

using cobalt::Status;
using std::string;
using wlan::SystemClock;

void TimerVal::AddStart(uint32_t metric_id, uint32_t encoding_id,
                        int64_t timestamp) {
  this->metric_id = metric_id;
  this->encoding_id = encoding_id;
  this->start_timestamp = timestamp;
}

void TimerVal::AddEnd(int64_t timestamp, const std::string& part_name,
                      fidl::VectorPtr<ObservationValue> observation) {
  this->end_timestamp = timestamp;
  this->part_name = std::move(part_name);
  this->observation = std::move(observation);
}

TimerManager::TimerManager() : clock_(new SystemClock()) {}
TimerManager::~TimerManager() {}

bool TimerManager::isReady(const std::unique_ptr<TimerVal>& timer_val_ptr) {
  if (!timer_val_ptr) {
    return false;
  }
  FXL_DCHECK(timer_val_ptr->start_timestamp > 0 &&
             timer_val_ptr->end_timestamp > 0);
  return true;
}

bool TimerManager::isMultipart(const std::unique_ptr<TimerVal>& timer_val_ptr) {
  return timer_val_ptr && timer_val_ptr->part_name != "";
}

bool TimerManager::isValidTimerArguments(fidl::StringPtr timer_id,
                                         int64_t timestamp,
                                         uint32_t timeout_s) {
  return !timer_id.is_null() && timer_id.get() != "" && timestamp > 0 &&
         timeout_s > 0 && timeout_s < kMaxTimerTimeout;
}

Status TimerManager::GetTimerValWithStart(
    uint32_t metric_id, uint32_t encoding_id, const std::string& timer_id,
    int64_t timestamp, uint32_t timeout_s,
    std::unique_ptr<TimerVal>* timer_val_ptr) {
  if (!timer_val_ptr ||
      !isValidTimerArguments(timer_id, timestamp, timeout_s)) {
    return Status::INVALID_ARGUMENTS;
  }

  timer_val_ptr->reset();
  auto timer_val_iter = timer_values_.find(timer_id);

  // An expired timer with that timer_id exists.
  if (timer_val_iter != timer_values_.end() &&
      timer_val_iter->second->expiry_time < clock_->Now()) {
    timer_values_.erase(timer_val_iter);
    timer_val_iter = timer_values_.end();
  }

  // No timer with the timer_id
  if (timer_val_iter == timer_values_.end()) {
    auto& timer = timer_values_[timer_id];
    timer.reset(new TimerVal());
    timer->AddStart(metric_id, encoding_id, timestamp);
    timer->expiry_time = clock_->Now() + zx::sec(timeout_s);
    return Status::OK;
  }

  // A valid start to a timer already exists with that timer_id.
  if (timer_val_iter->second->start_timestamp > 0) {
    timer_values_.erase(timer_id);
    return Status::FAILED_PRECONDITION;
  }

  // Return TimerVal with start_timestamp and end_timestamp.
  timer_val_iter->second->AddStart(metric_id, encoding_id, timestamp);
  *timer_val_ptr = std::move(timer_val_iter->second);
  timer_values_.erase(timer_val_iter);

  return Status::OK;
}

Status TimerManager::GetTimerValWithEnd(
    const std::string& timer_id, int64_t timestamp, uint32_t timeout_s,
    std::unique_ptr<TimerVal>* timer_val_ptr) {
  if (!timer_val_ptr ||
      !isValidTimerArguments(timer_id, timestamp, timeout_s)) {
    return Status::INVALID_ARGUMENTS;
  }

  timer_val_ptr->reset();
  auto timer_val_iter = timer_values_.find(timer_id);

  // An expired timer with that timer_id exists.
  if (timer_val_iter != timer_values_.end() &&
      timer_val_iter->second->expiry_time < clock_->Now()) {
    timer_values_.erase(timer_val_iter);
    timer_val_iter = timer_values_.end();
  }

  // No timer with the timer_id.
  if (timer_val_iter == timer_values_.end()) {
    auto& timer = timer_values_[timer_id];
    timer.reset(new TimerVal());
    timer->end_timestamp = timestamp;
    timer->expiry_time = clock_->Now() + zx::sec(timeout_s);
    return Status::OK;
  }

  // A valid end to a timer already exists with that timer_id.
  if (timer_val_iter->second->end_timestamp > 0) {
    timer_values_.erase(timer_val_iter);
    return Status::FAILED_PRECONDITION;
  }

  // Return TimerVal with start_timestamp and end_timestamp.
  timer_val_iter->second->end_timestamp = timestamp;
  *timer_val_ptr = std::move(timer_val_iter->second);
  timer_values_.erase(timer_val_iter);

  return Status::OK;
}

Status TimerManager::GetTimerValWithEnd(
    const std::string& timer_id, int64_t timestamp, uint32_t timeout_s,
    const std::string& part_name, fidl::VectorPtr<ObservationValue> observation,
    std::unique_ptr<TimerVal>* timer_val_ptr) {
  if (!timer_val_ptr ||
      !isValidTimerArguments(timer_id, timestamp, timeout_s)) {
    return Status::INVALID_ARGUMENTS;
  }

  timer_val_ptr->reset();
  auto timer_val_iter = timer_values_.find(timer_id);

  // An expired timer with that timer_id exists.
  if (timer_val_iter != timer_values_.end() &&
      timer_val_iter->second->expiry_time < clock_->Now()) {
    timer_values_.erase(timer_val_iter);
    timer_val_iter = timer_values_.end();
  }

  // No timer with the timer_id.
  if (timer_val_iter == timer_values_.end()) {
    auto& timer = timer_values_[timer_id];
    timer.reset(new TimerVal());
    timer->AddEnd(timestamp, part_name, std::move(observation));
    timer->expiry_time = clock_->Now() + zx::sec(timeout_s);
    return Status::OK;
  }

  // A valid end to a timer already exists with that timer_id.
  if (timer_val_iter->second->end_timestamp > 0) {
    timer_values_.erase(timer_val_iter);
    return Status::FAILED_PRECONDITION;
  }

  // Return TimerVal with start_timestamp and end_timestamp.
  timer_val_iter->second->AddEnd(timestamp, part_name, std::move(observation));
  *timer_val_ptr = std::move(timer_val_iter->second);
  timer_values_.erase(timer_val_iter);

  return Status::OK;
}
}  // namespace cobalt
