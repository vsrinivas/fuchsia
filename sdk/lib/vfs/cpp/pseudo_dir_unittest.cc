// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fdio/vfs.h>
#include <lib/vfs/cpp/pseudo_dir.h>
#include <lib/vfs/cpp/pseudo_file.h>
#include <lib/vfs/cpp/testing/dir_test_util.h>

#include "src/lib/testing/loop_fixture/real_loop_fixture.h"

namespace {

using vfs_tests::Dirent;

constexpr size_t kCommonCapacity = 1024;

class TestNode : public vfs::internal::Node {
 public:
  TestNode(std::function<void()> death_callback = nullptr) : death_callback_(death_callback) {}
  ~TestNode() override {
    if (death_callback_) {
      death_callback_();
    }
  }

 protected:
  fuchsia::io::OpenFlags GetAllowedFlags() const override { return {}; }
  fuchsia::io::OpenFlags GetProhibitiveFlags() const override { return {}; }

 private:
  void Describe(fuchsia::io::NodeInfoDeprecated* out_info) override {}
  void GetConnectionInfo(fuchsia::io::ConnectionInfo* out_info) override {}

  zx_status_t CreateConnection(fuchsia::io::OpenFlags flags,
                               std::unique_ptr<vfs::internal::Connection>* connection) override {
    return ZX_ERR_NOT_SUPPORTED;
  }

  std::function<void()> death_callback_;
};

class PseudoDirUnit : public ::testing::Test {
 protected:
  void Init(int number_of_nodes) {
    nodes_.resize(number_of_nodes);
    node_names_.resize(number_of_nodes);

    for (int i = 0; i < number_of_nodes; i++) {
      node_names_[i] = "node" + std::to_string(i);
      nodes_[i] = std::make_shared<TestNode>();
      ASSERT_EQ(ZX_OK, dir_.AddSharedEntry(node_names_[i], nodes_[i]));
    }
  }

  vfs::PseudoDir dir_;
  std::vector<std::string> node_names_;
  std::vector<std::shared_ptr<TestNode>> nodes_;
};

TEST_F(PseudoDirUnit, NotEmpty) {
  Init(1);
  ASSERT_FALSE(dir_.IsEmpty());
}

TEST_F(PseudoDirUnit, Empty) {
  Init(0);
  ASSERT_TRUE(dir_.IsEmpty());
}

TEST_F(PseudoDirUnit, Lookup) {
  Init(10);
  for (int i = 0; i < 10; i++) {
    vfs::internal::Node* n;
    ASSERT_EQ(ZX_OK, dir_.Lookup(node_names_[i], &n)) << "for " << node_names_[i];
    ASSERT_EQ(nodes_[i].get(), n) << "for " << node_names_[i];
  }
}

TEST_F(PseudoDirUnit, LookupUniqueNode) {
  Init(1);

  auto node = std::make_unique<TestNode>();
  vfs::internal::Node* node_ptr = node.get();
  ASSERT_EQ(ZX_OK, dir_.AddEntry("un", std::move(node)));
  vfs::internal::Node* n;
  ASSERT_EQ(ZX_OK, dir_.Lookup(node_names_[0], &n));
  ASSERT_EQ(nodes_[0].get(), n);

  ASSERT_EQ(ZX_OK, dir_.Lookup("un", &n));
  ASSERT_EQ(node_ptr, n);
}

TEST_F(PseudoDirUnit, InvalidLookup) {
  Init(3);
  vfs::internal::Node* n;
  ASSERT_EQ(ZX_ERR_NOT_FOUND, dir_.Lookup("invalid", &n));
}

TEST_F(PseudoDirUnit, RemoveEntry) {
  Init(5);
  for (int i = 0; i < 5; i++) {
    ASSERT_EQ(2, nodes_[i].use_count());
    ASSERT_EQ(ZX_OK, dir_.RemoveEntry(node_names_[i])) << "for " << node_names_[i];

    // cannot access
    vfs::internal::Node* n;
    ASSERT_EQ(ZX_ERR_NOT_FOUND, dir_.Lookup(node_names_[i], &n)) << "for " << node_names_[i];
    // check that use count went doen by 1
    ASSERT_EQ(1, nodes_[i].use_count());
  }
  ASSERT_TRUE(dir_.IsEmpty());
}

TEST_F(PseudoDirUnit, RemoveEntryWithNode) {
  Init(5);
  for (int i = 0; i < 5; i++) {
    ASSERT_EQ(2, nodes_[i].use_count());
    ASSERT_EQ(ZX_OK, dir_.RemoveEntry(node_names_[i], nodes_[i].get())) << "for " << node_names_[i];

    // cannot access
    vfs::internal::Node* n;
    ASSERT_EQ(ZX_ERR_NOT_FOUND, dir_.Lookup(node_names_[i], &n)) << "for " << node_names_[i];
    // check that use count went down by 1
    ASSERT_EQ(1, nodes_[i].use_count());
  }
  ASSERT_TRUE(dir_.IsEmpty());
}

TEST_F(PseudoDirUnit, RemoveUniqueNode) {
  Init(0);

  bool node_died = false;
  auto node = std::make_unique<TestNode>([&]() { node_died = true; });
  EXPECT_FALSE(node_died);
  ASSERT_EQ(ZX_OK, dir_.AddEntry("un", std::move(node)));
  ASSERT_EQ(ZX_OK, dir_.RemoveEntry("un"));
  EXPECT_TRUE(node_died);

  vfs::internal::Node* n;
  ASSERT_EQ(ZX_ERR_NOT_FOUND, dir_.Lookup("un", &n));
}

TEST_F(PseudoDirUnit, RemoveInvalidEntry) {
  Init(5);
  ASSERT_EQ(ZX_ERR_NOT_FOUND, dir_.RemoveEntry("invalid"));

  // make sure nothing was removed
  for (int i = 0; i < 5; i++) {
    vfs::internal::Node* n;
    ASSERT_EQ(ZX_OK, dir_.Lookup(node_names_[i], &n)) << "for " << node_names_[i];
    ASSERT_EQ(nodes_[i].get(), n) << "for " << node_names_[i];
  }
}

TEST_F(PseudoDirUnit, AddAfterRemove) {
  Init(5);
  ASSERT_EQ(ZX_OK, dir_.RemoveEntry(node_names_[2]));

  auto new_node = std::make_shared<TestNode>();
  ASSERT_EQ(ZX_OK, dir_.AddSharedEntry("new_node", new_node));

  for (int i = 0; i < 5; i++) {
    zx_status_t expected_status = ZX_OK;
    if (i == 2) {
      expected_status = ZX_ERR_NOT_FOUND;
    }
    vfs::internal::Node* n;
    ASSERT_EQ(expected_status, dir_.Lookup(node_names_[i], &n)) << "for " << node_names_[i];
    if (expected_status == ZX_OK) {
      ASSERT_EQ(nodes_[i].get(), n) << "for " << node_names_[i];
    }
  }

  vfs::internal::Node* n;
  ASSERT_EQ(ZX_OK, dir_.Lookup("new_node", &n));
  ASSERT_EQ(new_node.get(), n);
}

class DirectoryWrapper {
 public:
  DirectoryWrapper(bool start_loop = true)
      : dir_(std::make_shared<vfs::PseudoDir>()), loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {
    if (start_loop) {
      loop_.StartThread("vfs test thread");
    }
  }

