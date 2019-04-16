// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "network.h"

#include <src/lib/fxl/strings/string_printf.h>

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

  // set defaults:
  name_ = "";
  endpoints_.clear();

  // iterate over members:
  for (auto i = value.MemberBegin(); i != value.MemberEnd(); i++) {
    if (i->name == kName) {
      if ((!i->value.IsString()) || i->value.GetStringLength() == 0) {
        json_parser->ReportError("network name must be a non-empty string");
        return false;
      }
      name_ = i->value.GetString();
    } else if (i->name == kEndpoints) {
      if (!i->value.IsArray()) {
        json_parser->ReportError("network endpoints must be an array");
        return false;
      }
      auto eps = i->value.GetArray();
      for (auto e = eps.Begin(); e != eps.End(); e++) {
        auto& ne = endpoints_.emplace_back();
        if (!ne.ParseFromJSON(*e, json_parser)) {
          return false;
        }
      }
    } else {
      json_parser->ReportError(fxl::StringPrintf(
          "Unrecognized network member \"%s\"", i->name.GetString()));
      return false;
    }
  }

  // check that a non-empty name is provided:
  if (name_.empty()) {
    json_parser->ReportError(
        "network name must be provided and can't be an empty string");
    return false;
  }

  return true;
}

const std::string& Network::name() const { return name_; }

const std::vector<Endpoint>& Network::endpoints() const { return endpoints_; }

}  // namespace config
}  // namespace netemul