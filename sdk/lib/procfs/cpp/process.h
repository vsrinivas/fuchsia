// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_PROCFS_CPP_PROCESS_H_
#define LIB_PROCFS_CPP_PROCESS_H_

#include <lib/vfs/cpp/pseudo_dir.h>

#include <memory>

namespace procfs {

std::unique_ptr<vfs::PseudoDir> CreateProcessDir();

}  // namespace procfs

#endif  // LIB_PROCFS_CPP_PROCESS_H_
