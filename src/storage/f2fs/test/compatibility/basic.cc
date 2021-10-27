// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <lib/fit/defer.h>
#include <sys/stat.h>

#include <gtest/gtest.h>

#include "src/storage/f2fs/test/compatibility/compatibility.h"

namespace f2fs {
namespace {

constexpr uint64_t kBlockCount = 819200;  // 400MB
const std::string kTestFileFormat = "f2fs_file.XXXXXX";

zx::status<struct stat> EmulateStat(const fbl::RefPtr<fs::Vnode> vn) {
  fs::VnodeAttributes vn_attr;
  if (zx_status_t status = vn->GetAttributes(&vn_attr); status != ZX_OK) {
    return zx::error(status);
  }
  struct stat ret {};
  ret.st_ino = vn_attr.inode;
  ret.st_mode = static_cast<mode_t>(vn_attr.mode);
  ret.st_nlink = vn_attr.link_count;
  ret.st_size = vn_attr.content_size;
  ret.st_ctim.tv_sec = vn_attr.creation_time / ZX_SEC(1);
  ret.st_ctim.tv_nsec = vn_attr.creation_time % ZX_SEC(1);
  ret.st_mtim.tv_sec = vn_attr.modification_time / ZX_SEC(1);
  ret.st_mtim.tv_nsec = vn_attr.modification_time % ZX_SEC(1);
  return zx::ok(ret);
}

void CompareStat(const struct stat &a, const struct stat &b) {
  EXPECT_EQ(a.st_ino, b.st_ino);
  EXPECT_EQ(a.st_mode, b.st_mode);
  EXPECT_EQ(a.st_nlink, b.st_nlink);
  EXPECT_EQ(a.st_size, b.st_size);
  EXPECT_EQ(a.st_ctime, b.st_ctime);
  EXPECT_EQ(a.st_mtime, b.st_mtime);
}

class GeneralCompatibilityTest : public testing::Test {
 public:
  GeneralCompatibilityTest() {
    uint64_t kDiskSize = kBlockCount * kDefaultSectorSize;

    test_image_path_ = GenerateTestPath(kTestFileFormat);
    test_image_fd_ = fbl::unique_fd(mkstemp(test_image_path_.data()));
    ftruncate(test_image_fd_.get(), kDiskSize);

    mount_directory_ = GenerateTestPath(kTestFileFormat);
    mkdtemp(mount_directory_.data());
  }

  ~GeneralCompatibilityTest() {
    unlink(test_image_path_.c_str());
    rmdir(mount_directory_.c_str());
  }

