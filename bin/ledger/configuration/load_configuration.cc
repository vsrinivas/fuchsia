// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/configuration/load_configuration.h"

#include "apps/ledger/src/configuration/configuration_encoder.h"
#include "lib/ftl/files/directory.h"
#include "lib/ftl/files/file.h"

namespace configuration {

namespace {

bool CheckIfCompatible(const Configuration& last_config,
                       const Configuration& next_config) {
  if (!last_config.sync_params.firebase_id.empty() ||
      !last_config.sync_params.firebase_prefix.empty()) {
    if (last_config.sync_params != next_config.sync_params) {
      FTL_LOG(ERROR) << "A previous run of Ledger used a different Cloud Sync "
                     << "destination.";
      FTL_LOG(ERROR) << "Cloud Sync doesn't support migrations. If you want "
                     << "to use a different Cloud Sync destination, consider "
                     << "clearing Ledger data first: `rm -r /data/ledger`.";
      return false;
    }
  }
  return true;
}

}  // namespace

bool LoadConfiguration(Configuration* result) {
  const std::string config_file = kDefaultConfigurationFile.ToString();

  // Get the current configuration.
  Configuration config;
  if (files::IsFile(config_file)) {
    if (!ConfigurationEncoder::Decode(config_file, &config)) {
      FTL_LOG(ERROR) << "The configuration file is present at: "
                     << kDefaultConfigurationFile << " but can't be read.";
      return false;
    }

    FTL_LOG(INFO) << "Read the configuration file at "
                  << kDefaultConfigurationFile;
  } else {
    FTL_LOG(WARNING)
        << "No configuration file for Ledger. Using default configuration "
        << "without sync.";
  }

  // Get the configuration used for the previous run.
  const std::string last_config_file = kLastConfigurationFile.ToString();
  if (files::IsFile(last_config_file)) {
    Configuration last_config;
    if (!ConfigurationEncoder::Decode(last_config_file, &last_config)) {
      FTL_LOG(ERROR) << "Last run configuration file is present at: "
                     << kLastConfigurationFile << " but can't be read.";
      return false;
    }

    if (!CheckIfCompatible(last_config, config)) {
      return false;
    }
  }

  *result = config;
  return true;
}

bool SaveAsLastConfiguration(const Configuration& config) {
  return ConfigurationEncoder::Write(kLastConfigurationFile.ToString(), config);
}

}  // namespace configuration
