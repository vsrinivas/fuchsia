// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_NETEMUL_RUNNER_MODEL_LOGGER_FILTER_OPTIONS_H_
#define GARNET_BIN_NETEMUL_RUNNER_MODEL_LOGGER_FILTER_OPTIONS_H_

#include <lib/json/json_parser.h>
#include <src/lib/fxl/macros.h>

namespace netemul {
namespace config {

class LoggerFilterOptions {
 public:
  LoggerFilterOptions();
  LoggerFilterOptions(LoggerFilterOptions&& other) = default;

  bool ParseFromJSON(const rapidjson::Value& value, json::JSONParser* parser);
  void SetDefaults();

  uint8_t verbosity() const;
  const std::vector<std::string>& tags() const;

 private:
  uint8_t verbosity_;
  std::vector<std::string> tags_;

  FXL_DISALLOW_COPY_AND_ASSIGN(LoggerFilterOptions);
};

}  // namespace config
}  // namespace netemul

#endif  // GARNET_BIN_NETEMUL_RUNNER_MODEL_LOGGER_FILTER_OPTIONS_H_
