// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_TESTING_NETEMUL_RUNNER_VIRTUAL_DEVICES_H_
#define SRC_CONNECTIVITY_NETWORK_TESTING_NETEMUL_RUNNER_VIRTUAL_DEVICES_H_

#include <fbl/ref_ptr.h>
#include <fs/pseudo-dir.h>
#include <fs/synchronous-vfs.h>
#include <fs/vnode.h>
#include <fuchsia/netemul/network/cpp/fidl.h>
#include <lib/fidl/cpp/interface_ptr.h>

#include <string>

namespace netemul {

class VirtualDevices {
 public:
  using DevProxy = fuchsia::netemul::network::DeviceProxy;
  VirtualDevices();

  void AddEntry(const std::string& path, fidl::InterfacePtr<DevProxy> dev);
  void RemoveEntry(const std::string& path);

  zx::channel OpenAsDirectory();

 private:
  fs::SynchronousVfs vdev_vfs_;
  fbl::RefPtr<fs::PseudoDir> dir_;
};

}  // namespace netemul

#endif  // SRC_CONNECTIVITY_NETWORK_TESTING_NETEMUL_RUNNER_VIRTUAL_DEVICES_H_
