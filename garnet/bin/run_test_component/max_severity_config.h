// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_RUN_TEST_COMPONENT_MAX_SEVERITY_CONFIG_H_
#define GARNET_BIN_RUN_TEST_COMPONENT_MAX_SEVERITY_CONFIG_H_

#include <zircon/assert.h>

#include <map>

#include <src/lib/json_parser/json_parser.h>

namespace run {

// Parses config files to store "test url"-"max allowed severity" in logs mapping.
class MaxSeverityConfig {
 public:
  static MaxSeverityConfig ParseFromDirectory(const std::string& path);
  bool HasError() const { return json_parser_.HasError(); }
  std::string Error() const { return json_parser_.error_str(); }

  // Returns config. Crashes if there was error while parsing the config.
  const std::map<std::string, int32_t>& config() const {
    ZX_ASSERT_MSG(!HasError(), "Cannot call this function when there are errors.");
    return config_;
  }

 private:
  void ParseDirectory(const std::string& path);
  void ParseDocument(rapidjson::Document document);

  std::map<std::string, int32_t> config_;
  json::JSONParser json_parser_;
};

}  // namespace run

#endif  // GARNET_BIN_RUN_TEST_COMPONENT_MAX_SEVERITY_CONFIG_H_
