// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "endpoint.h"
#include <src/lib/fxl/strings/string_printf.h>
#include <cstdio>
#include <memory>

namespace netemul {
namespace config {

static const char* kName = "name";
static const char* kMtu = "mtu";
static const char* kMac = "mac";
static const char* kUp = "up";
static const bool kDefaultUp = true;
static const uint16_t kDefaultMtu = 1500;

bool Endpoint::ParseFromJSON(const rapidjson::Value& value,
                             json::JSONParser* parser) {
  if (!value.IsObject()) {
    parser->ReportError("endpoint must be object type");
    return false;
  }

  // set default values:
  name_ = "";
  mtu_ = kDefaultMtu;
  mac_ = nullptr;
  up_ = kDefaultUp;

  // iterate over members:
  for (auto i = value.MemberBegin(); i != value.MemberEnd(); i++) {
    if (i->name == kName) {
      if ((!i->value.IsString()) || i->value.GetStringLength() == 0) {
        parser->ReportError("endpoint name must be a non-empty string");
        return false;
      }
      name_ = i->value.GetString();
    } else if (i->name == kMtu) {
      if (!i->value.IsNumber()) {
        parser->ReportError("endpoint mtu must be number");
        return false;
      }
      auto v = static_cast<uint16_t>(i->value.GetUint());
      if (v == 0) {
        parser->ReportError(
            "endpoint with zero mtu is invalid, omit to use default");
        return false;
      }
      mtu_ = v;
    } else if (i->name == kMac) {
      if (!i->value.IsString()) {
        parser->ReportError("endpoint mac must be string");
        return false;
      }
      auto macval = std::make_unique<Mac>();
      if (std::sscanf(i->value.GetString(),
                      "%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx", macval->d,
                      macval->d + 1, macval->d + 2, macval->d + 3,
                      macval->d + 4, macval->d + 5) != 6) {
        parser->ReportError("Can't parse supplied mac address");
        return false;
      }
      mac_ = std::move(macval);
    } else if (i->name == kUp) {
      if (!i->value.IsBool()) {
        parser->ReportError("endpoint up must be bool");
        return false;
      }
      up_ = i->value.GetBool();
    } else {
      parser->ReportError(fxl::StringPrintf(
          "Unrecognized endpoint member \"%s\"", i->name.GetString()));
      return false;
    }
  }

  // check that a non-empty name is provided:
  if (name_.empty()) {
    parser->ReportError(
        "endpoint name must be provided and can't be an empty string");
    return false;
  }

  return true;
}

const std::string& Endpoint::name() const { return name_; }

const std::unique_ptr<Mac>& Endpoint::mac() const { return mac_; }

uint16_t Endpoint::mtu() const { return mtu_; }

bool Endpoint::up() const { return up_; }

}  // namespace config
}  // namespace netemul