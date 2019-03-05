// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_SYSTEM_MONITOR_HARVESTER_HARVESTER_H_
#define GARNET_BIN_SYSTEM_MONITOR_HARVESTER_HARVESTER_H_

#include <zircon/types.h>
#include <string>

#include "garnet/lib/system_monitor/dockyard/dockyard.h"

namespace harvester {

typedef std::vector<std::pair<const std::string, uint64_t>> SampleList;
typedef std::vector<std::pair<uint64_t, uint64_t>> SampleListById;

typedef int HarvesterStatus;
constexpr HarvesterStatus OK = 0;
constexpr HarvesterStatus ERROR = -1;

class Harvester {
 public:
  virtual ~Harvester() {}

  // Initialize the Harvester.
  virtual bool Init() = 0;

  // Send a single sample to the Dockyard.
  virtual HarvesterStatus SendSample(const std::string& stream_name,
                                     uint64_t value) = 0;

  // Send a list of samples with the same timestamp to the Dockyard.
  virtual HarvesterStatus SendSampleList(const SampleList list) = 0;
};

// Gather*Samples collect samples for a given subject. They are grouped to make
// the code more manageable and for enabling/disabling categories in the future.
void GatherCpuSamples(zx_handle_t root_resource, Harvester* harvester);
void GatherMemorySamples(zx_handle_t root_resource, Harvester* harvester);
void GatherThreadSamples(zx_handle_t root_resource, Harvester* harvester);

}  // namespace harvester

#endif  // GARNET_BIN_SYSTEM_MONITOR_HARVESTER_HARVESTER_H_
