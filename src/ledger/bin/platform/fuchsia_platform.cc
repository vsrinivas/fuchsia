
// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/platform/fuchsia_platform.h"

#include "src/lib/files/directory.h"
#include "src/lib/files/file.h"
#include "src/lib/files/path.h"
#include "util/env_fuchsia.h"

namespace ledger {
namespace {

class FuchsiaFileDescriptor : public FileSystem::FileDescriptor {
 public:
  // Creates a FuchsiaFileDescriptor with the given file descriptor.
  FuchsiaFileDescriptor(int fd) : unique_fd_(fd) {}

  ~FuchsiaFileDescriptor() = default;

  // Returns the int representation of this FuchsiaFileDescriptor.
  int Get() { return unique_fd_.get(); }

  // FileSystem::FileDescriptor:
  bool IsValid() { return unique_fd_.is_valid(); }

 private:
  fbl::unique_fd unique_fd_;
};

}  // namespace

// Opens a FileDescriptor at the given |path|. If the operation fails, the returned FileDescriptor
// will be invalid.
std::unique_ptr<FileSystem::FileDescriptor> FuchsiaFileSystem::OpenFD(DetachedPath path,
                                                                      DetachedPath* result_path) {
  auto fd = std::make_unique<FuchsiaFileDescriptor>(
      openat(path.root_fd(), path.path().c_str(), O_RDONLY | O_DIRECTORY));
  if (fd->IsValid()) {
    *result_path = DetachedPath(fd->Get());
  }
  return fd;
}

std::unique_ptr<leveldb::Env> FuchsiaFileSystem::MakeLevelDbEnvironment(
    DetachedPath db_path, DetachedPath* updated_db_path) {
  std::unique_ptr<FileSystem::FileDescriptor> unique_fd;
  *updated_db_path = db_path;
  if (db_path.path() != ".") {
    // Open a FileDescriptor at the db path.
    unique_fd = OpenFD(db_path, updated_db_path);
    if (!unique_fd->IsValid()) {
      FXL_LOG(ERROR) << "Unable to open directory at " << db_path.path() << ". errno: " << errno;
      return nullptr;
    }
  }
  return leveldb::MakeFuchsiaEnv(updated_db_path->root_fd());
}

bool FuchsiaFileSystem::ReadFileToString(DetachedPath path, std::string* content) {
  return files::ReadFileToStringAt(path.root_fd(), path.path(), content);
}

bool FuchsiaFileSystem::WriteFile(DetachedPath path, const std::string& content) {
  return files::WriteFileAt(path.root_fd(), path.path(), content.c_str(), content.size());
}

bool FuchsiaFileSystem::IsFile(DetachedPath path) {
  return files::IsFileAt(path.root_fd(), path.path());
}

bool FuchsiaFileSystem::GetFileSize(DetachedPath path, uint64_t* size) {
  return files::GetFileSizeAt(path.root_fd(), path.path(), size);
}

bool FuchsiaFileSystem::CreateDirectory(DetachedPath path) {
  return files::CreateDirectoryAt(path.root_fd(), path.path());
}

bool FuchsiaFileSystem::IsDirectory(DetachedPath path) {
  return files::IsDirectoryAt(path.root_fd(), path.path());
}

bool FuchsiaFileSystem::DeletePath(DetachedPath path) {
  return files::DeletePathAt(path.root_fd(), path.path(), /*recursive*/ false);
}

bool FuchsiaFileSystem::DeletePathRecursively(DetachedPath path) {
  return files::DeletePathAt(path.root_fd(), path.path(), /*recursive*/ true);
}

std::unique_ptr<Platform> MakePlatform() { return std::make_unique<FuchsiaPlatform>(); }

}  // namespace ledger
