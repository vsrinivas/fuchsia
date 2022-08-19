// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_F2FS_F2FS_H_
#define SRC_STORAGE_F2FS_F2FS_H_

// clang-format off
#ifdef __Fuchsia__

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <fbl/auto_lock.h>
#include <fbl/condition_variable.h>
#include <fbl/mutex.h>
#include <fidl/fuchsia.fs/cpp/wire.h>
#endif  // __Fuchsia__

#include <fcntl.h>

#include <storage/buffer/vmoid_registry.h>

#include <zircon/assert.h>
#include <zircon/errors.h>
#include <zircon/listnode.h>
#include <zircon/types.h>

#include <lib/syslog/cpp/macros.h>
#include <lib/fit/defer.h>
#include <lib/fit/function.h>
#include <lib/zx/status.h>

#include <fbl/algorithm.h>
#include <fbl/intrusive_wavl_tree.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/macros.h>
#include <fbl/ref_ptr.h>
#include <fbl/string_buffer.h>

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <semaphore>

#ifdef __Fuchsia__
#include "src/lib/storage/vfs/cpp/paged_vfs.h"
#include "src/lib/storage/vfs/cpp/paged_vnode.h"
#include "src/lib/storage/vfs/cpp/watcher.h"
#include "src/lib/storage/vfs/cpp/shared_mutex.h"
#include "src/lib/storage/vfs/cpp/service.h"

#include "lib/inspect/cpp/inspect.h"
#include "lib/inspect/service/cpp/service.h"
#include "src/lib/storage/vfs/cpp/fuchsia_vfs.h"
#include "src/lib/storage/vfs/cpp/inspect/inspect_tree.h"
#else  // __Fuchsia__
#include "src/storage/f2fs/sync_host.h"
#endif  // __Fuchsia__

#include "src/lib/storage/vfs/cpp/vfs.h"
#include "src/lib/storage/vfs/cpp/vnode.h"
#include "src/lib/storage/vfs/cpp/transaction/buffered_operations_builder.h"

#include "src/storage/f2fs/f2fs_types.h"
#include "src/storage/f2fs/f2fs_lib.h"
#include "src/storage/f2fs/f2fs_layout.h"
#ifdef __Fuchsia__
#include "src/storage/f2fs/vmo_manager.h"
#endif  // __Fuchsia__
#include "src/storage/f2fs/file_cache.h"
#include "src/storage/f2fs/node_page.h"
#include "src/storage/f2fs/f2fs_internal.h"
#include "src/storage/f2fs/namestring.h"
#include "src/storage/f2fs/bcache.h"
#include "src/storage/f2fs/storage_buffer.h"
#include "src/storage/f2fs/writeback.h"
#include "src/storage/f2fs/vnode.h"
#include "src/storage/f2fs/vnode_cache.h"
#include "src/storage/f2fs/dir.h"
#include "src/storage/f2fs/file.h"
#include "src/storage/f2fs/node.h"
#include "src/storage/f2fs/segment.h"
#include "src/storage/f2fs/gc.h"
#include "src/storage/f2fs/mkfs.h"
#include "src/storage/f2fs/mount.h"
#include "src/storage/f2fs/fsck.h"
#include "src/storage/f2fs/admin.h"
#include "src/storage/f2fs/dir_entry_cache.h"
#ifdef __Fuchsia__
#include "src/storage/f2fs/inspect.h"
#endif  // __Fuchsia__
// clang-format on

