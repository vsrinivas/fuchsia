// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_SYSTEM_MONITOR_HARVESTER_SAMPLE_BUNDLE_H_
#define GARNET_BIN_SYSTEM_MONITOR_HARVESTER_SAMPLE_BUNDLE_H_

#include <string>

#include "dockyard_proxy.h"

namespace harvester {

// A bundle (group) of samples that will all receive the same timestamp. Avoid
// keeping samples in a bundle for very long before calling Upload() since all
// samples in the bundle will be timestamped when Upload() is called.
class SampleBundle final {
 public:
  SampleBundle() = default;

  // After gathering the data, upload it to |dockyard|.
  void Upload(DockyardProxy* dockyard_proxy);

  // Add a value to the sample |int_sample_list_|.
  void AddIntSample(const std::string& dockyard_path,
                    dockyard::SampleValue value) {
    int_sample_list_.emplace_back(dockyard_path, value);
  }

  // Helper to add a value to the sample |int_sample_list_|.
  void AddIntSample(const std::string& type, uint64_t id,
                    const std::string& path, dockyard::SampleValue value) {
    std::ostringstream label;
    label << type << ":" << id << ":" << path;
    int_sample_list_.emplace_back(label.str(), value);
  }

  // Helper to add a value to the sample |string_sample_list_|.
  void AddStringSample(const std::string& type, uint64_t id,
                       const std::string& path, const std::string& value) {
    std::ostringstream label;
    label << type << ":" << id << ":" << path;
    string_sample_list_.emplace_back(label.str(), value);
  }

 private:
  SampleList int_sample_list_;
  StringSampleList string_sample_list_;
};

}  // namespace harvester

#endif  // GARNET_BIN_SYSTEM_MONITOR_HARVESTER_SAMPLE_BUNDLE_H_
