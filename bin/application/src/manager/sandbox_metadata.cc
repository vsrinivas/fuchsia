// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "application/src/manager/sandbox_metadata.h"

#include "third_party/rapidjson/rapidjson/document.h"

namespace app {

constexpr char kDev[] = "dev";

SandboxMetadata::SandboxMetadata() = default;

SandboxMetadata::~SandboxMetadata() = default;

bool SandboxMetadata::Parse(const std::string& data) {
  dev_.clear();

  rapidjson::Document document;
  document.Parse(data);
  if (!document.IsObject())
    return false;

  auto dev = document.FindMember(kDev);
  if (dev != document.MemberEnd()) {
    const auto& value = dev->value;
    if (!value.IsArray())
      return false;
    for (const auto& path : value.GetArray()) {
      if (!path.IsString())
        return false;
      dev_.push_back(path.GetString());
    }
  }

  return true;
}

}  // namespace app
