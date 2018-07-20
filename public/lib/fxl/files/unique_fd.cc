// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fxl/files/unique_fd.h"

#include <unistd.h>

namespace fxl {
namespace internal {

void UniqueFDTraits::Free(int fd) { close(fd); }

}  // namespace internal
}  // namespace fxl
