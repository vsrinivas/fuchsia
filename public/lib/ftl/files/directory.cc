// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/ftl/files/directory.h"

#include <limits.h>
#include <unistd.h>

#include "lib/ftl/logging.h"

namespace files {

std::string GetCurrentDirectory() {
  char buffer[PATH_MAX];
  FTL_CHECK(getcwd(buffer, sizeof(buffer)));
  return std::string(buffer);
}

}  // namespace files
