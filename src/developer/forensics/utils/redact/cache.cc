// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/utils/redact/cache.h"

#include <unordered_map>

namespace forensics {

RedactionIdCache::RedactionIdCache(const int starting_id) : next_id_(starting_id) {}

int RedactionIdCache::GetId(const std::string& value) {
  if (ids_.count(value) == 0) {
    ids_[value] = ++next_id_;
  }
  return ids_[value];
}

}  // namespace forensics
