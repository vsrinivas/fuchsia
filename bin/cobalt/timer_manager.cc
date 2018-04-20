// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/cobalt/timer_manager.h"

#include <thread>

namespace cobalt {

using cobalt::Status;
using std::string;

TimerManager::TimerManager() {}
TimerManager::~TimerManager() {}

bool TimerManager::isReady(const TimerVal& timer_val){
	return timer_val.start_timestamp > 0 && timer_val.end_timestamp > 0;
}

bool TimerManager::isMultipart(const TimerVal& timer_val){
	return timer_val.part_name == "";
}

bool TimerManager::isValidTimerArguments(fidl::StringPtr timer_id,
	                              uint32_t timeout_s) {
  return !timer_id.is_null() && timeout_s > 0 && timeout_s < 300;
}

void TimerManager::Start() {}

Status TimerManager::GetTimerValWithStart(uint32_t metric_id, uint32_t encoding_id,
    const std::string& timer_id, uint64_t timestamp, uint32_t timeout_s,
    TimerVal* timer_val){
    // TODO(ninai)
    return Status::OK;
}

Status TimerManager::GetTimerValWithEnd(const std::string& timer_id,
    uint64_t timestamp, uint32_t timeout_s, TimerVal* timer_val_ptr){
  // TODO(ninai)
  return Status::OK;
}

Status TimerManager::GetTimerValWithEnd(const std::string& timer_id,
    uint64_t timestamp, uint32_t timeout_s, const std::string& part_name,
    fidl::VectorPtr<ObservationValue> observation, TimerVal* timer_val_ptr){
    // TODO(ninai)
	return Status::OK;
}

void TimerManager::DeleteExpiredTimersEvery(std::chrono::seconds frequency) {
   // TODO(ninai)
}

void TimerManager::DeleteTimer(const string& timer_id) {
	// TODO(ninai)
}
}  // namespace cobalt
