// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "inspect-manager.h"

#include <fcntl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/namespace.h>
#include <lib/fpromise/single_threaded_executor.h>
#include <lib/inspect/cpp/reader.h>
#include <sys/stat.h>

#include <fbl/unique_fd.h>
#include <zxtest/zxtest.h>

#include "src/lib/fxl/strings/substitute.h"
#include "src/lib/storage/vfs/cpp/remote_dir.h"
#include "src/storage/memfs/scoped_memfs.h"

namespace {

constexpr char kTmpfsPath[] = "/fshost-inspect-tmp";

inspect::Hierarchy ReadInspect(const inspect::Inspector& inspector) {
  // take_value() will assert if the promise result is an error.
  return fpromise::run_single_threaded(inspect::ReadFromInspector(inspector)).take_value();
}

class InspectManagerTest : public zxtest::Test {
 public:
  InspectManagerTest() : memfs_loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {}

  void SetUp() override {
    ASSERT_EQ(memfs_loop_.StartThread(), ZX_OK);
    zx::result<ScopedMemfs> memfs =
        ScopedMemfs::CreateMountedAt(memfs_loop_.dispatcher(), kTmpfsPath);
    ASSERT_TRUE(memfs.is_ok());
    memfs_ = std::make_unique<ScopedMemfs>(std::move(*memfs));
  }

  void TearDown() override { memfs_.reset(); }

 protected:
  static fidl::ClientEnd<fuchsia_io::Directory> GetDir() {
    auto endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
    EXPECT_TRUE(endpoints.is_ok());
    auto [client, server] = *std::move(endpoints);
    EXPECT_EQ(ZX_OK, fdio_open(kTmpfsPath,
                               static_cast<uint32_t>(fuchsia_io::wire::OpenFlags::kRightReadable |
                                                     fuchsia_io::wire::OpenFlags::kRightExecutable),
                               server.TakeChannel().release()));
    return std::move(client);
  }

  static void AddFile(const std::string& path, size_t content_size) {
    std::string contents(content_size, 'X');
    fbl::unique_fd fd(open(fxl::Substitute("$0/$1", kTmpfsPath, path).c_str(), O_RDWR | O_CREAT,
                           S_IRUSR | S_IWUSR));
    ASSERT_TRUE(fd.is_valid());
    ASSERT_EQ(write(fd.get(), contents.c_str(), content_size), content_size);
  }

  static void MakeDir(const std::string& path) {
    ASSERT_EQ(0, mkdir(fxl::Substitute("$0/$1", kTmpfsPath, path).c_str(), 0666));
  }

  static void AssertValue(const inspect::Hierarchy& hierarchy, const std::vector<std::string>& path,
                          size_t expected, size_t other_children = 0) {
    auto file_node = hierarchy.GetByPath(path);
    EXPECT_NOT_NULL(file_node);
    ASSERT_EQ(1, file_node->node().properties().size());
    ASSERT_EQ(other_children, file_node->children().size());
    auto& size_property = file_node->node().properties()[0];
    EXPECT_EQ("size", size_property.name());
    EXPECT_EQ(expected, size_property.Get<inspect::UintPropertyValue>().value());
  }

