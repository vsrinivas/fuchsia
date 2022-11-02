// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_DRIVER_DEVFS_CPP_EXPORTER_H_
#define LIB_DRIVER_DEVFS_CPP_EXPORTER_H_

#include <fidl/fuchsia.device.fs/cpp/wire.h>
#include <lib/driver/component/cpp/namespace.h>
#include <lib/sys/component/cpp/service_client.h>

namespace driver {

// Allows a driver to export a service to devfs.
class DevfsExporter {
 public:
  // Creates a devfs exporter, where `ns` will be used to connect to
  // fuchsia.device.fs.Exporter, and `svc_dir` will be used to find the services
  // to export.
  static zx::result<DevfsExporter> Create(const Namespace& ns, async_dispatcher_t* dispatcher,
                                          fidl::WireSharedClient<fuchsia_io::Directory> svc_dir);

  DevfsExporter() = default;

  // Exports `service_path` to `devfs_path`, with an optionally associated export options and
  // `protocol_id`.
  zx_status_t ExportSync(std::string_view service_path, std::string_view devfs_path,
                         fuchsia_device_fs::wire::ExportOptions options,
                         uint32_t protocol_id = 0) const;

  // Exports `service_path` to `devfs_path`, with associated export options and
  // `protocol_id`.
  void Export(std::string_view service_path, std::string_view devfs_path,
              fuchsia_device_fs::wire::ExportOptions options, uint32_t protocol_id,
              fit::callback<void(zx_status_t)> callback) const;

  // Exports `T` to `devfs_path`, with an associated `options` and `protocol_id`.
  template <typename T>
  void Export(std::string_view devfs_path, fuchsia_device_fs::ExportOptions options,
              uint32_t protocol_id, fit::callback<void(zx_status_t)> callback) const {
    return Export(fidl::DiscoverableProtocolName<T>, devfs_path, options, protocol_id,
                  std::move(callback));
  }

  // Exports `ServiceMember` to `devfs_path`, with an associated `options` and `protocol_id`.
  template <typename ServiceMember>
  void ExportService(std::string_view devfs_path, fuchsia_device_fs::ExportOptions options,
                     uint32_t protocol_id, fit::callback<void(zx_status_t)> callback,
                     std::string_view instance = component::kDefaultInstance) const {
    auto service_path = component::MakeServiceMemberPath<ServiceMember>(instance);
    return Export(service_path, devfs_path, options, protocol_id, std::move(callback));
  }

  const fidl::WireSharedClient<fuchsia_device_fs::Exporter>& exporter() const { return exporter_; }

 private:
  DevfsExporter(fidl::WireSharedClient<fuchsia_device_fs::Exporter> exporter,
                fidl::WireSharedClient<fuchsia_io::Directory> svc_dir);

  fidl::WireSharedClient<fuchsia_device_fs::Exporter> exporter_;
  fidl::WireSharedClient<fuchsia_io::Directory> svc_dir_;
};

}  // namespace driver

#endif  // LIB_DRIVER_DEVFS_CPP_EXPORTER_H_
