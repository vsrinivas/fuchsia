// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/procfs/cpp/internal/environ.h>
#include <lib/procfs/cpp/process.h>

namespace procfs {

std::unique_ptr<vfs::PseudoDir> CreateProcessDir() {
  auto dir = std::make_unique<vfs::PseudoDir>();
  dir->AddEntry("environ", procfs::internal::CreateEnvironFile());
  return dir;
}

}  // namespace procfs
