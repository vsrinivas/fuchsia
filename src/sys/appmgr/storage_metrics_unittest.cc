// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/appmgr/storage_metrics.h"

#include <fcntl.h>
#include <lib/fdio/namespace.h>
#include <lib/fpromise/single_threaded_executor.h>
#include <lib/inspect/cpp/hierarchy.h>
#include <lib/inspect/cpp/inspector.h>
#include <lib/inspect/cpp/reader.h>
#include <lib/sync/completion.h>

#include <cstdint>

#include <fbl/unique_fd.h>
#include <gtest/gtest.h>

#include "src/lib/files/directory.h"
#include "src/lib/files/file.h"
#include "src/lib/files/path.h"
#include "src/lib/fxl/strings/join_strings.h"
#include "src/lib/testing/loop_fixture/real_loop_fixture.h"
#include "src/storage/memfs/scoped_memfs.h"

class StorageMetricsTest : public ::testing::Test {
 public:
  static constexpr const char* kTestRoot = "/test_storage";
  static constexpr const char* kPersistentPath = "/test_storage/persistent";
  static constexpr const char* kCachePath = "/test_storage/cache";
  static constexpr const char* kInspectNodeName = "storage_metrics";

  StorageMetricsTest() : loop_(async::Loop(&kAsyncLoopConfigAttachToCurrentThread)) {}

  void SetUp() override {
    testing::Test::SetUp();
    ASSERT_EQ(ZX_OK, loop_.StartThread());

    zx::result<ScopedMemfs> memfs = ScopedMemfs::CreateMountedAt(loop_.dispatcher(), kTestRoot);
    ASSERT_TRUE(memfs.is_ok());
    memfs_ = std::make_unique<ScopedMemfs>(std::move(*memfs));

    files::CreateDirectory(kPersistentPath);
    files::CreateDirectory(kCachePath);
    std::vector<std::string> watch = {kPersistentPath, kCachePath};
    // The dispatcher is used only for delaying calls. Setting it as null here since we don't
    // want any delayed actions and it would actually deadlock if we set it share the existing
    // loop_ dispatcher. Best to have it fail fast if anyone tries to do that.
    metrics_ = std::make_unique<StorageMetrics>(std::move(watch),
                                                inspector_.GetRoot().CreateChild(kInspectNodeName));
  }
  // Set up the async loop, create memfs, install memfs at /hippo_storage
  void TearDown() override {
    memfs_->set_cleanup_timeout(zx::sec(5));
    memfs_.reset();
  }

  // Grabs a new Hierarchy snapshot from Inspect.
  void GetHierarchy(inspect::Hierarchy* hierarchy_ptr) {
    fpromise::single_threaded_executor executor;
    fpromise::result<inspect::Hierarchy> hierarchy;
    executor.schedule_task(
        inspect::ReadFromInspector(inspector_).then([&](fpromise::result<inspect::Hierarchy>& res) {
          hierarchy = std::move(res);
        }));
    executor.run();
    ASSERT_TRUE(hierarchy.is_ok());
    *hierarchy_ptr = hierarchy.take_value();
  }

  // Rebuilds a UsageMap from the Inspect data. Do all the heavy lifting here so that the tests can
  // focus on verifying the right values.
  void GetUsageMap(StorageMetrics::UsageMap* usage, const std::string& path) {
    inspect::Hierarchy hierarchy;
    GetHierarchy(&hierarchy);

    *usage = StorageMetrics::UsageMap();
    std::vector<inspect::Hierarchy> child_nodes = hierarchy.take_children();
    auto it = std::find_if(
        child_nodes.begin(), child_nodes.end(),
        [](const inspect::Hierarchy& node) { return node.name() == kInspectNodeName; });
    child_nodes = it->take_children();

    for (auto& units : child_nodes) {
      if (units.name() == "inodes") {
        for (auto& child : units.take_children()) {
          if (child.name() != path) {
            continue;
          }
          for (auto& property : child.node_ptr()->take_properties()) {
            usage->AddForKey(property.name(),
                             {0, property.Get<inspect::UintPropertyValue>().value()});
          }
        }
      } else if (units.name() == "bytes") {
        for (auto& child : units.take_children()) {
          if (child.name() != path) {
            continue;
          }
          for (auto& property : child.node_ptr()->take_properties()) {
            usage->AddForKey(property.name(),
                             {property.Get<inspect::UintPropertyValue>().value(), 0});
          }
        }
      } else {
        ASSERT_TRUE(false) << "Unexpected child node: " << units.name();
      }
    }
  }

  void AggregateStorage() { metrics_->PollStorage(); }