  void AddEntry(const std::string& name, std::unique_ptr<vfs::internal::Node> node,
                zx_status_t expected_status = ZX_OK) {
    ASSERT_EQ(expected_status, dir_->AddEntry(name, std::move(node)));
  }

  void AddSharedEntry(const std::string& name, std::shared_ptr<vfs::internal::Node> node,
                      zx_status_t expected_status = ZX_OK) {
    ASSERT_EQ(expected_status, dir_->AddSharedEntry(name, std::move(node)));
  }

  fuchsia::io::DirectorySyncPtr Serve(
      fuchsia::io::OpenFlags flags = fuchsia::io::OpenFlags::RIGHT_READABLE) {
    fuchsia::io::DirectorySyncPtr ptr;
    dir_->Serve(flags, ptr.NewRequest().TakeChannel(), loop_.dispatcher());
    return ptr;
  }

  void AddReadOnlyFile(const std::string& file_name, const std::string& file_content) {
    auto read_fn = [file_content](std::vector<uint8_t>* output, size_t max_file_size) {
      output->resize(file_content.length());
      std::copy(file_content.begin(), file_content.end(), output->begin());
      return ZX_OK;
    };

    auto file = std::make_unique<vfs::PseudoFile>(file_content.size(), std::move(read_fn));

    AddEntry(file_name, std::move(file));
  }

  // This version borrows the |file_content| vector and uses it as the backing
  // storage for read and write requests.
  void AddReadWriteFile(const std::string& file_name, std::vector<uint8_t>* file_content) {
    auto read_fn = [file_content](std::vector<uint8_t>* output, size_t max_file_size) {
      output->resize(file_content->size());
      std::copy(file_content->begin(), file_content->end(), output->begin());
      return ZX_OK;
    };

    auto write_fn = [file_content](std::vector<uint8_t> input) {
      file_content->resize(input.size());
      std::copy(input.begin(), input.end(), file_content->begin());
      return ZX_OK;
    };

    auto file =
        std::make_unique<vfs::PseudoFile>(kCommonCapacity, std::move(read_fn), std::move(write_fn));

    AddEntry(file_name, std::move(file));
  }

  std::shared_ptr<vfs::PseudoDir>& dir() { return dir_; }

 private:
  std::shared_ptr<vfs::PseudoDir> dir_;
  async::Loop loop_;
};

class PseudoDirConnection : public vfs_tests::DirConnection {
 protected:
  vfs::internal::Directory* GetDirectoryNode() override { return dir_.dir().get(); }

