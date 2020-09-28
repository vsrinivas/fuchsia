// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <fuchsia/io/llcpp/fidl.h>
#include <lib/fdio/cpp/caller.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zircon/errors.h>

#include <iostream>
#include <string>
#include <tuple>
#include <vector>

#include <fbl/unique_fd.h>
#include <fvm/format.h>

#include "src/storage/fs_test/fs_test_fixture.h"
#include "src/storage/minfs/format.h"

namespace fs_test {
namespace {

namespace fio = ::llcpp::fuchsia::io;

using ParamType = std::tuple<TestFilesystemOptions, /*remount=*/bool>;

class ResizeTest : public BaseFilesystemTest, public testing::WithParamInterface<ParamType> {
 public:
  ResizeTest() : BaseFilesystemTest(std::get<0>(GetParam())) {}

  bool ShouldRemount() const { return std::get<1>(GetParam()); }

 protected:
  void QueryInfo(uint64_t* out_free_pool_size) {
    fbl::unique_fd fd(open(fs().mount_path().c_str(), O_RDONLY | O_DIRECTORY));
    ASSERT_TRUE(fd);
    fdio_cpp::FdioCaller caller(std::move(fd));
    auto query_result = fio::DirectoryAdmin::Call::QueryFilesystem(caller.channel());
    ASSERT_EQ(query_result.status(), ZX_OK);
    ASSERT_NE(query_result.Unwrap()->info, nullptr);
    fio::FilesystemInfo* info = query_result.Unwrap()->info.get();
    // This should always be true, for all filesystems.
    ASSERT_GT(info->total_bytes, info->used_bytes);
    *out_free_pool_size = info->free_shared_pool_bytes;
  }

  void EnsureCanGrow() {
    uint64_t free_pool_size;
    ASSERT_NO_FATAL_FAILURE(QueryInfo(&free_pool_size));
    // This tests expects to run with free FVM space.
    ASSERT_GT(free_pool_size, 0ul);
  }

