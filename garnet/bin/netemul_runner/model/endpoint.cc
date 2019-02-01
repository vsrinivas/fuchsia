// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "endpoint.h"
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

  auto name = value.FindMember(kName);
  if (name == value.MemberEnd()) {
    parser->ReportError("endpoint must have name property");
    return false;
  } else if ((!name->value.IsString()) || name->value.GetStringLength() == 0) {
    parser->ReportError("endpoint name must be a non-empty string");
    return false;
  } else {
    name_ = name->value.GetString();
  }

  auto mtu = value.FindMember(kMtu);
  if (mtu == value.MemberEnd()) {
    mtu_ = kDefaultMtu;
  } else if (!mtu->value.IsNumber()) {
    parser->ReportError("endpoint mtu must be number");
    return false;
  } else {
    auto v = static_cast<uint16_t>(mtu->value.GetUint());
    if (v == 0) {
      parser->ReportError(
          "endpoint with zero mtu is invalid, omit to use default");
      return false;
    }
    mtu_ = v;
  }

  auto mac = value.FindMember(kMac);
  if (mac == value.MemberEnd()) {
    mac_ = nullptr;
  } else if (!mac->value.IsString()) {
    parser->ReportError("endpoint mac must be string");
    return false;
  } else {
    auto macval = std::make_unique<Mac>();
    if (std::sscanf(mac->value.GetString(),
                    "%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx", macval->d,
                    macval->d + 1, macval->d + 2, macval->d + 3, macval->d + 4,
                    macval->d + 5) != 6) {
      parser->ReportError("Can't parse supplied mac address");
      return false;
    }
    mac_ = std::move(macval);
  }

  auto up = value.FindMember(kUp);
  if (up == value.MemberEnd()) {
    up_ = kDefaultUp;
  } else if (!up->value.IsBool()) {
    parser->ReportError("endpoint up must be bool");
    return false;
  } else {
    up_ = up->value.GetBool();
  }

  return true;
}

const std::string& Endpoint::name() const { return name_; }

const std::unique_ptr<Mac>& Endpoint::mac() const { return mac_; }

uint16_t Endpoint::mtu() const { return mtu_; }

bool Endpoint::up() const { return up_; }

}  // namespace config
}  // namespace netemul