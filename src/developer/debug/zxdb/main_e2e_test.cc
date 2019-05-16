// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/main_e2e_test.h"

#include <stdio.h>

#include "gtest/gtest.h"

namespace zxdb {

std::string e2e_init_command;

}  // namespace zxdb

int main(int argc, char* argv[]) {
  testing::InitGoogleTest(&argc, argv);

  for (int i = 0; i < argc; i++) {
    if (std::string(argv[i]) != "--cmd") {
      continue;
    }

    i++;
    if (i < argc) {
      zxdb::e2e_init_command = argv[i];
    }

    break;
  }

  if (zxdb::e2e_init_command.empty()) {
    fprintf(stderr,
            "No initialization command specified.\n"
            "Use --cmd \"<command>\" to specify a zxdb connection command for "
            "the client we are testing on.\n");
    return 1;
  }

  return RUN_ALL_TESTS();
}
