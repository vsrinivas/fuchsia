// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/f2fs/test/compatibility/v2/compatibility.h"

namespace f2fs {
std::string ConvertModeString(mode_t mode) {
  std::stringstream ss;
  ss << std::oct << mode;
  return ss.str();
}

zx_status_t LinuxOperator::Execute(const std::vector<std::string>& argv, std::string* result) {
  return debian_guest_->Execute(argv, {}, zx::time::infinite(), result, nullptr);
}

void LinuxOperator::ExecuteWithAssert(const std::vector<std::string>& argv, std::string* result) {
  ASSERT_EQ(Execute(argv, result), ZX_OK);
}

std::string LinuxOperator::ConvertPath(std::string_view path) {
  // Convert only if |path| starts with |linux_path_prefix|
  if (path.substr(0, linux_path_prefix.length()) == linux_path_prefix) {
    return std::string(mount_path_).append("/").append(path.substr(linux_path_prefix.length()));
  }

  return std::string(path);
}

void LinuxOperator::Mkfs(std::string_view opt) {
  ASSERT_EQ(Execute({"mkfs.f2fs", test_device_, "-f", opt.data()}), ZX_OK);
}

void LinuxOperator::Fsck() { ASSERT_EQ(Execute({"fsck.f2fs", test_device_, "--dry-run"}), ZX_OK); }

void LinuxOperator::Mount(std::string_view opt) {
  ASSERT_EQ(Execute({"mkdir", "-p", mount_path_}), ZX_OK);
  ASSERT_EQ(Execute({"mount", test_device_, mount_path_, opt.data()}), ZX_OK);
}

void LinuxOperator::Umount() { ASSERT_EQ(Execute({"umount", mount_path_}), ZX_OK); }

void LinuxOperator::Mkdir(std::string_view path, mode_t mode) {
  ASSERT_EQ(Execute({"mkdir", "-m", ConvertModeString(mode), ConvertPath(path)}), ZX_OK);
}

void FuchsiaOperator::Mkfs(MkfsOptions opt) {
  MkfsWorker mkfs(std::move(bc_), opt);
  auto ret = mkfs.DoMkfs();
  ASSERT_TRUE(ret.is_ok());
  bc_ = std::move(*ret);
}

void FuchsiaOperator::Fsck() {
  FsckWorker fsck(std::move(bc_), FsckOptions{.repair = false});
  ASSERT_EQ(fsck.Run(), ZX_OK);
  bc_ = fsck.Destroy();
}

void FuchsiaOperator::Mount(MountOptions opt) {
  auto vfs_or = Runner::CreateRunner(loop_.dispatcher());
  ASSERT_TRUE(vfs_or.is_ok());

  auto fs_or = F2fs::Create(loop_.dispatcher(), std::move(bc_), opt, (*vfs_or).get());
  ASSERT_TRUE(fs_or.is_ok());
  (*fs_or)->SetVfsForTests(std::move(*vfs_or));
  fs_ = std::move(*fs_or);

  ASSERT_EQ(VnodeF2fs::Vget(fs_.get(), fs_->RawSb().root_ino, &root_), ZX_OK);
  ASSERT_EQ(root_->Open(root_->ValidateOptions(fs::VnodeConnectionOptions()).value(), nullptr),
            ZX_OK);
}

void FuchsiaOperator::Umount() {
  ASSERT_EQ(root_->Close(), ZX_OK);
  root_.reset();

  fs_->SyncFs(true);
  fs_->PutSuper();

  auto vfs_or = fs_->TakeVfsForTests();
  ASSERT_TRUE(vfs_or.is_ok());

  auto bc_or = fs_->TakeBc();
  ASSERT_TRUE(bc_or.is_ok());
  bc_ = std::move(*bc_or);

  (*vfs_or).reset();
}

std::unique_ptr<TestFile> FuchsiaOperator::Open(std::string_view path, int flags, mode_t mode) {
  auto result = fs_->vfs()->Open(root_, path, ConvertFlag(flags), fs::Rights::ReadWrite(), mode);
  if (result.is_error()) {
    return std::unique_ptr<TestFile>(new FuchsiaTestFile(nullptr));
  }

  fbl::RefPtr<VnodeF2fs> vnode = fbl::RefPtr<VnodeF2fs>::Downcast(result.ok().vnode);

  std::unique_ptr<TestFile> ret = std::make_unique<FuchsiaTestFile>(std::move(vnode));

  return ret;
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

}  // namespace f2fs
