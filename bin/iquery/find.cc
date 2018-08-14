// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "find.h"

#include <stack>

#include <dirent.h>
#include <sys/types.h>

#include <lib/fxl/functional/auto_call.h>
#include <lib/fxl/strings/concatenate.h>
#include <lib/fxl/strings/string_printf.h>
#include <lib/fxl/strings/substitute.h>

#include "connect.h"

namespace iquery {

bool FindObjects(const std::string& base_directory,
                 std::vector<std::string>* out_results) {
  assert(out_results);

  std::vector<std::string> candidates;
  std::stack<std::string> search;
  search.emplace(base_directory);

  while (search.size() > 0) {
    std::string path = std::move(search.top());
    search.pop();

    FXL_VLOG(1) << fxl::Substitute("Finding in $0", path);

    auto* dir = opendir(path.c_str());
    auto cleanup = fxl::MakeAutoCall([dir] {
      if (dir != nullptr) {
        closedir(dir);
      }
    });

    if (dir == nullptr) {
      FXL_LOG(WARNING) << fxl::StringPrintf("Could not open %s (errno=%d)",
                                            path.c_str(), errno);
      continue;
    }

    while (auto* dirent = readdir(dir)) {
      FXL_VLOG(1) << "  checking " << dirent->d_name;
      if (strcmp(".", dirent->d_name) == 0) {
        FXL_VLOG(1) << "  skipping";
        continue;
      }
      if (dirent->d_type == DT_DIR) {
        // Another directory, recurse.
        FXL_VLOG(1) << "  will queue " << dirent->d_name;
        search.emplace(fxl::Concatenate({path, "/", dirent->d_name}));
      } else if (strcmp(".channel", dirent->d_name) == 0) {
        // This directory has a channel, mark it as a candidate for checking if
        // it is valid.
        FXL_VLOG(1) << fxl::Substitute("$0 is a candidate path", path);
        candidates.emplace_back(path);
      }
    }

    FXL_VLOG(1) << fxl::Substitute("Finished finding in $0", path);
  }

  for (const auto& candidate : candidates) {
    FXL_VLOG(1) << fxl::Substitute("Trying path $0", candidate);
    Connection c(candidate);
    if (c.Validate() && c.SyncOpen()) {
      FXL_VLOG(1) << fxl::Substitute("Accepted candidate $0", candidate);
      out_results->emplace_back(candidate);
    } else {
      FXL_VLOG(1) << fxl::Substitute(
          "Path $0 looks like an object, but is not valid", candidate);
    }
  }

  return true;
}

}  // namespace iquery
