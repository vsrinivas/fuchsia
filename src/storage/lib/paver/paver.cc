// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/lib/paver/paver.h"

#include <dirent.h>
#include <fcntl.h>
#include <lib/fdio/directory.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/fidl/epitaph.h>
#include <lib/fzl/vmo-mapper.h>
#include <lib/zx/channel.h>
#include <lib/zx/time.h>
#include <lib/zx/vmo.h>
#include <libgen.h>
#include <stddef.h>
#include <string.h>
#include <zircon/device/block.h>
#include <zircon/errors.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

#include <memory>
#include <string_view>
#include <utility>

#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_call.h>
#include <fbl/span.h>
#include <fbl/unique_fd.h>
#include <fs-management/fvm.h>
#include <fs-management/mount.h>
#include <zxcrypt/fdio-volume.h>

#include "src/storage/lib/paver/fvm.h"
#include "src/storage/lib/paver/pave-logging.h"
#include "src/storage/lib/paver/stream-reader.h"
#include "src/storage/lib/paver/validation.h"
#include "src/storage/lib/paver/vmo-reader.h"

#define ZXCRYPT_DRIVER_LIB "/boot/driver/zxcrypt.so"

namespace paver {
namespace {

using ::llcpp::fuchsia::paver::Asset;
using ::llcpp::fuchsia::paver::Configuration;
using ::llcpp::fuchsia::paver::WriteFirmwareResult;

// Get the architecture of the currently running platform.
inline constexpr Arch GetCurrentArch() {
#if defined(__x86_64__)
  return Arch::kX64;
#elif defined(__aarch64__)
  return Arch::kArm64;
#else
#error "Unknown arch"
#endif
}

Partition PartitionType(Configuration configuration, Asset asset) {
  switch (asset) {
    case Asset::KERNEL: {
      switch (configuration) {
        case Configuration::A:
          return Partition::kZirconA;
        case Configuration::B:
          return Partition::kZirconB;
        case Configuration::RECOVERY:
          return Partition::kZirconR;
      };
      break;
    }
    case Asset::VERIFIED_BOOT_METADATA: {
      switch (configuration) {
        case Configuration::A:
          return Partition::kVbMetaA;
        case Configuration::B:
          return Partition::kVbMetaB;
        case Configuration::RECOVERY:
          return Partition::kVbMetaR;
      };
      break;
    }
  };
  return Partition::kUnknown;
}

// Best effort attempt to see if payload contents match what is already inside
// of the partition.
bool CheckIfSame(PartitionClient* partition, const zx::vmo& vmo, size_t payload_size,
                 size_t block_size) {
  const size_t payload_size_aligned = fbl::round_up(payload_size, block_size);
  zx::vmo read_vmo;
  auto status = zx::vmo::create(fbl::round_up(payload_size_aligned, ZX_PAGE_SIZE), 0, &read_vmo);
  if (status != ZX_OK) {
    ERROR("Failed to create VMO: %s\n", zx_status_get_string(status));
    return false;
  }

  if (auto status = partition->Read(read_vmo, payload_size_aligned); status.is_error()) {
    return false;
  }

  fzl::VmoMapper first_mapper;
  fzl::VmoMapper second_mapper;

  status = first_mapper.Map(vmo, 0, 0, ZX_VM_PERM_READ);
  if (status != ZX_OK) {
    ERROR("Error mapping vmo: %s\n", zx_status_get_string(status));
    return false;
  }

  status = second_mapper.Map(read_vmo, 0, 0, ZX_VM_PERM_READ);
  if (status != ZX_OK) {
    ERROR("Error mapping vmo: %s\n", zx_status_get_string(status));
    return false;
  }
  return memcmp(first_mapper.start(), second_mapper.start(), payload_size) == 0;
}

// Returns a client for the FVM partition. If the FVM volume doesn't exist, a new
// volume will be created, without any associated children partitions.
zx::status<std::unique_ptr<PartitionClient>> GetFvmPartition(const DevicePartitioner& partitioner) {
  // FVM doesn't need content type support, use the default.
  const PartitionSpec spec(Partition::kFuchsiaVolumeManager);
  auto status = partitioner.FindPartition(spec);
  if (status.is_error()) {
    if (status.status_value() != ZX_ERR_NOT_FOUND) {
      ERROR("Failure looking for FVM partition: %s\n", status.status_string());
      return status.take_error();
    }

    LOG("Could not find FVM Partition on device. Attemping to add new partition\n");

    if (auto status = partitioner.AddPartition(spec); status.is_error()) {
      ERROR("Failure creating FVM partition: %s\n", status.status_string());
      return status.take_error();
    } else {
      return status.take_value();
    }
  } else {
    LOG("FVM Partition already exists\n");
  }
  return status.take_value();
}

zx::status<> FvmPave(const fbl::unique_fd& devfs_root, const DevicePartitioner& partitioner,
                     std::unique_ptr<fvm::ReaderInterface> payload) {
  LOG("Paving FVM partition.\n");
  auto status = GetFvmPartition(partitioner);
  if (status.is_error()) {
    return status.take_error();
  }
  std::unique_ptr<PartitionClient>& partition = status.value();

  if (partitioner.IsFvmWithinFtl()) {
    LOG("Attempting to format FTL...\n");
    zx::status<> status = partitioner.WipeFvm();
    if (status.is_error()) {
      ERROR("Failed to format FTL: %s\n", status.status_string());
    } else {
      LOG("Formatted partition successfully!\n");
    }
  }
  LOG("Streaming partitions to FVM...\n");
  {
    auto status = FvmStreamPartitions(devfs_root, std::move(partition), std::move(payload));
    if (status.is_error()) {
      ERROR("Failed to stream partitions to FVM: %s\n", status.status_string());
      return status.take_error();
    }
  }
  LOG("Completed FVM paving successfully\n");
  return zx::ok();
}

// Formats the FVM partition and returns a channel to the new volume.
zx::status<zx::channel> FormatFvm(const fbl::unique_fd& devfs_root,
                                  const DevicePartitioner& partitioner) {
  auto status = GetFvmPartition(partitioner);
  if (status.is_error()) {
    return status.take_error();
  }
  std::unique_ptr<PartitionClient> partition = std::move(status.value());

  // TODO(fxbug.dev/39753): Configuration values should come from the build or environment.
  fvm::SparseImage header = {};
  header.slice_size = 1 << 20;

  fbl::unique_fd fvm_fd(
      FvmPartitionFormat(devfs_root, partition->block_fd(), header, BindOption::Reformat));
  if (!fvm_fd) {
    ERROR("Couldn't format FVM partition\n");
    return zx::error(ZX_ERR_IO);
  }

  {
    auto status = AllocateEmptyPartitions(devfs_root, fvm_fd);
    if (status.is_error()) {
      ERROR("Couldn't allocate empty partitions\n");
      return status.take_error();
    }

    zx::channel channel;
    status =
        zx::make_status(fdio_get_service_handle(fvm_fd.release(), channel.reset_and_get_address()));
    if (status.is_error()) {
      ERROR("Couldn't get fvm handle\n");
      return zx::error(ZX_ERR_IO);
    }
    return zx::ok(std::move(channel));
  }
}

// Reads an image from disk into a vmo.
zx::status<::llcpp::fuchsia::mem::Buffer> PartitionRead(const DevicePartitioner& partitioner,
                                                        const PartitionSpec& spec) {
  LOG("Reading partition \"%s\".\n", spec.ToString().c_str());

  auto status = partitioner.FindPartition(spec);
  if (status.is_error()) {
    ERROR("Could not find \"%s\" Partition on device: %s\n", spec.ToString().c_str(),
          status.status_string());
    return status.take_error();
  }
  std::unique_ptr<PartitionClient>& partition = status.value();

  auto status2 = partition->GetPartitionSize();
  if (status2.is_error()) {
    ERROR("Error getting partition \"%s\" size: %s\n", spec.ToString().c_str(),
          status2.status_string());
    return status2.take_error();
  }
  const uint64_t partition_size = status2.value();

  zx::vmo vmo;
  if (auto status =
          zx::make_status(zx::vmo::create(fbl::round_up(partition_size, ZX_PAGE_SIZE), 0, &vmo));
      status.is_error()) {
    ERROR("Error creating vmo for \"%s\": %s\n", spec.ToString().c_str(), status.status_string());
    return status.take_error();
  }

  if (auto status = partition->Read(vmo, static_cast<size_t>(partition_size)); status.is_error()) {
    ERROR("Error writing partition data for \"%s\": %s\n", spec.ToString().c_str(),
          status.status_string());
    return status.take_error();
  }

  size_t asset_size = static_cast<size_t>(partition_size);
  // Try to find ZBI size if asset is a ZBI. This won't work on signed ZBI, nor vbmeta assets.
  fzl::VmoMapper mapper;
  if (zx::make_status(mapper.Map(vmo, 0, partition_size, ZX_VM_PERM_READ)).is_ok()) {
    auto data = fbl::Span(static_cast<uint8_t*>(mapper.start()), mapper.size());
    const zbi_header_t* container_header;
    fbl::Span<const uint8_t> container_data;
    if (ExtractZbiPayload(data, &container_header, &container_data)) {
      asset_size = container_data.size();
    }
  }

  LOG("Completed successfully\n");
  return zx::ok(::llcpp::fuchsia::mem::Buffer{std::move(vmo), asset_size});
}

zx::status<> ValidatePartitionPayload(const DevicePartitioner& partitioner,
                                      const zx::vmo& payload_vmo, size_t payload_size,
                                      const PartitionSpec& spec) {
  fzl::VmoMapper payload_mapper;
  auto status = zx::make_status(payload_mapper.Map(payload_vmo, 0, 0, ZX_VM_PERM_READ));
  if (status.is_error()) {
    ERROR("Could not map payload into memory: %s\n", status.status_string());
    return status.take_error();
  }
  ZX_ASSERT(payload_mapper.size() >= payload_size);

  auto payload =
      fbl::Span<const uint8_t>(static_cast<const uint8_t*>(payload_mapper.start()), payload_size);
  return partitioner.ValidatePayload(spec, payload);
}

// Paves an image onto the disk.
zx::status<> PartitionPave(const DevicePartitioner& partitioner, zx::vmo payload_vmo,
                           size_t payload_size, const PartitionSpec& spec) {
  LOG("Paving partition \"%s\".\n", spec.ToString().c_str());

  // The payload_vmo might be pager-backed. Commit its pages first before using it for
  // block writes below, to avoid deadlocks in the block server. If all the pages of the
  // payload_vmo are not in memory, the block server might see a read fault in the midst of a write.
  // Read faults need to be fulfilled by the block server itself, so it will deadlock.
  //
  // Note that these pages would be committed anyway when the block server pins them for the write.
  // We're simply committing a little early here.
  //
  // If payload_vmo is pager-backed, committing its pages guarantees that they will remain in memory
  // and not be evicted only if it's a clone of a pager-backed VMO, and not a root pager-backed VMO
  // (directly backed by a pager source). Blobfs only hands out clones of root pager-backed VMOs.
  // Assert that that is indeed the case. This will cause us to fail deterministically if that
  // invariant does not hold. Otherwise, if pages get evicted in the midst of the partition write,
  // the block server can deadlock due to a read fault, putting the device in an unrecoverable
  // state.
  //
  // TODO(ZX-48145): If it's possible for payload_vmo to be a root pager-backed VMO, we will need to
  // lock it instead of simply committing its pages, to opt it out of eviction. The assert below
  // verifying that it's a pager-backed clone will need to be removed as well.
  zx_info_vmo_t info;
  auto status =
      zx::make_status(payload_vmo.get_info(ZX_INFO_VMO, &info, sizeof(info), nullptr, nullptr));
  if (status.is_error()) {
    ERROR("Failed to get info for payload VMO for partition \"%s\": %s\n", spec.ToString().c_str(),
          status.status_string());
    return status.take_error();
  }
  // If payload_vmo is pager-backed, it is a clone (has a parent).
  ZX_ASSERT(!(info.flags & ZX_INFO_VMO_PAGER_BACKED) || info.parent_koid);

  status = zx::make_status(payload_vmo.op_range(ZX_VMO_OP_COMMIT, 0, payload_size, nullptr, 0));
  if (status.is_error()) {
    ERROR("Failed to commit payload VMO for partition \"%s\": %s\n", spec.ToString().c_str(),
          status.status_string());
    return status.take_error();
  }

  // Perform basic safety checking on the partition before we attempt to write it.
  status = ValidatePartitionPayload(partitioner, payload_vmo, payload_size, spec);
  if (status.is_error()) {
    ERROR("Failed to validate partition \"%s\": %s\n", spec.ToString().c_str(),
          status.status_string());
    return status.take_error();
  }

  // Find or create the appropriate partition.
  std::unique_ptr<PartitionClient> partition;
  if (auto status = partitioner.FindPartition(spec); status.is_ok()) {
    LOG("Partition \"%s\" already exists\n", spec.ToString().c_str());
    partition = std::move(status.value());
  } else {
    if (status.error_value() != ZX_ERR_NOT_FOUND) {
      ERROR("Failure looking for partition \"%s\": %s\n", spec.ToString().c_str(),
            status.status_string());
      return status.take_error();
    }

    LOG("Could not find \"%s\" Partition on device. Attemping to add new partition\n",
        spec.ToString().c_str());

    if (auto status = partitioner.AddPartition(spec); status.is_ok()) {
      partition = std::move(status.value());
    } else {
      ERROR("Failure creating partition \"%s\": %s\n", spec.ToString().c_str(),
            status.status_string());
      return status.take_error();
    }
  }

  zx::status<size_t> status_or_size = partition->GetBlockSize();
  if (status_or_size.is_error()) {
    ERROR("Couldn't get partition \"%s\" block size\n", spec.ToString().c_str());
    return status_or_size.take_error();
  }
  const size_t block_size_bytes = status_or_size.value();

  if (CheckIfSame(partition.get(), payload_vmo, payload_size, block_size_bytes)) {
    LOG("Skipping write as partition \"%s\" contents match payload.\n", spec.ToString().c_str());
  } else {
    // Pad payload with 0s to make it block size aligned.
    if (payload_size % block_size_bytes != 0) {
      const size_t remaining_bytes = block_size_bytes - (payload_size % block_size_bytes);
      size_t vmo_size;
      if (auto status = zx::make_status(payload_vmo.get_size(&vmo_size)); status.is_error()) {
        ERROR("Couldn't get vmo size for \"%s\"\n", spec.ToString().c_str());
        return status.take_error();
      }
      // Grow VMO if it's too small.
      if (vmo_size < payload_size + remaining_bytes) {
        const auto new_size = fbl::round_up(payload_size + remaining_bytes, ZX_PAGE_SIZE);
        status = zx::make_status(payload_vmo.set_size(new_size));
        if (status.is_error()) {
          ERROR("Couldn't grow vmo for \"%s\"\n", spec.ToString().c_str());
          return status.take_error();
        }
      }
      auto buffer = std::make_unique<uint8_t[]>(remaining_bytes);
      memset(buffer.get(), 0, remaining_bytes);
      status = zx::make_status(payload_vmo.write(buffer.get(), payload_size, remaining_bytes));
      if (status.is_error()) {
        ERROR("Failed to write padding to vmo for \"%s\"\n", spec.ToString().c_str());
        return status.take_error();
      }
      payload_size += remaining_bytes;
    }
    if (auto status = partition->Write(payload_vmo, payload_size); status.is_error()) {
      ERROR("Error writing partition \"%s\" data: %s\n", spec.ToString().c_str(),
            status.status_string());
      return status.take_error();
    }
  }

  if (auto status = partitioner.FinalizePartition(spec); status.is_error()) {
    ERROR("Failed to finalize partition \"%s\"\n", spec.ToString().c_str());
    return status.take_error();
  }

  LOG("Completed paving partition \"%s\" successfully\n", spec.ToString().c_str());
  return zx::ok();
}

zx::channel OpenServiceRoot() {
  zx::channel request, service_root;
  if (zx::channel::create(0, &request, &service_root) != ZX_OK) {
    return zx::channel();
  }
  if (fdio_service_connect("/svc/.", request.release()) != ZX_OK) {
    return zx::channel();
  }
  return service_root;
}

std::optional<Configuration> SlotIndexToConfiguration(AbrSlotIndex slot_index) {
  switch (slot_index) {
    case kAbrSlotIndexA:
      return Configuration::A;
    case kAbrSlotIndexB:
      return Configuration::B;
    case kAbrSlotIndexR:
      return Configuration::RECOVERY;
  }
  ERROR("Unknown Abr slot index %d\n", static_cast<int>(slot_index));
  return std::nullopt;
}

std::optional<AbrSlotIndex> ConfigurationToSlotIndex(Configuration config) {
  switch (config) {
    case Configuration::A:
      return kAbrSlotIndexA;
    case Configuration::B:
      return kAbrSlotIndexB;
    case Configuration::RECOVERY:
      return kAbrSlotIndexR;
  }
  ERROR("Unknown configuration %d\n", static_cast<int>(config));
  return std::nullopt;
}

std::optional<Configuration> GetActiveConfiguration(const abr::Client& abr_client) {
  auto slot_index = abr_client.GetBootSlot(false, nullptr);
  return slot_index == kAbrSlotIndexR ? std::nullopt : SlotIndexToConfiguration(slot_index);
}

// Helper to wrap a std::variant with a WriteFirmwareResult union.
//
// This can go away once llcpp unions support owning memory, but until then we
// need the variant to own the underlying data.
//
// |variant| must outlive the returned WriteFirmwareResult.
WriteFirmwareResult CreateWriteFirmwareResult(
    std::variant<zx_status_t, fidl::aligned<bool>>* variant) {
  WriteFirmwareResult result;
  if (std::holds_alternative<zx_status_t>(*variant)) {
    result.set_status(fidl::unowned_ptr(&std::get<zx_status_t>(*variant)));
  } else {
    result.set_unsupported(fidl::unowned_ptr(&std::get<fidl::aligned<bool>>(*variant)));
  }
  return result;
}

}  // namespace

void Paver::FindDataSink(zx::channel data_sink, FindDataSinkCompleter::Sync _completer) {
  // Use global devfs if one wasn't injected via set_devfs_root.
  if (!devfs_root_) {
    devfs_root_ = fbl::unique_fd(open("/dev", O_RDONLY));
  }
  if (!svc_root_) {
    svc_root_ = OpenServiceRoot();
  }

  DataSink::Bind(dispatcher_, devfs_root_.duplicate(), std::move(svc_root_), std::move(data_sink),
                 context_);
}

void Paver::UseBlockDevice(zx::channel block_device, zx::channel dynamic_data_sink,
                           UseBlockDeviceCompleter::Sync _completer) {
  UseBlockDevice(std::move(block_device), std::move(dynamic_data_sink));
}

void Paver::UseBlockDevice(zx::channel block_device, zx::channel dynamic_data_sink) {
  // Use global devfs if one wasn't injected via set_devfs_root.
  if (!devfs_root_) {
    devfs_root_ = fbl::unique_fd(open("/dev", O_RDONLY));
  }
  if (!svc_root_) {
    svc_root_ = OpenServiceRoot();
  }

  DynamicDataSink::Bind(dispatcher_, devfs_root_.duplicate(), std::move(svc_root_),
                        std::move(block_device), std::move(dynamic_data_sink), context_);
}

void Paver::FindBootManager(zx::channel boot_manager, FindBootManagerCompleter::Sync _completer) {
  // Use global devfs if one wasn't injected via set_devfs_root.
  if (!devfs_root_) {
    devfs_root_ = fbl::unique_fd(open("/dev", O_RDONLY));
  }
  if (!svc_root_) {
    svc_root_ = OpenServiceRoot();
  }

  BootManager::Bind(dispatcher_, devfs_root_.duplicate(), std::move(svc_root_), context_,
                    std::move(boot_manager));
}

void DataSink::ReadAsset(::llcpp::fuchsia::paver::Configuration configuration,
                         ::llcpp::fuchsia::paver::Asset asset, ReadAssetCompleter::Sync completer) {
  auto status = sink_.ReadAsset(configuration, asset);
  if (status.is_ok()) {
    completer.ReplySuccess(std::move(status.value()));
  } else {
    completer.ReplyError(status.error_value());
  }
}

void DataSink::WriteFirmware(::llcpp::fuchsia::paver::Configuration configuration,
                             fidl::StringView type, ::llcpp::fuchsia::mem::Buffer payload,
                             WriteFirmwareCompleter::Sync completer) {
  auto variant = sink_.WriteFirmware(configuration, std::move(type), std::move(payload));
  completer.Reply(CreateWriteFirmwareResult(&variant));
}

void DataSink::WipeVolume(WipeVolumeCompleter::Sync completer) {
  auto status = sink_.WipeVolume();
  if (status.is_ok()) {
    completer.ReplySuccess(std::move(status.value()));
  } else {
    completer.ReplyError(status.error_value());
  }
}

zx::status<::llcpp::fuchsia::mem::Buffer> DataSinkImpl::ReadAsset(Configuration configuration,
                                                                  Asset asset) {
  // No assets support content types yet, use the PartitionSpec default.
  PartitionSpec spec(PartitionType(configuration, asset));

  // Important: if we ever do pass a content type here, do NOT just return
  // ZX_ERR_NOT_SUPPORTED directly - the caller needs to be able to distinguish
  // between unknown asset types (which should be ignored) and actual errors
  // that happen to return this same status code.
  if (!partitioner_->SupportsPartition(spec)) {
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }

  auto status = PartitionRead(*partitioner_, spec);
  if (status.is_error()) {
    return status.take_error();
  }
  return zx::ok(std::move(status.value()));
}

zx::status<> DataSinkImpl::WriteAsset(Configuration configuration, Asset asset,
                                      ::llcpp::fuchsia::mem::Buffer payload) {
  // No assets support content types yet, use the PartitionSpec default.
  PartitionSpec spec(PartitionType(configuration, asset));

  // Important: if we ever do pass a content type here, do NOT just return
  // ZX_ERR_NOT_SUPPORTED directly - the caller needs to be able to distinguish
  // between unknown asset types (which should be ignored) and actual errors
  // that happen to return this same status code.
  if (!partitioner_->SupportsPartition(spec)) {
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }

  return PartitionPave(*partitioner_, std::move(payload.vmo), payload.size, spec);
}

std::variant<zx_status_t, fidl::aligned<bool>> DataSinkImpl::WriteFirmware(
    Configuration configuration, fidl::StringView type, ::llcpp::fuchsia::mem::Buffer payload) {
  // Currently all our supported firmware lives in Partition::kBootloaderA/B/R.
  Partition part_type;
  switch (configuration) {
    case Configuration::A:
      part_type = Partition::kBootloaderA;
      break;
    case Configuration::B:
      part_type = Partition::kBootloaderB;
      break;
    case Configuration::RECOVERY:
      part_type = Partition::kBootloaderR;
      break;
  }
  PartitionSpec spec = PartitionSpec(part_type, std::string_view(type.data(), type.size()));

  if (partitioner_->SupportsPartition(spec)) {
    return PartitionPave(*partitioner_, std::move(payload.vmo), payload.size, spec).status_value();
  }

  // unsupported_type = true.
  return fidl::aligned<bool>(true);
}

zx::status<> DataSinkImpl::WriteVolumes(zx::channel payload_stream) {
  auto status = StreamReader::Create(std::move(payload_stream));
  if (status.is_error()) {
    ERROR("Unable to create stream.\n");
    return status.take_error();
  }
  return FvmPave(devfs_root_, *partitioner_, std::move(status.value()));
}

// Deprecated in favor of WriteFirmware().
// TODO(fxbug.dev/45606): move clients off this function and delete it.
zx::status<> DataSinkImpl::WriteBootloader(::llcpp::fuchsia::mem::Buffer payload) {
  PartitionSpec spec(Partition::kBootloaderA);

  if (!partitioner_->SupportsPartition(spec)) {
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }

  return PartitionPave(*partitioner_, std::move(payload.vmo), payload.size, spec);
}

zx::status<> DataSinkImpl::WriteDataFile(fidl::StringView filename,
                                         ::llcpp::fuchsia::mem::Buffer payload) {
  const char* mount_path = "/volume/data";
  const uint8_t data_guid[] = GUID_DATA_VALUE;
  char minfs_path[PATH_MAX] = {0};
  char path[PATH_MAX] = {0};
  zx::status<> status = zx::ok();

  fbl::unique_fd part_fd(
      open_partition_with_devfs(devfs_root_.get(), nullptr, data_guid, ZX_SEC(1), path));
  if (!part_fd) {
    ERROR("DATA partition not found in FVM\n");
    return zx::error(ZX_ERR_NOT_FOUND);
  }

  auto disk_format = detect_disk_format(part_fd.get());
  fbl::unique_fd mountpoint_dev_fd;
  // By the end of this switch statement, mountpoint_dev_fd needs to be an
  // open handle to the block device that we want to mount at mount_path.
  switch (disk_format) {
    case DISK_FORMAT_MINFS:
      // If the disk we found is actually minfs, we can just use the block
      // device path we were given by open_partition.
      strncpy(minfs_path, path, PATH_MAX);
      mountpoint_dev_fd.reset(open(minfs_path, O_RDWR));
      break;

    case DISK_FORMAT_ZXCRYPT: {
      std::unique_ptr<zxcrypt::FdioVolume> zxc_volume;
      uint8_t slot = 0;
      if (status = zx::make_status(zxcrypt::FdioVolume::UnlockWithDeviceKey(
              std::move(part_fd), devfs_root_.duplicate(), static_cast<zxcrypt::key_slot_t>(slot),
              &zxc_volume));
          status.is_error()) {
        ERROR("Couldn't unlock zxcrypt volume: %s\n", status.status_string());
        return status.take_error();
      }

      // Most of the time we'll expect the volume to actually already be
      // unsealed, because we created it and unsealed it moments ago to
      // format minfs.
      if (status = zx::make_status(zxc_volume->Open(zx::sec(0), &mountpoint_dev_fd));
          status.is_ok()) {
        // Already unsealed, great, early exit.
        break;
      }

      // Ensure zxcrypt volume manager is bound.
      zx::channel zxc_manager_chan;
      if (status = zx::make_status(
              zxc_volume->OpenManager(zx::sec(5), zxc_manager_chan.reset_and_get_address()));
          status.is_error()) {
        ERROR("Couldn't open zxcrypt volume manager: %s\n", status.status_string());
        return status.take_error();
      }

      // Unseal.
      zxcrypt::FdioVolumeManager zxc_manager(std::move(zxc_manager_chan));
      if (status = zx::make_status(zxc_manager.UnsealWithDeviceKey(slot)); status.is_error()) {
        ERROR("Couldn't unseal zxcrypt volume: %s\n", status.status_string());
        return status.take_error();
      }

      // Wait for the device to appear, and open it.
      if (status = zx::make_status(zxc_volume->Open(zx::sec(5), &mountpoint_dev_fd));
          status.is_error()) {
        ERROR("Couldn't open block device atop unsealed zxcrypt volume: %s\n",
              status.status_string());
        return status.take_error();
      }
    } break;

    default:
      ERROR("unsupported disk format at %s\n", path);
      return zx::error(ZX_ERR_NOT_SUPPORTED);
  }

  mount_options_t opts(default_mount_options);
  opts.create_mountpoint = true;
  if (status = zx::make_status(
          mount(mountpoint_dev_fd.get(), mount_path, DISK_FORMAT_MINFS, &opts, launch_logs_async));
      status.is_error()) {
    ERROR("mount error: %s\n", status.status_string());
    return status.take_error();
  }

  int filename_size = static_cast<int>(filename.size());

  // mkdir any intermediate directories between mount_path and basename(filename).
  snprintf(path, sizeof(path), "%s/%.*s", mount_path, filename_size, filename.data());
  size_t cur = strlen(mount_path);
  size_t max = strlen(path) - strlen(basename(path));
  // note: the call to basename above modifies path, so it needs reconstruction.
  snprintf(path, sizeof(path), "%s/%.*s", mount_path, filename_size, filename.data());
  while (cur < max) {
    ++cur;
    if (path[cur] == '/') {
      path[cur] = 0;
      // errors ignored, let the open() handle that later.
      mkdir(path, 0700);
      path[cur] = '/';
    }
  }

  // We append here, because the primary use case here is to send SSH keys
  // which can be appended, but we may want to revisit this choice for other
  // files in the future.
  {
    uint8_t buf[8192];
    fbl::unique_fd kfd(open(path, O_CREAT | O_WRONLY | O_APPEND, 0600));
    if (!kfd) {
      umount(mount_path);
      ERROR("open %.*s error: %s\n", filename_size, filename.data(), strerror(errno));
      return zx::error(ZX_ERR_IO);
    }
    VmoReader reader(std::move(payload));
    auto status = reader.Read(buf, sizeof(buf));
    while (status.is_ok() && status.value() > 0) {
      size_t actual = status.value();
      if (write(kfd.get(), buf, actual) != static_cast<ssize_t>(actual)) {
        umount(mount_path);
        ERROR("write %.*s error: %s\n", filename_size, filename.data(), strerror(errno));
        return zx::error(ZX_ERR_IO);
      }
      status = reader.Read(buf, sizeof(buf));
    }
    fsync(kfd.get());
  }

  if (status = zx::make_status(umount(mount_path)); status.is_error()) {
    ERROR("unmount %s failed: %s\n", mount_path, status.status_string());
    return status.take_error();
  }

  LOG("Wrote %.*s\n", filename_size, filename.data());
  return zx::ok();
}

zx::status<zx::channel> DataSinkImpl::WipeVolume() {
  auto status = GetFvmPartition(*partitioner_);
  if (status.is_error()) {
    return status.take_error();
  }
  std::unique_ptr<PartitionClient> partition = std::move(status.value());

  // Bind the FVM driver to be in a well known state regarding races with block watcher.
  // The block watcher will attempt to bind the FVM driver automatically based on
  // the contents of the partition. However, that operation is not synchronized in
  // any way with this service so the driver can be loaded at any time.
  // WipeFvm basically writes underneath that driver, which means that we should
  // eliminate the races at this point: assuming that the driver can load, either
  // this call or the block watcher will succeed (and the other one will fail),
  // but the driver will be loaded before moving on.
  TryBindToFvmDriver(devfs_root_, partition->block_fd(), zx::sec(3));

  {
    auto status = partitioner_->WipeFvm();
    if (status.is_error()) {
      ERROR("Failure wiping partition: %s\n", status.status_string());
      return status.take_error();
    }
  }

  {
    auto status = FormatFvm(devfs_root_, *partitioner_);
    if (status.is_error()) {
      ERROR("Failure formatting partition: %s\n", status.status_string());
      return status.take_error();
    }

    return zx::ok(std::move(status.value()));
  }
}

void DataSink::Bind(async_dispatcher_t* dispatcher, fbl::unique_fd devfs_root, zx::channel svc_root,
                    zx::channel server, std::shared_ptr<Context> context) {
  auto partitioner = DevicePartitionerFactory::Create(devfs_root.duplicate(), std::move(svc_root),
                                                      GetCurrentArch(), context);
  if (!partitioner) {
    ERROR("Unable to initialize a partitioner.\n");
    fidl_epitaph_write(server.get(), ZX_ERR_BAD_STATE);
    return;
  }
  auto data_sink = std::make_unique<DataSink>(std::move(devfs_root), std::move(partitioner));
  fidl::BindSingleInFlightOnly(dispatcher, std::move(server), std::move(data_sink));
}

void DynamicDataSink::Bind(async_dispatcher_t* dispatcher, fbl::unique_fd devfs_root,
                           zx::channel svc_root, zx::channel block_device, zx::channel server,
                           std::shared_ptr<Context> context) {
  auto partitioner =
      DevicePartitionerFactory::Create(devfs_root.duplicate(), std::move(svc_root),
                                       GetCurrentArch(), context, std::move(block_device));
  if (!partitioner) {
    ERROR("Unable to initialize a partitioner.\n");
    fidl_epitaph_write(server.get(), ZX_ERR_BAD_STATE);
    return;
  }
  auto data_sink = std::make_unique<DynamicDataSink>(std::move(devfs_root), std::move(partitioner));
  fidl::BindSingleInFlightOnly(dispatcher, std::move(server), std::move(data_sink));
}

void DynamicDataSink::InitializePartitionTables(
    InitializePartitionTablesCompleter::Sync completer) {
  completer.Reply(sink_.partitioner()->InitPartitionTables().status_value());
}

void DynamicDataSink::WipePartitionTables(WipePartitionTablesCompleter::Sync completer) {
  completer.Reply(sink_.partitioner()->WipePartitionTables().status_value());
}

void DynamicDataSink::ReadAsset(::llcpp::fuchsia::paver::Configuration configuration,
                                ::llcpp::fuchsia::paver::Asset asset,
                                ReadAssetCompleter::Sync completer) {
  auto status = sink_.ReadAsset(configuration, asset);
  if (status.is_ok()) {
    completer.ReplySuccess(std::move(status.value()));
  } else {
    completer.ReplyError(status.error_value());
  }
}

void DynamicDataSink::WriteFirmware(::llcpp::fuchsia::paver::Configuration configuration,
                                    fidl::StringView type, ::llcpp::fuchsia::mem::Buffer payload,
                                    WriteFirmwareCompleter::Sync completer) {
  auto variant = sink_.WriteFirmware(configuration, std::move(type), std::move(payload));
  completer.Reply(CreateWriteFirmwareResult(&variant));
}

void DynamicDataSink::WipeVolume(WipeVolumeCompleter::Sync completer) {
  auto status = sink_.WipeVolume();
  if (status.is_ok()) {
    completer.ReplySuccess(std::move(status.value()));
  } else {
    completer.ReplyError(status.error_value());
  }
}

void BootManager::Bind(async_dispatcher_t* dispatcher, fbl::unique_fd devfs_root,
                       zx::channel svc_root, std::shared_ptr<Context> context, zx::channel server) {
  auto status = abr::ClientFactory::Create(std::move(devfs_root), std::move(svc_root), context);
  if (status.is_error()) {
    ERROR("Failed to get ABR client: %s\n", status.status_string());
    fidl_epitaph_write(server.get(), status.error_value());
    return;
  }
  auto& abr_client = status.value();

  auto boot_manager = std::make_unique<BootManager>(std::move(abr_client));
  fidl::BindSingleInFlightOnly(dispatcher, std::move(server), std::move(boot_manager));
}

void BootManager::QueryActiveConfiguration(QueryActiveConfigurationCompleter::Sync completer) {
  std::optional<Configuration> config = GetActiveConfiguration(*abr_client_);
  if (!config) {
    completer.ReplyError(ZX_ERR_NOT_SUPPORTED);
    return;
  }
  completer.ReplySuccess(config.value());
}

void BootManager::QueryConfigurationStatus(Configuration configuration,
                                           QueryConfigurationStatusCompleter::Sync completer) {
  auto slot_index = ConfigurationToSlotIndex(configuration);
  auto status = slot_index ? abr_client_->GetSlotInfo(*slot_index) : zx::error(ZX_ERR_INVALID_ARGS);
  if (status.is_error()) {
    ERROR("Failed to get slot info %d\n", static_cast<uint32_t>(configuration));
    completer.ReplyError(status.error_value());
    return;
  }
  const AbrSlotInfo& slot = status.value();

  if (!slot.is_bootable) {
    completer.ReplySuccess(::llcpp::fuchsia::paver::ConfigurationStatus::UNBOOTABLE);
  } else if (slot.is_marked_successful == 0) {
    completer.ReplySuccess(::llcpp::fuchsia::paver::ConfigurationStatus::PENDING);
  } else {
    completer.ReplySuccess(::llcpp::fuchsia::paver::ConfigurationStatus::HEALTHY);
  }
}

void BootManager::SetConfigurationActive(Configuration configuration,
                                         SetConfigurationActiveCompleter::Sync completer) {
  LOG("Setting configuration %d as active\n", static_cast<uint32_t>(configuration));

  auto slot_index = ConfigurationToSlotIndex(configuration);
  auto status =
      slot_index ? abr_client_->MarkSlotActive(*slot_index) : zx::error(ZX_ERR_INVALID_ARGS);
  if (status.is_error()) {
    ERROR("Failed to set configuration: %d active\n", static_cast<uint32_t>(configuration));
    completer.Reply(status.error_value());
    return;
  }

  LOG("Set active configuration to %d\n", static_cast<uint32_t>(configuration));

  completer.Reply(ZX_OK);
}

void BootManager::SetConfigurationUnbootable(Configuration configuration,
                                             SetConfigurationUnbootableCompleter::Sync completer) {
  LOG("Setting configuration %d as unbootable\n", static_cast<uint32_t>(configuration));

  auto slot_index = ConfigurationToSlotIndex(configuration);
  auto status =
      slot_index ? abr_client_->MarkSlotUnbootable(*slot_index) : zx::error(ZX_ERR_INVALID_ARGS);
  if (status.is_error()) {
    ERROR("Failed to set configuration: %d unbootable\n", static_cast<uint32_t>(configuration));
    completer.Reply(status.error_value());
    return;
  }

  LOG("Set %d configuration as unbootable\n", static_cast<uint32_t>(configuration));

  completer.Reply(ZX_OK);
}

void BootManager::SetActiveConfigurationHealthy(
    SetActiveConfigurationHealthyCompleter::Sync completer) {
  LOG("Setting active configuration as healthy\n");

  std::optional<Configuration> config = GetActiveConfiguration(*abr_client_);
  if (!config) {
    ERROR("No configuration bootable. Cannot mark as successful boot.\n");
    completer.Reply(ZX_ERR_BAD_STATE);
    return;
  }

  if (auto status = abr_client_->MarkSlotSuccessful(*ConfigurationToSlotIndex(*config));
      status.is_error()) {
    ERROR("Failed to set configuration: %d healthy\n", static_cast<uint32_t>(*config));
    completer.Reply(status.error_value());
    return;
  }

  LOG("Set active configuration as healthy\n");

  completer.Reply(ZX_OK);
}

}  // namespace paver
