// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_MANAGER_INSPECT_H_
#define SRC_DEVICES_BIN_DRIVER_MANAGER_INSPECT_H_

#include <lib/inspect/cpp/inspect.h>
#include <lib/zx/channel.h>

#include <fbl/ref_ptr.h>
#include <fs/pseudo_dir.h>
#include <fs/synchronous_vfs.h>

class InspectManager {
 public:
  explicit InspectManager(async_dispatcher_t* dispatcher);

  InspectManager() = delete;

  zx::unowned_channel diagnostics_channel() { return zx::unowned_channel(diagnostics_client_); }

  fs::PseudoDir& diagnostics_dir() { return *diagnostics_dir_; }

  fbl::RefPtr<fs::PseudoDir> driver_host_dir() { return driver_host_dir_; }

  inspect::Node& root_node() { return inspect_.GetRoot(); }

 private:
  inspect::Inspector inspect_;
  zx::vmo inspect_vmo_;

  fbl::RefPtr<fs::PseudoDir> diagnostics_dir_;
  std::unique_ptr<fs::SynchronousVfs> diagnostics_vfs_;
  fbl::RefPtr<fs::PseudoDir> driver_host_dir_;

  zx::channel diagnostics_client_;
};

#endif  // SRC_DEVICES_BIN_DRIVER_MANAGER_INSPECT_H_
