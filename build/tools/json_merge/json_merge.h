// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BUILD_TOOLS_JSON_MERGE_JSON_MERGE_H_
#define BUILD_TOOLS_JSON_MERGE_JSON_MERGE_H_

#include <iostream>
#include <memory>
#include <vector>

struct input_file {
  std::string name;
  std::unique_ptr<std::istream> contents;
};

struct MergeConfig {
  bool relaxed_input = false;
  bool deep_merge = false;
  bool minify = false;
};

// Merge one or more JSON documents.
// Returns zero if successful.
// On non-zero return value, writes human-readable errors to `errors`.
int JSONMerge(const std::vector<struct input_file>& inputs, std::ostream& output,
              std::ostream& errors, const MergeConfig& config);

#endif  // BUILD_TOOLS_JSON_MERGE_JSON_MERGE_H_