namespace f2fs {

zx_status_t LoadSuperblock(f2fs::Bcache *bc, Superblock *out_info);
zx_status_t LoadSuperblock(f2fs::Bcache *bc, Superblock *out_info, block_t bno);

#ifdef __Fuchsia__
zx::status<std::unique_ptr<F2fs>> CreateFsAndRoot(const MountOptions &mount_options,
                                                  async_dispatcher_t *dispatcher,
                                                  std::unique_ptr<f2fs::Bcache> bcache,
                                                  fidl::ServerEnd<fuchsia_io::Directory> root,
                                                  fit::closure on_unmount);

using SyncCallback = fs::Vnode::SyncCallback;
#else   // __Fuchsia__
zx::status<std::unique_ptr<F2fs>> CreateFsAndRoot(const MountOptions &mount_options,
                                                  std::unique_ptr<f2fs::Bcache> bcache);
#endif  // __Fuchsia__

#ifdef __Fuchsia__
// The F2fs class *has* to be final because it calls PagedVfs::TearDown from
// its destructor which is required to ensure thread-safety at destruction time.
class F2fs final : public fs::PagedVfs {
#else   // __Fuchsia__
class F2fs : public fs::Vfs {
#endif  // __Fuchsia__
 public:
  // Not copyable or moveable
  F2fs(const F2fs &) = delete;
  F2fs &operator=(const F2fs &) = delete;
  F2fs(F2fs &&) = delete;
  F2fs &operator=(F2fs &&) = delete;
  ~F2fs() override;

#ifdef __Fuchsia__
  explicit F2fs(async_dispatcher_t *dispatcher, std::unique_ptr<f2fs::Bcache> bc,
                std::unique_ptr<Superblock> sb, const MountOptions &mount_options);
  [[nodiscard]] static zx_status_t Create(async_dispatcher_t *dispatcher,
                                          std::unique_ptr<f2fs::Bcache> bc,
                                          const MountOptions &options, std::unique_ptr<F2fs> *out);

  void SetUnmountCallback(fit::closure closure) { on_unmount_ = std::move(closure); }
  void Shutdown(fs::FuchsiaVfs::ShutdownCallback cb) final;
  void OnNoConnections() final;

  void SetAdminService(fbl::RefPtr<AdminService> svc) { admin_svc_ = std::move(svc); }

  zx::status<fs::FilesystemInfo> GetFilesystemInfo() final;
  DirEntryCache &GetDirEntryCache() { return dir_entry_cache_; }
  InspectTree &GetInspectTree() { return inspect_tree_; }
  void Sync(SyncCallback closure);
#else   // __Fuchsia__
  explicit F2fs(std::unique_ptr<f2fs::Bcache> bc, std::unique_ptr<Superblock> sb,
                const MountOptions &mount_options);
  [[nodiscard]] static zx_status_t Create(std::unique_ptr<f2fs::Bcache> bc,
                                          const MountOptions &options, std::unique_ptr<F2fs> *out);
#endif  // __Fuchsia__

  VnodeCache &GetVCache() { return vnode_cache_; }
  zx_status_t InsertVnode(VnodeF2fs *vn) { return vnode_cache_.Add(vn); }
  void EvictVnode(VnodeF2fs *vn) { vnode_cache_.Evict(vn); }
  zx_status_t LookupVnode(ino_t ino, fbl::RefPtr<VnodeF2fs> *out) {
    return vnode_cache_.Lookup(ino, out);
  }

  void ResetBc(std::unique_ptr<f2fs::Bcache> *out = nullptr) {
    if (out == nullptr) {
      bc_.reset();
      return;
    }
    *out = std::move(bc_);
  }
  Bcache &GetBc() {
    ZX_DEBUG_ASSERT(bc_ != nullptr);
    return *bc_;
  }
  Superblock &RawSb() {
    ZX_DEBUG_ASSERT(raw_sb_ != nullptr);
    return *raw_sb_;
  }
  SuperblockInfo &GetSuperblockInfo() {
    ZX_DEBUG_ASSERT(superblock_info_ != nullptr);
    return *superblock_info_;
  }
  SegmentManager &GetSegmentManager() {
    ZX_DEBUG_ASSERT(segment_manager_ != nullptr);
    return *segment_manager_;
  }
  NodeManager &GetNodeManager() {
    ZX_DEBUG_ASSERT(node_manager_ != nullptr);
    return *node_manager_;
  }
  GcManager &GetGcManager() {
    ZX_DEBUG_ASSERT(gc_manager_ != nullptr);
    return *gc_manager_;
  }

  // For testing Reset() and ResetBc()
  bool IsValid() const;
  void ResetPsuedoVnodes() {
    root_vnode_.reset();
    meta_vnode_.reset();
    node_vnode_.reset();
  }
  void ResetSuperblockInfo() { superblock_info_.reset(); }
  void ResetSegmentManager() {
    segment_manager_->DestroySegmentManager();
    segment_manager_.reset();
  }
  void ResetNodeManager() {
    node_manager_->DestroyNodeManager();
    node_manager_.reset();
  }
  void ResetGcManager() { gc_manager_.reset(); }

