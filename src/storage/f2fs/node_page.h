// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_F2FS_NODE_PAGE_H_
#define SRC_STORAGE_F2FS_NODE_PAGE_H_

#include <fbl/recycler.h>

#include "src/storage/f2fs/file_cache.h"

namespace f2fs {

class NodePage : public Page, public fbl::Recyclable<NodePage> {
 public:
  NodePage() = delete;
  NodePage(FileCache *file_cache, pgoff_t index) : Page(file_cache, index) {}
  NodePage(const NodePage &) = delete;
  NodePage &operator=(const NodePage &) = delete;
  NodePage(const NodePage &&) = delete;
  NodePage &operator=(const NodePage &&) = delete;
  void fbl_recycle() { RecyclePage(); }

  void FillNodeFooter(nid_t nid, nid_t ino, uint32_t ofs, bool reset);
  void CopyNodeFooterFrom(NodePage &src);
  void FillNodeFooterBlkaddr(block_t blkaddr);
  nid_t InoOfNode();
  nid_t NidOfNode();
  uint32_t OfsOfNode();
  uint64_t CpverOfNode();
  block_t NextBlkaddrOfNode();
  bool IsDnode();
  bool IsColdNode();
  bool IsFsyncDnode();
  bool IsDentDnode();
  void SetColdNode(VnodeF2fs &vnode);
  void SetFsyncMark(bool mark);
  void SetDentryMark(bool mark);

  // It returns the starting file offset that |node_page| indicates.
  // The file offset can be calcuated by using the node offset that |node_page| has.
  // See NodePage::IsDnode().
  block_t StartBidxOfNode(const VnodeF2fs &vnode);

  void SetNid(size_t off, nid_t nid, bool is_inode);
  nid_t GetNid(size_t off, bool is_inode);

 private:
  Node &GetRawNode() { return *GetAddress<Node>(); }
  static constexpr uint32_t kOfsInode = 0;
  static constexpr uint32_t kOfsDirectNode1 = 1;
  static constexpr uint32_t kOfsDirectNode2 = 2;
  static constexpr uint32_t kOfsIndirectNode1 = 3;
  static constexpr uint32_t kOfsIndirectNode2 = 4 + kNidsPerBlock;
  static constexpr uint32_t kOfsDoubleIndirectNode = 5 + 2 * kNidsPerBlock;
};
}  // namespace f2fs
#endif  // SRC_STORAGE_F2FS_NODE_PAGE_H_
