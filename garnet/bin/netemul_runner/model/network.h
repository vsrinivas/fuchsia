// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_NETEMUL_RUNNER_MODEL_NETWORK_H_
#define GARNET_BIN_NETEMUL_RUNNER_MODEL_NETWORK_H_

#include <lib/json/json_parser.h>
#include <src/lib/fxl/macros.h>
#include "endpoint.h"

namespace netemul {
namespace config {

class Network {
 public:
  Network() = default;
  Network(Network&& other) = default;

  bool ParseFromJSON(const rapidjson::Value& value,
                     json::JSONParser* json_parser);

  const std::string& name() const;
  const std::vector<Endpoint>& endpoints() const;

 private:
  std::string name_;
  std::vector<Endpoint> endpoints_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Network);
};

}  // namespace config
}  // namespace netemul
#endif  // GARNET_BIN_NETEMUL_RUNNER_MODEL_NETWORK_H_
