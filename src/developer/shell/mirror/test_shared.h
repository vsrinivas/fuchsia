// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/memfs/memfs.h>

#include <string>

#ifndef SRC_DEVELOPER_SHELL_MIRROR_TEST_SHARED_H_
#define SRC_DEVELOPER_SHELL_MIRROR_TEST_SHARED_H_

namespace shell::mirror {

// For testing: manages a filesystem in memfs.
class FileRepo {
 public:
  FileRepo(const async_loop_config_t* config) : loop_(config) {}
  ~FileRepo();

  // Initializes a repo in memfs
  void InitMemRepo(std::string path);

  // Writes the pairs (filename, file contents) to the golden, relative to path as passed to
  // InitMemRepo.
  void WriteFiles(const std::vector<std::pair<std::string, std::string>>& golden);

 private:
  async::Loop loop_;
  std::string path_;
  memfs_filesystem_t* fs_;
};

}  // namespace shell::mirror

#endif  // SRC_DEVELOPER_SHELL_MIRROR_TEST_SHARED_H_
