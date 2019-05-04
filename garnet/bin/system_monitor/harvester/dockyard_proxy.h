// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_SYSTEM_MONITOR_HARVESTER_DOCKYARD_PROXY_H_
#define GARNET_BIN_SYSTEM_MONITOR_HARVESTER_DOCKYARD_PROXY_H_

#include <zircon/types.h>

#include <iostream>
#include <string>

#include "garnet/lib/system_monitor/dockyard/dockyard.h"

namespace harvester {

typedef std::vector<std::pair<const std::string, uint64_t>> SampleList;
typedef std::vector<std::pair<uint64_t, uint64_t>> SampleListById;
typedef std::vector<std::pair<const std::string, const std::string>>
    StringSampleList;

enum class DockyardProxyStatus : int {
  OK = 0,
  ERROR = -1,
};

std::ostream& operator<<(std::ostream& out, const DockyardProxyStatus& status);

class DockyardProxy {
 public:
  virtual ~DockyardProxy() {}

  // Initialize the DockyardProxy.
  virtual DockyardProxyStatus Init() = 0;

  // Send inspection data to the Dockyard.
  virtual DockyardProxyStatus SendInspectJson(const std::string& stream_name,
                                              const std::string& json) = 0;

  // Send a single sample to the Dockyard.
  virtual DockyardProxyStatus SendSample(const std::string& stream_name,
                                         uint64_t value) = 0;

  // Send a list of samples with the same timestamp to the Dockyard.
  virtual DockyardProxyStatus SendSampleList(const SampleList list) = 0;

  virtual DockyardProxyStatus SendStringSampleList(
      const StringSampleList list) = 0;
};

}  // namespace harvester

#endif  // GARNET_BIN_SYSTEM_MONITOR_HARVESTER_DOCKYARD_PROXY_H_
