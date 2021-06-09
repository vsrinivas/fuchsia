// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback/migration/shell/directory_migrator_impl.h"

#include <lib/syslog/cpp/macros.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/lib/files/scoped_temp_dir.h"

namespace forensics::feedback::migration_shell {
namespace {

class DirectoryMigratorImplTest : public ::testing::Test {
 protected:
  std::string DataPath() { return data_dir_.path(); }
  std::string CachePath() { return cache_dir_.path(); }

  void CreateDataFile() {
    std::string unused_path;
    FX_CHECK(data_dir_.NewTempFile(&unused_path));
  }

  void CreateCacheFile() {
    std::string unused_path;
    FX_CHECK(cache_dir_.NewTempFile(&unused_path));
  }

 private:
  files::ScopedTempDir data_dir_;
  files::ScopedTempDir cache_dir_;
};

TEST_F(DirectoryMigratorImplTest, ValidDirectories) {
  bool called{false};
  ::fidl::InterfaceHandle<fuchsia::io::Directory> data_dir_handle;
  ::fidl::InterfaceHandle<fuchsia::io::Directory> cache_dir_handle;

  CreateDataFile();
  CreateCacheFile();

  DirectoryMigratorImpl migrator(DataPath(), CachePath());
  migrator.GetDirectories([&](::fidl::InterfaceHandle<fuchsia::io::Directory> data_handle,
                              ::fidl::InterfaceHandle<fuchsia::io::Directory> cache_handle) {
    called = true;
    data_dir_handle = std::move(data_handle);
    cache_dir_handle = std::move(cache_handle);
  });

  ASSERT_TRUE(called);
  EXPECT_TRUE(data_dir_handle.is_valid());
  EXPECT_TRUE(cache_dir_handle.is_valid());

  fuchsia::io::DirectorySyncPtr data_dir;
  data_dir.Bind(std::move(data_dir_handle));
  {
    int32_t s;
    uint32_t flags;

    ASSERT_EQ(data_dir->NodeGetFlags(&s, &flags), ZX_OK);
    EXPECT_EQ(s, ZX_OK);
    ASSERT_EQ(flags, (fuchsia::io::OPEN_RIGHT_READABLE | fuchsia::io::OPEN_RIGHT_WRITABLE));
  }

  fuchsia::io::DirectorySyncPtr cache_dir;
  cache_dir.Bind(std::move(cache_dir_handle));
  {
    int32_t s;
    uint32_t flags;

    ASSERT_EQ(cache_dir->NodeGetFlags(&s, &flags), ZX_OK);
    EXPECT_EQ(s, ZX_OK);
    ASSERT_EQ(flags, (fuchsia::io::OPEN_RIGHT_READABLE | fuchsia::io::OPEN_RIGHT_WRITABLE));
  }
}

TEST_F(DirectoryMigratorImplTest, EmptyDirectories) {
  bool called{false};
  ::fidl::InterfaceHandle<fuchsia::io::Directory> data_dir_handle;
  ::fidl::InterfaceHandle<fuchsia::io::Directory> cache_dir_handle;

  DirectoryMigratorImpl migrator(DataPath(), CachePath());
  migrator.GetDirectories([&](::fidl::InterfaceHandle<fuchsia::io::Directory> data_handle,
                              ::fidl::InterfaceHandle<fuchsia::io::Directory> cache_handle) {
    called = true;
    data_dir_handle = std::move(data_handle);
    cache_dir_handle = std::move(cache_handle);
  });

  ASSERT_TRUE(called);
  EXPECT_TRUE(data_dir_handle.is_valid());
  EXPECT_TRUE(cache_dir_handle.is_valid());
}

TEST_F(DirectoryMigratorImplTest, MissingDirectories) {
  bool called{false};
  ::fidl::InterfaceHandle<fuchsia::io::Directory> data_dir_handle;
  ::fidl::InterfaceHandle<fuchsia::io::Directory> cache_dir_handle;

  DirectoryMigratorImpl migrator("missing-path", "missing-path");
  migrator.GetDirectories([&](::fidl::InterfaceHandle<fuchsia::io::Directory> data_handle,
                              ::fidl::InterfaceHandle<fuchsia::io::Directory> cache_handle) {
    called = true;
    data_dir_handle = std::move(data_handle);
    cache_dir_handle = std::move(cache_handle);
  });

  ASSERT_TRUE(called);
  EXPECT_FALSE(data_dir_handle.is_valid());
  EXPECT_FALSE(cache_dir_handle.is_valid());
}

}  // namespace
}  // namespace forensics::feedback::migration_shell
