// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_HWSTRESS_CPU_WORKLOADS_H_
#define GARNET_BIN_HWSTRESS_CPU_WORKLOADS_H_

#include <string>
#include <utility>
#include <vector>

#include "cpu_stressor.h"

namespace hwstress {

// A CPU stress workload.
struct Workload {
  std::string name;
  std::function<void(const StopIndicator& indicator)> work;
};

// Return a list of available workloads.
std::vector<Workload> GetWorkloads();

}  // namespace hwstress

#endif  // GARNET_BIN_HWSTRESS_CPU_WORKLOADS_H_
