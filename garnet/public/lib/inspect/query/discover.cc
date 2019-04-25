// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "discover.h"

#include <dirent.h>
#include <fcntl.h>
#include <fuchsia/io/cpp/fidl.h>
#include <lib/fit/promise.h>
#include <lib/inspect/query/source.h>
#include <lib/inspect/reader.h>
#include <src/lib/files/glob.h>
#include <src/lib/files/path.h>
#include <src/lib/fxl/strings/concatenate.h>

#include "source.h"

namespace inspect {

namespace {

fit::result<Location> FileToLocation(std::string directory_path,
                                     std::string file_name) {
  if (file_name.compare(fuchsia::inspect::Inspect::Name_) == 0) {
    return fit::ok(Location{.type = Location::Type::INSPECT_FIDL,
                            .directory_path = std::move(directory_path),
                            .file_name = std::move(file_name),
                            .inspect_path_components = {}});
  } else if (std::regex_search(file_name, inspect_vmo_file_regex())) {
    return fit::ok(Location{.type = Location::Type::INSPECT_VMO,
                            .directory_path = std::move(directory_path),
                            .file_name = std::move(file_name),
                            .inspect_path_components = {}});
  }
  return fit::error();
}

}  // namespace

std::vector<Location> SyncFindPaths(const std::string& path) {
  if (path.find("#") != std::string::npos) {
    // This path refers to something nested inside an inspect hierarchy,
    // return it directly.
    auto location = Location::Parse(path);
    if (location.is_ok()) {
      return {location.take_value()};
    } else {
      return {};
    }
  }

  std::vector<Location> locations;

  std::vector<std::string> search_paths;
  search_paths.push_back(path);

  while (!search_paths.empty()) {
    std::string path = std::move(search_paths.back());
    search_paths.pop_back();

    DIR* dir = opendir(path.c_str());
    if (!dir) {
      continue;
    }

    struct dirent* de;
    while ((de = readdir(dir)) != nullptr) {
      if (strcmp(de->d_name, ".") == 0) {
        continue;
      }
      if (de->d_type == DT_DIR) {
        search_paths.push_back(files::JoinPath(path, de->d_name));
      } else {
        auto result = FileToLocation(path.c_str(), de->d_name);
        if (result.is_ok()) {
          locations.emplace_back(result.take_value());
        }
      }
    }

    closedir(dir);
  }

  return locations;
}

std::vector<Location> SyncSearchGlobs(const std::vector<std::string>& globs) {
  std::vector<Location> locations;
  for (const auto& glob_path : globs) {
    for (const auto* g : files::Glob(glob_path)) {
      auto result = FileToLocation(files::GetDirectoryName(g).c_str(),
                                   files::GetBaseName(g).c_str());
      if (result.is_ok()) {
        locations.emplace_back(result.take_value());
      }
    }
  }
  return locations;
}

}  // namespace inspect
