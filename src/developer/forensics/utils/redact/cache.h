// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_UTILS_REDACT_CACHE_H_
#define SRC_DEVELOPER_FORENSICS_UTILS_REDACT_CACHE_H_

#include <string>
#include <unordered_map>

namespace forensics {

// Associates unique integer identifiers with strings, e.g. the string "12345" will be the only
// string to have an ID X and always have it.
class RedactionIdCache {
 public:
  RedactionIdCache() = default;

  int GetId(const std::string& value);

  // Require move-only semantics are required because the cache is stateful.
  RedactionIdCache(const RedactionIdCache&) = delete;
  RedactionIdCache& operator=(const RedactionIdCache&) = delete;
  RedactionIdCache(RedactionIdCache&&) = default;
  RedactionIdCache& operator=(RedactionIdCache&&) = default;

 private:
  // TODO(fxbug.dev/94086): The map grows unbounded, expose the number of elements with Inspect.
  int next_id_{0};
  std::unordered_map<std::string, int> ids_;
};

}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_UTILS_REDACT_CACHE_H_
