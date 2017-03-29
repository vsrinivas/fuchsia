// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FTL_FILES_FILE_H_
#define LIB_FTL_FILES_FILE_H_

#include <string>
#include <vector>

#include "lib/ftl/inttypes.h"

namespace files {

// Writes the given data to the file at the given path. Return true if the data
// was successfully written, otherwise returns false.
bool WriteFile(const std::string& path, const char* data, ssize_t size);

// Reads the contents of the file at the given path and stores the data in
// result. Returns true if the file was read successfully, otherwise returns
// false. If this function returns false, |result| will be the empty string.
bool ReadFileToString(const std::string& path, std::string* result);

// Reads the contents of the file at the given path and stores the data in
// result. Returns true if the file was read successfully, otherwise returns
// false. If this function returns false, |result| will be the empty string.
bool ReadFileToVector(const std::string& path, std::vector<uint8_t>* result);

// Returns whether the given path is a file.
bool IsFile(const std::string& path);

// If the given path is a file, set size to the size of the file.
bool GetFileSize(const std::string& path, uint64_t* size);

}  // namespace files

#endif  // LIB_FTL_FILES_FILE_H_
