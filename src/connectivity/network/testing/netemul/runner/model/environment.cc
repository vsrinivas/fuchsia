// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "environment.h"

#include <src/lib/fxl/strings/string_printf.h>

namespace netemul {
namespace config {

static const char* kDefaultName = "";
static const char* kName = "name";
static const char* kServices = "services";
static const char* kDevices = "devices";
static const char* kChildren = "children";
static const char* kTest = "test";
static const char* kInheritServices = "inherit_services";
static const char* kApps = "apps";
static const char* kSetup = "setup";
static const char* kLoggerOptions = "logger_options";
static const bool kDefaultInheritServices = true;

bool Environment::ParseFromJSON(const rapidjson::Value& value,
                                json::JSONParser* parser) {
  if (!value.IsObject()) {
    parser->ReportError("environment must be object type");
    return false;
  }

  // load defaults:
  name_ = kDefaultName;
  inherit_services_ = kDefaultInheritServices;
  devices_.clear();
  services_.clear();
  test_.clear();
  children_.clear();
  apps_.clear();
  setup_.clear();
  logger_options_.SetDefaults();

  // iterate over members:
  for (auto i = value.MemberBegin(); i != value.MemberEnd(); i++) {
    if (i->name == kName) {
      if (!i->value.IsString()) {
        parser->ReportError("environment name must be string value");
        return false;
      }
      name_ = i->value.GetString();
    } else if (i->name == kInheritServices) {
      if (!i->value.IsBool()) {
        parser->ReportError("inherit_services must be boolean value");
        return false;
      }
      inherit_services_ = i->value.GetBool();
    } else if (i->name == kDevices) {
      if (!i->value.IsArray()) {
        parser->ReportError("environment devices must be array of strings");
        return false;
      }
      auto devs = i->value.GetArray();
      for (auto d = devs.Begin(); d != devs.End(); d++) {
        if (!d->IsString()) {
          parser->ReportError("environment devices must be array of strings");
          return false;
        }
        devices_.emplace_back(d->GetString());
      }
    } else if (i->name == kServices) {
      if (!i->value.IsObject()) {
        parser->ReportError("environment services must be object");
        return false;
      }
      for (auto s = i->value.MemberBegin(); s != i->value.MemberEnd(); s++) {
        auto& ns = services_.emplace_back(s->name.GetString());
        if (!ns.ParseFromJSON(s->value, parser)) {
          return false;
        }
      }
    } else if (i->name == kTest) {
      if (!i->value.IsArray()) {
        parser->ReportError("environment tests must be array of objects");
        return false;
      }
      auto test_arr = i->value.GetArray();
      for (auto t = test_arr.Begin(); t != test_arr.End(); t++) {
        auto& nt = test_.emplace_back();
        if (!nt.ParseFromJSON(*t, parser)) {
          return false;
        }
      }
    } else if (i->name == kChildren) {
      if (!i->value.IsArray()) {
        parser->ReportError("environment children must be array of objects");
        return false;
      }
      auto ch_arr = i->value.GetArray();
      for (auto c = ch_arr.Begin(); c != ch_arr.End(); c++) {
        auto& nc = children_.emplace_back();
        if (!nc.ParseFromJSON(*c, parser)) {
          return false;
        }
      }
    } else if (i->name == kApps) {
      if (!i->value.IsArray()) {
        parser->ReportError("environment apps must be array");
        return false;
      }
      auto app_arr = i->value.GetArray();
      for (auto a = app_arr.Begin(); a != app_arr.End(); a++) {
        auto& na = apps_.emplace_back();
        if (!na.ParseFromJSON(*a, parser)) {
          return false;
        }
      }
    } else if (i->name == kSetup) {
      if (!i->value.IsArray()) {
        parser->ReportError("environment setup must be array");
        return false;
      }
      auto setup_arr = i->value.GetArray();
      for (auto s = setup_arr.Begin(); s != setup_arr.End(); s++) {
        auto& ns = setup_.emplace_back();
        if (!ns.ParseFromJSON(*s, parser)) {
          return false;
        }
      }
    } else if (i->name == kLoggerOptions) {
      if (!logger_options_.ParseFromJSON(i->value, parser)) {
        return false;
      }
    } else {
      parser->ReportError(fxl::StringPrintf(
          "Unrecognized environment member \"%s\"", i->name.GetString()));
      return false;
    }
  }

  return true;
}

const std::string& Environment::name() const { return name_; }

const std::vector<Environment>& Environment::children() const {
  return children_;
}

const std::vector<std::string>& Environment::devices() const {
  return devices_;
}

const std::vector<LaunchService>& Environment::services() const {
  return services_;
}

const std::vector<LaunchApp>& Environment::test() const { return test_; }

bool Environment::inherit_services() const { return inherit_services_; }

const std::vector<LaunchApp>& Environment::apps() const { return apps_; }

const std::vector<LaunchApp>& Environment::setup() const { return setup_; }

const LoggerOptions& Environment::logger_options() const {
  return logger_options_;
}

}  // namespace config
}  // namespace netemul
