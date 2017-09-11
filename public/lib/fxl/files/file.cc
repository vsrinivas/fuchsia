// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fxl/files/file.h"

#include <fcntl.h>
#include <limits.h>
#include <sys/stat.h>

#if defined(OS_WIN)
#define FILE_CREATE_MODE _S_IREAD | _S_IWRITE
#else
#define FILE_CREATE_MODE 0666
#endif

#include "lib/fxl/files/eintr_wrapper.h"
#include "lib/fxl/files/file_descriptor.h"
#include "lib/fxl/files/scoped_temp_dir.h"
#include "lib/fxl/files/unique_fd.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/portable_unistd.h"

namespace files {
namespace {

template <typename T>
bool ReadFile(const std::string& path, T* result) {
  FXL_DCHECK(result);
  result->clear();

  fxl::UniqueFD fd(open(path.c_str(), O_RDONLY));
  if (!fd.is_valid())
    return false;

  constexpr size_t kBufferSize = 1 << 16;
  size_t offset = 0;
  ssize_t bytes_read = 0;
  do {
    offset += bytes_read;
    result->resize(offset + kBufferSize);
    bytes_read = HANDLE_EINTR(read(fd.get(), &(*result)[offset], kBufferSize));
  } while (bytes_read > 0);

  if (bytes_read < 0) {
    result->clear();
    return false;
  }

  result->resize(offset + bytes_read);
  return true;
}

}  // namespace

bool WriteFile(const std::string& path, const char* data, ssize_t size) {
  fxl::UniqueFD fd(HANDLE_EINTR(creat(path.c_str(), FILE_CREATE_MODE)));
  if (!fd.is_valid())
    return false;
  return fxl::WriteFileDescriptor(fd.get(), data, size);
}

bool WriteFileInTwoPhases(const std::string& path,
                          fxl::StringView data,
                          const std::string& temp_root) {
  ScopedTempDir temp_dir(temp_root);

  std::string temp_file_path;
  if (!temp_dir.NewTempFile(&temp_file_path)) {
    return false;
  }

  if (!WriteFile(temp_file_path, data.data(), data.size())) {
    return false;
  }

  if (rename(temp_file_path.c_str(), path.c_str()) != 0) {
    return false;
  }

  return true;
}

bool ReadFileToString(const std::string& path, std::string* result) {
  return ReadFile(path, result);
}

bool ReadFileToVector(const std::string& path, std::vector<uint8_t>* result) {
  return ReadFile(path, result);
}

bool IsFile(const std::string& path) {
  struct stat buf;
  if (stat(path.c_str(), &buf) != 0)
    return false;
  return S_ISREG(buf.st_mode);
}

bool GetFileSize(const std::string& path, uint64_t* size) {
  struct stat stat_buffer;
  if (stat(path.c_str(), &stat_buffer) != 0)
    return false;
  *size = stat_buffer.st_size;
  return true;
}

}  // namespace files
