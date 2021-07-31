// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/bin/driver_manager/devfs_exporter.h"

#include "src/devices/lib/log/log.h"
#include "src/lib/storage/vfs/cpp/service.h"

namespace fdfs = fuchsia_device_fs;

namespace driver_manager {

DevfsExporter::DevfsExporter(Devnode* root, async_dispatcher_t* dispatcher)
    : root_(root), dispatcher_(dispatcher) {}

zx::status<> DevfsExporter::PublishExporter(const fbl::RefPtr<fs::PseudoDir>& svc_dir) {
  const auto service = [this](fidl::ServerEnd<fdfs::Exporter> request) {
    fidl::BindServer(dispatcher_, std::move(request), this);
    return ZX_OK;
  };
  zx_status_t status = svc_dir->AddEntry(fidl::DiscoverableProtocolName<fdfs::Exporter>,
                                         fbl::MakeRefCounted<fs::Service>(service));
  if (status != ZX_OK) {
    LOGF(ERROR, "Failed to add directory entry '%s': %s",
         fidl::DiscoverableProtocolName<fdfs::Exporter>, zx_status_get_string(status));
  }
  return zx::make_status(status);
}

void DevfsExporter::Export(ExportRequestView request, ExportCompleter::Sync& completer) {
  zx_status_t status = devfs_export(root_, std::move(request->service_node),
                                    request->devfs_path.get(), request->protocol_id, devnodes_);
  if (status != ZX_OK) {
    LOGF(ERROR, "Failed to export service to devfs path \"%.*s\": %s",
         static_cast<int>(request->devfs_path.size()), request->devfs_path.data(),
         zx_status_get_string(status));
    completer.ReplyError(status);
    return;
  }
  completer.ReplySuccess();
}

}  // namespace driver_manager
