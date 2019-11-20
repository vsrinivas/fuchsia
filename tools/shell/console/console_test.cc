// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/shell/console/console.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/memfs/memfs.h>
#include <stdlib.h>

#include <array>
#include <fstream>
#include <string>

#include "gtest/gtest.h"

// Sanity check test to make sure Hello World works.
TEST(Console, Sanity) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_EQ(loop.StartThread(), ZX_OK);
  ASSERT_EQ(ZX_OK, memfs_install_at(loop.dispatcher(), "/test_tmp"));

  constexpr const char* name = "/test_tmp/tmp.XXXXXX";
  std::unique_ptr<char[]> buffer(new char[strlen(name) + 1]);
  strcpy(buffer.get(), name);
  int cfd = mkstemp(buffer.get());
  ASSERT_NE(cfd, -1);
  std::string filename = buffer.get();

  std::string command;
  std::string expected = "Hello World";
  command += "file = std.open('" + filename + "', 'rw+');";
  command += "file.puts('" + expected + "');";
  command += "file.flush();";
  constexpr int kNumArgs = 7;
  std::array<const char*, kNumArgs> argv = {"test_program",      "-j", "/pkg/data/lib/", "-f",
                                            "/pkg/data/fidling", "-c", command.c_str()};
  ASSERT_EQ(0, shell::ConsoleMain(kNumArgs, argv.data()));

  std::ifstream in(filename.c_str());
  std::string actual((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  ASSERT_STREQ(expected.c_str(), actual.c_str());
}
