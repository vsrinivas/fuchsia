// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_SYSTEM_MONITOR_HARVESTER_HARVESTER_LOCAL_H_
#define GARNET_BIN_SYSTEM_MONITOR_HARVESTER_HARVESTER_LOCAL_H_

#include <string>

#include "harvester.h"

namespace harvester {

// A local harvester that simply prints to stdout rather than sending messages
// to the Dockyard.
class HarvesterLocal : public Harvester {
 public:
  HarvesterLocal() {}

  // |Harvester|.
  virtual HarvesterStatus Init() override;

  // |Harvester|.
  virtual HarvesterStatus SendInspectJson(const std::string& stream_name,
                                          const std::string& json) override;

  // |Harvester|.
  virtual HarvesterStatus SendSample(const std::string& stream_name,
                                     uint64_t value) override;

  // |Harvester|.
  virtual HarvesterStatus SendSampleList(const SampleList list) override;
};

}  // namespace harvester

#endif  // GARNET_BIN_SYSTEM_MONITOR_HARVESTER_HARVESTER_LOCAL_H_
