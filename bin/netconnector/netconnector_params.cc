// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/netconnector/src/netconnector_params.h"

#include <rapidjson/document.h>

#include "lib/ftl/files/file.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/strings/split_string.h"

namespace netconnector {
namespace {

constexpr const char kConfigHost[] = "host";
constexpr const char kConfigResponders[] = "responders";
constexpr const char kConfigDevices[] = "devices";
constexpr const char kDefaultConfigFileName[] =
    "/system/data/netconnector/netconnector.config";
}  // namespace

NetConnectorParams::NetConnectorParams(const ftl::CommandLine& command_line) {
  is_valid_ = false;

  listen_ = command_line.HasOption("listen");

  if (!command_line.GetOptionValue("name", &host_name_)) {
    host_name_ = std::string();
  }

  if (!command_line.HasOption("no-config")) {
    std::string config_file_name;
    if (!command_line.GetOptionValue("config", &config_file_name)) {
      config_file_name = kDefaultConfigFileName;
    }

    if (config_file_name.empty()) {
      Usage();
      return;
    }

    if (!ReadConfigFrom(config_file_name)) {
      FTL_LOG(ERROR) << "Failed to parse config file " << config_file_name;
      return;
    }
  }

  for (auto option : command_line.options()) {
    if (option.name == "responder") {
      auto split = ftl::SplitString(option.value, "@", ftl::kTrimWhitespace,
                                    ftl::kSplitWantNonEmpty);
      if (split.size() != 2) {
        Usage();
        return;
      }

      RegisterResponder(split[0].ToString(), split[1].ToString());
    }

    if (option.name == "device") {
      auto split = ftl::SplitString(option.value, "@", ftl::kTrimWhitespace,
                                    ftl::kSplitWantNonEmpty);
      if (split.size() != 2) {
        Usage();
        return;
      }

      RegisterDevice(split[0].ToString(), split[1].ToString());
    }
  }

  is_valid_ = true;
}

void NetConnectorParams::Usage() {
  FTL_LOG(INFO) << "netconnector usage:";
  FTL_LOG(INFO) << "    @boot netconnector [ options ]";
  FTL_LOG(INFO) << "options:";
  FTL_LOG(INFO)
      << "    --no-config                        don't read a config file";
  FTL_LOG(INFO)
      << "    --config=<file>                    read config file (default "
      << kDefaultConfigFileName << ")";
  FTL_LOG(INFO) << "    --host=<name>                      set the host name";
  FTL_LOG(INFO) << "    --responder=<name>@<servcie_name>  register responder";
  FTL_LOG(INFO) << "    --device=<name>@<ip>               register device";
  FTL_LOG(INFO) << "    --listen                           run as listener";
  FTL_LOG(INFO) << "Multiple responders and devices can be registered.";
}

void NetConnectorParams::RegisterResponder(const std::string& name,
                                           const std::string& service_name) {
  auto result = service_names_by_responder_name_.emplace(name, service_name);
  if (!result.second) {
    FTL_DCHECK(result.first != service_names_by_responder_name_.end());
    result.first->second = service_name;
  }
}

void NetConnectorParams::RegisterDevice(const std::string& name,
                                        const std::string& address) {
  auto result = device_addresses_by_name_.emplace(name, address);
  if (!result.second) {
    FTL_DCHECK(result.first != device_addresses_by_name_.end());
    result.first->second = address;
  }
}

bool NetConnectorParams::ReadConfigFrom(const std::string& config_file_name) {
  std::string config_file_contents;
  return files::ReadFileToString(config_file_name, &config_file_contents) &&
         ParseConfig(config_file_contents);
}

bool NetConnectorParams::ParseConfig(const std::string& string) {
  rapidjson::Document document;
  document.Parse(string.data(), string.size());
  if (!document.IsObject())
    return false;

  auto iter = document.FindMember(kConfigHost);
  if (iter != document.MemberEnd()) {
    const auto& value = iter->value;
    if (!value.IsString()) {
      return false;
    }

    host_name_ = value.GetString();
  }

  iter = document.FindMember(kConfigResponders);
  if (iter != document.MemberEnd()) {
    const auto& value = iter->value;
    if (!value.IsObject()) {
      return false;
    }

    for (const auto& pair : value.GetObject()) {
      if (!pair.name.IsString() || !pair.value.IsString()) {
        return false;
      }

      RegisterResponder(pair.name.GetString(), pair.value.GetString());
    }
  }

  iter = document.FindMember(kConfigDevices);
  if (iter != document.MemberEnd()) {
    const auto& value = iter->value;
    if (!value.IsObject()) {
      return false;
    }

    for (const auto& pair : value.GetObject()) {
      if (!pair.name.IsString() || !pair.value.IsString()) {
        return false;
      }

      RegisterDevice(pair.name.GetString(), pair.value.GetString());
    }
  }

  return true;
}

}  // namespace netconnector
