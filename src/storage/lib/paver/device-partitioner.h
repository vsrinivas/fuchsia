// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_LIB_PAVER_DEVICE_PARTITIONER_H_
#define SRC_STORAGE_LIB_PAVER_DEVICE_PARTITIONER_H_

#include <fuchsia/boot/llcpp/fidl.h>
#include <fuchsia/fshost/llcpp/fidl.h>
#include <fuchsia/hardware/block/llcpp/fidl.h>
#include <fuchsia/paver/llcpp/fidl.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/fdio/directory.h>
#include <lib/zx/channel.h>
#include <lib/zx/status.h>
#include <stdbool.h>
#include <zircon/types.h>

#include <memory>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include <fbl/function.h>
#include <fbl/span.h>
#include <fbl/string.h>
#include <fbl/unique_fd.h>
#include <gpt/gpt.h>

#include "partition-client.h"
#include "paver-context.h"

namespace paver {
using gpt::GptDevice;
using ::llcpp::fuchsia::paver::Configuration;

enum class Partition {
  kUnknown,
  kBootloader,
  kZirconA,
  kZirconB,
  kZirconR,
  kVbMetaA,
  kVbMetaB,
  kVbMetaR,
  kAbrMeta,
  kFuchsiaVolumeManager,
};

const char* PartitionName(Partition type);

enum class Arch {
  kX64,
  kArm64,
};

// This class pauses the block watcher when it is Create()d, and
// resumes it when the destructor is called.
class BlockWatcherPauser {
 public:
  BlockWatcherPauser(BlockWatcherPauser&& other)
      : watcher_(std::move(other.watcher_)), valid_(other.valid_) {
    other.valid_ = false;
  }
  // Destructor for the pauser, which automatically resumes the watcher.
  ~BlockWatcherPauser();

  // This is the function used for creating the BlockWatcherPauser.
  static zx::status<BlockWatcherPauser> Create(const zx::channel& svc_root);

 private:
  // Create a new Pauser. This should immediately be followed by a call to Pause().
  BlockWatcherPauser(zx::channel chan)
      : watcher_(llcpp::fuchsia::fshost::BlockWatcher::SyncClient(std::move(chan))),
        valid_(false) {}
  zx::status<> Pause();

  llcpp::fuchsia::fshost::BlockWatcher::SyncClient watcher_;
  bool valid_;
};

// Operations on a specific partition take two identifiers, a partition type
// and a content type.
//
// The first is the conceptual partition type. This may not necessarily map 1:1
// with on-disk partitions. For example, some devices have multiple bootloader
// stages which live in different partitions on-disk but which would both have
// type kBootloader.
//
// The second is a device-specific string identifying the contents the caller
// wants to read/write. The backend uses this to decide which on-disk partitions
// to use and where the content lives in them. The default content type is
// null or empty.
struct PartitionSpec {
 public:
  // Creates a spec with the given partition and default (null) content type.
  explicit PartitionSpec(Partition partition) : PartitionSpec(partition, std::string_view()) {}

  // Creates a spec with the given partition and content type.
  PartitionSpec(Partition partition, std::string_view content_type)
      : partition(partition), content_type(content_type) {}

  // Returns a descriptive string for logging.
  fbl::String ToString() const;

  Partition partition;
  std::string_view content_type;
};

// Abstract device partitioner definition.
// This class defines common APIs for interacting with a device partitioner.
class DevicePartitioner {
 public:
  // Factory method which automatically returns the correct DevicePartitioner
  // implementation. Returns nullptr on failure.
  // |block_device| is root block device whichs contains the logical partitions we wish to operate
  // against. It's only meaningful for EFI and CROS devices which may have multiple storage devices.
  static std::unique_ptr<DevicePartitioner> Create(fbl::unique_fd devfs_root,
                                                   const zx::channel& svc_root, Arch arch,
                                                   std::shared_ptr<Context> context,
                                                   zx::channel block_device = zx::channel());

