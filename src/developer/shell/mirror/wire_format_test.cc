// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/shell/mirror/wire_format.h"

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <atomic>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "gtest/gtest.h"
#include "src/developer/shell/mirror/common.h"
#include "src/developer/shell/mirror/test_shared.h"

namespace shell::mirror {

namespace {

class FilesTest : public ::testing::Test {
 public:
  FilesTest() = default;

 protected:
  std::unique_ptr<Files> file_files_;
};

TEST_F(FilesTest, BasicSerialization) {
  // Spin up some temp storage
  const std::string kRootDir = "/usr";
  const std::string kDataDir = "/wire_format_test_tmp";
  FileRepo repo(&kAsyncLoopConfigNoAttachToCurrentThread);
  repo.InitMemRepo(kDataDir);

  // Generate some fake files.
  std::vector<std::pair<std::string, std::string>> golden = {
      {kRootDir + "/z.txt", ""},
      {kRootDir + "/a.txt", "Once upon a midnight dreary, while I pondered, weak and weary,"},
      {kRootDir + "/b.txt", "Over many a quaint and curious volume of forgotten lore"},
      {kRootDir + "/c.txt", "While I nodded, nearly napping, suddenly there came a tapping,"},
      {kRootDir + "/d.txt", "As of some one gently rapping, rapping at my chamber door."}};

  // Try serializing 0..5 files
  for (size_t num_files = 0; num_files <= golden.size(); num_files++) {
    Files golden_files(kRootDir);
    for (size_t i = 0; i < num_files; i++) {
      char* dup = new char[golden[i].second.length() + 1];
      strcpy(dup, golden[i].second.c_str());
      ASSERT_EQ(0,
                golden_files.AddFile(golden[i].first, std::unique_ptr<char[]>(dup), strlen(dup)));
    }
    std::vector<char> dumped_files;

    // Write files to temp storage.
    ASSERT_EQ(0, golden_files.DumpFiles(&dumped_files));
    const std::string kSimpleFile = kDataDir + "/simple" + std::to_string(num_files);
    std::ofstream fout(kSimpleFile, std::ios::out | std::ios::binary);
    fout.write(&dumped_files[0], dumped_files.size());
    fout.close();

    // Read the files from temp storage
    int infd = open(kSimpleFile.c_str(), O_RDONLY);
    ASSERT_NE(-1, infd);
    shell::mirror::Err error;
    Files out_files = Files::FilesFromFD(infd, &error);
    ASSERT_EQ(0, error.code) << error.msg;
    const auto& actual_files = out_files.GetFiles();

    // Make sure what we read is the same as what we wrote.
    ASSERT_EQ(actual_files.size(), num_files);
    for (size_t i = 0; i < actual_files.size(); i++) {
      bool found = false;
      for (size_t j = 0; j < golden.size(); j++) {
        if (golden[j].first == (kRootDir + "/" + actual_files[i].Name())) {
          found = true;
          ASSERT_EQ(golden[j].second, actual_files[i].View());
        }
      }
      ASSERT_TRUE(found) << actual_files[i].Name() << " not found";
    }
  }
}

}  // namespace
}  // namespace shell::mirror
