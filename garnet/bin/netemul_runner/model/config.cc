// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "config.h"
#include <lib/fxl/strings/string_printf.h>

namespace netemul {
namespace config {

static const char* kNetworks = "networks";
static const char* kEnvironment = "environment";
static const char* kDefaultUrl = "default_url";
static const char* kDisabled = "disabled";
static const char* kTimeout = "timeout";
static const char* kCapture = "capture";
static const char* kCaptureAlways = "ALWAYS";
static const char* kCaptureOnError = "ON_ERROR";
static const char* kCaptureNo = "NO";

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

  // load all defaults:
  if (!environment_.ParseFromJSON(rapidjson::Value(rapidjson::kObjectType),
                                  json_parser)) {
    return false;
  }
  default_url_ = "";
  disabled_ = false;
  timeout_ = zx::duration::infinite();
  networks_.clear();
  capture_mode_ = CaptureMode::NONE;

  // iterate over config members:
  for (auto i = value.MemberBegin(); i != value.MemberEnd(); i++) {
    if (i->name == kNetworks) {
      if (!i->value.IsArray()) {
        json_parser->ReportError("\"networks\" property must be an Array");
        return false;
      }
      const auto& nets = i->value.GetArray();
      for (auto n = nets.Begin(); n != nets.End(); n++) {
        auto& net = networks_.emplace_back();
        if (!net.ParseFromJSON(*n, json_parser)) {
          return false;
        }
      }
    } else if (i->name == kEnvironment) {
      if (!environment_.ParseFromJSON(i->value, json_parser)) {
        return false;
      }
    } else if (i->name == kDefaultUrl) {
      if (!i->value.IsString()) {
        json_parser->ReportError("\"default_url\" must be a String");
        return false;
      }
      default_url_ = i->value.GetString();
    } else if (i->name == kDisabled) {
      if (!i->value.IsBool()) {
        json_parser->ReportError("\"disabled\" must be a Boolean value");
        return false;
      }
      disabled_ = i->value.GetBool();
    } else if (i->name == kTimeout) {
      if (!i->value.IsUint64() || i->value.GetUint64() <= 0) {
        json_parser->ReportError(
            "\"timeout\" must be a positive integer Number value");
        return false;
      }
      timeout_ = zx::sec(i->value.GetUint64());
    } else if (i->name == kCapture) {
      if (i->value.IsBool()) {
        capture_mode_ =
            i->value.GetBool() ? CaptureMode::ON_ERROR : CaptureMode::NONE;
      } else if (i->value.IsString()) {
        std::string val = i->value.GetString();
        if (val == kCaptureNo) {
          capture_mode_ = CaptureMode::NONE;
        } else if (val == kCaptureOnError) {
          capture_mode_ = CaptureMode::ON_ERROR;
        } else if (val == kCaptureAlways) {
          capture_mode_ = CaptureMode::ALWAYS;
        } else {
          json_parser->ReportError("unrecognized \"capture\" option");
          return false;
        }
      } else {
        json_parser->ReportError(
            "\"capture\" must be a Boolean or String value");
        return false;
      }
    } else {
      json_parser->ReportError(fxl::StringPrintf(
          "Unrecognized config member \"%s\"", i->name.GetString()));
      return false;
    }
  }

  return true;
}

const std::vector<Network>& Config::networks() const { return networks_; }

const Environment& Config::environment() const { return environment_; }

const std::string& Config::default_url() const { return default_url_; }

bool Config::disabled() const { return disabled_; }

zx::duration Config::timeout() const { return timeout_; }

CaptureMode Config::capture() const { return capture_mode_; }

}  // namespace config
}  // namespace netemul