// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/bin/linux_runner/block_devices.h"

#include <dirent.h>
#include <fcntl.h>
#include <fuchsia/hardware/block/volume/cpp/fidl.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/namespace.h>
#include <lib/fit/defer.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>
#include <zircon/hw/gpt.h>

namespace {

constexpr size_t kNumRetries = 5;
constexpr auto kRetryDelay = zx::msec(100);

constexpr const char kBlockPath[] = "/dev/class/block";
constexpr auto kGuidSize = fuchsia::hardware::block::partition::GUID_LENGTH;
constexpr const char kGuestPartitionName[] = "guest";
constexpr std::array<uint8_t, kGuidSize> kGuestPartitionGuid = {
    0x9a, 0x17, 0x7d, 0x2d, 0x8b, 0x24, 0x4a, 0x4c, 0x87, 0x11, 0x1f, 0x99, 0x05, 0xb7, 0x6e, 0xd1,
};
constexpr std::array<uint8_t, kGuidSize> kFvmGuid = GUID_FVM_VALUE;
constexpr std::array<uint8_t, kGuidSize> kGptFvmGuid = GPT_FVM_TYPE_GUID;

using VolumeHandle = fidl::InterfaceHandle<fuchsia::hardware::block::volume::Volume>;
using ManagerHandle = fidl::InterfaceHandle<fuchsia::hardware::block::volume::VolumeManager>;

// Information about a disk image.
struct DiskImage {
  const char* path;                             // Path to the file containing the image
  fuchsia::virtualization::BlockFormat format;  // Format of the disk image
  bool read_only;
};

#if defined(USE_VOLATILE_BLOCK)
constexpr bool kForceVolatileWrites = true;
#else
constexpr bool kForceVolatileWrites = false;
#endif

#ifdef USE_PREBUILT_STATEFUL_IMAGE
constexpr DiskImage kStatefulImage = DiskImage{
    .path = "/pkg/data/stateful.qcow2",
    .format = fuchsia::virtualization::BlockFormat::QCOW,
    .read_only = true,
};
#else
constexpr DiskImage kStatefulImage = DiskImage{
    .format = fuchsia::virtualization::BlockFormat::BLOCK,
    .read_only = false,
};
#endif

constexpr DiskImage kExtrasImage = DiskImage{
    .path = "/pkg/data/extras.img",
    .format = fuchsia::virtualization::BlockFormat::FILE,
    .read_only = true,
};

// Finds the guest FVM partition, and the FVM GPT partition.
zx::status<std::tuple<VolumeHandle, ManagerHandle>> FindPartitions(DIR* dir) {
  VolumeHandle volume;
  ManagerHandle manager;

  fdio_cpp::UnownedFdioCaller caller(dirfd(dir));
  for (dirent* entry; (entry = readdir(dir)) != nullptr;) {
    fuchsia::hardware::block::partition::PartitionSyncPtr partition;
    zx_status_t status = fdio_service_connect_at(caller.borrow_channel(), entry->d_name,
                                                 partition.NewRequest().TakeChannel().release());
    if (status != ZX_OK) {
      FX_LOGS(ERROR) << "Failed to connect to '" << entry->d_name
                     << "': " << zx_status_get_string(status);
      return zx::error(status);
    }

    zx_status_t guid_status;
    std::unique_ptr<fuchsia::hardware::block::partition::GUID> guid;
    status = partition->GetTypeGuid(&guid_status, &guid);
    if (status != ZX_OK || guid_status != ZX_OK || !guid) {
      continue;
    } else if (std::equal(kGuestPartitionGuid.begin(), kGuestPartitionGuid.end(),
                          guid->value.begin())) {
      // If we find the guest FVM partition, then we can break out of the loop.
      // We only need to find the FVM GPT partition if there is no guest FVM
      // partition, in order to create the guest FVM partition.
      volume.set_channel(partition.Unbind().TakeChannel());
      break;
    } else if (std::equal(kFvmGuid.begin(), kFvmGuid.end(), guid->value.begin()) ||
               std::equal(kGptFvmGuid.begin(), kGptFvmGuid.end(), guid->value.begin())) {
      fuchsia::device::ControllerSyncPtr controller;
      controller.Bind(partition.Unbind().TakeChannel());
      fuchsia::device::Controller_GetTopologicalPath_Result topo_result;
      status = controller->GetTopologicalPath(&topo_result);
      if (status != ZX_OK || topo_result.is_err()) {
        FX_LOGS(ERROR) << "Failed to get topological path for '" << entry->d_name << "'";
        return zx::error(ZX_ERR_IO);
      }

      auto fvm_path = topo_result.response().path + "/fvm";
      status = fdio_service_connect(fvm_path.data(), manager.NewRequest().TakeChannel().release());
      if (status != ZX_OK) {
        FX_LOGS(ERROR) << "Failed to connect to '" << fvm_path
                       << "': " << zx_status_get_string(status);
        return zx::error(status);
      }
    }
  }

  return zx::ok(std::make_tuple(std::move(volume), std::move(manager)));
}

// Waits for the guest partition to be allocated.
//
// TODO(fxbug.dev/90469): Use a directory watcher instead of scanning for
// new partitions.
zx::status<VolumeHandle> WaitForPartition(DIR* dir) {
  for (size_t retry = 0; retry != kNumRetries; retry++) {
    auto partitions = FindPartitions(dir);
    if (partitions.is_error()) {
      return partitions.take_error();
    }
    auto& [volume, manager] = *partitions;
    if (volume) {
      return zx::ok(std::move(volume));
    }
    zx::nanosleep(zx::deadline_after(kRetryDelay));
  }
  FX_LOGS(ERROR) << "Failed to create guest partition";
  return zx::error(ZX_ERR_IO);
}

// Locates the FVM partition for a guest block device. If a partition does not
// exist, allocate one.
zx::status<VolumeHandle> FindOrAllocatePartition(std::string_view path, size_t partition_size) {
  auto dir = opendir(path.data());
  if (dir == nullptr) {
    FX_LOGS(ERROR) << "Failed to open directory '" << path << "'";
    return zx::error(ZX_ERR_IO);
  }
  auto defer = fit::defer([dir] { closedir(dir); });

  auto partitions = FindPartitions(dir);
  if (partitions.is_error()) {
    return partitions.take_error();
  }
  auto& [volume, manager] = *partitions;

  if (!volume) {
    if (!manager) {
      FX_LOGS(ERROR) << "Failed to find FVM";
      return zx::error(ZX_ERR_NOT_FOUND);
    }
    auto sync = manager.BindSync();
    zx_status_t info_status = ZX_OK;
    // Get the partition slice size.
    std::unique_ptr<fuchsia::hardware::block::volume::VolumeManagerInfo> info;
    zx_status_t status = sync->GetInfo(&info_status, &info);
    if (status != ZX_OK || info_status != ZX_OK) {
      FX_LOGS(ERROR) << "Failed to get volume info: " << zx_status_get_string(status) << " and "
                     << zx_status_get_string(info_status);
      return zx::error(ZX_ERR_IO);
    }
    size_t slices = partition_size / info->slice_size;
    zx_status_t part_status = ZX_OK;
    status = sync->AllocatePartition(slices, {.value = kGuestPartitionGuid}, {},
                                     kGuestPartitionName, 0, &part_status);
    if (status != ZX_OK || part_status != ZX_OK) {
      FX_LOGS(ERROR) << "Failed to allocate partition: " << zx_status_get_string(status) << " and "
                     << zx_status_get_string(part_status);
      return zx::error(ZX_ERR_IO);
    }
    return WaitForPartition(dir);
  }

  return zx::ok(std::move(volume));
}

// Opens the given disk image.
zx::status<fuchsia::io::FileHandle> GetPartition(const DiskImage& image) {
  TRACE_DURATION("linux_runner", "GetPartition");
  fuchsia::io::OpenFlags flags = fuchsia::io::OpenFlags::RIGHT_READABLE;
  if (!image.read_only) {
    flags |= fuchsia::io::OpenFlags::RIGHT_WRITABLE;
  }
  fuchsia::io::FileHandle file;
  zx_status_t status = fdio_open(image.path, static_cast<uint32_t>(flags),
                                 file.NewRequest().TakeChannel().release());
  if (status) {
    return zx::error(status);
  }
  return zx::ok(std::move(file));
}

}  // namespace

