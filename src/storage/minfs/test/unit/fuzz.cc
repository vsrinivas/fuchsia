// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>

#include <cinttypes>
#include <vector>

#include <fuzzer/FuzzedDataProvider.h>

#include "src/lib/storage/block_client/cpp/fake_block_device.h"
#include "src/storage/minfs/runner.h"

namespace minfs {
namespace {

constexpr uint64_t kBlockCount = (1u << 17);
constexpr size_t kMaxOpenFiles = 10u;
constexpr size_t kMaxReadWriteBytes = (1u << 16);
constexpr uint32_t kExpectedAllocBlockCount = 2u;
constexpr uint32_t kExpectedAllocInodeCount = 2u;

using ::block_client::FakeBlockDevice;

enum class Operation {
  kFinished,  // Must be first to ensure each case finishes.
  kCreate,
  kRead,
  kWrite,
  kMaxValue = kWrite
};

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  std::unique_ptr<Runner> runner;
  {
    std::unique_ptr device = std::make_unique<FakeBlockDevice>(kBlockCount, kMinfsBlockSize);
    zx::result bcache_or = Bcache::Create(std::move(device), kBlockCount);
    ZX_ASSERT_MSG(bcache_or.is_ok(), "Failed to create Bcache: %s", bcache_or.status_string());
    zx::result mkfs_result = Mkfs(bcache_or.value().get());
    ZX_ASSERT_MSG(mkfs_result.is_ok(), "Mkfs failure: %s", mkfs_result.status_string());
    MountOptions options = {};
    zx::result fs_or = Runner::Create(loop.dispatcher(), std::move(bcache_or.value()), options);
    ZX_ASSERT_MSG(fs_or.is_ok(), "Failed to create Runner: %s", fs_or.status_string());
    runner = std::move(fs_or.value());
  }
  ZX_ASSERT(runner != nullptr);
  Minfs& fs = runner->minfs();

  fbl::RefPtr<fs::Vnode> root_node;
  {
    zx::result root_or = fs.VnodeGet(kMinfsRootIno);
    ZX_ASSERT_MSG(root_or.is_ok(), "Failed to get root node: %s", root_or.status_string());
    root_node = std::move(root_or.value());
  }

  std::vector<fbl::RefPtr<fs::Vnode>> open_files;
  open_files.reserve(kMaxOpenFiles);
  std::vector<std::string> created_files;
  std::vector<uint8_t> buffer(kMaxReadWriteBytes);

  FuzzedDataProvider fuzzed_data(data, size);
  for (;;) {
    auto operation = fuzzed_data.ConsumeEnum<Operation>();
    switch (operation) {
      case Operation::kFinished: {
        for (auto& file : open_files) {
          zx_status_t status = file->Close();
          ZX_ASSERT_MSG(status == ZX_OK, "Failed to close file: %s", zx_status_get_string(status));
        }
        // Make sure we drop the file node refcounts before destroying Minfs.
        open_files = {};
        for (const auto& name : created_files) {
          zx_status_t status = root_node->Unlink(name, /*must_be_dir=*/false);
          ZX_ASSERT_MSG(status == ZX_OK, "Failed to unlink file: %s", zx_status_get_string(status));
        }
        // Make sure we don't hold onto the root Vnode after we destroy Minfs.
        root_node = nullptr;
        // Fsck should always pass regardless of if we flushed any outstanding transactions or not.
        if (fuzzed_data.ConsumeBool()) {
          zx::result status = fs.BlockingJournalSync();
          ZX_ASSERT_MSG(status.is_ok(), "Failed to sync: %s", status.status_string());
        }
        loop.RunUntilIdle();
        // Validate the final allocated block/inode counts in the superblock.
        ZX_ASSERT_MSG(fs.Info().alloc_block_count == kExpectedAllocBlockCount,
                      "Incorrect allocated block count - actual: %" PRIu32 ", expected: %" PRIu32,
                      fs.Info().alloc_block_count, kExpectedAllocBlockCount);
        ZX_ASSERT_MSG(fs.Info().alloc_inode_count == kExpectedAllocInodeCount,
                      "Incorrect allocated block count - actual: %" PRIu32 ", expected: %" PRIu32,
                      fs.Info().alloc_inode_count, kExpectedAllocInodeCount);
        // Destroy Minfs and run Fsck.
        std::unique_ptr<Bcache> bcache = Runner::Destroy(std::move(runner));
        auto fsck_result = Fsck(std::move(bcache), FsckOptions{.read_only = true, .quiet = true});
        ZX_ASSERT_MSG(fsck_result.is_ok(), "Fsck failure: %s", fsck_result.status_string());
        return 0;
      }
      case Operation::kCreate: {
        ZX_ASSERT(open_files.size() <= kMaxOpenFiles);
        // If we have |kMaxOpenFiles| files still open, close and remove one at random.
        if (open_files.size() == kMaxOpenFiles) {
          ssize_t file_index = fuzzed_data.ConsumeIntegralInRange<ssize_t>(0, kMaxOpenFiles - 1);
          zx_status_t status = open_files[file_index]->Close();
          ZX_ASSERT_MSG(status == ZX_OK, "Failed to close file: %s", zx_status_get_string(status));
          open_files.erase(open_files.begin() + file_index);
        }
        // Try to create a file with a randomized path/mode.
        std::string name = fuzzed_data.ConsumeRandomLengthString(NAME_MAX + 2);
        uint32_t mode = fuzzed_data.ConsumeIntegral<uint32_t>();
        fbl::RefPtr<fs::Vnode> vnode;
        zx_status_t status = root_node->Create(name, mode, &vnode);
        // It's okay if we failed to create the file, since name/mode may be invalid.
        if (status == ZX_OK) {
          created_files.push_back(std::move(name));
          open_files.push_back(std::move(vnode));
        }
        break;
      }
      case Operation::kRead: {
        if (open_files.empty())
          break;
        size_t file_index = fuzzed_data.ConsumeIntegralInRange<size_t>(0, open_files.size() - 1);
        size_t read_len = fuzzed_data.ConsumeIntegralInRange<size_t>(0, buffer.size());
        size_t offset = fuzzed_data.ConsumeIntegral<size_t>();
        open_files[file_index]->Read(buffer.data(), read_len, offset, &read_len);
        break;
      }
      case Operation::kWrite: {
        if (open_files.empty())
          break;
        size_t file_index = fuzzed_data.ConsumeIntegralInRange<size_t>(0, open_files.size() - 1);
        size_t write_len = fuzzed_data.ConsumeIntegralInRange<size_t>(0, buffer.size());
        size_t offset = fuzzed_data.ConsumeIntegral<size_t>();
        open_files[file_index]->Write(buffer.data(), write_len, offset, &write_len);
        break;
      }
    }
  }  // for (;;)
}

}  // namespace minfs
