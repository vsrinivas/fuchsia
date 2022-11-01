// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_USB_LIB_USB_MONITOR_UTIL_INCLUDE_USB_MONITOR_UTIL_USB_MONITOR_UTIL_H_
#define SRC_DEVICES_USB_LIB_USB_MONITOR_UTIL_INCLUDE_USB_MONITOR_UTIL_USB_MONITOR_UTIL_H_

#include <fuchsia/hardware/usb/request/c/banjo.h>

#include <atomic>

#include "zircon/system/ulib/fbl/include/fbl/mutex.h"

#ifdef __cplusplus

// Stores fields the USBMonitor stores
struct USBMonitorStats {
  unsigned int num_records;
};

// Records USB transactions and statistics on them.
class USBMonitor {
 public:
  // Start recording USB transactions. These are currently stored as traces.
  void Start();

  // Stop recording USB transactions.
  void Stop();

  // True if USB transaction recording is started.
  bool Started() const;

  // Records a new usb request.
  void AddRecord(usb_request_t request);

  // Returns statistics on the currently stored USB transactions.
  USBMonitorStats GetStats() const;

 private:
  bool started_ __TA_GUARDED(mutex_);
  std::atomic_uint num_records_ __TA_GUARDED(mutex_);
  fbl::Mutex mutex_;
};

#endif  // __cplusplus

#endif  // SRC_DEVICES_USB_LIB_USB_MONITOR_UTIL_INCLUDE_USB_MONITOR_UTIL_USB_MONITOR_UTIL_H_
