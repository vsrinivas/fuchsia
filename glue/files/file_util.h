// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GLUE_FILES_FILE_UTIL_H_
#define GLUE_FILES_FILE_UTIL_H_

#include <string>

namespace glue {

bool GetFileSize(const std::string& path, int64_t* size);

bool IsDirectory(const std::string& path);

bool CreateDirectory(const std::string& path);

bool DeletePath(const std::string& path, bool recursive);

}  // namespace glue

#endif  // GLUE_FILES_FILE_UTIL_H_
