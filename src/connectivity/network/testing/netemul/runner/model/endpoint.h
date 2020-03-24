// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_TESTING_NETEMUL_RUNNER_MODEL_ENDPOINT_H_
#define SRC_CONNECTIVITY_NETWORK_TESTING_NETEMUL_RUNNER_MODEL_ENDPOINT_H_

#include <fuchsia/netemul/network/cpp/fidl.h>

#include "src/lib/fxl/macros.h"
#include "src/lib/json_parser/json_parser.h"

namespace netemul {
namespace config {

using Mac = fuchsia::net::MacAddress;

class Endpoint {
 public:
  Endpoint() = default;
  Endpoint(Endpoint&& other) = default;

  bool ParseFromJSON(const rapidjson::Value& value, json::JSONParser* parser);

  const std::string& name() const;
  const std::unique_ptr<Mac>& mac() const;
  uint16_t mtu() const;
  bool up() const;
  fuchsia::netemul::network::EndpointBacking backing() const;

 private:
  std::string name_;
  std::unique_ptr<Mac> mac_;
  uint16_t mtu_;
  bool up_;
  fuchsia::netemul::network::EndpointBacking backing_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Endpoint);
};

}  // namespace config
}  // namespace netemul
#endif  // SRC_CONNECTIVITY_NETWORK_TESTING_NETEMUL_RUNNER_MODEL_ENDPOINT_H_
