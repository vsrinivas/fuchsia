// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_TESTING_SCOPED_MEMFS_MANAGER_H_
#define SRC_DEVELOPER_FORENSICS_TESTING_SCOPED_MEMFS_MANAGER_H_

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/memfs/memfs.h>
#include <lib/sync/completion.h>
#include <lib/syslog/cpp/macros.h>

#include <map>
#include <string>
#include <vector>

namespace forensics::testing {

// Manages creating and destroying MemFs backed directories in the calling processes namespace.
class ScopedMemFsManager {
 public:
  ScopedMemFsManager() : loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {}

  ~ScopedMemFsManager() {
    std::vector<std::string> paths;
    paths.reserve(filesystems_.size());

    // Store the paths because Destroy mutates |filesystems_|.
    for (const auto& [path, _] : filesystems_) {
      paths.push_back(path);
    }

    for (const auto& path : paths) {
      Destroy(path);
    }

    loop_.Shutdown();
  }

  bool Contains(const std::string& path) const { return filesystems_.count(path) != 0; }

  // Create a memfs backed directory at |path| in the component's namespace.
  void Create(const std::string& path) {
    FX_CHECK(!Contains(path));
    if (!started_) {
      FX_CHECK(loop_.StartThread("forensics-scoped-memfs-manager") == ZX_OK);
      started_ = true;
    }
    FX_CHECK(memfs_install_at(loop_.dispatcher(), path.c_str(), &filesystems_[path]) == ZX_OK);
  }

  // Destroy the memfs backed directory at |path| in the component's namespace.
  void Destroy(const std::string& path) {
    FX_CHECK(Contains(path));
    sync_completion_t wait;
    memfs_free_filesystem(filesystems_[path], &wait);
    FX_CHECK(sync_completion_wait(&wait, ZX_TIME_INFINITE) == 0);
    filesystems_.erase(path);
  }

 private:
  std::map<std::string, memfs_filesystem_t*> filesystems_;
  async::Loop loop_;
  bool started_{false};
};

}  // namespace forensics::testing

#endif  // SRC_DEVELOPER_FORENSICS_TESTING_SCOPED_MEMFS_MANAGER_H_
