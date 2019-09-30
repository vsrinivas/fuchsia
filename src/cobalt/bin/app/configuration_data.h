// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_COBALT_BIN_APP_CONFIGURATION_DATA_H_
#define SRC_COBALT_BIN_APP_CONFIGURATION_DATA_H_

#include <iostream>
#include <string>

#include "third_party/cobalt/src/system_data/configuration_data.h"

namespace cobalt {

// Encapsulation of the configuration data used by Cobalt in Fuchsia.
class FuchsiaConfigurationData : public config::ConfigurationData {
 public:
  explicit FuchsiaConfigurationData(const std::string& config_dir = kDefaultConfigDir);

  // Get the path to the public key file to use for encrypting Observations.
  const char* AnalyzerPublicKeyPath();

  // Get the path to the public key file to use for encrypting Envelopes.
  const char* ShufflerPublicKeyPath();

 private:
  static const char kDefaultConfigDir[];
};

}  // namespace cobalt

#endif  // SRC_COBALT_BIN_APP_CONFIGURATION_DATA_H_
