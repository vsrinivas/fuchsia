// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unordered_set>

#include <gtest/gtest.h>

#include "src/lib/storage/block_client/cpp/fake_block_device.h"
#include "src/storage/f2fs/f2fs.h"
#include "unit_lib.h"

namespace f2fs {
namespace {

TEST(InlineDirTest, InlineDirCreation) {
  std::unique_ptr<Bcache> bc;
  FileTester::MkfsOnFakeDev(&bc);

  std::unique_ptr<F2fs> fs;
  MountOptions options{};
  // Enable inline dir option
  ASSERT_EQ(options.SetValue(options.GetNameView(kOptInlineDentry), 1), ZX_OK);
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  FileTester::MountWithOptions(loop.dispatcher(), options, &bc, &fs);

  fbl::RefPtr<VnodeF2fs> root;
  FileTester::CreateRoot(fs.get(), &root);

  fbl::RefPtr<Dir> root_dir = fbl::RefPtr<Dir>::Downcast(std::move(root));

  // Inline dir creation
  std::string inline_dir_name("inline");
  fbl::RefPtr<fs::Vnode> inline_child;
  ASSERT_EQ(root_dir->Create(inline_dir_name, S_IFDIR, &inline_child), ZX_OK);

  fbl::RefPtr<VnodeF2fs> inline_child_dir =
      fbl::RefPtr<VnodeF2fs>::Downcast(std::move(inline_child));

  FileTester::CheckInlineDir(inline_child_dir.get());

  ASSERT_EQ(inline_child_dir->Close(), ZX_OK);
  inline_child_dir = nullptr;
  ASSERT_EQ(root_dir->Close(), ZX_OK);
  root_dir = nullptr;

  FileTester::Unmount(std::move(fs), &bc);

  // Disable inline dir option
  ASSERT_EQ(options.SetValue(options.GetNameView(kOptInlineDentry), 0), ZX_OK);
  FileTester::MountWithOptions(loop.dispatcher(), options, &bc, &fs);

  FileTester::CreateRoot(fs.get(), &root);
  root_dir = fbl::RefPtr<Dir>::Downcast(std::move(root));

  // Check if existing inline dir is still inline regardless of mount option
  FileTester::Lookup(root_dir.get(), inline_dir_name, &inline_child);
  inline_child_dir = fbl::RefPtr<VnodeF2fs>::Downcast(std::move(inline_child));
  FileTester::CheckInlineDir(inline_child_dir.get());

  // However, newly created dir should be non-inline
  std::string non_inline_dir_name("noninline");
  fbl::RefPtr<fs::Vnode> non_inline_child;
  ASSERT_EQ(root_dir->Create(non_inline_dir_name, S_IFDIR, &non_inline_child), ZX_OK);

  fbl::RefPtr<VnodeF2fs> non_inline_child_dir =
      fbl::RefPtr<VnodeF2fs>::Downcast(std::move(non_inline_child));
  FileTester::CheckNonInlineDir(non_inline_child_dir.get());

  ASSERT_EQ(inline_child_dir->Close(), ZX_OK);
  inline_child_dir = nullptr;
  ASSERT_EQ(non_inline_child_dir->Close(), ZX_OK);
  non_inline_child_dir = nullptr;
  ASSERT_EQ(root_dir->Close(), ZX_OK);
  root_dir = nullptr;

  FileTester::Unmount(std::move(fs), &bc);
  EXPECT_EQ(Fsck(std::move(bc), FsckOptions{.repair = false}, &bc), ZX_OK);
}

TEST(InlineDirTest, InlineDirConvert) {
  std::unique_ptr<Bcache> bc;
  FileTester::MkfsOnFakeDev(&bc);

  std::unique_ptr<F2fs> fs;
  MountOptions options{};
  // Enable inline dir option
  ASSERT_EQ(options.SetValue(options.GetNameView(kOptInlineDentry), 1), ZX_OK);
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  FileTester::MountWithOptions(loop.dispatcher(), options, &bc, &fs);

  fbl::RefPtr<VnodeF2fs> root;
  FileTester::CreateRoot(fs.get(), &root);

  fbl::RefPtr<Dir> root_dir = fbl::RefPtr<Dir>::Downcast(std::move(root));

  // Inline dir creation
  std::string inline_dir_name("inline");
  fbl::RefPtr<fs::Vnode> inline_child;
  ASSERT_EQ(root_dir->Create(inline_dir_name, S_IFDIR, &inline_child), ZX_OK);

  fbl::RefPtr<Dir> inline_child_dir = fbl::RefPtr<Dir>::Downcast(std::move(inline_child));

  unsigned int child_count = 0;

  // Fill all slots of inline dentry
  // Since two dentry slots are already allocated for "." and "..", decrease 2 from kNrInlineDentry
  for (; child_count < inline_child_dir->MaxInlineDentry() - 2; ++child_count) {
    uint32_t mode = child_count % 2 == 0 ? S_IFDIR : S_IFREG;
    FileTester::CreateChild(inline_child_dir.get(), mode, std::to_string(child_count));
  }

  // It should be inline
  FileTester::CheckInlineDir(inline_child_dir.get());

  ASSERT_EQ(inline_child_dir->Close(), ZX_OK);
  inline_child_dir = nullptr;
  ASSERT_EQ(root_dir->Close(), ZX_OK);
  root_dir = nullptr;

  FileTester::Unmount(std::move(fs), &bc);

  // Disable inline dir option
  ASSERT_EQ(options.SetValue(options.GetNameView(kOptInlineDentry), 0), ZX_OK);
  FileTester::MountWithOptions(loop.dispatcher(), options, &bc, &fs);

  FileTester::CreateRoot(fs.get(), &root);
  root_dir = fbl::RefPtr<Dir>::Downcast(std::move(root));

  // Check if existing inline dir is still inline regardless of mount option
  FileTester::Lookup(root_dir.get(), inline_dir_name, &inline_child);
  inline_child_dir = fbl::RefPtr<Dir>::Downcast(std::move(inline_child));
  FileTester::CheckInlineDir(inline_child_dir.get());

  // If one more dentry is added, it should be converted to non-inline dir
  uint32_t mode = child_count % 2 == 0 ? S_IFDIR : S_IFREG;
  FileTester::CreateChild(inline_child_dir.get(), mode, std::to_string(child_count));

  FileTester::CheckNonInlineDir(inline_child_dir.get());

  ASSERT_EQ(inline_child_dir->Close(), ZX_OK);
  inline_child_dir = nullptr;
  ASSERT_EQ(root_dir->Close(), ZX_OK);
  root_dir = nullptr;

  FileTester::Unmount(std::move(fs), &bc);
  EXPECT_EQ(Fsck(std::move(bc), FsckOptions{.repair = false}, &bc), ZX_OK);
}

TEST(InlineDirTest, InlineDentryOps) {
  std::unique_ptr<Bcache> bc;
  FileTester::MkfsOnFakeDev(&bc);

  std::unique_ptr<F2fs> fs;
  MountOptions options{};
  // Enable inline dir option
  ASSERT_EQ(options.SetValue(options.GetNameView(kOptInlineDentry), 1), ZX_OK);
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  FileTester::MountWithOptions(loop.dispatcher(), options, &bc, &fs);

  fbl::RefPtr<VnodeF2fs> root;
  FileTester::CreateRoot(fs.get(), &root);

  fbl::RefPtr<Dir> root_dir = fbl::RefPtr<Dir>::Downcast(std::move(root));

  // Inline dir creation
  std::string inline_dir_name("inline");
  fbl::RefPtr<fs::Vnode> inline_child;
  ASSERT_EQ(root_dir->Create(inline_dir_name, S_IFDIR, &inline_child), ZX_OK);

  fbl::RefPtr<Dir> inline_child_dir = fbl::RefPtr<Dir>::Downcast(std::move(inline_child));

  std::unordered_set<std::string> child_set = {"a", "b", "c", "d", "e"};

  Dir *dir_ptr = inline_child_dir.get();

  for (auto iter : child_set) {
    FileTester::CreateChild(dir_ptr, S_IFDIR, iter);
  }
  FileTester::CheckChildrenFromReaddir(dir_ptr, child_set);

  // remove "b" and "d"
  ASSERT_EQ(dir_ptr->Unlink("b", true), ZX_OK);
  child_set.erase("b");
  ASSERT_EQ(dir_ptr->Unlink("d", true), ZX_OK);
  child_set.erase("d");
  FileTester::CheckChildrenFromReaddir(dir_ptr, child_set);

  // create "f" and "g"
  FileTester::CreateChild(dir_ptr, S_IFDIR, "f");
  child_set.insert("f");
  FileTester::CreateChild(dir_ptr, S_IFDIR, "g");
  child_set.insert("g");
  FileTester::CheckChildrenFromReaddir(dir_ptr, child_set);

  // rename "g" to "h"
  ASSERT_EQ(dir_ptr->Rename(inline_child_dir, "g", "h", true, true), ZX_OK);
  child_set.erase("g");
  child_set.insert("h");
  FileTester::CheckChildrenFromReaddir(dir_ptr, child_set);

  // fill all inline dentry slots
  auto child_count = child_set.size();
  for (; child_count < inline_child_dir->MaxInlineDentry() - 2; ++child_count) {
    FileTester::CreateChild(dir_ptr, S_IFDIR, std::to_string(child_count));
    child_set.insert(std::to_string(child_count));
  }
  FileTester::CheckChildrenFromReaddir(dir_ptr, child_set);

  // It should be inline
  FileTester::CheckInlineDir(dir_ptr);

  // one more entry
  FileTester::CreateChild(dir_ptr, S_IFDIR, std::to_string(child_count));
  child_set.insert(std::to_string(child_count));
  FileTester::CheckChildrenFromReaddir(dir_ptr, child_set);

  // It should be non inline
  FileTester::CheckNonInlineDir(dir_ptr);

  ASSERT_EQ(inline_child_dir->Close(), ZX_OK);
  inline_child_dir = nullptr;
  ASSERT_EQ(root_dir->Close(), ZX_OK);
  root_dir = nullptr;
  FileTester::Unmount(std::move(fs), &bc);

  // Check dentry after remount
  FileTester::MountWithOptions(loop.dispatcher(), options, &bc, &fs);

  FileTester::CreateRoot(fs.get(), &root);
  root_dir = fbl::RefPtr<Dir>::Downcast(std::move(root));

  FileTester::Lookup(root_dir.get(), inline_dir_name, &inline_child);
  inline_child_dir = fbl::RefPtr<Dir>::Downcast(std::move(inline_child));
  dir_ptr = inline_child_dir.get();

  FileTester::CheckNonInlineDir(dir_ptr);
  FileTester::CheckChildrenFromReaddir(dir_ptr, child_set);

  ASSERT_EQ(inline_child_dir->Close(), ZX_OK);
  inline_child_dir = nullptr;
  ASSERT_EQ(root_dir->Close(), ZX_OK);
  root_dir = nullptr;

  FileTester::Unmount(std::move(fs), &bc);
  EXPECT_EQ(Fsck(std::move(bc), FsckOptions{.repair = false}, &bc), ZX_OK);
}

TEST(InlineDirTest, NestedInlineDirectories) {
  // There was a reported malfunction of inline-directories when the volume size is small.
  // This test evaluates such case.
  std::unique_ptr<Bcache> bc;
  FileTester::MkfsOnFakeDev(&bc, 102400, 512);

  std::unique_ptr<F2fs> fs;
  MountOptions options{};
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  FileTester::MountWithOptions(loop.dispatcher(), options, &bc, &fs);

  fbl::RefPtr<VnodeF2fs> root;
  FileTester::CreateRoot(fs.get(), &root);
  fbl::RefPtr<Dir> root_dir = fbl::RefPtr<Dir>::Downcast(std::move(root));

  fbl::RefPtr<fs::Vnode> vnode;
  ASSERT_EQ(root_dir->Create(std::string("alpha"), S_IFDIR, &vnode), ZX_OK);
  fbl::RefPtr<Dir> parent_dir = fbl::RefPtr<Dir>::Downcast(std::move(vnode));

  ASSERT_EQ(parent_dir->Create(std::string("bravo"), S_IFDIR, &vnode), ZX_OK);
  fbl::RefPtr<Dir> child_dir = fbl::RefPtr<Dir>::Downcast(std::move(vnode));

  ASSERT_EQ(child_dir->Create(std::string("charlie"), S_IFREG, &vnode), ZX_OK);
  fbl::RefPtr<File> child_file = fbl::RefPtr<File>::Downcast(std::move(vnode));

  char data[] = "Hello, world!";
  FileTester::AppendToFile(child_file.get(), data, sizeof(data));

  ASSERT_EQ(child_file->Close(), ZX_OK);
  ASSERT_EQ(child_dir->Close(), ZX_OK);
  ASSERT_EQ(parent_dir->Close(), ZX_OK);
  ASSERT_EQ(root_dir->Close(), ZX_OK);
  root_dir = parent_dir = child_dir = nullptr;
  child_file = nullptr;

  FileTester::Unmount(std::move(fs), &bc);
  EXPECT_EQ(Fsck(std::move(bc), FsckOptions{.repair = false}), ZX_OK);
}

TEST(InlineDirTest, InlineDirPino) {
  std::unique_ptr<Bcache> bc;
  FileTester::MkfsOnFakeDev(&bc);

  std::unique_ptr<F2fs> fs;
  MountOptions options{};

  // Enable inline dir option
  ASSERT_EQ(options.SetValue(options.GetNameView(kOptInlineDentry), 1), ZX_OK);
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  FileTester::MountWithOptions(loop.dispatcher(), options, &bc, &fs);

  fbl::RefPtr<VnodeF2fs> root;
  FileTester::CreateRoot(fs.get(), &root);

  fbl::RefPtr<Dir> root_dir = fbl::RefPtr<Dir>::Downcast(std::move(root));

  // Inline dir creation
  fbl::RefPtr<fs::Vnode> vnode;
  ASSERT_EQ(root_dir->Create("a", S_IFDIR, &vnode), ZX_OK);
  fbl::RefPtr<Dir> a_dir = fbl::RefPtr<Dir>::Downcast(std::move(vnode));
  ASSERT_EQ(a_dir->GetParentNid(), root_dir->Ino());

  ASSERT_EQ(root_dir->Create("b", S_IFDIR, &vnode), ZX_OK);
  fbl::RefPtr<Dir> b_dir = fbl::RefPtr<Dir>::Downcast(std::move(vnode));
  ASSERT_EQ(b_dir->GetParentNid(), root_dir->Ino());

  ASSERT_EQ(a_dir->Create("c", S_IFDIR, &vnode), ZX_OK);
  fbl::RefPtr<Dir> c_dir = fbl::RefPtr<Dir>::Downcast(std::move(vnode));
  ASSERT_EQ(c_dir->GetParentNid(), a_dir->Ino());

  ASSERT_EQ(a_dir->Create("d", S_IFREG, &vnode), ZX_OK);
  fbl::RefPtr<File> d1_file = fbl::RefPtr<File>::Downcast(std::move(vnode));
  ASSERT_EQ(d1_file->GetParentNid(), a_dir->Ino());

  ASSERT_EQ(b_dir->Create("d", S_IFREG, &vnode), ZX_OK);
  fbl::RefPtr<File> d2_file = fbl::RefPtr<File>::Downcast(std::move(vnode));
  ASSERT_EQ(d2_file->GetParentNid(), b_dir->Ino());

  // rename "/a/c" to "/b/c" and "/a/d" to "/b/d"
  ASSERT_EQ(a_dir->Rename(b_dir, "c", "c", true, true), ZX_OK);
  ASSERT_EQ(a_dir->Rename(b_dir, "d", "d", false, false), ZX_OK);

  // Check i_pino of renamed directory
  ASSERT_EQ(c_dir->GetParentNid(), b_dir->Ino());
  ASSERT_EQ(d1_file->GetParentNid(), b_dir->Ino());

  ASSERT_EQ(d1_file->Close(), ZX_OK);
  ASSERT_EQ(d2_file->Close(), ZX_OK);
  ASSERT_EQ(c_dir->Close(), ZX_OK);
  ASSERT_EQ(b_dir->Close(), ZX_OK);
  ASSERT_EQ(a_dir->Close(), ZX_OK);
  ASSERT_EQ(root_dir->Close(), ZX_OK);
  root_dir = a_dir = b_dir = c_dir = nullptr;
  d1_file = d2_file = nullptr;

  // Remount
  FileTester::Unmount(std::move(fs), &bc);
  FileTester::MountWithOptions(loop.dispatcher(), options, &bc, &fs);

  FileTester::CreateRoot(fs.get(), &root);
  root_dir = fbl::RefPtr<Dir>::Downcast(std::move(root));

  FileTester::Lookup(root_dir.get(), "b", &vnode);
  b_dir = fbl::RefPtr<Dir>::Downcast(std::move(vnode));
  FileTester::Lookup(b_dir.get(), "c", &vnode);
  c_dir = fbl::RefPtr<Dir>::Downcast(std::move(vnode));
  FileTester::Lookup(b_dir.get(), "d", &vnode);
  d1_file = fbl::RefPtr<File>::Downcast(std::move(vnode));

  // Check i_pino of renamed directory
  ASSERT_EQ(c_dir->GetParentNid(), b_dir->Ino());
  ASSERT_EQ(d1_file->GetParentNid(), b_dir->Ino());

  ASSERT_EQ(d1_file->Close(), ZX_OK);
  ASSERT_EQ(c_dir->Close(), ZX_OK);
  ASSERT_EQ(b_dir->Close(), ZX_OK);
  ASSERT_EQ(root_dir->Close(), ZX_OK);
  root_dir = b_dir = c_dir = nullptr;
  d1_file = nullptr;

  FileTester::Unmount(std::move(fs), &bc);
  EXPECT_EQ(Fsck(std::move(bc), FsckOptions{.repair = false}, &bc), ZX_OK);
}

TEST(InlineDataTest, InlineRegFileCreation) {
  std::unique_ptr<Bcache> bc;
  FileTester::MkfsOnFakeDev(&bc);

  std::unique_ptr<F2fs> fs;
  MountOptions options{};
  // Enable inline data option
  ASSERT_EQ(options.SetValue(options.GetNameView(kOptInlineData), 1), ZX_OK);
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  FileTester::MountWithOptions(loop.dispatcher(), options, &bc, &fs);

  fbl::RefPtr<VnodeF2fs> root;
  FileTester::CreateRoot(fs.get(), &root);

  fbl::RefPtr<Dir> root_dir = fbl::RefPtr<Dir>::Downcast(std::move(root));

  // Inline file creation
  std::string inline_file_name("inline");
  fbl::RefPtr<fs::Vnode> inline_child;
  ASSERT_EQ(root_dir->Create(inline_file_name, S_IFREG, &inline_child), ZX_OK);

  fbl::RefPtr<VnodeF2fs> inline_child_file =
      fbl::RefPtr<VnodeF2fs>::Downcast(std::move(inline_child));

  FileTester::CheckInlineFile(inline_child_file.get());

  ASSERT_EQ(inline_child_file->Close(), ZX_OK);
  inline_child_file = nullptr;
  ASSERT_EQ(root_dir->Close(), ZX_OK);
  root_dir = nullptr;

  FileTester::Unmount(std::move(fs), &bc);

  // Disable inline data option
  ASSERT_EQ(options.SetValue(options.GetNameView(kOptInlineData), 0), ZX_OK);
  FileTester::MountWithOptions(loop.dispatcher(), options, &bc, &fs);

  FileTester::CreateRoot(fs.get(), &root);
  root_dir = fbl::RefPtr<Dir>::Downcast(std::move(root));

  // Check if existing inline file is still inline regardless of mount option
  FileTester::Lookup(root_dir.get(), inline_file_name, &inline_child);
  inline_child_file = fbl::RefPtr<VnodeF2fs>::Downcast(std::move(inline_child));
  FileTester::CheckInlineFile(inline_child_file.get());

  // However, newly created file should be non-inline
  std::string non_inline_file_name("noninline");
  fbl::RefPtr<fs::Vnode> non_inline_child;
  ASSERT_EQ(root_dir->Create(non_inline_file_name, S_IFREG, &non_inline_child), ZX_OK);

  fbl::RefPtr<VnodeF2fs> non_inline_child_file =
      fbl::RefPtr<VnodeF2fs>::Downcast(std::move(non_inline_child));
  FileTester::CheckNonInlineFile(non_inline_child_file.get());

  ASSERT_EQ(inline_child_file->Close(), ZX_OK);
  inline_child_file = nullptr;
  ASSERT_EQ(non_inline_child_file->Close(), ZX_OK);
  non_inline_child_file = nullptr;
  ASSERT_EQ(root_dir->Close(), ZX_OK);
  root_dir = nullptr;

  FileTester::Unmount(std::move(fs), &bc);
  EXPECT_EQ(Fsck(std::move(bc), FsckOptions{.repair = false}, &bc), ZX_OK);
}

TEST(InlineDataTest, InlineRegFileIO) {
  srand(testing::UnitTest::GetInstance()->random_seed());

  std::unique_ptr<Bcache> bc;
  FileTester::MkfsOnFakeDev(&bc);

  std::unique_ptr<F2fs> fs;
  MountOptions options{};
  // Enable inline data option
  ASSERT_EQ(options.SetValue(options.GetNameView(kOptInlineData), 1), ZX_OK);
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  FileTester::MountWithOptions(loop.dispatcher(), options, &bc, &fs);

  fbl::RefPtr<VnodeF2fs> root;
  FileTester::CreateRoot(fs.get(), &root);

  fbl::RefPtr<Dir> root_dir = fbl::RefPtr<Dir>::Downcast(std::move(root));

  // Inline file creation
  std::string inline_file_name("inline");
  fbl::RefPtr<fs::Vnode> inline_child;
  ASSERT_EQ(root_dir->Create(inline_file_name, S_IFREG, &inline_child), ZX_OK);

  fbl::RefPtr<VnodeF2fs> inline_child_file =
      fbl::RefPtr<VnodeF2fs>::Downcast(std::move(inline_child));

  FileTester::CheckInlineFile(inline_child_file.get());

  // Write and verify
  File *inline_child_file_ptr = static_cast<File *>(inline_child_file.get());

  size_t target_size = inline_child_file_ptr->MaxInlineData() - 1;

  char w_buf[target_size];
  char r_buf[target_size];

  for (size_t i = 0; i < target_size; ++i) {
    w_buf[i] = static_cast<char>(rand());
  }

  FileTester::AppendToFile(inline_child_file_ptr, w_buf, target_size);
  FileTester::CheckInlineFile(inline_child_file.get());
  ASSERT_EQ(inline_child_file_ptr->GetSize(), target_size);

  FileTester::ReadFromFile(inline_child_file_ptr, r_buf, target_size, 0);
  ASSERT_EQ(memcmp(r_buf, w_buf, target_size), 0);

  inline_child_file_ptr = nullptr;
  ASSERT_EQ(inline_child_file->Close(), ZX_OK);
  inline_child_file = nullptr;
  ASSERT_EQ(root_dir->Close(), ZX_OK);
  root_dir = nullptr;

  FileTester::Unmount(std::move(fs), &bc);

  // Remount and verify
  FileTester::MountWithOptions(loop.dispatcher(), options, &bc, &fs);

  FileTester::CreateRoot(fs.get(), &root);
  root_dir = fbl::RefPtr<Dir>::Downcast(std::move(root));

  FileTester::Lookup(root_dir.get(), inline_file_name, &inline_child);
  inline_child_file = fbl::RefPtr<VnodeF2fs>::Downcast(std::move(inline_child));
  FileTester::CheckInlineFile(inline_child_file.get());

  inline_child_file_ptr = static_cast<File *>(inline_child_file.get());
  ASSERT_EQ(inline_child_file_ptr->GetSize(), target_size);

  FileTester::ReadFromFile(inline_child_file_ptr, r_buf, target_size, 0);
  ASSERT_EQ(memcmp(r_buf, w_buf, target_size), 0);

  inline_child_file_ptr = nullptr;
  ASSERT_EQ(inline_child_file->Close(), ZX_OK);
  inline_child_file = nullptr;
  ASSERT_EQ(root_dir->Close(), ZX_OK);
  root_dir = nullptr;

  FileTester::Unmount(std::move(fs), &bc);
  EXPECT_EQ(Fsck(std::move(bc), FsckOptions{.repair = false}, &bc), ZX_OK);
}

TEST(InlineDataTest, InlineRegFileConvert) {
  srand(testing::UnitTest::GetInstance()->random_seed());

  std::unique_ptr<Bcache> bc;
  FileTester::MkfsOnFakeDev(&bc);

  std::unique_ptr<F2fs> fs;
  MountOptions options{};
  // Enable inline data option
  ASSERT_EQ(options.SetValue(options.GetNameView(kOptInlineData), 1), ZX_OK);
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  FileTester::MountWithOptions(loop.dispatcher(), options, &bc, &fs);

  fbl::RefPtr<VnodeF2fs> root;
  FileTester::CreateRoot(fs.get(), &root);

  fbl::RefPtr<Dir> root_dir = fbl::RefPtr<Dir>::Downcast(std::move(root));

  // Inline file creation
  std::string inline_file_name("inline");
  fbl::RefPtr<fs::Vnode> inline_child;
  ASSERT_EQ(root_dir->Create(inline_file_name, S_IFREG, &inline_child), ZX_OK);

  fbl::RefPtr<VnodeF2fs> inline_child_file =
      fbl::RefPtr<VnodeF2fs>::Downcast(std::move(inline_child));

  FileTester::CheckInlineFile(inline_child_file.get());

  // Write until entire inline data space is written
  File *inline_child_file_ptr = static_cast<File *>(inline_child_file.get());
  size_t target_size = inline_child_file_ptr->MaxInlineData() - 1;

  char w_buf[inline_child_file_ptr->MaxInlineData()];
  char r_buf[inline_child_file_ptr->MaxInlineData()];

  for (size_t i = 0; i < inline_child_file_ptr->MaxInlineData(); ++i) {
    w_buf[i] = static_cast<char>(rand());
  }

  FileTester::AppendToFile(inline_child_file_ptr, w_buf, target_size);
  FileTester::CheckInlineFile(inline_child_file.get());
  ASSERT_EQ(inline_child_file_ptr->GetSize(), target_size);

  FileTester::ReadFromFile(inline_child_file_ptr, r_buf, target_size, 0);
  ASSERT_EQ(memcmp(r_buf, w_buf, target_size), 0);

  // Write one more byte, then it should be converted to noinline
  target_size = inline_child_file_ptr->MaxInlineData();

  FileTester::AppendToFile(inline_child_file_ptr, &(w_buf[target_size - 1]), 1);
  FileTester::CheckNonInlineFile(inline_child_file.get());
  ASSERT_EQ(inline_child_file_ptr->GetSize(), target_size);

  FileTester::ReadFromFile(inline_child_file_ptr, r_buf, target_size, 0);
  ASSERT_EQ(memcmp(r_buf, w_buf, target_size), 0);

  inline_child_file_ptr = nullptr;
  ASSERT_EQ(inline_child_file->Close(), ZX_OK);
  inline_child_file = nullptr;
  ASSERT_EQ(root_dir->Close(), ZX_OK);
  root_dir = nullptr;

  FileTester::Unmount(std::move(fs), &bc);

  // Remount and verify
  FileTester::MountWithOptions(loop.dispatcher(), options, &bc, &fs);

  FileTester::CreateRoot(fs.get(), &root);
  root_dir = fbl::RefPtr<Dir>::Downcast(std::move(root));

  FileTester::Lookup(root_dir.get(), inline_file_name, &inline_child);
  inline_child_file = fbl::RefPtr<VnodeF2fs>::Downcast(std::move(inline_child));
  FileTester::CheckNonInlineFile(inline_child_file.get());

  inline_child_file_ptr = static_cast<File *>(inline_child_file.get());
  ASSERT_EQ(inline_child_file_ptr->GetSize(), target_size);

  FileTester::ReadFromFile(inline_child_file_ptr, r_buf, target_size, 0);
  ASSERT_EQ(memcmp(r_buf, w_buf, target_size), 0);

  inline_child_file_ptr = nullptr;
  ASSERT_EQ(inline_child_file->Close(), ZX_OK);
  inline_child_file = nullptr;
  ASSERT_EQ(root_dir->Close(), ZX_OK);
  root_dir = nullptr;

  FileTester::Unmount(std::move(fs), &bc);
  EXPECT_EQ(Fsck(std::move(bc), FsckOptions{.repair = false}, &bc), ZX_OK);
}

TEST(InlineDataTest, InlineRegFileTruncate) {
  srand(testing::UnitTest::GetInstance()->random_seed());

  std::unique_ptr<Bcache> bc;
  FileTester::MkfsOnFakeDev(&bc);

  std::unique_ptr<F2fs> fs;
  MountOptions options{};
  // Enable inline data option
  ASSERT_EQ(options.SetValue(options.GetNameView(kOptInlineData), 1), ZX_OK);
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  FileTester::MountWithOptions(loop.dispatcher(), options, &bc, &fs);

  fbl::RefPtr<VnodeF2fs> root;
  FileTester::CreateRoot(fs.get(), &root);

  fbl::RefPtr<Dir> root_dir = fbl::RefPtr<Dir>::Downcast(std::move(root));

  // Inline file creation
  std::string inline_file_name("inline");
  fbl::RefPtr<fs::Vnode> inline_child;
  ASSERT_EQ(root_dir->Create(inline_file_name, S_IFREG, &inline_child), ZX_OK);

  fbl::RefPtr<VnodeF2fs> inline_child_file =
      fbl::RefPtr<VnodeF2fs>::Downcast(std::move(inline_child));

  FileTester::CheckInlineFile(inline_child_file.get());

  // Write until entire inline data space is written
  File *inline_child_file_ptr = static_cast<File *>(inline_child_file.get());
  size_t target_size = inline_child_file_ptr->MaxInlineData() - 1;

  char w_buf[kPageSize];
  char r_buf[kPageSize];

  for (size_t i = 0; i < kPageSize; ++i) {
    w_buf[i] = static_cast<char>(rand());
  }

  FileTester::AppendToFile(inline_child_file_ptr, w_buf, target_size);
  FileTester::CheckInlineFile(inline_child_file.get());
  ASSERT_EQ(inline_child_file_ptr->GetSize(), target_size);

  FileTester::ReadFromFile(inline_child_file_ptr, r_buf, target_size, 0);
  ASSERT_EQ(memcmp(r_buf, w_buf, target_size), 0);

  // Truncate to reduced size, then verify
  target_size = inline_child_file_ptr->MaxInlineData() / 2;
  ASSERT_EQ(inline_child_file_ptr->Truncate(target_size), ZX_OK);
  FileTester::CheckInlineFile(inline_child_file.get());
  ASSERT_EQ(inline_child_file_ptr->GetSize(), target_size);

  FileTester::ReadFromFile(inline_child_file_ptr, r_buf, target_size, 0);
  ASSERT_EQ(memcmp(r_buf, w_buf, target_size), 0);

  // Truncate to original size, then verify
  target_size = inline_child_file_ptr->MaxInlineData() - 1;

  for (size_t i = inline_child_file_ptr->MaxInlineData() / 2; i < kPageSize; ++i) {
    w_buf[i] = 0;
  }

  ASSERT_EQ(inline_child_file_ptr->Truncate(target_size), ZX_OK);
  FileTester::CheckInlineFile(inline_child_file.get());
  ASSERT_EQ(inline_child_file_ptr->GetSize(), target_size);

  FileTester::ReadFromFile(inline_child_file_ptr, r_buf, target_size, 0);
  ASSERT_EQ(memcmp(r_buf, w_buf, target_size), 0);

  // Truncate to more than inline data size, then verify
  target_size = kPageSize;

  ASSERT_EQ(inline_child_file_ptr->Truncate(kPageSize), ZX_OK);
  FileTester::CheckNonInlineFile(inline_child_file.get());
  ASSERT_EQ(inline_child_file_ptr->GetSize(), target_size);

  FileTester::ReadFromFile(inline_child_file_ptr, r_buf, target_size, 0);
  ASSERT_EQ(memcmp(r_buf, w_buf, target_size), 0);

  inline_child_file_ptr = nullptr;
  ASSERT_EQ(inline_child_file->Close(), ZX_OK);
  inline_child_file = nullptr;
  ASSERT_EQ(root_dir->Close(), ZX_OK);
  root_dir = nullptr;

  FileTester::Unmount(std::move(fs), &bc);

  // Remount and verify
  FileTester::MountWithOptions(loop.dispatcher(), options, &bc, &fs);

  FileTester::CreateRoot(fs.get(), &root);
  root_dir = fbl::RefPtr<Dir>::Downcast(std::move(root));

  FileTester::Lookup(root_dir.get(), inline_file_name, &inline_child);
  inline_child_file = fbl::RefPtr<VnodeF2fs>::Downcast(std::move(inline_child));
  FileTester::CheckNonInlineFile(inline_child_file.get());

  inline_child_file_ptr = static_cast<File *>(inline_child_file.get());
  ASSERT_EQ(inline_child_file_ptr->GetSize(), target_size);

  FileTester::ReadFromFile(inline_child_file_ptr, r_buf, target_size, 0);
  ASSERT_EQ(memcmp(r_buf, w_buf, target_size), 0);

  inline_child_file_ptr = nullptr;
  ASSERT_EQ(inline_child_file->Close(), ZX_OK);
  inline_child_file = nullptr;
  ASSERT_EQ(root_dir->Close(), ZX_OK);
  root_dir = nullptr;

  FileTester::Unmount(std::move(fs), &bc);
  EXPECT_EQ(Fsck(std::move(bc), FsckOptions{.repair = false}, &bc), ZX_OK);
}

TEST(InlineDataTest, DataExistFlag) {
  std::unique_ptr<Bcache> bc;
  FileTester::MkfsOnFakeDev(&bc);

  std::unique_ptr<F2fs> fs;
  MountOptions options{};
  // Enable inline data option
  ASSERT_EQ(options.SetValue(options.GetNameView(kOptInlineData), 1), ZX_OK);
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  FileTester::MountWithOptions(loop.dispatcher(), options, &bc, &fs);

  fbl::RefPtr<VnodeF2fs> root;
  FileTester::CreateRoot(fs.get(), &root);

  fbl::RefPtr<Dir> root_dir = fbl::RefPtr<Dir>::Downcast(std::move(root));

  // Inline file creation, then check if kDataExist flag is unset
  std::string inline_file_name("inline");
  fbl::RefPtr<fs::Vnode> inline_child;
  ASSERT_EQ(root_dir->Create(inline_file_name, S_IFREG, &inline_child), ZX_OK);

  fbl::RefPtr<VnodeF2fs> inline_child_file =
      fbl::RefPtr<VnodeF2fs>::Downcast(std::move(inline_child));

  FileTester::CheckInlineFile(inline_child_file.get());
  FileTester::CheckDataExistFlagUnset(inline_child_file.get());

  // Write some data, then check if kDataExist flag is set
  File *inline_child_file_ptr = static_cast<File *>(inline_child_file.get());
  constexpr std::string_view data_string = "hello";

  FileTester::AppendToFile(inline_child_file_ptr, data_string.data(), data_string.size());
  FileTester::CheckInlineFile(inline_child_file.get());
  ASSERT_EQ(inline_child_file_ptr->GetSize(), data_string.size());
  FileTester::CheckDataExistFlagSet(inline_child_file.get());

  // Truncate to non-zero size, then check if kDataExist flag is set
  ASSERT_EQ(inline_child_file_ptr->Truncate(data_string.size() / 2), ZX_OK);
  FileTester::CheckInlineFile(inline_child_file.get());
  ASSERT_EQ(inline_child_file_ptr->GetSize(), data_string.size() / 2);
  FileTester::CheckDataExistFlagSet(inline_child_file.get());

  // Truncate to zero size, then check if kDataExist flag is unset
  ASSERT_EQ(inline_child_file_ptr->Truncate(0), ZX_OK);
  FileTester::CheckInlineFile(inline_child_file.get());
  ASSERT_EQ(inline_child_file_ptr->GetSize(), 0UL);
  FileTester::CheckDataExistFlagUnset(inline_child_file.get());

  // Write data again, then check if kDataExist flag is set
  FileTester::AppendToFile(inline_child_file_ptr, data_string.data(), data_string.size());
  FileTester::CheckInlineFile(inline_child_file.get());
  ASSERT_EQ(inline_child_file_ptr->GetSize(), data_string.size());
  FileTester::CheckDataExistFlagSet(inline_child_file.get());

  inline_child_file_ptr = nullptr;
  ASSERT_EQ(inline_child_file->Close(), ZX_OK);
  inline_child_file = nullptr;
  ASSERT_EQ(root_dir->Close(), ZX_OK);
  root_dir = nullptr;

  FileTester::Unmount(std::move(fs), &bc);

  // Remount and check if KDataExist flag is still set
  FileTester::MountWithOptions(loop.dispatcher(), options, &bc, &fs);

  FileTester::CreateRoot(fs.get(), &root);
  root_dir = fbl::RefPtr<Dir>::Downcast(std::move(root));

  FileTester::Lookup(root_dir.get(), inline_file_name, &inline_child);
  inline_child_file = fbl::RefPtr<VnodeF2fs>::Downcast(std::move(inline_child));
  FileTester::CheckInlineFile(inline_child_file.get());
  FileTester::CheckDataExistFlagSet(inline_child_file.get());

  ASSERT_EQ(inline_child_file->Close(), ZX_OK);
  inline_child_file = nullptr;
  ASSERT_EQ(root_dir->Close(), ZX_OK);
  root_dir = nullptr;

  FileTester::Unmount(std::move(fs), &bc);
  EXPECT_EQ(Fsck(std::move(bc), FsckOptions{.repair = false}, &bc), ZX_OK);
}

}  // namespace
}  // namespace f2fs
