// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/bin/sessionmgr/storage/encode_module_path.h"

#include <lib/syslog/cpp/macros.h>

#include <string>
#include <vector>

#include "src/lib/fxl/strings/join_strings.h"
#include "src/modular/lib/string_escape/string_escape.h"

namespace modular {

constexpr char kEscaper = '\\';
constexpr char kCharsToEscape[] = ":/";
constexpr char kSubSeparator[] = ":";

std::string EncodeModulePath(const std::vector<std::string>& module_path) {
  std::vector<std::string> segments;
  segments.reserve(module_path.size());
  for (const auto& module_path_part : module_path) {
    segments.emplace_back(StringEscape(module_path_part, kCharsToEscape, kEscaper));
  }
  return fxl::JoinStrings(segments, kSubSeparator);
}

}  // namespace modular