  async::Loop memfs_loop_;
  std::unique_ptr<ScopedMemfs> memfs_;
};

TEST_F(InspectManagerTest, ServeStats) {
  // Initialize test directory
  MakeDir("a");
  MakeDir("a/b");
  MakeDir("a/c");
  AddFile("top.txt", 12);
  AddFile("a/a.txt", 13);
  AddFile("a/b/b.txt", 14);
  AddFile("a/c/c.txt", 15);
  AddFile("a/c/d.txt", 16);

  // Serve inspect stats.
  auto inspect_manager = fshost::FshostInspectManager();
  auto test_dir = GetDir();
  inspect_manager.ServeStats("test_dir", std::move(test_dir));

  // Read inspect
  inspect::Hierarchy hierarchy = ReadInspect(inspect_manager.inspector());

  // Assert root
  ASSERT_EQ(1, hierarchy.children().size());
  ASSERT_EQ(0, hierarchy.node().properties().size());

  // Assert all size values.
  AssertValue(hierarchy, {"test_dir_stats", "test_dir"}, 70, 2);
  AssertValue(hierarchy, {"test_dir_stats", "test_dir", "top.txt"}, 12);
  AssertValue(hierarchy, {"test_dir_stats", "test_dir", "a"}, 58, 3);
  AssertValue(hierarchy, {"test_dir_stats", "test_dir", "a", "a.txt"}, 13);
  AssertValue(hierarchy, {"test_dir_stats", "test_dir", "a", "b"}, 14, 1);
  AssertValue(hierarchy, {"test_dir_stats", "test_dir", "a", "b", "b.txt"}, 14);
  AssertValue(hierarchy, {"test_dir_stats", "test_dir", "a", "c"}, 31, 2);
  AssertValue(hierarchy, {"test_dir_stats", "test_dir", "a", "c", "c.txt"}, 15);
  AssertValue(hierarchy, {"test_dir_stats", "test_dir", "a", "c", "d.txt"}, 16);
}

// Validate that using a bad handle to serve a stats node doesn't block indefinitely.
TEST_F(InspectManagerTest, ServeStatsBadHandle) {
  // Serve inspect stats using an invalid channel.
  auto inspect_manager = fshost::FshostInspectManager();
  fidl::ClientEnd<fuchsia_io::Directory> client_end;
  ASSERT_FALSE(client_end.is_valid());
  inspect_manager.ServeStats("test_dir", std::move(client_end));
  inspect::Hierarchy hierarchy = ReadInspect(inspect_manager.inspector());
  // Ensure the node doesn't actually exist since the callback should return an error.
  ASSERT_EQ(hierarchy.GetByPath({"test_dir_stats"}), nullptr);
}

TEST_F(InspectManagerTest, DirectoryEntryIteratorGetNext) {
  MakeDir("iterator-test");
  for (int64_t i = 0; i < 5000; i++) {
    if (i % 2 == 0) {
      MakeDir(fxl::Substitute("/iterator-test/dir$0", std::to_string(i)));
    } else {
      AddFile(fxl::Substitute("/iterator-test/file$0", std::to_string(i)), 10);
    }
  }
  auto endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
  ASSERT_TRUE(endpoints.is_ok());
  auto [root, server] = *std::move(endpoints);
  ASSERT_EQ(ZX_OK, fdio_open(kTmpfsPath,
                             static_cast<uint32_t>(fuchsia_io::wire::OpenFlags::kRightReadable |
                                                   fuchsia_io::wire::OpenFlags::kRightExecutable),
                             server.TakeChannel().release()));
  fidl::ClientEnd<fuchsia_io::Node> test_dir_chan;
  auto status = fshost::OpenNode(root, "/iterator-test", S_IFDIR, &test_dir_chan);
  ASSERT_EQ(status, ZX_OK);

  // The opened node must be a directory because of the |MakeDir| call above.
  fidl::ClientEnd<fuchsia_io::Directory> test_dir(test_dir_chan.TakeChannel());
  auto iterator = fshost::DirectoryEntriesIterator(std::move(test_dir));
  int64_t found = 0;
  while (auto entry = iterator.GetNext()) {
    if (entry->name.find("dir") == 0) {
      EXPECT_EQ(entry->size, 0);
      EXPECT_TRUE(entry->is_dir);
    } else {
      EXPECT_EQ(entry->name.find("file"), 0);
      EXPECT_EQ(entry->size, 10);
      EXPECT_FALSE(entry->is_dir);
    }
    EXPECT_TRUE(entry->node.is_valid());
    found += 1;
  }
  EXPECT_EQ(found, 5000);
}

TEST_F(InspectManagerTest, CorruptionEvents) {
  fshost::FshostInspectManager inspect_manager;
  // There should be no "corruption_events" node until an event is reported.
  inspect::Hierarchy hierarchy = ReadInspect(inspect_manager.inspector());
  ASSERT_EQ(hierarchy.GetByPath({"corruption_events"}), nullptr);

  // Report some corruption events and make sure they show up where we expect.
  inspect_manager.LogCorruption(fs_management::DiskFormat::kDiskFormatMinfs);
  inspect_manager.LogCorruption(fs_management::DiskFormat::kDiskFormatFxfs);
  inspect_manager.LogCorruption(fs_management::DiskFormat::kDiskFormatFxfs);
  inspect_manager.LogCorruption(fs_management::DiskFormat::kDiskFormatFxfs);

  hierarchy = ReadInspect(inspect_manager.inspector());
  const inspect::Hierarchy* corruption_events = hierarchy.GetByPath({"corruption_events"});
  ASSERT_NE(corruption_events, nullptr);

  const auto* minfs_corruption_events =
      corruption_events->node().get_property<inspect::UintPropertyValue>("minfs");
  ASSERT_NE(minfs_corruption_events, nullptr);
  ASSERT_EQ(minfs_corruption_events->value(), 1u);

  const auto* fxfs_corruption_events =
      corruption_events->node().get_property<inspect::UintPropertyValue>("fxfs");
  ASSERT_NE(fxfs_corruption_events, nullptr);
  ASSERT_EQ(fxfs_corruption_events->value(), 3u);
}

}  // namespace
