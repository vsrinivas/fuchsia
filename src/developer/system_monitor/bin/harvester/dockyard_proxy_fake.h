// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_SYSTEM_MONITOR_HARVESTER_DOCKYARD_PROXY_FAKE_H_
#define GARNET_BIN_SYSTEM_MONITOR_HARVESTER_DOCKYARD_PROXY_FAKE_H_

#include <map>
#include <string>

#include "dockyard_proxy.h"

namespace harvester {

class DockyardProxyFake : public DockyardProxy {
 public:
  DockyardProxyFake() = default;
  ~DockyardProxyFake() = default;

  // |DockyardProxy|.
  DockyardProxyStatus Init() override;

  // |DockyardProxy|.
  DockyardProxyStatus SendInspectJson(const std::string& stream_name,
                                      const std::string& json) override;

  // |DockyardProxy|.
  DockyardProxyStatus SendSample(const std::string& stream_name,
                                 uint64_t value) override;

  // |DockyardProxy|.
  DockyardProxyStatus SendSampleList(const SampleList& list) override;

  // |DockyardProxy|.
  DockyardProxyStatus SendStringSampleList(
      const StringSampleList& list) override;

  // |DockyardProxy|.
  DockyardProxyStatus SendSamples(
      const SampleList& int_samples,
      const StringSampleList& string_samples) override;

  // Get the value (or string) for a given dockyard path. Used for testing.
  // Returns true if the value was sent at all; false if it wasn't sent.
  bool CheckJsonSent(const std::string& dockyard_path, std::string* json) const;
  bool CheckValueSent(const std::string& dockyard_path,
                      dockyard::SampleValue* value) const;
  // Returns true if the substring appears in any value path.
  bool CheckValueSubstringSent(const std::string& dockyard_path_substring) const;
  bool CheckStringSent(const std::string& dockyard_path,
                       std::string* string) const;
  bool CheckStringPrefixSent(const std::string& dockyard_path_prefix,
                             std::string* string) const;

  size_t ValuesSentCount() { return sent_values_.size(); }
  size_t StringsSentCount() { return sent_strings_.size(); }
  size_t JsonSentCount() { return sent_json_.size(); }

 private:
  std::map<std::string, dockyard::SampleValue> sent_values_;
  std::map<std::string, std::string> sent_strings_;
  std::map<std::string, std::string> sent_json_;

  friend std::ostream& operator<<(std::ostream& out,
                                  const DockyardProxyFake& dockyard);
};

std::ostream& operator<<(std::ostream& out, const DockyardProxyFake& dockyard);

}  // namespace harvester

#endif  // GARNET_BIN_SYSTEM_MONITOR_HARVESTER_DOCKYARD_PROXY_FAKE_H_
