// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/memfs/cpp/vnode.h>
#include <sys/stat.h>

#include <zxtest/zxtest.h>

namespace memfs {
namespace {

TEST(MemfsTest, DirectoryLifetime) {
  std::unique_ptr<Vfs> vfs;
  fbl::RefPtr<VnodeDir> root;
  ASSERT_OK(Vfs::Create("<tmp>", &vfs, &root));
}

TEST(MemfsTest, CreateFile) {
  std::unique_ptr<Vfs> vfs;
  fbl::RefPtr<VnodeDir> root;
  ASSERT_OK(Vfs::Create("<tmp>", &vfs, &root));
  fbl::RefPtr<fs::Vnode> file;
  ASSERT_OK(root->Create("foobar", S_IFREG, &file));
  auto directory = static_cast<fbl::RefPtr<fs::Vnode>>(root);
  fs::VnodeAttributes directory_attr, file_attr;
  ASSERT_OK(directory->GetAttributes(&directory_attr));
  ASSERT_OK(file->GetAttributes(&file_attr));

  // Directory created before file.
  ASSERT_LE(directory_attr.creation_time, file_attr.creation_time);

  // Observe that the modify time of the directory is larger than the file.
  // This implies "the file is created, then the directory is updated".
  ASSERT_GE(directory_attr.modification_time, file_attr.modification_time);
}

TEST(MemfsTest, SubdirectoryUpdateTime) {
  std::unique_ptr<Vfs> vfs;
  fbl::RefPtr<VnodeDir> root;
  ASSERT_OK(Vfs::Create("<tmp>", &vfs, &root));
  fbl::RefPtr<fs::Vnode> index;
  ASSERT_OK(root->Create("index", S_IFREG, &index));
  fbl::RefPtr<fs::Vnode> subdirectory;
  ASSERT_OK(root->Create("subdirectory", S_IFDIR, &subdirectory));

  // Write a file at "subdirectory/file".
  fbl::RefPtr<fs::Vnode> file;
  ASSERT_OK(subdirectory->Create("file", S_IFREG, &file));
  file->DidModifyStream();

  // Overwrite a file at "index".
  index->DidModifyStream();

  fs::VnodeAttributes subdirectory_attr, index_attr;
  ASSERT_OK(subdirectory->GetAttributes(&subdirectory_attr));
  ASSERT_OK(index->GetAttributes(&index_attr));

  // "index" was written after "subdirectory".
  ASSERT_LE(subdirectory_attr.modification_time, index_attr.modification_time);
}

}  // namespace
}  // namespace memfs
