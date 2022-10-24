// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/f2fs/test/compatibility/compatibility.h"

#include <filesystem>

namespace f2fs {

std::string GenerateTestPath(std::string_view format) {
  std::string test_path = std::filesystem::temp_directory_path().append(format).string();
  return test_path;
}

bool HostTestFile::is_valid() const { return fd_.is_valid(); }

ssize_t HostTestFile::Read(void *buf, size_t count) { return read(fd_.get(), buf, count); }

ssize_t HostTestFile::Write(const void *buf, size_t count) { return write(fd_.get(), buf, count); }

int HostTestFile::Fchmod(mode_t mode) { return fchmod(fd_.get(), mode); }

int HostTestFile::Fstat(struct stat *file_stat) { return fstat(fd_.get(), file_stat); }

int HostTestFile::Ftruncate(off_t len) { return ftruncate(fd_.get(), len); }

int HostTestFile::Fallocate(int mode, off_t offset, off_t len) {
  return fallocate(fd_.get(), mode, offset, len);
}

void HostOperator::Mkfs(std::string_view opt) {
  ASSERT_EQ(
      system(std::string("mkfs.f2fs ").append(opt).append(" ").append(test_image_path_).c_str()),
      0);
}

void HostOperator::Mount(std::string_view opt) {
  ASSERT_EQ(system(std::string("mount -t f2fs ")
                       .append(test_image_path_)
                       .append(" ")
                       .append(opt)
                       .append(" ")
                       .append(mount_directory_)
                       .c_str()),
            0);
}

void HostOperator::Unmount() {
  ASSERT_EQ(system(std::string("umount ").append(mount_directory_).c_str()), 0);
}

void HostOperator::Fsck() {
  ASSERT_EQ(system(std::string("fsck.f2fs --dry-run ").append(test_image_path_).c_str()), 0);
}

void HostOperator::Mkdir(std::string_view path, mode_t mode) {
  ASSERT_EQ(mkdir(GetAbsolutePath(path).c_str(), mode), 0);
}

int HostOperator::Rmdir(std::string_view path) { return rmdir(GetAbsolutePath(path).c_str()); }

std::unique_ptr<TestFile> HostOperator::Open(std::string_view path, int flags, mode_t mode) {
  std::unique_ptr<TestFile> ret =
      std::make_unique<HostTestFile>(open(GetAbsolutePath(path).c_str(), flags, mode));
  return ret;
}

void HostOperator::Rename(std::string_view oldpath, std::string_view newpath) {
  ASSERT_EQ(rename(GetAbsolutePath(oldpath).c_str(), GetAbsolutePath(newpath).c_str()), 0);
}

bool TargetTestFile::is_valid() const { return (vnode_ != nullptr); }

ssize_t TargetTestFile::Read(void *buf, size_t count) {
  if (!vnode_->IsReg()) {
    return 0;
  }

  File *file = static_cast<File *>(vnode_.get());
  size_t ret = 0;

  if (file->Read(buf, count, offset_, &ret) != ZX_OK) {
    return 0;
  }

  offset_ += ret;

  return ret;
}

ssize_t TargetTestFile::Write(const void *buf, size_t count) {
  if (!vnode_->IsReg()) {
    return 0;
  }

  File *file = static_cast<File *>(vnode_.get());
  size_t ret = 0;

  if (file->Write(buf, count, offset_, &ret) != ZX_OK) {
    return 0;
  }

  offset_ += ret;

  return ret;
}

int TargetTestFile::Fchmod(mode_t mode) { return -ENOTSUP; }

int TargetTestFile::Fstat(struct stat *file_stat) {
  fs::VnodeAttributes attr;
  if (zx_status_t status = vnode_->GetAttributes(&attr); status != ZX_OK) {
    // TODO: convert |status| to errno
    return -EIO;
  }

  file_stat->st_ino = attr.inode;
  file_stat->st_mode = static_cast<mode_t>(attr.mode);
  file_stat->st_nlink = attr.link_count;
  file_stat->st_size = attr.content_size;
  file_stat->st_ctim.tv_sec = attr.creation_time / ZX_SEC(1);
  file_stat->st_ctim.tv_nsec = attr.creation_time % ZX_SEC(1);
  file_stat->st_mtim.tv_sec = attr.modification_time / ZX_SEC(1);
  file_stat->st_mtim.tv_nsec = attr.modification_time % ZX_SEC(1);
  file_stat->st_blocks = (vnode_->GetBlocks())
                         << vnode_->fs()->GetSuperblockInfo().GetLogSectorsPerBlock();

  return 0;
}

int TargetTestFile::Ftruncate(off_t len) {
  if (!vnode_->IsReg()) {
    return -ENOTSUP;
  }

  File *file = static_cast<File *>(vnode_.get());
  if (zx_status_t status = file->Truncate(len); status != ZX_OK) {
    // TODO: convert |status| to errno
    return -EIO;
  }

  return 0;
}

int TargetTestFile::Fallocate(int mode, off_t offset, off_t len) { return -EOPNOTSUPP; }

void TargetOperator::Mkfs(const MkfsOptions &opt) {
  if (bcache_ == nullptr) {
    ASSERT_EQ(Bcache::Create(std::move(test_image_fd_), block_count_, &bcache_), ZX_OK);
  }

  MkfsWorker mkfs(std::move(bcache_), opt);
  auto ret = mkfs.DoMkfs();
  ASSERT_EQ(ret.is_error(), false);

  bcache_ = std::move(*ret);
}

void TargetOperator::Mount(const MountOptions &opt) {
  if (bcache_ == nullptr) {
    ASSERT_EQ(Bcache::Create(std::move(test_image_fd_), block_count_, &bcache_), ZX_OK);
  }

  // Create a vfs object for unit tests.
  // Host tests do not require async_dispatcher_t.
  auto vfs_or = Runner::CreateRunner(nullptr);
  ASSERT_TRUE(vfs_or.is_ok());
  auto fs_or = F2fs::Create(nullptr, std::move(bcache_), opt, (*vfs_or).get());
  ASSERT_TRUE(fs_or.is_ok());
  (*fs_or)->SetVfsForTests(std::move(*vfs_or));
  fs_ = std::move(*fs_or);
  ASSERT_EQ(VnodeF2fs::Vget(fs_.get(), fs_->RawSb().root_ino, &root_), ZX_OK);
  ASSERT_EQ(root_->Open(root_->ValidateOptions(fs::VnodeConnectionOptions()).value(), nullptr),
            ZX_OK);
}

void TargetOperator::Unmount() {
  ASSERT_EQ(root_->Close(), ZX_OK);
  root_.reset();
  fs_->SyncFs(true);
  fs_->PutSuper();
  auto vfs_or = fs_->TakeVfsForTests();
  ASSERT_TRUE(vfs_or.is_ok());
  auto bc_or = fs_->TakeBc();
  ASSERT_TRUE(bc_or.is_ok());
  bcache_ = std::move(*bc_or);
  // Trigger teardown before deleting fs.
  (*vfs_or).reset();
}

void TargetOperator::Fsck() {
  if (bcache_ == nullptr) {
    ASSERT_EQ(Bcache::Create(std::move(test_image_fd_), block_count_, &bcache_), ZX_OK);
  }

  FsckWorker fsck(std::move(bcache_), FsckOptions{.repair = false});
  ASSERT_EQ(fsck.Run(), ZX_OK);
  bcache_ = fsck.Destroy();
}

void TargetOperator::Mkdir(std::string_view path, mode_t mode) {
  auto new_dir = Open(path, O_CREAT | O_EXCL, S_IFDIR | mode);
  ASSERT_TRUE(new_dir->is_valid());
}

int TargetOperator::Rmdir(std::string_view path) {
  auto r = GetLastDirVnodeAndFileName(path);
  if (!r.is_ok()) {
    // TODO: convert |status| to errno
    return -1;
  }
  auto [parent_vn, child_name] = r.value();

  if (zx_status_t status = fs_->vfs()->Unlink(parent_vn, child_name, true); status != ZX_OK) {
    // TODO: convert |status| to errno
    return -1;
  }
  return 0;
}

fs::VnodeConnectionOptions ConvertFlag(int flags) {
  fs::VnodeConnectionOptions options;

  // TODO: O_PATH, O_DIRECT, O_TRUNC, O_APPEND
  switch (flags & O_ACCMODE) {
    case O_RDONLY:
      options.rights.read = true;
      break;
    case O_WRONLY:
      options.rights.write = true;
      break;
    case O_RDWR:
      options.rights.read = true;
      options.rights.write = true;
      break;
    default:
      break;
  }

  if (flags & O_CREAT) {
    options.flags.create = true;
  }
  if (flags & O_EXCL) {
    options.flags.fail_if_exists = true;
  }

  return options;
}

std::unique_ptr<TestFile> TargetOperator::Open(std::string_view path, int flags, mode_t mode) {
  auto result = fs_->vfs()->Open(root_, path, ConvertFlag(flags), fs::Rights::ReadWrite(), mode);
  if (result.is_error()) {
    return std::unique_ptr<TestFile>(new TargetTestFile(nullptr));
  }

  fbl::RefPtr<VnodeF2fs> vnode = fbl::RefPtr<VnodeF2fs>::Downcast(result.ok().vnode);

  std::unique_ptr<TestFile> ret = std::make_unique<TargetTestFile>(std::move(vnode));

  return ret;
}

void TargetOperator::Rename(std::string_view oldpath, std::string_view newpath) {
  auto result = GetLastDirVnodeAndFileName(oldpath);
  ASSERT_TRUE(result.is_ok());
  auto [oldparent_vn, oldchild_name] = result.value();

  result = GetLastDirVnodeAndFileName(newpath);
  ASSERT_TRUE(result.is_ok());
  auto [newparent_vn, newchild_name] = result.value();

  ASSERT_EQ(oldparent_vn->Rename(newparent_vn, oldchild_name, newchild_name, false, false), ZX_OK);
}

zx::result<std::pair<fbl::RefPtr<fs::Vnode>, std::string>>
TargetOperator::GetLastDirVnodeAndFileName(std::string_view absolute_path) {
  std::filesystem::path path(absolute_path);
  if (!path.has_root_directory() || !path.has_filename()) {
    return zx::error(ZX_ERR_INVALID_ARGS);
  }
  fbl::RefPtr<fs::Vnode> vn = root_;

  for (auto token : path.parent_path().relative_path()) {
    if (auto r = vn->Lookup(token.string(), &vn); r != ZX_OK) {
      return zx::error(r);
    }
  }
  return zx::ok(make_pair(std::move(vn), path.filename().string()));
}

}  // namespace f2fs
