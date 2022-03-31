// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/lib/driver2/devfs_exporter.h"

#include <lib/fpromise/bridge.h>

namespace fdfs = fuchsia_device_fs;

namespace driver {

namespace {

fpromise::promise<void, zx_status_t> CheckFileExists(
    const fidl::WireSharedClient<fuchsia_io::Directory>& dir, std::string_view path,
    async_dispatcher_t* dispatcher) {
  auto svc_endpoints = fidl::CreateEndpoints<fuchsia_io::Node>();
  if (svc_endpoints.is_error()) {
    return fpromise::make_error_promise(svc_endpoints.status_value());
  }
  auto result = dir->Open(fuchsia_io::wire::kOpenFlagNodeReference, 0,
                          fidl::StringView::FromExternal(path), std::move(svc_endpoints->server));
  if (!result.ok()) {
    return fpromise::make_error_promise(result.status());
  }
  auto file =
      fidl::WireSharedClient<fuchsia_io::Node>(std::move(svc_endpoints->client), dispatcher);

  // Call something simple on the Node to make sure we actually opened it successfully.
  // Otherwise, the open call is pipelined and it could fail silently.
  fpromise::bridge<void, zx_status_t> bridge;
  file->GetFlags().ThenExactlyOnce(
      [file = file.Clone(), completer = std::move(bridge.completer)](
          fidl::WireUnownedResult<fuchsia_io::Node::GetFlags>& result) mutable {
        if (result.ok()) {
          completer.complete_ok();
        } else {
          zx_status_t status = result.status();
          if (status == ZX_ERR_PEER_CLOSED) {
            status = ZX_ERR_NOT_FOUND;
          }
          completer.complete_error(status);
        }
      });
  return bridge.consumer.promise();
}

}  // namespace

zx::status<DevfsExporter> DevfsExporter::Create(
    const Namespace& ns, async_dispatcher_t* dispatcher,
    fidl::WireSharedClient<fuchsia_io::Directory> svc_dir) {
  auto result = ns.Connect<fdfs::Exporter>();
  if (result.is_error()) {
    return result.take_error();
  }
  fidl::WireSharedClient<fdfs::Exporter> client(std::move(*result), dispatcher);
  return zx::ok(DevfsExporter(dispatcher, std::move(client), std::move(svc_dir)));
}

DevfsExporter::DevfsExporter(async_dispatcher_t* dispatcher,
                             fidl::WireSharedClient<fdfs::Exporter> exporter,
                             fidl::WireSharedClient<fuchsia_io::Directory> svc_dir)
    : dispatcher_(dispatcher), exporter_(std::move(exporter)), svc_dir_(std::move(svc_dir)) {}

fpromise::promise<void, zx_status_t> DevfsExporter::ExportImpl(std::string_view service_path,
                                                               std::string_view devfs_path,
                                                               uint32_t protocol_id) const {
  // Get a connection to svc_dir.
  auto svc_endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
  if (svc_endpoints.is_error()) {
    return fpromise::make_error_promise(svc_endpoints.status_value());
  }

  auto result =
      svc_dir_->Clone(fuchsia_io::wire::kCloneFlagSameRights,
                      fidl::ServerEnd<fuchsia_io::Node>(svc_endpoints->server.TakeChannel()));
  if (!result.ok()) {
    return fpromise::make_error_promise(result.status());
  }

  // Call the Exporter function.
  fpromise::bridge<void, zx_status_t> bridge;
  auto callback = [completer = std::move(bridge.completer)](
                      fidl::WireUnownedResult<fdfs::Exporter::Export>& response) mutable {
    if (!response.ok()) {
      completer.complete_error(response.status());
    } else if (response->result.is_err()) {
      completer.complete_error(response->result.err());
    } else {
      completer.complete_ok();
    }
  };
  exporter_
      ->Export(std::move(svc_endpoints->client), fidl::StringView::FromExternal(service_path),
               fidl::StringView::FromExternal(devfs_path), protocol_id)
      .ThenExactlyOnce(std::move(callback));
  return bridge.consumer.promise_or(fpromise::error(ZX_ERR_INTERNAL));
}

fpromise::promise<void, zx_status_t> DevfsExporter::Export(std::string_view service_path,
                                                           std::string_view devfs_path,
                                                           uint32_t protocol_id) const {
  return CheckFileExists(svc_dir_, service_path, dispatcher_)
      .and_then([this, service_path = std::string(service_path),
                 devfs_path = std::string(devfs_path),
                 protocol_id]() { return ExportImpl(service_path, devfs_path, protocol_id); });
}

}  // namespace driver
