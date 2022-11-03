// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_CONFIG_H_
#define SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_CONFIG_H_

#include <optional>

#include "rapidjson/document.h"
#include "src/connectivity/network/mdns/service/common/mdns_addresses.h"
#include "src/connectivity/network/mdns/service/mdns.h"
#include "src/lib/json_parser/json_parser.h"

namespace mdns {

class Config {
 public:
  // Describes a publication from config files.
  struct Publication {
    std::string service_;
    std::string instance_;
    std::unique_ptr<Mdns::Publication> publication_;
    bool perform_probe_;
    Media media_;
  };

  Config() = default;
  ~Config() = default;

  // Reads the config files from |config_dir|. |local_host_name| is the local host name as
  // defined by the operating system (e.g. the result of posix's |gethostname|).
  // The default value for |config_dir| is "/config/data".
  void ReadConfigFiles(const std::string& local_host_name,
                       const std::string& config_dir = kConfigDir);

  // Indicates whether the configuration is valid.
  bool valid() const { return !parser_.HasError(); }

  // Returns a string describing the error if |valid()| returns true, otherwise
  // an empty string.
  std::string error() const { return parser_.error_str(); }

  // Indicates whether a probe should be performed for the hostname.
  bool perform_host_name_probe() const {
    return perform_host_name_probe_.has_value() ? perform_host_name_probe_.value() : true;
  }

  // Gets the publications.
  const std::vector<Publication>& publications() const { return publications_; }

  // Gets the alternate services.
  const std::vector<std::string>& alt_services() const { return alt_services_; }

 private:
  static const char kConfigDir[];

  // Integrates the config file represented by |document| into this
  // configuration.
  void IntegrateDocument(const rapidjson::Document& document, const std::string& local_host_name);

  // Integrates the publication represented by |value| into this configuration.
  // |value| must be a JSON object.
  void IntegratePublication(const rapidjson::Value& value, const std::string& local_host_name);

  // Sets the value indicating whether a host name probe is required.
  void SetPerformHostNameProbe(bool perform_host_name_probe);

  json::JSONParser parser_;
  std::optional<bool> perform_host_name_probe_;
  std::vector<Publication> publications_;
  std::vector<std::string> alt_services_;

 public:
  // Disallow copy, assign and move.
  Config(const Config&) = delete;
  Config(Config&&) = delete;
  Config& operator=(const Config&) = delete;
  Config& operator=(Config&&) = delete;
};

}  // namespace mdns

#endif  // SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_CONFIG_H_
