// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_LIB_PAVER_PAVER_H_
#define SRC_STORAGE_LIB_PAVER_PAVER_H_

#include <fuchsia/paver/llcpp/fidl.h>
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

class Paver : public ::llcpp::fuchsia::paver::Paver::Interface {
 public:
  void FindDataSink(zx::channel data_sink, FindDataSinkCompleter::Sync completer) override;

  void UseBlockDevice(zx::channel block_device, zx::channel dynamic_data_sink,
                      UseBlockDeviceCompleter::Sync completer) override;

  void UseBlockDevice(zx::channel block_device, zx::channel dynamic_data_sink);

  void FindBootManager(zx::channel boot_manager, FindBootManagerCompleter::Sync completer) override;

  void set_dispatcher(async_dispatcher_t* dispatcher) { dispatcher_ = dispatcher; }
  void set_devfs_root(fbl::unique_fd devfs_root) { devfs_root_ = std::move(devfs_root); }
  void set_svc_root(zx::channel svc_root) { svc_root_ = std::move(svc_root); }

  Paver() : context_(std::make_shared<Context>()) {}

 private:
  // Used for test injection.
  fbl::unique_fd devfs_root_;
  zx::channel svc_root_;

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

  zx::status<::llcpp::fuchsia::mem::Buffer> ReadAsset(
      ::llcpp::fuchsia::paver::Configuration configuration, ::llcpp::fuchsia::paver::Asset asset);

  zx::status<> WriteAsset(::llcpp::fuchsia::paver::Configuration configuration,
                          ::llcpp::fuchsia::paver::Asset asset,
                          ::llcpp::fuchsia::mem::Buffer payload);

  // FIDL llcpp unions don't currently support memory ownership so we need to
  // return something that does own the underlying memory.
  //
  // Once unions do support owned memory we can just return
  // WriteBootloaderResult directly here.
  std::variant<zx_status_t, fidl::aligned<bool>> WriteFirmware(
      ::llcpp::fuchsia::paver::Configuration configuration, fidl::StringView type,
      ::llcpp::fuchsia::mem::Buffer payload);

  zx::status<> WriteVolumes(zx::channel payload_stream);

  zx::status<> WriteBootloader(::llcpp::fuchsia::mem::Buffer payload);

  zx::status<> WriteDataFile(fidl::StringView filename, ::llcpp::fuchsia::mem::Buffer payload);

  zx::status<zx::channel> WipeVolume();

  DevicePartitioner* partitioner() { return partitioner_.get(); }

 private:
  // Used for test injection.
  fbl::unique_fd devfs_root_;

  std::unique_ptr<DevicePartitioner> partitioner_;
};

class DataSink : public ::llcpp::fuchsia::paver::DataSink::Interface {
 public:
  DataSink(fbl::unique_fd devfs_root, std::unique_ptr<DevicePartitioner> partitioner)
      : sink_(std::move(devfs_root), std::move(partitioner)) {}

  // Automatically finds block device to use.
  static void Bind(async_dispatcher_t* dispatcher, fbl::unique_fd devfs_root, zx::channel svc_root,
                   zx::channel server, std::shared_ptr<Context> context);

  void ReadAsset(::llcpp::fuchsia::paver::Configuration configuration,
                 ::llcpp::fuchsia::paver::Asset asset, ReadAssetCompleter::Sync completer) override;

  void WriteAsset(::llcpp::fuchsia::paver::Configuration configuration,
                  ::llcpp::fuchsia::paver::Asset asset, ::llcpp::fuchsia::mem::Buffer payload,
                  WriteAssetCompleter::Sync completer) override {
    completer.Reply(sink_.WriteAsset(configuration, asset, std::move(payload)).status_value());
  }

  void WriteFirmware(::llcpp::fuchsia::paver::Configuration configuration, fidl::StringView type,
                     ::llcpp::fuchsia::mem::Buffer payload,
                     WriteFirmwareCompleter::Sync completer) override;

  void WriteVolumes(zx::channel payload_stream, WriteVolumesCompleter::Sync completer) override {
    completer.Reply(sink_.WriteVolumes(std::move(payload_stream)).status_value());
  }

  void WriteBootloader(::llcpp::fuchsia::mem::Buffer payload,
                       WriteBootloaderCompleter::Sync completer) override {
    completer.Reply(sink_.WriteBootloader(std::move(payload)).status_value());
  }