  DirectoryWrapper dir_;
};

TEST_F(PseudoDirConnection, ReadDirSimple) {
  auto subdir = std::make_shared<vfs::PseudoDir>();
  dir_.AddSharedEntry("subdir", subdir);
  dir_.AddReadOnlyFile("file1", "file1");
  dir_.AddReadOnlyFile("file2", "file2");
  dir_.AddReadOnlyFile("file3", "file3");

  auto ptr = dir_.Serve();

  std::vector<Dirent> expected_dirents = {
      Dirent::DirentForDot(),         Dirent::DirentForDirectory("subdir"),
      Dirent::DirentForFile("file1"), Dirent::DirentForFile("file2"),
      Dirent::DirentForFile("file3"),
  };
  AssertReadDirents(ptr, kCommonCapacity, expected_dirents);
}

TEST_F(PseudoDirConnection, WatchDirNotSupported) {
  auto ptr = dir_.Serve();

  fidl::InterfacePtr<fuchsia::io::DirectoryWatcher> watcher;
  zx_status_t status;
  ptr->Watch({}, 0, watcher.NewRequest(), &status);
  EXPECT_EQ(ZX_ERR_NOT_SUPPORTED, status);
}

TEST_F(PseudoDirConnection, ReadDirOnEmptyDirectory) {
  auto ptr = dir_.Serve();

  std::vector<Dirent> expected_dirents = {
      Dirent::DirentForDot(),
  };
  AssertReadDirents(ptr, kCommonCapacity, expected_dirents);
}

TEST_F(PseudoDirConnection, ReadDirSizeLessThanFirstEntry) {
  auto subdir = std::make_shared<vfs::PseudoDir>();

  auto ptr = dir_.Serve();

  std::vector<Dirent> expected_dirents;
  AssertReadDirents(ptr, sizeof(vdirent_t), expected_dirents, ZX_ERR_INVALID_ARGS);
}

TEST_F(PseudoDirConnection, ReadDirSizeLessThanEntry) {
  auto subdir = std::make_shared<vfs::PseudoDir>();

  dir_.AddSharedEntry("subdir", subdir);

  auto ptr = dir_.Serve();

  std::vector<Dirent> expected_dirents = {Dirent::DirentForDot()};
  AssertReadDirents(ptr, sizeof(vdirent_t) + 1, expected_dirents);
  std::vector<Dirent> empty_dirents;
  AssertReadDirents(ptr, sizeof(vdirent_t), empty_dirents, ZX_ERR_INVALID_ARGS);
}

TEST_F(PseudoDirConnection, ReadDirInParts) {
  auto subdir = std::make_shared<vfs::PseudoDir>();
  dir_.AddSharedEntry("subdir", subdir);
  dir_.AddReadOnlyFile("file1", "file1");
  dir_.AddReadOnlyFile("file2", "file2");
  dir_.AddReadOnlyFile("file3", "file3");

  auto ptr = dir_.Serve();

  std::vector<Dirent> expected_dirents1 = {
      Dirent::DirentForDot(),
      Dirent::DirentForDirectory("subdir"),
  };

  std::vector<Dirent> expected_dirents2 = {
      Dirent::DirentForFile("file1"),
      Dirent::DirentForFile("file2"),
      Dirent::DirentForFile("file3"),
  };
  AssertReadDirents(ptr, 2 * sizeof(vdirent_t) + 10, expected_dirents1);
  AssertReadDirents(ptr, 3 * sizeof(vdirent_t) + 20, expected_dirents2);
}

TEST_F(PseudoDirConnection, ReadDirWithExactBytes) {
  auto subdir = std::make_shared<vfs::PseudoDir>();
  dir_.AddSharedEntry("subdir", subdir);
  dir_.AddReadOnlyFile("file1", "file1");
  dir_.AddReadOnlyFile("file2", "file2");
  dir_.AddReadOnlyFile("file3", "file3");

  auto ptr = dir_.Serve();

  std::vector<Dirent> expected_dirents = {
      Dirent::DirentForDot(),         Dirent::DirentForDirectory("subdir"),
      Dirent::DirentForFile("file1"), Dirent::DirentForFile("file2"),
      Dirent::DirentForFile("file3"),
  };
  uint64_t exact_size = 0;
  for (auto& d : expected_dirents) {
    exact_size += d.size_in_bytes();
  }

  AssertReadDirents(ptr, exact_size, expected_dirents);
}

TEST_F(PseudoDirConnection, ReadDirInPartsWithExactBytes) {
  auto subdir = std::make_shared<vfs::PseudoDir>();
  dir_.AddSharedEntry("subdir", subdir);
  dir_.AddReadOnlyFile("file1", "file1");
  dir_.AddReadOnlyFile("file2", "file2");
  dir_.AddReadOnlyFile("file3", "file3");

  auto ptr = dir_.Serve();

  std::vector<Dirent> expected_dirents1 = {
      Dirent::DirentForDot(),
      Dirent::DirentForDirectory("subdir"),
  };

  std::vector<Dirent> expected_dirents2 = {
      Dirent::DirentForFile("file1"),
      Dirent::DirentForFile("file2"),
      Dirent::DirentForFile("file3"),
  };
  uint64_t exact_size1 = 0;
  for (auto& d : expected_dirents1) {
    exact_size1 += d.size_in_bytes();
  }

  uint64_t exact_size2 = 0;
  for (auto& d : expected_dirents2) {
    exact_size2 += d.size_in_bytes();
  }

  AssertReadDirents(ptr, exact_size1, expected_dirents1);
  AssertReadDirents(ptr, exact_size2, expected_dirents2);
}

TEST_F(PseudoDirConnection, ReadDirAfterFullRead) {
  auto subdir = std::make_shared<vfs::PseudoDir>();
  dir_.AddSharedEntry("subdir", subdir);

  auto ptr = dir_.Serve();

  std::vector<Dirent> expected_dirents = {
      Dirent::DirentForDot(),
      Dirent::DirentForDirectory("subdir"),
  };

  std::vector<Dirent> empty_dirents;

  AssertReadDirents(ptr, kCommonCapacity, expected_dirents);
  AssertReadDirents(ptr, kCommonCapacity, empty_dirents);
}

TEST_F(PseudoDirConnection, RewindWorksAfterFullRead) {
  auto subdir = std::make_shared<vfs::PseudoDir>();
  dir_.AddSharedEntry("subdir", subdir);

  auto ptr = dir_.Serve();

  std::vector<Dirent> expected_dirents = {
      Dirent::DirentForDot(),
      Dirent::DirentForDirectory("subdir"),
  };

  std::vector<Dirent> empty_dirents;

  AssertReadDirents(ptr, kCommonCapacity, expected_dirents);
  AssertReadDirents(ptr, kCommonCapacity, empty_dirents);

  AssertRewind(ptr);

  AssertReadDirents(ptr, kCommonCapacity, expected_dirents);
}

TEST_F(PseudoDirConnection, RewindWorksAfterPartialRead) {
  auto subdir = std::make_shared<vfs::PseudoDir>();
  dir_.AddSharedEntry("subdir", subdir);
  dir_.AddReadOnlyFile("file1", "file1");
  dir_.AddReadOnlyFile("file2", "file2");
  dir_.AddReadOnlyFile("file3", "file3");

  auto ptr = dir_.Serve();

  std::vector<Dirent> expected_dirents1 = {
      Dirent::DirentForDot(),
      Dirent::DirentForDirectory("subdir"),
  };

  std::vector<Dirent> expected_dirents2 = {
      Dirent::DirentForFile("file1"),
      Dirent::DirentForFile("file2"),
      Dirent::DirentForFile("file3"),
  };
  AssertReadDirents(ptr, 2 * sizeof(vdirent_t) + 10, expected_dirents1);
  AssertRewind(ptr);
  AssertReadDirents(ptr, 2 * sizeof(vdirent_t) + 10, expected_dirents1);
  AssertReadDirents(ptr, 3 * sizeof(vdirent_t) + 20, expected_dirents2);
}

TEST_F(PseudoDirConnection, ReadDirAfterAddingEntry) {
  auto subdir = std::make_shared<vfs::PseudoDir>();
  dir_.AddSharedEntry("subdir", subdir);

  auto ptr = dir_.Serve();

  std::vector<Dirent> expected_dirents1 = {
      Dirent::DirentForDot(),
      Dirent::DirentForDirectory("subdir"),
  };
  AssertReadDirents(ptr, kCommonCapacity, expected_dirents1);

  dir_.AddReadOnlyFile("file1", "file1");
  std::vector<Dirent> expected_dirents2 = {
      Dirent::DirentForFile("file1"),
  };
  AssertReadDirents(ptr, kCommonCapacity, expected_dirents2);
}

TEST_F(PseudoDirConnection, ReadDirAndRewindAfterAddingEntry) {
  auto subdir = std::make_shared<vfs::PseudoDir>();
  dir_.AddSharedEntry("subdir", subdir);

  auto ptr = dir_.Serve();

  std::vector<Dirent> expected_dirents1 = {
      Dirent::DirentForDot(),
      Dirent::DirentForDirectory("subdir"),
  };
  AssertReadDirents(ptr, kCommonCapacity, expected_dirents1);

  dir_.AddReadOnlyFile("file1", "file1");
  AssertRewind(ptr);
  std::vector<Dirent> expected_dirents2 = {
      Dirent::DirentForDot(),
      Dirent::DirentForDirectory("subdir"),
      Dirent::DirentForFile("file1"),
  };
  AssertReadDirents(ptr, kCommonCapacity, expected_dirents2);
}

TEST_F(PseudoDirConnection, ReadDirAfterRemovingEntry) {
  auto subdir = std::make_shared<vfs::PseudoDir>();
  dir_.AddSharedEntry("subdir", subdir);

  auto ptr = dir_.Serve();

  std::vector<Dirent> expected_dirents1 = {
      Dirent::DirentForDot(),
      Dirent::DirentForDirectory("subdir"),
  };
  AssertReadDirents(ptr, kCommonCapacity, expected_dirents1);
  std::vector<Dirent> empty_dirents;
  ASSERT_EQ(ZX_OK, dir_.dir()->RemoveEntry("subdir"));
  AssertReadDirents(ptr, kCommonCapacity, empty_dirents);

  // rewind and check again
  AssertRewind(ptr);

  std::vector<Dirent> expected_dirents2 = {
      Dirent::DirentForDot(),
  };
  AssertReadDirents(ptr, kCommonCapacity, expected_dirents2);
}

TEST_F(PseudoDirConnection, CantReadNodeReferenceDir) {
  auto ptr = dir_.Serve(fuchsia::io::OpenFlags::NODE_REFERENCE);
  // make sure node reference was opened
  zx_status_t status;
  fuchsia::io::NodeAttributes attr;
  ASSERT_EQ(ZX_OK, ptr->GetAttr(&status, &attr));
  ASSERT_EQ(ZX_OK, status);
  ASSERT_NE(0u, attr.mode | fuchsia::io::MODE_TYPE_DIRECTORY);

  std::vector<uint8_t> out_dirents;
  ASSERT_EQ(ZX_ERR_PEER_CLOSED, ptr->ReadDirents(100, &status, &out_dirents));
}

TEST_F(PseudoDirConnection, ServeOnInvalidFlags) {
  fuchsia::io::OpenFlags not_allowed_flags[] = {
      fuchsia::io::OpenFlags::CREATE, fuchsia::io::OpenFlags::CREATE_IF_ABSENT,
      fuchsia::io::OpenFlags::TRUNCATE, fuchsia::io::OpenFlags::APPEND};

  for (auto not_allowed_flag : not_allowed_flags) {
    SCOPED_TRACE(std::to_string(static_cast<uint32_t>(not_allowed_flag)));
    AssertOpen(dispatcher(), not_allowed_flag, ZX_ERR_INVALID_ARGS);
  }
}

TEST_F(PseudoDirConnection, ServeOnValidFlags) {
  fuchsia::io::OpenFlags allowed_flags[] = {
      fuchsia::io::OpenFlags::RIGHT_READABLE, fuchsia::io::OpenFlags::RIGHT_WRITABLE,
      fuchsia::io::OpenFlags::NODE_REFERENCE, fuchsia::io::OpenFlags::DIRECTORY};
  for (auto allowed_flag : allowed_flags) {
    SCOPED_TRACE(std::to_string(static_cast<uint32_t>(allowed_flag)));
    AssertOpen(dispatcher(), allowed_flag, ZX_OK);
  }
}

TEST_F(PseudoDirConnection, OpenSelf) {
  std::string paths[] = {
      ".",      "./",     ".//",   "././",
      "././/.", "././//", "././/", "././././/././././////./././//././/./././/././."};
  auto subdir = std::make_shared<vfs::PseudoDir>();
  dir_.AddSharedEntry("subdir", subdir);
  auto ptr = dir_.Serve();
  std::vector<Dirent> expected_dirents = {Dirent::DirentForDot(),
                                          Dirent::DirentForDirectory("subdir")};
  for (auto& path : paths) {
    SCOPED_TRACE("path: " + path);

    fuchsia::io::DirectorySyncPtr new_ptr;
    AssertOpenPath(ptr, path, new_ptr, fuchsia::io::OpenFlags::RIGHT_READABLE);

    // assert correct directory was opened
    AssertReadDirents(new_ptr, kCommonCapacity, expected_dirents);
  }
}

TEST_F(PseudoDirConnection, OpenEmptyPath) {
  auto ptr = dir_.Serve();
  fuchsia::io::DirectorySyncPtr new_ptr;
  AssertOpenPath(ptr, "", new_ptr, fuchsia::io::OpenFlags::RIGHT_READABLE, 0, ZX_ERR_BAD_PATH);
}

TEST_F(PseudoDirConnection, OpenSubDir) {
  DirectoryWrapper subdir1(false);
  DirectoryWrapper subdir2(false);
  dir_.AddSharedEntry("subdir1", subdir1.dir());
  dir_.AddSharedEntry("subdir2", subdir2.dir());
  subdir1.AddReadOnlyFile("file1", "file1");
  subdir2.AddReadOnlyFile("file2", "file2");

  auto ptr = dir_.Serve();
  std::vector<Dirent> expected_dirents_sub1 = {Dirent::DirentForDot(),
                                               Dirent::DirentForFile("file1")};
  std::vector<Dirent> expected_dirents_sub2 = {Dirent::DirentForDot(),
                                               Dirent::DirentForFile("file2")};

  std::string paths1[] = {"./subdir1",
                          "././subdir1",
                          ".//./././././/./subdir1",
                          "subdir1",
                          "subdir1/",
                          "subdir1/.",
                          "subdir1//",
                          "subdir1///",
                          "subdir1/./",
                          "subdir1/.//",
                          "subdir1/.//.",
                          "subdir1/.//././//./",
                          "subdir1/.//././/./."};
  for (auto& path : paths1) {
    SCOPED_TRACE("path: " + path);

    fuchsia::io::DirectorySyncPtr new_ptr;
    AssertOpenPath(ptr, path, new_ptr, fuchsia::io::OpenFlags::RIGHT_READABLE);

    // assert correct directory was opened
    AssertReadDirents(new_ptr, kCommonCapacity, expected_dirents_sub1);
  }

  // test with other directory
  std::string paths2[] = {"./subdir2",
                          "././subdir2",
                          ".//./././././/./subdir2",
                          "subdir2",
                          "subdir2/",
                          "subdir2/.",
                          "subdir2//",
                          "subdir2///",
                          "subdir2/./",
                          "subdir2/.//",
                          "subdir2/.//.",
                          "subdir2/.//././//./",
                          "subdir2/.//././/./."};
  for (auto& path : paths2) {
    SCOPED_TRACE("path: " + path);

    fuchsia::io::DirectorySyncPtr new_ptr;
    AssertOpenPath(ptr, path, new_ptr, fuchsia::io::OpenFlags::RIGHT_READABLE);

    // assert correct directory was opened
    AssertReadDirents(new_ptr, kCommonCapacity, expected_dirents_sub2);
  }
}

TEST_F(PseudoDirConnection, OpenFile) {
  dir_.AddReadOnlyFile("file1", "file1");
  dir_.AddReadOnlyFile("file2", "file2");
  dir_.AddReadOnlyFile("..foo", "..foo");
  dir_.AddReadOnlyFile("...foo", "...foo");
  dir_.AddReadOnlyFile(".foo", ".foo");

  DirectoryWrapper subdir1(false);
  DirectoryWrapper subdir2(false);

  dir_.AddSharedEntry("subdir1", subdir1.dir());
  dir_.AddSharedEntry("subdir2", subdir2.dir());
  subdir1.AddReadOnlyFile("file2", "subdir1/file2");
  subdir1.AddReadOnlyFile("file1", "subdir1/file1");
  subdir1.AddReadOnlyFile("..foo", "subdir1/..foo");
  subdir1.AddReadOnlyFile("...foo", "subdir1/...foo");
  subdir1.AddReadOnlyFile(".foo", "subdir1/.foo");
  subdir2.AddReadOnlyFile("file3", "subdir2/file3");
  subdir2.AddReadOnlyFile("file4", "subdir2/file4");

  auto ptr = dir_.Serve();

  std::string files[] = {"file1",         "file2",         ".foo",          "..foo",
                         "...foo",        "subdir1/file1", "subdir1/file2", "subdir2/file3",
                         "subdir2/file4", "subdir1/.foo",  "subdir1/..foo", "subdir1/...foo"};
  for (auto& file : files) {
    SCOPED_TRACE("file: " + file);
    fuchsia::io::FileSyncPtr file_ptr;
    AssertOpenPath(ptr, file, file_ptr, fuchsia::io::OpenFlags::RIGHT_READABLE);

    AssertRead(file_ptr, 100, file);
  }
}

TEST_F(PseudoDirConnection, OpenFileWithMultipleSlashesAndDotsInPath) {
  DirectoryWrapper subdir1(false);

  dir_.AddSharedEntry("subdir1", subdir1.dir());
  subdir1.AddReadOnlyFile("file1", "file1");
  dir_.AddReadOnlyFile("file1", "file1");

  auto ptr = dir_.Serve();

  std::string files[] = {"./file1",
                         ".//file1",
                         "././/././///././file1",
                         "subdir1//file1",
                         "subdir1///file1",
                         "subdir1////file1",
                         "subdir1/./file1",
                         "subdir1/.//./file1",
                         "subdir1/././file1",
                         "subdir1/././///file1"};
  for (auto& file : files) {
    SCOPED_TRACE("file: " + file);
    fuchsia::io::FileSyncPtr file_ptr;
    AssertOpenPath(ptr, file, file_ptr, fuchsia::io::OpenFlags::RIGHT_READABLE);

    AssertRead(file_ptr, 100, "file1");
  }
}

TEST_F(PseudoDirConnection, OpenWithInvalidPaths) {
  dir_.AddReadOnlyFile("file1", "file1");

  DirectoryWrapper subdir1(false);
  DirectoryWrapper subdir2(false);

  dir_.AddSharedEntry("subdir1", subdir1.dir());
  dir_.AddSharedEntry("subdir2", subdir2.dir());
  subdir1.AddReadOnlyFile("file1", "subdir1/file1");
  subdir2.AddReadOnlyFile("file3", "subdir2/file3");

  auto ptr = dir_.Serve();

  std::vector<std::string> not_found_paths = {"file", "subdir", "subdir1/file", "subdir2/file1"};

  std::string big_path(NAME_MAX + 1, 'a');
  std::vector<std::string> invalid_args_paths = {"..",
                                                 "../",
                                                 "subdir1/..",
                                                 "subdir1/../",
                                                 "subdir1/../file1",
                                                 "file1/../file1",
                                                 std::move(big_path)};

  std::vector<std::string> not_dir_paths = {
      "subdir1/file1/",      "subdir1/file1//", "subdir1/file1///", "subdir1/file1/.",
      "subdir1/file1/file2", "./file1/",        "./file1/.",        "./file1/file2"};

  std::vector<zx_status_t> expected_errors = {ZX_ERR_NOT_FOUND, ZX_ERR_INVALID_ARGS,
                                              ZX_ERR_NOT_DIR};
  std::vector<std::vector<std::string>> invalid_paths = {not_found_paths, invalid_args_paths,
                                                         not_dir_paths};

  // sanity check
  ASSERT_EQ(expected_errors.size(), invalid_paths.size());

  for (size_t i = 0; i < expected_errors.size(); i++) {
    auto expected_status = expected_errors[i];
    auto& paths = invalid_paths[i];
    for (auto& path : paths) {
      SCOPED_TRACE("path: " + path);
      fuchsia::io::NodeSyncPtr file_ptr;
      AssertOpenPath(ptr, path, file_ptr, fuchsia::io::OpenFlags::RIGHT_READABLE, 0,
                     expected_status);
    }
  }
}

TEST_F(PseudoDirConnection, CannotOpenFileWithDirectoryFlag) {
  dir_.AddReadOnlyFile("file1", "file1");
  auto ptr = dir_.Serve();
  fuchsia::io::FileSyncPtr file_ptr;
  AssertOpenPath(ptr, "file1", file_ptr,
                 fuchsia::io::OpenFlags::RIGHT_READABLE | fuchsia::io::OpenFlags::DIRECTORY, 0,
                 ZX_ERR_NOT_DIR);
}

TEST_F(PseudoDirConnection, CannotOpenDirectoryWithInvalidFlags) {
  fuchsia::io::OpenFlags invalid_flags[] = {
      fuchsia::io::OpenFlags::CREATE, fuchsia::io::OpenFlags::CREATE_IF_ABSENT,
      fuchsia::io::OpenFlags::TRUNCATE, fuchsia::io::OpenFlags::APPEND};
  DirectoryWrapper subdir1(false);
  dir_.AddSharedEntry("subdir1", subdir1.dir());

  auto ptr = dir_.Serve();
  std::string paths[] = {".", "subdir1"};

  for (auto& path : paths) {
    for (auto flag : invalid_flags) {
      SCOPED_TRACE("path: " + path + ", flag: " + std::to_string(static_cast<uint32_t>(flag)));
      fuchsia::io::NodeSyncPtr node_ptr;
      AssertOpenPath(ptr, path, node_ptr, flag, 0, ZX_ERR_INVALID_ARGS);
    }
  }
}

TEST_F(PseudoDirConnection, OpenDirWithCorrectMode) {
  DirectoryWrapper subdir1(false);
  dir_.AddSharedEntry("subdir1", subdir1.dir());

  auto ptr = dir_.Serve();
  std::string paths[] = {".", "subdir1"};

  uint32_t modes[] = {fuchsia::io::MODE_TYPE_DIRECTORY, V_IXUSR, V_IWUSR, V_IRUSR};

  for (auto& path : paths) {
    for (auto mode : modes) {
      SCOPED_TRACE("path: " + path + ", mode: " + std::to_string(mode));
      fuchsia::io::NodeSyncPtr node_ptr;
      AssertOpenPath(ptr, path, node_ptr, fuchsia::io::OpenFlags::RIGHT_READABLE, mode);
    }
  }
}

TEST_F(PseudoDirConnection, OpenDirWithIncorrectMode) {
  DirectoryWrapper subdir1(false);
  dir_.AddSharedEntry("subdir1", subdir1.dir());

  auto ptr = dir_.Serve();
  std::string paths[] = {".", "subdir1"};

  uint32_t modes[] = {
      fuchsia::io::MODE_TYPE_FILE,
      fuchsia::io::MODE_TYPE_BLOCK_DEVICE,
      fuchsia::io::MODE_TYPE_SERVICE,
  };

  for (auto& path : paths) {
    for (auto mode : modes) {
      SCOPED_TRACE("path: " + path + ", mode: " + std::to_string(mode));
      fuchsia::io::NodeSyncPtr node_ptr;
      // Opening should succeed even with the incorrect mode, since we are not
      // creating a new resource (see fuchsia.io for further details on mode handling).
      AssertOpenPath(ptr, path, node_ptr, fuchsia::io::OpenFlags::RIGHT_READABLE, mode);
    }
  }
}

TEST_F(PseudoDirConnection, OpenFileWithCorrectMode) {
  dir_.AddReadOnlyFile("file1", "file1");
  auto ptr = dir_.Serve();

  uint32_t modes[] = {fuchsia::io::MODE_TYPE_FILE, V_IXUSR, V_IWUSR, V_IRUSR};

  for (auto mode : modes) {
    SCOPED_TRACE("mode: " + std::to_string(mode));
    fuchsia::io::NodeSyncPtr node_ptr;
    AssertOpenPath(ptr, "file1", node_ptr, fuchsia::io::OpenFlags::RIGHT_READABLE, mode);
  }
}

TEST_F(PseudoDirConnection, OpenFileWithIncorrectMode) {
  dir_.AddReadOnlyFile("file1", "file1");
  auto ptr = dir_.Serve();

  uint32_t modes[] = {
      fuchsia::io::MODE_TYPE_DIRECTORY,
      fuchsia::io::MODE_TYPE_BLOCK_DEVICE,
      fuchsia::io::MODE_TYPE_SERVICE,
  };

  for (auto mode : modes) {
    SCOPED_TRACE("mode: " + std::to_string(mode));
    fuchsia::io::NodeSyncPtr node_ptr;
    // Opening should succeed even with the incorrect mode, since we are not
    // creating a new resource (see fuchsia.io for further details on mode handling).
    AssertOpenPath(ptr, "file1", node_ptr, fuchsia::io::OpenFlags::RIGHT_READABLE, mode);
  }
}

TEST_F(PseudoDirConnection, CanCloneDirectoryConnection) {
  dir_.AddReadOnlyFile("file1", "file1");
  auto ptr = dir_.Serve();
  fuchsia::io::DirectorySyncPtr cloned_ptr;
  ptr->Clone(fuchsia::io::OpenFlags::CLONE_SAME_RIGHTS,
             fidl::InterfaceRequest<fuchsia::io::Node>(cloned_ptr.NewRequest().TakeChannel()));

  fuchsia::io::NodeSyncPtr node_ptr;
  AssertOpenPath(cloned_ptr, "file1", node_ptr, fuchsia::io::OpenFlags::RIGHT_READABLE, 0);
}

TEST_F(PseudoDirConnection, CloneFlagSameRightsFailsWithSpecificRights) {
  auto ptr = dir_.Serve();

  fuchsia::io::OpenFlags rights[] = {fuchsia::io::OpenFlags::RIGHT_READABLE,
                                     fuchsia::io::OpenFlags::RIGHT_WRITABLE};

  for (auto right : rights) {
    fuchsia::io::DirectorySyncPtr cloned_ptr;
    ptr->Clone(fuchsia::io::OpenFlags::CLONE_SAME_RIGHTS | right,
               fidl::InterfaceRequest<fuchsia::io::Node>(cloned_ptr.NewRequest().TakeChannel()));
    // The other end of |cloned_ptr| is closed.
    zx_status_t status;
    std::vector<uint8_t> out_dirents;
    ASSERT_EQ(ZX_ERR_PEER_CLOSED, cloned_ptr->ReadDirents(100, &status, &out_dirents));
  }
}

TEST_F(PseudoDirConnection, NodeReferenceIsClonedAsNodeReference) {
  fuchsia::io::DirectorySyncPtr cloned_ptr;
  {
    auto ptr = dir_.Serve(fuchsia::io::OpenFlags::NODE_REFERENCE);

    ptr->Clone(fuchsia::io::OpenFlags::CLONE_SAME_RIGHTS,
               fidl::InterfaceRequest<fuchsia::io::Node>(cloned_ptr.NewRequest().TakeChannel()));
  }
  // make sure node reference was cloned
  zx_status_t status;
  fuchsia::io::NodeAttributes attr;
  ASSERT_EQ(ZX_OK, cloned_ptr->GetAttr(&status, &attr));
  ASSERT_EQ(ZX_OK, status);
  ASSERT_NE(0u, attr.mode | fuchsia::io::MODE_TYPE_DIRECTORY);

  std::vector<uint8_t> out_dirents;
  ASSERT_EQ(ZX_ERR_PEER_CLOSED, cloned_ptr->ReadDirents(100, &status, &out_dirents));
}

TEST_F(PseudoDirConnection, NodeReferenceIsInvalidWithRights) {
  DirectoryWrapper subdir1(false);
  dir_.AddSharedEntry("subdir1", subdir1.dir());
  auto ptr = dir_.Serve();
  fuchsia::io::DirectorySyncPtr new_ptr;
  AssertOpenPath(ptr, "subdir1", new_ptr,
                 fuchsia::io::OpenFlags::NODE_REFERENCE | fuchsia::io::OpenFlags::RIGHT_READABLE, 0,
                 ZX_ERR_INVALID_ARGS);
}

TEST_F(PseudoDirConnection, NodeReferenceWithInvalidFlags) {
  DirectoryWrapper subdir1(false);
  dir_.AddSharedEntry("subdir1", subdir1.dir());
  auto ptr = dir_.Serve();
  fuchsia::io::DirectorySyncPtr new_ptr;
  AssertOpenPath(ptr, "subdir1", new_ptr,
                 fuchsia::io::OpenFlags::NODE_REFERENCE | fuchsia::io::OpenFlags::POSIX_WRITABLE, 0,
                 ZX_ERR_INVALID_ARGS);
}

TEST_F(PseudoDirConnection, OpeningWithNoRightsAndNoNodeReference) {
  DirectoryWrapper subdir1(false);
  dir_.AddSharedEntry("subdir1", subdir1.dir());
  auto ptr = dir_.Serve();
  fuchsia::io::DirectorySyncPtr new_ptr;
  AssertOpenPath(ptr, "subdir1", new_ptr, {}, 0, ZX_OK);
}

TEST_F(PseudoDirConnection, DirectoryRightsAreHierarchical) {
  DirectoryWrapper subdir1(false);
  dir_.AddSharedEntry("subdir1", subdir1.dir());
  subdir1.AddReadOnlyFile("file1", "file1");

  {
    auto ptr = dir_.Serve(fuchsia::io::OpenFlags::RIGHT_READABLE);
    fuchsia::io::DirectorySyncPtr new_ptr;
    AssertOpenPath(ptr, "subdir1", new_ptr, fuchsia::io::OpenFlags::RIGHT_READABLE, 0, ZX_OK);
    new_ptr.Unbind();
    AssertOpenPath(ptr, "subdir1", new_ptr, fuchsia::io::OpenFlags::RIGHT_WRITABLE, 0,
                   ZX_ERR_ACCESS_DENIED);
  }

  {
    auto ptr = dir_.Serve(fuchsia::io::OpenFlags::RIGHT_WRITABLE);
    fuchsia::io::DirectorySyncPtr new_ptr;
    AssertOpenPath(ptr, "subdir1", new_ptr, fuchsia::io::OpenFlags::RIGHT_READABLE, 0,
                   ZX_ERR_ACCESS_DENIED);
    AssertOpenPath(ptr, "subdir1", new_ptr, fuchsia::io::OpenFlags::RIGHT_WRITABLE, 0, ZX_OK);
  }

  {
    auto ptr =
        dir_.Serve(fuchsia::io::OpenFlags::RIGHT_READABLE | fuchsia::io::OpenFlags::RIGHT_WRITABLE);
    fuchsia::io::DirectorySyncPtr sub_ptr;
    AssertOpenPath(ptr, "subdir1", sub_ptr, fuchsia::io::OpenFlags::RIGHT_READABLE);
    fuchsia::io::FileSyncPtr file_ptr;
    AssertOpenPath(sub_ptr, "file1", file_ptr, fuchsia::io::OpenFlags::RIGHT_READABLE);
    file_ptr.Unbind();
    AssertOpenPath(sub_ptr, "file1", file_ptr,
                   fuchsia::io::OpenFlags::RIGHT_READABLE | fuchsia::io::OpenFlags::RIGHT_WRITABLE,
                   0, ZX_ERR_ACCESS_DENIED);
    AssertOpenPath(sub_ptr, "file1", file_ptr, fuchsia::io::OpenFlags::RIGHT_WRITABLE, 0,
                   ZX_ERR_ACCESS_DENIED);
  }
}

TEST_F(PseudoDirConnection, PosixFlagExpandsInheritedRights) {
  DirectoryWrapper subdir1(false);
  dir_.AddSharedEntry("subdir1", subdir1.dir());
  std::vector<uint8_t> buffer(10);
  subdir1.AddReadWriteFile("file1", &buffer);
  auto ptr =
      dir_.Serve(fuchsia::io::OpenFlags::RIGHT_READABLE | fuchsia::io::OpenFlags::RIGHT_WRITABLE);
  fuchsia::io::DirectorySyncPtr sub_ptr;
  AssertOpenPath(ptr, "subdir1", sub_ptr,
                 fuchsia::io::OpenFlags::RIGHT_READABLE | fuchsia::io::OpenFlags::POSIX_WRITABLE);
  fuchsia::io::FileSyncPtr file_ptr;
  AssertOpenPath(sub_ptr, "file1", file_ptr, fuchsia::io::OpenFlags::RIGHT_READABLE);
  file_ptr.Unbind();
  AssertOpenPath(sub_ptr, "file1", file_ptr,
                 fuchsia::io::OpenFlags::RIGHT_READABLE | fuchsia::io::OpenFlags::RIGHT_WRITABLE);
  file_ptr.Unbind();
  AssertOpenPath(sub_ptr, "file1", file_ptr, fuchsia::io::OpenFlags::RIGHT_WRITABLE);
  file_ptr.Unbind();
}

}  // namespace
