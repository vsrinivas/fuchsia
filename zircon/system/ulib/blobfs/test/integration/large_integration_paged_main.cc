// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "launch.h"

int main() {
  // This is a temporary solution until we can support tests as packages on the bringup target,
  // which the blobfs-large-integration-test runs on. There is no good alternative currently to run
  // blobfs-large-integration-test with the additional --pager commandline arg.
  //
  // TODO(ZX-48961):
  // We might not need this test at all in the future, when we have paging enabled by default on all
  // platforms. The tests will then have the pager enabled by default as well.
  const char* argv[] = {"/boot/test/fs/blobfs-large-integration-test", "--pager", nullptr};

  return Execute(argv);
}
