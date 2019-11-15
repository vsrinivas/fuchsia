// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_COBALT_BIN_APP_CONFIGURATION_DATA_H_
#define SRC_COBALT_BIN_APP_CONFIGURATION_DATA_H_

#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "third_party/cobalt/src/registry/metric_definition.pb.h"
#include "third_party/cobalt/src/system_data/configuration_data.h"

namespace cobalt {

// Encapsulation of the configuration data used by Cobalt in Fuchsia.
class FuchsiaConfigurationData {
 public:
  explicit FuchsiaConfigurationData(const std::string& config_dir = kDefaultConfigDir);

  // Get the (possibly multiple) backend environments to write to.
  std::vector<config::Environment> GetBackendEnvironments() const;

  // Get the path to the public key file to use for encrypting Observations.
  const char* AnalyzerPublicKeyPath() const;

  // Get the path to the public key file to use for encrypting Envelopes.
  const char* ShufflerPublicKeyPath(const config::Environment& backend_environment) const;

  // Get the Clearcut Log Source ID that Cobalt should write its logs to.
  int32_t GetLogSourceId(const config::Environment& backend_environment) const;

  cobalt::ReleaseStage GetReleaseStage() const;

 private:
  static const char kDefaultConfigDir[];
  std::map<config::Environment, const config::ConfigurationData> backend_configurations_;
  cobalt::ReleaseStage release_stage_;
};

}  // namespace cobalt

#endif  // SRC_COBALT_BIN_APP_CONFIGURATION_DATA_H_
