// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "types.h"

#include <unordered_map>

namespace broker_service::cobalt {

SupportedType GetSupportedType(std::string_view value) {
  static const std::unordered_map<std::string_view, SupportedType> kTypeMap = {
      {"HISTOGRAM", SupportedType::kHistogram},
      {"COUNTER", SupportedType::kCounter},
  };
  auto it = kTypeMap.find(value);
  if (it == kTypeMap.end()) {
    return SupportedType::kUnknown;
  }
  return it->second;
}

}  // namespace broker_service::cobalt
