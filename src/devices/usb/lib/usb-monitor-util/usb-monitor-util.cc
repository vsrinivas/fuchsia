// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/usb-monitor-util/usb-monitor-util.h"

#include <fuchsia/hardware/usb/request/c/banjo.h>
#include <zircon/system/ulib/trace/include/lib/trace/event.h>

#include "zircon/system/ulib/fbl/include/fbl/auto_lock.h"

void USBMonitor::Start() {
  fbl::AutoLock start_lock(&mutex_);
  if (!started_) {
    TRACE_INSTANT("USB Monitor Util", "START", TRACE_SCOPE_PROCESS);
    started_ = true;
  }
}

void USBMonitor::Stop() {
  fbl::AutoLock start_lock(&mutex_);
  if (started_) {
    TRACE_INSTANT("USB Monitor Util", "STOP", TRACE_SCOPE_PROCESS);
    started_ = false;
  }
}

bool USBMonitor::Started() const {
  fbl::AutoLock start_lock(&mutex_);
  return started_;
}

void USBMonitor::AddRecord(usb_request_t request) {
  ++num_records_;
  TRACE_INSTANT("USB Monitor Util", "STOP", TRACE_SCOPE_PROCESS);
}

USBMonitorStats USBMonitor::GetStats() const { return USBMonitorStats{num_records_.load()}; }
