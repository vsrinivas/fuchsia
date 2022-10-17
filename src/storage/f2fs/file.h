// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_F2FS_FILE_H_
#define SRC_STORAGE_F2FS_FILE_H_

namespace f2fs {
class File : public VnodeF2fs, public fbl::Recyclable<File> {
 public:
  explicit File(F2fs* fs, ino_t ino);

  // Required for memory management, see the class comment above Vnode for more.
  void fbl_recycle() { RecycleNode(); }

#if 0  // porting needed
  // int F2fsVmPageMkwrite(vm_area_struct* vma, vm_fault* vmf);
  // int F2fsFileMmap(/*file *file,*/ vm_area_struct* vma);
  // void FillZero(pgoff_t index, loff_t start, loff_t len);
  // int PunchHole(loff_t offset, loff_t len, int mode);
  // int ExpandInodeData(loff_t offset, off_t len, int mode);
  // long F2fsFallocate(int mode, loff_t offset, loff_t len);
  // uint32_t F2fsMaskFlags(umode_t mode, uint32_t flags);
  // long F2fsIoctl(/*file *filp,*/ unsigned int cmd, uint64_t arg);
#endif

  zx_status_t Read(void* data, size_t len, size_t off, size_t* out_actual) final
      __TA_EXCLUDES(mutex_);
  zx_status_t DoWrite(const void* data, size_t len, size_t offset, size_t* out_actual)
      __TA_EXCLUDES(mutex_);
  zx_status_t Write(const void* data, size_t len, size_t offset, size_t* out_actual) final
      __TA_EXCLUDES(mutex_);
  zx_status_t Append(const void* data, size_t len, size_t* out_end, size_t* out_actual) final
      __TA_EXCLUDES(mutex_);
  zx_status_t Truncate(size_t len) final __TA_EXCLUDES(mutex_);
#ifdef __Fuchsia__
  zx::result<> PopulateVmoWithInlineData(zx::vmo& vmo) final __TA_EXCLUDES(mutex_);
#endif  // __Fuchsia__
  zx_status_t RecoverInlineData(NodePage& node_page) final __TA_EXCLUDES(mutex_);

 private:
  zx_status_t ReadInline(void* data, size_t len, size_t off, size_t* out_actual)
      __TA_EXCLUDES(mutex_);
  zx_status_t WriteInline(const void* data, size_t len, size_t offset, size_t* out_actual)
      __TA_EXCLUDES(mutex_);
  zx_status_t TruncateInline(size_t len, bool is_recover) __TA_EXCLUDES(mutex_);
  zx_status_t ConvertInlineData() __TA_EXCLUDES(mutex_);

  uint8_t* InlineDataPtr(Page* page);
  loff_t MaxFileSize(unsigned bits);
};

}  // namespace f2fs

#endif  // SRC_STORAGE_F2FS_FILE_H_
