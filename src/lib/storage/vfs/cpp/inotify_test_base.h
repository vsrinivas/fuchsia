// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_STORAGE_VFS_CPP_INOTIFY_TEST_BASE_H_
#define SRC_LIB_STORAGE_VFS_CPP_INOTIFY_TEST_BASE_H_

#include <fuchsia/io/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/inotify.h>
#include <lib/fdio/namespace.h>
#include <lib/fpromise/single_threaded_executor.h>
#include <lib/memfs/memfs.h>
#include <sys/stat.h>

#include <fbl/unique_fd.h>
#include <zxtest/zxtest.h>

#include "src/lib/fxl/strings/substitute.h"
#include "src/lib/storage/vfs/cpp/remote_dir.h"

namespace fs {
class InotifyTest : public zxtest::Test {
 public:
  InotifyTest() : memfs_loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {}
  void SetUp() override;
  void TearDown() override;

 protected:
  fbl::RefPtr<fs::RemoteDir> GetRemoteDir();
  void AddFile(const std::string& path, size_t content_size);
  void WriteToFile(const std::string& path, size_t content_size);
  void TruncateFile(const std::string& path, size_t new_file_size);
  void MakeDir(const std::string& path);

  static constexpr char kTmpfsPath[] = "/fshost-inotify-tmp";
  fdio_ns_t* namespace_;
  async::Loop memfs_loop_;
  memfs_filesystem_t* memfs_;
};
}  // namespace fs

#endif  // SRC_LIB_STORAGE_VFS_CPP_INOTIFY_TEST_BASE_H_
