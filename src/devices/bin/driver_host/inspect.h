// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_HOST_INSPECT_H_
#define SRC_DEVICES_BIN_DRIVER_HOST_INSPECT_H_

#include <lib/inspect/cpp/inspect.h>

#include <fbl/ref_ptr.h>
#include <fs/pseudo_dir.h>
#include <fs/synchronous_vfs.h>

class DriverHostInspect {
 public:
  DriverHostInspect();

  inspect::Node& root_node() { return inspect_.GetRoot(); }
  fs::PseudoDir& diagnostics_dir() { return *diagnostics_dir_; }

  zx_status_t Serve(zx::channel remote, async_dispatcher_t* dispatcher);

 private:
  inspect::Inspector inspect_;
  zx::vmo inspect_vmo_;
  fbl::RefPtr<fs::PseudoDir> diagnostics_dir_;
  std::unique_ptr<fs::SynchronousVfs> diagnostics_vfs_;
};

#endif  // SRC_DEVICES_BIN_DRIVER_HOST_INSPECT_H_
