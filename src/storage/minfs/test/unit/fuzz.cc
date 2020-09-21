// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <block-client/cpp/fake-device.h>
#include <fuzzer/FuzzedDataProvider.h>

#include "minfs_private.h"

namespace minfs {
namespace {

using ::block_client::FakeBlockDevice;

enum class Operation {
  kFinished,  // Must be first to ensure each case finishes.
  kCreate,
  kRead,
  kWrite,
  kMaxValue = kWrite
};

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  static Minfs& fs = []() -> Minfs& {
    constexpr uint64_t kBlockCount = 1 << 17;
    auto device = std::make_unique<FakeBlockDevice>(kBlockCount, kMinfsBlockSize);
    std::unique_ptr<Bcache> bcache;
    ZX_ASSERT(Bcache::Create(std::move(device), kBlockCount, &bcache) == ZX_OK);
    ZX_ASSERT(Mkfs(bcache.get()) == ZX_OK);
    MountOptions options = {};
    std::unique_ptr<Minfs> fs;
    ZX_ASSERT(Minfs::Create(std::move(bcache), options, &fs) == ZX_OK);
    return *fs.release();
  }();

  fbl::RefPtr<VnodeMinfs> root;
  ZX_ASSERT(fs.VnodeGet(&root, kMinfsRootIno) == ZX_OK);

  std::array<fbl::RefPtr<VnodeMinfs>, 10> files;
  int open_files = 0;
  void* buffer = std::malloc(65536);
  std::vector<std::string> created_files;

  FuzzedDataProvider fuzzed_data(data, size);
  for (;;) {
    auto operation = fuzzed_data.ConsumeEnum<Operation>();
    switch (operation) {
      case Operation::kFinished: {
        for (auto& file : files) {
          if (file) {
            file->Close();
            file = nullptr;
          }
        }
        free(buffer);
        for (auto& name : created_files) {
          ZX_ASSERT(root->Unlink(name, /*must_be_dir=*/false) == ZX_OK);
        }
        ZX_ASSERT(fs.Info().alloc_block_count == 2);
        ZX_ASSERT(fs.Info().alloc_inode_count == 2);
        return 0;
      }
      case Operation::kCreate: {
        int index = fuzzed_data.ConsumeIntegralInRange<int>(0, files.size() - 1);
        std::string name = fuzzed_data.ConsumeRandomLengthString(NAME_MAX + 2);
        uint32_t mode = fuzzed_data.ConsumeIntegral<uint32_t>();
        if (files[index])
          --open_files;
        fbl::RefPtr<fs::Vnode> vnode;
        zx_status_t status = root->Create(&vnode, name, mode);
        if (status == ZX_OK) {
          created_files.push_back(std::move(name));
          if (files[index]) {
            files[index]->Close();
          } else {
            ++open_files;
          }
          files[index] = fbl::RefPtr<VnodeMinfs>::Downcast(std::move(vnode));
        }
        break;
      }
      case Operation::kRead: {
        if (open_files == 0)
          break;
        int index = fuzzed_data.ConsumeIntegralInRange<uint8_t>(0, open_files - 1);
        size_t read_len = fuzzed_data.ConsumeIntegralInRange<size_t>(0, 65536);
        size_t offset = fuzzed_data.ConsumeIntegral<size_t>();
        for (auto& file : files) {
          if (file && --index <= 0) {
            file->Read(buffer, read_len, offset, &read_len);
            break;
          }
        }
        break;
      }
      case Operation::kWrite: {
        if (open_files == 0)
          break;
        int index = fuzzed_data.ConsumeIntegralInRange<uint8_t>(0, open_files - 1);
        size_t write_len = fuzzed_data.ConsumeIntegralInRange<size_t>(0, 65536);
        size_t offset = fuzzed_data.ConsumeIntegral<size_t>();
        for (auto& file : files) {
          if (file && --index <= 0) {
            file->Write(buffer, write_len, offset, &write_len);
            break;
          }
        }
        break;
      }
    }
  }  // for (;;)
}

}  // namespace
}  // namespace minfs
