// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_MEMORY_METRICS_PRINTER_H_
#define SRC_DEVELOPER_MEMORY_METRICS_PRINTER_H_

#include <iostream>
#include <sstream>
#include <string>

#include <src/lib/fxl/macros.h>

#include "src/developer/memory/metrics/capture.h"
#include "src/developer/memory/metrics/digest.h"
#include "src/developer/memory/metrics/summary.h"

namespace memory {

extern const size_t kMaxFormattedStringSize;
const char* FormatSize(uint64_t size, char* buffer);

enum Sorted { UNSORTED, SORTED };

class Printer {
 public:
  explicit Printer(std::ostream& os) : os_(os) {}
  void PrintCapture(const Capture& capture, CaptureLevel level, Sorted sorted);
  void PrintSummary(const Summary& summary, CaptureLevel level, Sorted sorted);
  void PrintDigest(const Digest& digest);
  void OutputSummary(const Summary& summary, Sorted sorted, zx_koid_t pid);
  void OutputDigest(const Digest& digest);

 private:
  void OutputSizes(const Sizes& sizes);
  std::ostream& os_;
  FXL_DISALLOW_COPY_AND_ASSIGN(Printer);
};

}  // namespace memory

#endif  // SRC_DEVELOPER_MEMORY_METRICS_PRINTER_H_
