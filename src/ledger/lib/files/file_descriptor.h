// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_LIB_FILES_FILE_DESCRIPTOR_H_
#define SRC_LEDGER_LIB_FILES_FILE_DESCRIPTOR_H_

#include <stdint.h>
#include <unistd.h>

namespace ledger {

bool WriteFileDescriptor(int fd, const char* data, ssize_t size);
ssize_t ReadFileDescriptor(int fd, char* data, ssize_t max_size);

}  // namespace ledger

#endif  // SRC_LEDGER_LIB_FILES_FILE_DESCRIPTOR_H_
