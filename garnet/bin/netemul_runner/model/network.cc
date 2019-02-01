// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "network.h"

namespace netemul {
namespace config {

static const char* kName = "name";
static const char* kEndpoints = "endpoints";

bool Network::ParseFromJSON(const rapidjson::Value& value,
                            json::JSONParser* json_parser) {
  if (!value.IsObject()) {
    json_parser->ReportError("network entry must be an object");
    return false;
  }

  auto name = value.FindMember(kName);
  if (name == value.MemberEnd()) {
    json_parser->ReportError("network must have name property set");
    return false;
  } else if ((!name->value.IsString()) || name->value.GetStringLength() == 0) {
    json_parser->ReportError("network name must be a non-empty string");
    return false;
  } else {
    name_ = name->value.GetString();
  }

  auto endpoints = value.FindMember(kEndpoints);
  if (endpoints == value.MemberEnd()) {
    endpoints_.clear();
  } else if (!endpoints->value.IsArray()) {
    json_parser->ReportError("network endpoints must be an array");
    return false;
  } else {
    auto eps = endpoints->value.GetArray();
    for (auto e = eps.Begin(); e != eps.End(); e++) {
      auto& ne = endpoints_.emplace_back();
      if (!ne.ParseFromJSON(*e, json_parser)) {
        return false;
      }
    }
  }

  return true;
}

const std::string& Network::name() const { return name_; }

const std::vector<Endpoint>& Network::endpoints() const { return endpoints_; }

}  // namespace config
}  // namespace netemul