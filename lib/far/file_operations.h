// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_FAR_FILE_OPERATIONS_H_
#define GARNET_LIB_FAR_FILE_OPERATIONS_H_

#include <vector>

#include "lib/fxl/files/file_descriptor.h"

namespace archive {

template <typename T>
bool ReadObject(int fd, T* object) {
  char buffer[sizeof(T)];
  ssize_t actual = fxl::ReadFileDescriptor(fd, buffer, sizeof(T));
  if (actual < 0 || static_cast<size_t>(actual) != sizeof(T))
    return false;
  memcpy(object, buffer, sizeof(T));
  return true;
}

template <typename T>
bool WriteObject(int fd, const T& object) {
  char buffer[sizeof(T)];
  memcpy(buffer, &object, sizeof(T));
  return fxl::WriteFileDescriptor(fd, buffer, sizeof(T));
}

template <typename T>
bool ReadVector(int fd, std::vector<T>* vector) {
  size_t requested = vector->size() * sizeof(T);
  char* buffer = reinterpret_cast<char*>(vector->data());
  ssize_t actual = fxl::ReadFileDescriptor(fd, buffer, requested);
  return actual >= 0 && static_cast<size_t>(actual) == requested;
}

template <typename T>
bool WriteVector(int fd, const std::vector<T>& vector) {
  size_t requested = vector.size() * sizeof(T);
  const char* buffer = reinterpret_cast<const char*>(vector.data());
  return fxl::WriteFileDescriptor(fd, buffer, requested);
}

bool CopyPathToFile(const char* src_path, int dst_fd, uint64_t length);
bool CopyFileToPath(int src_fd, const char* dst_path, uint64_t length);
bool CopyFileToFile(int src_fd, int dst_fd, uint64_t length);

}  // namespace archive

#endif  // GARNET_LIB_FAR_FILE_OPERATIONS_H_
