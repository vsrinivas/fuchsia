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
  uint32_t i_flags = 0;              // keep an inode flags for ioctl
  uint8_t i_advise = 0;              // use to give file attribute hints
  uint8_t i_dir_level = 0;           // use for dentry level for large dir
  uint16_t i_extra_isize = 0;        // extra inode attribute size in bytes
  uint16_t i_inline_xattr_size = 0;  // inline xattr size
  uint64_t i_current_depth = 0;      // use only in directory structure
  umode_t i_acl_mode = 0;            // keep file acl mode temporarily

  uint32_t flags = 0;         // use to pass per-file flags
  uint64_t data_version = 0;  // lastest version of data for fsync
  atomic_t dirty_pages = 0;   // # of dirty dentry/data pages
  f2fs_hash_t chash;          // hash value of given file name
  uint64_t clevel = 0;        // maximum level of given file name
  nid_t i_xattr_nid = 0;      // node id that contains xattrs
  ExtentInfo ext;             // in-memory extent cache entry
};

struct LockedPagesAndAddrs {
  std::vector<block_t> block_addrs;  // Allocated block address
  std::vector<LockedPage> pages;     // Pages matched with block address
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
    return safemath::CheckMul<uint32_t>(sizeof(uint32_t), (GetAddrsPerInode() - 1)).ValueOrDie();
  }
  uint32_t MaxInlineDentry() const {
    return safemath::checked_cast<uint32_t>(
        safemath::CheckDiv(safemath::CheckMul(MaxInlineData(), kBitsPerByte).ValueOrDie(),
                           ((kSizeOfDirEntry + kDentrySlotLen) * kBitsPerByte + 1))
            .ValueOrDie());
  }
  uint32_t GetAddrsPerInode() const {
    return safemath::checked_cast<uint32_t>(
        (safemath::CheckSub(kAddrsPerInode, safemath::CheckDiv(GetExtraISize(), sizeof(uint32_t))) -
         GetInlineXattrAddrs())
            .ValueOrDie());
  }

  static void Allocate(F2fs *fs, ino_t ino, uint32_t mode, fbl::RefPtr<VnodeF2fs> *out);
  static zx_status_t Create(F2fs *fs, ino_t ino, fbl::RefPtr<VnodeF2fs> *out);
  void Init();

  ino_t GetKey() const { return ino_; }

#ifdef __Fuchsia__
  void Sync(SyncCallback closure) override;
#endif  // __Fuchsia__
  zx_status_t SyncFile(loff_t start, loff_t end, int datasync);

  void fbl_recycle() { RecycleNode(); }

  F2fs *fs() const { return fs_; }

  ino_t Ino() const { return ino_; }

  zx_status_t GetAttributes(fs::VnodeAttributes *a) final __TA_EXCLUDES(mutex_);
  zx_status_t SetAttributes(fs::VnodeAttributesUpdate attr) final __TA_EXCLUDES(mutex_);

  fs::VnodeProtocolSet GetProtocols() const final;

