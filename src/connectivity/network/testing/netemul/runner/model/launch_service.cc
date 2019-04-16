// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "launch_service.h"

#include "src/lib/pkg_url/fuchsia_pkg_url.h"

namespace netemul {
namespace config {

LaunchService::LaunchService(std::string name) : name_(std::move(name)) {}

bool LaunchService::ParseFromJSON(const rapidjson::Value& value,
                                  json::JSONParser* parser) {
  return launch_.ParseFromJSON(value, parser);
}

const std::string& LaunchService::name() const { return name_; }

const LaunchApp& LaunchService::launch() const { return launch_; }

}  // namespace config
}  // namespace netemul
