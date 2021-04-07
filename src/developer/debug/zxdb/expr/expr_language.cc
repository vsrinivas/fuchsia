// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/expr_language.h"

#include <filesystem>
#include <map>

namespace zxdb {

std::optional<ExprLanguage> FileNameToLanguage(const std::string& name) {
  static std::map<std::string, ExprLanguage> mapping;
  if (mapping.empty()) {
    mapping[".c"] = ExprLanguage::kC;
    mapping[".cc"] = ExprLanguage::kC;
    mapping[".cpp"] = ExprLanguage::kC;
    mapping[".cxx"] = ExprLanguage::kC;
    mapping[".c++"] = ExprLanguage::kC;
    mapping[".h"] = ExprLanguage::kC;
    mapping[".hh"] = ExprLanguage::kC;
    mapping[".hpp"] = ExprLanguage::kC;
    mapping[".hxx"] = ExprLanguage::kC;
    mapping[".h++"] = ExprLanguage::kC;
    mapping[".inc"] = ExprLanguage::kC;

    mapping[".rs"] = ExprLanguage::kRust;
  }

  if (auto found = mapping.find(std::filesystem::path(name).extension()); found != mapping.end())
    return found->second;
  return std::nullopt;
}

}  // namespace zxdb
