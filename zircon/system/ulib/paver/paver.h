// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/string.h>
#include <fbl/unique_fd.h>
#include <fuchsia/paver/llcpp/fidl.h>
#include <lib/zx/channel.h>
#include <zircon/types.h>

#include "device-partitioner.h"

namespace paver {

// Options for locating an FVM within a partition.
enum class BindOption {
  // Bind to the FVM, if it exists already.
  TryBind,
  // Reformat the partition, regardless of if it already exists as an FVM.
  Reformat,
};

// Public for testing.
fbl::unique_fd FvmPartitionFormat(const fbl::unique_fd& devfs_root, fbl::unique_fd partition_fd,
                                  size_t slice_size, BindOption option);

class Paver : public ::llcpp::fuchsia::paver::Paver::Interface {
 public:
  // Writes a kernel or verified boot metadata payload to the appropriate
  // partition.
  void WriteAsset(::llcpp::fuchsia::paver::Configuration configuration,
                  ::llcpp::fuchsia::paver::Asset asset, ::llcpp::fuchsia::mem::Buffer payload,
                  WriteAssetCompleter::Sync completer);

  // Writes volumes to the FVM partition.
  void WriteVolumes(zx::channel payload_stream, WriteVolumesCompleter::Sync completer);

  // Writes a bootloader image to the appropriate partition.
  void WriteBootloader(::llcpp::fuchsia::mem::Buffer payload,
                       WriteBootloaderCompleter::Sync completer);

  // Writes a file to the data minfs partition, managed by the FVM.
  void WriteDataFile(fidl::StringView filename, ::llcpp::fuchsia::mem::Buffer payload,
                     WriteDataFileCompleter::Sync completer);

  // Wipes all volumes from the FVM partition.
  void WipeVolumes(zx::channel gpt_block_device, WipeVolumesCompleter::Sync completer);

  // Initializes GPT and add FVM entries on given block device.
  void InitializePartitionTables(zx::channel gpt_block_device,
                                 InitializePartitionTablesCompleter::Sync completer);

  void QueryActiveConfiguration(QueryActiveConfigurationCompleter::Sync completer) {
    ::llcpp::fuchsia::paver::Paver_QueryActiveConfiguration_Result result;
    result.set_err(ZX_ERR_NOT_SUPPORTED);
    completer.Reply(std::move(result));
  }

  void SetActiveConfiguration(::llcpp::fuchsia::paver::Configuration configuration,
                              SetActiveConfigurationCompleter::Sync completer) {
    completer.Reply(ZX_ERR_NOT_SUPPORTED);
  }

  void MarkActiveConfigurationSuccessful(
      MarkActiveConfigurationSuccessfulCompleter::Sync completer) {
    completer.Reply(ZX_ERR_NOT_SUPPORTED);
  }

  void ForceRecoveryConfiguration(ForceRecoveryConfigurationCompleter::Sync completer) {
    completer.Reply(ZX_ERR_NOT_SUPPORTED);
  }

  void set_devfs_root(fbl::unique_fd devfs_root) { devfs_root_ = std::move(devfs_root); }

 private:
  bool InitializePartitioner();

  // Used for test injection.
  fbl::unique_fd devfs_root_;
  // Lazily initialized to allow test to inject a fake devfs root after creating.
  std::unique_ptr<DevicePartitioner> partitioner_;
};

}  // namespace paver
