// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dockyard_proxy_fake.h"

#include "gtest/gtest.h"

namespace harvester {

DockyardProxyStatus DockyardProxyFake::Init() {
  sent_values_.clear();
  sent_strings_.clear();
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
  for (const auto& sample : list) {
#ifdef VERBOSE_OUTPUT
    std::cout << "Sending " << sample.first << " " << sample.second
              << std::endl;
#endif  // VERBOSE_OUTPUT
    sent_values_.emplace(sample.first, sample.second);
  }
  return DockyardProxyStatus::OK;
}

DockyardProxyStatus DockyardProxyFake::SendStringSampleList(
    const StringSampleList list) {
  EXPECT_TRUE(list.size() > 0);
  for (const auto& sample : list) {
#ifdef VERBOSE_OUTPUT
    std::cout << "Sending " << sample.first << " " << sample.second
              << std::endl;
#endif  // VERBOSE_OUTPUT
    sent_strings_.emplace(sample.first, sample.second);
  }
  return DockyardProxyStatus::OK;
}

bool DockyardProxyFake::CheckValueSent(const std::string& dockyard_path,
                                       dockyard::SampleValue* value) const {
  const auto& iter = sent_values_.find(dockyard_path);
  if (iter == sent_values_.end()) {
    return false;
  }
  *value = iter->second;
  return true;
}

bool DockyardProxyFake::CheckStringSent(const std::string& dockyard_path,
                                        std::string* string) const {
  const auto& iter = sent_strings_.find(dockyard_path);
  if (iter == sent_strings_.end()) {
    return false;
  }
  *string = iter->second;
  return true;
}

}  // namespace harvester
