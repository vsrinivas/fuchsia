// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_F2FS_VNODE_H_
#define SRC_STORAGE_F2FS_VNODE_H_

namespace f2fs {
constexpr uint32_t kNullIno = std::numeric_limits<uint32_t>::max();

class F2fs;
// for in-memory extent cache entry
struct ExtentInfo {
  fs::SharedMutex ext_lock;  // rwlock for consistency
  uint64_t fofs = 0;         // start offset in a file
  uint32_t blk_addr = 0;     // start block address of the extent
  uint32_t len = 0;          // lenth of the extent
};

// i_advise uses Fadvise:xxx bit. We can add additional hints later.
enum class FAdvise {
  kCold = 1,
};

struct InodeInfo {
  uint32_t i_flags = 0;          // keep an inode flags for ioctl
  uint8_t i_advise = 0;          // use to give file attribute hints
  uint8_t i_dir_level = 0;       // use for dentry level for large dir
  uint16_t i_extra_isize = 0;    // extra inode attribute size in bytes
  uint64_t i_current_depth = 0;  // use only in directory structure
  umode_t i_acl_mode = 0;        // keep file acl mode temporarily

  uint32_t flags = 0;         // use to pass per-file flags
  uint64_t data_version = 0;  // lastest version of data for fsync
  atomic_t dirty_pages = 0;   // # of dirty dentry/data pages
  f2fs_hash_t chash;          // hash value of given file name
  uint64_t clevel = 0;        // maximum level of given file name
  nid_t i_xattr_nid = 0;      // node id that contains xattrs
  ExtentInfo ext;             // in-memory extent cache entry
};

#ifdef __Fuchsia__
class VnodeF2fs : public fs::PagedVnode,
                  public fbl::Recyclable<VnodeF2fs>,
                  public fbl::WAVLTreeContainable<VnodeF2fs *>,
                  public fbl::DoublyLinkedListable<fbl::RefPtr<VnodeF2fs>> {
#else   // __Fuchsia__
class VnodeF2fs : public fs::Vnode,
                  public fbl::Recyclable<VnodeF2fs>,
                  public fbl::WAVLTreeContainable<VnodeF2fs *>,
                  public fbl::DoublyLinkedListable<fbl::RefPtr<VnodeF2fs>> {
#endif  // __Fuchsia__
 public:
  explicit VnodeF2fs(F2fs *fs, ino_t ino);

  uint32_t InlineDataOffset() const {
    return kPageSize - sizeof(NodeFooter) -
           sizeof(uint32_t) * (kAddrsPerInode + kNidsPerInode - 1) + GetExtraISize();
  }
  uint32_t MaxInlineData() const {
    return sizeof(uint32_t) *
           (kAddrsPerInode - GetExtraISize() / sizeof(uint32_t) - kInlineXattrAddrs - 1);
  }

  static void Allocate(F2fs *fs, ino_t ino, uint32_t mode, fbl::RefPtr<VnodeF2fs> *out);
  static zx_status_t Create(F2fs *fs, ino_t ino, fbl::RefPtr<VnodeF2fs> *out);
  void Init();

  ino_t GetKey() const { return ino_; }

#ifdef __Fuchsia__
  void Sync(SyncCallback closure) override;
#endif  // __Fuchsia__
  zx_status_t SyncFile(loff_t start, loff_t end, int datasync);
  bool NeedToSyncDir();

  void fbl_recycle() { RecycleNode(); }

  F2fs *Vfs() __TA_EXCLUDES(mutex_) {
    fs::SharedLock lock(mutex_);
    return reinterpret_cast<F2fs *>(vfs());
  }
  ino_t Ino() const { return ino_; }

  zx_status_t GetAttributes(fs::VnodeAttributes *a) final __TA_EXCLUDES(mutex_);
  zx_status_t SetAttributes(fs::VnodeAttributesUpdate attr) final __TA_EXCLUDES(mutex_);

#ifdef __Fuchsia__
  zx_status_t GetNodeInfoForProtocol([[maybe_unused]] fs::VnodeProtocol protocol,
                                     [[maybe_unused]] fs::Rights rights,
                                     fs::VnodeRepresentation *info) final;
#endif  // __Fuchsia__

  fs::VnodeProtocolSet GetProtocols() const final;

#ifdef __Fuchsia__
  zx_status_t GetVmo(fuchsia_io::wire::VmoFlags flags, zx::vmo *out_vmo,
                     size_t *out_size) override {
    FX_LOGS(ERROR) << "Unsupported GetVMO in VnodeF2fs. This method should be overridden.";
    return ZX_ERR_NOT_SUPPORTED;
  }

  void VmoRead(uint64_t offset, uint64_t length) override {
    FX_LOGS(ERROR) << "Unsupported VmoRead in VnodeF2fs. This method should be overridden.";
  }

  void VmoDirty(uint64_t offset, uint64_t length) override {
    FX_LOGS(ERROR) << "Unsupported VmoDirty in VnodeF2fs. This method should be overridden.";
  }
#endif  // __Fuchsia__

#if 0  // porting needed
  // void F2fsSetInodeFlags();
  // int F2fsIgetTest(void *data);
  // static int CheckExtentCache(inode *inode, pgoff_t pgofs,
  // static int GetDataBlockRo(inode *inode, sector_t iblock,
  //      buffer_head *bh_result, int create);
#endif

  static zx_status_t Vget(F2fs *fs, ino_t ino, fbl::RefPtr<VnodeF2fs> *out);
  void UpdateInode(Page *node_page);
  zx_status_t WriteInode(bool is_reclaim = false);
  zx_status_t DoTruncate(size_t len);
  int TruncateDataBlocksRange(DnodeOfData *dn, int count);
  void TruncateDataBlocks(DnodeOfData *dn);
  void TruncatePartialDataPage(uint64_t from);
  zx_status_t TruncateBlocks(uint64_t from);
  zx_status_t TruncateHole(pgoff_t pg_start, pgoff_t pg_end);
  void TruncateToSize();
  void EvictVnode();

  void SetDataBlkaddr(DnodeOfData *dn, block_t new_addr);
  zx_status_t ReserveNewBlock(DnodeOfData *dn);

  void UpdateExtentCache(block_t blk_addr, DnodeOfData *dn);
  zx_status_t FindDataPage(pgoff_t index, fbl::RefPtr<Page> *out);
  zx_status_t GetLockDataPage(pgoff_t index, fbl::RefPtr<Page> *out);
  zx_status_t GetNewDataPage(pgoff_t index, bool new_i_size, fbl::RefPtr<Page> *out);

  zx_status_t DoWriteDataPage(fbl::RefPtr<Page> page);
  zx_status_t WriteDataPage(fbl::RefPtr<Page> page, bool is_reclaim = false);
  zx_status_t WriteBegin(size_t pos, size_t len, fbl::RefPtr<Page> *page);

#ifdef __Fuchsia__
  void Notify(std::string_view name, unsigned event) final;
  zx_status_t WatchDir(fs::Vfs *vfs, uint32_t mask, uint32_t options, zx::channel watcher) final;
#endif  // __Fuchsia__

  void MarkInodeDirty() __TA_EXCLUDES(mutex_);

  void GetExtentInfo(const Extent &i_ext);
  void SetRawExtent(Extent &i_ext);

  void IncNlink() __TA_EXCLUDES(mutex_) {
    std::lock_guard lock(mutex_);
    ++nlink_;
  }

  void DropNlink() __TA_EXCLUDES(mutex_) {
    std::lock_guard lock(mutex_);
    --nlink_;
  }

  void ClearNlink() __TA_EXCLUDES(mutex_) {
    std::lock_guard lock(mutex_);
    nlink_ = 0;
  }

  void SetNlink(const uint32_t &nlink) __TA_EXCLUDES(mutex_) {
    std::lock_guard lock(mutex_);
    nlink_ = nlink;
  }

  uint32_t GetNlink() const __TA_EXCLUDES(mutex_) {
    fs::SharedLock lock(mutex_);
    return nlink_;
  }

  void SetMode(const umode_t &mode);
  umode_t GetMode() const;
  bool IsDir() const;
  bool IsReg() const;
  bool IsLink() const;
  bool IsChr() const;
  bool IsBlk() const;
  bool IsSock() const;
  bool IsFifo() const;
  bool HasGid() const;
  bool IsMeta() __TA_EXCLUDES(mutex_);
  bool IsNode() __TA_EXCLUDES(mutex_);

  void SetName(std::string_view name) { name_ = name; }
  bool IsSameName(std::string_view name) const {
    return (name_.GetStringView().compare(name) == 0);
  }
  std::string_view GetNameView() const { return name_.GetStringView(); }
  uint32_t GetNameLen() const { return name_.GetLen(); }
  const char *GetName() { return name_.GetData(); }

  // stat_lock
  uint64_t GetBlockCount() const { return (size_ + kBlockSize - 1) / kBlockSize; }
  void IncBlocks(const block_t &nblocks) { blocks_ += nblocks; }
  void DecBlocks(const block_t &nblocks) {
    ZX_ASSERT(blocks_ >= nblocks);
    blocks_ -= nblocks;
  }
  void InitBlocks() { blocks_ = 0; }
  uint64_t GetBlocks() const { return blocks_; }
  void SetBlocks(const uint64_t &blocks) { blocks_ = blocks; }
  bool HasBlocks() const {
    // TODO: Need to consider i_xattr_nid
    return (GetBlocks() > kDefaultAllocatedBlocks);
  }

  void SetSize(const uint64_t &nbytes) __TA_EXCLUDES(mutex_) {
    std::lock_guard lock(mutex_);
    size_ = nbytes;
  }

  void InitSize() __TA_EXCLUDES(mutex_) {
    std::lock_guard lock(mutex_);
    size_ = 0;
  }

  uint64_t GetSize() const __TA_EXCLUDES(mutex_) {
    fs::SharedLock lock(mutex_);
    return size_;
  }

  void SetParentNid(const ino_t &pino) { parent_ino_ = pino; }
  ino_t GetParentNid() const { return parent_ino_; }

  void SetGeneration(const uint32_t &gen) { generation_ = gen; }
  uint32_t GetGeneration() const { return generation_; }

  void SetUid(const uid_t &uid) { uid_ = uid; }
  uid_t GetUid() const { return uid_; }

  void SetGid(const gid_t &gid) { gid_ = gid; }
  gid_t GetGid() const { return gid_; }

  timespec GetATime() const { return atime_; }
  void SetATime(const timespec &time) { atime_ = time; }
  void SetATime(const uint64_t &sec, const uint32_t &nsec) {
    atime_.tv_sec = sec;
    atime_.tv_nsec = nsec;
  }

  timespec GetMTime() const { return mtime_; }
  void SetMTime(const timespec &time) { mtime_ = time; }
  void SetMTime(const uint64_t &sec, const uint32_t &nsec) {
    mtime_.tv_sec = sec;
    mtime_.tv_nsec = nsec;
  }

  timespec GetCTime() const { return ctime_; }
  void SetCTime(const timespec &time) { ctime_ = time; }
  void SetCTime(const uint64_t &sec, const uint32_t &nsec) {
    ctime_.tv_sec = sec;
    ctime_.tv_nsec = nsec;
  }

  void SetInodeFlags(const uint32_t &flags) { fi_.i_flags = flags; }
  uint32_t GetInodeFlags() const { return fi_.i_flags; }

  bool SetFlag(const InodeInfoFlag &flag) __TA_EXCLUDES(mutex_) {
    std::lock_guard lock(mutex_);
    return TestAndSetBit(static_cast<int>(flag), &fi_.flags);
  }
  bool ClearFlag(const InodeInfoFlag &flag) __TA_EXCLUDES(mutex_) {
    std::lock_guard lock(mutex_);
    return TestAndClearBit(static_cast<int>(flag), &fi_.flags);
  }
  bool TestFlag(const InodeInfoFlag &flag) __TA_EXCLUDES(mutex_) {
    fs::SharedLock lock(mutex_);
    return TestBit(static_cast<int>(flag), &fi_.flags);
  }

  void ClearAdvise(const FAdvise &bit) { ClearBit(static_cast<int>(bit), &fi_.i_advise); }
  void SetAdvise(const FAdvise &bit) { SetBit(static_cast<int>(bit), &fi_.i_advise); }
  uint8_t GetAdvise() const { return fi_.i_advise; }
  void SetAdvise(const uint8_t &bits) { fi_.i_advise = bits; }
  int IsAdviseSet(const FAdvise &bit) { return TestBit(static_cast<int>(bit), &fi_.i_advise); }

  uint64_t GetDirHashLevel() const { return fi_.clevel; }
  bool IsSameDirHash(const f2fs_hash_t &hash) const { return (fi_.chash == hash); }
  void ClearDirHash() { fi_.chash = 0; }
  void SetDirHash(const f2fs_hash_t &hash, const uint64_t &level) {
    fi_.chash = hash;
    fi_.clevel = level;
  }

  void IncreaseDirtyPageCount() {
    atomic_fetch_add_explicit(&fi_.dirty_pages, 1, std::memory_order_relaxed);
  }
  void DecreaseDirtyPageCount() {
    atomic_fetch_sub_explicit(&fi_.dirty_pages, 1, std::memory_order_relaxed);
  }
  int GetDirtyPageCount() {
    return atomic_load_explicit(&fi_.dirty_pages, std::memory_order_acquire);
  }

  uint8_t GetDirLevel() const { return fi_.i_dir_level; }
  void SetDirLevel(const uint8_t level) { fi_.i_dir_level = level; }

  uint64_t GetCurDirDepth() const { return fi_.i_current_depth; }
  void SetCurDirDepth(const uint64_t depth) { fi_.i_current_depth = depth; }

  nid_t GetXattrNid() const { return fi_.i_xattr_nid; }
  void SetXattrNid(const nid_t nid) { fi_.i_xattr_nid = nid; }
  void ClearXattrNid() { fi_.i_xattr_nid = 0; }

  uint16_t GetExtraISize() const { return fi_.i_extra_isize; }
  void SetExtraISize(const uint16_t size) { fi_.i_extra_isize = size; }
  void UpdateVersion();

  bool IsBad() { return TestFlag(InodeInfoFlag::kBad); }

  void Activate() __TA_EXCLUDES(mutex_) { SetFlag(InodeInfoFlag::kActive); }

  void Deactivate() __TA_EXCLUDES(mutex_) {
    ClearFlag(InodeInfoFlag::kActive);
    flag_cvar_.notify_all();
  }

  bool IsActive() __TA_EXCLUDES(mutex_) { return TestFlag(InodeInfoFlag::kActive); }

  bool WaitForDeactive(std::mutex &mutex) __TA_REQUIRES_SHARED(mutex) {
    if (IsActive()) {
      flag_cvar_.wait(mutex, [this]() {
        return (TestBit(static_cast<int>(InodeInfoFlag::kActive), &fi_.flags) == 0);
      });
      return true;
    }
    return false;
  }

  bool ClearDirty() __TA_EXCLUDES(mutex_) { return ClearFlag(InodeInfoFlag::kDirty); }

  bool IsDirty() __TA_EXCLUDES(mutex_) { return TestFlag(InodeInfoFlag::kDirty); }

  bool ShouldFlush() __TA_EXCLUDES(mutex_) {
    if (!GetNlink() || !IsDirty() || IsBad()) {
      return false;
    }
    return true;
  }

  void WaitForInit() __TA_EXCLUDES(mutex_) {
    fs::SharedLock lock(mutex_);
    if (TestBit(static_cast<int>(InodeInfoFlag::kInit), &fi_.flags)) {
      flag_cvar_.wait(
          mutex_, [this]() __TA_EXCLUDES(mutex_) { return (TestFlag(InodeInfoFlag::kInit) == 0); });
    }
  }

  void UnlockNewInode() __TA_EXCLUDES(mutex_) {
    ClearFlag(InodeInfoFlag::kInit);
    flag_cvar_.notify_all();
  }

  zx_status_t FindPage(pgoff_t index, fbl::RefPtr<Page> *out) {
    return file_cache_.FindPage(index, out);
  }
  zx_status_t GrabCachePage(pgoff_t index, fbl::RefPtr<Page> *out) {
    return file_cache_.GetPage(index, out);
  }
  pgoff_t Writeback(WritebackOperation &operation) { return file_cache_.Writeback(operation); }
  void InvalidatePages(pgoff_t start = 0, pgoff_t end = kPgOffMax) {
    file_cache_.InvalidatePages(start, end);
  }

  // TODO: When |is_reclaim| is set, release |page| after the IO completion
  zx_status_t WriteDirtyPage(fbl::RefPtr<Page> page, bool is_reclaim);

 protected:
  void RecycleNode() override;
  std::condition_variable_any flag_cvar_{};

 private:
  zx_status_t OpenNode(ValidatedOptions options, fbl::RefPtr<Vnode> *out_redirect) final
      __TA_EXCLUDES(mutex_);
  zx_status_t CloseNode() final;

  FileCache file_cache_{this};
  InodeInfo fi_;
  uid_t uid_ = 0;
  gid_t gid_ = 0;
  uint64_t size_ = 0;
  uint64_t blocks_ = 0;
  uint32_t nlink_ = 0;
  uint32_t generation_ = 0;
  umode_t mode_ = 0;
  NameString name_;
  ino_t parent_ino_{kNullIno};
  timespec atime_ = {0, 0};
  timespec mtime_ = {0, 0};
  timespec ctime_ = {0, 0};
  ino_t ino_ = 0;
#ifdef __Fuchsia__
  fs::WatcherContainer watcher_{};
#endif  // __Fuchsia__
};

}  // namespace f2fs

#endif  // SRC_STORAGE_F2FS_VNODE_H_
