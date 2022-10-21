// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_F2FS_GC_H_
#define SRC_STORAGE_F2FS_GC_H_

namespace f2fs {

class GcManager {
 public:
  GcManager(const GcManager &) = delete;
  GcManager &operator=(const GcManager &) = delete;
  GcManager(GcManager &&) = delete;
  GcManager &operator=(GcManager &&) = delete;
  GcManager() = delete;
  GcManager(F2fs *fs) : fs_(fs), cur_victim_sec_(kNullSecNo) {}

  zx::result<uint32_t> F2fsGc() __TA_EXCLUDES(gc_mutex_);

  // For testing
  void DisableFgGc() { disable_gc_for_test_ = true; }
  void EnableFgGc() { disable_gc_for_test_ = false; }

  void SetCurVictimSec(uint32_t secno) { cur_victim_sec_ = secno; }
  uint32_t GetCurVictimSec() const { return cur_victim_sec_; }

 private:
  friend class GcTester;
  zx::result<uint32_t> GetGcVictim(GcType gc_type, CursegType type) __TA_REQUIRES(gc_mutex_);
  zx_status_t DoGarbageCollect(uint32_t segno, GcType gc_type) __TA_REQUIRES(gc_mutex_);

  bool CheckValidMap(uint32_t segno, uint64_t offset) __TA_REQUIRES(gc_mutex_);
  zx_status_t GcNodeSegment(const SummaryBlock &sum_blk, uint32_t segno, GcType gc_type)
      __TA_REQUIRES(gc_mutex_);

  // CheckDnode() returns ino of target block and start block index of the target block's dnode
  // block. It also checks the validity of summary.
  zx::result<std::pair<nid_t, block_t>> CheckDnode(const Summary &sum, block_t blkaddr)
      __TA_REQUIRES(gc_mutex_);
  zx_status_t GcDataSegment(const SummaryBlock &sum_blk, unsigned int segno, GcType gc_type)
      __TA_REQUIRES(gc_mutex_);

  F2fs *fs_ = nullptr;
  std::mutex gc_mutex_;      // mutex for GC
  uint32_t cur_victim_sec_;  // current victim section num

  // For testing
  bool disable_gc_for_test_ = false;
};
}  // namespace f2fs

#endif  // SRC_STORAGE_F2FS_GC_H_
