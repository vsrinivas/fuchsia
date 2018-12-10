// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>

#include <stack>

#include <lib/fit/defer.h>
#include <lib/fxl/strings/concatenate.h>
#include <lib/fxl/strings/join_strings.h>
#include <lib/fxl/strings/string_printf.h>
#include <lib/fxl/strings/substitute.h>

#include "garnet/bin/iquery/connect.h"
#include "garnet/bin/iquery/modes.h"

#include <iostream>

namespace iquery {

// RunCat ----------------------------------------------------------------------

namespace {

// Joins the basepath and the relative path together.
std::string GetCurrentPath(const std::string& basepath,
                           const std::vector<std::string>& rel_path) {
  if (rel_path.empty())
    return basepath;
  return fxl::Concatenate({basepath, "/", fxl::JoinStrings(rel_path, "/")});
}

bool RecursiveRunCat(const fuchsia::inspect::InspectSyncPtr& channel_ptr,
                     ObjectNode* current_node, const std::string& basepath,
                     std::vector<std::string>* rel_path) {
  std::string current_path = GetCurrentPath(basepath, *rel_path);
  FXL_VLOG(1) << fxl::Substitute("Finding in $0", current_path);

  // We check one level.
  FXL_VLOG(1) << "  attempting to list children";
  fidl::VectorPtr<fidl::StringPtr> children;
  auto status = channel_ptr->ListChildren(&children);
  if (status != ZX_OK) {
    FXL_LOG(WARNING) << "Failed listing children for " << current_path;
    return false;
  }

  FXL_VLOG(1) << "  successfully listed children";
  current_node->children.reserve(children->size());
  for (const std::string& child_name : *children) {
    FXL_VLOG(1) << "  attempting to open " << child_name;
    bool success;
    fuchsia::inspect::InspectSyncPtr child_channel;
    channel_ptr->OpenChild(child_name, child_channel.NewRequest(), &success);
    if (!success) {
      FXL_LOG(WARNING) << "Could not open child for " << current_path << "/"
                       << child_name;
      continue;
    }
    FXL_VLOG(1) << "    successfully opened";
    FXL_VLOG(1) << "    reading data";

    // Fill out the data.
    ObjectNode child_node;
    child_channel->ReadData(&child_node.object);
    child_node.basepath = fxl::Concatenate({current_path, "/", child_name});

    FXL_VLOG(1) << "    recursing down";
    // We create the relative path stack.
    rel_path->push_back(child_name);
    RecursiveRunCat(child_channel, &child_node, basepath, rel_path);
    rel_path->pop_back();

    // Add it to the tree.
    current_node->children.emplace_back(std::move(child_node));
  }

  return true;
}

}  // namespace

bool RunCat(const Options& options, std::vector<ObjectNode>* out) {
  for (const auto& path : options.paths) {
    FXL_VLOG(1) << fxl::Substitute("Running cat in $0", path);
    // Get the root. The rest of tree will be obtained through ListChildren.
    FXL_VLOG(1) << "  opening a connection";
    Connection connection(path);
    auto channel_ptr = connection.SyncOpen();
    if (!channel_ptr) {
      FXL_LOG(ERROR) << "Failed opening " << path;
      continue;
    }

    // We open the first node outside the recursion in case there is no need to
    // step down for children.
    FXL_VLOG(1) << "  reading root node";
    ObjectNode root;
    root.basepath = path;
    auto status = channel_ptr->ReadData(&(root.object));
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed reading " << path;
      continue;
    }

    if (options.recursive) {
      FXL_VLOG(1) << "  recursing for " << path;
      std::vector<std::string> path_stack;
      RecursiveRunCat(channel_ptr, &root, path, &path_stack);
    }

    out->push_back(std::move(root));
  }

  return true;
}

// RunFind ---------------------------------------------------------------------

