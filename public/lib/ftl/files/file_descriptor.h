// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FTL_FILES_FILE_DESCRIPTOR_H_
#define LIB_FTL_FILES_FILE_DESCRIPTOR_H_

#include "lib/ftl/inttypes.h"
#include "lib/ftl/portable_unistd.h"

namespace ftl {

bool WriteFileDescriptor(int fd, const char* data, ssize_t size);
ssize_t ReadFileDescriptor(int fd, char* data, ssize_t max_size);

}  // namespace ftl

#endif  // LIB_FTL_FILES_FILE_DESCRIPTOR_H_
