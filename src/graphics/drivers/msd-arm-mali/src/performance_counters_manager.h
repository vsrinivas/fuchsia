// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERFORMANCE_COUNTERS_MANAGER_H
#define PERFORMANCE_COUNTERS_MANAGER_H

#include <vector>

// The manager handles keeping track of what performance counter flags are enabled or disabled.
class PerformanceCountersManager {
 public:
  virtual std::vector<uint64_t> EnabledPerfCountFlags() = 0;
};

#endif  // PERFORMANCE_COUNTERS_MANAGER_H
