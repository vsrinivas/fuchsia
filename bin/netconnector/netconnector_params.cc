// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/netconnector/src/netconnector_params.h"

#include <rapidjson/document.h>

#include "application/services/application_launcher.fidl.h"
#include "apps/netconnector/src/ip_address.h"
#include "lib/ftl/files/file.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/strings/split_string.h"

namespace netconnector {
namespace {

constexpr char kConfigServices[] = "services";
constexpr char kConfigDevices[] = "devices";
constexpr char kDefaultConfigFileName[] =
    "/system/data/netconnector/netconnector.config";
}  // namespace

NetConnectorParams::NetConnectorParams(const ftl::CommandLine& command_line) {
  is_valid_ = false;

  listen_ = command_line.HasOption("listen");
  show_devices_ = command_line.HasOption("show-devices");
  mdns_verbose_ = command_line.HasOption("mdns-verbose");

  if (listen_ && show_devices_) {
    FTL_LOG(ERROR) << "--listen and --show-devices are mutually exclusive";
    Usage();
    return;
  }

  std::string config_file_name;
  if (!command_line.GetOptionValue("config", &config_file_name)) {
    config_file_name = kDefaultConfigFileName;
  }

  if (config_file_name.empty()) {
    Usage();
    return;
  }

  if (listen_ && !ReadConfigFrom(config_file_name)) {
    FTL_LOG(ERROR) << "Failed to parse config file " << config_file_name;
    return;
  }

  is_valid_ = true;
}

void NetConnectorParams::Usage() {
  FTL_LOG(INFO) << "netconnector usage:";
  FTL_LOG(INFO) << "    @boot netconnector [ options ]";
  FTL_LOG(INFO) << "options:";
  FTL_LOG(INFO)
      << "    --config=<file>                  read config file (default "
      << kDefaultConfigFileName << ")";
  FTL_LOG(INFO) << "    --show-devices                   show known devices";
  FTL_LOG(INFO) << "    --mdns-verbose                   log mDNS traffic";
  FTL_LOG(INFO) << "    --listen                         run as listener";
}

void NetConnectorParams::RegisterService(
    const std::string& name,
    app::ApplicationLaunchInfoPtr launch_info) {
  auto result =
      launch_infos_by_service_name_.emplace(name, std::move(launch_info));

  if (!result.second) {
    FTL_DCHECK(result.first != launch_infos_by_service_name_.end());
    result.first->second = std::move(launch_info);
  }
}

void NetConnectorParams::RegisterDevice(const std::string& name,
                                        const IpAddress& address) {
  auto result = device_addresses_by_name_.emplace(name, address);

  if (!result.second) {
    FTL_DCHECK(result.first != device_addresses_by_name_.end());
    result.first->second = address;
  }
}

void NetConnectorParams::UnregisterDevice(const std::string& name) {
  device_addresses_by_name_.erase(name);
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

  auto iter = document.FindMember(kConfigServices);
  if (iter != document.MemberEnd()) {
    const auto& value = iter->value;
    if (!value.IsObject()) {
      return false;
    }

    for (const auto& pair : value.GetObject()) {
      if (!pair.name.IsString()) {
        return false;
      }

      auto launch_info = app::ApplicationLaunchInfo::New();
      if (pair.value.IsString()) {
        launch_info->url = pair.value.GetString();
      } else if (pair.value.IsArray()) {
        const auto& array = pair.value.GetArray();

        if (array.Empty() || !array[0].IsString()) {
          return false;
        }

        launch_info->url = array[0].GetString();
        for (size_t i = 1; i < array.Size(); ++i) {
          if (!array[i].IsString()) {
            return false;
          }

          launch_info->arguments.push_back(array[i].GetString());
        }
      } else {
        return false;
      }

      RegisterService(pair.name.GetString(), std::move(launch_info));
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

      IpAddress address = IpAddress::FromString(pair.value.GetString());
      if (!address.is_valid()) {
        FTL_LOG(ERROR) << "Config file contains invalid IP address "
                       << pair.value.GetString();
        return false;
      }

      RegisterDevice(pair.name.GetString(), address);
    }
  }

  return true;
}

}  // namespace netconnector
