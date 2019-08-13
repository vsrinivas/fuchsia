// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdbool.h>

#include <fbl/function.h>
#include <fbl/string.h>
#include <fbl/unique_fd.h>
#include <fbl/unique_ptr.h>
#include <fuchsia/hardware/block/llcpp/fidl.h>
#include <gpt/gpt.h>
#include <lib/fzl/fdio.h>
#include <lib/zx/channel.h>
#include <zircon/types.h>

#include <optional>
#include <utility>
#include <vector>

namespace paver {
using gpt::GptDevice;

enum class Partition {
  kUnknown,
  kBootloader,
  kKernelC,
  kEfi,
  kZirconA,
  kZirconB,
  kZirconR,
  kVbMetaA,
  kVbMetaB,
  kVbMetaR,
  kFuchsiaVolumeManager,
};

const char* PartitionName(Partition type);

// A special filter for test injection.
// API should return true if device passed in should be filtered out.
extern bool (*TestBlockFilter)(const fbl::unique_fd&);

enum class Arch {
  kX64,
  kArm64,
};

// Abstract device partitioner definition.
// This class defines common APIs for interacting with a device partitioner.
class DevicePartitioner {
 public:
  // Factory method which automatically returns the correct DevicePartitioner
  // implementation. Returns nullptr on failure.
  // |block_device| is root block device whichs contains the logical partitions we wish to operate
  // against. It's only meaningful for EFI and CROS devices which may have multiple storage devices.
  static fbl::unique_ptr<DevicePartitioner> Create(fbl::unique_fd devfs_root, Arch arch,
                                                   zx::channel block_device = zx::channel());

  virtual ~DevicePartitioner() = default;

  // Whether to use skip block interface or block interface for non-FVM
  // partitions.
  virtual bool UseSkipBlockInterface() const = 0;

  // Returns a file descriptor to a partition of type |partition_type|, creating it.
  // Assumes that the partition does not already exist.
  virtual zx_status_t AddPartition(Partition partition_type, fbl::unique_fd* out_fd) const = 0;

  // Returns a file descriptor to a partition of type |partition_type| if one exists.
  virtual zx_status_t FindPartition(Partition partition_type, fbl::unique_fd* out_fd) const = 0;

  // Finalizes the partition of type |partition_type| after it has been
  // written.
  virtual zx_status_t FinalizePartition(Partition partition_type) const = 0;

  // Wipes Fuchsia Volume Manager partition.
  virtual zx_status_t WipeFvm() const = 0;

  // Returns block size in bytes for specified device.
  virtual zx_status_t GetBlockSize(const fbl::unique_fd& device_fd, uint32_t* block_size) const = 0;
};

// Useful for when a GPT table is available (e.g. x86 devices). Provides common
// utility functions.
class GptDevicePartitioner {
 public:
  using FilterCallback = fbl::Function<bool(const gpt_partition_t&)>;

  // Find and initialize a GPT based device.
  //
  // If block_device is provided, then search is skipped, and block_device is used
  // directly. If it is not provided, we search for a device with a valid GPT,
  // with an entry for an FVM. If multiple devices with valid GPT containing
  // FVM entries are found, an error is returned.
  static zx_status_t InitializeGpt(fbl::unique_fd devfs_root, Arch arch,
                                   std::optional<fbl::unique_fd> block_device,
                                   fbl::unique_ptr<GptDevicePartitioner>* gpt_out);

  // Returns block info for a specified block device.
  zx_status_t GetBlockInfo(::llcpp::fuchsia::hardware::block::BlockInfo* block_info) const {
    memcpy(block_info, &block_info_, sizeof(*block_info));
    return ZX_OK;
  }

  GptDevice* GetGpt() const { return gpt_.get(); }
  zx::unowned_channel Channel() const { return caller_.channel(); }

  // Find the first spot that has at least |bytes_requested| of space.
  //
  // Returns the |start_out| block and |length_out| blocks, indicating
  // how much space was found, on success. This may be larger than
  // the number of bytes requested.
  zx_status_t FindFirstFit(size_t bytes_requested, size_t* start_out, size_t* length_out) const;

  // Creates a partition, adds an entry to the GPT, and returns a file descriptor to it.
  // Assumes that the partition does not already exist.
  zx_status_t AddPartition(const char* name, uint8_t* type, size_t minimum_size_bytes,
                           size_t optional_reserve_bytes, fbl::unique_fd* out_fd) const;

  // Returns a file descriptor to a partition which can be paved,
  // if one exists.
  zx_status_t FindPartition(FilterCallback filter, fbl::unique_fd* out_fd,
                            gpt_partition_t** out = nullptr) const;

  // Wipes a specified partition from the GPT, and overwrites first 8KiB with
  // nonsense.
  zx_status_t WipeFvm() const;

 private:
  using GptDevices = std::vector<std::pair<std::string, fbl::unique_fd>>;
  // Find all block devices which could contain a GPT.
  static bool FindGptDevices(const fbl::unique_fd& devfs_root, GptDevices* out);

  // Initializes GPT for a device which was explicitly provided. If |gpt_device| doesn't have a
  // valid GPT, it will initialize it with a valid one.
  static zx_status_t InitializeProvidedGptDevice(fbl::unique_fd devfs_root,
                                                 fbl::unique_fd gpt_device,
                                                 fbl::unique_ptr<GptDevicePartitioner>* gpt_out);

