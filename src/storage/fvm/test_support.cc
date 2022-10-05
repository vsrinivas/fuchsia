// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/fvm/test_support.h"

#include <errno.h>
#include <fcntl.h>
#include <fidl/fuchsia.device/cpp/wire.h>
#include <fidl/fuchsia.hardware.block.volume/cpp/wire.h>
#include <fidl/fuchsia.io/cpp/wire.h>
#include <fuchsia/hardware/block/partition/c/fidl.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/fidl/cpp/wire/sync_call.h>
#include <lib/fidl/cpp/wire/vector_view.h>
#include <lib/zx/time.h>
#include <zircon/status.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <sdk/lib/device-watcher/cpp/device-watcher.h>
#include <zxtest/zxtest.h>

#include "src/lib/storage/fs_management/cpp/fvm.h"

namespace fvm {
namespace {

constexpr char kRamdiskCtlPath[] = "sys/platform/00:00:2d/ramctl";
constexpr zx::duration kDeviceWaitTime = zx::sec(30);

zx_status_t RebindBlockDevice(DeviceRef* device) {
  // We need to create a DirWatcher to wait for the block device's child to disappear.
  std::unique_ptr<device_watcher::DirWatcher> watcher;
  fbl::unique_fd dir_fd(openat(device->devfs_root_fd(), device->path(), O_RDONLY | O_DIRECTORY));
  zx_status_t status = device_watcher::DirWatcher::Create(std::move(dir_fd), &watcher);
  if (status != ZX_OK) {
    ADD_FAILURE("DirWatcher create failed. Path: %s", device->path());
    return status;
  }

  zx_status_t fidl_status =
      fuchsia_hardware_block_BlockRebindDevice(device->channel()->get(), &status);

  if (fidl_status != ZX_OK || status != ZX_OK) {
    ADD_FAILURE("Block device rebind failed. Path: %s", device->path());
    if (status != ZX_OK) {
      return status;
    }
    return fidl_status;
  }
  status = watcher->WaitForRemoval(fbl::String() /* any file */, kDeviceWaitTime);
  if (status != ZX_OK) {
    ADD_FAILURE("Wait for removal failed.Path: %s", device->path());
    return status;
  }
  device->Reconnect();
  return status;
}

fidl::VectorView<uint8_t> ToFidlVector(const fbl::Array<uint8_t>& data) {
  return fidl::VectorView<uint8_t>::FromExternal(const_cast<uint8_t*>(data.data()), data.size());
}

using FidlGuid = fuchsia_hardware_block_partition::wire::Guid;

zx::unowned_channel GetChannel(int fd) {
  if (fd < 0) {
    return zx::unowned_channel();
  }
  fdio_cpp::UnownedFdioCaller caller(fd);
  return zx::unowned_channel(caller.borrow_channel());
}

}  // namespace

// namespace

DeviceRef::DeviceRef(const fbl::unique_fd& devfs_root, const std::string& path, fbl::unique_fd fd)
    : devfs_root_(devfs_root.get()), fd_(std::move(fd)), channel_(GetChannel(fd_.get())) {
  path_.append(path.c_str());
}

void DeviceRef::Reconnect() {
  ASSERT_FALSE(path_.empty(), "Attempt to reconnect device with unset path.");
  fd_.reset(openat(devfs_root_, path_.c_str(), O_RDWR));
  ASSERT_TRUE(fd_.is_valid(), "Failed to reconnect to device.");
  channel_ = GetChannel(fd_.get());
}

std::unique_ptr<DeviceRef> DeviceRef::Create(const fbl::unique_fd& devfs_root,
                                             const std::string& device_path) {
  fbl::unique_fd device_fd(openat(devfs_root.get(), device_path.c_str(), O_RDWR));
  if (!device_fd.is_valid()) {
    ADD_FAILURE("Unable to obtain handle to block_device at %s. Reason: %s", device_path.c_str(),
                strerror(errno));
    return nullptr;
  }

  return std::make_unique<DeviceRef>(devfs_root, device_path, std::move(device_fd));
}

std::unique_ptr<RamdiskRef> RamdiskRef::Create(const fbl::unique_fd& devfs_root,
                                               uint64_t block_size, uint64_t block_count) {
  if (!devfs_root.is_valid()) {
    ADD_FAILURE("Bad devfs root handle.");
    return nullptr;
  }

  if (block_size == 0 || block_count == 0) {
    ADD_FAILURE("Attempting to create 0 sized ramdisk.");
    return nullptr;
  }

  zx_status_t status = wait_for_device_at(devfs_root.get(), kRamdiskCtlPath, kDeviceWaitTime.get());
  if (status != ZX_OK) {
    ADD_FAILURE("Failed to wait for RamCtl. Reason: %s", zx_status_get_string(status));
    return nullptr;
  }

  RamdiskClient* client;
  if ((status = ramdisk_create_at(devfs_root.get(), block_size, block_count, &client)) != ZX_OK) {
    ADD_FAILURE("Failed to create ramdisk. Reason: %s", zx_status_get_string(status));
    return nullptr;
  }
  const char* path = ramdisk_get_path(client);
  fbl::unique_fd device_fd(openat(devfs_root.get(), path, O_RDWR));
  if (!device_fd.is_valid()) {
    ADD_FAILURE("Error: Unable to obtain handle to block_device at %s. Reason: %s", path,
                strerror(errno));
    return nullptr;
  }

  return std::make_unique<RamdiskRef>(devfs_root, path, std::move(device_fd), client);
}

RamdiskRef::~RamdiskRef() { ramdisk_destroy(ramdisk_client_); }

zx_status_t RamdiskRef::Grow(uint64_t target_size) {
  return ramdisk_grow(ramdisk_client_, target_size);
}

void BlockDeviceAdapter::WriteAt(const fbl::Array<uint8_t>& data, uint64_t offset) {
  const fidl::WireResult result =
      fidl::WireCall<fuchsia_io::File>(device()->channel())->WriteAt(ToFidlVector(data), offset);
  ASSERT_OK(result.status(), "Failed to communicate with block device.");
  const fit::result response = result.value();
  ASSERT_TRUE(response.is_ok(), "%s", zx_status_get_string(response.error_value()));
  ASSERT_EQ(data.size(), response.value()->actual_count);
}

void BlockDeviceAdapter::ReadAt(uint64_t offset, fbl::Array<uint8_t>* out_data) {
  const fidl::WireResult result =
      fidl::WireCall<fuchsia_io::File>(device()->channel())->ReadAt(out_data->size(), offset);
  ASSERT_OK(result.status(), "Failed to communicate with block device.");
  const fit::result response = result.value();
  ASSERT_TRUE(response.is_ok(), "%s", zx_status_get_string(response.error_value()));
  const fidl::VectorView data = response.value()->data;
  memcpy(out_data->data(), data.data(), data.count());
}

void BlockDeviceAdapter::CheckContentsAt(const fbl::Array<uint8_t>& data, uint64_t offset) {
  ASSERT_GT(data.size(), 0, "data::size must be greater than 0.");
  fbl::Array<uint8_t> device_data(new uint8_t[data.size()], data.size());
  ASSERT_NO_FAILURES(ReadAt(offset, &device_data));
  ASSERT_BYTES_EQ(device_data.data(), data.data(), data.size());
}

zx_status_t BlockDeviceAdapter::WaitUntilVisible() const {
  zx_status_t status = wait_for_device_at(devfs_root_, device()->path(), kDeviceWaitTime.get());

  if (status != ZX_OK) {
    ADD_FAILURE("Block device did not become visible at: %s", device()->path());
  }
  return status;
}

zx_status_t BlockDeviceAdapter::Rebind() {
  zx_status_t status;

  if ((status = RebindBlockDevice(device())) != ZX_OK) {
    return status;
  }

  // Block device is visible again.
  if ((status = WaitUntilVisible()) != ZX_OK) {
    return status;
  }

  return ZX_OK;
}

std::unique_ptr<VPartitionAdapter> VPartitionAdapter::Create(const fbl::unique_fd& devfs_root,
                                                             const std::string& name,
                                                             const Guid& guid, const Guid& type) {
  if (name.empty() || type.size() == 0 || guid.size() == 0) {
    ADD_FAILURE(
        "Partition name(size=%lu), type(size=%lu) and guid(size=%lu) must be non "
        "empty.\n"
        "Partition {\n"
        "    name: %s\n"
        "    type: %s\n"
        "    guid: %s\n"
        "}",
        name.size(), type.size(), guid.size(), name.c_str(), type.ToString().c_str(),
        guid.ToString().c_str());
    return nullptr;
  }

  std::string out_path;
  fs_management::PartitionMatcher matcher{
      .type_guid = type.data(),
      .instance_guid = guid.data(),
  };
  auto device_fd_or = fs_management::OpenPartitionWithDevfs(devfs_root.get(), &matcher,
                                                            kDeviceWaitTime.get(), &out_path);
  if (device_fd_or.is_error()) {
    ADD_FAILURE("Unable to obtain handle for partition.");
    return nullptr;
  }
  auto channel = GetChannel(device_fd_or->get());
  return std::make_unique<VPartitionAdapter>(devfs_root, std::move(channel), out_path.c_str(),
                                             *std::move(device_fd_or), name, guid, type);
}

VPartitionAdapter::~VPartitionAdapter() {
  fs_management::DestroyPartitionWithDevfs(devfs_root_, guid_.data(), type_.data());
}

zx_status_t VPartitionAdapter::Extend(uint64_t offset, uint64_t length) {
  zx_status_t status;
  zx_status_t fidl_status =
      fuchsia_hardware_block_volume_VolumeExtend(channel_->get(), offset, length, &status);
  if (fidl_status != ZX_OK) {
    return fidl_status;
  }
  return status;
}

void VPartitionAdapter::Reconnect() {
  fs_management::PartitionMatcher matcher{
      .type_guid = type_.data(),
      .instance_guid = guid_.data(),
  };
  auto fd_or = fs_management::OpenPartitionWithDevfs(devfs_root_, &matcher,
                                                     zx::duration::infinite().get(), &path_);
  ASSERT_EQ(fd_or.status_value(), ZX_OK);
  fd_ = *std::move(fd_or);
  channel_ = GetChannel(fd_.get());
}

std::unique_ptr<FvmAdapter> FvmAdapter::Create(const fbl::unique_fd& devfs_root,
                                               uint64_t block_size, uint64_t block_count,
                                               uint64_t slice_size, DeviceRef* device) {
  return CreateGrowable(devfs_root, block_size, block_count, block_count, slice_size, device);
}

std::unique_ptr<FvmAdapter> FvmAdapter::CreateGrowable(const fbl::unique_fd& devfs_root,
                                                       uint64_t block_size,
                                                       uint64_t initial_block_count,
                                                       uint64_t maximum_block_count,
                                                       uint64_t slice_size, DeviceRef* device) {
  if (device == nullptr) {
    ADD_FAILURE("Create requires non-null device pointer.");
    return nullptr;
  }

  if (!device->channel()->is_valid()) {
    ADD_FAILURE("Invalid device handle.");
    return nullptr;
  }

  if (fs_management::FvmInitPreallocated(device->fd(), initial_block_count * block_size,
                                         maximum_block_count * block_size, slice_size) != ZX_OK) {
    return nullptr;
  }

  zx_status_t status = ZX_OK;
  auto resp =
      fidl::WireCall<fuchsia_device::Controller>(zx::unowned_channel(device->channel()->get()))
          ->Bind(::fidl::StringView(kFvmDriverLib));
  zx_status_t fidl_status = resp.status();
  if (resp->is_error()) {
    status = resp->error_value();
  }

  if (fidl_status != ZX_OK || status != ZX_OK) {
    ADD_FAILURE("Binding FVM driver failed. Reason: %s",
                zx_status_get_string((fidl_status != ZX_OK) ? fidl_status : status));
    return nullptr;
  }

  fbl::StringBuffer<kPathMax> fvm_path;
  fvm_path.AppendPrintf("%s/fvm", device->path());

  if (wait_for_device_at(devfs_root.get(), fvm_path.c_str(), kDeviceWaitTime.get()) != ZX_OK) {
    ADD_FAILURE("Loading FVM driver timeout.");
    return nullptr;
  }

  fbl::unique_fd device_fd(openat(devfs_root.get(), fvm_path.c_str(), O_RDWR));
  if (!device_fd.is_valid()) {
    ADD_FAILURE("Failed to acquire handle for fvm.");
    return nullptr;
  }

  return std::make_unique<FvmAdapter>(devfs_root, fvm_path.c_str(), std::move(device_fd), device);
}

FvmAdapter::~FvmAdapter() {
  fs_management::FvmDestroyWithDevfs(devfs_root_, block_device_->path());
}

zx_status_t FvmAdapter::AddPartition(const fbl::unique_fd& devfs_root, const std::string& name,
                                     const Guid& guid, const Guid& type, uint64_t slice_count,
                                     std::unique_ptr<VPartitionAdapter>* out_partition) {
  FidlGuid fidl_guid, fidl_type;
  memcpy(fidl_guid.value.data(), guid.data(), guid.size());
  memcpy(fidl_type.value.data(), type.data(), type.size());

  auto response =
      fidl::WireCall(fidl::UnownedClientEnd<fuchsia_hardware_block_volume::VolumeManager>(channel_))
          ->AllocatePartition(slice_count, fidl_type, fidl_guid,
                              fidl::StringView::FromExternal(name), 0u);
  if (response.status() != ZX_OK) {
    return response.status();
  }

  if (response.value().status != ZX_OK) {
    return response.value().status;
  }

  auto vpartition = VPartitionAdapter::Create(devfs_root, name, guid, type);
  if (vpartition == nullptr) {
    return ZX_ERR_INVALID_ARGS;
  }
  if (zx_status_t status = vpartition->WaitUntilVisible(); status != ZX_OK) {
    return status;
  }

  if (out_partition != nullptr) {
    *out_partition = std::move(vpartition);
  }

  return ZX_OK;
}

zx_status_t FvmAdapter::Rebind(fbl::Vector<VPartitionAdapter*> vpartitions) {
  zx_status_t status = RebindBlockDevice(block_device_);

  if (status != ZX_OK) {
    ADD_FAILURE("FvmAdapter block device rebind failed.");
    return status;
  }

  auto resp = fidl::WireCall<fuchsia_device::Controller>(
                  zx::unowned_channel(block_device_->channel()->get()))
                  ->Bind(::fidl::StringView(kFvmDriverLib));
  zx_status_t fidl_status = resp.status();
  status = ZX_OK;
  if (resp->is_error()) {
    status = resp->error_value();
  }

  // Bind the FVM to the block device.
  if (fidl_status != ZX_OK || status != ZX_OK) {
    ADD_FAILURE("Rebinding FVM driver failed.");
    if (status != ZX_OK) {
      return status;
    }
    return fidl_status;
  }

  // Wait for FVM driver to become visible.
  if ((status = wait_for_device_at(devfs_root_, path(), kDeviceWaitTime.get())) != ZX_OK) {
    ADD_FAILURE("Loading FVM driver timeout.");
    return status;
  }

  // Acquire new FD for the FVM driver.
  Reconnect();

  for (auto* vpartition : vpartitions) {
    // Reopen them, since all the channels have been closed.
    vpartition->Reconnect();
    if ((status = vpartition->WaitUntilVisible()) != ZX_OK) {
      return status;
    }
  }
  return ZX_OK;
}

zx_status_t FvmAdapter::Query(VolumeManagerInfo* info) const {
  if (auto info_or = fs_management::FvmQuery(fd()); info_or.is_error())
    return info_or.error_value();
  else
    *info = *std::move(info_or);
  return ZX_OK;
}

fbl::Array<uint8_t> MakeRandomBuffer(size_t size, unsigned int* seed) {
  fbl::Array data(new uint8_t[size], size);

  for (size_t byte = 0; byte < size; ++byte) {
    data[byte] = static_cast<uint8_t>(rand_r(seed));
  }

  return data;
}

bool IsConsistentAfterGrowth(const VolumeManagerInfo& before, const VolumeManagerInfo& after) {
  // Frowing a FVM should not allocate any slices nor should it change the slice size.
  return before.slice_size == after.slice_size &&
         before.assigned_slice_count == after.assigned_slice_count;
}

}  // namespace fvm
