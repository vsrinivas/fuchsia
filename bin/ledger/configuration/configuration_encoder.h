// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_CONFIGURATION_CONFIGURATION_ENCODER_H_
#define APPS_LEDGER_SRC_CONFIGURATION_CONFIGURATION_ENCODER_H_

#include <string>

#include "apps/ledger/src/configuration/configuration.h"

namespace configuration {

class ConfigurationEncoder {
 public:
  // Decodes a configuration from a file.
  static bool Decode(const std::string& configuration_path,
                     Configuration* configuration);

  // Writes a configuration to a file.
  static bool Write(const std::string& configuration_path,
                    const Configuration& configuration);

 private:
  static bool DecodeFromString(const std::string& json,
                               Configuration* configuration);

  static std::string EncodeToString(const Configuration& configuration);
};
}  // namespace configuration

#endif  // APPS_LEDGER_SRC_CONFIGURATION_CONFIGURATION_ENCODER_H_
