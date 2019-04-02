// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_NETEMUL_RUNNER_MODEL_ENDPOINT_H_
#define GARNET_BIN_NETEMUL_RUNNER_MODEL_ENDPOINT_H_

#include <lib/json/json_parser.h>
#include <src/lib/fxl/macros.h>

namespace netemul {
namespace config {

struct Mac {
  uint8_t d[6];
};

class Endpoint {
 public:
  Endpoint() = default;
  Endpoint(Endpoint&& other) = default;

  bool ParseFromJSON(const rapidjson::Value& value, json::JSONParser* parser);

  const std::string& name() const;
  const std::unique_ptr<Mac>& mac() const;
  uint16_t mtu() const;
  bool up() const;

 private:
  std::string name_;
  std::unique_ptr<Mac> mac_;
  uint16_t mtu_;
  bool up_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Endpoint);
};

}  // namespace config
}  // namespace netemul
#endif  // GARNET_BIN_NETEMUL_RUNNER_MODEL_ENDPOINT_H_
