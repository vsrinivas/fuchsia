// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_NETEMUL_RUNNER_MODEL_CONFIG_H_
#define GARNET_BIN_NETEMUL_RUNNER_MODEL_CONFIG_H_

#include <lib/zx/time.h>
#include <vector>
#include "environment.h"
#include "src/lib/fxl/macros.h"
#include "lib/json/json_parser.h"
#include "network.h"

namespace netemul {
namespace config {

enum CaptureMode { NONE, ON_ERROR, ALWAYS };

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
  bool disabled() const;
  zx::duration timeout() const;
  CaptureMode capture() const;

 private:
  std::vector<Network> networks_;
  Environment environment_;
  std::string default_url_;
  bool disabled_;
  zx::duration timeout_;
  CaptureMode capture_mode_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Config);
};

}  // namespace config
}  // namespace netemul
#endif  // GARNET_BIN_NETEMUL_RUNNER_MODEL_CONFIG_H_
