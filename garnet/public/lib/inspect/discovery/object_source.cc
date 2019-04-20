// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/public/lib/inspect/discovery/object_source.h"

#include <dirent.h>
#include <fcntl.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fit/bridge.h>
#include <lib/fit/defer.h>
#include <lib/inspect/reader.h>
#include <src/lib/fxl/strings/concatenate.h>
#include <src/lib/fxl/strings/join_strings.h>
#include <src/lib/fxl/strings/split_string.h>
#include <src/lib/fxl/strings/string_printf.h>
#include <src/lib/fxl/strings/substitute.h>
#include <sys/stat.h>

#include <iostream>
#include <regex>
#include <stack>
#include <thread>

#include "src/lib/files/file.h"
#include "src/lib/files/path.h"

using namespace std::chrono_literals;

namespace inspect {
namespace {

constexpr int kPingPeriodMs = 50;

// This class is a workaround for ZX-3284, which sometimes causes processes to
// hang when resuming from a suspend. It works by periodically poking at a
// service hosted by the formerly suspended process to unstick it.
// TODO(ZX-3284) Remove this hack.
class FilePinger {
 public:
  FilePinger(const std::string& path) : path_(path), done_(false) {
    thread_ = std::thread([this] {
      int spawned = 0;
      while (true) {
        {
          std::unique_lock<std::mutex> lock(mutex_);
          cond_.wait_for(lock, kPingPeriodMs * 1ms);
          if (done_) {
            return;
          }
        }

        // If we get here, the caller did not cancel the thread in time. This
        // was probably caused by the caller getting stuck, so spawn a new
        // thread that just tries to open the wrapped path. In the event that
        // that event is stuck as well, continue spawning threads up to a limit.
        // This is very hacky, but experimental results show that it fixes the
        // hang for the time being.
        FXL_VLOG(1) << "BUG: File ping triggered " << spawned;
        // Ping the path.
        if (spawned++ < 10) {
          std::thread([path = path_] { files::IsFile(path); }).detach();
        } else {
          FXL_LOG(ERROR) << "BUG: File ping triggered at limit";
          return;
        }
      }
    });
  }
  ~FilePinger() {
    {
      std::unique_lock<std::mutex> lock(mutex_);
      done_ = true;
      cond_.notify_all();
    }
    thread_.join();
  }

 private:
  const std::string path_;
  std::mutex mutex_;
  std::condition_variable cond_;
  bool done_;
  std::thread thread_;
};

// Creates a regex object for matching file names with the Inspect VMO format
// extension.
std::regex inspect_vmo_file_regex() { return std::regex("\\.inspect$"); }

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

std::string ObjectLocation::RelativeFilePath() const {
  return files::JoinPath(directory_path, file_name);
}

std::string ObjectLocation::AbsoluteFilePath() const {
  return files::AbsolutePath(RelativeFilePath());
}

std::string ObjectLocation::SimplifiedFilePath() const {
  if (type == Type::INSPECT_FIDL) {
    return directory_path;
  }
  return RelativeFilePath();
}

std::string ObjectLocation::ObjectPath(
    const std::vector<std::string>& suffix) const {
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
        return inspect::ReadFromFidl(reader.take_value(), depth);
      })
      .then([root_reader, location = std::move(location)](
                fit::result<inspect::ObjectHierarchy>& result)
                -> fit::result<ObjectSource> {
        if (!result.is_ok()) {
          FXL_LOG(ERROR) << fxl::Substitute("Failed to read $0",
                                            location.ObjectPath());
          return fit::error();
        }

        ObjectSource ret;
        ret.location_ = std::move(location);
        ret.hierarchy_ = result.take_value();
        return fit::ok(std::move(ret));
      });
}

