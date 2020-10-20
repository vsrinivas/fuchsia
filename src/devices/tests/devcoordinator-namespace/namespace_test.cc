// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <lib/fdio/directory.h>

#include <string>
#include <vector>

#include <fbl/string_printf.h>
#include <zxtest/zxtest.h>

std::vector<std::string> list_dir_contents(const char* name) {
  std::vector<std::string> res;

  DIR* dir;
  struct dirent* ent;
  if ((dir = opendir(name)) != NULL) {
    /* print all the files and directories within directory */
    while ((ent = readdir(dir)) != NULL) {
      res.push_back(std::string(ent->d_name));
    }
    closedir(dir);
  }

  return res;
}

TEST(NamespaceTest, Test) {
  // For each directory in the root directory, let's make sure that it actually
  // goes somewhere. We're testing that the handle has something responding at
  // the other side, not that it goes somewhere valid, so it's fine if we get an
  // error while using it.
  auto top_level_dir_names = list_dir_contents("/");
  for (const auto& name : top_level_dir_names) {
    fbl::String sub_dir_name = fbl::StringPrintf("/%s", name.c_str());
    // /system-delayed will never respond on bringup.
    if (sub_dir_name.compare("/system-delayed") == 0) {
      continue;
    }
    auto dir_contents = list_dir_contents(sub_dir_name.c_str());
  }
}
