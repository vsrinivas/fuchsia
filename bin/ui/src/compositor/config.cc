// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/compositor/config.h"

#include <utility>

#include "lib/ftl/files/file.h"

namespace compositor {

constexpr char kDevicePixelRatio[] = "device_pixel_ratio";

Config::Config() = default;

Config::~Config() = default;

bool Config::ReadFrom(const std::string& config_file) {
  std::string data;
  return files::ReadFileToString(config_file, &data) &&
         Parse(data, config_file);
}

bool Config::Parse(const std::string& string, const std::string& config_file) {
  modular::JsonDoc document;
  document.Parse(string);
  FTL_CHECK(!document.HasParseError())
      << "Could not parse file at " << config_file;

  auto device_pixel_ratio_it = document.FindMember(kDevicePixelRatio);
  if (device_pixel_ratio_it != document.MemberEnd()) {
    const auto& value = device_pixel_ratio_it->value;
    if (!value.IsNumber())
      return false;

    device_pixel_ratio_ = value.GetFloat();
  }

  return true;
}

}  // namespace compositor
