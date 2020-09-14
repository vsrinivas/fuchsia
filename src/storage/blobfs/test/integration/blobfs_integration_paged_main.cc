// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <vector>

#include "launch.h"

int main(int argc, const char* argv[]) {
  // This is a temporary solution until we can support tests as packages on the bringup target,
  // which the blobfs-integration-test runs on. There is no good alternative currently to run
  // blobfs-integration-test with the additional --pager commandline arg.
  //
  // TODO(ZX-48961):
  // We might not need this test at all in the future, when we have paging enabled by default on all
  // platforms. The tests will then have the pager enabled by default as well.

  std::vector<const char*> args = {"/pkg/test/blobfs-integration-test", "--pager"};
  for (int i = 1; i < argc; ++i) {
    args.push_back(argv[i]);
  }
  args.push_back(nullptr);

  return Execute(args.data());
}