  void EnsureCannotGrow() {
    uint64_t free_pool_size;
    ASSERT_NO_FATAL_FAILURE(QueryInfo(&free_pool_size));
    ASSERT_EQ(free_pool_size, 0ul);
  }
};

using MaxInodeTest = ResizeTest;

TEST_P(MaxInodeTest, UseAllInodes) {
  ASSERT_NO_FATAL_FAILURE(EnsureCanGrow());

  // Create 100,000 inodes.
  // We expect that this will force enough inodes to cause the
  // filesystem structures to resize partway through.
  constexpr size_t kFilesPerDirectory = 100;
  size_t d = 0;
  while (true) {
    if (d % 100 == 0) {
      std::cerr << "Creating directory (containing 100 files): " << d << std::endl;
    }
    const std::string dname = GetPath(std::to_string(d));
    if (mkdir(dname.c_str(), 0666) < 0) {
      ASSERT_EQ(errno, ENOSPC);
      break;
    }
    bool stop = false;
    for (size_t f = 0; f < kFilesPerDirectory; f++) {
      const std::string fname = dname + "/" + std::to_string(f);
      fbl::unique_fd fd(open(fname.c_str(), O_CREAT | O_RDWR | O_EXCL));
      if (!fd) {
        ASSERT_EQ(errno, ENOSPC);
        stop = true;
        break;
      }
    }
    if (stop) {
      break;
    }
    d++;
  }

  ASSERT_NO_FATAL_FAILURE(EnsureCannotGrow());

  if (ShouldRemount()) {
    std::cerr << "Unmounting, Verifying, Re-mounting..." << std::endl;
    EXPECT_EQ(fs().Unmount().status_value(), ZX_OK);
    EXPECT_EQ(fs().Fsck().status_value(), ZX_OK);
    EXPECT_EQ(fs().Mount().status_value(), ZX_OK);
  }

  size_t directory_count = d;
  for (size_t d = 0; d < directory_count; d++) {
    if (d % 100 == 0) {
      std::cerr << "Deleting directory (containing 100 files): " << d << std::endl;
    }
    const std::string dname = GetPath(std::to_string(d));
    for (size_t f = 0; f < kFilesPerDirectory; f++) {
      const std::string fname = dname + "/" + std::to_string(f);
      ASSERT_EQ(unlink(fname.c_str()), 0);
    }
    ASSERT_EQ(rmdir(dname.c_str()), 0);
  }
}

using MaxDataTest = ResizeTest;

TEST_P(MaxDataTest, UseAllData) {
  constexpr size_t kBufSize = 1 << 20;
  constexpr size_t kFileSize = 20 * kBufSize;
  ASSERT_NO_FATAL_FAILURE(EnsureCanGrow());

  uint64_t disk_size = fs().options().device_block_count * fs().options().device_block_size;
  size_t metadata_size = fvm::MetadataSize(disk_size, fs().options().fvm_slice_size);

  ASSERT_GT(disk_size, metadata_size * 2);
  disk_size -= 2 * metadata_size;

  ASSERT_GT(disk_size, minfs::kMinfsMinimumSlices * fs().options().fvm_slice_size);
  disk_size -= minfs::kMinfsMinimumSlices * fs().options().fvm_slice_size;

  std::vector<uint8_t> buf(kBufSize);

  size_t f = 0;
  while (true) {
    std::cerr << "Creating 20 MB file " << f << std::endl;
    const std::string fname = GetPath(std::to_string(f));
    fbl::unique_fd fd(open(fname.c_str(), O_CREAT | O_RDWR | O_EXCL));
    if (!fd) {
      ASSERT_EQ(errno, ENOSPC);
      break;
    }
    f++;
    bool stop = false;
    ASSERT_EQ(ftruncate(fd.get(), kFileSize), 0);
    for (size_t done = 0; done < kFileSize;) {
      ssize_t r = write(fd.get(), buf.data(), std::min(kBufSize, kFileSize - done));
      if (r < 0) {
        ASSERT_EQ(errno, ENOSPC);
        stop = true;
        break;
      }
      done += r;
    }
    if (stop) {
      break;
    }
  }

  ASSERT_NO_FATAL_FAILURE(EnsureCannotGrow());

  if (ShouldRemount()) {
    std::cerr << "Unmounting, Verifying, Re-mounting..." << std::endl;
    EXPECT_EQ(fs().Unmount().status_value(), ZX_OK);
    EXPECT_EQ(fs().Fsck().status_value(), ZX_OK);
    EXPECT_EQ(fs().Mount().status_value(), ZX_OK);
  }

  size_t file_count = f;
  for (size_t f = 0; f < file_count; f++) {
    const std::string fname = GetPath(std::to_string(f));
    ASSERT_EQ(unlink(fname.c_str()), 0);
  }
}

std::string GetParamDescription(const testing::TestParamInfo<ParamType>& param) {
  std::stringstream s;
  s << std::get<0>(param.param) << (std::get<1>(param.param) ? "WithRemount" : "WithoutRemount");
  return s.str();
}

std::vector<ParamType> GetTestCombinationsForMaxInodeTest() {
  std::vector<ParamType> test_combinations;
  for (TestFilesystemOptions options : AllTestFilesystems()) {
    if (options.use_fvm && options.filesystem->GetTraits().supports_resize) {
      options.device_block_count = 1LLU << 15;
      options.device_block_size = 1LLU << 9;
      options.fvm_slice_size = 1LLU << 20;
      test_combinations.push_back(ParamType{options, false});
      if (options.filesystem->GetTraits().can_unmount) {
        test_combinations.push_back(ParamType{options, true});
      }
    }
  }
  return test_combinations;
}

std::vector<ParamType> GetTestCombinationsForMaxDataTest() {
  std::vector<ParamType> test_combinations;
  for (TestFilesystemOptions options : AllTestFilesystems()) {
    if (options.use_fvm && options.filesystem->GetTraits().supports_resize) {
      options.device_block_count = 1LLU << 17;
      options.device_block_size = 1LLU << 9;
      options.fvm_slice_size = 1LLU << 20;
      test_combinations.push_back(ParamType{options, false});
      if (options.filesystem->GetTraits().can_unmount) {
        test_combinations.push_back(ParamType{options, true});
      }
    }
  }
  return test_combinations;
}

INSTANTIATE_TEST_SUITE_P(/*no prefix*/, MaxInodeTest,
                         testing::ValuesIn(GetTestCombinationsForMaxInodeTest()),
                         GetParamDescription);

INSTANTIATE_TEST_SUITE_P(/*no prefix*/, MaxDataTest,
                         testing::ValuesIn(GetTestCombinationsForMaxDataTest()),
                         GetParamDescription);

}  // namespace
}  // namespace fs_test
