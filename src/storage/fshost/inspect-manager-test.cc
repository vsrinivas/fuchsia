// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "inspect-manager.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/namespace.h>
#include <lib/fpromise/single_threaded_executor.h>
#include <lib/inspect/cpp/reader.h>
#include <lib/memfs/memfs.h>
#include <sys/stat.h>

#include <fbl/unique_fd.h>
#include <zxtest/zxtest.h>

#include "src/lib/fxl/strings/substitute.h"
#include "src/lib/storage/vfs/cpp/remote_dir.h"

namespace {

constexpr char kTmpfsPath[] = "/fshost-inspect-tmp";

class InspectManagerTest : public zxtest::Test {
 public:
  InspectManagerTest() : memfs_loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {}

  void SetUp() override {
    ASSERT_EQ(memfs_loop_.StartThread(), ZX_OK);
    zx::channel memfs_root;
    ASSERT_EQ(memfs_create_filesystem(memfs_loop_.dispatcher(), &memfs_,
                                      memfs_root.reset_and_get_address()),
              ZX_OK);
    ASSERT_OK(fdio_ns_get_installed(&namespace_));
    ASSERT_OK(fdio_ns_bind(namespace_, kTmpfsPath, memfs_root.release()));
  }

  void TearDown() override {
    ASSERT_OK(fdio_ns_unbind(namespace_, kTmpfsPath));
    sync_completion_t unmounted;
    memfs_free_filesystem(memfs_, &unmounted);
    ASSERT_EQ(ZX_OK, sync_completion_wait(&unmounted, zx::duration::infinite().get()));
  }

 protected:
  fbl::RefPtr<fs::RemoteDir> GetRemoteDir() {
    auto endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
    EXPECT_TRUE(endpoints.is_ok());
    auto [client, server] = *std::move(endpoints);
    EXPECT_EQ(ZX_OK, fdio_open(kTmpfsPath, ZX_FS_RIGHT_READABLE | ZX_FS_RIGHT_EXECUTABLE,
                               server.TakeChannel().release()));
    return fbl::MakeRefCounted<fs::RemoteDir>(std::move(client));
  }

  fpromise::result<inspect::Hierarchy> ReadInspect(const inspect::Inspector& inspector) {
    fpromise::result<inspect::Hierarchy> hierarchy;
    fpromise::single_threaded_executor exec;
    exec.schedule_task(inspect::ReadFromInspector(inspector).then(
        [&](fpromise::result<inspect::Hierarchy>& result) { hierarchy = std::move(result); }));
    exec.run();
    return hierarchy;
  }

  void AddFile(const std::string& path, size_t content_size) {
    std::string contents(content_size, 'X');
    fbl::unique_fd fd(open(fxl::Substitute("$0/$1", kTmpfsPath, path).c_str(), O_RDWR | O_CREAT));
    ASSERT_TRUE(fd.is_valid());
    ASSERT_EQ(write(fd.get(), contents.c_str(), content_size), content_size);
  }

  void MakeDir(const std::string& path) {
    ASSERT_EQ(0, mkdir(fxl::Substitute("$0/$1", kTmpfsPath, path).c_str(), 0666));
  }

  void AssertValue(const inspect::Hierarchy& hierarchy, const std::vector<std::string>& path,
                   size_t expected, size_t other_children = 0) {
    auto file_node = hierarchy.GetByPath(path);
    EXPECT_NOT_NULL(file_node);
    ASSERT_EQ(1, file_node->node().properties().size());
    ASSERT_EQ(other_children, file_node->children().size());
    auto& size_property = file_node->node().properties()[0];
    EXPECT_EQ("size", size_property.name());
    EXPECT_EQ(expected, size_property.Get<inspect::UintPropertyValue>().value());
  }

  fdio_ns_t* namespace_;
  async::Loop memfs_loop_;
  memfs_filesystem_t* memfs_;
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
  auto inspect_manager = fshost::InspectManager();
  auto remote_dir = GetRemoteDir();
  inspect_manager.ServeStats("test_dir", remote_dir);

  //// Read inspect
  auto result = ReadInspect(inspect_manager.inspector());
  ASSERT_TRUE(result.is_ok());
  auto& hierarchy = result.value();

  // Assert root
  ASSERT_EQ(2, hierarchy.children().size());
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
  ASSERT_EQ(ZX_OK, fdio_open(kTmpfsPath, ZX_FS_RIGHT_READABLE | ZX_FS_RIGHT_EXECUTABLE,
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

}  // namespace
