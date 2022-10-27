// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/storage/fs_management/cpp/fvm.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fidl/fuchsia.device/cpp/wire.h>
#include <fidl/fuchsia.hardware.block.volume/cpp/wire.h>
#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/limits.h>
#include <lib/fdio/vfs.h>
#include <lib/fdio/watcher.h>
#include <lib/fit/defer.h>
#include <lib/stdcompat/string_view.h>
#include <lib/sys/component/cpp/service_client.h>
#include <string.h>
#include <unistd.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>
#include <zircon/device/block.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>

#include <algorithm>
#include <memory>
#include <utility>

#include <fbl/string_printf.h>
#include <fbl/unique_fd.h>

#include "src/lib/storage/block_client/cpp/remote_block_device.h"
#include "src/lib/storage/fs_management/cpp/format.h"
#include "src/lib/storage/fs_management/cpp/fvm_internal.h"
#include "src/storage/fvm/fvm.h"

namespace fs_management {

namespace {

constexpr char kBlockDevPath[] = "/dev/class/block/";
constexpr char kBlockDevRelativePath[] = "class/block/";

constexpr int64_t kOpenPartitionTimeout = ZX_SEC(30);

// Overwrites the FVM and waits for it to disappear from devfs.
//
// devfs_root_fd: (OPTIONAL) A connection to devfs. If supplied, |path| is relative to this root.
// parent_fd: An fd to the parent of the FVM device.
// path: The path to the FVM device. Relative to |devfs_root_fd| if supplied.
zx_status_t DestroyFvmAndWait(int devfs_root_fd, fbl::unique_fd parent_fd, fbl::unique_fd driver_fd,
                              const char* path) {
  auto volume_info_or = fs_management::FvmQuery(driver_fd.get());
  if (volume_info_or.is_error()) {
    return ZX_ERR_WRONG_TYPE;
  }

  struct FvmDestroyer {
    int devfs_root_fd;
    uint64_t slice_size;
    const char* path;
    bool destroyed;
  } destroyer;
  destroyer.devfs_root_fd = devfs_root_fd;
  destroyer.slice_size = volume_info_or->slice_size;
  destroyer.path = path;
  destroyer.destroyed = false;

  auto cb = [](int dirfd, int event, const char* fn, void* cookie) {
    auto destroyer = static_cast<FvmDestroyer*>(cookie);
    if (event == WATCH_EVENT_WAITING) {
      zx_status_t status = ZX_ERR_INTERNAL;
      if (destroyer->devfs_root_fd != -1) {
        status =
            FvmOverwriteWithDevfs(destroyer->devfs_root_fd, destroyer->path, destroyer->slice_size);
      } else {
        status = FvmOverwrite(destroyer->path, destroyer->slice_size);
      }
      destroyer->destroyed = true;
      return status;
    }
    if ((event == WATCH_EVENT_REMOVE_FILE) && !strcmp(fn, "fvm")) {
      return ZX_ERR_STOP;
    }
    return ZX_OK;
  };
  if (zx_status_t status =
          fdio_watch_directory(parent_fd.get(), cb, zx::time::infinite().get(), &destroyer);
      status != ZX_ERR_STOP) {
    return status;
  }
  return ZX_OK;
}

// Helper function to overwrite FVM given the slice_size
zx_status_t FvmOverwriteImpl(fidl::UnownedClientEnd<fuchsia_hardware_block::Block> device,
                             size_t slice_size) {
  const fidl::WireResult result = fidl::WireCall(device)->GetInfo();
  if (!result.ok()) {
    return result.status();
  }
  const fidl::WireResponse response = result.value();
  if (zx_status_t status = response.status; status != ZX_OK) {
    return status;
  }
  const fuchsia_hardware_block::wire::BlockInfo& block_info = *response.info;

  size_t disk_size = block_info.block_count * block_info.block_size;
  fvm::Header header = fvm::Header::FromDiskSize(fvm::kMaxUsablePartitions, disk_size, slice_size);

  // Overwrite all the metadata from the beginning of the device to the start of the data.
  // TODO(jfsulliv) Use MetadataBuffer::BytesNeeded() when that's ready.
  size_t metadata_size = header.GetDataStartOffset();
  std::unique_ptr<uint8_t[]> buf(new uint8_t[metadata_size]);
  memset(buf.get(), 0, metadata_size);

  if (block_client::SingleWriteBytes(device, buf.get(), metadata_size, 0) != ZX_OK) {
    fprintf(stderr, "FvmOverwriteImpl: Failed to write metadata\n");
    return ZX_ERR_IO;
  }

  {
    const fidl::WireResult result = fidl::WireCall(device)->RebindDevice();
    if (!result.ok()) {
      return result.status();
    }
    const fidl::WireResponse response = result.value();
    return response.status;
  }
}

zx_status_t FvmAllocatePartitionImpl(int fvm_fd, const alloc_req_t* request) {
  fdio_cpp::UnownedFdioCaller caller(fvm_fd);

  fuchsia_hardware_block_partition::wire::Guid type_guid;
  memcpy(type_guid.value.data(), request->type, BLOCK_GUID_LEN);
  fuchsia_hardware_block_partition::wire::Guid instance_guid;
  memcpy(instance_guid.value.data(), request->guid, BLOCK_GUID_LEN);

  // TODO(fxbug.dev/52757): Add name_size to alloc_req_t.
  //
  // Here, we rely on request->name being a C-style string terminated by \x00,
  // but no greater than BLOCK_NAME_LEN. Instead, we should add a name_size
  // field to the alloc_req_t object to pass this explicitly.
  size_t request_name_size = BLOCK_NAME_LEN;
  for (size_t i = 0; i < BLOCK_NAME_LEN; i++) {
    if (request->name[i] == 0) {
      request_name_size = i;
      break;
    }
  }
  fidl::UnownedClientEnd<fuchsia_hardware_block_volume::VolumeManager> client(
      caller.borrow_channel());
  auto response = fidl::WireCall(client)->AllocatePartition(
      request->slice_count, type_guid, instance_guid,
      fidl::StringView::FromExternal(request->name, request_name_size), request->flags);
  if (response.status() != ZX_OK) {
    return response.status();
  }
  if (response.value().status != ZX_OK) {
    return response.value().status;
  }
  return ZX_OK;
}

// Takes ownership of |dir|.
zx::result<fbl::unique_fd> OpenPartitionImpl(DIR* dir, std::string_view out_path_base,
                                             const PartitionMatcher& matcher, zx_duration_t timeout,
                                             std::string* out_path) {
  auto cleanup = fit::defer([&]() { closedir(dir); });

  struct AllocHelperInfo {
    const PartitionMatcher& matcher;
    std::string_view out_path_base;
    std::string* out_path;
    fbl::unique_fd out_partition;
  } info = {
      .matcher = matcher,
      .out_path_base = out_path_base,
      .out_path = out_path,
  };

  auto cb = [](int dirfd, int event, const char* fn, void* cookie) {
    if (event != WATCH_EVENT_ADD_FILE || std::string_view{fn} == ".") {
      return ZX_OK;
    }
    auto& info = *static_cast<AllocHelperInfo*>(cookie);
    fdio_cpp::UnownedFdioCaller caller(dirfd);
    zx::result channel = component::ConnectAt<fuchsia_hardware_block_partition::PartitionAndDevice>(
        caller.directory(), fn);
    if (channel.is_error()) {
      return channel.status_value();
    }
    if (PartitionMatches(channel.value(), info.matcher)) {
      if (zx_status_t status = fdio_fd_create(channel.value().TakeChannel().release(),
                                              info.out_partition.reset_and_get_address());
          status != ZX_OK) {
        return status;
      }
      if (info.out_path != nullptr) {
        *info.out_path = std::string(info.out_path_base).append(fn);
      }
      return ZX_ERR_STOP;
    }
    return ZX_OK;
  };

  zx_time_t deadline = zx_deadline_after(timeout);
  if (zx_status_t status = fdio_watch_directory(dirfd(dir), cb, deadline, &info);
      status != ZX_ERR_STOP) {
    return zx::error(status);
  }
  return zx::ok(std::move(info.out_partition));
}

zx_status_t DestroyPartitionImpl(fbl::unique_fd&& fd) {
  fdio_cpp::FdioCaller partition_caller(std::move(fd));

  zx_status_t status;
  zx_status_t io_status =
      fuchsia_hardware_block_volume_VolumeDestroy(partition_caller.borrow_channel(), &status);
  if (io_status) {
    return io_status;
  }
  return status;
}

}  // namespace

__EXPORT
bool PartitionMatches(
    fidl::UnownedClientEnd<fuchsia_hardware_block_partition::PartitionAndDevice> channel,
    const PartitionMatcher& matcher) {
  ZX_ASSERT(matcher.type_guid || matcher.instance_guid || matcher.detected_disk_format ||
            matcher.num_labels > 0 || !matcher.parent_device.empty());
  if (matcher.num_labels > 0) {
    ZX_ASSERT(matcher.labels);
  }

  zx::unowned_channel partition_channel = channel.channel();

  fuchsia_hardware_block_partition_GUID guid;
  zx_status_t io_status, status;
  if (matcher.type_guid) {
    io_status = fuchsia_hardware_block_partition_PartitionGetTypeGuid(partition_channel->get(),
                                                                      &status, &guid);
    if (io_status != ZX_OK || status != ZX_OK ||
        memcmp(guid.value, matcher.type_guid, BLOCK_GUID_LEN) != 0) {
      return false;
    }
  }
  if (matcher.instance_guid) {
    io_status = fuchsia_hardware_block_partition_PartitionGetInstanceGuid(partition_channel->get(),
                                                                          &status, &guid);
    if (io_status != ZX_OK || status != ZX_OK ||
        memcmp(guid.value, matcher.instance_guid, BLOCK_GUID_LEN) != 0) {
      return false;
    }
  }
  if (matcher.num_labels > 0) {
    char name[fuchsia_hardware_block_partition_NAME_LENGTH];
    size_t name_len;
    io_status = fuchsia_hardware_block_partition_PartitionGetName(partition_channel->get(), &status,
                                                                  name, sizeof(name), &name_len);
    if (io_status != ZX_OK || status != ZX_OK || name_len == 0) {
      return false;
    }
    bool matches_label = false;
    for (size_t i = 0; i < matcher.num_labels; ++i) {
      if (!strncmp(name, matcher.labels[i], name_len)) {
        matches_label = true;
        break;
      }
    }
    if (!matches_label) {
      return false;
    }
  }
  std::string topological_path;
  if (!matcher.parent_device.empty() || !matcher.ignore_prefix.empty() ||
      !matcher.ignore_if_path_contains.empty()) {
    auto resp =
        fidl::WireCall(fidl::UnownedClientEnd<fuchsia_device::Controller>(partition_channel))
            ->GetTopologicalPath();
    if (!resp.ok() || resp->is_error()) {
      return false;
    }

    topological_path = std::string(resp->value()->path.data(), resp->value()->path.size());
  }
  const std::string_view path(topological_path);
  if (!matcher.parent_device.empty() && !cpp20::starts_with(path, matcher.parent_device)) {
    return false;
  }
  if (!matcher.ignore_prefix.empty() && cpp20::starts_with(path, matcher.ignore_prefix)) {
    return false;
  }
  if (!matcher.ignore_if_path_contains.empty() &&
      path.find(matcher.ignore_if_path_contains) != std::string::npos) {
    return false;
  }
  if (matcher.detected_disk_format != kDiskFormatUnknown) {
    if (DetectDiskFormat(fidl::UnownedClientEnd<fuchsia_hardware_block::Block>(
            partition_channel)) != matcher.detected_disk_format) {
      return false;
    }
  }
  return true;
}

__EXPORT
zx_status_t FvmInitPreallocated(fidl::UnownedClientEnd<fuchsia_hardware_block::Block> device,
                                uint64_t initial_volume_size, uint64_t max_volume_size,
                                size_t slice_size) {
  if (slice_size % fvm::kBlockSize != 0) {
    // Alignment
    return ZX_ERR_INVALID_ARGS;
  }
  if ((slice_size * fvm::kMaxVSlices) / fvm::kMaxVSlices != slice_size) {
    // Overflow
    return ZX_ERR_INVALID_ARGS;
  }
  if (initial_volume_size > max_volume_size || initial_volume_size == 0 || max_volume_size == 0) {
    return ZX_ERR_INVALID_ARGS;
  }

  fvm::Header header = fvm::Header::FromGrowableDiskSize(
      fvm::kMaxUsablePartitions, initial_volume_size, max_volume_size, slice_size);
  if (header.pslice_count == 0) {
    return ZX_ERR_NO_SPACE;
  }

  // This buffer needs to hold both copies of the metadata.
  // TODO(fxbug.dev/60709): Eliminate layout assumptions.
  size_t metadata_allocated_bytes = header.GetMetadataAllocatedBytes();
  size_t dual_metadata_bytes = metadata_allocated_bytes * 2;
  std::unique_ptr<uint8_t[]> mvmo(new uint8_t[dual_metadata_bytes]);
  // Clear entire primary copy of metadata
  memset(mvmo.get(), 0, metadata_allocated_bytes);

  // Save the header to our primary metadata.
  memcpy(mvmo.get(), &header, sizeof(fvm::Header));
  size_t metadata_used_bytes = header.GetMetadataUsedBytes();
  fvm::UpdateHash(mvmo.get(), metadata_used_bytes);

  // Copy the new primary metadata to the backup copy.
  void* backup = mvmo.get() + header.GetSuperblockOffset(fvm::SuperblockType::kSecondary);
  memcpy(backup, mvmo.get(), metadata_allocated_bytes);

  // Validate our new state.
  if (!fvm::PickValidHeader(mvmo.get(), backup, metadata_used_bytes)) {
    return ZX_ERR_BAD_STATE;
  }

  // Write to primary copy.
  auto status = block_client::SingleWriteBytes(device, mvmo.get(), metadata_allocated_bytes, 0);
  if (status != ZX_OK) {
    return status;
  }
  // Write to secondary copy, to overwrite any previous FVM metadata copy that
  // could be here.
  return block_client::SingleWriteBytes(device, mvmo.get(), metadata_allocated_bytes,
                                        metadata_allocated_bytes);
}

__EXPORT
zx_status_t FvmInitWithSize(fidl::UnownedClientEnd<fuchsia_hardware_block::Block> device,
                            uint64_t volume_size, size_t slice_size) {
  return FvmInitPreallocated(device, volume_size, volume_size, slice_size);
}

__EXPORT
zx_status_t FvmInit(fidl::UnownedClientEnd<fuchsia_hardware_block::Block> device,
                    size_t slice_size) {
  // The metadata layout of the FVM is dependent on the
  // size of the FVM's underlying partition.
  const fidl::WireResult result = fidl::WireCall(device)->GetInfo();
  if (!result.ok()) {
    return result.status();
  }
  const fidl::WireResponse response = result.value();
  if (zx_status_t status = response.status; status != ZX_OK) {
    return status;
  }
  const fuchsia_hardware_block::wire::BlockInfo& block_info = *response.info;
  if (slice_size == 0 || slice_size % block_info.block_size) {
    return ZX_ERR_BAD_STATE;
  }

  return FvmInitWithSize(device, block_info.block_count * block_info.block_size, slice_size);
}

__EXPORT
zx_status_t FvmOverwrite(const char* path, size_t slice_size) {
  zx::result device = component::Connect<fuchsia_hardware_block::Block>(path);
  if (device.is_error()) {
    return device.status_value();
  }
  return FvmOverwriteImpl(device.value(), slice_size);
}

__EXPORT
zx_status_t FvmOverwriteWithDevfs(int devfs_root_fd, const char* relative_path, size_t slice_size) {
  fdio_cpp::UnownedFdioCaller caller(devfs_root_fd);
  zx::result device =
      component::ConnectAt<fuchsia_hardware_block::Block>(caller.directory(), relative_path);
  if (device.is_error()) {
    return device.status_value();
  }
  return FvmOverwriteImpl(device.value(), slice_size);
}

// Helper function to destroy FVM
__EXPORT
zx_status_t FvmDestroy(const char* path) {
  fbl::String driver_path = fbl::StringPrintf("%s/fvm", path);

  fbl::unique_fd parent_fd(open(path, O_RDONLY | O_DIRECTORY));
  if (!parent_fd) {
    return ZX_ERR_NOT_FOUND;
  }
  fbl::unique_fd fvm_fd(open(driver_path.c_str(), O_RDWR));
  if (!fvm_fd) {
    return ZX_ERR_NOT_FOUND;
  }
  return DestroyFvmAndWait(-1, std::move(parent_fd), std::move(fvm_fd), path);
}

__EXPORT
zx_status_t FvmDestroyWithDevfs(int devfs_root_fd, const char* relative_path) {
  fbl::String driver_path = fbl::StringPrintf("%s/fvm", relative_path);

  fbl::unique_fd parent_fd(openat(devfs_root_fd, relative_path, O_RDONLY | O_DIRECTORY));
  if (!parent_fd) {
    return ZX_ERR_NOT_FOUND;
  }
  fbl::unique_fd fvm_fd(openat(devfs_root_fd, driver_path.c_str(), O_RDWR));
  if (!fvm_fd) {
    return ZX_ERR_NOT_FOUND;
  }
  return DestroyFvmAndWait(devfs_root_fd, std::move(parent_fd), std::move(fvm_fd), relative_path);
}

// Helper function to allocate, find, and open VPartition.
__EXPORT
zx::result<fbl::unique_fd> FvmAllocatePartition(int fvm_fd, const alloc_req_t* request) {
  if (zx_status_t status = FvmAllocatePartitionImpl(fvm_fd, request); status != ZX_OK) {
    return zx::error(status);
  }
  PartitionMatcher matcher{
      .type_guid = request->type,
      .instance_guid = request->guid,
  };
  return OpenPartition(matcher, kOpenPartitionTimeout, nullptr);
}

__EXPORT
zx::result<fbl::unique_fd> FvmAllocatePartitionWithDevfs(int devfs_root_fd, int fvm_fd,
                                                         const alloc_req_t* request) {
  int alloc_status = FvmAllocatePartitionImpl(fvm_fd, request);
  if (alloc_status != 0) {
    return zx::error(alloc_status);
  }
  PartitionMatcher matcher{
      .type_guid = request->type,
      .instance_guid = request->guid,
  };
  return OpenPartitionWithDevfs(devfs_root_fd, matcher, kOpenPartitionTimeout, nullptr);
}

__EXPORT
zx::result<fuchsia_hardware_block_volume::wire::VolumeManagerInfo> FvmQuery(int fvm_fd) {
  fdio_cpp::UnownedFdioCaller caller(fvm_fd);

  const fidl::WireResult result =
      fidl::WireCall(caller.borrow_as<fuchsia_hardware_block_volume::VolumeManager>())->GetInfo();
  if (!result.ok()) {
    return zx::error(result.status());
  }
  const fidl::WireResponse response = result.value();
  if (zx_status_t status = response.status; status != ZX_OK) {
    return zx::error(status);
  }
  return zx::ok(*response.info);
}

__EXPORT
zx::result<fbl::unique_fd> OpenPartition(const PartitionMatcher& matcher, zx_duration_t timeout,
                                         std::string* out_path) {
  DIR* dir = opendir(kBlockDevPath);
  if (dir == nullptr) {
    return zx::error(ZX_ERR_IO);
  }

  return OpenPartitionImpl(dir, kBlockDevPath, matcher, timeout, out_path);
}

__EXPORT
zx::result<fbl::unique_fd> OpenPartitionWithDevfs(int devfs_root_fd,
                                                  const PartitionMatcher& matcher,
                                                  zx_duration_t timeout,
                                                  std::string* out_path_relative) {
  fbl::unique_fd block_dev_fd(openat(devfs_root_fd, kBlockDevRelativePath, O_RDONLY));
  if (!block_dev_fd) {
    return zx::error(ZX_ERR_IO);
  }
  DIR* dir = fdopendir(block_dev_fd.get());
  if (dir == nullptr) {
    return zx::error(ZX_ERR_IO);
  }

  return OpenPartitionImpl(dir, kBlockDevRelativePath, matcher, timeout, out_path_relative);
}

__EXPORT
zx_status_t DestroyPartition(const uint8_t* uniqueGUID, const uint8_t* typeGUID) {
  PartitionMatcher matcher{
      .type_guid = typeGUID,
      .instance_guid = uniqueGUID,
  };
  auto fd_or = OpenPartition(matcher, 0, nullptr);
  if (fd_or.is_error())
    return fd_or.status_value();
  return DestroyPartitionImpl(*std::move(fd_or));
}

__EXPORT
zx_status_t DestroyPartitionWithDevfs(int devfs_root_fd, const uint8_t* uniqueGUID,
                                      const uint8_t* typeGUID) {
  PartitionMatcher matcher{
      .type_guid = typeGUID,
      .instance_guid = uniqueGUID,
  };
  auto fd_or = OpenPartitionWithDevfs(devfs_root_fd, matcher, 0, nullptr);
  if (fd_or.is_error())
    return fd_or.status_value();
  return DestroyPartitionImpl(*std::move(fd_or));
}

__EXPORT
zx_status_t FvmActivate(int fvm_fd, fuchsia_hardware_block_partition::wire::Guid deactivate,
                        fuchsia_hardware_block_partition::wire::Guid activate) {
  fdio_cpp::UnownedFdioCaller caller(fvm_fd);
  fidl::UnownedClientEnd<fuchsia_hardware_block_volume::VolumeManager> client(
      caller.borrow_channel());
  auto response = fidl::WireCall(client)->Activate(deactivate, activate);
  if (response.status() != ZX_OK) {
    return response.status();
  }
  if (response.value().status != ZX_OK) {
    return response.value().status;
  }
  return ZX_OK;
}

}  // namespace fs_management
