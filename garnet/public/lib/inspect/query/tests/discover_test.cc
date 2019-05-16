// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/io/cpp/fidl.h>
#include <lib/fdio/namespace.h>
#include <lib/inspect/query/discover.h>
#include <lib/vfs/cpp/pseudo_dir.h>
#include <lib/vfs/cpp/pseudo_file.h>

#include "fixture.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "lib/inspect/query/location.h"
#include "src/lib/files/glob.h"

using inspect::Location;
using ::testing::UnorderedElementsAre;

namespace {

std::unique_ptr<vfs::PseudoFile> MakePseudoFile() {
  return std::make_unique<vfs::PseudoFile>(
      1024,  // arbitrary number. this test should not require a larger file.
      [](std::vector<uint8_t>* unused, size_t unused_size) { return ZX_OK; });
}

class DiscoverTest : public TestFixture {
 public:
  DiscoverTest() {
    root_dir_ = std::make_unique<vfs::PseudoDir>();
    {
      auto hub = std::make_unique<vfs::PseudoDir>();
      hub->AddEntry("root.inspect", MakePseudoFile());
      hub->AddEntry("test.inspect", MakePseudoFile());

      {
        auto nest = std::make_unique<vfs::PseudoDir>();
        nest->AddEntry("fuchsia.inspect.Inspect", MakePseudoFile());

        hub->AddEntry("nest", std::move(nest));
      }

      root_dir_->AddEntry("hub", std::move(hub));
    }
    {
      auto other = std::make_unique<vfs::PseudoDir>();
      other->AddEntry("fuchsia.inspect.Inspect", MakePseudoFile());
      other->AddEntry("root.inspect", MakePseudoFile());

      root_dir_->AddEntry("other", std::move(other));
    }

    ZX_ASSERT(fdio_ns_get_installed(&ns_) == ZX_OK);

    fuchsia::io::DirectoryPtr ptr;
    root_dir_->Serve(fuchsia::io::OPEN_RIGHT_READABLE,
                     ptr.NewRequest().TakeChannel());
    ZX_ASSERT(fdio_ns_bind(ns_, "/test",
                           ptr.Unbind().TakeChannel().release()) == ZX_OK);
  }

  ~DiscoverTest() { ZX_ASSERT(fdio_ns_unbind(ns_, "/test") == ZX_OK); }

 private:
  std::unique_ptr<vfs::PseudoDir> root_dir_;
  fdio_ns_t* ns_ = nullptr;
};

TEST_F(DiscoverTest, SyncFindPaths) {
  // Run the search on the promise thread so it doesn't deadlock with the
  // server.

  std::vector<inspect::Location> locations;
  SchedulePromise(
      fit::make_promise([&] { locations = inspect::SyncFindPaths("/"); }));

  RunLoopWithTimeoutOrUntil([&] { return !locations.empty(); });

  EXPECT_THAT(locations, ::testing::UnorderedElementsAre(
                             Location{
                                 .directory_path = "/test/other",
                                 .file_name = "fuchsia.inspect.Inspect",
                                 .type = Location::Type::INSPECT_FIDL,
                                 .inspect_path_components = {},
                             },
                             Location{
                                 .directory_path = "/test/other",
                                 .file_name = "root.inspect",
                                 .type = Location::Type::INSPECT_VMO,
                                 .inspect_path_components = {},
                             },
                             Location{
                                 .directory_path = "/test/hub",
                                 .file_name = "root.inspect",
                                 .type = Location::Type::INSPECT_VMO,
                                 .inspect_path_components = {},
                             },
                             Location{
                                 .directory_path = "/test/hub",
                                 .file_name = "test.inspect",
                                 .type = Location::Type::INSPECT_VMO,
                                 .inspect_path_components = {},
                             },
                             Location{
                                 .directory_path = "/test/hub/nest",
                                 .file_name = "fuchsia.inspect.Inspect",
                                 .type = Location::Type::INSPECT_FIDL,
                                 .inspect_path_components = {},
                             }));
};

TEST_F(DiscoverTest, SyncFindNestedPath) {
  std::vector<inspect::Location> locations1, locations2;
  SchedulePromise(fit::make_promise(
      [&] { locations1 = inspect::SyncFindPaths("/test/hub#child/a"); }));
  SchedulePromise(fit::make_promise([&] {
    locations2 = inspect::SyncFindPaths("/test/hub/root.inspect#child/a");
  }));

  RunLoopWithTimeoutOrUntil(
      [&] { return !locations1.empty() && !locations2.empty(); });

  EXPECT_FALSE(locations1.empty());
  EXPECT_FALSE(locations2.empty());

  EXPECT_THAT(locations1, ::testing::UnorderedElementsAre(Location{
                              .directory_path = "/test/hub",
                              .file_name = "fuchsia.inspect.Inspect",
                              .type = Location::Type::INSPECT_FIDL,
                              .inspect_path_components = {"child", "a"},
                          }));

  EXPECT_THAT(locations2, ::testing::UnorderedElementsAre(Location{
                              .directory_path = "/test/hub",
                              .file_name = "root.inspect",
                              .type = Location::Type::INSPECT_VMO,
                              .inspect_path_components = {"child", "a"},
                          }));
};

TEST_F(DiscoverTest, SyncFindGlobs) {
  // Run the search on the promise thread so it doesn't deadlock with the
  // server.

  std::vector<inspect::Location> locations;
  SchedulePromise(fit::make_promise([&] {
    locations =
        inspect::SyncSearchGlobs({"/*/hub/*", "/test/*", "/test/hub/*/*"});
  }));

  RunLoopWithTimeoutOrUntil([&] { return !locations.empty(); });

  EXPECT_FALSE(locations.empty());

  EXPECT_THAT(locations, ::testing::UnorderedElementsAre(
                             Location{
                                 .directory_path = "/test/hub",
                                 .file_name = "root.inspect",
                                 .type = Location::Type::INSPECT_VMO,
                                 .inspect_path_components = {},
                             },
                             Location{
                                 .directory_path = "/test/hub",
                                 .file_name = "test.inspect",
                                 .type = Location::Type::INSPECT_VMO,
                                 .inspect_path_components = {},
                             },
                             Location{
                                 .directory_path = "/test/hub/nest",
                                 .file_name = "fuchsia.inspect.Inspect",
                                 .type = Location::Type::INSPECT_FIDL,
                                 .inspect_path_components = {},
                             }));
};

}  // namespace
