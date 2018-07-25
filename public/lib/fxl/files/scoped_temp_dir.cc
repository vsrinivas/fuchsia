// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fxl/files/scoped_temp_dir.h"

#include "lib/fxl/build_config.h"

#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include "lib/fxl/files/directory.h"
#include "lib/fxl/files/eintr_wrapper.h"
#include "lib/fxl/files/file.h"
#include "lib/fxl/files/path.h"
#include "lib/fxl/files/unique_fd.h"
#include "lib/fxl/logging.h"

namespace files {

namespace {
fxl::StringView GetGlobalTmpDir() {
  const char* env_var = getenv("TMPDIR");
  return fxl::StringView(env_var ? env_var : "/tmp");
}

// Fills the first 6 bytes of |tp| with random characters suitable for the file
// system. The implementation is taken from __randname.c in //zircon
void GenerateRandName(char* tp) {
  FXL_DCHECK(strlen(tp) >= 6);

  struct timespec ts;
  unsigned long r;

  clock_gettime(CLOCK_REALTIME, &ts);
  r = ts.tv_nsec * 65537 ^
      (reinterpret_cast<uintptr_t>(&ts) / 16 + reinterpret_cast<uintptr_t>(tp));
  for (size_t i = 0; i < 6; i++, r >>= 5) {
    tp[i] = 'A' + (r & 15) + (r & 16) * 2;
  }
}

// Creates a unique temporary file under |root_fd| from template |tp|.
fxl::UniqueFD MksTempAt(int root_fd, char* tp, size_t tp_length) {
  FXL_DCHECK(strlen(tp) == tp_length);
  FXL_DCHECK(tp_length >= 6);
  FXL_DCHECK(memcmp(tp + tp_length - 6, "XXXXXX", 6) == 0);
  int retries = 100;
  do {
    GenerateRandName(tp + tp_length - 6);
    fxl::UniqueFD result(
        HANDLE_EINTR(openat(root_fd, tp, O_CREAT | O_EXCL, 0700)));
    if (result.is_valid()) {
      return result;
    }
  } while (--retries && errno == EEXIST);

  memcpy(tp + tp_length - 6, "XXXXXX", 6);
  return fxl::UniqueFD();
}

// Creates a unique temporary directory under |root_fd| from template |tp|.
char* MkdTempAt(int root_fd, char* tp, size_t tp_length) {
  FXL_DCHECK(strlen(tp) == tp_length);
  FXL_DCHECK(tp_length >= 6);
  FXL_DCHECK(memcmp(tp + tp_length - 6, "XXXXXX", 6) == 0);
  int retries = 100;
  do {
    GenerateRandName(tp + tp_length - 6);
    if (mkdirat(root_fd, tp, 0700) == 0) {
      return tp;
    }
  } while (--retries && errno == EEXIST);

  memcpy(tp + tp_length - 6, "XXXXXX", 6);
  return nullptr;
}
}  // namespace

ScopedTempDirAt::ScopedTempDirAt(int root_fd) : ScopedTempDirAt(root_fd, ".") {}

ScopedTempDirAt::ScopedTempDirAt(int root_fd, fxl::StringView parent_path)
    : root_fd_(root_fd) {
  const std::string parent_path_str = parent_path.ToString();
  // MkdTempAt replaces "XXXXXX" so that the resulting directory path is unique.
  directory_path_ = parent_path_str + "/temp_dir_XXXXXX";
  if (!CreateDirectoryAt(root_fd_, parent_path_str) ||
      !MkdTempAt(root_fd, &directory_path_[0], directory_path_.size())) {
    directory_path_ = "";
  }
}

ScopedTempDirAt::~ScopedTempDirAt() {
  if (directory_path_.size()) {
    if (!DeletePathAt(root_fd_, directory_path_, true)) {
      FXL_LOG(WARNING) << "Unable to delete: " << directory_path_;
    }
  }
}

const std::string& ScopedTempDirAt::path() { return directory_path_; }

int ScopedTempDirAt::root_fd() { return root_fd_; }

bool ScopedTempDirAt::NewTempFile(std::string* output) {
  // MksTempAt replaces "XXXXXX" so that the resulting file path is unique.
  std::string file_path = directory_path_ + "/XXXXXX";
  fxl::UniqueFD fd = MksTempAt(root_fd_, &file_path[0], file_path.size());
  if (!fd.is_valid()) {
    return false;
  }
  output->swap(file_path);
  return true;
}

bool ScopedTempDirAt::NewTempFileWithData(const std::string& data,
                                          std::string* output) {
  if (!NewTempFile(output)) {
    return false;
  }
  return WriteFile(*output, data.data(), data.size());
}

ScopedTempDir::ScopedTempDir() : ScopedTempDir("") {}

ScopedTempDir::ScopedTempDir(fxl::StringView parent_path)
    : base_(AT_FDCWD, parent_path.empty() ? GetGlobalTmpDir() : parent_path) {}

ScopedTempDir::~ScopedTempDir() {}

const std::string& ScopedTempDir::path() { return base_.path(); }

bool ScopedTempDir::NewTempFile(std::string* output) {
  return base_.NewTempFile(output);
}

bool ScopedTempDir::NewTempFileWithData(const std::string& data,
                                        std::string* output) {
  return base_.NewTempFileWithData(data, output);
}
}  // namespace files
