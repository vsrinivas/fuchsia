// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/framework/coverage/event-queue.h"

namespace fuzzing {

CoverageEventQueue::~CoverageEventQueue() {
  std::lock_guard<std::mutex> lock(mutex_);
  stopped_ = true;
  events_.clear();
  available_.Signal();
}

Options CoverageEventQueue::GetOptions() {
  std::lock_guard<std::mutex> lock(mutex_);
  return CopyOptions(options_);
}

void CoverageEventQueue::SetOptions(Options options) {
  std::lock_guard<std::mutex> lock(mutex_);
  options_ = std::move(options);
}

void CoverageEventQueue::AddProcess(uint64_t target_id, InstrumentedProcess instrumented) {
  AddEvent(target_id, Payload::WithProcessStarted(std::move(instrumented)));
}

void CoverageEventQueue::AddLlvmModule(uint64_t target_id, LlvmModule llvm_module) {
  AddEvent(target_id, Payload::WithLlvmModuleAdded(std::move(llvm_module)));
}

void CoverageEventQueue::AddEvent(uint64_t target_id, Payload payload) {
  auto event = CoverageEvent::New();
  event->target_id = target_id;
  event->payload = std::move(payload);
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stopped_) {
      return;
    }
    events_.emplace_back(std::move(event));
    available_.Signal();
  }
}

CoverageEventPtr CoverageEventQueue::GetEvent() {
  available_.WaitFor("coverage events to be available");
  std::lock_guard<std::mutex> lock(mutex_);
  if (stopped_) {
    return CoverageEventPtr();
  }
  CoverageEventPtr event = std::move(events_.front());
  events_.pop_front();
  if (events_.empty()) {
    available_.Reset();
  }
  return event;
}

void CoverageEventQueue::Stop() {
  std::lock_guard<std::mutex> lock(mutex_);
  stopped_ = true;
  available_.Signal();
}

}  // namespace fuzzing
