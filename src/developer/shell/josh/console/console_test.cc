// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/shell/josh/console/console.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/memfs/memfs.h>
#include <stdlib.h>

#include <array>
#include <fstream>
#include <string>

#include <gtest/gtest.h>

namespace shell {

class ConsoleTest : public ::testing::Test {
 public:
  // Generate a random file. The last six characters of the name template must be XXXXXX
  std::string GetRandomFile(const char *name_template) {
    std::unique_ptr<char[]> buffer(new char[strlen(name_template) + 1]);
    strcpy(buffer.get(), name_template);
    mkstemp(buffer.get());
    return buffer.get();
  }

 protected:
  void SetUp() override {
    loop_ = new async::Loop(&kAsyncLoopConfigNoAttachToCurrentThread);
    ASSERT_EQ(loop_->StartThread(), ZX_OK);
    ASSERT_EQ(ZX_OK, memfs_install_at(loop_->dispatcher(), "/test_tmp", &fs_));

    // Make sure file creation is OK so memfs is running OK.
    char tmpfs_test_file[] = "/test_tmp/write.test.XXXXXX";
    ASSERT_NE(mkstemp(tmpfs_test_file), -1);
  }

  void TearDown() override {
    // Synchronously clean up.
    sync_completion_t unmounted;
    memfs_free_filesystem(fs_, &unmounted);
    sync_completion_wait(&unmounted, zx::duration::infinite().get());
    fs_ = nullptr;

    loop_->Shutdown();
    delete loop_;
    loop_ = nullptr;
  }

  async::Loop *loop_;
  memfs_filesystem_t *fs_;
};

// Sanity check test to make sure Hello World works.
TEST_F(ConsoleTest, Sanity) {
  std::string filename = GetRandomFile("/test_tmp/tmp.XXXXXX");

  // Generate the js command to run.
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

// Sanity check test to make sure Hello World script works.
TEST_F(ConsoleTest, ScriptSanity) {
  std::string random_filename = GetRandomFile("/test_tmp/tmp.XXXXXX");
  std::string random_script_name = GetRandomFile("/test_tmp/script.js.XXXXXX");

  // Write js into the script file
  std::string expected = "Hello World";
  std::ofstream test_script;
  test_script.open(random_script_name);
  test_script << "file = std.open('" + random_filename + "', 'rw+');\n";
  test_script << "file.puts('" + expected + "');";
  test_script << "file.flush();";
  test_script.close();

  constexpr int kNumArgs = 7;
  std::array<const char *, kNumArgs> argv = {
      "test_program",      "-j", "/pkg/data/lib/",          "-f",
      "/pkg/data/fidling", "-r", random_script_name.c_str()};
  ASSERT_EQ(0, shell::ConsoleMain(kNumArgs, argv.data()));

  std::ifstream in(random_filename.c_str());
  std::string actual((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  ASSERT_STREQ(expected.c_str(), actual.c_str());
}

}  // namespace shell
