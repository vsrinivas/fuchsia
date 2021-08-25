// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_F2FS_TEST_UNIT_UNIT_LIB_H_
#define SRC_STORAGE_F2FS_TEST_UNIT_UNIT_LIB_H_

#include <unordered_set>

#include "src/storage/f2fs/f2fs.h"

namespace f2fs {
namespace unittest_lib {

void MkfsOnFakeDev(std::unique_ptr<Bcache> *bc, uint64_t blockCount = 819200,
                   uint32_t blockSize = kDefaultSectorSize, bool btrim = true);
void MountWithOptions(MountOptions &options, std::unique_ptr<Bcache> *bc,
                      std::unique_ptr<F2fs> *fs);
void Unmount(std::unique_ptr<F2fs> fs, std::unique_ptr<Bcache> *bc);
void SuddenPowerOff(std::unique_ptr<F2fs> fs, std::unique_ptr<Bcache> *bc);

void CreateRoot(F2fs *fs, fbl::RefPtr<VnodeF2fs> *out);
void Lookup(VnodeF2fs *parent, std::string_view name, fbl::RefPtr<fs::Vnode> *out);

void CreateChild(Dir *vn, uint32_t mode, std::string_view name);
void DeleteChild(Dir *vn, std::string_view name);
void CreateChildren(F2fs *fs, std::vector<fbl::RefPtr<VnodeF2fs>> &vnodes,
                    std::vector<uint32_t> &inos, fbl::RefPtr<Dir> &parent, std::string name,
                    uint32_t inode_cnt);
void DeleteChildren(std::vector<fbl::RefPtr<VnodeF2fs>> &vnodes, fbl::RefPtr<Dir> &parent,
                    uint32_t inode_cnt);

void VnodeWithoutParent(F2fs *fs, uint32_t mode, fbl::RefPtr<VnodeF2fs> &vnode);

void CheckInlineDir(VnodeF2fs *vn);
void CheckNonInlineDir(VnodeF2fs *vn);

void CheckChildrenFromReaddir(Dir *dir, std::unordered_set<std::string> childs);
void CheckChildrenInBlock(Dir *vn, unsigned int bidx, std::unordered_set<std::string> childs);

std::string GetRandomName(unsigned int len);

void AppendToFile(File *file, const void *data, size_t len);

void CheckNodeLevel(F2fs *fs, VnodeF2fs *vn, int level);

void CheckNidsFree(F2fs *fs, std::unordered_set<nid_t> &nids);
void CheckNidsInuse(F2fs *fs, std::unordered_set<nid_t> &nids);
void CheckBlkaddrsFree(F2fs *fs, std::unordered_set<block_t> &blkaddrs);
void CheckBlkaddrsInuse(F2fs *fs, std::unordered_set<block_t> &blkaddrs);

void CheckDnodeOfData(DnodeOfData *dn, nid_t exp_nid, pgoff_t exp_index, bool is_inode);

void RemoveTruncatedNode(NmInfo *nm_i, std::vector<nid_t> &nids);

bool IsCachedNat(NmInfo *nm_i, nid_t n);
}  // namespace unittest_lib
}  // namespace f2fs

#endif  // SRC_STORAGE_F2FS_TEST_UNIT_UNIT_LIB_H_