  virtual ~DevicePartitioner() = default;

  // Whether or not the Fuchsia Volume Manager exists within an FTL.
  virtual bool IsFvmWithinFtl() const = 0;

  // Checks if the device supports the given partition spec.
  //
  // This is the only function that will definitively say whether a spec is
  // supported or not. Other partition functions may return ZX_ERR_NOT_SUPPORTED
  // on unsupported spec, but they may also return it for other reasons such as
  // a lower-level driver error. They also may return other errors even if given
  // an unsupported spec if something else goes wong.
  virtual bool SupportsPartition(const PartitionSpec& spec) const = 0;

  // Returns a PartitionClient matching |spec|, creating the partition.
  // Assumes that the partition does not already exist.
  virtual zx::status<std::unique_ptr<PartitionClient>> AddPartition(
      const PartitionSpec& spec) const = 0;

  // Returns a PartitionClient matching |spec| if one exists.
  virtual zx::status<std::unique_ptr<PartitionClient>> FindPartition(
      const PartitionSpec& spec) const = 0;

  // Finalizes the PartitionClient matching |spec| after it has been written.
  virtual zx::status<> FinalizePartition(const PartitionSpec& spec) const = 0;

  // Wipes Fuchsia Volume Manager partition.
  virtual zx::status<> WipeFvm() const = 0;

  // Initializes partition tables.
  virtual zx::status<> InitPartitionTables() const = 0;

  // Wipes partition tables.
  virtual zx::status<> WipePartitionTables() const = 0;

  // Determine if the given data file is a valid image for this device.
  //
  // This analysis is best-effort only, providing only basic safety checks.
  virtual zx::status<> ValidatePayload(const PartitionSpec& spec,
                                       fbl::Span<const uint8_t> data) const = 0;

  // Flush all buffered write to persistant storage.
  virtual zx::status<> Flush() const = 0;
};

// Useful for when a GPT table is available (e.g. x86 devices). Provides common
// utility functions.
class GptDevicePartitioner {
 public:
  using FilterCallback = fbl::Function<bool(const gpt_partition_t&)>;

  struct InitializeGptResult {
    std::unique_ptr<GptDevicePartitioner> gpt;
    bool initialize_partition_tables;
  };

  // Find and initialize a GPT based device.
  //
  // If block_device is provided, then search is skipped, and block_device is used
  // directly. If it is not provided, we search for a device with a valid GPT,
  // with an entry for an FVM. If multiple devices with valid GPT containing
  // FVM entries are found, an error is returned.
  static zx::status<InitializeGptResult> InitializeGpt(fbl::unique_fd devfs_root,
                                                       const zx::channel& svc_root,
                                                       std::optional<fbl::unique_fd> block_device);

  // Returns block info for a specified block device.
  const ::llcpp::fuchsia::hardware::block::BlockInfo& GetBlockInfo() const { return block_info_; }

  GptDevice* GetGpt() const { return gpt_.get(); }
  zx::unowned_channel Channel() const { return caller_.channel(); }

  struct FindFirstFitResult {
    size_t start;
    size_t length;
  };

  // Find the first spot that has at least |bytes_requested| of space.
  //
  // Returns the |start_out| block and |length_out| blocks, indicating
  // how much space was found, on success. This may be larger than
  // the number of bytes requested.
  zx::status<FindFirstFitResult> FindFirstFit(size_t bytes_requested) const;

  // Creates a partition, adds an entry to the GPT, and returns a file descriptor to it.
  // Assumes that the partition does not already exist.
  zx::status<std::unique_ptr<PartitionClient>> AddPartition(const char* name, const uint8_t* type,
                                                            size_t minimum_size_bytes,
                                                            size_t optional_reserve_bytes) const;

  struct FindPartitionResult {
    std::unique_ptr<PartitionClient> partition;
    gpt_partition_t* gpt_partition;
  };

