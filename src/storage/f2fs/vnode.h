// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_F2FS_VNODE_H_
#define THIRD_PARTY_F2FS_VNODE_H_

namespace f2fs {

constexpr uint32_t kNullIno = std::numeric_limits<uint32_t>::max();

// Used by fsck
class F2fs;

class VnodeF2fs : public fs::Vnode,
                  public fbl::Recyclable<VnodeF2fs>,
                  public fbl::WAVLTreeContainable<VnodeF2fs *>,
                  public fbl::DoublyLinkedListable<fbl::RefPtr<VnodeF2fs>> {
 public:
  explicit VnodeF2fs(F2fs *fs);
  explicit VnodeF2fs(F2fs *fs, ino_t ino);
  ~VnodeF2fs() = default;

  static void Allocate(F2fs *fs, ino_t ino, uint32_t mode, fbl::RefPtr<VnodeF2fs> *out);
  static void Create(F2fs *fs, ino_t ino, fbl::RefPtr<VnodeF2fs> *out);
  void Init();

  ino_t GetKey() const { return ino_; }

  static size_t GetHash(ino_t key) { return fnv1a_tiny(key, kHashBits); }
  void Sync(SyncCallback closure) final;
  zx_status_t SyncFile(loff_t start, loff_t end, int datasync);
  int NeedToSyncDir();

  zx_status_t QueryFilesystem(fuchsia_io::wire::FilesystemInfo *info) final;

  void fbl_recycle() { RecycleNode(); };

  F2fs *Vfs() __TA_EXCLUDES(mutex_) {
    fs::SharedLock lock(mutex_);
    return reinterpret_cast<F2fs *>(vfs());
  }
  ino_t Ino() const { return ino_; }

  zx_status_t GetAttributes(fs::VnodeAttributes *a) final __TA_EXCLUDES(mutex_);
  zx_status_t SetAttributes(fs::VnodeAttributesUpdate attr) final __TA_EXCLUDES(mutex_);

  zx_status_t GetNodeInfoForProtocol([[maybe_unused]] fs::VnodeProtocol protocol,
                                     [[maybe_unused]] fs::Rights rights,
                                     fs::VnodeRepresentation *info) final;

  fs::VnodeProtocolSet GetProtocols() const final;

#if 0  // porting needed
  // void F2fsSetInodeFlags();
  // int F2fsIgetTest(void *data);
  // VnodeF2fs *F2fsIgetNowait(uint64_t ino);
  // static int CheckExtentCache(inode *inode, pgoff_t pgofs,
  //        buffer_head *bh_result);
  // static int GetDataBlockRo(inode *inode, sector_t iblock,
  //      buffer_head *bh_result, int create);
  // static int F2fsReadDataPage(file *file, page *page);
  // static int F2fsReadDataPages(file *file,
  //       address_space *mapping,
  //       list_head *pages, unsigned nr_pages);
  // int F2fsWriteDataPages(/*address_space *mapping,*/
  //                        WritebackControl *wbc);
  // ssize_t F2fsDirectIO(/*int rw, kiocb *iocb,
  //   const iovec *iov, */
  //                      loff_t offset, uint64_t nr_segs);
  //   [[maybe_unused]] static void F2fsInvalidateDataPage(Page *page, uint64_t offset);
  //   [[maybe_unused]] static int F2fsReleaseDataPage(Page *page, gfp_t wait);
  // int F2fsSetDataPageDirty(Page *page);
#endif

  static zx_status_t Vget(F2fs *fs, uint64_t ino, fbl::RefPtr<VnodeF2fs> *out);
  void UpdateInode(Page *node_page);
  zx_status_t WriteInode(WritebackControl *wbc);
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
  zx_status_t FindDataPage(pgoff_t index, Page **out);
  zx_status_t GetLockDataPage(pgoff_t index, Page **out);
  zx_status_t GetNewDataPage(pgoff_t index, bool new_i_size, Page **out);

  static zx_status_t Readpage(F2fs *fs, Page *page, block_t blk_addr, int type);
  zx_status_t DoWriteDataPage(Page *page);
  zx_status_t WriteDataPageReq(Page *page, WritebackControl *wbc);
  zx_status_t WriteBegin(size_t pos, size_t len, Page **page);

  void Notify(std::string_view name, unsigned event) final;
  zx_status_t WatchDir(fs::Vfs *vfs, uint32_t mask, uint32_t options, zx::channel watcher) final;

  void MarkInodeDirty() __TA_EXCLUDES(mutex_);

  inline void GetExtentInfo(const Extent &i_ext);
  inline void SetRawExtent(Extent &i_ext);

  inline void IncNlink() __TA_EXCLUDES(mutex_) {
    std::lock_guard lock(mutex_);
    nlink_++;
  }
  inline void DropNlink() __TA_EXCLUDES(mutex_) {
    std::lock_guard lock(mutex_);
    nlink_--;
  }
  inline void ClearNlink() __TA_EXCLUDES(mutex_) {
    std::lock_guard lock(mutex_);
    nlink_ = 0;
  }
  inline void SetNlink(const uint32_t &nlink) __TA_EXCLUDES(mutex_) {
    std::lock_guard lock(mutex_);
    nlink_ = nlink;
  }
  inline uint32_t GetNlink() const __TA_EXCLUDES(mutex_) {
    fs::SharedLock lock(mutex_);
    return nlink_;
  }

  inline void SetMode(const umode_t &mode) { mode_ = mode; }
  inline umode_t GetMode() const { return mode_; }
  inline bool IsDir() const { return S_ISDIR(mode_); }
  inline bool IsReg() const { return S_ISREG(mode_); }
  inline bool IsLink() const { return S_ISLNK(mode_); }
  inline bool IsChr() const { return S_ISCHR(mode_); }
  inline bool IsBlk() const { return S_ISBLK(mode_); }
  inline bool IsSock() const { return S_ISSOCK(mode_); }
  inline bool IsFifo() const { return S_ISFIFO(mode_); }
  inline bool HasGid() const { return mode_ & S_ISGID; }

  inline void SetName(const std::string_view &name) { name_ = name; }
  inline bool IsSameName(const std::string_view &name) const {
    return (name_.GetStringView().compare(name) == 0);
  }
  inline std::string_view GetNameView() const { return name_.GetStringView(); }
  inline uint32_t GetNameLen() const { return name_.GetLen(); }
  inline const char *GetName() { return name_.GetData(); }

  // stat_lock
  inline uint64_t GetBlockCount() const { return (size_ + kBlockSize - 1) / kBlockSize; }
  inline void IncBlocks(const block_t &nblocks) { blocks_ += nblocks; }
  inline void DecBlocks(const block_t &nblocks) {
    ZX_ASSERT(blocks_ >= nblocks);
    blocks_ -= nblocks;
  }
  inline void InitBlocks() { blocks_ = 0; }
  inline uint64_t GetBlocks() const { return blocks_; }
  inline void SetBlocks(const uint64_t &blocks) { blocks_ = blocks; }
  inline bool HasBlocks() const {
    // TODO: Need to consider i_xattr_nid
    return (GetBlocks() > kDefaultAllocatedBlocks);
  }

  inline void SetSize(const uint64_t &nbytes) __TA_EXCLUDES(mutex_) {
    std::lock_guard lock(mutex_);
    size_ = nbytes;
  }
  inline void InitSize() __TA_EXCLUDES(mutex_) {
    std::lock_guard lock(mutex_);
    size_ = 0;
  }
  inline uint64_t GetSize() const __TA_EXCLUDES(mutex_) {
    fs::SharedLock lock(mutex_);
    return size_;
  }

  inline void SetParentNid(const ino_t &pino) { parent_ino_ = pino; };
  inline ino_t GetParentNid() const { return parent_ino_; };

  inline void SetGeneration(const uint32_t &gen) { generation_ = gen; }
  inline uint32_t GetGeneration() const { return generation_; }

  inline void SetUid(const uid_t &uid) { uid_ = uid; }
  inline uid_t GetUid() const { return uid_; }

  inline void SetGid(const gid_t &gid) { gid_ = gid; }
  inline gid_t GetGid() const { return gid_; }

  inline timespec GetATime() const { return atime_; }
  inline void SetATime(const timespec &time) { atime_ = time; }
  inline void SetATime(const uint64_t &sec, const uint32_t &nsec) {
    atime_.tv_sec = sec;
    atime_.tv_nsec = nsec;
  }

  inline timespec GetMTime() const { return mtime_; }
  inline void SetMTime(const timespec &time) { mtime_ = time; }
  inline void SetMTime(const uint64_t &sec, const uint32_t &nsec) {
    mtime_.tv_sec = sec;
    mtime_.tv_nsec = nsec;
  }

  inline timespec GetCTime() const { return ctime_; }
  inline void SetCTime(const timespec &time) { ctime_ = time; }
  inline void SetCTime(const uint64_t &sec, const uint32_t &nsec) {
    ctime_.tv_sec = sec;
    ctime_.tv_nsec = nsec;
  }

  inline void SetInodeFlags(const uint32_t &flags) { fi_.i_flags = flags; }
  inline uint32_t GetInodeFlags() const { return fi_.i_flags; }

  inline bool SetFlag(const InodeInfoFlag &flag) __TA_EXCLUDES(mutex_) {
    std::lock_guard lock(mutex_);
    return (test_and_set_bit_le(static_cast<int>(flag), &fi_.flags) != 0);
  }
  inline bool ClearFlag(const InodeInfoFlag &flag) __TA_EXCLUDES(mutex_) {
    std::lock_guard lock(mutex_);
    return (test_and_clear_bit_le(static_cast<int>(flag), &fi_.flags) != 0);
  }
  inline bool TestFlag(const InodeInfoFlag &flag) __TA_EXCLUDES(mutex_) {
    fs::SharedLock lock(mutex_);
    return (test_bit(static_cast<int>(flag), &fi_.flags) != 0);
  }

  inline void ClearAdvise(const FAdvise &bit) { clear_bit(static_cast<int>(bit), &fi_.i_advise); }
  inline void SetAdvise(const FAdvise &bit) { set_bit(static_cast<int>(bit), &fi_.i_advise); }
  inline uint8_t GetAdvise() const { return fi_.i_advise; }
  inline void SetAdvise(const uint8_t &bits) { fi_.i_advise = bits; }
  inline int IsAdviseSet(const FAdvise &bit) {
    return test_bit(static_cast<int>(bit), &fi_.i_advise);
  }

  inline uint64_t GetDirHashLevel() const { return fi_.clevel; }
  inline bool IsSameDirHash(const f2fs_hash_t &hash) const { return (fi_.chash == hash); }
  inline void ClearDirHash() { fi_.chash = 0; }
  inline void SetDirHash(const f2fs_hash_t &hash, const uint64_t &level) {
    fi_.chash = hash;
    fi_.clevel = level;
  }

  inline uint64_t GetCurDirDepth() const { return fi_.i_current_depth; }
  inline void SetCurDirDepth(const uint64_t depth) { fi_.i_current_depth = depth; }

  inline nid_t GetXattrNid() const { return fi_.i_xattr_nid; }
  inline void SetXattrNid(const nid_t nid) { fi_.i_xattr_nid = nid; }
  inline void ClearXattrNid() { fi_.i_xattr_nid = 0; }

  bool IsBad() { return TestFlag(InodeInfoFlag::kBad); }

  inline void Activate() __TA_EXCLUDES(mutex_) { SetFlag(InodeInfoFlag::kActive); }

  inline void Deactivate() __TA_EXCLUDES(mutex_) {
    ClearFlag(InodeInfoFlag::kActive);
    flag_cvar_.notify_all();
  }

  inline bool IsActive() __TA_EXCLUDES(mutex_) { return TestFlag(InodeInfoFlag::kActive); }

  bool WaitForDeactive(fs::SharedMutex &mutex) __TA_REQUIRES_SHARED(mutex) {
    if (IsActive()) {
      flag_cvar_.wait(mutex, [this]() {
        return (test_bit(static_cast<int>(InodeInfoFlag::kActive), &fi_.flags) == 0);
      });
      return true;
    }
    return false;
  }

  inline bool ClearDirty() __TA_EXCLUDES(mutex_) { return ClearFlag(InodeInfoFlag::kDirty); }

  inline bool IsDirty() __TA_EXCLUDES(mutex_) { return TestFlag(InodeInfoFlag::kDirty); }

  inline bool ShouldFlush() __TA_EXCLUDES(mutex_) {
    if (!GetNlink() || !IsDirty() || IsBad()) {
      return false;
    }
    return true;
  }

  void WaitForInit() __TA_EXCLUDES(mutex_) {
    fs::SharedLock lock(mutex_);
    if (test_bit(static_cast<int>(InodeInfoFlag::kInit), &fi_.flags)) {
      flag_cvar_.wait(
          mutex_, [this]() __TA_EXCLUDES(mutex_) { return (TestFlag(InodeInfoFlag::kInit) == 0); });
    }
  }

  void UnlockNewInode() __TA_EXCLUDES(mutex_) {
    ClearFlag(InodeInfoFlag::kInit);
    flag_cvar_.notify_all();
  }

 protected:
  void RecycleNode() override;
  std::condition_variable_any flag_cvar_{};
  fs::SharedMutex io_lock_;

 private:
  zx_status_t OpenNode(ValidatedOptions options, fbl::RefPtr<Vnode> *out_redirect) final
      __TA_EXCLUDES(mutex_);
  zx_status_t CloseNode() final;

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
  fs::WatcherContainer watcher_{};
};

}  // namespace f2fs

#endif  // THIRD_PARTY_F2FS_VNODE_H_
