// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_COBALT_BIN_APP_CONFIGURATION_DATA_H_
#define SRC_COBALT_BIN_APP_CONFIGURATION_DATA_H_

#include <iostream>
#include <string>

#include "third_party/cobalt/src/registry/metric_definition.pb.h"
#include "third_party/cobalt/src/system_data/configuration_data.h"

namespace cobalt {

// Encapsulation of the configuration data used by Cobalt in Fuchsia.
class FuchsiaConfigurationData {
 public:
  explicit FuchsiaConfigurationData(const std::string& config_dir = kDefaultConfigDir,
                                    const std::string& environment_dir = kDefaultEnvironmentDir);

  // Get the backend environment to write to.
  config::Environment GetBackendEnvironment() const;

  // Get the path to the public key file to use for encrypting Observations.
  const char* AnalyzerPublicKeyPath() const;

  // Get the path to the public key file to use for encrypting Envelopes.
  const char* ShufflerPublicKeyPath() const;

  // Get the Clearcut Log Source ID that Cobalt should write its logs to.
  int32_t GetLogSourceId() const;

  cobalt::ReleaseStage GetReleaseStage() const;

  // Returns the cobalt API key. If it cannot be found, return the default API key.
  std::string GetApiKey() const;

 private:
  static const char kDefaultConfigDir[];
  static const char kDefaultEnvironmentDir[];
  config::Environment backend_environment_;
  config::ConfigurationData backend_configuration_;
  cobalt::ReleaseStage release_stage_;
  std::string api_key_;
};

}  // namespace cobalt

#endif  // SRC_COBALT_BIN_APP_CONFIGURATION_DATA_H_
