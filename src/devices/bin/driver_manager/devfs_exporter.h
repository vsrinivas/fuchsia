// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_MANAGER_DEVFS_EXPORTER_H_
#define SRC_DEVICES_BIN_DRIVER_MANAGER_DEVFS_EXPORTER_H_

#include <fidl/fuchsia.device.fs/cpp/wire.h>
#include <lib/sys/component/cpp/outgoing_directory.h>

#include "src/devices/bin/driver_manager/devfs.h"

namespace driver_manager {

// Each ExportWatcher represents one call to `DevfsExporter::Export`. It holds all of the
// nodes created by that export.
class ExportWatcher : public fidl::WireAsyncEventHandler<fuchsia_io::Node> {
 public:
  explicit ExportWatcher() = default;

  // Create an ExportWatcher.
  static zx::result<std::unique_ptr<ExportWatcher>> Create(
      async_dispatcher_t* dispatcher, Devfs& devfs, Devnode* root,
      fidl::ClientEnd<fuchsia_io::Directory> service_dir, std::string_view service_path,
      std::string_view devfs_path, uint32_t protocol_id,
      fuchsia_device_fs::wire::ExportOptions options);

  // Set a callback that will be called when the connection to `service_path` is closed.
  void set_on_close_callback(fit::callback<void()> callback) { callback_ = std::move(callback); }

  // Because `ExportWatcher` is bound to `client_`, this will be called whenever
  // there is a FIDL error on `client_`. Since we do not send reports, we know
  // that this will only be called when the connection closes.
  void on_fidl_error(fidl::UnbindInfo error) override {
    if (callback_) {
      callback_();
    }
  }

  std::string_view devfs_path() const { return devfs_path_; }

  zx_status_t MakeVisible();

 private:
  fit::callback<void()> callback_;
  fidl::WireClient<fuchsia_io::Node> client_;
  std::vector<std::unique_ptr<Devnode>> devnodes_;
  std::string devfs_path_;
};

class DevfsExporter : public fidl::WireServer<fuchsia_device_fs::Exporter> {
 public:
  // The `root` Devnode must outlive `this`.
  DevfsExporter(Devfs& devfs, Devnode* root, async_dispatcher_t* dispatcher);

  void PublishExporter(component::OutgoingDirectory& outgoing);

 private:
  // fidl::WireServer<fuchsia_device_fs::Exporter>
  void Export(ExportRequestView request, ExportCompleter::Sync& completer) override;

  void ExportOptions(ExportOptionsRequestView request,
                     ExportOptionsCompleter::Sync& completer) override;

  void MakeVisible(MakeVisibleRequestView request, MakeVisibleCompleter::Sync& completer) override;

  Devfs& devfs_;
  Devnode* const root_;
  async_dispatcher_t* const dispatcher_;

  std::vector<std::unique_ptr<ExportWatcher>> exports_;
};

}  // namespace driver_manager

#endif  // SRC_DEVICES_BIN_DRIVER_MANAGER_DEVFS_EXPORTER_H_
