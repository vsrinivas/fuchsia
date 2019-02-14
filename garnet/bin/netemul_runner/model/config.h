// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_NETEMUL_RUNNER_MODEL_CONFIG_H_
#define GARNET_BIN_NETEMUL_RUNNER_MODEL_CONFIG_H_

#include <vector>
#include "environment.h"
#include "lib/fxl/macros.h"
#include "lib/json/json_parser.h"
#include "network.h"

namespace netemul {
namespace config {

class Config {
 public:
  Config() = default;
  Config(Config&& other) = default;

  static const char Facet[];
  bool ParseFromJSON(const rapidjson::Value& value,
                     json::JSONParser* json_parser);

  const std::vector<Network>& networks() const;
  const Environment& environment() const;
  const std::string& default_url() const;

 private:
  std::vector<Network> networks_;
  Environment environment_;
  std::string default_url_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Config);
};

}  // namespace config
}  // namespace netemul
#endif  // GARNET_BIN_NETEMUL_RUNNER_MODEL_CONFIG_H_
