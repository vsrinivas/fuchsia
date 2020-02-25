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
  ASSERT_OK(root->Create(&file, "foobar", S_IFREG));
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

TEST(MemfsTest, UpdateTimeLargeFile) {
  std::unique_ptr<Vfs> vfs;
  fbl::RefPtr<VnodeDir> root;
  ASSERT_OK(Vfs::Create("<tmp>", &vfs, &root));
  fbl::RefPtr<fs::Vnode> file;
  ASSERT_OK(root->Create(&file, "foobar", S_IFREG));

  // Truncate the file to "half a page less than 512MB".
  //
  // 512MB is the maximum memfs file size; observe that writing
  // up to the file size updates the underlying modified time.
  //
  // This catches a regression where previously, time was not updated
  // when ZX_ERR_FILE_BIG was returned.
  size_t offset = (512 * 1024 * 1024) - PAGE_SIZE / 2;
  ASSERT_OK(file->Truncate(offset));
  fs::VnodeAttributes before_file_attr, after_file_attr;
  ASSERT_OK(file->GetAttributes(&before_file_attr));
  size_t actual = 3984u;
  uint8_t buf[PAGE_SIZE]{};
  ASSERT_EQ(ZX_OK, file->Write(buf, PAGE_SIZE, offset, &actual));
  ASSERT_EQ(PAGE_SIZE / 2, actual);
  ASSERT_OK(file->GetAttributes(&after_file_attr));

  ASSERT_EQ(after_file_attr.creation_time, before_file_attr.creation_time);
  ASSERT_GT(after_file_attr.modification_time, before_file_attr.modification_time);
}

TEST(MemfsTest, SubdirectoryUpdateTime) {
  std::unique_ptr<Vfs> vfs;
  fbl::RefPtr<VnodeDir> root;
  ASSERT_OK(Vfs::Create("<tmp>", &vfs, &root));
  fbl::RefPtr<fs::Vnode> index;
  ASSERT_OK(root->Create(&index, "index", S_IFREG));
  fbl::RefPtr<fs::Vnode> subdirectory;
  ASSERT_OK(root->Create(&subdirectory, "subdirectory", S_IFDIR));

  // Write a file at "subdirectory/file".
  fbl::RefPtr<fs::Vnode> file;
  ASSERT_OK(subdirectory->Create(&file, "file", S_IFREG));
  uint8_t buf[PAGE_SIZE]{};
  size_t actual;
  ASSERT_OK(file->Write(buf, PAGE_SIZE, 0, &actual));
  ASSERT_EQ(PAGE_SIZE, actual);

  // Overwrite a file at "index".
  ASSERT_OK(index->Write(buf, PAGE_SIZE, 0, &actual));
  ASSERT_EQ(PAGE_SIZE, actual);

  fs::VnodeAttributes subdirectory_attr, index_attr;
  ASSERT_OK(subdirectory->GetAttributes(&subdirectory_attr));
  ASSERT_OK(index->GetAttributes(&index_attr));

  // "index" was written after "subdirectory".
  ASSERT_LE(subdirectory_attr.modification_time, index_attr.modification_time);
}

}  // namespace
}  // namespace memfs
