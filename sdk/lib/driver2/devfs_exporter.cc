// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/driver2/devfs_exporter.h>

namespace fdfs = fuchsia_device_fs;

namespace driver {

zx::result<DevfsExporter> DevfsExporter::Create(
    const Namespace& ns, async_dispatcher_t* dispatcher,
    fidl::WireSharedClient<fuchsia_io::Directory> svc_dir) {
  auto result = ns.Connect<fdfs::Exporter>();
  if (result.is_error()) {
    return result.take_error();
  }
  fidl::WireSharedClient<fdfs::Exporter> client(std::move(*result), dispatcher);
  return zx::ok(DevfsExporter(std::move(client), std::move(svc_dir)));
}

DevfsExporter::DevfsExporter(fidl::WireSharedClient<fdfs::Exporter> exporter,
                             fidl::WireSharedClient<fuchsia_io::Directory> svc_dir)
    : exporter_(std::move(exporter)), svc_dir_(std::move(svc_dir)) {}

zx_status_t DevfsExporter::ExportSync(std::string_view service_path, std::string_view devfs_path,
                                      fuchsia_device_fs::wire::ExportOptions options,
                                      uint32_t protocol_id) const {
  // Get a connection to svc_dir.
  auto svc_endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
  if (svc_endpoints.is_error()) {
    return svc_endpoints.status_value();
  }

  auto result =
      svc_dir_->Clone(fuchsia_io::wire::OpenFlags::kCloneSameRights,
                      fidl::ServerEnd<fuchsia_io::Node>(svc_endpoints->server.TakeChannel()));
  if (!result.ok()) {
    return result.status();
  }

  auto response = exporter_.sync()->ExportOptions(
      std::move(svc_endpoints->client), fidl::StringView::FromExternal(service_path),
      fidl::StringView::FromExternal(devfs_path), protocol_id, options);
  if (!response.ok()) {
    return response.error().status();
  }
  if (!response->is_ok()) {
    return response->error_value();
  }
  return ZX_OK;
}

void DevfsExporter::Export(std::string_view service_path, std::string_view devfs_path,
                           fuchsia_device_fs::wire::ExportOptions options, uint32_t protocol_id,
                           fit::callback<void(zx_status_t)> callback) const {
  // Get a connection to svc_dir.
  auto svc_endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
  if (svc_endpoints.is_error()) {
    callback(svc_endpoints.status_value());
    return;
  }

  auto result =
      svc_dir_->Clone(fuchsia_io::wire::OpenFlags::kCloneSameRights,
                      fidl::ServerEnd<fuchsia_io::Node>(svc_endpoints->server.TakeChannel()));
  if (!result.ok()) {
    callback(result.status());
    return;
  }

  exporter_
      ->ExportOptions(std::move(svc_endpoints->client),
                      fidl::StringView::FromExternal(service_path),
                      fidl::StringView::FromExternal(devfs_path), protocol_id, options)
      .ThenExactlyOnce([callback = std::move(callback)](auto& result) mutable {
        if (result.status() != ZX_OK) {
          callback(result.status());
          return;
        }
        if (result->is_error()) {
          callback(result->error_value());
          return;
        }
        callback(ZX_OK);
      });
}

}  // namespace driver
