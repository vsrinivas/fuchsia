// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/appmgr/sandbox_metadata.h"

#include <algorithm>

#include "third_party/rapidjson/rapidjson/document.h"

namespace component {
namespace {

template <typename Value>
bool CopyArrayToVector(const Value& value, std::vector<std::string>* vector) {
  if (!value.IsArray())
    return false;
  for (const auto& entry : value.GetArray()) {
    if (!entry.IsString())
      return false;
    vector->push_back(entry.GetString());
  }
  return true;
}

}  // namespace

constexpr char kDev[] = "dev";
constexpr char kSystem[] = "system";
constexpr char kPkgfs[] = "pkgfs";
constexpr char kFeatures[] = "features";
constexpr char kBoot[] = "boot";

SandboxMetadata::SandboxMetadata() = default;

SandboxMetadata::~SandboxMetadata() = default;

bool SandboxMetadata::Parse(const rapidjson::Value& sandbox_value) {
  dev_.clear();
  features_.clear();

  if (!sandbox_value.IsObject()) {
    return false;
  }

  auto dev = sandbox_value.FindMember(kDev);
  if (dev != sandbox_value.MemberEnd()) {
    if (!CopyArrayToVector(dev->value, &dev_))
      return false;
  }

  auto system = sandbox_value.FindMember(kSystem);
  if (system != sandbox_value.MemberEnd()) {
    if (!CopyArrayToVector(system->value, &system_))
      return false;
  }

  auto pkgfs = sandbox_value.FindMember(kPkgfs);
  if (pkgfs != sandbox_value.MemberEnd()) {
    if (!CopyArrayToVector(pkgfs->value, &pkgfs_))
      return false;
  }

  auto features = sandbox_value.FindMember(kFeatures);
  if (features != sandbox_value.MemberEnd()) {
    if (!CopyArrayToVector(features->value, &features_))
      return false;
  }

  auto boot = sandbox_value.FindMember(kBoot);
  if (boot != sandbox_value.MemberEnd()) {
    if (!CopyArrayToVector(boot->value, &boot_))
      return false;
  }

  return true;
}

bool SandboxMetadata::HasFeature(const std::string& feature) {
  return std::find(features_.begin(), features_.end(), feature) !=
         features_.end();
}

void SandboxMetadata::AddFeature(std::string feature) {
  features_.push_back(std::move(feature));
}

}  // namespace component
