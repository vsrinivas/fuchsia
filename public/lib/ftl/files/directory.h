// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FTL_FILES_DIRECTORY_H_
#define LIB_FTL_FILES_DIRECTORY_H_

#include <string>

namespace files {

// Returns the current directory. If the current directory cannot be determined,
// this function will terminate the process.
std::string GetCurrentDirectory();

}  // namespace files

#endif  // LIB_FTL_FILES_DIRECTORY_H_
