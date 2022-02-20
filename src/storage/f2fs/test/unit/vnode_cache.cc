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

class VnodeCacheTest : public F2fsFakeDevTestFixture {
 public:
  VnodeCacheTest()
      : F2fsFakeDevTestFixture(TestOptions{.mount_options = {{kOptInlineDentry, 0}}}) {}
};

uint32_t GetCachedVnodeCount(VnodeCache &vnode_cache) {
  uint32_t cached_vnode_count = 0;
  vnode_cache.ForAllVnodes([&cached_vnode_count](fbl::RefPtr<VnodeF2fs> &vnode) {
    ++cached_vnode_count;
    return ZX_OK;
  });
  return cached_vnode_count;
}

uint32_t GetDirtyVnodeCount(VnodeCache &vnode_cache) {
  uint32_t dirty_vnode_count = 0;
  vnode_cache.ForDirtyVnodesIf(
      [&dirty_vnode_count](fbl::RefPtr<VnodeF2fs> &vnode) {
        ++dirty_vnode_count;
        return ZX_OK;
      },
      nullptr);
  return dirty_vnode_count;
}

TEST_F(VnodeCacheTest, Basic) {
  fbl::RefPtr<fs::Vnode> test_dir;
  ASSERT_EQ(root_dir_->Create("test", S_IFDIR, &test_dir), ZX_OK);

  fbl::RefPtr<VnodeF2fs> test_dir_vn = fbl::RefPtr<VnodeF2fs>::Downcast(std::move(test_dir));

  Dir *test_dir_ptr = static_cast<Dir *>(test_dir_vn.get());

  std::unordered_set<std::string> child_set = {"a", "b", "c", "d", "e"};
  std::vector<ino_t> child_ino_set(0);
  std::vector<std::string> deleted_child_set(0);

  // create a, b, c, d, e in test
  for (auto iter : child_set) {
    FileTester::CreateChild(test_dir_ptr, S_IFDIR, iter);
  }

  // check if {a, b, c, d, e} vnodes are in both containers.
  for (auto iter : child_set) {
    fbl::RefPtr<fs::Vnode> vn;
    FileTester::Lookup(test_dir_ptr, iter, &vn);
    ASSERT_TRUE(vn);
    VnodeF2fs *raw_vnode = reinterpret_cast<VnodeF2fs *>(vn.get());
    ASSERT_TRUE(raw_vnode->IsDirty());
    ASSERT_EQ((*raw_vnode).fbl::DoublyLinkedListable<fbl::RefPtr<VnodeF2fs>>::InContainer(), true);
    ASSERT_EQ((*raw_vnode).fbl::WAVLTreeContainable<VnodeF2fs *>::InContainer(), true);
    child_ino_set.push_back(raw_vnode->GetKey());
    vn->Close();
    vn.reset();
  }
  ASSERT_EQ(test_dir_vn->GetSize(), kPageSize);

  // flush dirty vnodes.
  fs_->WriteCheckpoint(false, false);

  // check if dirty vnodes are removed from dirty_list_
  ASSERT_TRUE(fs_->GetVCache().IsDirtyListEmpty());
  for (auto iter : child_set) {
    fbl::RefPtr<fs::Vnode> vn;
    FileTester::Lookup(test_dir_ptr, iter, &vn);
    ASSERT_TRUE(vn);
    VnodeF2fs *raw_vnode = reinterpret_cast<VnodeF2fs *>(vn.get());
    ASSERT_FALSE(raw_vnode->IsDirty());
    ASSERT_FALSE((*raw_vnode).fbl::DoublyLinkedListable<fbl::RefPtr<VnodeF2fs>>::InContainer());
    ASSERT_EQ((*raw_vnode).fbl::WAVLTreeContainable<VnodeF2fs *>::InContainer(), true);
    vn->Close();
  }

  // remove "b" and "d".
  FileTester::DeleteChild(test_dir_ptr, "b");
  deleted_child_set.push_back("b");
  FileTester::DeleteChild(test_dir_ptr, "d");
  deleted_child_set.push_back("d");

  // free nids for b and d.
  fs_->WriteCheckpoint(false, false);

  // check if nodemgr and vnode cache remove b and d.
  int i = 0;
  for (auto iter : child_set) {
    fbl::RefPtr<fs::Vnode> vn;
    auto child = find(deleted_child_set.begin(), deleted_child_set.end(), iter);
    FileTester::Lookup(test_dir_ptr, iter, &vn);
    if (child != deleted_child_set.end()) {
      ASSERT_FALSE(vn);
      ino_t ino = child_ino_set.at(i);
      fbl::RefPtr<VnodeF2fs> vn2;
      ASSERT_EQ(fs_->GetVCache().Lookup(ino, &vn2), ZX_ERR_NOT_FOUND);
      NodeInfo ni;
      fs_->GetNodeManager().GetNodeInfo(ino, ni);
      ASSERT_FALSE(ni.blk_addr);
    } else {
      ASSERT_TRUE(vn);
      VnodeF2fs *raw_vnode = reinterpret_cast<VnodeF2fs *>(vn.get());
      ASSERT_FALSE(raw_vnode->IsDirty());
      ASSERT_FALSE((*raw_vnode).fbl::DoublyLinkedListable<fbl::RefPtr<VnodeF2fs>>::InContainer());
      ASSERT_EQ((*raw_vnode).fbl::WAVLTreeContainable<VnodeF2fs *>::InContainer(), true);
      vn->Close();
      ino_t ino = child_ino_set.at(i);
      fbl::RefPtr<VnodeF2fs> vn2;
      ASSERT_EQ(fs_->GetVCache().Lookup(ino, &vn2), ZX_OK);
      NodeInfo ni;
      fs_->GetNodeManager().GetNodeInfo(ino, ni);
      ASSERT_TRUE(ni.blk_addr);
    }
    ++i;
  }

  ASSERT_EQ(test_dir_vn->Close(), ZX_OK);
  test_dir_vn = nullptr;
}

