// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/vfs/cpp/remote_dir.h>

#include <memory>

#include <fuchsia/io/cpp/fidl.h>
#include <lib/fdio/vfs.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/vfs/cpp/pseudo_dir.h>
#include <lib/vfs/cpp/pseudo_file.h>
#include <lib/vfs/cpp/testing/dir_test_util.h>

namespace {

class RemoteDirConnection : public vfs_tests::DirConnection {
 public:
  RemoteDirConnection() : loop_(&kAsyncLoopConfigNoAttachToThread) {
    AddFileToPseudoDir("file1");
    AddFileToPseudoDir("file2");
    AddFileToPseudoDir("file3");
    loop_.StartThread("vfs test thread");
  }

 protected:
  vfs::Directory* GetDirectoryNode() override { return dir_.get(); }

  fuchsia::io::DirectoryPtr GetPseudoDirConnection() {
    fuchsia::io::DirectoryPtr ptr;
    pseudo_dir_.Serve(fuchsia::io::OPEN_RIGHT_READABLE,
                      ptr.NewRequest().TakeChannel(), loop_.dispatcher());
    return ptr;
  }

  void ReadDir(vfs::Directory* dir, std::vector<uint8_t>* dirents,
               uint64_t buffer_size = 1024) {
    fuchsia::io::DirectorySyncPtr ptr;
    dir->Serve(fuchsia::io::OPEN_RIGHT_READABLE, ptr.NewRequest().TakeChannel(),
               loop_.dispatcher());
    zx_status_t status;
    ptr->ReadDirents(buffer_size, &status, dirents);
    ASSERT_EQ(ZX_OK, status);
    ASSERT_GT(dirents->size(), 0u);
  }

  void CompareReadDirs() {
    std::vector<uint8_t> remote_dir_dirents;
    std::vector<uint8_t> pseudo_dir_dirents;
    ReadDir(&pseudo_dir_, &pseudo_dir_dirents);
    ReadDir(dir_.get(), &remote_dir_dirents);
    ASSERT_EQ(remote_dir_dirents, pseudo_dir_dirents);
  }

  vfs::PseudoDir pseudo_dir_;
  std::unique_ptr<vfs::RemoteDir> dir_;
  async::Loop loop_;

 private:
  void AddFileToPseudoDir(const std::string& name) {
    pseudo_dir_.AddEntry(name, std::make_unique<vfs::BufferedPseudoFile>(
                                   [name](std::vector<uint8_t>* output) {
                                     output->resize(name.length());
                                     std::copy(name.begin(), name.end(),
                                               output->begin());
                                     return ZX_OK;
                                   }));
  }
};

TEST_F(RemoteDirConnection, ConstructorWithChannel) {
  auto connection = GetPseudoDirConnection();
  dir_ = std::make_unique<vfs::RemoteDir>(connection.Unbind().TakeChannel());
  CompareReadDirs();
}

TEST_F(RemoteDirConnection, ConstructorWithInterfaceHandle) {
  auto connection = GetPseudoDirConnection();
  dir_ = std::make_unique<vfs::RemoteDir>(connection.Unbind());
  CompareReadDirs();
}

TEST_F(RemoteDirConnection, ConstructorWithDirPtr) {
  dir_ = std::make_unique<vfs::RemoteDir>(GetPseudoDirConnection());
  CompareReadDirs();
}

TEST_F(RemoteDirConnection, RemoteDirContainedInPseudoDir) {
  vfs::PseudoDir containing_dir;
  containing_dir.AddEntry(
      "remote", std::make_unique<vfs::RemoteDir>(GetPseudoDirConnection()));

  fuchsia::io::DirectorySyncPtr ptr;
  containing_dir.Serve(fuchsia::io::OPEN_RIGHT_READABLE,
                       ptr.NewRequest().TakeChannel(), loop_.dispatcher());

  std::vector<vfs_tests::Dirent> expected = {
      vfs_tests::Dirent::DirentForDot(),
      vfs_tests::Dirent::DirentForDirectory("remote")};
  AssertReadDirents(ptr, 1024, expected);
}

}  // namespace
