// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "environment.h"

namespace netemul {
namespace config {

static const char* kDefaultName = "test-env";
static const char* kName = "name";
static const char* kServices = "services";
static const char* kDevices = "devices";
static const char* kChildren = "children";
static const char* kTest = "test";
static const char* kInheritServices = "inherit_services";
static const char* kApps = "apps";
static const char* kSetup = "setup";
static const bool kDefaultInheritServices = true;

bool Environment::ParseFromJSON(const rapidjson::Value& value,
                                json::JSONParser* parser) {
  if (!value.IsObject()) {
    parser->ReportError("environment must be object type");
    return false;
  }

  auto name = value.FindMember(kName);
  if (name == value.MemberEnd()) {
    name_ = kDefaultName;
  } else if (!name->value.IsString()) {
    parser->ReportError("environment name must be string value");
    return false;
  } else {
    name_ = name->value.GetString();
  }

  auto inherit = value.FindMember(kInheritServices);
  if (inherit == value.MemberEnd()) {
    inherit_services_ = kDefaultInheritServices;
  } else if (!inherit->value.IsBool()) {
    parser->ReportError("inherit_services must be boolean value");
    return false;
  } else {
    inherit_services_ = inherit->value.GetBool();
  }

  auto devices = value.FindMember(kDevices);
  if (devices == value.MemberEnd()) {
    devices_.clear();
  } else if (!devices->value.IsArray()) {
    parser->ReportError("environment devices must be array of strings");
    return false;
  } else {
    auto devs = devices->value.GetArray();
    devices_.clear();
    for (auto d = devs.Begin(); d != devs.End(); d++) {
      if (!d->IsString()) {
        parser->ReportError("environment devices must be array of strings");
        return false;
      }
      devices_.emplace_back(d->GetString());
    }
  }

  auto services = value.FindMember(kServices);
  if (services == value.MemberEnd()) {
    services_.clear();
  } else if (!services->value.IsObject()) {
    parser->ReportError("environment services must be object");
    return false;
  } else {
    for (auto s = services->value.MemberBegin();
         s != services->value.MemberEnd(); s++) {
      auto& ns = services_.emplace_back(s->name.GetString());
      if (!ns.ParseFromJSON(s->value, parser)) {
        return false;
      }
    }
  }

  auto test = value.FindMember(kTest);
  if (test == value.MemberEnd()) {
    test_.clear();
  } else if (!test->value.IsArray()) {
    parser->ReportError("environment tests must be array of objects");
    return false;
  } else {
    auto test_arr = test->value.GetArray();
    for (auto t = test_arr.Begin(); t != test_arr.End(); t++) {
      auto& nt = test_.emplace_back();
      if (!nt.ParseFromJSON(*t, parser)) {
        return false;
      }
    }
  }

  auto children = value.FindMember(kChildren);
  if (children == value.MemberEnd()) {
    children_.clear();
  } else if (!children->value.IsArray()) {
    parser->ReportError("environment children must be array of objects");
    return false;
  } else {
    auto ch_arr = children->value.GetArray();
    for (auto c = ch_arr.Begin(); c != ch_arr.End(); c++) {
      auto& nc = children_.emplace_back();
      if (!nc.ParseFromJSON(*c, parser)) {
        return false;
      }
    }
  }

  auto apps = value.FindMember(kApps);
  if (apps == value.MemberEnd()) {
    apps_.clear();
  } else if (!apps->value.IsArray()) {
    parser->ReportError("environment apps must be array");
    return false;
  } else {
    auto app_arr = apps->value.GetArray();
    for (auto a = app_arr.Begin(); a != app_arr.End(); a++) {
      auto& na = apps_.emplace_back();
      if (!na.ParseFromJSON(*a, parser)) {
        return false;
      }
    }
  }

  auto setup = value.FindMember(kSetup);
  if (setup == value.MemberEnd()) {
    setup_.clear();
  } else if (!setup->value.IsArray()) {
    parser->ReportError("environment setup must be array");
    return false;
  } else {
    auto setup_arr = setup->value.GetArray();
    for (auto s = setup_arr.Begin(); s != setup_arr.End(); s++) {
      auto& ns = setup_.emplace_back();
      if (!ns.ParseFromJSON(*s, parser)) {
        return false;
      }
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

}  // namespace config
}  // namespace netemul