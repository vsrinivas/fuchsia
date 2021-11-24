// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fit/defer.h>

#include <vector>

#include "src/storage/f2fs/test/compatibility/compatibility.h"

namespace f2fs {
namespace {

using DirCompatibilityTest = CompatibilityTest;

TEST_F(DirCompatibilityTest, DirWidthTestHostToFuchsia) {
  // Maximum number of directories on linux. It depends on disk image size.
  // TODO: To make HostOperator::Mkdir return success/fail instead of using ASSERT
  constexpr int kDirWidth = 37791;
  {
    host_operator_->Mkfs();
    host_operator_->Mount();

    auto umount = fit::defer([&] { host_operator_->Unmount(); });

    for (int width = 0; width < kDirWidth; ++width) {
      std::string dir_name = std::string("/").append(std::to_string(width));
      host_operator_->Mkdir(dir_name, 0644);
    }
  }

  {
    target_operator_->Fsck();
    target_operator_->Mount();

    auto umount = fit::defer([&] { target_operator_->Unmount(); });

    for (int width = 0; width < kDirWidth; ++width) {
      std::string dir_name = std::string("/").append(std::to_string(width));
      auto file = target_operator_->Open(dir_name, O_RDONLY | O_DIRECTORY, 0644);
      ASSERT_TRUE(file->is_valid());
    }
  }
}

TEST_F(DirCompatibilityTest, DirWidthTestFuchsiaToHost) {
  // Maximum number of directories on linux. It depends on disk image size.
  // TODO: To make HostOperator::Mkdir return success/fail instead of using ASSERT
  constexpr int kDirWidth = 37791;
  {
    target_operator_->Mkfs();
    target_operator_->Mount();

    auto umount = fit::defer([&] { target_operator_->Unmount(); });

    for (int width = 0; width < kDirWidth; ++width) {
      std::string dir_name = std::string("/").append(std::to_string(width));
      target_operator_->Mkdir(dir_name, 0644);
    }
  }

  {
    host_operator_->Fsck();
    host_operator_->Mount();

    auto umount = fit::defer([&] { host_operator_->Unmount(); });

    for (int width = 0; width < kDirWidth; ++width) {
      std::string dir_name = std::string("/").append(std::to_string(width));
      auto file = host_operator_->Open(dir_name, O_RDONLY | O_DIRECTORY, 0644);
      ASSERT_TRUE(file->is_valid());
    }
  }
}

TEST_F(DirCompatibilityTest, DirDepthTestHostToFuchsia) {
  // Maximum depth of directories on linux. It doesn't depend on disk image size.
  constexpr int kDirDepth = 1035;
  {
    host_operator_->Mkfs();
    host_operator_->Mount();

    auto umount = fit::defer([&] { host_operator_->Unmount(); });

    std::string dir_name;
    for (int depth = 0; depth < kDirDepth; ++depth) {
      dir_name.append("/").append(std::to_string(depth));
      host_operator_->Mkdir(dir_name, 0644);
    }
  }

  {
    target_operator_->Fsck();
    target_operator_->Mount();

    auto umount = fit::defer([&] { target_operator_->Unmount(); });

    std::string dir_name;
    for (int depth = 0; depth < kDirDepth; ++depth) {
      dir_name.append("/").append(std::to_string(depth));
      auto file = target_operator_->Open(dir_name, O_RDONLY | O_DIRECTORY, 0644);
      ASSERT_TRUE(file->is_valid());
    }
  }
}

TEST_F(DirCompatibilityTest, DirDepthTestFuchsiaToHost) {
  // constexpr int kDirDepth = 1035;
  constexpr int kDirDepth = 1000;
  {
    target_operator_->Mkfs();
    target_operator_->Mount();

    auto umount = fit::defer([&] { target_operator_->Unmount(); });

    std::string dir_name;
    for (int depth = 0; depth < kDirDepth; ++depth) {
      dir_name.append("/").append(std::to_string(depth));
      target_operator_->Mkdir(dir_name, 0644);
    }
  }

  {
    host_operator_->Fsck();
    host_operator_->Mount();

    auto umount = fit::defer([&] { host_operator_->Unmount(); });

    std::string dir_name;
    for (int depth = 0; depth < kDirDepth; ++depth) {
      dir_name.append("/").append(std::to_string(depth));
      auto file = host_operator_->Open(dir_name, O_RDONLY | O_DIRECTORY, 0644);
      ASSERT_TRUE(file->is_valid());
    }
  }
}

TEST_F(DirCompatibilityTest, DirRemoveTestHostToFuchsia) {
  std::vector<std::string> dir_paths = {"/d_a", "/d_a/d_b", "/d_c"};

  std::vector<std::string> remove_fail = {"/d_a"};
  std::vector<std::string> remove_success = {"/d_a/d_b", "/d_c"};
  {
    host_operator_->Mkfs();
    host_operator_->Mount();

    auto umount = fit::defer([&] { host_operator_->Unmount(); });

    for (auto dir_name : dir_paths) {
      host_operator_->Mkdir(dir_name, 0644);
    }

    for (auto dir_name : remove_fail) {
      ASSERT_NE(host_operator_->Rmdir(dir_name), 0);
    }

    for (auto dir_name : remove_success) {
      ASSERT_EQ(host_operator_->Rmdir(dir_name), 0);
    }
  }

  {
    target_operator_->Fsck();
    target_operator_->Mount();

    auto umount = fit::defer([&] { target_operator_->Unmount(); });

    // check deleted
    for (auto dir_name : remove_success) {
      auto file = target_operator_->Open(dir_name, O_RDONLY | O_DIRECTORY, 0644);
      ASSERT_FALSE(file->is_valid());
    }

    // check remained
    for (auto dir_name : remove_fail) {
      auto file = target_operator_->Open(dir_name, O_RDONLY | O_DIRECTORY, 0644);
      ASSERT_TRUE(file->is_valid());
    }
  }
}

TEST_F(DirCompatibilityTest, DirRemoveTestFuchsiaToHost) {
  std::vector<std::string> dir_paths = {"/d_a", "/d_a/d_b", "/d_c"};

  std::vector<std::string> remove_fail = {"/d_a"};
  std::vector<std::string> remove_success = {"/d_a/d_b", "/d_c"};
  {
    target_operator_->Mkfs();
    target_operator_->Mount();

    auto umount = fit::defer([&] { target_operator_->Unmount(); });

    for (auto dir_name : dir_paths) {
      target_operator_->Mkdir(dir_name, 0644);
    }

    for (auto dir_name : remove_fail) {
      ASSERT_NE(target_operator_->Rmdir(dir_name), 0);
    }

    for (auto dir_name : remove_success) {
      ASSERT_EQ(target_operator_->Rmdir(dir_name), 0);
    }
  }

  {
    host_operator_->Fsck();
    host_operator_->Mount();

    auto umount = fit::defer([&] { host_operator_->Unmount(); });

    // check deleted
    for (auto dir_name : remove_success) {
      auto file = host_operator_->Open(dir_name, O_RDONLY | O_DIRECTORY, 0644);
      ASSERT_FALSE(file->is_valid());
    }

    // check remained
    for (auto dir_name : remove_fail) {
      auto file = host_operator_->Open(dir_name, O_RDONLY | O_DIRECTORY, 0644);
      ASSERT_TRUE(file->is_valid());
    }
  }
}

TEST_F(DirCompatibilityTest, DirRenameTestHostToFuchsia) {
  std::vector<std::string> dir_paths = {"/d_a", "/d_a/d_b", "/d_c"};
  std::vector<std::pair<std::string, std::string>> rename_from_to = {
      {"/d_a0", "/d_a0_"}, {"/d_a1", "/d_c/d_a1_"}, {"/d_a/d_b/d_ab0", "/d_c/d_ab0_"}};
  {
    host_operator_->Mkfs();
    host_operator_->Mount();

    auto umount = fit::defer([&] { host_operator_->Unmount(); });

    for (auto dir_name : dir_paths) {
      host_operator_->Mkdir(dir_name, 0644);
    }

    // Create
    for (auto [dir_name_from, dir_name_to] : rename_from_to) {
      host_operator_->Mkdir(dir_name_from, 0644);
    }

    // Rename
    for (auto [dir_name_from, dir_name_to] : rename_from_to) {
      host_operator_->Rename(dir_name_from, dir_name_to);
    }
  }

  {
    target_operator_->Fsck();
    target_operator_->Mount();

    auto umount = fit::defer([&] { target_operator_->Unmount(); });

    for (auto [dir_name_from, dir_name_to] : rename_from_to) {
      auto file = target_operator_->Open(dir_name_from, O_RDONLY | O_DIRECTORY, 0644);
      ASSERT_FALSE(file->is_valid());

      file = target_operator_->Open(dir_name_to, O_RDONLY | O_DIRECTORY, 0644);
      ASSERT_TRUE(file->is_valid());
    }
  }
}

TEST_F(DirCompatibilityTest, DirRenameTestFuchsiaToHost) {
  std::vector<std::string> dir_paths = {"/d_a", "/d_a/d_b", "/d_c"};
  std::vector<std::pair<std::string, std::string>> rename_from_to = {
      {"/d_a0", "/d_a0_"}, {"/d_a1", "/d_c/d_a1_"}, {"/d_a/d_b/d_ab0", "/d_c/d_ab0_"}};
  {
    MountOptions options{};
    ASSERT_EQ(options.SetValue(options.GetNameView(kOptInlineDentry), 0), ZX_OK);
    target_operator_->Mkfs();
    target_operator_->Mount(options);

    auto umount = fit::defer([&] { target_operator_->Unmount(); });

    for (auto dir_name : dir_paths) {
      target_operator_->Mkdir(dir_name, 0644);
    }

    // Create
    for (auto [dir_name_from, dir_name_to] : rename_from_to) {
      target_operator_->Mkdir(dir_name_from, 0644);
    }

    // Rename
    for (auto [dir_name_from, dir_name_to] : rename_from_to) {
      target_operator_->Rename(dir_name_from, dir_name_to);
    }
  }

  {
    host_operator_->Fsck();
    host_operator_->Mount();

    auto umount = fit::defer([&] { host_operator_->Unmount(); });

    for (auto [dir_name_from, dir_name_to] : rename_from_to) {
      auto file = host_operator_->Open(dir_name_from, O_RDONLY | O_DIRECTORY, 0644);
      ASSERT_FALSE(file->is_valid());

      file = host_operator_->Open(dir_name_to, O_RDONLY | O_DIRECTORY, 0644);
      ASSERT_TRUE(file->is_valid());
    }
  }
}

}  // namespace
}  // namespace f2fs