  // Returns a file descriptor to a partition which can be paved,
  // if one exists.
  zx::status<FindPartitionResult> FindPartition(FilterCallback filter) const;

  // Wipes a specified partition from the GPT, and overwrites first 8KiB with
  // nonsense.
  zx::status<> WipeFvm() const;

  // Removes all partitions from GPT.
  zx::status<> WipePartitionTables() const;

  // Wipes all partitions meeting given criteria.
  zx::status<> WipePartitions(FilterCallback filter) const;

  const fbl::unique_fd& devfs_root() { return devfs_root_; }

  const zx::channel& svc_root() { return svc_root_; }

 private:
  using GptDevices = std::vector<std::pair<std::string, fbl::unique_fd>>;

  // Find all block devices which could contain a GPT.
  static bool FindGptDevices(const fbl::unique_fd& devfs_root, GptDevices* out);

  // Initializes GPT for a device which was explicitly provided. If |gpt_device| doesn't have a
  // valid GPT, it will initialize it with a valid one.
  static zx::status<std::unique_ptr<GptDevicePartitioner>> InitializeProvidedGptDevice(
      fbl::unique_fd devfs_root, const zx::channel& svc_root, fbl::unique_fd gpt_device);

  GptDevicePartitioner(fbl::unique_fd devfs_root, const zx::channel& svc_root, fbl::unique_fd fd,
                       std::unique_ptr<GptDevice> gpt,
                       ::llcpp::fuchsia::hardware::block::BlockInfo block_info)
      : devfs_root_(std::move(devfs_root)),
        svc_root_(fdio_service_clone(svc_root.get())),
        caller_(std::move(fd)),
        gpt_(std::move(gpt)),
        block_info_(block_info) {}

  zx::status<std::array<uint8_t, GPT_GUID_LEN>> CreateGptPartition(const char* name,
                                                                   const uint8_t* type,
                                                                   uint64_t offset,
                                                                   uint64_t blocks) const;

  fbl::unique_fd devfs_root_;
  zx::channel svc_root_;
  fdio_cpp::FdioCaller caller_;
  mutable std::unique_ptr<GptDevice> gpt_;
  ::llcpp::fuchsia::hardware::block::BlockInfo block_info_;
};

// DevicePartitioner implementation for EFI based devices.
class EfiDevicePartitioner : public DevicePartitioner {
 public:
  static zx::status<std::unique_ptr<DevicePartitioner>> Initialize(
      fbl::unique_fd devfs_root, const zx::channel& svc_root, Arch arch,
      std::optional<fbl::unique_fd> block_device);

  bool IsFvmWithinFtl() const override { return false; }

  bool SupportsPartition(const PartitionSpec& spec) const override;

  zx::status<std::unique_ptr<PartitionClient>> AddPartition(
      const PartitionSpec& spec) const override;

  zx::status<std::unique_ptr<PartitionClient>> FindPartition(
      const PartitionSpec& spec) const override;

  zx::status<> FinalizePartition(const PartitionSpec& spec) const override;

  zx::status<> WipeFvm() const override;

  zx::status<> InitPartitionTables() const override;

  zx::status<> WipePartitionTables() const override;

  zx::status<> ValidatePayload(const PartitionSpec& spec,
                               fbl::Span<const uint8_t> data) const override;

  zx::status<> Flush() const override { return zx::ok(); }

 private:
  EfiDevicePartitioner(Arch arch, std::unique_ptr<GptDevicePartitioner> gpt)
      : gpt_(std::move(gpt)), arch_(arch) {}

  std::unique_ptr<GptDevicePartitioner> gpt_;
  Arch arch_;
};

// DevicePartitioner implementation for ChromeOS devices.
class CrosDevicePartitioner : public DevicePartitioner {
 public:
  static zx::status<std::unique_ptr<DevicePartitioner>> Initialize(
      fbl::unique_fd devfs_root, const zx::channel& svc_root, Arch arch,
      std::optional<fbl::unique_fd> block_device);

