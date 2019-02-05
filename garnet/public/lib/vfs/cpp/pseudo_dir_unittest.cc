// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/vfs/cpp/pseudo_dir.h"

#include <lib/fdio/vfs.h>
#include <lib/gtest/real_loop_fixture.h>
#include <lib/vfs/cpp/pseudo_file.h>

namespace {

class TestNode : public vfs::Node {
 public:
  TestNode(std::function<void()> death_callback = nullptr)
      : death_callback_(death_callback) {}
  ~TestNode() override {
    if (death_callback_) {
      death_callback_();
    }
  }

 private:
  bool IsDirectory() const override { return false; }

  void Describe(fuchsia::io::NodeInfo* out_info) override {}

  zx_status_t CreateConnection(
      uint32_t flags, std::unique_ptr<vfs::Connection>* connection) override {
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
    vfs::Node* n;
    ASSERT_EQ(ZX_OK, dir_.Lookup(node_names_[i], &n))
        << "for " << node_names_[i];
    ASSERT_EQ(nodes_[i].get(), n) << "for " << node_names_[i];
  }
}

TEST_F(PseudoDirUnit, LookupUniqueNode) {
  Init(1);

  auto node = std::make_unique<TestNode>();
  vfs::Node* node_ptr = node.get();
  ASSERT_EQ(ZX_OK, dir_.AddEntry("un", std::move(node)));
  vfs::Node* n;
  ASSERT_EQ(ZX_OK, dir_.Lookup(node_names_[0], &n));
  ASSERT_EQ(nodes_[0].get(), n);

  ASSERT_EQ(ZX_OK, dir_.Lookup("un", &n));
  ASSERT_EQ(node_ptr, n);
}

TEST_F(PseudoDirUnit, InvalidLookup) {
  Init(3);
  vfs::Node* n;
  ASSERT_EQ(ZX_ERR_NOT_FOUND, dir_.Lookup("invalid", &n));
}

TEST_F(PseudoDirUnit, RemoveEntry) {
  Init(5);
  for (int i = 0; i < 5; i++) {
    ASSERT_EQ(2, nodes_[i].use_count());
    ASSERT_EQ(ZX_OK, dir_.RemoveEntry(node_names_[i]))
        << "for " << node_names_[i];

    // cannot access
    vfs::Node* n;
    ASSERT_EQ(ZX_ERR_NOT_FOUND, dir_.Lookup(node_names_[i], &n))
        << "for " << node_names_[i];
    // check that use count went doen by 1
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

  vfs::Node* n;
  ASSERT_EQ(ZX_ERR_NOT_FOUND, dir_.Lookup("un", &n));
}

TEST_F(PseudoDirUnit, RemoveInvalidEntry) {
  Init(5);
  ASSERT_EQ(ZX_ERR_NOT_FOUND, dir_.RemoveEntry("invalid"));

  // make sure nothing was removed
  for (int i = 0; i < 5; i++) {
    vfs::Node* n;
    ASSERT_EQ(ZX_OK, dir_.Lookup(node_names_[i], &n))
        << "for " << node_names_[i];
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
    vfs::Node* n;
    ASSERT_EQ(expected_status, dir_.Lookup(node_names_[i], &n))
        << "for " << node_names_[i];
    if (expected_status == ZX_OK) {
      ASSERT_EQ(nodes_[i].get(), n) << "for " << node_names_[i];
    }
  }

  vfs::Node* n;
  ASSERT_EQ(ZX_OK, dir_.Lookup("new_node", &n));
  ASSERT_EQ(new_node.get(), n);
}

class Dirent {
 public:
  uint64_t ino_;
  uint8_t type_;
  uint8_t size_;
  std::string name_;

  uint64_t size_in_bytes_;

  static Dirent DirentForDot() { return DirentForDirectory("."); }

  static Dirent DirentForDirectory(const std::string& name) {
    return Dirent(fuchsia::io::INO_UNKNOWN, fuchsia::io::DIRENT_TYPE_DIRECTORY,
                  name);
  }

  static Dirent DirentForFile(const std::string& name) {
    return Dirent(fuchsia::io::INO_UNKNOWN, fuchsia::io::DIRENT_TYPE_FILE,
                  name);
  }

  std::string String() {
    return "Dirent:\nino: " + std ::to_string(ino_) +
           "\ntype: " + std ::to_string(type_) +
           "\nsize: " + std ::to_string(size_) + "\nname: " + name_;
  }

 private:
  Dirent(uint64_t ino, uint8_t type, const std::string& name)
      : ino_(ino),
        type_(type),
        size_(static_cast<uint8_t>(name.length())),
        name_(name) {
    ZX_DEBUG_ASSERT(name.length() <= static_cast<uint64_t>(NAME_MAX));
    size_in_bytes_ = sizeof(vdirent_t) + size_;
  }
};

class DirectoryWrapper {
 public:
  DirectoryWrapper() : loop_(&kAsyncLoopConfigNoAttachToThread) {
    loop_.StartThread("vfs test thread");
  }

  void AddEntry(const std::string& name, std::unique_ptr<vfs::Node> node,
                zx_status_t expected_status = ZX_OK) {
    ASSERT_EQ(expected_status, dir_.AddEntry(name, std::move(node)));
  }

  void AddSharedEntry(const std::string& name, std::shared_ptr<vfs::Node> node,
                      zx_status_t expected_status = ZX_OK) {
    ASSERT_EQ(expected_status, dir_.AddSharedEntry(name, std::move(node)));
  }

  fuchsia::io::DirectorySyncPtr Serve(
      int flags = fuchsia::io::OPEN_RIGHT_READABLE) {
    fuchsia::io::DirectorySyncPtr ptr;
    dir_.Serve(flags, ptr.NewRequest().TakeChannel(), loop_.dispatcher());
    return ptr;
  }

  void AddReadOnlyFile(const std::string& file_name,
                       const std::string& file_content,
                       zx_status_t expected_status = ZX_OK) {
    auto read_fn = [file_content](std::vector<uint8_t>* output) {
      output->resize(file_content.length());
      std::copy(file_content.begin(), file_content.end(), output->begin());
      return ZX_OK;
    };

    auto file =
        std::make_unique<vfs::BufferedPseudoFile>(std::move(read_fn), nullptr);

    AddEntry(file_name, std::move(file));
  }

  vfs::PseudoDir& dir() { return dir_; };

 private:
  vfs::PseudoDir dir_;
  async::Loop loop_;
};

class PseudoDirConnection : public gtest::RealLoopFixture {
 protected:
  void AssertReadDirents(fuchsia::io::DirectorySyncPtr& ptr, uint64_t max_bytes,
                         std::vector<Dirent>& expected_dirents,
                         zx_status_t expected_status = ZX_OK) {
    std::vector<uint8_t> out_dirents;
    zx_status_t status;
    ptr->ReadDirents(max_bytes, &status, &out_dirents);
    ASSERT_EQ(expected_status, status);
    if (status != ZX_OK) {
      return;
    }
    uint64_t expected_size = 0;
    for (auto& d : expected_dirents) {
      expected_size += d.size_in_bytes_;
    }
    EXPECT_EQ(expected_size, out_dirents.size());
    uint64_t offset = 0;
    auto data_ptr = out_dirents.data();
    for (auto& d : expected_dirents) {
      SCOPED_TRACE(d.String());
      ASSERT_LE(sizeof(vdirent_t), out_dirents.size() - offset);
      vdirent_t* de = reinterpret_cast<vdirent_t*>(data_ptr + offset);
      EXPECT_EQ(d.ino_, de->ino);
      EXPECT_EQ(d.size_, de->size);
      EXPECT_EQ(d.type_, de->type);
      ASSERT_LE(d.size_in_bytes_, out_dirents.size() - offset);
      EXPECT_EQ(d.name_, std::string(de->name, de->size));

      offset += sizeof(vdirent_t) + de->size;
    }
  }

  void AssertRewind(fuchsia::io::DirectorySyncPtr& ptr,
                    zx_status_t expected_status = ZX_OK) {
    zx_status_t status;
    ptr->Rewind(&status);
    ASSERT_EQ(expected_status, status);
  }

  void AssertOpen(async_dispatcher_t* dispatcher, uint32_t flags,
                  zx_status_t expected_status, bool test_on_open_event = true) {
    fuchsia::io::NodePtr node_ptr;
    if (test_on_open_event) {
      flags |= fuchsia::io::OPEN_FLAG_DESCRIBE;
    }
    EXPECT_EQ(expected_status,
              dir_.dir().Serve(flags, node_ptr.NewRequest().TakeChannel(),
                               dispatcher));

    if (test_on_open_event) {
      bool on_open_called = false;
      node_ptr.events().OnOpen =
          [&](zx_status_t status, std::unique_ptr<fuchsia::io::NodeInfo> info) {
            EXPECT_FALSE(on_open_called);  // should be called only once
            on_open_called = true;
            EXPECT_EQ(expected_status, status);
            if (expected_status == ZX_OK) {
              ASSERT_NE(info.get(), nullptr);
              EXPECT_TRUE(info->is_directory());
            } else {
              EXPECT_EQ(info.get(), nullptr);
            }
          };

      ASSERT_TRUE(RunLoopWithTimeoutOrUntil([&]() { return on_open_called; }));
    }
  }

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
  AssertReadDirents(ptr, 1024, expected_dirents);
}

TEST_F(PseudoDirConnection, ReadDirOnEmptyDirectory) {
  auto ptr = dir_.Serve();

  std::vector<Dirent> expected_dirents = {
      Dirent::DirentForDot(),
  };
  AssertReadDirents(ptr, 1024, expected_dirents);
}

TEST_F(PseudoDirConnection, ReadDirSizeLessThanFirstEntry) {
  auto subdir = std::make_shared<vfs::PseudoDir>();

  auto ptr = dir_.Serve();

  std::vector<Dirent> expected_dirents;
  AssertReadDirents(ptr, sizeof(vdirent_t), expected_dirents,
                    ZX_ERR_INVALID_ARGS);
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
    exact_size += d.size_in_bytes_;
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
    exact_size1 += d.size_in_bytes_;
  }

  uint64_t exact_size2 = 0;
  for (auto& d : expected_dirents2) {
    exact_size2 += d.size_in_bytes_;
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

  AssertReadDirents(ptr, 1024, expected_dirents);
  AssertReadDirents(ptr, 1024, empty_dirents);
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

  AssertReadDirents(ptr, 1024, expected_dirents);
  AssertReadDirents(ptr, 1024, empty_dirents);

  AssertRewind(ptr);

  AssertReadDirents(ptr, 1024, expected_dirents);
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
  AssertReadDirents(ptr, 1024, expected_dirents1);

  dir_.AddReadOnlyFile("file1", "file1");
  std::vector<Dirent> expected_dirents2 = {
      Dirent::DirentForFile("file1"),
  };
  AssertReadDirents(ptr, 1024, expected_dirents2);
}

TEST_F(PseudoDirConnection, ReadDirAndRewindAfterAddingEntry) {
  auto subdir = std::make_shared<vfs::PseudoDir>();
  dir_.AddSharedEntry("subdir", subdir);

  auto ptr = dir_.Serve();

  std::vector<Dirent> expected_dirents1 = {
      Dirent::DirentForDot(),
      Dirent::DirentForDirectory("subdir"),
  };
  AssertReadDirents(ptr, 1024, expected_dirents1);

  dir_.AddReadOnlyFile("file1", "file1");
  AssertRewind(ptr);
  std::vector<Dirent> expected_dirents2 = {
      Dirent::DirentForDot(),
      Dirent::DirentForDirectory("subdir"),
      Dirent::DirentForFile("file1"),
  };
  AssertReadDirents(ptr, 1024, expected_dirents2);
}

TEST_F(PseudoDirConnection, ReadDirAfterRemovingEntry) {
  auto subdir = std::make_shared<vfs::PseudoDir>();
  dir_.AddSharedEntry("subdir", subdir);

  auto ptr = dir_.Serve();

  std::vector<Dirent> expected_dirents1 = {
      Dirent::DirentForDot(),
      Dirent::DirentForDirectory("subdir"),
  };
  AssertReadDirents(ptr, 1024, expected_dirents1);
  std::vector<Dirent> empty_dirents;
  ASSERT_EQ(ZX_OK, dir_.dir().RemoveEntry("subdir"));
  AssertReadDirents(ptr, 1024, empty_dirents);

  // rewind and check again
  AssertRewind(ptr);

  std::vector<Dirent> expected_dirents2 = {
      Dirent::DirentForDot(),
  };
  AssertReadDirents(ptr, 1024, expected_dirents2);
}

TEST_F(PseudoDirConnection, CantReadNodeReferenceDir) {
  auto ptr = dir_.Serve(fuchsia::io::OPEN_FLAG_NODE_REFERENCE);
  // make sure node reference was opened
  zx_status_t status;
  fuchsia::io::NodeAttributes attr;
  ASSERT_EQ(ZX_OK, ptr->GetAttr(&status, &attr));
  ASSERT_EQ(ZX_OK, status);
  ASSERT_NE(0u, attr.mode | fuchsia::io::MODE_TYPE_DIRECTORY);

  std::vector<uint8_t> out_dirents;
  ASSERT_EQ(ZX_ERR_PEER_CLOSED, ptr->ReadDirents(100, &status, &out_dirents));
}

TEST_F(PseudoDirConnection, ServeOnInValidFlags) {
  uint32_t prohibitive_flags[] = {fuchsia::io::OPEN_RIGHT_ADMIN,
                                  fuchsia::io::OPEN_FLAG_NO_REMOTE};
  uint32_t not_allowed_flags[] = {
      fuchsia::io::OPEN_FLAG_CREATE, fuchsia::io::OPEN_FLAG_CREATE_IF_ABSENT,
      fuchsia::io::OPEN_FLAG_TRUNCATE, fuchsia::io::OPEN_FLAG_APPEND};

  for (auto not_allowed_flag : not_allowed_flags) {
    SCOPED_TRACE(std::to_string(not_allowed_flag));
    AssertOpen(dispatcher(), not_allowed_flag, ZX_ERR_INVALID_ARGS);
  }

  for (auto prohibitive_flag : prohibitive_flags) {
    SCOPED_TRACE(std::to_string(prohibitive_flag));
    AssertOpen(dispatcher(), prohibitive_flag, ZX_ERR_NOT_SUPPORTED);
  }
}

TEST_F(PseudoDirConnection, ServeOnValidFlags) {
  uint32_t allowed_flags[] = {
      fuchsia::io::OPEN_RIGHT_READABLE, fuchsia::io::OPEN_RIGHT_WRITABLE,
      fuchsia::io::OPEN_FLAG_NODE_REFERENCE, fuchsia::io::OPEN_FLAG_DIRECTORY};
  for (auto allowed_flag : allowed_flags) {
    SCOPED_TRACE(std::to_string(allowed_flag));
    AssertOpen(dispatcher(), allowed_flag, ZX_OK);
  }
}

}  // namespace
