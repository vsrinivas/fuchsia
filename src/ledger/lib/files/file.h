// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_LIB_FILES_FILE_H_
#define SRC_LEDGER_LIB_FILES_FILE_H_

#include <stdint.h>

#include <string>

namespace ledger {

// Writes the given data to the file at the given path. Returns true if the data was successfully
// written, otherwise returns false.
bool WriteFileAt(int dirfd, const std::string& path, const char* data, ssize_t size);

// Reads the contents of the file at the given path and stores the data in result. Returns true if
// the file was read successfully, otherwise returns false. If this function returns false, |result|
// will be the empty string.
bool ReadFileToStringAt(int dirfd, const std::string& path, std::string* result);

// Returns whether the given path is a file.
bool IsFileAt(int dirfd, const std::string& path);

// If the given path is a file, set size to the size of the file.
bool GetFileSizeAt(int dirfd, const std::string& path, uint64_t* size);

}  // namespace ledger

#endif  // SRC_LEDGER_LIB_FILES_FILE_H_
