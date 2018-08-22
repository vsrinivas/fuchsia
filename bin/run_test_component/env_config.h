// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_RUN_TEST_COMPONENT_ENV_CONFIG_H_
#define GARNET_BIN_RUN_TEST_COMPONENT_ENV_CONFIG_H_

#include <string>
#include <unordered_map>
#include <vector>

#include "third_party/rapidjson/rapidjson/document.h"

namespace run {

enum EnvironmentType { ROOT, SYS };

class EnvironmentConfig {
 public:
  ~EnvironmentConfig();

  static EnvironmentConfig CreateFromFile(const std::string& file_path);

  bool has_error() const { return has_error_; }
  const std::vector<std::string>& errors() const { return errors_; }
  const std::unordered_map<std::string, EnvironmentType>& url_map() const {
    return url_map_;
  }

 private:
  EnvironmentConfig();
  void CreateMap(const std::string& environment_name, EnvironmentType env_type,
                 const rapidjson::Document& document);

  bool has_error_;
  std::vector<std::string> errors_;
  std::unordered_map<std::string, EnvironmentType> url_map_;
};

}  // namespace run

#endif  // GARNET_BIN_RUN_TEST_COMPONENT_ENV_CONFIG_H_