TEST_F(VnodeCacheTest, VnoceCacheExceptionCase) {
  fbl::RefPtr<VnodeF2fs> test_vnode;

  // 1. Check EvictVnode() exception
  ASSERT_EQ(GetDirtyVnodeCount(fs_->GetVCache()), 0U);
  ASSERT_EQ(GetCachedVnodeCount(fs_->GetVCache()), 1U);
  ASSERT_EQ(VnodeF2fs::Vget(fs_.get(), fs_->GetSuperblockInfo().GetNodeIno(), &test_vnode), ZX_OK);
  fs_->EvictVnode(test_vnode.get());
  ASSERT_EQ(GetDirtyVnodeCount(fs_->GetVCache()), 0U);
  ASSERT_EQ(GetCachedVnodeCount(fs_->GetVCache()), 1U);

  // 2. Check RemoveDirtyUnsafe() exception
  ASSERT_EQ(VnodeF2fs::Vget(fs_.get(), fs_->GetSuperblockInfo().GetNodeIno(), &test_vnode), ZX_OK);
  ASSERT_EQ(fs_->GetVCache().RemoveDirty(test_vnode.get()), ZX_ERR_NOT_FOUND);
  fs_->EvictVnode(test_vnode.get());
  ASSERT_EQ(GetDirtyVnodeCount(fs_->GetVCache()), 0U);
  ASSERT_EQ(GetCachedVnodeCount(fs_->GetVCache()), 1U);
  test_vnode = nullptr;

  // 3. Check Add() exception
  fbl::RefPtr<fs::Vnode> test_fs_vnode;
  FileTester::CreateChild(root_dir_.get(), S_IFDIR, "add_test");
  FileTester::Lookup(root_dir_.get(), "add_test", &test_fs_vnode);
  test_vnode = fbl::RefPtr<VnodeF2fs>::Downcast(std::move(test_fs_vnode));
  ASSERT_EQ(GetDirtyVnodeCount(fs_->GetVCache()), 2U);
  ASSERT_EQ(GetCachedVnodeCount(fs_->GetVCache()), 2U);
  fs_->GetVCache().Add(test_vnode.get());
  ASSERT_EQ(GetDirtyVnodeCount(fs_->GetVCache()), 2U);
  ASSERT_EQ(GetCachedVnodeCount(fs_->GetVCache()), 2U);

  // 4. Check AddDirty() exception
  fs_->GetVCache().AddDirty(test_vnode.get());
  ASSERT_EQ(GetDirtyVnodeCount(fs_->GetVCache()), 2U);
  ASSERT_EQ(GetCachedVnodeCount(fs_->GetVCache()), 2U);

  // 5. Check ForAllVnodes() callback function
  ASSERT_EQ(
      fs_->GetVCache().ForAllVnodes([](fbl::RefPtr<VnodeF2fs> &vnode) { return ZX_ERR_STOP; }),
      ZX_OK);

  ASSERT_EQ(fs_->GetVCache().ForAllVnodes(
                [](fbl::RefPtr<VnodeF2fs> &vnode) { return ZX_ERR_INVALID_ARGS; }),
            ZX_ERR_INVALID_ARGS);

  ASSERT_EQ(
      fs_->GetVCache().ForDirtyVnodesIf([](fbl::RefPtr<VnodeF2fs> &vnode) { return ZX_ERR_STOP; },
                                        [](fbl::RefPtr<VnodeF2fs> &vnode) { return ZX_OK; }),
      ZX_OK);

  ASSERT_EQ(fs_->GetVCache().ForDirtyVnodesIf(
                [](fbl::RefPtr<VnodeF2fs> &vnode) { return ZX_ERR_INVALID_ARGS; },
                [](fbl::RefPtr<VnodeF2fs> &vnode) { return ZX_OK; }),
            ZX_ERR_INVALID_ARGS);

  // 6. Check Reset()
  WritebackOperation op = {.bSync = true};
  test_vnode->Writeback(op);
  WritebackOperation op_root = {.bSync = true};
  root_dir_->Writeback(op_root);
  fs_->GetVCache().RemoveDirty(test_vnode.get());
  fs_->GetVCache().RemoveDirty(root_dir_.get());
  ASSERT_EQ(GetDirtyVnodeCount(fs_->GetVCache()), 0U);
  ASSERT_EQ(GetCachedVnodeCount(fs_->GetVCache()), 2U);

  fs_->GetVCache().Reset();
  ASSERT_EQ(GetDirtyVnodeCount(fs_->GetVCache()), 0U);
  ASSERT_EQ(GetCachedVnodeCount(fs_->GetVCache()), 0U);

  ASSERT_EQ(test_vnode->Close(), ZX_OK);
  test_vnode = nullptr;
}

}  // namespace
}  // namespace f2fs