#ifdef __Fuchsia__
  zx_status_t GetNodeInfoForProtocol([[maybe_unused]] fs::VnodeProtocol protocol,
                                     [[maybe_unused]] fs::Rights rights,
                                     fs::VnodeRepresentation *info) final;

  // For fs::PagedVnode
  zx_status_t GetVmo(fuchsia_io::wire::VmoFlags flags, zx::vmo *out_vmo) final
      __TA_EXCLUDES(mutex_);
  void VmoRead(uint64_t offset, uint64_t length) final __TA_EXCLUDES(mutex_);
  void VmoDirty(uint64_t offset, uint64_t length) final {
    FX_LOGS(ERROR) << "Unsupported VmoDirty in VnodeF2fs.";
  }
  zx::result<zx::vmo> PopulateAndGetMmappedVmo(const size_t offset, const size_t length)
      __TA_EXCLUDES(mutex_);
  void OnNoPagedVmoClones() final __TA_REQUIRES(mutex_);
  virtual zx::result<> PopulateVmoWithInlineData(zx::vmo &vmo) __TA_EXCLUDES(mutex_) {
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }
#endif  // __Fuchsia__

  zx_status_t InvalidatePagedVmo(uint64_t offset, size_t len) __TA_EXCLUDES(mutex_);
  zx_status_t WritePagedVmo(const void *buffer_address, uint64_t offset, size_t len)
      __TA_EXCLUDES(mutex_);
  void ReleasePagedVmo() __TA_EXCLUDES(mutex_);
  zx::result<bool> ReleasePagedVmoUnsafe() __TA_REQUIRES(mutex_);

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
  // Caller should ensure node_page is locked.
  int TruncateDataBlocksRange(NodePage &node_page, uint32_t ofs_in_node, uint32_t count);
  // Caller should ensure node_page is locked.
  void TruncateDataBlocks(NodePage &node_page);
  void TruncatePartialDataPage(uint64_t from);
  zx_status_t TruncateBlocks(uint64_t from);
  zx_status_t TruncateHole(pgoff_t pg_start, pgoff_t pg_end);
  void TruncateToSize();
  void EvictVnode();

  // Caller should ensure node_page is locked.
  void SetDataBlkaddr(NodePage &node_page, uint32_t ofs_in_node, block_t new_addr);
  zx::result<block_t> FindDataBlkAddr(pgoff_t index);
  // Caller should ensure node_page is locked.
  zx_status_t ReserveNewBlock(NodePage &node_page, uint32_t ofs_in_node);

  void UpdateExtentCache(block_t blk_addr, pgoff_t file_offset);
  zx_status_t FindDataPage(pgoff_t index, fbl::RefPtr<Page> *out);
  // This function returns block addresses and LockedPages for requested offsets. If there is no
  // node page of a offset or the block address is not assigned, this function adds null LockedPage
  // and kNullAddr to LockedPagesAndAddrs struct. The handling of null LockedPage and kNullAddr is
  // responsible for StorageBuffer::ReserveReadOperations().
  zx::result<LockedPagesAndAddrs> FindDataBlockAddrsAndPages(const pgoff_t start,
                                                             const pgoff_t end);
  zx_status_t GetLockedDataPage(pgoff_t index, LockedPage *out);
  zx::result<std::vector<LockedPage>> GetLockedDataPages(pgoff_t start, pgoff_t end);
  zx_status_t GetNewDataPage(pgoff_t index, bool new_i_size, LockedPage *out);

  zx_status_t DoWriteDataPage(LockedPage &page);
  zx_status_t WriteDataPage(LockedPage &page, bool is_reclaim = false);
  zx::result<std::vector<LockedPage>> WriteBegin(const size_t offset, const size_t len);

  virtual zx_status_t RecoverInlineData(NodePage &node_page) __TA_EXCLUDES(mutex_) {
    return ZX_ERR_NOT_SUPPORTED;
  }

#ifdef __Fuchsia__
  void Notify(std::string_view name, fuchsia_io::wire::WatchEvent event) final;
  zx_status_t WatchDir(fs::Vfs *vfs, fuchsia_io::wire::WatchMask mask, uint32_t options,
                       fidl::ServerEnd<fuchsia_io::DirectoryWatcher> watcher) final;
