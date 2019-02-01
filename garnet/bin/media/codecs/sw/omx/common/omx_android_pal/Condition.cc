// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utils/Condition.h>

#include <utils/Mutex.h>

namespace android {

status_t Condition::wait(Mutex& mutex) {
  std::unique_lock<std::mutex> tmp(mutex.mutex_, std::adopt_lock);
  condition_.wait(tmp);
  tmp.release();
  // This _might_ be a lie compared to android's semantics when there's a
  // spurious wake, but relevant call sites don't appear to care.
  return OK;
}

status_t Condition::waitRelative(Mutex& mutex, nsecs_t relative_timeout) {
  std::unique_lock<std::mutex> tmp(mutex.mutex_, std::adopt_lock);
  condition_.wait_for(tmp, std::chrono::nanoseconds(relative_timeout));
  tmp.release();
  // This _might_ be a lie compared to android's semantics when there's a
  // spurious wake, but relevant call sites don't appear to care.
  return OK;
}

void Condition::signal() { condition_.notify_one(); }

void Condition::broadcast() { condition_.notify_all(); }

}  // namespace android
