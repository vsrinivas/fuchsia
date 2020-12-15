// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <sys/stat.h>

#include <iterator>
#include <memory>
#include <utility>

#include <digest/digest.h>
#include <digest/merkle-tree.h>
#include <fbl/algorithm.h>
#include <fbl/function.h>
#include <fbl/string.h>
#include <fbl/string_buffer.h>
#include <fbl/string_printf.h>
#include <fbl/unique_fd.h>
#include <fs-management/mount.h>
#include <fs-test-utils/fixture.h>
#include <fs-test-utils/perftest.h>
#include <perftest/perftest.h>
#include <unittest/unittest.h>

#include "src/storage/blobfs/format.h"

namespace {

using digest::Digest;
using digest::MerkleTreeCreator;
using digest::MerkleTreeVerifier;
using fs_test_utils::Fixture;
using fs_test_utils::FixtureOptions;
using fs_test_utils::PerformanceTestOptions;
using fs_test_utils::TestCaseInfo;
using fs_test_utils::TestInfo;

// Supported read orders for this benchmark.
enum class ReadOrder {
  // Blobs are read in the order they were written
  kSequentialForward,
  // Blobs are read in the inverse order they were written
  kSequentialReverse,
  // Blobs are read in a random order
  kRandom,
};

// An in-memory representation of a blob.
struct BlobInfo {
  // Path to the generated blob.
  fbl::StringBuffer<fs_test_utils::kPathSize> path;

  std::unique_ptr<char[]> merkle;
  size_t size_merkle;

  std::unique_ptr<char[]> data;
  size_t size_data;
};

// Describes the parameters of the test case.
struct BlobfsInfo {
  // Total number of blobs in blobfs.
  ssize_t blob_count;

  // Size in bytes of each blob in BlobFs.
  size_t blob_size;

  // Path to every blob in Blobfs
  fbl::Vector<fbl::StringBuffer<fs_test_utils::kPathSize>> paths;

  // Order in which to read the blobs from blobfs.
  fbl::Vector<uint64_t> path_index;
};

// Helper for streaming operations (such as read, write) which may need to be
// repeated multiple times.
template <typename T, typename U>
inline int StreamAll(T func, int fd, U* buf, size_t max) {
  size_t n = 0;
  while (n != max) {
    ssize_t d = func(fd, &buf[n], max - n);
    if (d < 0) {
      return -1;
    }
    n += d;
  }
  return 0;
}

// Get a readable name for a given number of bytes.
fbl::String GetNameForSize(size_t size_in_bytes) {
  static const char* const kUnits[] = {"bytes", "Kbytes", "Mbytes", "Gbytes"};
  size_t current_unit = 0;
  size_t current_size = size_in_bytes;
  size_t size;
  while (current_unit < std::size(kUnits) && current_size >= (1u << (10 * (current_unit + 1)))) {
    current_size = current_size / (1 << 10 * current_unit);
    ++current_unit;
  }

  size = (size_in_bytes >> (10 * current_unit));
  return fbl::StringPrintf("%lu%s", size, kUnits[current_unit]);
}

fbl::String GetNameForOrder(ReadOrder order) {
  switch (order) {
    case ReadOrder::kSequentialForward:
      return "Sequential";
    case ReadOrder::kSequentialReverse:
      return "Reverse";
    case ReadOrder::kRandom:
      return "Random";
  }

  return "";
}

// Creates a an in memory blob.
bool MakeBlob(fbl::String fs_path, size_t blob_size, unsigned int* seed,
              std::unique_ptr<BlobInfo>* out) {
  BEGIN_HELPER;
  // Generate a Blob of random data
  fbl::AllocChecker ac;
  std::unique_ptr<BlobInfo> info(new (&ac) BlobInfo);
  EXPECT_EQ(ac.check(), true);
  info->data.reset(new (&ac) char[blob_size]);
  EXPECT_EQ(ac.check(), true);
  // rand_r produces a cyclic sequence, in order to avoid hitting that cap
  // and generating identical blobs, we avoid consuming an element of the
  // sequence for each byte. We did hit this issue, which translates into
  // test failures.
  unsigned int initial_seed = rand_r(seed);
  for (size_t i = 0; i < blob_size; i++) {
    info->data[i] = static_cast<char>(rand_r(&initial_seed));
  }
  info->size_data = blob_size;

  // Generate the Merkle Tree
  Digest digest;
  std::unique_ptr<uint8_t[]> tree;
  ASSERT_EQ(MerkleTreeCreator::Create(info->data.get(), info->size_data, &tree, &info->size_merkle,
                                      &digest),
            ZX_OK, "Couldn't create Merkle Tree");
  info->merkle.reset(reinterpret_cast<char*>(tree.release()));
  info->path.AppendPrintf("%s/%s", fs_path.c_str(), digest.ToString().c_str());

  // Sanity-check the merkle tree
  ASSERT_EQ(MerkleTreeVerifier::Verify(info->data.get(), info->size_data, 0, info->size_data,
                                       info->merkle.get(), info->size_merkle, digest),
            ZX_OK, "Failed to validate Merkle Tree");

  *out = std::move(info);
  END_HELPER;
}

// Returns a path within the fs such that it is a valid blobpath.
// The generated path is 'root_path/0....0'.
fbl::String GetNegativeLookupPath(const fbl::String& fs_path) {
  fbl::String negative_path =
      fbl::StringPrintf("%s/%*d", fs_path.c_str(), static_cast<int>(2 * digest::kSha256Length), 0);
  return negative_path;
}

class BlobfsTest {
 public:
  BlobfsTest(BlobfsInfo&& info) : info_(std::move(info)) {}

