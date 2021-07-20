// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/lib/driver2/devfs_exporter.h"

#include <lib/fpromise/bridge.h>

namespace fdfs = fuchsia_device_fs;

namespace driver {

zx::status<DevfsExporter> DevfsExporter::Create(const Namespace& ns, async_dispatcher_t* dispatcher,
                                                const fbl::RefPtr<fs::PseudoDir>& svc_dir) {
  auto result = ns.Connect<fdfs::Exporter>();
  if (result.is_error()) {
    return result.take_error();
  }
  fidl::WireSharedClient<fdfs::Exporter> client(std::move(*result), dispatcher);
  return zx::ok(DevfsExporter(std::move(client), svc_dir));
}

DevfsExporter::DevfsExporter(fidl::WireSharedClient<fdfs::Exporter> exporter,
                             const fbl::RefPtr<fs::PseudoDir>& svc_dir)
    : exporter_(std::move(exporter)), svc_dir_(svc_dir) {}

fpromise::promise<void, zx_status_t> DevfsExporter::Export(std::string_view service_name,
                                                           std::string_view devfs_path,
                                                           uint32_t protocol_id) const {
  auto endpoints = fidl::CreateEndpoints<fuchsia_io::Node>();
  if (endpoints.is_error()) {
    return fpromise::make_error_promise(endpoints.status_value());
  }
  fbl::RefPtr<fs::Vnode> service;
  zx_status_t status = svc_dir_->Lookup(service_name, &service);
  if (status != ZX_OK) {
    return fpromise::make_error_promise(status);
  }
  status = service->ConnectService(endpoints->server.TakeChannel());
  if (status != ZX_OK) {
    return fpromise::make_error_promise(status);
  }
  fpromise::bridge<void, zx_status_t> bridge;
  auto callback = [completer = std::move(bridge.completer)](
                      fidl::WireResponse<fdfs::Exporter::Export>* response) mutable {
    if (response->result.is_err()) {
      completer.complete_error(response->result.err());
    } else {
      completer.complete_ok();
    }
  };
  auto result =
      exporter_->Export(std::move(endpoints->client), fidl::StringView::FromExternal(devfs_path),
                        protocol_id, std::move(callback));
  if (!result.ok()) {
    return fpromise::make_error_promise(result.status());
  }
  return bridge.consumer.promise_or(fpromise::error(ZX_ERR_UNAVAILABLE));
}

}  // namespace driver