  void WriteDataFile(fidl::StringView filename, ::llcpp::fuchsia::mem::Buffer payload,
                     WriteDataFileCompleter::Sync completer) override {
    completer.Reply(sink_.WriteDataFile(std::move(filename), std::move(payload)).status_value());
  }

  void WipeVolume(WipeVolumeCompleter::Sync completer) override;

  void Flush(FlushCompleter::Sync completer) override {
    completer.Reply(sink_.partitioner()->Flush().status_value());
  }

 private:
  DataSinkImpl sink_;
};

class DynamicDataSink : public ::llcpp::fuchsia::paver::DynamicDataSink::Interface {
 public:
  DynamicDataSink(fbl::unique_fd devfs_root, std::unique_ptr<DevicePartitioner> partitioner)
      : sink_(std::move(devfs_root), std::move(partitioner)) {}

  static void Bind(async_dispatcher_t* dispatcher, fbl::unique_fd devfs_root, zx::channel svc_root,
                   zx::channel block_device, zx::channel server, std::shared_ptr<Context> context);

  void InitializePartitionTables(InitializePartitionTablesCompleter::Sync completer) override;

  void WipePartitionTables(WipePartitionTablesCompleter::Sync completer) override;

  void ReadAsset(::llcpp::fuchsia::paver::Configuration configuration,
                 ::llcpp::fuchsia::paver::Asset asset, ReadAssetCompleter::Sync completer) override;

  void WriteAsset(::llcpp::fuchsia::paver::Configuration configuration,
                  ::llcpp::fuchsia::paver::Asset asset, ::llcpp::fuchsia::mem::Buffer payload,
                  WriteAssetCompleter::Sync completer) override {
    completer.Reply(sink_.WriteAsset(configuration, asset, std::move(payload)).status_value());
  }

  void WriteFirmware(::llcpp::fuchsia::paver::Configuration configuration, fidl::StringView type,
                     ::llcpp::fuchsia::mem::Buffer payload,
                     WriteFirmwareCompleter::Sync completer) override;

  void WriteVolumes(zx::channel payload_stream, WriteVolumesCompleter::Sync completer) override {
    completer.Reply(sink_.WriteVolumes(std::move(payload_stream)).status_value());
  }

  void WriteBootloader(::llcpp::fuchsia::mem::Buffer payload,
                       WriteBootloaderCompleter::Sync completer) override {
    completer.Reply(sink_.WriteBootloader(std::move(payload)).status_value());
  }

  void WriteDataFile(fidl::StringView filename, ::llcpp::fuchsia::mem::Buffer payload,
                     WriteDataFileCompleter::Sync completer) override {
    completer.Reply(sink_.WriteDataFile(std::move(filename), std::move(payload)).status_value());
  }

  void WipeVolume(WipeVolumeCompleter::Sync completer) override;

  void Flush(FlushCompleter::Sync completer) override {
    completer.Reply(sink_.partitioner()->Flush().status_value());
  }

 private:
  DataSinkImpl sink_;
};

class BootManager : public ::llcpp::fuchsia::paver::BootManager::Interface {
 public:
  BootManager(std::unique_ptr<abr::Client> abr_client) : abr_client_(std::move(abr_client)) {}

  static void Bind(async_dispatcher_t* dispatcher, fbl::unique_fd devfs_root, zx::channel svc_root,
                   std::shared_ptr<Context> context, zx::channel server);

  void QueryActiveConfiguration(QueryActiveConfigurationCompleter::Sync completer) override;

  void QueryConfigurationStatus(::llcpp::fuchsia::paver::Configuration configuration,
                                QueryConfigurationStatusCompleter::Sync completer) override;

  void SetConfigurationActive(::llcpp::fuchsia::paver::Configuration configuration,
                              SetConfigurationActiveCompleter::Sync completer) override;

  void SetConfigurationUnbootable(::llcpp::fuchsia::paver::Configuration configuration,
                                  SetConfigurationUnbootableCompleter::Sync completer) override;

  void SetActiveConfigurationHealthy(
      SetActiveConfigurationHealthyCompleter::Sync completer) override;

  void Flush(FlushCompleter::Sync completer) override {
    completer.Reply(abr_client_->Flush().status_value());
  }

 private:
  std::unique_ptr<abr::Client> abr_client_;
};

}  // namespace paver

#endif  // SRC_STORAGE_LIB_PAVER_PAVER_H_