  // Measure how much time each of the operations in the Fs takes, for a known size.
  // First we add as many blobs as we need to, and then, we proceed to execute each operation.
  bool ApiTest(perftest::RepeatState* state, Fixture* fixture) {
    BEGIN_HELPER;

    // How many blobs do we need to add.
    std::unique_ptr<BlobInfo> new_blob;

    for (int64_t curr = 0; curr < info_.blob_count; ++curr) {
      MakeBlob(fixture->fs_path(), info_.blob_size, fixture->mutable_seed(), &new_blob);
      fbl::unique_fd fd(open(new_blob->path.c_str(), O_CREAT | O_RDWR));
      ASSERT_TRUE(fd, strerror(errno));
      ASSERT_EQ(ftruncate(fd.get(), info_.blob_size), 0, strerror(errno));
      ASSERT_EQ(StreamAll(write, fd.get(), new_blob->data.get(), new_blob->size_data), 0,
                strerror(errno));
      info_.paths.push_back(new_blob->path);
      info_.path_index.push_back(curr);
      new_blob.reset();
    }

    fbl::AllocChecker ac;
    std::unique_ptr<char[]> buffer(new (&ac) char[info_.blob_size]);
    ASSERT_TRUE(ac.check());

    state->DeclareStep("generate_blob");
    state->DeclareStep("create");
    state->DeclareStep("truncate");
    state->DeclareStep("write");
    state->DeclareStep("close_write_fd");
    state->DeclareStep("open");
    state->DeclareStep("read");
    state->DeclareStep("unlink");
    state->DeclareStep("close_read_fd");

    // At this specific state, measure how much time in average it takes to perform each of the
    // operations declared.
    while (state->KeepRunning()) {
      MakeBlob(fixture->fs_path(), info_.blob_size, fixture->mutable_seed(), &new_blob);
      state->NextStep();

      fbl::unique_fd fd(open(new_blob->path.c_str(), O_CREAT | O_RDWR));
      ASSERT_TRUE(fd);
      state->NextStep();

      ASSERT_EQ(ftruncate(fd.get(), info_.blob_size), 0);
      state->NextStep();

      ASSERT_EQ(StreamAll(write, fd.get(), new_blob->data.get(), info_.blob_size), 0,
                "Failed to write Data");
      // Force pending writes to be sent to the underlying device.
      ASSERT_EQ(fsync(fd.get()), 0);
      state->NextStep();

      ASSERT_EQ(close(fd.release()), 0);
      state->NextStep();

      fd.reset(open(new_blob->path.c_str(), O_RDONLY));
      ASSERT_TRUE(fd);
      state->NextStep();

      ASSERT_EQ(StreamAll(read, fd.get(), &buffer[0], info_.blob_size), 0);
      ASSERT_EQ(memcmp(buffer.get(), new_blob->data.get(), new_blob->size_data), 0);
      state->NextStep();

      unlink(new_blob->path.c_str());
      ASSERT_EQ(fsync(fd.get()), 0);

      state->NextStep();
      ASSERT_EQ(close(fd.release()), 0);
    }
    END_HELPER;
  }

  // After doing the API test, we use the written blobs to measure, lookup, negative-lookup
  // read
  bool ReadTest(ReadOrder order, perftest::RepeatState* state, Fixture* fixture) {
    BEGIN_HELPER;
    state->DeclareStep("lookup");
    state->DeclareStep("read");
    state->DeclareStep("negative_lookup");
    ASSERT_EQ(info_.path_index.size(), info_.paths.size());
    ASSERT_GT(info_.path_index.size(), 0);
    SortPathsByOrder(order, fixture->mutable_seed());

    fbl::AllocChecker ac;
    std::unique_ptr<char[]> buffer(new (&ac) char[info_.blob_size]);
    ASSERT_TRUE(ac.check());

    uint64_t current = 0;
    fbl::String negative_path = GetNegativeLookupPath(fixture->fs_path());

    while (state->KeepRunning()) {
      size_t path_index = info_.path_index[current % info_.paths.size()];
      fbl::unique_fd fd(open(info_.paths[path_index].c_str(), O_RDONLY));
      ASSERT_TRUE(fd);
      state->NextStep();
      ASSERT_EQ(StreamAll(read, fd.get(), &buffer[0], info_.blob_size), 0);
      state->NextStep();
      fbl::unique_fd no_fd(open(negative_path.c_str(), O_RDONLY));
      ASSERT_FALSE(no_fd);
      ++current;
    }
    END_HELPER;
  }

