// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/appmgr/runtime_metadata.h"

#include "third_party/rapidjson/rapidjson/document.h"

namespace component {

constexpr char kRunner[] = "runner";

RuntimeMetadata::RuntimeMetadata() = default;

RuntimeMetadata::~RuntimeMetadata() = default;

bool RuntimeMetadata::Parse(const std::string& data) {
  rapidjson::Document document;
  document.Parse(data);
  if (!document.IsObject())
    return false;
  return Parse(document);
}

bool RuntimeMetadata::Parse(const rapidjson::Value& runtime_value) {
  runner_.clear();
  auto runner = runtime_value.FindMember(kRunner);
  if (runner == runtime_value.MemberEnd() || !runner->value.IsString()) {
    return false;
  }
  runner_ = runner->value.GetString();

  return true;
}

}  // namespace component
