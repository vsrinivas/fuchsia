// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unordered_set>

#include <block-client/cpp/fake-device.h>
#include <gtest/gtest.h>

#include "src/storage/f2fs/f2fs.h"
#include "unit_lib.h"

namespace f2fs {
namespace {

TEST(FileTest, BlkAddrLevel) {
  srand(time(nullptr));

  uint64_t blockCount = static_cast<uint64_t>(8) * 1024 * 1024 * 1024 / kDefaultSectorSize;
  std::unique_ptr<Bcache> bc;
  unittest_lib::MkfsOnFakeDev(&bc, blockCount);

  std::unique_ptr<F2fs> fs;
  MountOptions options{};
  ASSERT_EQ(options.SetValue(options.GetNameView(kOptInlineDentry), 0), ZX_OK);
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  unittest_lib::MountWithOptions(loop.dispatcher(), options, &bc, &fs);

  fbl::RefPtr<VnodeF2fs> root;
  unittest_lib::CreateRoot(fs.get(), &root);
  fbl::RefPtr<Dir> root_dir = fbl::RefPtr<Dir>::Downcast(std::move(root));

  fbl::RefPtr<fs::Vnode> test_file;
  ASSERT_EQ(root_dir->Create("test", S_IFREG, &test_file), ZX_OK);

  fbl::RefPtr<VnodeF2fs> test_file_vn = fbl::RefPtr<VnodeF2fs>::Downcast(std::move(test_file));
  File *test_file_ptr = static_cast<File *>(test_file_vn.get());

  char buf[kPageSize];
  unsigned int level = 0;

  for (size_t i = 0; i < kPageSize; i++) {
    buf[i] = rand() % 256;
  }

  // fill kAddrsPerInode blocks
  for (int i = 0; i < kAddrsPerInode; i++) {
    unittest_lib::AppendToFile(test_file_ptr, buf, kPageSize);
  }

  // check direct node #1 is not available yet
  unittest_lib::CheckNodeLevel(fs.get(), test_file_ptr, level);

  // fill one more block
  unittest_lib::AppendToFile(test_file_ptr, buf, kPageSize);

  // check direct node #1 is available
  unittest_lib::CheckNodeLevel(fs.get(), test_file_ptr, ++level);

  // fill direct node #1
  for (int i = 1; i < kAddrsPerBlock; i++) {
    unittest_lib::AppendToFile(test_file_ptr, buf, kPageSize);
  }

  // check direct node #2 is not available yet
  unittest_lib::CheckNodeLevel(fs.get(), test_file_ptr, level);

  // fill one more block
  unittest_lib::AppendToFile(test_file_ptr, buf, kPageSize);

  // check direct node #2 is available
  unittest_lib::CheckNodeLevel(fs.get(), test_file_ptr, ++level);

  // fill direct node #2
  for (int i = 1; i < kAddrsPerBlock; i++) {
    unittest_lib::AppendToFile(test_file_ptr, buf, kPageSize);
  }

  // check indirect node #1 is not available yet
  unittest_lib::CheckNodeLevel(fs.get(), test_file_ptr, level);

  // fill one more block
  unittest_lib::AppendToFile(test_file_ptr, buf, kPageSize);

  // check indirect node #1 is available
  unittest_lib::CheckNodeLevel(fs.get(), test_file_ptr, ++level);

  ASSERT_EQ(test_file_vn->Close(), ZX_OK);
  test_file_vn = nullptr;
  ASSERT_EQ(root_dir->Close(), ZX_OK);
  root_dir = nullptr;

  unittest_lib::Unmount(std::move(fs), &bc);
}

TEST(FileTest, NidAndBlkaddrAllocFree) {
  srand(time(nullptr));

  uint64_t blockCount = static_cast<uint64_t>(8) * 1024 * 1024 * 1024 / kDefaultSectorSize;
  std::unique_ptr<Bcache> bc;
  unittest_lib::MkfsOnFakeDev(&bc, blockCount);

  std::unique_ptr<F2fs> fs;
  MountOptions options{};
  ASSERT_EQ(options.SetValue(options.GetNameView(kOptInlineDentry), 0), ZX_OK);
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  unittest_lib::MountWithOptions(loop.dispatcher(), options, &bc, &fs);

  fbl::RefPtr<VnodeF2fs> root;
  unittest_lib::CreateRoot(fs.get(), &root);
  fbl::RefPtr<Dir> root_dir = fbl::RefPtr<Dir>::Downcast(std::move(root));

  fbl::RefPtr<fs::Vnode> test_file;
  ASSERT_EQ(root_dir->Create("test", S_IFREG, &test_file), ZX_OK);

  fbl::RefPtr<VnodeF2fs> test_file_vn = fbl::RefPtr<VnodeF2fs>::Downcast(std::move(test_file));
  File *test_file_ptr = static_cast<File *>(test_file_vn.get());

  char buf[kPageSize];

  for (size_t i = 0; i < kPageSize; i++) {
    buf[i] = rand() % 256;
  }

  // Fill until direct nodes are full
  unsigned int level = 2;
  for (int i = 0; i < kAddrsPerInode + kAddrsPerBlock * 2; i++) {
    unittest_lib::AppendToFile(test_file_ptr, buf, kPageSize);
  }

  unittest_lib::CheckNodeLevel(fs.get(), test_file_ptr, level);

  // Build nid and blkaddr set
  std::unordered_set<nid_t> nid_set;
  std::unordered_set<block_t> blkaddr_set;

  nid_set.insert(test_file_ptr->Ino());
  Page *ipage = nullptr;
  ASSERT_EQ(fs->Nodemgr().GetNodePage(test_file_ptr->Ino(), &ipage), ZX_OK);
  Inode *inode = &(static_cast<Node *>(PageAddress(ipage))->i);

  for (int i = 0; i < kNidsPerInode; i++) {
    if (inode->i_nid[i] != 0U)
      nid_set.insert(inode->i_nid[i]);
  }

  for (int i = 0; i < kAddrsPerInode; i++) {
    ASSERT_NE(inode->i_addr[i], kNullAddr);
    blkaddr_set.insert(inode->i_addr[i]);
  }

  for (int i = 0; i < 2; i++) {
    Page *direct_node_page = nullptr;
    ASSERT_EQ(fs->Nodemgr().GetNodePage(inode->i_nid[i], &direct_node_page), ZX_OK);
    DirectNode *direct_node = &(static_cast<Node *>(PageAddress(direct_node_page))->dn);

    for (int j = 0; j < kAddrsPerBlock; j++) {
      ASSERT_NE(direct_node->addr[j], kNullAddr);
      blkaddr_set.insert(direct_node->addr[j]);
    }

    F2fsPutPage(direct_node_page, 0);
  }

  F2fsPutPage(ipage, 0);

  ASSERT_EQ(nid_set.size(), level + 1);
  ASSERT_EQ(blkaddr_set.size(), static_cast<uint32_t>(kAddrsPerInode + kAddrsPerBlock * 2));

  // After writing checkpoint, check if nids are removed from free nid list
  // Also, for allocated blkaddr, check if corresponding bit is set in valid bitmap of segment
  fs->WriteCheckpoint(false, false);

  unittest_lib::CheckNidsInuse(fs.get(), nid_set);
  unittest_lib::CheckBlkaddrsInuse(fs.get(), blkaddr_set);

  // Remove file, writing checkpoint, then check if nids are added to free nid list
  // Also, for allocated blkaddr, check if corresponding bit is cleared in valid bitmap of segment
  ASSERT_EQ(test_file_vn->Close(), ZX_OK);
  test_file_vn = nullptr;

  root_dir->Unlink("test", false);
  fs->WriteCheckpoint(false, false);

  unittest_lib::CheckNidsFree(fs.get(), nid_set);
  unittest_lib::CheckBlkaddrsFree(fs.get(), blkaddr_set);

  ASSERT_EQ(root_dir->Close(), ZX_OK);
  root_dir = nullptr;

  unittest_lib::Unmount(std::move(fs), &bc);
}

}  // namespace
}  // namespace f2fs
