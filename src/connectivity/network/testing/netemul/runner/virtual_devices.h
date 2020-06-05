// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_TESTING_NETEMUL_RUNNER_VIRTUAL_DEVICES_H_
#define SRC_CONNECTIVITY_NETWORK_TESTING_NETEMUL_RUNNER_VIRTUAL_DEVICES_H_

#include <fuchsia/netemul/network/cpp/fidl.h>
#include <lib/fidl/cpp/interface_ptr.h>

#include <string>

#include <fbl/ref_ptr.h>
#include <fs/pseudo_dir.h>
#include <fs/synchronous_vfs.h>
#include <fs/vnode.h>

namespace netemul {

class VirtualDevices {
 public:
  using DevProxy = fuchsia::netemul::network::DeviceProxy;
  VirtualDevices();

  void AddEntry(std::string path, fidl::InterfacePtr<DevProxy> dev);
  void RemoveEntry(std::string path);

  // Returns a channel serving the fuchsia.io.Directory protocol for a directory
  // at the relative path |path|.
  //
  // If the directory does not exist, it will be created.
  zx::channel OpenAsDirectory(std::string path);

 private:
  // Get the directory at the relative path specified by |parts|.
  //
  // If the directory does not exist, it will be created.
  fbl::RefPtr<fs::PseudoDir> GetDirectory(std::vector<std::string_view> parts);

  fs::SynchronousVfs vdev_vfs_;
  fbl::RefPtr<fs::PseudoDir> dir_;
};

}  // namespace netemul

#endif  // SRC_CONNECTIVITY_NETWORK_TESTING_NETEMUL_RUNNER_VIRTUAL_DEVICES_H_
