// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_TESTING_NETEMUL_RUNNER_VIRTUAL_DATA_H_
#define SRC_CONNECTIVITY_NETWORK_TESTING_NETEMUL_RUNNER_VIRTUAL_DATA_H_

#include <fbl/ref_ptr.h>
#include <fs/synchronous-vfs.h>
#include <fs/vnode.h>
#include <lib/memfs/cpp/vnode.h>

namespace netemul {

class VirtualData {
 public:
  VirtualData();
  ~VirtualData();
  zx::channel GetDirectory();

 private:
  std::unique_ptr<memfs::Vfs> vfs_;
  fbl::RefPtr<memfs::VnodeDir> dir_;
};

}  // namespace netemul

#endif  // SRC_CONNECTIVITY_NETWORK_TESTING_NETEMUL_RUNNER_VIRTUAL_DATA_H_