fit::promise<ObjectSource> ObjectSource::Make(ObjectLocation root_location,
                                              fuchsia::io::FilePtr file_ptr,
                                              int depth) {
  fit::bridge<fuchsia::io::NodeInfo> vmo_read_bridge;
  file_ptr->Describe(vmo_read_bridge.completer.bind());

  return vmo_read_bridge.consumer.promise_or(fit::error())
      .or_else([failed_path = root_location.RelativeFilePath()]()
                   -> fit::result<fuchsia::io::NodeInfo> {
        FXL_LOG(ERROR) << "Failed to describe file at " << failed_path;
        return fit::error();
      })
      .and_then([depth, file_ptr = std::move(file_ptr),
                 root_location = std::move(root_location)](
                    fuchsia::io::NodeInfo& info) -> fit::result<ObjectSource> {
        if (!info.is_vmofile()) {
          FXL_LOG(WARNING) << "File is not actually a vmofile";
          return fit::error();
        }

        auto read_result = inspect::ReadFromVmo(std::move(info.vmofile().vmo));
        if (!read_result.is_ok()) {
          FXL_LOG(ERROR) << "Failure reading the VMO";
          return fit::error();
        }

        auto hierarchy_root = read_result.take_value();

        auto* hierarchy = &hierarchy_root;

        // Navigate within the hierarchy to the correct location.
        for (const auto& path_component :
             root_location.inspect_path_components) {
          auto child = std::find_if(
              hierarchy->children().begin(), hierarchy->children().end(),
              [&path_component](inspect::ObjectHierarchy& obj) {
                return obj.node().name() == path_component;
              });
          if (child == hierarchy->children().end()) {
            FXL_LOG(ERROR) << "Could not find child named " << path_component;
            return fit::error();
          }
          hierarchy = &(*child);
        }

        if (depth >= 0) {
          // If we have a specific depth requirement, prune the hierarchy tree
          // to the requested depth. Reading the VMO is all or nothing, so we
          // require post-processing to implement specific depth cutoffs.

          // Stack of ObjectHierarchies along with an associated depth.
          // Hierarchies at the max depth will have their children pruned, while
          // hierarchies at a lower depth simply push their children onto the
          // stack.
          std::stack<std::pair<inspect::ObjectHierarchy*, int>>
              object_depth_stack;

          object_depth_stack.push(std::make_pair(hierarchy, 0));
          while (object_depth_stack.size() > 0) {
            auto pair = object_depth_stack.top();
            object_depth_stack.pop();

            if (pair.second == depth) {
              pair.first->children().clear();
            } else {
              for (auto& child : pair.first->children()) {
                object_depth_stack.push(
                    std::make_pair(&child, pair.second + 1));
              }
            }
          }
        }

        ObjectSource ret;
        ret.location_ = std::move(root_location);
        ret.hierarchy_ = std::move(*hierarchy);
        return fit::ok(std::move(ret));
      });
}

std::string ObjectSource::FormatRelativePath(
    const std::vector<std::string>& suffix) const {
  return location_.ObjectPath(suffix);
}

void ObjectSource::SortHierarchy() {
  std::stack<inspect::ObjectHierarchy*> hierarchies_to_sort;
  hierarchies_to_sort.push(&hierarchy_);
  while (hierarchies_to_sort.size() > 0) {
    auto* hierarchy = hierarchies_to_sort.top();
    hierarchies_to_sort.pop();
    hierarchy->Sort();
    for (auto& child : hierarchy->children()) {
      hierarchies_to_sort.push(&child);
    }
  }
}

void ObjectSource::VisitObjectsInHierarchyRecursively(
    const Visitor& visitor, const inspect::ObjectHierarchy& current,
    std::vector<std::string>* path) const {
  visitor(*path, current);

  for (const auto& child : current.children()) {
    path->push_back(child.node().name());
    VisitObjectsInHierarchyRecursively(visitor, child, path);
    path->pop_back();
  }
}

void ObjectSource::VisitObjectsInHierarchy(Visitor visitor) const {
  std::vector<std::string> path;
  VisitObjectsInHierarchyRecursively(visitor, GetRootHierarchy(), &path);
}

