// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_KTRACE_PROVIDER_TAGS_H_
#define GARNET_BIN_KTRACE_PROVIDER_TAGS_H_

#include <lib/zircon-internal/ktrace.h>

#include "stdint.h"

#include <unordered_map>

namespace ktrace_provider {

enum class TagType { kBasic, kQuad, kName };

struct TagInfo {
  uint32_t num;
  uint32_t group;
  TagType type;
  const char* name;
};

// Gets a map of trace tags descriptions keyed by tag value.
using TagMap = std::unordered_map<uint32_t, TagInfo>;
const TagMap& GetTags();

}  // namespace ktrace_provider

#endif  // GARNET_BIN_KTRACE_PROVIDER_TAGS_H_
