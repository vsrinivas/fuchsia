// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FXL_FILES_FILE_H_
#define LIB_FXL_FILES_FILE_H_

#include <stdint.h>

#include <string>
#include <vector>

#include "src/lib/fxl/strings/string_view.h"

namespace files {

// Writes the given data to the file at the given path. Returns true if the data
// was successfully written, otherwise returns false.
bool WriteFile(const std::string& path, const char* data, ssize_t size);
bool WriteFileAt(int dirfd, const std::string& path, const char* data, ssize_t size);

// Writes the given data a temporary file under |temp_root| and then moves the
// temporary file to |path|, ensuring write atomicity. Returns true if the data
// was successfully written, otherwise returns false.
//
// Note that |path| and |temp_root| must be within the same filesystem for the
// move to work. For example, it will not work to use |path| under /data and
// |temp_root| under /tmp.
bool WriteFileInTwoPhases(const std::string& path, fxl::StringView data,
                          const std::string& temp_root);

// Reads the contents of the file at the given path or file descriptor and
// stores the data in result. Returns true if the file was read successfully,
// otherwise returns false. If this function returns false, |result| will be
// the empty string.
bool ReadFileToString(const std::string& path, std::string* result);
bool ReadFileDescriptorToString(int fd, std::string* result);
bool ReadFileToStringAt(int dirfd, const std::string& path, std::string* result);

// Reads the contents of the file at the given path or file descriptor and stores the data in
// result. Returns true if the file was read successfully, otherwise returns
// false. If this function returns false, |result| will be the empty string.
bool ReadFileToVector(const std::string& path, std::vector<uint8_t>* result);
bool ReadFileDescriptorToVector(int fd, std::vector<uint8_t>* result);

// Reads the contents of the file at the given path and if successful, returns
// pair of read allocated bytes with data and size of the data if successful.
// pair of <nullptr, -1> if read failed.
std::pair<uint8_t*, intptr_t> ReadFileToBytes(const std::string& path);
std::pair<uint8_t*, intptr_t> ReadFileDescriptorToBytes(int fd);

// Returns whether the given path is a file.
bool IsFile(const std::string& path);
bool IsFileAt(int dirfd, const std::string& path);

// If the given path is a file, set size to the size of the file.
bool GetFileSize(const std::string& path, uint64_t* size);
bool GetFileSizeAt(int dirfd, const std::string& path, uint64_t* size);

}  // namespace files

#endif  // LIB_FXL_FILES_FILE_H_
