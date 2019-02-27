// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "config.h"

namespace netemul {
namespace config {

static const char* kNetworks = "networks";
static const char* kEnvironment = "environment";
static const char* kDefaultUrl = "default_url";
static const char* kDisabled = "disabled";
static const char* kTimeout = "timeout";

const char Config::Facet[] = "fuchsia.netemul";

bool Config::ParseFromJSON(const rapidjson::Value& value,
                           json::JSONParser* json_parser) {
  // null value keeps config as it is
  if (value.IsNull()) {
    return true;
  }

  if (!value.IsObject()) {
    json_parser->ReportError("fuchsia.netemul object must be an Object");
    return false;
  }

  auto nets_value = value.FindMember(kNetworks);
  if (nets_value != value.MemberEnd()) {
    if (!nets_value->value.IsArray()) {
      json_parser->ReportError("\"networks\" property must be an Array");
      return false;
    }
    const auto& nets = nets_value->value.GetArray();
    for (auto n = nets.Begin(); n != nets.End(); n++) {
      auto& net = networks_.emplace_back();
      if (!net.ParseFromJSON(*n, json_parser)) {
        return false;
      }
    }
  }

  auto env_value = value.FindMember(kEnvironment);
  if (env_value == value.MemberEnd()) {
    // parse from empty object if not present
    if (!environment_.ParseFromJSON(rapidjson::Value(rapidjson::kObjectType),
                                    json_parser)) {
      return false;
    }
  } else {
    if (!environment_.ParseFromJSON(env_value->value, json_parser)) {
      return false;
    }
  }

  auto default_url = value.FindMember(kDefaultUrl);
  if (default_url != value.MemberEnd()) {
    if (!default_url->value.IsString()) {
      json_parser->ReportError("\"default_url\" must be a String");
      return false;
    }
    default_url_ = default_url->value.GetString();
  }

  auto disabled = value.FindMember(kDisabled);
  if (disabled != value.MemberEnd()) {
    if (!disabled->value.IsBool()) {
      json_parser->ReportError("\"disabled\" must be a Boolean value");
      return false;
    }
    disabled_ = disabled->value.GetBool();
  } else {
    disabled_ = false;
  }

  auto timeout = value.FindMember(kTimeout);
  if (timeout != value.MemberEnd()) {
    if (!timeout->value.IsUint64() || timeout->value.GetUint64() <= 0) {
      json_parser->ReportError(
          "\"timeout\" must be a positive integer Number value");
      return false;
    }
    timeout_ = zx::sec(timeout->value.GetUint64());
  } else {
    timeout_ = zx::duration::infinite();
  }

  return true;
}

const std::vector<Network>& Config::networks() const { return networks_; }

const Environment& Config::environment() const { return environment_; }

const std::string& Config::default_url() const { return default_url_; }

bool Config::disabled() const { return disabled_; }

zx::duration Config::timeout() const { return timeout_; }

}  // namespace config
}  // namespace netemul