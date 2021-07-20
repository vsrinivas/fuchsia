// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fdio/io.h>

#include <vector>

#include <gtest/gtest.h>

#include "src/storage/blobfs/mount.h"
#include "src/storage/blobfs/test/blob_utils.h"
#include "src/storage/blobfs/test/integration/fdio_test.h"

namespace blobfs {
namespace {

using ::testing::UnitTest;

// With 32KB chunks coming from blobfs, we only have 160 page faults available for a 5MB file.
constexpr int kFileSize = 5 << 20;
constexpr int kChunkSize = 32 << 10;
constexpr int kReadsPerFile = kFileSize / kChunkSize;

class BlobfsMultithreadedSmokeTest : public FdioTest, public testing::WithParamInterface<int> {
 public:
  BlobfsMultithreadedSmokeTest() {
    MountOptions options;
    options.paging_threads = NumThreads();
    options.compression_settings = {CompressionAlgorithm::kChunked, 14};
    set_mount_options(options);
  }

  int NumThreads() const { return GetParam(); }
};

struct ReadLocation {
  size_t file;
  size_t offset;
};

void PerformReads(const ReadLocation* locations, size_t num_reads, const zx::vmo* vmos) {
  uint8_t ch;
  for (size_t i = 0; i < num_reads; ++i) {
    vmos[locations[i].file].read(&ch, locations[i].offset, 1);
  }
}

TEST_P(BlobfsMultithreadedSmokeTest, MultithreadedReads) {
  srand(testing::GTEST_FLAG(random_seed) + NumThreads());
  std::vector<std::unique_ptr<BlobInfo>> file_info;
  // Add more files for more threads. We'll need it for scaling up the number of available pages to
  // fault in.
  for (int i = 0; i < NumThreads(); ++i) {
    // A fairly sized blob that should get realistically compressed.
    std::unique_ptr<BlobInfo> info = GenerateRealisticBlob(".", kFileSize);
    fbl::unique_fd fd(openat(root_fd(), info->path, O_CREAT | O_RDWR));
    ASSERT_TRUE(fd.is_valid());
    ASSERT_EQ(ftruncate(fd.get(), info->size_data), 0);
    ASSERT_EQ(StreamAll(write, fd.get(), info->data.get(), info->size_data), 0)
        << "Failed to write Data";
    file_info.push_back(std::move(info));
  }

  std::vector<zx::vmo> vmos;
  for (const auto& info : file_info) {
    fbl::unique_fd fd(openat(root_fd(), info->path, O_RDONLY));
    ASSERT_TRUE(fd.is_valid());
    zx_handle_t handle;
    ASSERT_EQ(fdio_get_vmo_clone(fd.get(), &handle), ZX_OK);
    vmos.emplace_back(handle);
  }

  // Generate every page fault possible, then scramble them up.
  std::vector<ReadLocation> reads(static_cast<size_t>(NumThreads() * kReadsPerFile));
  for (size_t file_id = 0; file_id < vmos.size(); ++file_id) {
    for (size_t offset = 0; offset < kReadsPerFile; ++offset) {
      reads[file_id * kReadsPerFile + offset] = {file_id, offset * kChunkSize};
    }
  }
  ReadLocation tmp = reads[0];
  size_t location = 0;
  for (int i = 0; i < NumThreads() * kReadsPerFile - 1; ++i) {
    size_t next = rand() % reads.size();
    reads[location] = reads[next];
    tmp = reads[next];
    location = next;
  }

  std::vector<std::thread> threads;
  for (size_t i = 0; static_cast<int>(i) < NumThreads(); i++) {
    threads.emplace_back(PerformReads, &reads[i * kReadsPerFile], kReadsPerFile, vmos.data());
  }

  for (auto& t : threads) {
    t.join();
  }
}

INSTANTIATE_TEST_SUITE_P(/*no prefix*/, BlobfsMultithreadedSmokeTest, testing::Values(1, 2, 4),
                         testing::PrintToStringParamName());

}  // namespace
}  // namespace blobfs
