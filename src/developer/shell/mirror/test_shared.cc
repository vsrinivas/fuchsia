// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/shell/mirror/test_shared.h"

#include <fstream>

#include "gtest/gtest.h"
#include "src/developer/shell/mirror/wire_format.h"

namespace shell::mirror {

void FileRepo::InitMemRepo(std::string path) {
  path_ = path;
  ASSERT_EQ(loop_.StartThread(), ZX_OK);
  ASSERT_EQ(ZX_OK, memfs_install_at(loop_.dispatcher(), path.c_str(), &fs_));
}

FileRepo::~FileRepo() {
  loop_.Shutdown();
  memfs_uninstall_unsafe(fs_, path_.c_str());
}

void FileRepo::WriteFiles(const std::vector<std::pair<std::string, std::string>>& golden) {
  for (const auto& gold : golden) {
    std::ofstream fout(gold.first, std::ios::out | std::ios::binary);
    fout << gold.second;
    fout.close();
  }
}

}  // namespace shell::mirror
