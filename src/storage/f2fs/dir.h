// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_F2FS_DIR_H_
#define SRC_STORAGE_F2FS_DIR_H_

namespace f2fs {

extern const unsigned char kFiletypeTable[];

class Dir : public VnodeF2fs, public fbl::Recyclable<Dir> {
 public:
  explicit Dir(F2fs *fs, ino_t ino);

  // Required for memory management, see the class comment above Vnode for more.
  void fbl_recycle() { RecycleNode(); }

  // Lookup
  zx_status_t Lookup(std::string_view name, fbl::RefPtr<fs::Vnode> *out) final
      __TA_EXCLUDES(dir_mutex_);

  zx_status_t DoLookup(std::string_view name, fbl::RefPtr<fs::Vnode> *out)
      __TA_REQUIRES_SHARED(dir_mutex_);
  DirEntry *FindEntryOnDevice(std::string_view name, fbl::RefPtr<Page> *res_page)
      __TA_REQUIRES_SHARED(dir_mutex_);
  DirEntry *FindEntry(std::string_view name, fbl::RefPtr<Page> *res_page)
      __TA_REQUIRES_SHARED(dir_mutex_);
  zx::result<DirEntry> FindEntry(std::string_view name) __TA_REQUIRES_SHARED(dir_mutex_);
  DirEntry *FindInInlineDir(std::string_view name, fbl::RefPtr<Page> *res_page)
      __TA_REQUIRES_SHARED(dir_mutex_);
  DirEntry *FindInBlock(fbl::RefPtr<Page> dentry_page, std::string_view name, uint64_t *max_slots,
                        f2fs_hash_t namehash, fbl::RefPtr<Page> *res_page);
  DirEntry *FindInLevel(unsigned int level, std::string_view name, f2fs_hash_t namehash,
                        fbl::RefPtr<Page> *res_page) __TA_REQUIRES_SHARED(dir_mutex_);
  zx_status_t Readdir(fs::VdirCookie *cookie, void *dirents, size_t len, size_t *out_actual) final
      __TA_EXCLUDES(dir_mutex_);
  zx_status_t ReadInlineDir(fs::VdirCookie *cookie, void *dirents, size_t len, size_t *out_actual)
      __TA_REQUIRES_SHARED(dir_mutex_);

  // rename
  zx_status_t Rename(fbl::RefPtr<fs::Vnode> _newdir, std::string_view oldname,
                     std::string_view newname, bool src_must_be_dir, bool dst_must_be_dir) final
      __TA_EXCLUDES(dir_mutex_);
  void SetLink(DirEntry *de, fbl::RefPtr<Page> &page, VnodeF2fs *inode) __TA_REQUIRES(dir_mutex_);
  DirEntry *ParentDir(fbl::RefPtr<Page> *out) __TA_EXCLUDES(dir_mutex_);
  DirEntry *ParentInlineDir(fbl::RefPtr<Page> *out) __TA_REQUIRES_SHARED(dir_mutex_);

  // create and link
  zx_status_t Link(std::string_view name, fbl::RefPtr<fs::Vnode> _target) final
      __TA_EXCLUDES(dir_mutex_);
  zx_status_t Create(std::string_view name, uint32_t mode, fbl::RefPtr<fs::Vnode> *out) final
      __TA_EXCLUDES(dir_mutex_);
  zx_status_t DoCreate(std::string_view name, uint32_t mode, fbl::RefPtr<fs::Vnode> *out)
      __TA_REQUIRES(dir_mutex_);
  zx_status_t NewInode(uint32_t mode, fbl::RefPtr<VnodeF2fs> *out) __TA_REQUIRES(dir_mutex_);
  zx_status_t Mkdir(std::string_view name, uint32_t mode, fbl::RefPtr<fs::Vnode> *out)
      __TA_REQUIRES(dir_mutex_);
  zx_status_t AddLink(std::string_view name, VnodeF2fs *vnode) __TA_REQUIRES(dir_mutex_);
  zx::result<bool> AddInlineEntry(std::string_view name, VnodeF2fs *vnode)
      __TA_REQUIRES(dir_mutex_);
  zx_status_t ConvertInlineDir() __TA_REQUIRES(dir_mutex_);
  void UpdateParentMetadata(VnodeF2fs *inode, unsigned int current_depth) __TA_REQUIRES(dir_mutex_);
  zx_status_t InitInodeMetadata(VnodeF2fs *vnode) __TA_REQUIRES(dir_mutex_);
  zx_status_t MakeEmpty(VnodeF2fs *vnode) __TA_REQUIRES(dir_mutex_);
  zx_status_t MakeEmptyInlineDir(VnodeF2fs *vnode) __TA_REQUIRES(dir_mutex_);
  void InitDentInode(VnodeF2fs *vnode, NodePage &page) __TA_REQUIRES(dir_mutex_);
  unsigned int RoomInInlineDir(Page *ipage, int slots) __TA_REQUIRES_SHARED(dir_mutex_);
  int RoomForFilename(DentryBlock *dentry_blk, int slots) __TA_REQUIRES_SHARED(dir_mutex_);

