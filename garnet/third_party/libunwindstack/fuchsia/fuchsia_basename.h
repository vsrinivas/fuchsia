// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_THIRD_PARTY_LIBUNWINDSTACK_FUCHSIA_FUCHSIA_BASENAME_H_
#define GARNET_THIRD_PARTY_LIBUNWINDSTACK_FUCHSIA_FUCHSIA_BASENAME_H_

#include "src/lib/fxl/logging.h"

// Empty implementation of the POSIX libgen basename call. The Fuchsia
// port does not read files from the filesystem so does not need this call,
// although some code that is compiled does reference it.
//
// This can be implemented easily if we find we need it.
inline const char* basename(const char* path) {
  FXL_CHECK(false);
  return path;
}

#endif  // GARNET_THIRD_PARTY_LIBUNWINDSTACK_FUCHSIA_FUCHSIA_BASENAME_H_