  bool IsFvmWithinFtl() const override { return false; }

  bool SupportsPartition(const PartitionSpec& spec) const override;

  zx::status<std::unique_ptr<PartitionClient>> AddPartition(
      const PartitionSpec& spec) const override;

  zx::status<std::unique_ptr<PartitionClient>> FindPartition(
      const PartitionSpec& spec) const override;

  zx::status<> FinalizePartition(const PartitionSpec& spec) const override;

  zx::status<> WipeFvm() const override;

  zx::status<> InitPartitionTables() const override;

  zx::status<> WipePartitionTables() const override;

  zx::status<> ValidatePayload(const PartitionSpec& spec,
                               fbl::Span<const uint8_t> data) const override;

  zx::status<> Flush() const override { return zx::ok(); }

 private:
  CrosDevicePartitioner(std::unique_ptr<GptDevicePartitioner> gpt) : gpt_(std::move(gpt)) {}

  std::unique_ptr<GptDevicePartitioner> gpt_;
};

// DevicePartitioner implementation for devices which have fixed partition maps (e.g. ARM
// devices). It will not attempt to write a partition map of any kind to the device.
// Assumes standardized partition layout structure (e.g. ZIRCON-A, ZIRCON-B,
// ZIRCON-R).
class FixedDevicePartitioner : public DevicePartitioner {
 public:
  static zx::status<std::unique_ptr<DevicePartitioner>> Initialize(fbl::unique_fd devfs_root);

  bool IsFvmWithinFtl() const override { return false; }

  bool SupportsPartition(const PartitionSpec& spec) const override;

  zx::status<std::unique_ptr<PartitionClient>> AddPartition(
      const PartitionSpec& spec) const override;

  zx::status<std::unique_ptr<PartitionClient>> FindPartition(
      const PartitionSpec& spec) const override;

  zx::status<> FinalizePartition(const PartitionSpec& spec) const override { return zx::ok(); }

  zx::status<> WipeFvm() const override;

  zx::status<> InitPartitionTables() const override;

  zx::status<> WipePartitionTables() const override;

  zx::status<> ValidatePayload(const PartitionSpec& spec,
                               fbl::Span<const uint8_t> data) const override;

  zx::status<> Flush() const override { return zx::ok(); }

 private:
  FixedDevicePartitioner(fbl::unique_fd devfs_root) : devfs_root_(std::move(devfs_root)) {}

  fbl::unique_fd devfs_root_;
};

// DevicePartitioner implementation for devices which have fixed partition maps, but do not expose a
// block device interface. Instead they expose devices with skip-block IOCTL interfaces. Like the
// FixedDevicePartitioner, it will not attempt to write a partition map of any kind to the device.
// Assumes standardized partition layout structure (e.g. ZIRCON-A, ZIRCON-B,
// ZIRCON-R).
class SkipBlockDevicePartitioner {
 public:
  SkipBlockDevicePartitioner(fbl::unique_fd devfs_root) : devfs_root_(std::move(devfs_root)) {}

  zx::status<std::unique_ptr<PartitionClient>> FindPartition(const uint8_t* guid) const;

  zx::status<std::unique_ptr<PartitionClient>> FindFvmPartition() const;

  zx::status<> WipeFvm() const;

  fbl::unique_fd& devfs_root() { return devfs_root_; }

 private:
  fbl::unique_fd devfs_root_;
};

class AstroPartitioner : public DevicePartitioner {
 public:
  enum class AbrWearLevelingOption { ON, OFF };

  static zx::status<std::unique_ptr<DevicePartitioner>> Initialize(
      fbl::unique_fd devfs_root, const zx::channel& svc_root, std::shared_ptr<Context> context);

  bool IsFvmWithinFtl() const override { return true; }

  bool SupportsPartition(const PartitionSpec& spec) const override;