  // delete
  zx_status_t Unlink(std::string_view name, bool must_be_dir) final __TA_EXCLUDES(dir_mutex_);
  zx_status_t Rmdir(Dir *vnode, std::string_view name) __TA_REQUIRES(dir_mutex_);
  zx_status_t DoUnlink(VnodeF2fs *vnode, std::string_view name) __TA_REQUIRES(dir_mutex_);
  void DeleteEntry(DirEntry *dentry, fbl::RefPtr<Page> &page, VnodeF2fs *vnode)
      __TA_REQUIRES(dir_mutex_);
  void DeleteInlineEntry(DirEntry *dentry, fbl::RefPtr<Page> &page, VnodeF2fs *vnode)
      __TA_REQUIRES(dir_mutex_);

  // recovery
  zx::result<> RecoverLink(VnodeF2fs &vnode) __TA_EXCLUDES(dir_mutex_);

  // inline helper
  uint64_t InlineDentryBitmapSize() const;

  // helper
  static uint32_t DirBuckets(uint32_t level, uint8_t dir_level);
  static uint32_t BucketBlocks(uint32_t level);
  static uint64_t DirBlockIndex(uint32_t level, uint8_t dir_level, uint32_t idx);
  void SetColdFile(VnodeF2fs &vnode);

 private:
  // helper
  bool IsMultimediaFile(VnodeF2fs &vnode, std::string_view sub);
  block_t DirBlocks();
  void SetDeType(DirEntry *de, VnodeF2fs *vnode);
  bool EarlyMatchName(std::string_view name, f2fs_hash_t namehash, const DirEntry &de);
  zx::result<bool> IsSubdir(Dir *possible_dir);
  bool IsEmptyDir();
  bool IsEmptyInlineDir();

  // inline helper
  uint8_t *InlineDentryBitmap(Page *page);
  DirEntry *InlineDentryArray(Page *page, VnodeF2fs &vnode);
  uint8_t (*InlineDentryFilenameArray(Page *page, VnodeF2fs &vnode))[kDentrySlotLen];

  // link helper to update link information in Rename()
  DirEntry *FindEntrySafe(std::string_view name, fbl::RefPtr<Page> *res_page)
      __TA_EXCLUDES(dir_mutex_);
  zx_status_t AddLinkSafe(std::string_view name, VnodeF2fs *vnode) __TA_EXCLUDES(dir_mutex_);
  void SetLinkSafe(DirEntry *de, fbl::RefPtr<Page> &page, VnodeF2fs *inode)
      __TA_EXCLUDES(dir_mutex_);

  // It must be acquired only by link helpers or overriding methods from fs::vnode.
  fs::SharedMutex dir_mutex_;
#if 0  // porting needed
//   int F2fsSymlink(dentry *dentry, const char *symname);
//   int F2fsMknod(dentry *dentry, umode_t mode, dev_t rdev);
#endif
};

}  // namespace f2fs

#endif  // SRC_STORAGE_F2FS_DIR_H_
