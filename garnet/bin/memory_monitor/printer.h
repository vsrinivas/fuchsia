// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEMORY_MONITOR_PRINTER_H_
#define GARNET_BIN_MEMORY_MONITOR_PRINTER_H_

#include <iostream>
#include <src/lib/fxl/macros.h>
#include <string>
#include <sstream>

#include "garnet/bin/memory_monitor/capture.h"
#include "garnet/bin/memory_monitor/summary.h"

namespace memory {

enum Sorted { UNSORTED, SORTED };

class Printer {
 public:
  Printer(std::ostream& os) : os_(os) {}
  void PrintCapture(const Capture& capture, CaptureLevel level, Sorted sorted);
  void PrintSummary(const Summary& summary, CaptureLevel level, Sorted sorted);
  void OutputSummary(const Summary& summary, Sorted sorted, zx_koid_t pid);

 private:
  std::ostream& os_;
  FXL_DISALLOW_COPY_AND_ASSIGN(Printer);
};

}  // namespace memory

#endif  // GARNET_BIN_MEMORY_MONITOR_PRINTER_H_
