// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/run_test_component/env_config.h"

#include "garnet/lib/json/json_parser.h"
#include "lib/fxl/strings/substitute.h"
#include "third_party/rapidjson/rapidjson/document.h"

namespace run {
namespace {

using fxl::Substitute;

}  // namespace

EnvironmentConfig::EnvironmentConfig() : has_error_(false) {}

EnvironmentConfig::~EnvironmentConfig() {}

void EnvironmentConfig::CreateMap(const std::string& environment_name,
                                  EnvironmentType env_type,
                                  const rapidjson::Document& document) {
  if (!document.HasMember(environment_name)) {
    has_error_ = true;
    errors_.push_back(
        Substitute("Invalid config. '$0' not found", environment_name));
    return;
  }
  const rapidjson::Value& urls = document[environment_name];
  if (!urls.IsArray()) {
    has_error_ = true;
    errors_.push_back(Substitute(
        "Invalid config. '$0' section should be a array", environment_name));
    return;
  }
  for (rapidjson::SizeType i = 0; i < urls.Size(); i++) {
    auto& url = urls[i];
    if (!url.IsString()) {
      has_error_ = true;
      errors_.push_back(
          Substitute("Invalid config. '$0' section should be a string array",
                     environment_name));
      return;
    }
    url_map_[url.GetString()] = env_type;
  }
}

EnvironmentConfig EnvironmentConfig::CreateFromFile(
    const std::string& file_path) {
  EnvironmentConfig config;
  json::JSONParser parser;
  rapidjson::Document document = parser.ParseFromFile(file_path);
  if (parser.HasError()) {
    config.has_error_ = true;
    config.errors_.push_back(parser.error_str());
    return config;
  }
  config.CreateMap("root", EnvironmentType::ROOT, document);
  config.CreateMap("sys", EnvironmentType::SYS, document);
  return config;
}

}  // namespace run
