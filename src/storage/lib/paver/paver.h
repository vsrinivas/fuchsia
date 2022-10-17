// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_LIB_PAVER_PAVER_H_
#define SRC_STORAGE_LIB_PAVER_PAVER_H_

#include <fidl/fuchsia.paver/cpp/wire.h>
#include <lib/zx/channel.h>
#include <zircon/types.h>

#include <variant>

#include <fbl/mutex.h>
#include <fbl/string.h>
#include <fbl/unique_fd.h>

#include "abr-client.h"
#include "device-partitioner.h"
#include "lib/async/dispatcher.h"
#include "paver-context.h"

namespace paver {

class Paver : public fidl::WireServer<fuchsia_paver::Paver> {
 public:
  void FindDataSink(FindDataSinkRequestView request,
                    FindDataSinkCompleter::Sync& completer) override;

  void UseBlockDevice(UseBlockDeviceRequestView request,
                      UseBlockDeviceCompleter::Sync& completer) override;

  void UseBlockDevice(zx::channel block_device, zx::channel dynamic_data_sink);

  void FindBootManager(FindBootManagerRequestView request,
                       FindBootManagerCompleter::Sync& completer) override;

  void FindSysconfig(FindSysconfigRequestView request,
                     FindSysconfigCompleter::Sync& completer) override;

  void FindSysconfig(zx::channel sysconfig);

  void set_dispatcher(async_dispatcher_t* dispatcher) { dispatcher_ = dispatcher; }
  void set_devfs_root(fbl::unique_fd devfs_root) { devfs_root_ = std::move(devfs_root); }
  void set_svc_root(fidl::ClientEnd<fuchsia_io::Directory> svc_root) {
    svc_root_ = std::move(svc_root);
  }

  Paver() : context_(std::make_shared<Context>()) {}

 private:
  // Used for test injection.
  fbl::unique_fd devfs_root_;
  fidl::ClientEnd<fuchsia_io::Directory> svc_root_;

  async_dispatcher_t* dispatcher_ = nullptr;

  // Declared as shared_ptr to avoid life time issues (i.e. Paver exiting before the created device
  // partitioners).
  std::shared_ptr<Context> context_;
};

// Common shared implementation for DataSink and DynamicDataSink. Necessary to work around lack of
// "is-a" relationship in llcpp bindings.
class DataSinkImpl {
 public:
  DataSinkImpl(fbl::unique_fd devfs_root, std::unique_ptr<DevicePartitioner> partitioner)
      : devfs_root_(std::move(devfs_root)), partitioner_(std::move(partitioner)) {}

  zx::result<fuchsia_mem::wire::Buffer> ReadAsset(fuchsia_paver::wire::Configuration configuration,
                                                  fuchsia_paver::wire::Asset asset);

  zx::result<> WriteAsset(fuchsia_paver::wire::Configuration configuration,
                          fuchsia_paver::wire::Asset asset, fuchsia_mem::wire::Buffer payload);

  zx::result<> WriteOpaqueVolume(fuchsia_mem::wire::Buffer payload);

  // FIDL llcpp unions don't currently support memory ownership so we need to
  // return something that does own the underlying memory.
  //
  // Once unions do support owned memory we can just return
  // WriteBootloaderResult directly here.
  std::variant<zx_status_t, bool> WriteFirmware(fuchsia_paver::wire::Configuration configuration,
                                                fidl::StringView type,
                                                fuchsia_mem::wire::Buffer payload);

  zx::result<fuchsia_mem::wire::Buffer> ReadFirmware(
      fuchsia_paver::wire::Configuration configuration, fidl::StringView type);

  zx::result<> WriteVolumes(zx::channel payload_stream);

  zx::result<> WriteBootloader(fuchsia_mem::wire::Buffer payload);

  zx::result<zx::channel> WipeVolume();

  DevicePartitioner* partitioner() { return partitioner_.get(); }

 private:
  // Used for test injection.
  fbl::unique_fd devfs_root_;

  std::unique_ptr<DevicePartitioner> partitioner_;

  // A helper to get firmware partition spec.
  std::optional<PartitionSpec> GetFirmwarePartitionSpec(
      fuchsia_paver::wire::Configuration configuration, fidl::StringView type);
};

class DataSink : public fidl::WireServer<fuchsia_paver::DataSink> {
 public:
  DataSink(fbl::unique_fd devfs_root, std::unique_ptr<DevicePartitioner> partitioner)
      : sink_(std::move(devfs_root), std::move(partitioner)) {}

  // Automatically finds block device to use.
  static void Bind(async_dispatcher_t* dispatcher, fbl::unique_fd devfs_root,
                   fidl::ClientEnd<fuchsia_io::Directory> svc_root, zx::channel server,
                   std::shared_ptr<Context> context);

  void ReadAsset(ReadAssetRequestView request, ReadAssetCompleter::Sync& completer) override;

  void WriteAsset(WriteAssetRequestView request, WriteAssetCompleter::Sync& completer) override {
    completer.Reply(
        sink_.WriteAsset(request->configuration, request->asset, std::move(request->payload))
            .status_value());
  }

  void WriteOpaqueVolume(WriteOpaqueVolumeRequestView request,
                         WriteOpaqueVolumeCompleter::Sync& completer) override {
    zx::result<> res = sink_.WriteOpaqueVolume(std::move(request->payload));
    if (res.is_ok()) {
      completer.ReplySuccess();
    } else {
      completer.ReplyError(res.status_value());
    }
  }

