// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fit/defer.h>

#include "src/storage/f2fs/test/compatibility/compatibility.h"

namespace f2fs {
namespace {

using InlineCompatibilityTest = CompatibilityTest;

TEST_F(InlineCompatibilityTest, InlineDentryHostToFuchsia) {
  constexpr std::string_view inline_dir_path = "/inline";
  constexpr std::string_view noninline_dir_path = "/noninline";

  uint32_t nr_child_of_inline_dir, nr_child_of_noninline_dir;

  // Get max inline dentry
  {
    uint64_t sector_count = 819200;  // 400MB
    uint64_t disk_size = sector_count * kDefaultSectorSize;

    std::string tmp_image = GenerateTestPath(kTestFileFormat);
    fbl::unique_fd tmp_fd = fbl::unique_fd(mkstemp(tmp_image.data()));
    ftruncate(tmp_fd.get(), disk_size);

    std::string tmp_mount_dir = GenerateTestPath(kTestFileFormat);
    mkdtemp(tmp_mount_dir.data());

    std::unique_ptr<TargetOperator> tmp_target_operator =
        std::make_unique<TargetOperator>(tmp_image, std::move(tmp_fd), sector_count);

    tmp_target_operator->Mkfs();
    MountOptions options;
    ASSERT_EQ(options.SetValue(options.GetNameView(kOptInlineDentry), 1), ZX_OK);
    tmp_target_operator->Mount(options);

    auto umount = fit::defer([&] { tmp_target_operator->Unmount(); });

    // Create inline directory
    tmp_target_operator->Mkdir(inline_dir_path, 0755);
    auto inline_dir = tmp_target_operator->Open(inline_dir_path, O_RDWR, 0644);
    ASSERT_TRUE(inline_dir->is_valid());
    Dir *raw_ptr =
        static_cast<Dir *>(static_cast<TargetTestFile *>(inline_dir.get())->GetRawVnodePtr());

    uint32_t max_inline_dentry = raw_ptr->MaxInlineDentry();
    nr_child_of_inline_dir = max_inline_dentry / 2;
    nr_child_of_noninline_dir = max_inline_dentry * 2;
  }

  // Create child on Linux
  {
    host_operator_->Mkfs();
    host_operator_->Mount();

    auto umount = fit::defer([&] { host_operator_->Unmount(); });

    host_operator_->Mkdir(inline_dir_path, 0755);

    for (uint32_t i = 0; i < nr_child_of_inline_dir; ++i) {
      std::string child_name(inline_dir_path);
      child_name.append("/").append(std::to_string(i));
      host_operator_->Mkdir(child_name, 0755);
    }

    host_operator_->Mkdir(noninline_dir_path, 0755);

    for (uint32_t i = 0; i < nr_child_of_noninline_dir; ++i) {
      std::string child_name(noninline_dir_path);
      child_name.append("/").append(std::to_string(i));
      host_operator_->Mkdir(child_name, 0755);
    }
  }

  {
    target_operator_->Fsck();
    target_operator_->Mount();

    auto umount = fit::defer([&] { target_operator_->Unmount(); });

    // Check if inline directory is still inline on Fuchsia
    auto inline_dir = target_operator_->Open(inline_dir_path, O_RDWR, 0644);
    ASSERT_TRUE(inline_dir->is_valid());

    VnodeF2fs *raw_vn_ptr = static_cast<TargetTestFile *>(inline_dir.get())->GetRawVnodePtr();
    ASSERT_TRUE(raw_vn_ptr->TestFlag(InodeInfoFlag::kInlineDentry));

    // Check if children of inline directory are accessible
    for (uint32_t i = 0; i < nr_child_of_inline_dir; ++i) {
      std::string child_name(inline_dir_path);
      child_name.append("/").append(std::to_string(i));
      auto child = target_operator_->Open(child_name, O_RDWR, 0644);
      ASSERT_TRUE(child->is_valid());
    }

    // Create one more child, and the directory should remain in inline
    std::string additional_child(inline_dir_path);
    additional_child.append("/").append(std::to_string(nr_child_of_inline_dir));
    target_operator_->Mkdir(additional_child, 0755);
    ASSERT_TRUE(raw_vn_ptr->TestFlag(InodeInfoFlag::kInlineDentry));

    // Check if noninline directory is still noninline on Fuchsia
    auto noninline_dir = target_operator_->Open(noninline_dir_path, O_RDWR, 0644);
    ASSERT_TRUE(inline_dir->is_valid());

    raw_vn_ptr = static_cast<TargetTestFile *>(noninline_dir.get())->GetRawVnodePtr();
    ASSERT_FALSE(raw_vn_ptr->TestFlag(InodeInfoFlag::kInlineDentry));

    // Check if children of noninline directory are accessible
    for (uint32_t i = 0; i < nr_child_of_noninline_dir; ++i) {
      std::string child_name(noninline_dir_path);
      child_name.append("/").append(std::to_string(i));
      auto child = target_operator_->Open(child_name, O_RDWR, 0644);
      ASSERT_TRUE(child->is_valid());
    }
  }

  // Check new child exist on Linux
  {
    host_operator_->Fsck();
    host_operator_->Mount();

    auto umount = fit::defer([&] { host_operator_->Unmount(); });

    std::string child_name(inline_dir_path);
    child_name.append("/").append(std::to_string(nr_child_of_inline_dir));

    ASSERT_EQ(
        system(std::string("ls ").append(host_operator_->GetAbsolutePath(child_name)).c_str()), 0);
  }
}

TEST_F(InlineCompatibilityTest, InlineDentryFuchsiaToHost) {
  constexpr std::string_view inline_dir_path = "/inline";
  constexpr std::string_view noninline_dir_path = "/noninline";

  uint32_t nr_child_of_inline_dir, nr_child_of_noninline_dir;

  // Create child on Fuchsia
  {
    target_operator_->Mkfs();
    MountOptions options;
    ASSERT_EQ(options.SetValue(options.GetNameView(kOptInlineDentry), 1), ZX_OK);
    target_operator_->Mount(options);

    auto umount = fit::defer([&] { target_operator_->Unmount(); });

    // Create inline directory
    target_operator_->Mkdir(inline_dir_path, 0755);
    auto inline_dir = target_operator_->Open(inline_dir_path, O_RDWR, 0644);
    ASSERT_TRUE(inline_dir->is_valid());

    VnodeF2fs *raw_inline_vn_ptr =
        static_cast<TargetTestFile *>(inline_dir.get())->GetRawVnodePtr();
    ASSERT_TRUE(raw_inline_vn_ptr->TestFlag(InodeInfoFlag::kInlineDentry));

    // Get max inline dentry
    Dir *raw_dir_ptr = static_cast<Dir *>(raw_inline_vn_ptr);
    uint32_t max_inline_dentry = raw_dir_ptr->MaxInlineDentry();
    nr_child_of_inline_dir = max_inline_dentry / 2;
    nr_child_of_noninline_dir = max_inline_dentry * 2;

    // Create children up to |nr_child_of_inline_dir|, and the directory should be inline
    for (uint32_t i = 0; i < nr_child_of_inline_dir; ++i) {
      std::string child_name(inline_dir_path);
      child_name.append("/").append(std::to_string(i));
      target_operator_->Mkdir(child_name, 0755);
    }
    ASSERT_TRUE(raw_inline_vn_ptr->TestFlag(InodeInfoFlag::kInlineDentry));

    // Create inline directory
    target_operator_->Mkdir(noninline_dir_path, 0755);
    auto noninline_dir = target_operator_->Open(noninline_dir_path, O_RDWR, 0644);
    ASSERT_TRUE(noninline_dir->is_valid());

    auto *raw_noninline_vn_ptr =
        static_cast<TargetTestFile *>(noninline_dir.get())->GetRawVnodePtr();
    ASSERT_TRUE(raw_noninline_vn_ptr->TestFlag(InodeInfoFlag::kInlineDentry));

    // Create children up to |nr_child_of_noninline_dir|, and the directory should be non inline
    for (uint32_t i = 0; i < nr_child_of_noninline_dir; ++i) {
      std::string child_name(noninline_dir_path);
      child_name.append("/").append(std::to_string(i));
      target_operator_->Mkdir(child_name, 0755);
    }
    ASSERT_FALSE(raw_noninline_vn_ptr->TestFlag(InodeInfoFlag::kInlineDentry));
  }

  // Check children exist on Linux
  {
    host_operator_->Fsck();
    host_operator_->Mount();

    auto umount = fit::defer([&] { host_operator_->Unmount(); });

    for (uint32_t i = 0; i < nr_child_of_inline_dir; ++i) {
      std::string child_name(inline_dir_path);
      child_name.append("/").append(std::to_string(i));
      ASSERT_EQ(
          system(std::string("ls ").append(host_operator_->GetAbsolutePath(child_name)).c_str()),
          0);
    }

    for (uint32_t i = 0; i < nr_child_of_noninline_dir; ++i) {
      std::string child_name(noninline_dir_path);
      child_name.append("/").append(std::to_string(i));
      ASSERT_EQ(
          system(std::string("ls ").append(host_operator_->GetAbsolutePath(child_name)).c_str()),
          0);
    }
  }
}

TEST_F(InlineCompatibilityTest, InlineDataHostToFuchsia) {
  constexpr std::string_view inline_file_name = "/inline";

  uint32_t max_inline_data;

  // Get max inline data size
  {
    uint64_t sector_count = 819200;  // 400MB
    uint64_t disk_size = sector_count * kDefaultSectorSize;

    std::string tmp_image = GenerateTestPath(kTestFileFormat);
    fbl::unique_fd tmp_fd = fbl::unique_fd(mkstemp(tmp_image.data()));
    ftruncate(tmp_fd.get(), disk_size);

    std::string tmp_mount_dir = GenerateTestPath(kTestFileFormat);
    mkdtemp(tmp_mount_dir.data());

    std::unique_ptr<TargetOperator> tmp_target_operator =
        std::make_unique<TargetOperator>(tmp_image, std::move(tmp_fd), sector_count);

    tmp_target_operator->Mkfs();
    MountOptions options;
    ASSERT_EQ(options.SetValue(options.GetNameView(kOptInlineData), 1), ZX_OK);
    tmp_target_operator->Mount(options);

    auto umount = fit::defer([&] { tmp_target_operator->Unmount(); });

    auto tmpfile = tmp_target_operator->Open("/tmpfile", O_RDWR | O_CREAT, 0644);
    ASSERT_TRUE(tmpfile->is_valid());

    File *raw_ptr =
        static_cast<File *>(static_cast<TargetTestFile *>(tmpfile.get())->GetRawVnodePtr());

    max_inline_data = raw_ptr->MaxInlineData();
  }

  uint32_t r_buf[kPageSize / sizeof(uint32_t)];
  uint32_t w_buf[kPageSize / sizeof(uint32_t)];
  for (uint32_t i = 0; i < kPageSize / sizeof(uint32_t); ++i) {
    w_buf[i] = CpuToLe(i);
  }

  // Create and write inline file on Linux
  {
    host_operator_->Mkfs();
    host_operator_->Mount();

    auto umount = fit::defer([&] { host_operator_->Unmount(); });

    auto test_file = host_operator_->Open(inline_file_name, O_RDWR | O_CREAT, 0644);
    ASSERT_TRUE(test_file->is_valid());

    ASSERT_EQ(test_file->Write(w_buf, max_inline_data / 2), max_inline_data / 2);
  }

  // Verify on Fuchsia
  {
    target_operator_->Fsck();
    target_operator_->Mount();

    auto umount = fit::defer([&] { target_operator_->Unmount(); });

    // Check if inline file is still inline on Fuchsia
    auto test_file = target_operator_->Open(inline_file_name, O_RDWR, 0644);
    ASSERT_TRUE(test_file->is_valid());

    VnodeF2fs *raw_vn_ptr = static_cast<TargetTestFile *>(test_file.get())->GetRawVnodePtr();
    ASSERT_TRUE(raw_vn_ptr->TestFlag(InodeInfoFlag::kInlineData));

    // Read verify
    ASSERT_EQ(test_file->Read(r_buf, max_inline_data / 2), max_inline_data / 2);
    ASSERT_EQ(memcmp(r_buf, w_buf, max_inline_data / 2), 0);
  }
}

TEST_F(InlineCompatibilityTest, InlineDataFuchsiaToHost) {
  constexpr std::string_view inline_file_name = "/inline";

  uint32_t max_inline_data;

  // Get max inline data size
  {
    uint64_t sector_count = 819200;  // 400MB
    uint64_t disk_size = sector_count * kDefaultSectorSize;

    std::string tmp_image = GenerateTestPath(kTestFileFormat);
    fbl::unique_fd tmp_fd = fbl::unique_fd(mkstemp(tmp_image.data()));
    ftruncate(tmp_fd.get(), disk_size);

    std::string tmp_mount_dir = GenerateTestPath(kTestFileFormat);
    mkdtemp(tmp_mount_dir.data());

    std::unique_ptr<TargetOperator> tmp_target_operator =
        std::make_unique<TargetOperator>(tmp_image, std::move(tmp_fd), sector_count);

    tmp_target_operator->Mkfs();
    MountOptions options;
    ASSERT_EQ(options.SetValue(options.GetNameView(kOptInlineData), 1), ZX_OK);
    tmp_target_operator->Mount(options);

    auto umount = fit::defer([&] { tmp_target_operator->Unmount(); });

    auto tmpfile = tmp_target_operator->Open("/tmpfile", O_RDWR | O_CREAT, 0644);
    ASSERT_TRUE(tmpfile->is_valid());

    File *raw_ptr =
        static_cast<File *>(static_cast<TargetTestFile *>(tmpfile.get())->GetRawVnodePtr());

    max_inline_data = raw_ptr->MaxInlineData();
  }

  uint32_t r_buf[kPageSize / sizeof(uint32_t)];
  uint32_t w_buf[kPageSize / sizeof(uint32_t)];
  for (uint32_t i = 0; i < kPageSize / sizeof(uint32_t); ++i) {
    w_buf[i] = CpuToLe(i);
  }

  // Create and write inline file on Fuchsia
  {
    target_operator_->Mkfs();
    MountOptions options;
    ASSERT_EQ(options.SetValue(options.GetNameView(kOptInlineData), 1), ZX_OK);
    target_operator_->Mount(options);

    auto umount = fit::defer([&] { target_operator_->Unmount(); });

    auto test_file = target_operator_->Open(inline_file_name, O_RDWR | O_CREAT, 0644);
    ASSERT_TRUE(test_file->is_valid());

    ASSERT_EQ(test_file->Write(w_buf, max_inline_data / 2), max_inline_data / 2);

    VnodeF2fs *raw_vn_ptr = static_cast<TargetTestFile *>(test_file.get())->GetRawVnodePtr();
    ASSERT_TRUE(raw_vn_ptr->TestFlag(InodeInfoFlag::kInlineData));
  }

  // Verify on Linux
  {
    host_operator_->Fsck();
    host_operator_->Mount();

    auto umount = fit::defer([&] { host_operator_->Unmount(); });

    // Check if inline file is still inline on Fuchsia
    auto test_file = host_operator_->Open(inline_file_name, O_RDWR, 0644);
    ASSERT_TRUE(test_file->is_valid());

    // Read verify
    ASSERT_EQ(test_file->Read(r_buf, max_inline_data / 2), max_inline_data / 2);
    ASSERT_EQ(memcmp(r_buf, w_buf, max_inline_data / 2), 0);
  }
}

TEST_F(InlineCompatibilityTest, DataExistFlagHostToFuchsia) {
  constexpr std::string_view filenames[4] = {"alpha", "bravo", "charlie", "delta"};
  constexpr std::string_view test_string = "hello";

  // Create and write inline files on Linux
  {
    host_operator_->Mkfs();
    host_operator_->Mount();

    auto umount = fit::defer([&] { host_operator_->Unmount(); });

    // create file
    auto test_file = host_operator_->Open(filenames[0], O_RDWR | O_CREAT, 0644);
    ASSERT_TRUE(test_file->is_valid());

    // write some data
    test_file = host_operator_->Open(filenames[1], O_RDWR | O_CREAT, 0644);
    ASSERT_TRUE(test_file->is_valid());

    ASSERT_EQ(test_file->Write(test_string.data(), test_string.size()),
              static_cast<ssize_t>(test_string.size()));

    // truncate to non-zero size
    test_file = host_operator_->Open(filenames[2], O_RDWR | O_CREAT, 0644);
    ASSERT_TRUE(test_file->is_valid());

    ASSERT_EQ(test_file->Write(test_string.data(), test_string.size()),
              static_cast<ssize_t>(test_string.size()));
    ASSERT_EQ(test_file->Ftruncate(test_string.size() / 2), 0);

    // truncate to zero size
    test_file = host_operator_->Open(filenames[3], O_RDWR | O_CREAT, 0644);
    ASSERT_TRUE(test_file->is_valid());

    ASSERT_EQ(test_file->Write(test_string.data(), test_string.size()),
              static_cast<ssize_t>(test_string.size()));
    ASSERT_EQ(test_file->Ftruncate(0), 0);
  }

  // check if all files have correct flags on Fuchsia
  {
    target_operator_->Fsck();
    target_operator_->Mount();

    auto umount = fit::defer([&] { target_operator_->Unmount(); });

    // only created, kDataExist should be unset
    auto test_file = target_operator_->Open(filenames[0], O_RDWR | O_CREAT, 0644);
    ASSERT_TRUE(test_file->is_valid());

    VnodeF2fs *raw_vn_ptr = static_cast<TargetTestFile *>(test_file.get())->GetRawVnodePtr();
    ASSERT_TRUE(raw_vn_ptr->TestFlag(InodeInfoFlag::kInlineData));
    ASSERT_FALSE(raw_vn_ptr->TestFlag(InodeInfoFlag::kDataExist));

    // data written, kDataExist should be set
    test_file = target_operator_->Open(filenames[1], O_RDWR | O_CREAT, 0644);
    ASSERT_TRUE(test_file->is_valid());

    raw_vn_ptr = static_cast<TargetTestFile *>(test_file.get())->GetRawVnodePtr();
    ASSERT_TRUE(raw_vn_ptr->TestFlag(InodeInfoFlag::kInlineData));
    ASSERT_TRUE(raw_vn_ptr->TestFlag(InodeInfoFlag::kDataExist));

    // truncated to non-zero size, kDataExist should be set
    test_file = target_operator_->Open(filenames[2], O_RDWR | O_CREAT, 0644);
    ASSERT_TRUE(test_file->is_valid());

    raw_vn_ptr = static_cast<TargetTestFile *>(test_file.get())->GetRawVnodePtr();
    ASSERT_TRUE(raw_vn_ptr->TestFlag(InodeInfoFlag::kInlineData));
    ASSERT_TRUE(raw_vn_ptr->TestFlag(InodeInfoFlag::kDataExist));

    // truncated to zero size, kDataExist should be unset
    test_file = target_operator_->Open(filenames[3], O_RDWR | O_CREAT, 0644);
    ASSERT_TRUE(test_file->is_valid());

    raw_vn_ptr = static_cast<TargetTestFile *>(test_file.get())->GetRawVnodePtr();
    ASSERT_TRUE(raw_vn_ptr->TestFlag(InodeInfoFlag::kInlineData));
    ASSERT_FALSE(raw_vn_ptr->TestFlag(InodeInfoFlag::kDataExist));
  }
}

TEST_F(InlineCompatibilityTest, DataExistFlagFuchsiaToHost) {
  constexpr std::string_view filenames[4] = {"alpha", "bravo", "charlie", "delta"};
  constexpr std::string_view test_string = "hello";

  // Create and write inline files on Fuchsia
  {
    target_operator_->Mkfs();
    MountOptions options;
    ASSERT_EQ(options.SetValue(options.GetNameView(kOptInlineData), 1), ZX_OK);
    target_operator_->Mount(options);

    auto umount = fit::defer([&] { target_operator_->Unmount(); });

    // create file, then check if kDataExist is unset
    auto test_file = target_operator_->Open(filenames[0], O_RDWR | O_CREAT, 0644);
    ASSERT_TRUE(test_file->is_valid());

    VnodeF2fs *raw_vn_ptr = static_cast<TargetTestFile *>(test_file.get())->GetRawVnodePtr();
    ASSERT_TRUE(raw_vn_ptr->TestFlag(InodeInfoFlag::kInlineData));
    ASSERT_FALSE(raw_vn_ptr->TestFlag(InodeInfoFlag::kDataExist));

    // write some data, then check if kDataExist is set
    test_file = target_operator_->Open(filenames[1], O_RDWR | O_CREAT, 0644);
    ASSERT_TRUE(test_file->is_valid());

    ASSERT_EQ(test_file->Write(test_string.data(), test_string.size()),
              static_cast<ssize_t>(test_string.size()));

    raw_vn_ptr = static_cast<TargetTestFile *>(test_file.get())->GetRawVnodePtr();
    ASSERT_TRUE(raw_vn_ptr->TestFlag(InodeInfoFlag::kInlineData));
    ASSERT_TRUE(raw_vn_ptr->TestFlag(InodeInfoFlag::kDataExist));

    // truncate to non-zero size, then check if kDataExist is set
    test_file = target_operator_->Open(filenames[2], O_RDWR | O_CREAT, 0644);
    ASSERT_TRUE(test_file->is_valid());

    ASSERT_EQ(test_file->Write(test_string.data(), test_string.size()),
              static_cast<ssize_t>(test_string.size()));
    ASSERT_EQ(test_file->Ftruncate(test_string.size() / 2), 0);

    raw_vn_ptr = static_cast<TargetTestFile *>(test_file.get())->GetRawVnodePtr();
    ASSERT_TRUE(raw_vn_ptr->TestFlag(InodeInfoFlag::kInlineData));
    ASSERT_TRUE(raw_vn_ptr->TestFlag(InodeInfoFlag::kDataExist));

    // truncate to zero size, then check if kDataExist is unset
    test_file = target_operator_->Open(filenames[3], O_RDWR | O_CREAT, 0644);
    ASSERT_TRUE(test_file->is_valid());

    ASSERT_EQ(test_file->Write(test_string.data(), test_string.size()),
              static_cast<ssize_t>(test_string.size()));
    ASSERT_EQ(test_file->Ftruncate(0), 0);

    raw_vn_ptr = static_cast<TargetTestFile *>(test_file.get())->GetRawVnodePtr();
    ASSERT_TRUE(raw_vn_ptr->TestFlag(InodeInfoFlag::kInlineData));
    ASSERT_FALSE(raw_vn_ptr->TestFlag(InodeInfoFlag::kDataExist));
  }

  // check if all files pass fsck on Linux
  { host_operator_->Fsck(); }
}

}  // namespace
}  // namespace f2fs
