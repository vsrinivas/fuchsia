// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/iquery/modes.h"

#include <dirent.h>
#include <fcntl.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fit/bridge.h>
#include <lib/fit/defer.h>
#include <lib/inspect/query/discover.h>
#include <lib/inspect/query/read.h>
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

namespace iquery {

// RunCat ----------------------------------------------------------------------

fit::promise<std::vector<inspect::Source>> RunCat(const Options* options) {
  std::vector<fit::promise<inspect::Source, std::string>> promises;
  for (const auto& path : options->paths) {
    FXL_VLOG(1) << fxl::Substitute("Running cat in $0", path);

    auto location_result = inspect::Location::Parse(path);
    if (!location_result.is_ok()) {
      FXL_LOG(ERROR) << path << " not found";
      continue;
    }

    promises.emplace_back(inspect::ReadLocation(location_result.take_value(),
                                                options->depth()));
  }

  return fit::join_promise_vector(std::move(promises))
      .and_then(
          [](std::vector<fit::result<inspect::Source, std::string>>& result) {
            std::vector<inspect::Source> ret;

            for (auto& entry : result) {
              if (entry.is_ok()) {
                ret.emplace_back(entry.take_value());
              } else {
                FXL_LOG(ERROR) << entry.error();
              }
            }

            return fit::ok(std::move(ret));
          });
}

// RunFind
// ---------------------------------------------------------------------

fit::promise<std::vector<inspect::Source>> RunFind(const Options* options) {
  return fit::make_promise([options] {
           std::vector<fit::promise<inspect::Source, std::string>> promises;
           for (const auto& path : options->paths) {
             for (auto& location : inspect::SyncFindPaths(path)) {
               ((void)location);
               promises.emplace_back(inspect::ReadLocation(
                   std::move(location),
                   options->depth()));
             }
           }
           return fit::join_promise_vector(std::move(promises));
         })
      .and_then([](std::vector<fit::result<inspect::Source, std::string>>&
                       entry_points) {
        std::vector<inspect::Source> ret;
        for (auto& entry : entry_points) {
          if (entry.is_ok()) {
            ret.push_back(entry.take_value());
          } else {
            FXL_LOG(ERROR) << entry.error();
          }
        }

        return fit::ok(std::move(ret));
      });
}

// RunLs
// -----------------------------------------------------------------------

fit::promise<std::vector<inspect::Source>> RunLs(const Options* options) {
  std::vector<fit::promise<inspect::Source, std::string>> promises;
  for (const auto& path : options->paths) {
    FXL_VLOG(1) << fxl::Substitute("Running ls in $0", path);

    auto location_result = inspect::Location::Parse(path);

    if (!location_result.is_ok()) {
      FXL_LOG(ERROR) << path << " not valid";
    }

    promises.emplace_back(
        inspect::ReadLocation(location_result.take_value(), /*depth=*/1));
  }

  return fit::join_promise_vector(std::move(promises))
      .and_then(
          [](std::vector<fit::result<inspect::Source, std::string>>& result) {
            std::vector<inspect::Source> ret;

            for (auto& entry : result) {
              if (entry.is_ok()) {
                ret.emplace_back(entry.take_value());
              } else {
                FXL_LOG(ERROR) << entry.error();
              }
            }

            return fit::ok(std::move(ret));
          });
}

}  // namespace iquery