 private:
  void SortPathsByOrder(ReadOrder order, unsigned int* seed) {
    switch (order) {
      case ReadOrder::kSequentialForward:
        for (uint64_t curr = 0; curr < info_.paths.size(); ++curr) {
          info_.path_index[curr] = curr;
        }
        break;

      case ReadOrder::kSequentialReverse:
        for (uint64_t curr = 0; curr < info_.paths.size(); ++curr) {
          info_.path_index[curr] = info_.paths.size() - curr - 1;
        }
        break;

      case ReadOrder::kRandom:
        int64_t swaps = info_.paths.size();
        while (swaps > 0) {
          size_t src = rand_r(seed) % info_.paths.size();
          size_t target = rand_r(seed) % info_.paths.size();
          info_.path_index[src] = info_.path_index[target];
          info_.path_index[target] = info_.path_index[src];
          --swaps;
        }
        break;
    }
  }

  BlobfsInfo info_;
};

bool RunBenchmark(int argc, char** argv) {
  FixtureOptions f_opts = FixtureOptions::Default(DISK_FORMAT_BLOBFS);
  PerformanceTestOptions p_opts;
  // 30 Samples for each operation at each stage.
  constexpr uint32_t kSampleCount = 100;
  const size_t blob_sizes[] = {
      128,          // 128 b
      128 * 1024,   // 128 Kb
      1024 * 1024,  // 1 MB
  };
  const size_t blob_counts[] = {
      10,
      100,
      1000,
      10000,
  };
  const ReadOrder orders[] = {
      ReadOrder::kSequentialForward,
      ReadOrder::kSequentialReverse,
      ReadOrder::kRandom,
  };

  if (!fs_test_utils::ParseCommandLineArgs(argc, argv, &f_opts, &p_opts)) {
    return false;
  }

  fbl::Vector<TestCaseInfo> testcases;
  fbl::Vector<BlobfsTest> blobfs_tests;
  size_t test_index = 0;
  for (auto blob_size : blob_sizes) {
    for (auto blob_count : blob_counts) {
      // Skip the largest blob size/count combination because it
      // increases the overall running time too much.
      if (blob_size >= 1024 * 1024 && blob_count >= 10000) {
        continue;
      }
      BlobfsInfo fs_info;
      fs_info.blob_count = (p_opts.is_unittest) ? 1 : blob_count;
      fs_info.blob_size = blob_size;
      blobfs_tests.push_back(std::move(fs_info));
      TestCaseInfo testcase;
      testcase.teardown = false;
      testcase.sample_count = kSampleCount;

      fbl::String size = GetNameForSize(blob_size);

      TestInfo api_test;
      api_test.name = fbl::StringPrintf("%s/%s/%luBlobs/Api", disk_format_string_[f_opts.fs_type],
                                        size.c_str(), blob_count);
      // There should be enough space for each blob, the merkle tree nodes, and the inodes.
      api_test.required_disk_space =
          blob_count * (blob_size + 2 * digest::kDefaultNodeSize + blobfs::kBlobfsInodeSize);
      api_test.test_fn = [test_index, &blobfs_tests](perftest::RepeatState* state,
                                                     fs_test_utils::Fixture* fixture) {
        return blobfs_tests[test_index].ApiTest(state, fixture);
      };
      testcase.tests.push_back(std::move(api_test));

      if (blob_count > 0) {
        for (auto order : orders) {
          TestInfo read_test;
          read_test.name =
              fbl::StringPrintf("%s/%s/%luBlobs/Read%s", disk_format_string_[f_opts.fs_type],
                                size.c_str(), blob_count, GetNameForOrder(order).c_str());
          read_test.test_fn = [test_index, order, &blobfs_tests](perftest::RepeatState* state,
                                                                 fs_test_utils::Fixture* fixture) {
            return blobfs_tests[test_index].ReadTest(order, state, fixture);
          };
          read_test.required_disk_space =
              blob_count * (blob_size + 2 * digest::kDefaultNodeSize + blobfs::kBlobfsInodeSize);
          testcase.tests.push_back(std::move(read_test));
        }
      }
      testcases.push_back(std::move(testcase));
      ++test_index;
    }
  }

  return fs_test_utils::RunTestCases(f_opts, p_opts, testcases);
}

}  // namespace

int main(int argc, char** argv) {
  return fs_test_utils::RunWithMemFs([argc, argv]() { return RunBenchmark(argc, argv) ? 0 : -1; });
}
