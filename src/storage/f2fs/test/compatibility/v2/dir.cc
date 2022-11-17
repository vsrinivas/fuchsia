// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/f2fs/test/compatibility/v2/compatibility.h"

namespace f2fs {
namespace {

using DirCompatibilityTest = GuestTest<F2fsDebianGuest>;

TEST_F(DirCompatibilityTest, DirWidthTestLinuxToFuchsia) {
  // Mkdir on Linux
  // TODO(fxbug.dev/115142): more children for slow test
  constexpr int kDirWidth = 200;
  {
    GetEnclosedGuest().GetLinuxOperator().Mkfs();
    GetEnclosedGuest().GetLinuxOperator().Mount();

    auto umount = fit::defer([&] { GetEnclosedGuest().GetLinuxOperator().Umount(); });

    for (int width = 0; width <= kDirWidth; ++width) {
      std::string dir_name = linux_path_prefix + std::to_string(width);
      GetEnclosedGuest().GetLinuxOperator().Mkdir(dir_name, 0644);
    }
  }

  // Check on Fuchsia
  {
    GetEnclosedGuest().GetFuchsiaOperator().Fsck();
    GetEnclosedGuest().GetFuchsiaOperator().Mount();

    auto umount = fit::defer([&] { GetEnclosedGuest().GetFuchsiaOperator().Umount(); });

    for (int width = 0; width <= kDirWidth; ++width) {
      std::string dir_name = std::to_string(width);
      auto dir =
          GetEnclosedGuest().GetFuchsiaOperator().Open(dir_name, O_RDONLY | O_DIRECTORY, 0644);
      ASSERT_TRUE(dir->IsValid());
    }
  }
}

TEST_F(DirCompatibilityTest, DirWidthTestFuchsiaToLinux) {
  // Mkdir on Fuchsia
  // TODO(fxbug.dev/115142): more children for slow test
  constexpr int kDirWidth = 200;
  {
    GetEnclosedGuest().GetFuchsiaOperator().Mkfs();
    GetEnclosedGuest().GetFuchsiaOperator().Mount();

    auto umount = fit::defer([&] { GetEnclosedGuest().GetFuchsiaOperator().Umount(); });

    for (int width = 0; width <= kDirWidth; ++width) {
      std::string dir_name = std::to_string(width);
      GetEnclosedGuest().GetFuchsiaOperator().Mkdir(dir_name, 0644);
    }
  }

  // Check on Linux
  {
    GetEnclosedGuest().GetLinuxOperator().Fsck();
    GetEnclosedGuest().GetLinuxOperator().Mount();

    auto umount = fit::defer([&] { GetEnclosedGuest().GetLinuxOperator().Umount(); });

    for (int width = 0; width <= kDirWidth; ++width) {
      std::string dir_name = linux_path_prefix + std::to_string(width);
      auto dir = GetEnclosedGuest().GetLinuxOperator().Open(dir_name, O_RDONLY | O_DIRECTORY, 0644);
      ASSERT_TRUE(dir->IsValid());
    }
  }
}

TEST_F(DirCompatibilityTest, DirDepthTestLinuxToFuchsia) {
  constexpr int kDirDepth = 60;

  // Mkdir on Linux
  {
    GetEnclosedGuest().GetLinuxOperator().Mkfs();
    GetEnclosedGuest().GetLinuxOperator().Mount();

    auto umount = fit::defer([&] { GetEnclosedGuest().GetLinuxOperator().Umount(); });

    std::string dir_name = linux_path_prefix;
    for (int depth = 0; depth < kDirDepth; ++depth) {
      dir_name.append("/").append(std::to_string(depth));
      GetEnclosedGuest().GetLinuxOperator().Mkdir(dir_name, 0644);
    }
  }

  // Check on Fuchsia
  {
    GetEnclosedGuest().GetFuchsiaOperator().Fsck();
    GetEnclosedGuest().GetFuchsiaOperator().Mount();

    auto umount = fit::defer([&] { GetEnclosedGuest().GetFuchsiaOperator().Umount(); });

    std::string dir_name;
    for (int depth = 0; depth < kDirDepth; ++depth) {
      dir_name.append("/").append(std::to_string(depth));
      auto file =
          GetEnclosedGuest().GetFuchsiaOperator().Open(dir_name, O_RDONLY | O_DIRECTORY, 0644);
      ASSERT_TRUE(file->IsValid());
    }
  }
}

TEST_F(DirCompatibilityTest, DirDepthTestFuchsiaToLinux) {
  constexpr int kDirDepth = 60;

  // Mkdir on Fuchsia
  {
    GetEnclosedGuest().GetFuchsiaOperator().Mkfs();
    GetEnclosedGuest().GetFuchsiaOperator().Mount();

    auto umount = fit::defer([&] { GetEnclosedGuest().GetFuchsiaOperator().Umount(); });

    std::string dir_name;
    for (int depth = 0; depth < kDirDepth; ++depth) {
      dir_name.append("/").append(std::to_string(depth));
      GetEnclosedGuest().GetFuchsiaOperator().Mkdir(dir_name, 0644);
    }
  }

  // Check on Linux
  {
    GetEnclosedGuest().GetLinuxOperator().Fsck();
    GetEnclosedGuest().GetLinuxOperator().Mount();

    auto umount = fit::defer([&] { GetEnclosedGuest().GetLinuxOperator().Umount(); });
    std::string dir_name = linux_path_prefix;
    for (int depth = 0; depth < kDirDepth; ++depth) {
      dir_name.append("/").append(std::to_string(depth));
      auto dir = GetEnclosedGuest().GetLinuxOperator().Open(dir_name, O_RDONLY | O_DIRECTORY, 0644);
      ASSERT_TRUE(dir->IsValid());
    }
  }
}

TEST_F(DirCompatibilityTest, DirRemoveTestLinuxToFuchsia) {
  std::vector<std::string> dir_paths = {"/d_a", "/d_a/d_b", "/d_c"};

  std::vector<std::string> remove_fail = {"/d_a"};
  std::vector<std::string> remove_success = {"/d_a/d_b", "/d_c"};
  {
    GetEnclosedGuest().GetLinuxOperator().Mkfs();
    GetEnclosedGuest().GetLinuxOperator().Mount();

    auto umount = fit::defer([&] { GetEnclosedGuest().GetLinuxOperator().Umount(); });

    for (auto dir_name : dir_paths) {
      GetEnclosedGuest().GetLinuxOperator().Mkdir(linux_path_prefix + dir_name, 0644);
    }

    for (auto dir_name : remove_fail) {
      ASSERT_NE(GetEnclosedGuest().GetLinuxOperator().Rmdir(linux_path_prefix + dir_name), 0);
    }

    for (auto dir_name : remove_success) {
      ASSERT_EQ(GetEnclosedGuest().GetLinuxOperator().Rmdir(linux_path_prefix + dir_name), 0);
    }
  }

  {
    GetEnclosedGuest().GetFuchsiaOperator().Fsck();
    GetEnclosedGuest().GetFuchsiaOperator().Mount();

    auto umount = fit::defer([&] { GetEnclosedGuest().GetFuchsiaOperator().Umount(); });

    // Check deleted
    for (auto dir_name : remove_success) {
      auto file =
          GetEnclosedGuest().GetFuchsiaOperator().Open(dir_name, O_RDONLY | O_DIRECTORY, 0644);
      ASSERT_FALSE(file->IsValid());
    }

    // Check remained
    for (auto dir_name : remove_fail) {
      auto file =
          GetEnclosedGuest().GetFuchsiaOperator().Open(dir_name, O_RDONLY | O_DIRECTORY, 0644);
      ASSERT_TRUE(file->IsValid());
    }
  }
}

}  // namespace
}  // namespace f2fs
