// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_MINFS_SPACE_DUMPER_H_
#define SRC_STORAGE_MINFS_SPACE_DUMPER_H_

namespace minfs {

class Minfs;

class SpaceDumper {
 public:
  // The Minfs instance must register itself as the global minfs so we can dump its state.
  static void SetMinfs(Minfs* minfs);
  static void ClearMinfs();

  static void DumpFilesystem();

 private:
  static Minfs* minfs_;
};

}  // namespace minfs

#endif  // SRC_STORAGE_MINFS_SPACE_DUMPER_H_
