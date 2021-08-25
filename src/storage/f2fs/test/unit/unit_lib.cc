// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "unit_lib.h"

#include <block-client/cpp/fake-device.h>
#include <gtest/gtest.h>

#include "src/storage/f2fs/f2fs.h"

namespace f2fs {
namespace unittest_lib {

using block_client::FakeBlockDevice;

void MkfsOnFakeDev(std::unique_ptr<Bcache> *bc, uint64_t blockCount, uint32_t blockSize,
                   bool btrim) {
  auto device = std::make_unique<FakeBlockDevice>(FakeBlockDevice::Config{
      .block_count = blockCount, .block_size = blockSize, .supports_trim = btrim});
  bool readonly_device = false;
  ASSERT_EQ(CreateBcache(std::move(device), &readonly_device, bc), ZX_OK);

  MkfsWorker mkfs(bc->get());
  ASSERT_EQ(mkfs.DoMkfs(), ZX_OK);
}

void MountWithOptions(MountOptions &options, std::unique_ptr<Bcache> *bc,
                      std::unique_ptr<F2fs> *fs) {
  ASSERT_EQ(F2fs::Create(std::move(*bc), options, fs), ZX_OK);
}

void Unmount(std::unique_ptr<F2fs> fs, std::unique_ptr<Bcache> *bc) {
  fs->PutSuper();
  fs->ResetBc(bc);
  fs.reset();
}

void SuddenPowerOff(std::unique_ptr<F2fs> fs, std::unique_ptr<Bcache> *bc) {
  SbInfo &sbi = fs->GetSbInfo();

  fs->GetVCache().Reset();

  // destroy f2fs internal modules
  fs->Nodemgr().DestroyNodeManager();
  fs->Segmgr().DestroySegmentManager();

  delete GetCheckpoint(&sbi);
  fs->ResetBc(bc);
  fs.reset();
}

void CreateRoot(F2fs *fs, fbl::RefPtr<VnodeF2fs> *out) {
  ASSERT_EQ(VnodeF2fs::Vget(fs, fs->RawSb().root_ino, out), ZX_OK);
  ASSERT_EQ((*out)->Open((*out)->ValidateOptions(fs::VnodeConnectionOptions()).value(), nullptr),
            ZX_OK);
}

void Lookup(VnodeF2fs *parent, std::string_view name, fbl::RefPtr<fs::Vnode> *out) {
  fbl::RefPtr<fs::Vnode> vn = nullptr;
  if (zx_status_t status = parent->Lookup(name, &vn); status != ZX_OK) {
    *out = nullptr;
    return;
  }
  ASSERT_TRUE(vn);
  ASSERT_EQ(vn->Open(vn->ValidateOptions(fs::VnodeConnectionOptions()).value(), nullptr), ZX_OK);
  *out = std::move(vn);
}

void CreateChild(Dir *vn, uint32_t mode, std::string_view name) {
  fbl::RefPtr<fs::Vnode> tmp_child;
  ASSERT_EQ(vn->Create(name, mode, &tmp_child), ZX_OK);
  ASSERT_EQ(tmp_child->Close(), ZX_OK);
}

void DeleteChild(Dir *vn, std::string_view name) {
  ASSERT_EQ(vn->Unlink(name, true), ZX_OK);
  // TODO: After EvictInode available, check if nids of the child are correctly freed
}

void CreateChildren(F2fs *fs, std::vector<fbl::RefPtr<VnodeF2fs>> &vnodes,
                    std::vector<uint32_t> &inos, fbl::RefPtr<Dir> &parent, std::string name,
                    uint32_t inode_cnt) {
  for (uint32_t i = 0; i < inode_cnt; i++) {
    fbl::RefPtr<fs::Vnode> test_file;

    name += std::to_string(i);
    ASSERT_EQ(parent->Create(name, S_IFREG, &test_file), ZX_OK);
    fbl::RefPtr<VnodeF2fs> test_file_vn = fbl::RefPtr<VnodeF2fs>::Downcast(std::move(test_file));

    inos.push_back(test_file_vn->Ino());
    vnodes.push_back(std::move(test_file_vn));
  }
}

void DeleteChildren(std::vector<fbl::RefPtr<VnodeF2fs>> &vnodes, fbl::RefPtr<Dir> &parent,
                    uint32_t inode_cnt) {
  uint32_t deleted_file_cnt = 0;
  for (const auto &iter : vnodes) {
    ASSERT_EQ(parent->Unlink(iter->GetName(), false), ZX_OK);
    deleted_file_cnt++;
  }
  ASSERT_EQ(deleted_file_cnt, inode_cnt);
}

void VnodeWithoutParent(F2fs *fs, uint32_t mode, fbl::RefPtr<VnodeF2fs> &vnode) {
  nid_t inode_nid;
  ASSERT_TRUE(fs->Nodemgr().AllocNid(&inode_nid));

  VnodeF2fs::Allocate(fs, inode_nid, S_IFREG, &vnode);
  ASSERT_EQ(vnode->Open(vnode->ValidateOptions(fs::VnodeConnectionOptions()).value(), nullptr),
            ZX_OK);
  vnode->UnlockNewInode();
  fs->Nodemgr().AllocNidDone(vnode->Ino());

  fs->InsertVnode(vnode.get());
  vnode->MarkInodeDirty();
}

void CheckInlineDir(VnodeF2fs *vn) {
  ASSERT_NE(vn->TestFlag(InodeInfoFlag::kInlineDentry), 0);
  ASSERT_EQ(vn->GetSize(), kMaxInlineData);
}

void CheckNonInlineDir(VnodeF2fs *vn) {
  ASSERT_EQ(vn->TestFlag(InodeInfoFlag::kInlineDentry), 0);
  ASSERT_GT(vn->GetSize(), kMaxInlineData);
}

void CheckChildrenFromReaddir(Dir *dir, std::unordered_set<std::string> childs) {
  childs.insert(".");

  fs::VdirCookie cookie;
  uint8_t buf[kPageSize];
  size_t len;

  ASSERT_EQ(dir->Readdir(&cookie, buf, sizeof(buf), &len), ZX_OK);

  uint8_t *buf_ptr = buf;

  while (len > 0 && buf_ptr < buf + kPageSize) {
    auto entry = reinterpret_cast<const vdirent_t *>(buf_ptr);
    size_t entry_size = entry->size + sizeof(vdirent_t);

    auto iter = childs.begin();
    for (; iter != childs.end(); iter++) {
      if (memcmp(entry->name, (*iter).c_str(), (*iter).length()) == 0) {
        break;
      }
    }

    ASSERT_NE(iter, childs.end());
    childs.erase(iter);

    buf_ptr += entry_size;
    len -= entry_size;
  }

  ASSERT_TRUE(childs.empty());
}

void CheckChildrenInBlock(Dir *vn, unsigned int bidx, std::unordered_set<std::string> childs) {
  if (bidx == 0) {
    childs.insert(".");
    childs.insert("..");
  }

  Page *page = nullptr;

  if (childs.empty()) {
    ASSERT_EQ(vn->FindDataPage(bidx, &page), ZX_ERR_NOT_FOUND);
    return;
  }

  ASSERT_EQ(vn->FindDataPage(bidx, &page), ZX_OK);
  DentryBlock *dentry_blk = reinterpret_cast<DentryBlock *>(page);

  uint64_t bit_pos = find_next_bit_le(&dentry_blk->dentry_bitmap, kNrDentryInBlock, 0);
  while (bit_pos < kNrDentryInBlock) {
    DirEntry *de = &dentry_blk->dentry[bit_pos];
    uint64_t slots = (LeToCpu(de->name_len) + kNameLen - 1) / kNameLen;

    auto iter = childs.begin();
    for (; iter != childs.end(); iter++) {
      if (memcmp(dentry_blk->filename[bit_pos], (*iter).c_str(), (*iter).length()) == 0) {
        break;
      }
    }

    ASSERT_NE(iter, childs.end());
    childs.erase(iter);

    bit_pos = find_next_bit_le(&dentry_blk->dentry_bitmap, kNrDentryInBlock, bit_pos + slots);
  }

  ASSERT_TRUE(childs.empty());

  F2fsPutPage(page, 0);
}

std::string GetRandomName(unsigned int len) {
  const char *char_list = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
  unsigned int char_list_len = strlen(char_list);
  auto generator = [&]() { return char_list[rand() % char_list_len]; };
  std::string str(len, 0);
  std::generate_n(str.begin(), len, generator);
  return str;
}

void AppendToFile(File *file, const void *data, size_t len) {
  size_t end = 0;
  size_t ret = 0;

  ASSERT_EQ(file->Append(data, len, &end, &ret), ZX_OK);
  ASSERT_EQ(ret, kPageSize);
}

void CheckNodeLevel(F2fs *fs, VnodeF2fs *vn, int level) {
  Page *ipage = nullptr;
  ASSERT_EQ(fs->Nodemgr().GetNodePage(vn->Ino(), &ipage), ZX_OK);
  Inode *inode = &(static_cast<Node *>(PageAddress(ipage))->i);

  int i;
  for (i = 0; i < level; i++)
    ASSERT_NE(inode->i_nid[i], 0U);

  for (; i < kNidsPerInode; i++)
    ASSERT_EQ(inode->i_nid[i], 0U);

  F2fsPutPage(ipage, 0);
}

void CheckNidsFree(F2fs *fs, std::unordered_set<nid_t> &nids) {
  SbInfo &sbi = fs->GetSbInfo();
  NmInfo *nm_i = GetNmInfo(&sbi);

  std::lock_guard lock(nm_i->free_nid_list_lock);
  for (auto nid : nids) {
    bool found = false;
    list_node_t *iter;
    list_for_every(&nm_i->free_nid_list, iter) {
      FreeNid *fnid = containerof(iter, FreeNid, list);
      if (fnid->nid == nid) {
        found = true;
        break;
      }
    }
    ASSERT_TRUE(found);
  }
}

void CheckNidsInuse(F2fs *fs, std::unordered_set<nid_t> &nids) {
  SbInfo &sbi = fs->GetSbInfo();
  NmInfo *nm_i = GetNmInfo(&sbi);

  std::lock_guard lock(nm_i->free_nid_list_lock);
  for (auto nid : nids) {
    bool found = false;
    list_node_t *iter;
    list_for_every(&nm_i->free_nid_list, iter) {
      FreeNid *fnid = containerof(iter, FreeNid, list);
      if (fnid->nid == nid) {
        found = true;
        break;
      }
    }
    ASSERT_FALSE(found);
  }
}

void CheckBlkaddrsFree(F2fs *fs, std::unordered_set<block_t> &blkaddrs) {
  SbInfo &sbi = fs->GetSbInfo();
  for (auto blkaddr : blkaddrs) {
    SegEntry *se = fs->Segmgr().GetSegEntry(GetSegNo(&sbi, blkaddr));
    uint32_t offset = GetSegOffFromSeg0(&sbi, blkaddr) & (sbi.blocks_per_seg - 1);
    ASSERT_EQ(TestValidBitmap(offset, reinterpret_cast<char *>(se->ckpt_valid_map)), 0);
  }
}

void CheckBlkaddrsInuse(F2fs *fs, std::unordered_set<block_t> &blkaddrs) {
  SbInfo &sbi = fs->GetSbInfo();
  for (auto blkaddr : blkaddrs) {
    SegEntry *se = fs->Segmgr().GetSegEntry(GetSegNo(&sbi, blkaddr));
    uint32_t offset = GetSegOffFromSeg0(&sbi, blkaddr) & (sbi.blocks_per_seg - 1);
    ASSERT_NE(TestValidBitmap(offset, reinterpret_cast<char *>(se->ckpt_valid_map)), 0);
  }
}

void CheckDnodeOfData(DnodeOfData *dn, nid_t exp_nid, pgoff_t exp_index, bool is_inode) {
  ASSERT_EQ(dn->nid, exp_nid);
  ASSERT_EQ(dn->ofs_in_node, static_cast<uint64_t>(1));
  ASSERT_EQ(dn->data_blkaddr, static_cast<block_t>(0));

  if (is_inode) {
    ASSERT_TRUE(dn->inode_page_locked);
  } else {
    ASSERT_FALSE(dn->inode_page_locked);
  }
}

template <typename T, typename F>
T *LookupList(list_node_t &entries, uint32_t value, F cond) {
  list_node_t *cur, *next;
  list_for_every_safe(&entries, cur, next) {
    T *e = containerof(cur, T, list);

    if (cond(*e, value)) {
      return e;
    }
  }
  return nullptr;
}

bool IsCachedNat(NmInfo *nm_i, nid_t n) {
  auto is_same_nid = [](NatEntry &e, nid_t nid) { return e.ni.nid == nid; };

  if (LookupList<NatEntry>(nm_i->dirty_nat_entries, n, is_same_nid)) {
    return true;
  }
  if (LookupList<NatEntry>(nm_i->nat_entries, n, is_same_nid)) {
    return true;
  }
  return false;
}

void RemoveTruncatedNode(NmInfo *nm_i, std::vector<nid_t> &nids) {
  auto is_same_nid = [](NatEntry &e, nid_t nid) { return e.ni.nid == nid; };

  for (auto iter = nids.begin(); iter != nids.end();) {
    if (NatEntry *ne = LookupList<NatEntry>(nm_i->dirty_nat_entries, *iter, is_same_nid)) {
      if (NatGetBlkaddr(ne) == kNullAddr) {
        iter = nids.erase(iter);
      } else {
        iter++;
      }
    }
  }
}

}  // namespace unittest_lib
}  // namespace f2fs
