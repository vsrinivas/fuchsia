// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/shell/mirror/wire_format.h"

#include <dirent.h>
#include <lib/syslog/cpp/macros.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <cstddef>
#include <fstream>
#include <iterator>
#include <memory>

#include "src/developer/shell/mirror/common.h"

namespace shell::mirror {

int Files::AddFile(const std::filesystem::path &path, std::unique_ptr<char[]> &&contents,
                   long length) {
  for (const auto &file : files_) {
    if (file.Name() == path) {
      // already present.  Consider replacing?
      return 0;
    }
  }

  files_.emplace_back(path, std::move(contents), length);
  return 0;
}

int Files::AddFile(const std::filesystem::path &path) {
  FILE *f = fopen(path.c_str(), "rb");
  if (f == nullptr) {
    return -1;
  }
  int err = fseek(f, 0, SEEK_END);
  if (err == -1) {
    return err;
  }
  long file_size = ftell(f);
  err = fseek(f, 0, SEEK_SET);
  if (err == -1) {
    return err;
  }

  std::unique_ptr<char[]> contents = std::make_unique<char[]>(file_size + 1);
  fread(contents.get(), file_size, 1, f);
  fclose(f);

  contents[file_size] = 0;

  return AddFile(path, std::move(contents), file_size);
}

namespace {

template <class T>
void WriteFixedLength(std::vector<char> *sink, T size) {
  for (std::size_t i = 0; i < sizeof(T); i++) {
    char *size_ch = reinterpret_cast<char *>(&size) + i;
    sink->push_back(*size_ch);
  }
}

}  // namespace

// Dumps the files to a vector, and returns that vector.
// Note that this requires enough memory to hold all of the files.
int Files::DumpFiles(std::vector<char> *sink) {
  WriteFixedLength<uint64_t>(sink, files_.size());
  std::string root = root_dir_.string();

  for (const auto &file : files_) {
    // Calculate remote pathname (i.e., remove local root path prefix)
    std::string name = file.Name();
    if (name.find(root_dir_.string()) == 0) {
      name.erase(0, root.length());
      while (name[0] == std::filesystem::path::preferred_separator) {
        name.erase(0, 1);
      }
    }
    // Write path length
    uint64_t path_size = name.length();
    WriteFixedLength<uint64_t>(sink, path_size);
    sink->reserve(sink->size() + path_size);

    // Write path
    std::vector<char> name_vec(name.begin(), name.end());
    sink->insert(sink->end(), name.begin(), name.end());

    std::string_view data_view = file.View();
    size_t data_size = data_view.size();
    const char *data = data_view.data();
    WriteFixedLength<uint64_t>(sink, data_size);
    sink->reserve(sink->size() + data_size);
    sink->insert(sink->end(), data, data + data_size);
  }
  return 0;
}

namespace {

// Read like read(2), potentially selecting on the fd first, potentially with a timeout, and
// continuing to read until the requested number of bytes has been read.
Err DoRead(int fd, void *buf, size_t count, struct timeval *timeout) {
  struct stat statbuf;
  int statres = fstat(fd, &statbuf);
  // Fuchsia doesn't support select on regular files.
  if (statres == -1 || !S_ISREG(statbuf.st_mode)) {
    fd_set set;
    struct timeval tv;
    FD_ZERO(&set);
    FD_SET(fd, &set);
    tv.tv_sec = 5;
    tv.tv_usec = 0;
    if (timeout == nullptr) {
      timeout = &tv;
    }
    int retval = select(fd + 1, &set, nullptr, nullptr, timeout);
    if (retval == -1) {
      std::string error = "Error in select(): " + std::string(strerror(errno));
      return Err(kRead, error);
    }
    if (retval != 1) {
      std::string error = "Timed out waiting for reload server: " + std::to_string(retval);
      return Err(kRead, error);
    }
  }

  ssize_t ct;
  size_t to_be_counted = count;
  while (to_be_counted != 0) {
    if ((ct = read(fd, buf, to_be_counted)) == -1) {
      std::string error = "Error reading: " + std::string(strerror(errno));
      return Err(kRead, error);
    }
    to_be_counted -= ct;
  }
  return Err();
}

}  // namespace

#define READ_RET(fd, buf, count)                                          \
  do {                                                                    \
    void *vbuf = const_cast<void *>(reinterpret_cast<const void *>(buf)); \
    Err res = DoRead(fd, vbuf, count, timeout);                           \
    if (res.code != 0) {                                                  \
      *error = res;                                                       \
      return Files();                                                     \
    }                                                                     \
  } while (0)

Files Files::FilesFromFD(int fd, Err *error, struct timeval *timeout) {
  Files files;
  uint64_t num_files;
  READ_RET(fd, &num_files, sizeof(num_files));
  files.files_.reserve(num_files);
  for (size_t i = 0; i < num_files; i++) {
    uint64_t path_size;
    READ_RET(fd, &path_size, sizeof(path_size));
    std::unique_ptr<char[]> path_name(new char[path_size + 1]);
    READ_RET(fd, path_name.get(), path_size);
    path_name.get()[path_size] = '\0';

    uint64_t file_size;
    READ_RET(fd, &file_size, sizeof(file_size));
    std::unique_ptr<char[]> data(new char[file_size]);
    READ_RET(fd, data.get(), file_size);

    files.files_.emplace_back(path_name.get(), std::move(data), file_size);
  }

  return files;
}

}  // namespace shell::mirror