// Convert an ObjectLocation into a promise for an ObjectSource loading
// Inspect data from that location.
fit::promise<ObjectSource> MakeObjectPromiseFromLocation(
    ObjectLocation location, int depth) {
  if (location.type == ObjectLocation::Type::INSPECT_FIDL) {
    auto handle = OpenInspectAtPath(location.AbsoluteFilePath());

    if (handle.is_ok()) {
      return ObjectSource::Make(std::move(location),
                                inspect::ObjectReader(handle.take_value()),
                                depth);
    }
  } else if (location.type == ObjectLocation::Type::INSPECT_VMO) {
    fuchsia::io::FilePtr file_ptr;
    zx_status_t status = fdio_open(
        location.AbsoluteFilePath().c_str(), fuchsia::io::OPEN_RIGHT_READABLE,
        file_ptr.NewRequest().TakeChannel().release());
    if (status != ZX_OK || !file_ptr.is_bound()) {
      FXL_LOG(WARNING) << "Failed to fdio_open and bind "
                       << location.AbsoluteFilePath() << " " << status;
      return fit::make_result_promise<ObjectSource>(fit::error());
    }
    return ObjectSource::Make(std::move(location), std::move(file_ptr), depth);
  } else {
    FXL_LOG(ERROR) << "Unknown location type "
                   << static_cast<int>(location.type);
  }

  FXL_LOG(ERROR) << "Failed to open " << location.AbsoluteFilePath();
  return fit::make_result_promise<ObjectSource>(fit::error());
}

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

  if (std::regex_search(parts[0], inspect_vmo_file_regex())) {
    // The file seems to be an inspect VMO.
    FXL_VLOG(1) << "File " << parts[0] << " seems to be an inspect VMO";
    return fit::ok(
        ObjectLocation{.type = ObjectLocation::Type::INSPECT_VMO,
                       .directory_path = files::GetDirectoryName(parts[0]),
                       .file_name = files::GetBaseName(parts[0]),
                       .inspect_path_components = std::move(inspect_parts)});
  } else if (files::GetBaseName(parts[0]) == fuchsia::inspect::Inspect::Name_) {
    // The file seems to be an inspect FIDL interface.
    FXL_VLOG(1) << "File " << parts[0]
                << " seems to be an inspect FIDL endpoint";
    return fit::ok(
        ObjectLocation{.directory_path = files::GetDirectoryName(parts[0]),
                       .file_name = files::GetBaseName(parts[0]),
                       .inspect_path_components = std::move(inspect_parts)});
  } else {
    // Default to treating the path as a directory, and look for the FIDL
    // interface inside.
    FXL_VLOG(1) << "Treating " << parts[0] << " as an objects directory";
    return fit::ok(
        ObjectLocation{.directory_path = parts[0],
                       .file_name = fuchsia::inspect::Inspect::Name_,
                       .inspect_path_components = std::move(inspect_parts)});
  }
}

// Synchronously recurse down the filesystem from the given path to find
// inspect endpoints.
std::vector<ObjectLocation> SyncFindPaths(const std::string& path) {
  FXL_VLOG(1) << "Synchronously listing paths under " << path;
  if (path.find("#") != std::string::npos) {
    // This path refers to something nested inside an inspect hierarchy,
    // return it directly.
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

    FilePinger fp(path);

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
        FXL_VLOG(1) << "  Adding child " << de->d_name;
        search_paths.push_back(fxl::Concatenate({path, "/", de->d_name}));
      } else {
        if (strcmp(de->d_name, fuchsia::inspect::Inspect::Name_) == 0) {
          FXL_VLOG(1) << "  Found fuchsia.inspect.Inspect at "
                      << files::JoinPath(path, de->d_name);
          ret.push_back(
              ObjectLocation{.type = ObjectLocation::Type::INSPECT_FIDL,
                             .directory_path = path,
                             .file_name = de->d_name,
                             .inspect_path_components = {}});
        } else if (std::regex_search(de->d_name, inspect_vmo_file_regex())) {
          FXL_VLOG(1) << "  Found Inspect VMO at "
                      << files::JoinPath(path, de->d_name);
          ret.push_back(
              ObjectLocation{.type = ObjectLocation::Type::INSPECT_VMO,
                             .directory_path = path,
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

}  // namespace inspect
