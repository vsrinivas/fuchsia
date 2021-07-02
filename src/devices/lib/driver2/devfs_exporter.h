// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_DRIVER2_DEVFS_EXPORTER_H_
#define SRC_DEVICES_LIB_DRIVER2_DEVFS_EXPORTER_H_

#include <fuchsia/device/fs/llcpp/fidl.h>
#include <lib/fpromise/promise.h>

#include <fbl/ref_ptr.h>

#include "src/devices/lib/driver2/namespace.h"
#include "src/lib/storage/vfs/cpp/pseudo_dir.h"

namespace driver {

// Allows a driver to export a service to devfs.
class DevfsExporter {
 public:
  // Creates a devfs exporter, where `ns` will be used to connect to
  // fuchsia.device.fs.Exporter, and `svc_dir` will be used to find the services
  // to export.
  static zx::status<DevfsExporter> Create(const Namespace& ns, async_dispatcher_t* dispatcher,
                                          const fbl::RefPtr<fs::PseudoDir>& svc_dir);

  DevfsExporter() = default;

  // Exports `service_name` to `devfs_path`, with an optionally associated
  // `protocol_id`.
  fpromise::promise<void, zx_status_t> Export(std::string_view service_name,
                                              std::string_view devfs_path,
                                              uint32_t protocol_id = 0) const;

  // Exports `T` to `devfs_path`, with an optionally associated `protocol_id`.
  template <typename T>
  fpromise::promise<void, zx_status_t> Export(std::string_view devfs_path,
                                              uint32_t protocol_id = 0) const {
    return Export(fidl::DiscoverableProtocolName<T>, devfs_path, protocol_id);
  }

 private:
  DevfsExporter(fidl::Client<fuchsia_device_fs::Exporter> exporter,
                const fbl::RefPtr<fs::PseudoDir>& svc_dir);

  fidl::Client<fuchsia_device_fs::Exporter> exporter_;
  fbl::RefPtr<fs::PseudoDir> svc_dir_;
};

}  // namespace driver

#endif  // SRC_DEVICES_LIB_DRIVER2_DEVFS_EXPORTER_H_
