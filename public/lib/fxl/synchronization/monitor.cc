// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fxl/synchronization/monitor.h"

#include "lib/fxl/logging.h"

namespace fxl {

Monitor::Monitor() {}

Monitor::~Monitor() {}

void Monitor::Enter() {
  mutex_.Lock();
}

void Monitor::Exit() {
  mutex_.Unlock();
}

void Monitor::Signal() {
  cv_.Signal();
}

void Monitor::Wait() {
  cv_.Wait(&mutex_);
}

MonitorLocker::MonitorLocker(Monitor* monitor) : monitor_(monitor) {
  FXL_DCHECK(monitor_);
  monitor_->Enter();
}

MonitorLocker::~MonitorLocker() {
  monitor_->Exit();
}

void MonitorLocker::Wait() FXL_NO_THREAD_SAFETY_ANALYSIS {
  monitor_->Wait();
}

void MonitorLocker::Signal() {
  monitor_->Signal();
}

}  // namespace fxl