fitx::result<std::string, std::vector<fuchsia::virtualization::BlockSpec>> GetBlockDevices(
    size_t stateful_image_size) {
  TRACE_DURATION("linux_runner", "Guest::GetBlockDevices");

  std::vector<fuchsia::virtualization::BlockSpec> devices;

  // Get/create the stateful partition.
  zx::channel stateful;
  if (kStatefulImage.format == fuchsia::virtualization::BlockFormat::BLOCK) {
    auto handle = FindOrAllocatePartition(kBlockPath, stateful_image_size);
    if (handle.is_error()) {
      return fitx::error("Failed to find or allocate a partition");
    }
    stateful = handle->TakeChannel();
  } else {
    auto handle = GetPartition(kStatefulImage);
    if (handle.is_error()) {
      return fitx::error("Failed to open or create stateful file");
    }
    stateful = handle->TakeChannel();
  }
  devices.push_back({
      .id = "stateful",
      .mode = (kStatefulImage.read_only || kForceVolatileWrites)
                  ? fuchsia::virtualization::BlockMode::VOLATILE_WRITE
                  : fuchsia::virtualization::BlockMode::READ_WRITE,
      .format = kStatefulImage.format,
      .client = std::move(stateful),
  });

  // Drop access to /dev, in order to prevent any further access.
  fdio_ns_t* ns;
  zx_status_t status = fdio_ns_get_installed(&ns);
  FX_CHECK(status == ZX_OK) << "Failed to get installed namespace";
  if (fdio_ns_is_bound(ns, "/dev")) {
    status = fdio_ns_unbind(ns, "/dev");
    FX_CHECK(status == ZX_OK) << "Failed to unbind '/dev' from the installed namespace";
  }

  // Add the extras partition if it exists.
  auto extras = GetPartition(kExtrasImage);
  if (extras.is_ok()) {
    devices.push_back({
        .id = "extras",
        .mode = fuchsia::virtualization::BlockMode::VOLATILE_WRITE,
        .format = kExtrasImage.format,
        .client = extras->TakeChannel(),
    });
  }

  return fitx::success(std::move(devices));
}