#endif  // __Fuchsia__

  void MarkInodeDirty() __TA_EXCLUDES(mutex_);

  void GetExtentInfo(const Extent &i_ext);
  void SetRawExtent(Extent &i_ext);

  void InitNlink() __TA_EXCLUDES(mutex_) {
    std::lock_guard lock(mutex_);
    nlink_ = 1;
  }

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
  bool IsMeta() const __TA_EXCLUDES(mutex_);
  bool IsNode() const __TA_EXCLUDES(mutex_);

  void SetName(std::string_view name) { name_ = name; }
  bool IsSameName(std::string_view name) const {
    return (name_.GetStringView().compare(name) == 0);
  }
  std::string_view GetNameView() const { return name_.GetStringView(); }

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
    uint64_t xattr_block = GetXattrNid() ? 1 : 0;
    return (GetBlocks() > xattr_block);
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

  void SetParentNid(const ino_t &pino) __TA_EXCLUDES(mutex_) {
    std::lock_guard lock(mutex_);
    parent_ino_ = pino;
  }

  ino_t GetParentNid() const __TA_EXCLUDES(mutex_) {
    fs::SharedLock lock(mutex_);
    return parent_ino_;
  }

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

  uint16_t GetInlineXattrAddrs() const { return fi_.i_inline_xattr_size; }
  void SetInlineXattrAddrs(const uint16_t addrs) { fi_.i_inline_xattr_size = addrs; }

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

  zx_status_t GrabCachePage(pgoff_t index, LockedPage *out) {
    return file_cache_.GetPage(index, out);
  }

  zx::result<std::vector<LockedPage>> GrabCachePages(pgoff_t start, pgoff_t end) {
    return file_cache_.GetPages(start, end);
  }

  zx::result<std::vector<LockedPage>> GrabCachePages(const std::vector<pgoff_t> &page_offsets) {
    return file_cache_.GetPages(page_offsets);
  }

  pgoff_t Writeback(WritebackOperation &operation) { return file_cache_.Writeback(operation); }
  std::vector<LockedPage> InvalidatePages(pgoff_t start = 0, pgoff_t end = kPgOffMax) {
    return file_cache_.InvalidatePages(start, end);
  }
  void ClearDirtyPages(pgoff_t start = 0, pgoff_t end = kPgOffMax) {
    if (!file_cache_.SetOrphan()) {
      file_cache_.ClearDirtyPages(start, end);
    }
  }

  // TODO: When |is_reclaim| is set, release |page| after the IO completion
  zx_status_t WriteDirtyPage(LockedPage &page, bool is_reclaim);

  PageType GetPageType() {
    if (IsNode()) {
      return PageType::kNode;
    } else if (IsMeta()) {
      return PageType::kMeta;
    } else {
      return PageType::kData;
    }
  }

#ifdef __Fuchsia__
  // For testing
  bool HasPagedVmo() {
    fs::SharedLock rlock(mutex_);
    return paged_vmo() ? true : false;
  }
#endif

  // Overriden methods for thread safety analysis annotations.
  zx_status_t Read(void *data, size_t len, size_t off, size_t *out_actual) override
      __TA_EXCLUDES(mutex_) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  zx_status_t Write(const void *data, size_t len, size_t offset, size_t *out_actual) override
      __TA_EXCLUDES(mutex_) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  zx_status_t Append(const void *data, size_t len, size_t *out_end, size_t *out_actual) override
      __TA_EXCLUDES(mutex_) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  zx_status_t Truncate(size_t len) override __TA_EXCLUDES(mutex_) { return ZX_ERR_NOT_SUPPORTED; }

 protected:
  void RecycleNode() override;
  std::condition_variable_any flag_cvar_{};

 private:
  zx_status_t OpenNode(ValidatedOptions options, fbl::RefPtr<Vnode> *out_redirect) final
      __TA_EXCLUDES(mutex_);
  zx_status_t CloseNode() final;

  bool NeedToSyncDir() const __TA_EXCLUDES(mutex_);
  bool NeedDoCheckpoint() __TA_EXCLUDES(mutex_);

#ifdef __Fuchsia__
  zx_status_t CreatePagedVmo(size_t size) __TA_REQUIRES(mutex_);
  zx_status_t ClonePagedVmo(fuchsia_io::wire::VmoFlags flags, size_t size, zx::vmo *out_vmo)
      __TA_REQUIRES(mutex_);
  void SetPagedVmoName() __TA_REQUIRES(mutex_);
  void ReportPagerError(const uint64_t offset, const uint64_t length, const zx_status_t err)
      __TA_REQUIRES_SHARED(mutex_);

  VmoManager vmo_manager_;
  FileCache file_cache_{this, &vmo_manager_};
#else   // __Fuchsia__
  FileCache file_cache_{this};
#endif  // __Fuchsia__

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
  F2fs *const fs_ = nullptr;
#ifdef __Fuchsia__
  fs::WatcherContainer watcher_{};
#endif  // __Fuchsia__
};

}  // namespace f2fs

#endif  // SRC_STORAGE_F2FS_VNODE_H_
