// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_TESTING_NETEMUL_RUNNER_MODEL_ENVIRONMENT_H_
#define SRC_CONNECTIVITY_NETWORK_TESTING_NETEMUL_RUNNER_MODEL_ENVIRONMENT_H_

#include <lib/json/json_parser.h>
#include <src/lib/fxl/macros.h>

#include "launch_app.h"
#include "launch_service.h"
#include "logger_options.h"

namespace netemul {
namespace config {

class Environment {
 public:
  Environment() = default;
  Environment(Environment&& other) = default;

  void DisableLogging(bool recursive);

  bool ParseFromJSON(const rapidjson::Value& value, json::JSONParser* parser);

  const std::string& name() const;
  const std::vector<Environment>& children() const;
  const std::vector<std::string>& devices() const;
  const std::vector<LaunchService>& services() const;
  const std::vector<LaunchApp>& test() const;
  const std::vector<LaunchApp>& apps() const;
  const std::vector<LaunchApp>& setup() const;
  const LoggerOptions& logger_options() const;
  bool inherit_services() const;

 private:
  std::string name_;
  std::vector<Environment> children_;
  std::vector<std::string> devices_;
  std::vector<LaunchService> services_;
  std::vector<LaunchApp> test_;
  std::vector<LaunchApp> apps_;
  std::vector<LaunchApp> setup_;
  bool inherit_services_{};
  LoggerOptions logger_options_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Environment);
};

}  // namespace config
}  // namespace netemul
#endif  // SRC_CONNECTIVITY_NETWORK_TESTING_NETEMUL_RUNNER_MODEL_ENVIRONMENT_H_
