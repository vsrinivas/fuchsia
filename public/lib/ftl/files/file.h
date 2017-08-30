// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FTL_FILES_FILE_H_
#define LIB_FTL_FILES_FILE_H_

#include <string>
#include <vector>

#include "lib/ftl/ftl_export.h"
#include "lib/ftl/inttypes.h"
#include "lib/ftl/strings/string_view.h"

namespace files {

// Writes the given data to the file at the given path. Returns true if the data
// was successfully written, otherwise returns false.
FTL_EXPORT bool WriteFile(const std::string& path,
                          const char* data,
                          ssize_t size);

// Writes the given data a temporary file under |temp_root| and then moves the
// temporary file to |path|, ensuring write atomicity. Returns true if the data
// was successfully written, otherwise returns false.
//
// Note that |path| and |temp_root| must be within the same filesystem for the
// move to work. For example, it will not work to use |path| under /data and
// |temp_root| under /tmp.
FTL_EXPORT bool WriteFileInTwoPhases(const std::string& path,
                                     ftl::StringView data,
                                     const std::string& temp_root);

// Reads the contents of the file at the given path and stores the data in
// result. Returns true if the file was read successfully, otherwise returns
// false. If this function returns false, |result| will be the empty string.
FTL_EXPORT bool ReadFileToString(const std::string& path, std::string* result);

// Reads the contents of the file at the given path and stores the data in
// result. Returns true if the file was read successfully, otherwise returns
// false. If this function returns false, |result| will be the empty string.
FTL_EXPORT bool ReadFileToVector(const std::string& path,
                                 std::vector<uint8_t>* result);

// Returns whether the given path is a file.
FTL_EXPORT bool IsFile(const std::string& path);

// If the given path is a file, set size to the size of the file.
FTL_EXPORT bool GetFileSize(const std::string& path, uint64_t* size);

}  // namespace files

#endif  // LIB_FTL_FILES_FILE_H_