  // super.cc
  void PutSuper();
  void SyncFs(bool bShutdown = false);
  zx_status_t SanityCheckRawSuper();
  zx_status_t SanityCheckCkpt();
  void InitSuperblockInfo();
  zx_status_t FillSuper();
  void ParseOptions();
  void Reset();
#if 0  // porting needed
  void InitOnce(void *foo);
  VnodeF2fs *F2fsAllocInode();
  static void F2fsICallback(rcu_head *head);
  void F2fsDestroyInode(inode *inode);
  int F2fsStatfs(dentry *dentry /*, kstatfs *buf*/);
  int F2fsShowOptions(/*seq_file *seq*/);
  VnodeF2fs *F2fsNfsGetInode(uint64_t ino, uint32_t generation);
  dentry *F2fsFhToDentry(fid *fid, int fh_len, int fh_type);
  dentry *F2fsFhToParent(fid *fid, int fh_len, int fh_type);
  dentry *F2fsMount(file_system_type *fs_type, int flags,
       const char *dev_name, void *data);
  int InitInodecache(void);
  void DestroyInodecache(void);
  int /*__init*/ initF2fsFs(void);
  void /*__exit*/ exitF2fsFs(void);
#endif

  // checkpoint.cc
  zx_status_t GrabMetaPage(pgoff_t index, LockedPage *out);
  zx_status_t GetMetaPage(pgoff_t index, LockedPage *out);
  zx_status_t F2fsWriteMetaPage(LockedPage &page, bool is_reclaim = false);

  bool CanReclaim() const;
  zx_status_t CheckOrphanSpace();
  void AddOrphanInode(VnodeF2fs *vnode);
  void RecoverOrphanInode(nid_t ino);
  int RecoverOrphanInodes();
  void WriteOrphanInodes(block_t start_blk);
  zx_status_t GetValidCheckpoint();
  zx_status_t ValidateCheckpoint(block_t cp_addr, uint64_t *version, LockedPage *out);
  void BlockOperations();
  void UnblockOperations();
  void DoCheckpoint(bool is_umount);
  void WriteCheckpoint(bool blocked, bool is_umount);
  uint32_t GetFreeSectionsForDirtyPages();
  bool IsCheckpointAvailable();
#if 0  // porting needed
  int F2fsWriteMetaPages(address_space *mapping, WritebackControl *wbc);
  int F2fsSetMetaPageDirty(Page *page);
  void SetDirtyDirPage(VnodeF2fs *vnode, Page *page);
  void RemoveDirtyDirInode(VnodeF2fs *vnode);
  int CreateCheckpointCaches();
  void DestroyCheckpointCaches();
#endif

  // recovery.cc
  // For the list of fsync inodes, used only during recovery
  class FsyncInodeEntry : public fbl::DoublyLinkedListable<std::unique_ptr<FsyncInodeEntry>> {
   public:
    explicit FsyncInodeEntry(fbl::RefPtr<VnodeF2fs> vnode_refptr)
        : vnode_(std::move(vnode_refptr)) {}

    FsyncInodeEntry() = delete;
    FsyncInodeEntry(const FsyncInodeEntry &) = delete;
    FsyncInodeEntry &operator=(const FsyncInodeEntry &) = delete;
    FsyncInodeEntry(FsyncInodeEntry &&) = delete;
    FsyncInodeEntry &operator=(FsyncInodeEntry &&) = delete;

    block_t GetLastDnodeBlkaddr() const { return last_dnode_blkaddr_; }
    void SetLastDnodeBlkaddr(block_t blkaddr) { last_dnode_blkaddr_ = blkaddr; }
    VnodeF2fs &GetVnode() const { return *vnode_; }

   private:
    fbl::RefPtr<VnodeF2fs> vnode_ = nullptr;  // vfs inode pointer
    block_t last_dnode_blkaddr_ = 0;          // block address locating the last dnode
  };
  using FsyncInodeList = fbl::DoublyLinkedList<std::unique_ptr<FsyncInodeEntry>>;

