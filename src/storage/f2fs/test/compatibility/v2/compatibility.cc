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

bool LinuxTestFile::IsValid() {
  std::string result;
  linux_operator_->ExecuteWithAssert(
      {"[ -e ", linux_operator_->ConvertPath(filename_), " ]; echo $?"}, &result);
  return result == "0" || result == "0\n";
}

ssize_t LinuxTestFile::Write(const void* buf, size_t count) {
  const uint8_t* buf_c = static_cast<const uint8_t*>(buf);

  std::string hex_string;
  for (uint64_t i = 0; i < count; ++i) {
    char substring[10];
    sprintf(substring, "\\x%02x", buf_c[i]);
    hex_string.append(substring);

    if (i > 0 && i % 500 == 0) {
      linux_operator_->ExecuteWithAssert({"echo", "-en",
                                          std::string("\"").append(hex_string).append("\""), ">>",
                                          linux_operator_->ConvertPath(filename_)});
      hex_string.clear();
    }
  }

  linux_operator_->ExecuteWithAssert({"echo", "-en",
                                      std::string("\"").append(hex_string).append("\""), ">>",
                                      linux_operator_->ConvertPath(filename_)});
  linux_operator_->ExecuteWithAssert({"ls -al", linux_operator_->ConvertPath(filename_)});

  return count;
}

ssize_t FuchsiaTestFile::Read(void* buf, size_t count) {
  if (!vnode_->IsReg()) {
    return 0;
  }

  File* file = static_cast<File*>(vnode_.get());
  size_t ret = 0;

  if (file->Read(buf, count, offset_, &ret) != ZX_OK) {
    return 0;
  }

  offset_ += ret;

  return ret;
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
  ExecuteWithAssert({"mkfs.f2fs", test_device_, "-f", opt.data()});
}

void LinuxOperator::Fsck() { ExecuteWithAssert({"fsck.f2fs", test_device_, "--dry-run"}); }

void LinuxOperator::Mount(std::string_view opt) {
  ExecuteWithAssert({"mkdir", "-p", mount_path_});
  ExecuteWithAssert({"mount", test_device_, mount_path_, opt.data()});
}

void LinuxOperator::Umount() { ExecuteWithAssert({"umount", mount_path_}); }

void LinuxOperator::Mkdir(std::string_view path, mode_t mode) {
  ExecuteWithAssert({"mkdir", "-m", ConvertModeString(mode), ConvertPath(path)});
}

std::unique_ptr<TestFile> LinuxOperator::Open(std::string_view path, int flags, mode_t mode) {
  if (flags & O_CREAT) {
    if (flags & O_DIRECTORY) {
      Mkdir(path, mode);
    } else {
      ExecuteWithAssert({"touch", ConvertPath(path)});
      ExecuteWithAssert({"chmod", ConvertModeString(mode), ConvertPath(path)});
    }
  }

  return std::unique_ptr<TestFile>(new LinuxTestFile(path, this));
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

void FuchsiaOperator::Mkdir(std::string_view path, mode_t mode) {
  auto new_dir = Open(path, O_CREAT | O_EXCL, S_IFDIR | mode);
  ASSERT_TRUE(new_dir->IsValid());
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
