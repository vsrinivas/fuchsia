// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>

#include <stack>

#include <fuchsia/io/cpp/fidl.h>
#include <lib/fdio/util.h>
#include <lib/fit/defer.h>
#include <lib/fxl/files/file.h>
#include <lib/fxl/files/path.h>
#include <lib/fxl/strings/concatenate.h>
#include <lib/fxl/strings/join_strings.h>
#include <lib/fxl/strings/split_string.h>
#include <lib/fxl/strings/string_printf.h>
#include <lib/fxl/strings/substitute.h>
#include <lib/inspect/reader.h>

#include "garnet/bin/iquery/modes.h"

#include <iostream>

namespace iquery {
namespace {

// Consult the file system to find out how to open an inspect endpoint at the
// given path.
//
// Returns the parsed location on success.
//
// Note: This function uses synchronous filesystem operations and may block
// execution.
fit::result<ObjectLocation> ParseToLocation(const std::string& path) {
  auto parts =
      fxl::SplitStringCopy(path, "#", fxl::kKeepWhitespace, fxl::kSplitWantAll);
  if (parts.size() > 2) {
    FXL_LOG(WARNING) << "Error parsing " << path;
    return fit::error();
  }

  std::vector<std::string> inspect_parts;
  if (parts.size() == 2) {
    inspect_parts = fxl::SplitStringCopy(parts[1], "/", fxl::kKeepWhitespace,
                                         fxl::kSplitWantAll);
  }

  if (files::IsFile(parts[0])) {
    // Entry point is a file, split out the directory and base name.
    return fit::ok(
        ObjectLocation{.directory_path = files::GetDirectoryName(parts[0]),
                       .file_name = files::GetBaseName(parts[0]),
                       .inspect_path_components = std::move(inspect_parts)});
  } else if (files::IsFile(
                 files::JoinPath(parts[0], fuchsia::inspect::Inspect::Name_))) {
    return fit::ok(
        ObjectLocation{.directory_path = std::move(parts[0]),
                       .file_name = fuchsia::inspect::Inspect::Name_,
                       .inspect_path_components = std::move(inspect_parts)});
  } else {
    FXL_LOG(WARNING) << "No inspect entry point found at " << parts[0];
    return fit::error();
  }
}

// Synchronously recurse down the filesystem from the given path to find
// inspect endpoints.
std::vector<ObjectLocation> SyncFindPaths(const std::string& path) {
  FXL_VLOG(1) << "Synchronously listing paths under " << path;
  if (path.find("#") != std::string::npos) {
    // This path refers to something nested inside an inspect hierarchy, return
    // it directly.
    FXL_VLOG(1) << " Path is inside inspect hierarchy, returning directly";
    auto location = ParseToLocation(path);
    if (location.is_ok()) {
      return {location.take_value()};
    }
  }

  std::vector<ObjectLocation> ret;

  std::vector<std::string> search_paths;
  search_paths.push_back(path);

  while (!search_paths.empty()) {
    std::string path = std::move(search_paths.back());
    search_paths.pop_back();
    FXL_VLOG(1) << " Reading " << path;
    DIR* dir = opendir(path.c_str());
    if (!dir) {
      FXL_VLOG(1) << " Failed to open";
      continue;
    }

    FXL_VLOG(1) << " Opened";

    struct dirent* de;
    while ((de = readdir(dir)) != nullptr) {
      if (strcmp(de->d_name, ".") == 0) {
        continue;
      }
      if (de->d_type == DT_DIR) {
        FXL_VLOG(1) << " Adding child " << de->d_name;
        search_paths.push_back(fxl::Concatenate({path, "/", de->d_name}));
      } else {
        if (strcmp(de->d_name, fuchsia::inspect::Inspect::Name_) == 0) {
          FXL_VLOG(1) << " Found fuchsia.inspect.Inspect";
          ret.push_back(ObjectLocation{.directory_path = path,
                                       .file_name = de->d_name,
                                       .inspect_path_components = {}});
        }
      }
    }

    closedir(dir);
    FXL_VLOG(1) << " Closed";
  }

  FXL_VLOG(1) << "Done listing, found " << ret.size() << " inspect endpoints";

  return ret;
}

fit::promise<inspect::ObjectReader> OpenPathInsideRoot(
    inspect::ObjectReader reader, std::vector<std::string> path_components,
    size_t index = 0) {
  if (index >= path_components.size()) {
    return fit::make_promise([reader]() -> fit::result<inspect::ObjectReader> {
      return fit::ok(reader);
    });
  }

  return reader.OpenChild(path_components[index])
      .and_then([=](inspect::ObjectReader& child) {
        return OpenPathInsideRoot(child, path_components, index + 1);
      });
}

fit::result<fidl::InterfaceHandle<fuchsia::inspect::Inspect>> OpenInspectAtPath(
    const std::string& path) {
  fuchsia::inspect::InspectPtr inspect;
  auto endpoint = files::AbsolutePath(path);
  zx_status_t status = fdio_service_connect(
      endpoint.c_str(), inspect.NewRequest().TakeChannel().get());
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to connect at " << endpoint << " with " << status;
    return fit::error();
  }

  return fit::ok(inspect.Unbind());
}

}  // namespace