  void WriteFirmware(WriteFirmwareRequestView request,
                     WriteFirmwareCompleter::Sync& completer) override;

  void ReadFirmware(ReadFirmwareRequestView request,
                    ReadFirmwareCompleter::Sync& completer) override;

  void WriteVolumes(WriteVolumesRequestView request,
                    WriteVolumesCompleter::Sync& completer) override {
    completer.Reply(sink_.WriteVolumes(request->payload.TakeChannel()).status_value());
  }

  void WriteBootloader(WriteBootloaderRequestView request,
                       WriteBootloaderCompleter::Sync& completer) override {
    completer.Reply(sink_.WriteBootloader(std::move(request->payload)).status_value());
  }

  void WipeVolume(WipeVolumeCompleter::Sync& completer) override;

  void Flush(FlushCompleter::Sync& completer) override {
    completer.Reply(sink_.partitioner()->Flush().status_value());
  }

 private:
  DataSinkImpl sink_;
};

class DynamicDataSink : public fidl::WireServer<fuchsia_paver::DynamicDataSink> {
 public:
  DynamicDataSink(fbl::unique_fd devfs_root, std::unique_ptr<DevicePartitioner> partitioner)
      : sink_(std::move(devfs_root), std::move(partitioner)) {}

  static void Bind(async_dispatcher_t* dispatcher, fbl::unique_fd devfs_root,
                   fidl::ClientEnd<fuchsia_io::Directory> svc_root, zx::channel block_device,
                   zx::channel server, std::shared_ptr<Context> context);

  void InitializePartitionTables(InitializePartitionTablesCompleter::Sync& completer) override;

  void WipePartitionTables(WipePartitionTablesCompleter::Sync& completer) override;

  void ReadAsset(ReadAssetRequestView request, ReadAssetCompleter::Sync& completer) override;

  void WriteAsset(WriteAssetRequestView request, WriteAssetCompleter::Sync& completer) override {
    completer.Reply(
        sink_.WriteAsset(request->configuration, request->asset, std::move(request->payload))
            .status_value());
  }

  void WriteOpaqueVolume(WriteOpaqueVolumeRequestView request,
                         WriteOpaqueVolumeCompleter::Sync& completer) override {
    zx::result<> res = sink_.WriteOpaqueVolume(std::move(request->payload));
    if (res.is_ok()) {
      completer.ReplySuccess();
    } else {
      completer.ReplyError(res.status_value());
    }
  }

  void WriteFirmware(WriteFirmwareRequestView request,
                     WriteFirmwareCompleter::Sync& completer) override;

  void ReadFirmware(ReadFirmwareRequestView request,
                    ReadFirmwareCompleter::Sync& completer) override;

  void WriteVolumes(WriteVolumesRequestView request,
                    WriteVolumesCompleter::Sync& completer) override {
    completer.Reply(sink_.WriteVolumes(request->payload.TakeChannel()).status_value());
  }

  void WriteBootloader(WriteBootloaderRequestView request,
                       WriteBootloaderCompleter::Sync& completer) override {
    completer.Reply(sink_.WriteBootloader(std::move(request->payload)).status_value());
  }

  void WipeVolume(WipeVolumeCompleter::Sync& completer) override;

  void Flush(FlushCompleter::Sync& completer) override {
    completer.Reply(sink_.partitioner()->Flush().status_value());
  }

 private:
  DataSinkImpl sink_;
};

class BootManager : public fidl::WireServer<fuchsia_paver::BootManager> {
 public:
  BootManager(std::unique_ptr<abr::Client> abr_client, fbl::unique_fd devfs_root,
              fidl::ClientEnd<fuchsia_io::Directory> svc_root)
      : abr_client_(std::move(abr_client)),
        devfs_root_(std::move(devfs_root)),
        svc_root_(std::move(svc_root)) {}

  static void Bind(async_dispatcher_t* dispatcher, fbl::unique_fd devfs_root,
                   fidl::ClientEnd<fuchsia_io::Directory> svc_root,
                   std::shared_ptr<Context> context, zx::channel server);

  void QueryCurrentConfiguration(QueryCurrentConfigurationCompleter::Sync& completer) override;

  void QueryActiveConfiguration(QueryActiveConfigurationCompleter::Sync& completer) override;

  void QueryConfigurationLastSetActive(
      QueryConfigurationLastSetActiveCompleter::Sync& completer) override;

  void QueryConfigurationStatus(QueryConfigurationStatusRequestView request,
                                QueryConfigurationStatusCompleter::Sync& completer) override;

  void SetConfigurationActive(SetConfigurationActiveRequestView request,
                              SetConfigurationActiveCompleter::Sync& completer) override;

  void SetConfigurationUnbootable(SetConfigurationUnbootableRequestView request,
                                  SetConfigurationUnbootableCompleter::Sync& completer) override;

  void SetConfigurationHealthy(SetConfigurationHealthyRequestView request,
                               SetConfigurationHealthyCompleter::Sync& completer) override;

  void Flush(FlushCompleter::Sync& completer) override {
    completer.Reply(abr_client_->Flush().status_value());
  }

 private:
  std::unique_ptr<abr::Client> abr_client_;
  fbl::unique_fd devfs_root_;
  fidl::ClientEnd<fuchsia_io::Directory> svc_root_;
};

}  // namespace paver

#endif  // SRC_STORAGE_LIB_PAVER_PAVER_H_
