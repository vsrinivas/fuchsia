// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_KAZOO_READ_FILE_TO_STRING_H_
#define TOOLS_KAZOO_READ_FILE_TO_STRING_H_

#include <string>

bool ReadFileToString(const std::string& path, std::string* result);

#endif  // TOOLS_KAZOO_READ_FILE_TO_STRING_H_
