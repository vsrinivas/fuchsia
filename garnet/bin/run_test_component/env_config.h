// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_RUN_TEST_COMPONENT_ENV_CONFIG_H_
#define GARNET_BIN_RUN_TEST_COMPONENT_ENV_CONFIG_H_

#include <string>
#include <unordered_map>
#include <vector>

#include "lib/json/json_parser.h"
#include "rapidjson/document.h"

namespace run {

enum EnvironmentType { SYS };

class EnvironmentConfig {
 public:
  bool ParseFromFile(const std::string& file_path);

  bool HasError() const { return json_parser_.HasError(); }
  std::string error_str() const { return json_parser_.error_str(); }

  const std::unordered_map<std::string, EnvironmentType>& url_map() const { return url_map_; }

 private:
  void CreateMap(const std::string& environment_name, EnvironmentType env_type,
                 const rapidjson::Document& document);

  json::JSONParser json_parser_;
  std::unordered_map<std::string, EnvironmentType> url_map_;
};

}  // namespace run

#endif  // GARNET_BIN_RUN_TEST_COMPONENT_ENV_CONFIG_H_
