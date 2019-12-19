// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fuchsia/paver/llcpp/fidl.h>
#include <lib/zx/channel.h>
#include <zircon/types.h>

#include <fbl/string.h>
#include <fbl/unique_fd.h>

#include "abr-client.h"
#include "device-partitioner.h"
#include "lib/async/dispatcher.h"

namespace paver {

class Paver : public ::llcpp::fuchsia::paver::Paver::Interface {
 public:
  void FindDataSink(zx::channel data_sink, FindDataSinkCompleter::Sync completer) override;

  void UseBlockDevice(zx::channel block_device, zx::channel dynamic_data_sink,
                      UseBlockDeviceCompleter::Sync completer) override;

  void FindBootManager(zx::channel boot_manager, bool intialize,
                       FindBootManagerCompleter::Sync completer) override;

  void set_dispatcher(async_dispatcher_t* dispatcher) { dispatcher_ = dispatcher; }
  void set_devfs_root(fbl::unique_fd devfs_root) { devfs_root_ = std::move(devfs_root); }
  void set_svc_root(zx::channel svc_root) { svc_root_ = std::move(svc_root); }

 private:
  // Used for test injection.
  fbl::unique_fd devfs_root_;
  zx::channel svc_root_;

  async_dispatcher_t* dispatcher_ = nullptr;
};

// Common shared implementation for DataSink and DynamicDataSink. Necessary to work around lack of
// "is-a" relationship in llcpp bindings.
class DataSinkImpl {
 public:
  DataSinkImpl(fbl::unique_fd devfs_root, std::unique_ptr<DevicePartitioner> partitioner)
      : devfs_root_(std::move(devfs_root)), partitioner_(std::move(partitioner)) {}

  zx_status_t ReadAsset(::llcpp::fuchsia::paver::Configuration configuration,
                        ::llcpp::fuchsia::paver::Asset asset, ::llcpp::fuchsia::mem::Buffer* buf);

  zx_status_t WriteAsset(::llcpp::fuchsia::paver::Configuration configuration,
                         ::llcpp::fuchsia::paver::Asset asset,
                         ::llcpp::fuchsia::mem::Buffer payload);

  zx_status_t WriteVolumes(zx::channel payload_stream);

  zx_status_t WriteBootloader(::llcpp::fuchsia::mem::Buffer payload);

  zx_status_t WriteDataFile(fidl::StringView filename, ::llcpp::fuchsia::mem::Buffer payload);

  zx_status_t WipeVolume(zx::channel* out);

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
                   zx::channel server);

  void ReadAsset(::llcpp::fuchsia::paver::Configuration configuration,
                 ::llcpp::fuchsia::paver::Asset asset,
                 ReadAssetCompleter::Sync completer) override;

  void WriteAsset(::llcpp::fuchsia::paver::Configuration configuration,
                  ::llcpp::fuchsia::paver::Asset asset, ::llcpp::fuchsia::mem::Buffer payload,
                  WriteAssetCompleter::Sync completer) override {
    completer.Reply(sink_.WriteAsset(configuration, asset, std::move(payload)));
  }

  void WriteVolumes(zx::channel payload_stream,
                    WriteVolumesCompleter::Sync completer) override {
    completer.Reply(sink_.WriteVolumes(std::move(payload_stream)));
  }

  void WriteBootloader(::llcpp::fuchsia::mem::Buffer payload,
                       WriteBootloaderCompleter::Sync completer) override {
    completer.Reply(sink_.WriteBootloader(std::move(payload)));
  }

  void WriteDataFile(fidl::StringView filename, ::llcpp::fuchsia::mem::Buffer payload,
                     WriteDataFileCompleter::Sync completer) override {
    completer.Reply(sink_.WriteDataFile(filename, std::move(payload)));
  }

  void WipeVolume(WipeVolumeCompleter::Sync completer) override;

 private:
  DataSinkImpl sink_;
};

class DynamicDataSink : public ::llcpp::fuchsia::paver::DynamicDataSink::Interface {
 public:
  DynamicDataSink(fbl::unique_fd devfs_root, std::unique_ptr<DevicePartitioner> partitioner)
      : sink_(std::move(devfs_root), std::move(partitioner)) {}

  static void Bind(async_dispatcher_t* dispatcher, fbl::unique_fd devfs_root, zx::channel svc_root,
                   zx::channel block_device, zx::channel server);

  void InitializePartitionTables(InitializePartitionTablesCompleter::Sync completer) override;

  void WipePartitionTables(WipePartitionTablesCompleter::Sync completer) override;

  void ReadAsset(::llcpp::fuchsia::paver::Configuration configuration,
                 ::llcpp::fuchsia::paver::Asset asset,
                 ReadAssetCompleter::Sync completer) override;

  void WriteAsset(::llcpp::fuchsia::paver::Configuration configuration,
                  ::llcpp::fuchsia::paver::Asset asset, ::llcpp::fuchsia::mem::Buffer payload,
                  WriteAssetCompleter::Sync completer) override {
    completer.Reply(sink_.WriteAsset(configuration, asset, std::move(payload)));
  }

  void WriteVolumes(zx::channel payload_stream,
                    WriteVolumesCompleter::Sync completer) override {
    completer.Reply(sink_.WriteVolumes(std::move(payload_stream)));
  }

  void WriteBootloader(::llcpp::fuchsia::mem::Buffer payload,
                       WriteBootloaderCompleter::Sync completer) override {
    completer.Reply(sink_.WriteBootloader(std::move(payload)));
  }

  void WriteDataFile(fidl::StringView filename, ::llcpp::fuchsia::mem::Buffer payload,
                     WriteDataFileCompleter::Sync completer) override {
    completer.Reply(sink_.WriteDataFile(filename, std::move(payload)));
  }

  void WipeVolume(WipeVolumeCompleter::Sync completer) override;

 private:
  DataSinkImpl sink_;
};

class BootManager : public ::llcpp::fuchsia::paver::BootManager::Interface {
 public:
  BootManager(std::unique_ptr<abr::Client> abr_client) : abr_client_(std::move(abr_client)) {}

  static void Bind(async_dispatcher_t* dispatcher, fbl::unique_fd devfs_root, zx::channel svc_root,
                   zx::channel server, bool initialize);

  void QueryActiveConfiguration(QueryActiveConfigurationCompleter::Sync completer) override;

  void QueryConfigurationStatus(::llcpp::fuchsia::paver::Configuration configuration,
                                QueryConfigurationStatusCompleter::Sync completer) override;

  void SetConfigurationActive(::llcpp::fuchsia::paver::Configuration configuration,
                              SetConfigurationActiveCompleter::Sync completer) override;

  void SetConfigurationUnbootable(::llcpp::fuchsia::paver::Configuration configuration,
                                  SetConfigurationUnbootableCompleter::Sync completer) override;

  void SetActiveConfigurationHealthy(
      SetActiveConfigurationHealthyCompleter::Sync completer) override;

 private:
  std::unique_ptr<abr::Client> abr_client_;
};

}  // namespace paver
