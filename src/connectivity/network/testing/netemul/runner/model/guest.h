// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_TESTING_NETEMUL_RUNNER_MODEL_GUEST_H_
#define SRC_CONNECTIVITY_NETWORK_TESTING_NETEMUL_RUNNER_MODEL_GUEST_H_

#include <map>
#include <string>
#include <vector>

#include "src/lib/fxl/macros.h"
#include "src/lib/json_parser/json_parser.h"

namespace netemul {
namespace config {

class Guest {
 public:
  Guest() = default;
  Guest(Guest&& other) = default;

  bool ParseFromJSON(const rapidjson::Value& value, json::JSONParser* parser);

  const std::string& guest_image_url() const;
  const std::string& guest_label() const;
  const std::vector<std::string>& networks() const;
  const std::map<std::string, std::string>& files() const;
  const std::map<std::string, std::string>& macs() const;

 private:
  std::string guest_image_url_;
  std::string guest_label_;
  std::vector<std::string> networks_;
  std::map<std::string, std::string> files_;
  std::map<std::string, std::string> macs_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Guest);
};

}  // namespace config
}  // namespace netemul

#endif  // SRC_CONNECTIVITY_NETWORK_TESTING_NETEMUL_RUNNER_MODEL_GUEST_H_