  bool SpaceForRollForward();
  FsyncInodeEntry *GetFsyncInode(FsyncInodeList &inode_list, nid_t ino);
  zx_status_t RecoverDentry(NodePage &ipage, VnodeF2fs &vnode);
  zx_status_t RecoverInode(VnodeF2fs &inode, NodePage &node_page);
  zx_status_t FindFsyncDnodes(FsyncInodeList &inode_list);
  void DestroyFsyncDnodes(FsyncInodeList &inode_list);
  void CheckIndexInPrevNodes(block_t blkaddr);
  void DoRecoverData(VnodeF2fs &inode, NodePage &page);
  void RecoverData(FsyncInodeList &inode_list, CursegType type);
  void RecoverFsyncData();

  // block count
  void DecValidBlockCount(VnodeF2fs *vnode, block_t count);
  zx_status_t IncValidBlockCount(VnodeF2fs *vnode, block_t count);
  block_t ValidUserBlocks();
  uint32_t ValidNodeCount();
  void IncValidInodeCount();
  void DecValidInodeCount();
  uint32_t ValidInodeCount();

  VnodeF2fs &GetNodeVnode() { return *node_vnode_; }
  VnodeF2fs &GetMetaVnode() { return *meta_vnode_; }

  // Flush all dirty Pages for the meta vnode that meet |operation|.if_page.
  pgoff_t SyncMetaPages(WritebackOperation &operation);
  // Flush all dirty data Pages for dirty vnodes that meet |operation|.if_vnode and if_page.
  pgoff_t SyncDirtyDataPages(WritebackOperation &operation);

  zx_status_t MakeOperation(storage::OperationType op, LockedPage &page, block_t blk_addr,
                            PageType type, block_t nblocks = 1);

  zx_status_t MakeOperation(storage::OperationType op, block_t blk_addr, block_t nblocks = 1);

  void ScheduleWriterSubmitPages(sync_completion_t *completion = nullptr,
                                 PageType type = PageType::kNrPageType) {
    writer_->ScheduleSubmitPages(completion, type);
  }
  void ScheduleWriteback();
  zx::status<> WaitForWriteback() {
    if (!writeback_flag_.try_acquire_for(kWriteTimeOut)) {
      return zx::error(ZX_ERR_TIMED_OUT);
    }
    writeback_flag_.release();
    return zx::ok();
  }
  std::atomic_flag &GetStopReclaimFlag() { return stop_reclaim_flag_; }

 private:
  zx_status_t MakeReadOperation(LockedPage &page, block_t blk_addr, bool is_sync = true);
  zx_status_t MakeWriteOperation(LockedPage &page, block_t blk_addr, PageType type);
  std::mutex checkpoint_mutex_;
  std::atomic_flag stop_reclaim_flag_ = ATOMIC_FLAG_INIT;
  std::binary_semaphore writeback_flag_{1};

  std::unique_ptr<f2fs::Bcache> bc_;

  std::unique_ptr<VnodeF2fs> node_vnode_;
  std::unique_ptr<VnodeF2fs> meta_vnode_;
  fbl::RefPtr<VnodeF2fs> root_vnode_;
  fit::closure on_unmount_;
  MountOptions mount_options_;

  std::shared_ptr<Superblock> raw_sb_;
  std::unique_ptr<SuperblockInfo> superblock_info_;
  std::unique_ptr<SegmentManager> segment_manager_;
  std::unique_ptr<NodeManager> node_manager_;
  std::unique_ptr<GcManager> gc_manager_;

  VnodeCache vnode_cache_;
  std::unique_ptr<Writer> writer_;

#ifdef __Fuchsia__
  DirEntryCache dir_entry_cache_;
  fbl::RefPtr<AdminService> admin_svc_;
  zx::event fs_id_;
  InspectTree inspect_tree_;
#endif  // __Fuchsia__
};

f2fs_hash_t DentryHash(std::string_view name);

}  // namespace f2fs

#endif  // SRC_STORAGE_F2FS_F2FS_H_