  zx::status<std::unique_ptr<PartitionClient>> AddPartition(
      const PartitionSpec& spec) const override;

  zx::status<std::unique_ptr<PartitionClient>> FindPartition(
      const PartitionSpec& spec) const override;

  zx::status<> FinalizePartition(const PartitionSpec& spec) const override { return zx::ok(); }

  zx::status<> WipeFvm() const override;

  zx::status<> InitPartitionTables() const override;

  zx::status<> WipePartitionTables() const override;

  zx::status<> ValidatePayload(const PartitionSpec& spec,
                               fbl::Span<const uint8_t> data) const override;

  zx::status<> Flush() const override;

 private:
  AstroPartitioner(std::unique_ptr<SkipBlockDevicePartitioner> skip_block,
                   std::shared_ptr<Context> context)
      : skip_block_(std::move(skip_block)), context_(context) {}

  static zx::status<> InitializeContext(const fbl::unique_fd& devfs_root,
                                        AbrWearLevelingOption abr_wear_leveling_opt,
                                        std::shared_ptr<Context> context);

  static bool CanSafelyUpdateLayout(std::shared_ptr<Context> context);

  std::unique_ptr<SkipBlockDevicePartitioner> skip_block_;

  std::shared_ptr<Context> context_;
};

class As370Partitioner : public DevicePartitioner {
 public:
  static zx::status<std::unique_ptr<DevicePartitioner>> Initialize(fbl::unique_fd devfs_root);

  bool IsFvmWithinFtl() const override { return true; }

  bool SupportsPartition(const PartitionSpec& spec) const override;

  zx::status<std::unique_ptr<PartitionClient>> AddPartition(
      const PartitionSpec& spec) const override;

  zx::status<std::unique_ptr<PartitionClient>> FindPartition(
      const PartitionSpec& spec) const override;

  zx::status<> FinalizePartition(const PartitionSpec& spec) const override { return zx::ok(); }

  zx::status<> WipeFvm() const override;

  zx::status<> InitPartitionTables() const override;

  zx::status<> WipePartitionTables() const override;

  zx::status<> ValidatePayload(const PartitionSpec& spec,
                               fbl::Span<const uint8_t> data) const override;

  zx::status<> Flush() const override { return zx::ok(); }

 private:
  As370Partitioner(std::unique_ptr<SkipBlockDevicePartitioner> skip_block)
      : skip_block_(std::move(skip_block)) {}

  std::unique_ptr<SkipBlockDevicePartitioner> skip_block_;
};

class SherlockPartitioner : public DevicePartitioner {
 public:
  static zx::status<std::unique_ptr<DevicePartitioner>> Initialize(
      fbl::unique_fd devfs_root, const zx::channel& svc_root,
      std::optional<fbl::unique_fd> block_device);

  bool IsFvmWithinFtl() const override { return false; }

  bool SupportsPartition(const PartitionSpec& spec) const override;

  zx::status<std::unique_ptr<PartitionClient>> AddPartition(
      const PartitionSpec& spec) const override;

  zx::status<std::unique_ptr<PartitionClient>> FindPartition(
      const PartitionSpec& spec) const override;

  zx::status<> FinalizePartition(const PartitionSpec& spec) const override { return zx::ok(); }

  zx::status<> WipeFvm() const override;

  zx::status<> InitPartitionTables() const override;

  zx::status<> WipePartitionTables() const override;

  zx::status<> ValidatePayload(const PartitionSpec& spec,
                               fbl::Span<const uint8_t> data) const override;

  zx::status<> Flush() const override { return zx::ok(); }

 private:
  SherlockPartitioner(std::unique_ptr<GptDevicePartitioner> gpt) : gpt_(std::move(gpt)) {}

  std::unique_ptr<GptDevicePartitioner> gpt_;
};

}  // namespace paver

#endif  // SRC_STORAGE_LIB_PAVER_DEVICE_PARTITIONER_H_
