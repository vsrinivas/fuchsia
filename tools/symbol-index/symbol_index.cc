// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/symbol-index/symbol_index.h"

#include <lib/syslog/cpp/macros.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>

#include "src/lib/fxl/strings/split_string.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace symbol_index {

namespace {

// Canonical path with the following rules
// 1. Allow non-existent components (as opposite to std::filesystem::canonical).
// 2. Remove tailing "/" (as opposite to std::filesystem::weakly_canonical).
std::string CanonicalPath(std::string path) {
  auto canonical = std::filesystem::absolute(path).lexically_normal();
  if (canonical.filename().empty())
    canonical = canonical.parent_path();
  return canonical;
}

}  // namespace

std::string SymbolIndex::Entry::ToString() const {
  std::string str = symbol_path;
  if (!build_dir.empty())
    str += "\t" + build_dir;
  return str;
}

SymbolIndex::SymbolIndex(const std::string& path) {
  if (path.empty()) {
    file_path_ = std::string(std::getenv("HOME")) + "/.fuchsia/debug/symbol-index";
  } else {
    file_path_ = path;
  }
}

// TODO: Split out the parsing into something that takes an input stream for better testing.
Error SymbolIndex::Load() {
  // Clears the entries_ first, in case Load gets called twice.
  entries_.clear();
  std::error_code err;

  if (!std::filesystem::exists(file_path_, err)) {
    return "";
  }

  std::ifstream file(file_path_);
  if (file.fail()) {
    return fxl::StringPrintf("Cannot open %s to read", file_path_.c_str());
  }

  std::string line;
  std::string symbol_path;
  std::string build_dir;

  while (!file.eof()) {
    std::getline(file, line);
    if (file.fail()) {
      // If the file ends with \n, we will get failbit, eofbit and line == "".
      if (file.eof())
        break;
      return fxl::StringPrintf("Error reading %s", file_path_.c_str());
    }

    size_t tab_index = line.find('\t');
    if (tab_index != std::string::npos) {
      symbol_path = line.substr(0, tab_index);
      build_dir = line.substr(tab_index + 1);
    } else {
      symbol_path = line;
      build_dir.clear();
    }

    // Both paths must be absolute.
    if (symbol_path.empty() || symbol_path[0] != '/' ||
        (!build_dir.empty() && build_dir[0] != '/')) {
      FX_LOGS(ERROR) << "Invalid line in " << file_path_ << ": " << line;
      continue;
    }

    entries_.emplace_back(symbol_path, build_dir);
  }
  return "";
}

bool SymbolIndex::Add(std::string symbol_path, std::string build_dir) {
  symbol_path = CanonicalPath(symbol_path);
  if (!build_dir.empty()) {
    build_dir = CanonicalPath(build_dir);
  }
  if (std::find_if(entries_.begin(), entries_.end(), [&symbol_path](Entry e) {
        return e.symbol_path == symbol_path;
      }) != entries_.end()) {
    return false;
  }
  entries_.emplace_back(symbol_path, build_dir);
  return true;
}

bool SymbolIndex::Remove(std::string symbol_path) {
  symbol_path = CanonicalPath(symbol_path);
  auto loc = std::find_if(entries_.begin(), entries_.end(),
                          [&symbol_path](Entry e) { return e.symbol_path == symbol_path; });
  if (loc == entries_.end()) {
    return false;
  }
  entries_.erase(loc);
  return true;
}

std::vector<SymbolIndex::Entry> SymbolIndex::Purge() {
  auto purge_begin = std::remove_if(entries_.begin(), entries_.end(), [](Entry e) {
    std::error_code err;
    return !std::filesystem::exists(e.symbol_path, err);
  });
  std::vector<Entry> result(purge_begin, entries_.end());
  entries_.erase(purge_begin, entries_.end());
  return result;
}

Error SymbolIndex::Save() {
  std::error_code err;
  std::filesystem::create_directories(std::filesystem::path(file_path_).parent_path(), err);
  std::ofstream file(file_path_);
  if (file.fail()) {
    return fxl::StringPrintf("Cannot open %s to write", file_path_.c_str());
  }

  for (const auto& entry : entries_) {
    file << entry.ToString() << std::endl;
  }
  return "";
}

}  // namespace symbol_index