  GptDevicePartitioner(fbl::unique_fd devfs_root, fbl::unique_fd fd, fbl::unique_ptr<GptDevice> gpt,
                       ::llcpp::fuchsia::hardware::block::BlockInfo block_info)
      : devfs_root_(std::move(devfs_root)),
        caller_(std::move(fd)),
        gpt_(std::move(gpt)),
        block_info_(block_info) {}

  zx_status_t CreateGptPartition(const char* name, uint8_t* type, uint64_t offset, uint64_t blocks,
                                 uint8_t* out_guid) const;

  fbl::unique_fd devfs_root_;
  fzl::FdioCaller caller_;
  mutable fbl::unique_ptr<GptDevice> gpt_;
  ::llcpp::fuchsia::hardware::block::BlockInfo block_info_;
};

// DevicePartitioner implementation for EFI based devices.
class EfiDevicePartitioner : public DevicePartitioner {
 public:
  static zx_status_t Initialize(fbl::unique_fd devfs_root, Arch arch,
                                std::optional<fbl::unique_fd> block_device,
                                fbl::unique_ptr<DevicePartitioner>* partitioner);

  bool UseSkipBlockInterface() const override { return false; }

  zx_status_t AddPartition(Partition partition_type, fbl::unique_fd* out_fd) const override;

  zx_status_t FindPartition(Partition partition_type, fbl::unique_fd* out_fd) const override;

  zx_status_t FinalizePartition(Partition unused) const override { return ZX_OK; }

  zx_status_t WipeFvm() const override;

  zx_status_t GetBlockSize(const fbl::unique_fd& device_fd, uint32_t* block_size) const override;

 private:
  EfiDevicePartitioner(fbl::unique_ptr<GptDevicePartitioner> gpt) : gpt_(std::move(gpt)) {}

  fbl::unique_ptr<GptDevicePartitioner> gpt_;
};

// DevicePartitioner implementation for ChromeOS devices.
class CrosDevicePartitioner : public DevicePartitioner {
 public:
  static zx_status_t Initialize(fbl::unique_fd devfs_root, Arch arch,
                                std::optional<fbl::unique_fd> block_device,
                                fbl::unique_ptr<DevicePartitioner>* partitioner);

  bool UseSkipBlockInterface() const override { return false; }

  zx_status_t AddPartition(Partition partition_type, fbl::unique_fd* out_fd) const override;

  zx_status_t FindPartition(Partition partition_type, fbl::unique_fd* out_fd) const override;

  zx_status_t FinalizePartition(Partition unused) const override;

  zx_status_t WipeFvm() const override;

  zx_status_t GetBlockSize(const fbl::unique_fd& device_fd, uint32_t* block_size) const override;

 private:
  CrosDevicePartitioner(fbl::unique_ptr<GptDevicePartitioner> gpt) : gpt_(std::move(gpt)) {}

  fbl::unique_ptr<GptDevicePartitioner> gpt_;
};

// DevicePartitioner implementation for devices which have fixed partition maps (e.g. ARM
// devices). It will not attempt to write a partition map of any kind to the device.
// Assumes standardized partition layout structure (e.g. ZIRCON-A, ZIRCON-B,
// ZIRCON-R).
class FixedDevicePartitioner : public DevicePartitioner {
 public:
  static zx_status_t Initialize(fbl::unique_fd devfs_root,
                                fbl::unique_ptr<DevicePartitioner>* partitioner);

  bool UseSkipBlockInterface() const override { return false; }

  zx_status_t AddPartition(Partition partition_type, fbl::unique_fd* out_fd) const override;

  zx_status_t FindPartition(Partition partition_type, fbl::unique_fd* out_fd) const override;

  zx_status_t FinalizePartition(Partition unused) const override { return ZX_OK; }

  zx_status_t WipeFvm() const override;

  zx_status_t GetBlockSize(const fbl::unique_fd& device_fd, uint32_t* block_size) const override;

 private:
  FixedDevicePartitioner(fbl::unique_fd devfs_root) : devfs_root_(std::move(devfs_root)) {}

  fbl::unique_fd devfs_root_;
};

// DevicePartitioner implementation for devices which have fixed partition maps, but do not expose a
// block device interface. Instead they expose devices with skip-block IOCTL interfaces. Like the
// FixedDevicePartitioner, it will not attempt to write a partition map of any kind to the device.
// Assumes standardized partition layout structure (e.g. ZIRCON-A, ZIRCON-B,
// ZIRCON-R).
class SkipBlockDevicePartitioner : public DevicePartitioner {
 public:
  static zx_status_t Initialize(fbl::unique_fd devfs_root,
                                fbl::unique_ptr<DevicePartitioner>* partitioner);

  bool UseSkipBlockInterface() const override { return true; }

  zx_status_t AddPartition(Partition partition_type, fbl::unique_fd* out_fd) const override;

  zx_status_t FindPartition(Partition partition_type, fbl::unique_fd* out_fd) const override;

  zx_status_t FinalizePartition(Partition unused) const override { return ZX_OK; }

  zx_status_t WipeFvm() const override;

  zx_status_t GetBlockSize(const fbl::unique_fd& device_fd, uint32_t* block_size) const override;

 private:
  SkipBlockDevicePartitioner(fbl::unique_fd devfs_root) : devfs_root_(std::move(devfs_root)) {}

  fbl::unique_fd devfs_root_;
};
}  // namespace paver
