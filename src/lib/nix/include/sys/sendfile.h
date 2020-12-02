// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_NIX_INCLUDE_SYS_SENDFILE_H_
#define SRC_LIB_NIX_INCLUDE_SYS_SENDFILE_H_

#include <unistd.h>

#ifdef __cplusplus
extern "C" {
#endif

ssize_t sendfile(int out_fd, int in_fd, off_t* offset, size_t count);

#ifdef __cplusplus
}
#endif

#endif // SRC_LIB_NIX_INCLUDE_SYS_SENDFILE_H_
