// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_MANAGER_DEVFS_EXPORTER_H_
#define SRC_DEVICES_BIN_DRIVER_MANAGER_DEVFS_EXPORTER_H_

#include <fuchsia/device/fs/llcpp/fidl.h>

#include "src/devices/bin/driver_manager/devfs.h"
#include "src/lib/storage/vfs/cpp/pseudo_dir.h"

namespace driver_manager {

class DevfsExporter : public fidl::WireServer<fuchsia_device_fs::Exporter> {
 public:
  // The `root` Devnode must outlive `this`.
  DevfsExporter(Devnode* root, async_dispatcher_t* dispatcher);

  zx::status<> PublishExporter(const fbl::RefPtr<fs::PseudoDir>& svc_dir);

 private:
  // fidl::WireServer<fuchsia_device_fs::Exporter>
  void Export(ExportRequestView request, ExportCompleter::Sync& completer) override;

  Devnode* const root_;
  async_dispatcher_t* const dispatcher_;

  std::vector<std::unique_ptr<Devnode>> devnodes_;
};

}  // namespace driver_manager

#endif  // SRC_DEVICES_BIN_DRIVER_MANAGER_DEVFS_EXPORTER_H_
