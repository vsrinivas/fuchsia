// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_COBALT_BIN_APP_CONFIGURATION_DATA_H_
#define SRC_COBALT_BIN_APP_CONFIGURATION_DATA_H_

#include <iostream>
#include <string>

#include "src/lib/json_parser/json_parser.h"
#include "third_party/cobalt/src/lib/statusor/statusor.h"
#include "third_party/cobalt/src/public/cobalt_service_interface.h"
#include "third_party/cobalt/src/registry/metric_definition.pb.h"
#include "third_party/cobalt/src/system_data/configuration_data.h"

namespace cobalt {

// A class for handling parsing and reading values from config.json
class JSONHelper {
 public:
  JSONHelper(const std::string& path);

  lib::statusor::StatusOr<std::string> GetString(const std::string& key) const;
  lib::statusor::StatusOr<bool> GetBool(const std::string& key) const;

 private:
  util::Status EnsureKey(const std::string& key) const;

  json::JSONParser json_parser_;
  rapidjson::Document config_file_contents_;
};

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

  cobalt::CobaltServiceInterface::DataCollectionPolicy GetDataCollectionPolicy() const;

  bool GetWatchForUserConsent() const;

  // Returns the cobalt API key. If it cannot be found, return the default API key.
  std::string GetApiKey() const;

 private:
  static const char kDefaultConfigDir[];
  static const char kDefaultEnvironmentDir[];
  config::Environment backend_environment_;
  config::ConfigurationData backend_configuration_;
  std::string api_key_;

  JSONHelper json_helper_;
  cobalt::ReleaseStage release_stage_;
  cobalt::CobaltServiceInterface::DataCollectionPolicy data_collection_policy_;
  bool watch_for_user_consent_;
};

}  // namespace cobalt

#endif  // SRC_COBALT_BIN_APP_CONFIGURATION_DATA_H_