 protected:
  std::string test_image_path_;
  fbl::unique_fd test_image_fd_;
  std::string mount_directory_;
};

TEST_F(GeneralCompatibilityTest, WriteVerifyHostToFuchsia) {
  constexpr uint32_t kVerifyPatternSize = 1024 * 1024 * 100;  // 100MB

  ASSERT_EQ(system(std::string("mkfs.f2fs ").append(test_image_path_).c_str()), 0);

  {
    ASSERT_EQ(system(std::string("mount -t f2fs ")
                         .append(test_image_path_)
                         .append(" -o noinline_data,noinline_xattr ")
                         .append(mount_directory_)
                         .c_str()),
              0);

    auto umount = fit::defer(
        [&] { ASSERT_EQ(system(std::string("umount ").append(mount_directory_).c_str()), 0); });

    ASSERT_EQ(mkdir(std::string(mount_directory_).append("/alpha").c_str(), 0755), 0);

    std::string bravo = std::string(mount_directory_).append("/alpha/bravo");
    auto bravo_fd = fbl::unique_fd(open(bravo.c_str(), O_RDWR | O_CREAT, 0644));
    ASSERT_GT(bravo_fd.get(), 0);

    uint32_t input[kBlockSize / sizeof(uint32_t)];
    // write to bravo with offset pattern
    for (uint32_t i = 0; i < kVerifyPatternSize / sizeof(input); ++i) {
      for (uint32_t j = 0; j < sizeof(input) / sizeof(uint32_t); ++j) {
        input[j] = CpuToLe(j);
      }
      ASSERT_EQ(write(bravo_fd.get(), input, sizeof(input)), static_cast<ssize_t>(sizeof(input)));
    }
  }

  // verify on Fuchsia
  {
    auto result = CreateFsAndRootFromImage(MountOptions{}, std::move(test_image_fd_), kBlockCount);
    ASSERT_TRUE(result.is_ok());
    auto fs = std::move(result->first);
    auto root_dir = result->second;

    auto umount = fit::defer([&] {
      ASSERT_EQ(root_dir->Close(), ZX_OK);
      fs->PutSuper();
    });

    fbl::RefPtr<fs::Vnode> vn = nullptr;
    ASSERT_EQ(root_dir->Lookup("alpha", &vn), ZX_OK);
    auto alpha = fbl::RefPtr<Dir>::Downcast(std::move(vn));

    ASSERT_EQ(alpha->Lookup("bravo", &vn), ZX_OK);
    auto bravo = fbl::RefPtr<File>::Downcast(std::move(vn));

    uint32_t buffer[kBlockSize / sizeof(uint32_t)];
    size_t read_len;
    size_t offset = 0;

    for (uint32_t i = 0; i < kVerifyPatternSize / sizeof(buffer); ++i) {
      ASSERT_EQ(bravo->Read(buffer, sizeof(buffer), offset, &read_len), ZX_OK);
      ASSERT_EQ(read_len, sizeof(buffer));

      for (uint32_t j = 0; j < sizeof(buffer) / sizeof(uint32_t); ++j) {
        ASSERT_EQ(buffer[j], LeToCpu(j));
      }
      offset += read_len;
    }
  }
}

TEST_F(GeneralCompatibilityTest, VerifyAttributesHostToFuchsia) {
  std::vector<std::pair<std::string, struct stat>> test_set{};

  ASSERT_EQ(system(std::string("mkfs.f2fs ").append(test_image_path_).c_str()), 0);
  {
    ASSERT_EQ(system(std::string("mount -t f2fs ")
                         .append(test_image_path_)
                         .append(" -o noinline_data,noinline_xattr ")
                         .append(mount_directory_)
                         .c_str()),
              0);

    auto umount = fit::defer(
        [&] { ASSERT_EQ(system(std::string("umount ").append(mount_directory_).c_str()), 0); });

    ASSERT_EQ(mkdir(std::string(mount_directory_).append("/alpha").c_str(), 0755), 0);

    for (mode_t mode = 0; mode <= (S_IRWXU | S_IRWXG | S_IRWXO); ++mode) {
      struct stat file_stat {};
      std::string child = std::string(mount_directory_).append("/alpha/").append(kTestFileFormat);
      auto child_fd = fbl::unique_fd(mkstemp(child.data()));
      ASSERT_GT(child_fd.get(), 0);
      ASSERT_EQ(fchmod(child_fd.get(), mode), 0);
      ASSERT_EQ(fstat(child_fd.get(), &file_stat), 0);

      test_set.push_back({basename(child.c_str()), file_stat});
    }
  }

  // verify on Fuchsia
  {
    auto result = CreateFsAndRootFromImage(MountOptions{}, std::move(test_image_fd_), kBlockCount);
    ASSERT_TRUE(result.is_ok());
    auto fs = std::move(result->first);
    auto root_dir = result->second;

    auto umount = fit::defer([&] {
      ASSERT_EQ(root_dir->Close(), ZX_OK);
      fs->PutSuper();
    });

    fbl::RefPtr<fs::Vnode> vn = nullptr;
    ASSERT_EQ(root_dir->Lookup("alpha", &vn), ZX_OK);
    auto alpha = fbl::RefPtr<Dir>::Downcast(std::move(vn));

    for (auto [name, host_stat] : test_set) {
      ASSERT_EQ(alpha->Lookup(name.c_str(), &vn), ZX_OK);
      auto child = fbl::RefPtr<Dir>::Downcast(std::move(vn));
      auto child_stat = EmulateStat(child);
      ASSERT_TRUE(child_stat.is_ok());
      CompareStat(*child_stat, host_stat);
    }
  }
}

TEST_F(GeneralCompatibilityTest, TruncateHostToFuchsia) {
  constexpr uint32_t kVerifyPatternSize = 1024 * 1024 * 100;  // 100MB
  constexpr uint64_t kTruncateSize = 64 * 1024;               // 64KB

  ASSERT_EQ(system(std::string("mkfs.f2fs ").append(test_image_path_).c_str()), 0);

  {
    ASSERT_EQ(system(std::string("mount -t f2fs ")
                         .append(test_image_path_)
                         .append(" -o noinline_data,noinline_xattr ")
                         .append(mount_directory_)
                         .c_str()),
              0);

    auto umount = fit::defer(
        [&] { ASSERT_EQ(system(std::string("umount ").append(mount_directory_).c_str()), 0); });

    ASSERT_EQ(mkdir(std::string(mount_directory_).append("/alpha").c_str(), 0755), 0);

    std::string extend = std::string(mount_directory_).append("/alpha/extend");
    auto extend_fd = fbl::unique_fd(open(extend.c_str(), O_RDWR | O_CREAT, 0755));
    ASSERT_GT(extend_fd.get(), 0);
    ASSERT_EQ(ftruncate(extend_fd.get(), kTruncateSize), 0);

    std::string shrink = std::string(mount_directory_).append("/alpha/shrink");
    auto shrink_fd = fbl::unique_fd(open(shrink.c_str(), O_RDWR | O_CREAT, 0755));
    ASSERT_GT(shrink_fd.get(), 0);

    uint32_t input[kBlockSize / sizeof(uint32_t)];
    // write with offset pattern
    for (uint32_t i = 0; i < kVerifyPatternSize / sizeof(input); ++i) {
      for (uint32_t j = 0; j < sizeof(input) / sizeof(uint32_t); ++j) {
        input[j] = CpuToLe(j);
      }
      ASSERT_EQ(write(shrink_fd.get(), input, sizeof(input)), static_cast<ssize_t>(sizeof(input)));
    }

    ASSERT_EQ(ftruncate(shrink_fd.get(), kTruncateSize), 0);
  }

  // verify on Fuchsia
  {
    auto result = CreateFsAndRootFromImage(MountOptions{}, std::move(test_image_fd_), kBlockCount);
    ASSERT_TRUE(result.is_ok());
    auto fs = std::move(result->first);
    auto root_dir = result->second;

    auto umount = fit::defer([&] {
      ASSERT_EQ(root_dir->Close(), ZX_OK);
      fs->PutSuper();
    });

    fbl::RefPtr<fs::Vnode> vn = nullptr;
    ASSERT_EQ(root_dir->Lookup("alpha", &vn), ZX_OK);
    auto alpha = fbl::RefPtr<Dir>::Downcast(std::move(vn));

    ASSERT_EQ(alpha->Lookup("extend", &vn), ZX_OK);
    auto extend = fbl::RefPtr<File>::Downcast(std::move(vn));
    fs::VnodeAttributes extend_attr;
    ASSERT_EQ(extend->GetAttributes(&extend_attr), ZX_OK);
    EXPECT_EQ(extend_attr.content_size, kTruncateSize);
    uint32_t buffer[kBlockSize / sizeof(uint32_t)];
    size_t read_len;
    size_t offset = 0;

    for (uint32_t i = 0; i < kTruncateSize / sizeof(buffer); ++i) {
      ASSERT_EQ(extend->Read(buffer, sizeof(buffer), offset, &read_len), ZX_OK);
      ASSERT_EQ(read_len, sizeof(buffer));

      for (uint32_t j = 0; j < sizeof(buffer) / sizeof(uint32_t); ++j) {
        ASSERT_EQ(buffer[j], static_cast<uint32_t>(0));
      }
      offset += read_len;
    }

    ASSERT_EQ(alpha->Lookup("shrink", &vn), ZX_OK);
    auto shrink = fbl::RefPtr<File>::Downcast(std::move(vn));
    fs::VnodeAttributes shrink_attr;
    ASSERT_EQ(shrink->GetAttributes(&shrink_attr), ZX_OK);
    EXPECT_EQ(shrink_attr.content_size, kTruncateSize);
    offset = 0;

    for (uint32_t i = 0; i < kTruncateSize / sizeof(buffer); ++i) {
      ASSERT_EQ(shrink->Read(buffer, sizeof(buffer), offset, &read_len), ZX_OK);
      ASSERT_EQ(read_len, sizeof(buffer));

      for (uint32_t j = 0; j < sizeof(buffer) / sizeof(uint32_t); ++j) {
        ASSERT_EQ(buffer[j], LeToCpu(j));
      }
      offset += read_len;
    }
  }
}

}  // namespace
}  // namespace f2fs
