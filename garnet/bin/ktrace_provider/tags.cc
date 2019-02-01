// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ktrace_provider/tags.h"

#include <mutex>

namespace ktrace_provider {
namespace {

#define TAG_TYPE_16B TagType::kBasic
#define TAG_TYPE_32B TagType::kQuad
#define TAG_TYPE_NAME TagType::kName
#define KTRACE_DEF(num, type, name, group) \
  {num, KTRACE_GRP_##group, TAG_TYPE_##type, #name},

constexpr TagInfo kTags[] = {
#include <lib/zircon-internal/ktrace-def.h>
};

std::once_flag g_tags_once;
TagMap g_tags;

}  // namespace

const TagMap& GetTags() {
  std::call_once(g_tags_once, [] {
    for (const auto& item : kTags) {
      g_tags.emplace(item.num, item);
    }
  });
  return g_tags;
}

}  // namespace ktrace_provider
