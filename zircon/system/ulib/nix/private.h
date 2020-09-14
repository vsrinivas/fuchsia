// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_ULIB_NIX_PRIVATE_H_
#define ZIRCON_SYSTEM_ULIB_NIX_PRIVATE_H_

#include <cerrno>

// Set errno to e, return -1
static inline int ERRNO(int e) {
  errno = e;
  return -1;
}

#endif  // ZIRCON_SYSTEM_ULIB_NIX_PRIVATE_H_
