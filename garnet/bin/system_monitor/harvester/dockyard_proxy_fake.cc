// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dockyard_proxy_fake.h"

#include "gtest/gtest.h"

namespace harvester {

DockyardProxyStatus DockyardProxyFake::Init() {
  return DockyardProxyStatus::OK;
}

DockyardProxyStatus DockyardProxyFake::SendInspectJson(
    const std::string& stream_name, const std::string& json) {
  return DockyardProxyStatus::OK;
}

DockyardProxyStatus DockyardProxyFake::SendSample(
    const std::string& stream_name, uint64_t value) {
  return DockyardProxyStatus::OK;
}

DockyardProxyStatus DockyardProxyFake::SendSampleList(const SampleList list) {
  EXPECT_TRUE(list.size() > 0);
  bool found_sample_label = false;
  for (const auto& sample : list) {
    // Current sample lists will be for cpu or memory. Check that one of these
    // is in the list.
    if (sample.first.compare("cpu:0:busy_time") == 0 ||
        sample.first.compare("memory:device_free_bytes") == 0) {
      found_sample_label = true;
      break;
    }
  }
  EXPECT_TRUE(found_sample_label);
  return DockyardProxyStatus::OK;
}

DockyardProxyStatus DockyardProxyFake::SendStringSampleList(
    const StringSampleList list) {
  return DockyardProxyStatus::OK;
}

}  // namespace harvester
