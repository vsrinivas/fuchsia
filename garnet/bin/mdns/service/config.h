// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MDNS_SERVICE_CONFIG_H_
#define GARNET_BIN_MDNS_SERVICE_CONFIG_H_

#include <optional>

#include "garnet/bin/mdns/service/mdns.h"
#include "lib/json/json_parser.h"
#include "rapidjson/document.h"

namespace mdns {

class Config {
 public:
  // Describes a publication from config files.
  struct Publication {
    std::string service_;
    std::string instance_;
    std::unique_ptr<Mdns::Publication> publication_;
    bool perform_probe_;
  };

  Config() = default;
  ~Config() = default;

  // Reads the config files from |config_dir|. |host_name| is the host name as
  // defined by the operating system (e.g. the result of posix's |gethostname|).
  // The default value for |config_dir| is "/config/data".
  void ReadConfigFiles(const std::string& host_name,
                       const std::string& config_dir = kConfigDir);

  // Indicates whether the configuration is valid.
  bool valid() { return !parser_.HasError(); }

  // Returns a string describing the error if |valid()| returns true, otherwise
  // an empty string.
  std::string error() { return parser_.error_str(); }

  // Indicates whether a probe should be performed for the hostname.
  bool perform_host_name_probe() {
    return perform_host_name_probe_.has_value()
               ? perform_host_name_probe_.value()
               : true;
  }

  // Gets the publications.
  const std::vector<Publication>& publications() { return publications_; }

 private:
  static const char kConfigDir[];

  // Integrates the config file represented by |document| into this
  // configuration.
  void IntegrateDocument(const rapidjson::Document& document,
                         const std::string& host_name);

  // Integrates the publication represented by |value| into this configuration.
  // |value| must be a JSON object.
  void IntegratePublication(const rapidjson::Value& value,
                            const std::string& host_name);

  // Sets the value indicating whether a host name probe is required.
  void SetPerformHostNameProbe(bool perform_host_name_probe);

  json::JSONParser parser_;
  std::optional<bool> perform_host_name_probe_;
  std::vector<Publication> publications_;
};

}  // namespace mdns

#endif  // GARNET_BIN_MDNS_SERVICE_CONFIG_H_
