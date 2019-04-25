// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/inspect/query/location.h>
#include <src/lib/files/directory.h>
#include <src/lib/files/path.h>

#include <algorithm>
#include <iterator>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using inspect::Location;

namespace {

std::vector<Location> GetTestLocations() {
  return std::vector<Location>{
      {.type = Location::Type::INSPECT_FIDL,
       .directory_path = "./file/path",
       .file_name = "fuchsia.inspect.Inspect",
       .inspect_path_components = {"child", "a"}},
      {.type = Location::Type::INSPECT_VMO,
       .directory_path = "./file/path2",
       .file_name = "root.inspect",
       .inspect_path_components = {"child", "a"}},
      {.type = Location::Type::INSPECT_FIDL,
       .directory_path = "/hub/path",
       .file_name = "fuchsia.inspect.Inspect"},
      {.type = Location::Type::INSPECT_VMO,
       .directory_path = "/hub/path",
       .file_name = "root.inspect"},
  };
}

TEST(Location, RelativePaths) {
  auto locations = GetTestLocations();

  std::vector<std::string> expected = {
      "./file/path/fuchsia.inspect.Inspect",
      "./file/path2/root.inspect",
      "/hub/path/fuchsia.inspect.Inspect",
      "/hub/path/root.inspect",
  };

  std::vector<std::string> location_paths;
  std::for_each(locations.begin(), locations.end(),
                [&](const inspect::Location& location) {
                  location_paths.push_back(location.RelativeFilePath());
                });

  EXPECT_THAT(expected, ::testing::Pointwise(::testing::Eq(), location_paths));
}

TEST(Location, AbsolutePaths) {
  auto locations = GetTestLocations();

  std::vector<std::string> expected = {
      files::JoinPath(files::GetCurrentDirectory(),
                      "file/path/fuchsia.inspect.Inspect"),
      files::JoinPath(files::GetCurrentDirectory(), "file/path2/root.inspect"),
      "/hub/path/fuchsia.inspect.Inspect",
      "/hub/path/root.inspect",
  };

  std::vector<std::string> location_paths;
  std::for_each(locations.begin(), locations.end(),
                [&](const inspect::Location& location) {
                  location_paths.push_back(location.AbsoluteFilePath());
                });

  EXPECT_THAT(expected, ::testing::Pointwise(::testing::Eq(), location_paths));
}

TEST(Location, SimplifiedFilePaths) {
  auto locations = GetTestLocations();

  std::vector<std::string> expected = {
      "./file/path",
      "./file/path2/root.inspect",
      "/hub/path",
      "/hub/path/root.inspect",
  };

  std::vector<std::string> location_paths;
  std::for_each(locations.begin(), locations.end(),
                [&](const inspect::Location& location) {
                  location_paths.push_back(location.SimplifiedFilePath());
                });

  EXPECT_THAT(expected, ::testing::Pointwise(::testing::Eq(), location_paths));
}

TEST(Location, NodePaths) {
  auto locations = GetTestLocations();

  std::vector<std::string> expected = {
      "./file/path#child/a",
      "./file/path2/root.inspect#child/a",
      "/hub/path",
      "/hub/path/root.inspect",
  };

  std::vector<std::string> expected_suffix = {
      "./file/path#child/a/b/c",
      "./file/path2/root.inspect#child/a/b/c",
      "/hub/path#b/c",
      "/hub/path/root.inspect#b/c",
  };

  std::vector<std::string> location_paths, location_suffix_paths;
  std::for_each(
      locations.begin(), locations.end(),
      [&](const inspect::Location& location) {
        location_paths.push_back(location.NodePath());
        location_suffix_paths.push_back(location.NodePath({"b", "c"}));
      });

  EXPECT_THAT(expected, ::testing::Pointwise(::testing::Eq(), location_paths));
  EXPECT_THAT(expected_suffix,
              ::testing::Pointwise(::testing::Eq(), location_suffix_paths));
}

TEST(Location, Parse) {
  auto expected_locations = GetTestLocations();

  std::vector<std::string> paths1 = {
      "./file/path#child/a",
      "./file/path2/root.inspect#child/a",
      "/hub/path",
      "/hub/path/root.inspect",
  };

  std::vector<std::string> paths2 = {
      "./file/path/fuchsia.inspect.Inspect#child/a",
      "./file/path2/root.inspect#child/a",
      "/hub/path/fuchsia.inspect.Inspect#",
      "/hub/path/root.inspect#",
  };

  std::vector<inspect::Location> locations1, locations2;
  std::for_each(paths1.begin(), paths1.end(), [&](const std::string& path) {
    locations1.emplace_back(Location::Parse(path).take_value());
  });
  std::for_each(paths2.begin(), paths2.end(), [&](const std::string& path) {
    locations2.emplace_back(Location::Parse(path).take_value());
  });

  EXPECT_THAT(expected_locations,
              ::testing::Pointwise(::testing::Eq(), locations1));
  EXPECT_THAT(expected_locations,
              ::testing::Pointwise(::testing::Eq(), locations2));
}

TEST(Location, ParseError) {
  for (const auto& bad_path : std::vector<std::string>{"##", "a#b#c"}) {
    EXPECT_FALSE(Location::Parse(bad_path).is_ok());
  }
}

}  // namespace
