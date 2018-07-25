// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/appmgr/runtime_metadata.h"

#include "third_party/rapidjson/rapidjson/document.h"

namespace component {

constexpr char kRunner[] = "runner";

RuntimeMetadata::RuntimeMetadata() = default;

RuntimeMetadata::~RuntimeMetadata() = default;

bool RuntimeMetadata::ParseFromData(const std::string& data) {
  runner_.clear();
  null_ = true;

  rapidjson::Document document;
  document.Parse(data);
  if (document.HasParseError()) {
    return false;
  }
  return ParseFromDocument(document);
}

bool RuntimeMetadata::ParseFromDocument(const rapidjson::Document& document) {
  runner_.clear();
  null_ = true;

  const auto runner = document.FindMember(kRunner);
  if (runner == document.MemberEnd()) {
    // Valid config, but no runtime.
    return true;
  }
  if (!runner->value.IsString()) {
    return false;
  }
  runner_ = runner->value.GetString();
  null_ = false;
  return true;
}

}  // namespace component
