// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_F2FS_DIR_H_
#define SRC_STORAGE_F2FS_DIR_H_

namespace f2fs {

extern const unsigned char kFiletypeTable[];

class Dir : public VnodeF2fs, public fbl::Recyclable<Dir> {
 public:
  explicit Dir(F2fs *fs);
  explicit Dir(F2fs *fs, ino_t ino);
  ~Dir() = default;

  // Required for memory management, see the class comment above Vnode for more.
  void fbl_recycle() { RecycleNode(); }

  // Lookup
  zx_status_t Lookup(std::string_view name, fbl::RefPtr<fs::Vnode> *out) final;
  zx_status_t DoLookup(std::string_view name, fbl::RefPtr<fs::Vnode> *out);
  DirEntry *FindEntry(std::string_view name, Page **res_page) __TA_EXCLUDES(io_lock_);
  DirEntry *FindInInlineDir(const std::string_view &name, Page **res_page);
  DirEntry *FindInBlock(Page *dentry_page, const char *name, int namelen, int *max_slots,
                        f2fs_hash_t namehash, Page **res_page);
  DirEntry *FindInLevel(unsigned int level, std::string_view name, int namelen,
                        f2fs_hash_t namehash, Page **res_page);
  zx_status_t Readdir(fs::VdirCookie *cookie, void *dirents, size_t len, size_t *out_actual) final
      __TA_EXCLUDES(io_lock_);
  zx_status_t ReadInlineDir(fs::VdirCookie *cookie, void *dirents, size_t len, size_t *out_actual);

  // delete & set link
  zx_status_t Rename(fbl::RefPtr<fs::Vnode> _newdir, std::string_view oldname,
                     std::string_view newname, bool src_must_be_dir, bool dst_must_be_dir);
  void SetLink(DirEntry *de, Page *page, VnodeF2fs *inode) __TA_EXCLUDES(io_lock_)
      __TA_EXCLUDES(io_lock_);
  DirEntry *ParentDir(Page **p);
  DirEntry *ParentInlineDir(Page **p);
  bool IsEmptyDir();
  bool IsEmptyInlineDir();
  zx::status<bool> IsSubdir(Dir *possible_dir);

  // create
  zx_status_t Link(std::string_view name, fbl::RefPtr<fs::Vnode> _target) final;
  zx_status_t Create(std::string_view name, uint32_t mode, fbl::RefPtr<fs::Vnode> *out) final;
  zx_status_t DoCreate(std::string_view name, uint32_t mode, fbl::RefPtr<fs::Vnode> *out);
  zx_status_t NewInode(uint32_t mode, fbl::RefPtr<VnodeF2fs> *out);
  zx_status_t Mkdir(std::string_view name, uint32_t mode, fbl::RefPtr<fs::Vnode> *out);
  zx_status_t AddLink(std::string_view name, VnodeF2fs *vnode) __TA_EXCLUDES(io_lock_)
      __TA_EXCLUDES(io_lock_);
  zx_status_t AddInlineEntry(std::string_view name, VnodeF2fs *vnode, bool *is_converted);
  unsigned int RoomInInlineDir(InlineDentry *dentry_blk, int slots);
  zx_status_t ConvertInlineDir(InlineDentry *inline_dentry);
  int RoomForFilename(DentryBlock *dentry_blk, int slots);
  void UpdateParentMetadata(VnodeF2fs *inode, unsigned int current_depth);
  zx_status_t InitInodeMetadata(VnodeF2fs *vnode);
  zx_status_t MakeEmpty(VnodeF2fs *vnode, VnodeF2fs *parent);
  zx_status_t MakeEmptyInlineDir(VnodeF2fs *vnode, VnodeF2fs *parent);
  void InitDentInode(VnodeF2fs *vnode, Page *ipage);

  // delete
  zx_status_t Unlink(std::string_view name, bool must_be_dir) final;
  zx_status_t Rmdir(Dir *vnode, std::string_view name);
  zx_status_t DoUnlink(VnodeF2fs *vnode, std::string_view name);
  void DeleteEntry(DirEntry *dentry, Page *page, VnodeF2fs *vnode) __TA_EXCLUDES(io_lock_)
      __TA_EXCLUDES(io_lock_);
  void DeleteInlineEntry(DirEntry *dentry, Page *page, VnodeF2fs *vnode);

  // helper
  ino_t InodeByName(std::string_view name);
  int IsMultimediaFile(const char *s, const char *sub);
  void SetColdFile(const char *name, VnodeF2fs *vnode);
  uint64_t DirBlocks();
  static unsigned int DirBuckets(unsigned int level);
  static unsigned int BucketBlocks(unsigned int level);
  void SetDeType(DirEntry *de, VnodeF2fs *vnode);
  static uint64_t DirBlockIndex(unsigned int level, unsigned int idx);
  bool EarlyMatchName(const char *name, int namelen, f2fs_hash_t namehash, DirEntry *de);

#if 0  // porting needed
//   int F2fsLink(dentry *old_dentry, dentry *dentry);
//   dentry *F2fsGetParent(dentry *child);
//   int F2fsSymlink(dentry *dentry, const char *symname);
//   int F2fsMknod(dentry *dentry, umode_t mode, dev_t rdev);
#endif
};

}  // namespace f2fs

#endif  // SRC_STORAGE_F2FS_DIR_H_