 protected:
  inspect::Inspector inspector_;
  std::unique_ptr<StorageMetrics> metrics_;

 private:
  async::Loop loop_;
  std::unique_ptr<ScopedMemfs> memfs_;
};

// Basic test with two components.
TEST_F(StorageMetricsTest, TwoComponents) {
  // Two components each with a single file. One empty, one at the minimum size.
  files::CreateDirectory(files::JoinPath(kPersistentPath, "12345"));
  files::CreateDirectory(files::JoinPath(kPersistentPath, "67890"));
  {
    fbl::unique_fd fd(open(files::JoinPath(kPersistentPath, "12345/afile").c_str(),
                           O_RDWR | O_CREAT, S_IRUSR | S_IWUSR));
    ASSERT_GE(fd.get(), 0);
    ASSERT_EQ(write(fd.get(), "1", 1), 1);
  }
  {
    fbl::unique_fd fd(open(files::JoinPath(kPersistentPath, "67890/other").c_str(),
                           O_RDWR | O_CREAT, S_IRUSR | S_IWUSR));
    ASSERT_GE(fd.get(), 0);
  }

  AggregateStorage();
  StorageMetrics::UsageMap usage;
  ASSERT_NO_FATAL_FAILURE(GetUsageMap(&usage, kPersistentPath));

  // Expect one file each.
  ASSERT_EQ(usage.map().at("12345").inodes, 1ul);
  ASSERT_EQ(usage.map().at("67890").inodes, 1ul);

  // Expect one file with non-zero size and one with zero size.
  ASSERT_GT(usage.map().at("12345").bytes, 0ul);
  ASSERT_EQ(usage.map().at("67890").bytes, 0ul);
}

// Verify that we recurse into subdirectories.
TEST_F(StorageMetricsTest, CountSubdirectories) {
  files::CreateDirectory(files::JoinPath(kPersistentPath, "12345"));
  files::CreateDirectory(files::JoinPath(kPersistentPath, "12345/subdir"));
  {
    fbl::unique_fd fd(open(files::JoinPath(kPersistentPath, "12345/afile").c_str(),
                           O_RDWR | O_CREAT, S_IRUSR | S_IWUSR));
    ASSERT_GE(fd.get(), 0);
  }
  {
    fbl::unique_fd fd(open(files::JoinPath(kPersistentPath, "12345/subdir/other").c_str(),
                           O_RDWR | O_CREAT, S_IRUSR | S_IWUSR));
    ASSERT_GE(fd.get(), 0);
  }

  AggregateStorage();
  StorageMetrics::UsageMap usage;
  ASSERT_NO_FATAL_FAILURE(GetUsageMap(&usage, kPersistentPath));

  // 3 Total files
  ASSERT_EQ(usage.map().at("12345").inodes, 3ul);
  // Specifically not testing byte count here. Memfs diverges from Minfs (and probably all normal
  // filesystems) in that it does not reserve blocks for directory listings, since it lives in
  // memory anyways, it just puts it on the heap.
}

// Ensure that we're counting reserved blocks and not just bytes usage.
TEST_F(StorageMetricsTest, IncrementByBlocks) {
  files::CreateDirectory(files::JoinPath(kPersistentPath, "12345"));
  {
    fbl::unique_fd fd(open(files::JoinPath(kPersistentPath, "12345/afile").c_str(),
                           O_RDWR | O_CREAT, S_IRUSR | S_IWUSR));
    ASSERT_GE(fd.get(), 0);
    ASSERT_EQ(write(fd.get(), "1", 1), 1);
  }

  AggregateStorage();
  StorageMetrics::UsageMap usage;
  ASSERT_NO_FATAL_FAILURE(GetUsageMap(&usage, kPersistentPath));

  // Check block size, the one byte file will allocate an entire block.
  ASSERT_EQ(usage.map().at("12345").inodes, 1ul);
  ASSERT_GT(usage.map().at("12345").bytes, 0ul);
  size_t block_size = usage.map().at("12345").bytes;
  ASSERT_GT(block_size, 1ul) << "Memfs block size is 1, so we can't verify block increments.";

  // Reopen file and make it 1 byte longer, it should not change the size.
  {
    fbl::unique_fd fd(open(files::JoinPath(kPersistentPath, "12345/afile").c_str(), O_RDWR));
    ASSERT_GE(fd.get(), 0);
    ASSERT_EQ(write(fd.get(), "12", 2), 2);
  }

  AggregateStorage();
  ASSERT_NO_FATAL_FAILURE(GetUsageMap(&usage, kPersistentPath));
  ASSERT_EQ(usage.map().at("12345").bytes, block_size);

  // Reopen file and make it block_size + 1 to make the result 2 * block_size
  {
    fbl::unique_fd fd(open(files::JoinPath(kPersistentPath, "12345/afile").c_str(), O_RDWR));
    ASSERT_GE(fd.get(), 0);
    const std::string data = "1234567890";
    size_t length = block_size + 1;
    while (length > 0) {
      // Cast ssize_t to match types with write(). Both inputs should be relatively small.
      ssize_t to_write = static_cast<ssize_t>(std::min(data.length(), length));
      ASSERT_GT(to_write, 0) << "Attempting to write a negative size";
      ASSERT_EQ(write(fd.get(), data.c_str(), to_write), to_write);
      length -= to_write;
    }
  }
  AggregateStorage();
  ASSERT_NO_FATAL_FAILURE(GetUsageMap(&usage, kPersistentPath));
  ASSERT_EQ(usage.map().at("12345").bytes, block_size * 2);
}