namespace {

// Depth-first search for children within the Inspect API, utilizing
// ListChildren and OpenChild on the API for traversal.
void ListFromInspectHierarchy(std::string in_path,
                              fuchsia::inspect::InspectSyncPtr in_root,
                              std::vector<ObjectNode>* out, bool recurse) {
  FXL_VLOG(1) << fxl::Substitute("Opened Inspect hierarchy at $0", in_path);

  if (!recurse) {
    FXL_VLOG(1) << "  processing " << in_path;
    auto it = out->emplace(out->end());
    in_root->ReadData(&(it->object));
    it->basepath = in_path;
  } else {
    std::stack<std::pair<std::string, fuchsia::inspect::InspectSyncPtr>> search;
    search.push(std::make_pair(std::move(in_path), std::move(in_root)));

    while (!search.empty()) {
      auto pair = std::move(search.top());
      search.pop();
      FXL_VLOG(1) << "  processing " << pair.first;
      auto it = out->emplace(out->end());
      pair.second->ReadData(&(it->object));
      it->basepath = pair.first;

      // Continue a DFS search of children underneath the root if recurse is
      // true.
      fidl::VectorPtr<fidl::StringPtr> children;
      pair.second->ListChildren(&children);

      for (const auto& child : *children) {
        fuchsia::inspect::InspectSyncPtr ptr;
        bool opened;
        pair.second->OpenChild(child, ptr.NewRequest(), &opened);
        if (opened) {
          FXL_VLOG(1) << "  opened child " << child;
          search.push(
              std::make_pair(fxl::Concatenate({pair.first, "/", child.get()}),
                             std::move(ptr)));
        } else {
          FXL_VLOG(1) << "  failed to open child " << child;
        }
      }
    }
  }

  FXL_VLOG(1) << "Done reading inspect hierarchy";
}

// Makes a DFS search for candidate channels under the |base_directory|.
// If |recursive| is not set, it will stop the decent of a particular branch
// upon finding a valid channel. When it set it will search the whole tree.
// This is used for being able to chain the results of the non-recursive result
// of find with a new call of iquery with cat.
bool FindObjects(const std::string& base_directory, bool recursive,
                 std::vector<ObjectNode>* out) {
  assert(out);

  std::vector<std::string> candidates;
  std::stack<std::string> search;
  search.emplace(base_directory);

  while (search.size() > 0) {
    std::string path = std::move(search.top());
    search.pop();

    FXL_VLOG(1) << fxl::Substitute("Finding in $0", path);

    auto* dir = opendir(path.c_str());
    auto cleanup = fit::defer([dir] {
      if (dir != nullptr) {
        closedir(dir);
      }
    });

    if (dir == nullptr) {
      FXL_LOG(WARNING) << fxl::StringPrintf("Could not open %s (errno=%d)",
                                            path.c_str(), errno);
      continue;
    }

    // By default we continue to search. If we find a good candidate, we need
    // to define then if we're going to continue recursing.
    std::vector<std::string> current_level_dirs;
    while (auto* dirent = readdir(dir)) {
      FXL_VLOG(1) << "  checking " << dirent->d_name;
      if (strcmp(".", dirent->d_name) == 0) {
        FXL_VLOG(1) << "  skipping";
        continue;
      }

      // We check all the possible directories in this level.
      // If recursive was not set as an option, we must stop at any level where
      // we find a suitable candidate.
      if (dirent->d_type == DT_DIR) {
        // Another candidate.
        FXL_VLOG(1) << fxl::Substitute("  will queue", path);
        current_level_dirs.emplace_back(
            fxl::Concatenate({path, "/", dirent->d_name}));
      } else if (strcmp(".inspect", dirent->d_name) == 0) {
        // We found a candidate, we check if it's a valid one.
        FXL_VLOG(1) << fxl::Substitute("  is a candidate path", path);
        Connection c(path);
        if (c.Validate()) {
          // This is valid candidate, so we try to open it.
          auto ptr = c.SyncOpen();
          if (ptr) {
            FXL_VLOG(1) << "  accepted";

            ListFromInspectHierarchy(path, std::move(ptr), out, recursive);
          } else {
            FXL_LOG(WARNING) << "Could not open "
                             << fxl::Concatenate({path, "/", dirent->d_name});
          }
        }
      }
    }

    // Now that we checked all the candidates within this directory, we
    // continue the recursion through directories.
    FXL_VLOG(1) << fxl::Substitute("Recursing from $0", path);
    for (const auto& child_dir : current_level_dirs) {
      search.emplace(std::move(child_dir));
    }
  }

  return true;
}

}  // namespace

bool RunFind(const Options& options, std::vector<ObjectNode>* out) {
  for (const auto& path : options.paths) {
    if (!FindObjects(path, options.recursive, out)) {
      FXL_LOG(WARNING) << "Failed searching " << path;
    }
  }

  return true;
}

// RunLs -----------------------------------------------------------------------

bool RunLs(const Options& options, std::vector<ObjectNode>* out) {
  for (const auto& path : options.paths) {
    FXL_VLOG(1) << fxl::Substitute("Running ls in $0", path);
    iquery::Connection connection(path);
    auto ptr = connection.SyncOpen();
    if (!ptr) {
      FXL_LOG(WARNING) << "Failed listing " << path;
      return 1;
    }

    FXL_VLOG(1) << "  listing children";

    ::fidl::VectorPtr<fidl::StringPtr> result;
    auto status = ptr->ListChildren(&result);
    if (status != ZX_OK) {
      FXL_LOG(WARNING) << "Failed listing children for " << path;
      return false;
    }

    for (const std::string& child_name : *result) {
      ObjectNode child_node;
      child_node.object.name = child_name;
      child_node.basepath = fxl::Concatenate({path, "/", child_name});
      out->emplace_back(std::move(child_node));
    }
  }

  return true;
}

}  // namespace iquery