fit::promise<ObjectSource> ObjectSource::Make(ObjectLocation location,
                                              inspect::ObjectReader root_reader,
                                              int depth) {
  return OpenPathInsideRoot(root_reader, location.inspect_path_components)
      .then([depth](fit::result<inspect::ObjectReader>& reader)
                -> fit::promise<inspect::ObjectHierarchy> {
        if (!reader.is_ok()) {
          return fit::make_promise(
              []() -> fit::result<inspect::ObjectHierarchy> {
                return fit::error();
              });
        }
        return inspect::ObjectHierarchy::Make(reader.take_value(), depth);
      })
      .then([root_reader, location = std::move(location)](
                fit::result<inspect::ObjectHierarchy>& result)
                -> fit::result<ObjectSource> {
        if (!result.is_ok()) {
          FXL_LOG(ERROR) << fxl::Substitute(
              "Failed to read $0$1$2",
              files::JoinPath(location.directory_path, location.file_name),
              std::string(location.inspect_path_components.empty() ? "" : "#"),
              fxl::JoinStrings(location.inspect_path_components, "/"));
          return fit::error();
        }

        ObjectSource ret;
        ret.location_ = std::move(location);
        ret.type_ = ObjectSource::Type::INSPECT_FIDL;
        ret.hierarchy_ = result.take_value();
        return fit::ok(std::move(ret));
      });
}

std::string ObjectSource::FormatRelativePath() const {
  return FormatRelativePath({});
}

std::string ObjectSource::FormatRelativePath(
    const std::vector<std::string>& suffix) const {
  std::string ret = location_.directory_path;
  // TODO(CF-218): Handle file name for the VMO case here.
  if (location_.inspect_path_components.empty() && suffix.empty()) {
    return ret;
  }
  ret.append("#");
  bool has_inspect_path = !location_.inspect_path_components.empty();
  if (has_inspect_path) {
    ret += fxl::JoinStrings(location_.inspect_path_components, "/");
  }
  if (!suffix.empty()) {
    if (has_inspect_path) {
      ret += "/";
    }
    ret += fxl::JoinStrings(suffix, "/");
  }

  return ret;
}

void ObjectSource::VisitObjectsInHierarchyRecursively(
    const Visitor& visitor, const inspect::ObjectHierarchy& current,
    std::vector<std::string>* path) const {
  visitor(*path, current);

  for (const auto& child : current.children()) {
    path->push_back(child.object().name);
    VisitObjectsInHierarchyRecursively(visitor, child, path);
    path->pop_back();
  }
}

void ObjectSource::VisitObjectsInHierarchy(Visitor visitor) const {
  std::vector<std::string> path;
  VisitObjectsInHierarchyRecursively(visitor, GetRootHierarchy(), &path);
}

// RunCat ----------------------------------------------------------------------

fit::promise<std::vector<ObjectSource>> RunCat(const Options* options) {
  std::vector<fit::promise<ObjectSource>> promises;
  for (const auto& path : options->paths) {
    FXL_VLOG(1) << fxl::Substitute("Running cat in $0", path);

    auto location_result = ParseToLocation(path);
    if (!location_result.is_ok()) {
      FXL_LOG(ERROR) << path << " not found";
      continue;
    }

    auto location = location_result.take_value();
    auto handle = OpenInspectAtPath(
        files::JoinPath(location.directory_path, location.file_name));

    if (!handle.is_ok()) {
      FXL_LOG(ERROR) << "Failed to open " << path;
      continue;
    }

    promises.emplace_back(ObjectSource::Make(
        std::move(location), inspect::ObjectReader(handle.take_value()),
        options->recursive ? -1 : 0));
  }

  return fit::join_promise_vector(std::move(promises))
      .and_then([](std::vector<fit::result<ObjectSource>>& result) {
        std::vector<ObjectSource> ret;

        for (auto& entry : result) {
          if (entry.is_ok()) {
            ret.emplace_back(entry.take_value());
          }
        }

        return fit::ok(std::move(ret));
      });
}

// RunFind
// ---------------------------------------------------------------------

fit::promise<std::vector<ObjectSource>> RunFind(const Options* options) {
  return fit::make_promise([options] {
           std::vector<fit::promise<ObjectSource>> promises;
           for (const auto& path : options->paths) {
             for (auto& location : SyncFindPaths(path)) {
               auto handle = OpenInspectAtPath(files::JoinPath(
                   location.directory_path, location.file_name));

               if (!handle.is_ok()) {
                 continue;
               }

               promises.emplace_back(ObjectSource::Make(
                   std::move(location),
                   inspect::ObjectReader(handle.take_value()),
                   options->recursive ? -1 : 0 /* depth */));
             }
           }
           return fit::join_promise_vector(std::move(promises));
         })
      .and_then([](std::vector<fit::result<ObjectSource>>& entry_points) {
        std::vector<ObjectSource> ret;
        for (auto& entry : entry_points) {
          if (entry.is_ok()) {
            ret.push_back(entry.take_value());
          }
        }

        return fit::ok(std::move(ret));
      });
}

// RunLs
// -----------------------------------------------------------------------

fit::promise<std::vector<ObjectSource>> RunLs(const Options* options) {
  std::vector<fit::promise<ObjectSource>> promises;
  for (const auto& path : options->paths) {
    FXL_VLOG(1) << fxl::Substitute("Running ls in $0", path);

    auto location_result = ParseToLocation(path);

    if (!location_result.is_ok()) {
      FXL_LOG(ERROR) << path << " not found";
    }

    auto location = location_result.take_value();
    auto handle = OpenInspectAtPath(
        files::JoinPath(location.directory_path, location.file_name));

    if (!handle.is_ok()) {
      FXL_LOG(ERROR) << "Failed to open " << path;
      continue;
    }

    promises.emplace_back(ObjectSource::Make(
        std::move(location), inspect::ObjectReader(handle.take_value()),
        1 /* depth */));
  }

  return fit::join_promise_vector(std::move(promises))
      .and_then([](std::vector<fit::result<ObjectSource>>& result) {
        std::vector<ObjectSource> ret;

        for (auto& entry : result) {
          if (entry.is_ok()) {
            ret.emplace_back(entry.take_value());
          }
        }

        return fit::ok(std::move(ret));
      });
}

}  // namespace iquery