// Empty component dir
TEST_F(StorageMetricsTest, EmptyComponent) {
  files::CreateDirectory(files::JoinPath(kPersistentPath, "12345"));
  AggregateStorage();
  StorageMetrics::UsageMap usage;
  ASSERT_NO_FATAL_FAILURE(GetUsageMap(&usage, kPersistentPath));
  ASSERT_EQ(usage.map().at("12345").inodes, 0ul);
  ASSERT_EQ(usage.map().at("12345").bytes, 0ul);
}

// Mix cache and persistent directories
TEST_F(StorageMetricsTest, MultipleWatchPaths) {
  files::CreateDirectory(files::JoinPath(kPersistentPath, "12345"));
  files::CreateDirectory(files::JoinPath(kCachePath, "12345"));
  {
    fbl::unique_fd fd(open(files::JoinPath(kPersistentPath, "12345/afile").c_str(),
                           O_RDWR | O_CREAT, S_IRUSR | S_IWUSR));
    ASSERT_GE(fd.get(), 0);
  }
  {
    fbl::unique_fd fd(open(files::JoinPath(kCachePath, "12345/other").c_str(), O_RDWR | O_CREAT,
                           S_IRUSR | S_IWUSR));
    ASSERT_GE(fd.get(), 0);
  }
  {
    fbl::unique_fd fd(open(files::JoinPath(kCachePath, "12345/third").c_str(), O_RDWR | O_CREAT,
                           S_IRUSR | S_IWUSR));
    ASSERT_GE(fd.get(), 0);
  }

  AggregateStorage();
  StorageMetrics::UsageMap persistent_usage;
  ASSERT_NO_FATAL_FAILURE(GetUsageMap(&persistent_usage, kPersistentPath));

  ASSERT_EQ(persistent_usage.map().at("12345").inodes, 1ul);

  StorageMetrics::UsageMap cache_usage;
  ASSERT_NO_FATAL_FAILURE(GetUsageMap(&cache_usage, kCachePath));

  ASSERT_EQ(cache_usage.map().at("12345").inodes, 2ul);
}

// Nested Realm gets included.
TEST_F(StorageMetricsTest, RealmNesting) {
  files::CreateDirectory(files::JoinPath(kPersistentPath, "r"));
  files::CreateDirectory(files::JoinPath(kPersistentPath, "r/sys"));
  files::CreateDirectory(files::JoinPath(kPersistentPath, "r/sys/12345"));
  files::CreateDirectory(files::JoinPath(kPersistentPath, "r/sys/r"));
  files::CreateDirectory(files::JoinPath(kPersistentPath, "r/sys/r/admin/67890"));
  {
    fbl::unique_fd fd(open(files::JoinPath(kPersistentPath, "r/sys/12345/afile").c_str(),
                           O_RDWR | O_CREAT, S_IRUSR | S_IWUSR));
    ASSERT_GE(fd.get(), 0);
  }
  {
    fbl::unique_fd fd(open(files::JoinPath(kPersistentPath, "r/sys/r/admin/67890/other").c_str(),
                           O_RDWR | O_CREAT, S_IRUSR | S_IWUSR));
    ASSERT_GE(fd.get(), 0);
  }

  AggregateStorage();
  StorageMetrics::UsageMap persistent_usage;
  ASSERT_NO_FATAL_FAILURE(GetUsageMap(&persistent_usage, kPersistentPath));

  ASSERT_EQ(persistent_usage.map().at("12345").inodes, 1ul);
  ASSERT_EQ(persistent_usage.map().at("67890").inodes, 1ul);
}
