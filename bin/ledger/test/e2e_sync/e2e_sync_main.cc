// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/test/app_test.h"
#include "peridot/bin/ledger/test/integration/sync/lib.h"

int main(int argc, char** argv) {
  if (!test::integration::sync::ProcessCommandLine(argc, argv)) {
    return -1;
  }
  return test::TestMain(argc, argv);
}
