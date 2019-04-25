// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/inspect/cpp/fidl.h>
#include <lib/inspect/query/location.h>
#include <src/lib/files/path.h>
#include <src/lib/fxl/strings/concatenate.h>
#include <src/lib/fxl/strings/join_strings.h>
#include <src/lib/fxl/strings/split_string.h>
#include <src/lib/fxl/strings/substitute.h>

#include "src/lib/fxl/strings/string_view.h"

namespace inspect {

std::regex inspect_vmo_file_regex() { return std::regex("\\.inspect$"); }

std::ostream& operator<<(std::ostream& os, const Location& location) {
  auto type = std::string(
      (location.type == Location::Type::INSPECT_VMO) ? "VMO" : "FIDL");
  os << fxl::Substitute(
      "Location('$0', '$1', $2, [$3])", location.directory_path,
      location.file_name, type,
      fxl::JoinStrings(location.inspect_path_components, ", "));

  return os;
}

bool Location::operator==(const Location& other) const {
  return type == other.type && directory_path == other.directory_path &&
         file_name == other.file_name &&
         inspect_path_components == other.inspect_path_components;
}

std::string Location::RelativeFilePath() const {
  return files::JoinPath(directory_path, file_name);
}

std::string Location::AbsoluteFilePath() const {
  return files::SimplifyPath(files::AbsolutePath(RelativeFilePath()));
}

std::string Location::SimplifiedFilePath() const {
  if (type == Type::INSPECT_FIDL) {
    return directory_path;
  }
  return RelativeFilePath();
}

std::string Location::NodePath(const std::vector<std::string>& suffix) const {
  auto ret = SimplifiedFilePath();
  if (inspect_path_components.size() == 0 && suffix.size() == 0) {
    return ret;
  }

  ret.push_back('#');

  ret.append(fxl::JoinStrings(inspect_path_components, "/"));
  if (inspect_path_components.size() > 0 && suffix.size() > 0) {
    ret.push_back('/');
  }
  ret.append(fxl::JoinStrings(suffix, "/"));
  return ret;
}

fit::result<Location, std::string> Location::Parse(const std::string& path) {
  auto parts =
      fxl::SplitStringCopy(path, "#", fxl::kKeepWhitespace, fxl::kSplitWantAll);
  if (parts.size() > 2) {
    return fit::error("Path contains more than one '#'");
  }

  std::vector<std::string> inspect_parts;
  if (parts.size() == 2) {
    inspect_parts = fxl::SplitStringCopy(parts[1], "/", fxl::kKeepWhitespace,
                                         fxl::kSplitWantAll);
  }

  if (std::regex_search(parts[0], inspect_vmo_file_regex())) {
    // The file seems to be an inspect VMO.
    return fit::ok(
        Location{.type = Location::Type::INSPECT_VMO,
                 .directory_path = files::GetDirectoryName(parts[0]),
                 .file_name = files::GetBaseName(parts[0]),
                 .inspect_path_components = std::move(inspect_parts)});
  } else if (files::GetBaseName(parts[0]) == fuchsia::inspect::Inspect::Name_) {
    // The file seems to be an inspect FIDL interface.
    return fit::ok(
        Location{.directory_path = files::GetDirectoryName(parts[0]),
                 .file_name = files::GetBaseName(parts[0]),
                 .inspect_path_components = std::move(inspect_parts)});
  } else {
    // Default to treating the path as a directory, and look for the FIDL
    // interface inside.
    return fit::ok(
        Location{.directory_path = parts[0],
                 .file_name = fuchsia::inspect::Inspect::Name_,
                 .inspect_path_components = std::move(inspect_parts)});
  }
}

}  // namespace inspect
