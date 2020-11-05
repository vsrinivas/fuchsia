// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/procfs/cpp/process.h>

#include <lib/procfs/cpp/internal/cwd.h>
#include <lib/procfs/cpp/internal/environ.h>

namespace procfs {

std::unique_ptr<vfs::PseudoDir> CreateProcessDir(async_dispatcher_t* dispatcher) {
  auto dir = std::make_unique<vfs::PseudoDir>();
  dir->AddEntry("environ", procfs::internal::CreateEnviron());
  dir->AddEntry("cwd", procfs::internal::CreateCwd());
  return dir;
}

}  // namespace procfs
