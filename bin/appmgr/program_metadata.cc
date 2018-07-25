// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/appmgr/program_metadata.h"

#include <algorithm>

#include "third_party/rapidjson/rapidjson/document.h"

namespace component {

constexpr char kBinary[] = "binary";

ProgramMetadata::ProgramMetadata() = default;

ProgramMetadata::~ProgramMetadata() = default;

bool ProgramMetadata::Parse(const rapidjson::Value& program_value) {
  binary_.clear();
  null_ = true;

  const auto binary = program_value.FindMember(kBinary);
  if (binary == program_value.MemberEnd() || !binary->value.IsString()) {
    return false;
  }
  binary_ = binary->value.GetString();

  null_ = false;
  return true;
}

}  // namespace component
