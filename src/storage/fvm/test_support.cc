// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/fvm/test_support.h"

#include <errno.h>
#include <fcntl.h>
#include <fidl/fuchsia.device/cpp/wire.h>
#include <fidl/fuchsia.hardware.block.volume/cpp/wire.h>
#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/fidl/cpp/wire/sync_call.h>
#include <lib/fidl/cpp/wire/vector_view.h>
#include <lib/sys/component/cpp/service_client.h>
#include <lib/zx/time.h>
#include <zircon/status.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <fbl/unique_fd.h>
#include <sdk/lib/device-watcher/cpp/device-watcher.h>
#include <zxtest/zxtest.h>

#include "src/lib/storage/fs_management/cpp/fvm.h"

namespace fvm {
namespace {

constexpr char kRamdiskCtlPath[] = "sys/platform/00:00:2d/ramctl";
constexpr zx::duration kDeviceWaitTime = zx::sec(30);

template <typename Protocol>
zx::result<fidl::ClientEnd<Protocol>> GetChannel(DeviceRef* device) {
  fdio_cpp::UnownedFdioCaller caller(device->devfs_root_fd());
  return component::ConnectAt<Protocol>(caller.directory(), device->path());
}

template <typename Protocol>
fidl::UnownedClientEnd<Protocol> GetChannel(VPartitionAdapter* device) {
  fdio_cpp::UnownedFdioCaller caller(device->fd());
  return caller.borrow_as<Protocol>();
}

zx_status_t RebindBlockDevice(DeviceRef* device) {
  // We need to create a DirWatcher to wait for the block device's child to disappear.
  std::unique_ptr<device_watcher::DirWatcher> watcher;
  fbl::unique_fd dir_fd(
      openat(device->devfs_root_fd().get(), device->path(), O_RDONLY | O_DIRECTORY));
  if (zx_status_t status = device_watcher::DirWatcher::Create(std::move(dir_fd), &watcher);
      status != ZX_OK) {
    ADD_FAILURE("DirWatcher::Create('%s'): %s", device->path(), zx_status_get_string(status));
    return status;
  }

  zx::result channel = GetChannel<fuchsia_hardware_block::Block>(device);
  if (channel.is_error()) {
    return channel.status_value();
  }
  const fidl::WireResult result = fidl::WireCall(channel.value())->RebindDevice();
  if (!result.ok()) {
    return result.status();
  }
  const fidl::WireResponse response = result.value();
  if (zx_status_t status = response.status; status != ZX_OK) {
    ADD_FAILURE("('%s').Rebind(): %s", device->path(), zx_status_get_string(status));
    return status;
  }
  if (zx_status_t status = watcher->WaitForRemoval(fbl::String() /* any file */, kDeviceWaitTime);
      status != ZX_OK) {
    ADD_FAILURE("Watcher('%s').WaitForRemoval: %s", device->path(), zx_status_get_string(status));
    return status;
  }
  return ZX_OK;
}

fidl::VectorView<uint8_t> ToFidlVector(const fbl::Array<uint8_t>& data) {
  return fidl::VectorView<uint8_t>::FromExternal(const_cast<uint8_t*>(data.data()), data.size());
}

using FidlGuid = fuchsia_hardware_block_partition::wire::Guid;

}  // namespace

// namespace

DeviceRef::DeviceRef(const fbl::unique_fd& devfs_root, const std::string& path)
    : devfs_root_(devfs_root) {
  path_.append(path);
}

std::unique_ptr<DeviceRef> DeviceRef::Create(const fbl::unique_fd& devfs_root,
                                             const std::string& device_path) {
  return std::make_unique<DeviceRef>(devfs_root, device_path);
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

  if (zx_status_t status =
          wait_for_device_at(devfs_root.get(), kRamdiskCtlPath, kDeviceWaitTime.get());
      status != ZX_OK) {
    ADD_FAILURE("Failed to wait for RamCtl. Reason: %s", zx_status_get_string(status));
    return nullptr;
  }

  RamdiskClient* client;
  if (zx_status_t status = ramdisk_create_at(devfs_root.get(), block_size, block_count, &client);
      status != ZX_OK) {
    ADD_FAILURE("Failed to create ramdisk. Reason: %s", zx_status_get_string(status));
    return nullptr;
  }
  const char* path = ramdisk_get_path(client);
  return std::make_unique<RamdiskRef>(devfs_root, path, client);
}

RamdiskRef::~RamdiskRef() { ramdisk_destroy(ramdisk_client_); }

zx_status_t RamdiskRef::Grow(uint64_t target_size) {
  return ramdisk_grow(ramdisk_client_, target_size);
}

void BlockDeviceAdapter::WriteAt(const fbl::Array<uint8_t>& data, uint64_t offset) {
  zx::result channel = GetChannel<fuchsia_io::File>(device());
  ASSERT_OK(channel.status_value());
  const fidl::WireResult result =
      fidl::WireCall(channel.value())->WriteAt(ToFidlVector(data), offset);
  ASSERT_OK(result.status(), "Failed to communicate with block device.");
  const fit::result response = result.value();
  ASSERT_TRUE(response.is_ok(), "%s", zx_status_get_string(response.error_value()));
  ASSERT_EQ(data.size(), response.value()->actual_count);
}

void BlockDeviceAdapter::ReadAt(uint64_t offset, fbl::Array<uint8_t>* out_data) {
  zx::result channel = GetChannel<fuchsia_io::File>(device());
  ASSERT_OK(channel.status_value());
  const fidl::WireResult result = fidl::WireCall(channel.value())->ReadAt(out_data->size(), offset);
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
  zx_status_t status =
      wait_for_device_at(devfs_root_.get(), device()->path(), kDeviceWaitTime.get());

  if (status != ZX_OK) {
    ADD_FAILURE("Block device did not become visible at: %s", device()->path());
  }
  return status;
}

zx_status_t BlockDeviceAdapter::Rebind() {
  if (zx_status_t status = RebindBlockDevice(device()); status != ZX_OK) {
    return status;
  }

  // Block device is visible again.
  if (zx_status_t status = WaitUntilVisible(); status != ZX_OK) {
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
  zx::result device_fd_or = fs_management::OpenPartitionWithDevfs(devfs_root.get(), matcher,
                                                                  kDeviceWaitTime.get(), &out_path);
  if (device_fd_or.is_error()) {
    ADD_FAILURE("Unable to obtain handle for partition.");
    return nullptr;
  }
  return std::make_unique<VPartitionAdapter>(devfs_root, out_path.c_str(),
                                             std::move(device_fd_or.value()), name, guid, type);
}

VPartitionAdapter::~VPartitionAdapter() {
  fs_management::DestroyPartitionWithDevfs(devfs_root_.get(), guid_.data(), type_.data());
}

zx_status_t VPartitionAdapter::Extend(uint64_t offset, uint64_t length) {
  fidl::UnownedClientEnd channel = GetChannel<fuchsia_hardware_block_volume::Volume>(this);
  const fidl::WireResult result = fidl::WireCall(channel)->Extend(offset, length);
  if (!result.ok()) {
    return result.status();
  }
  const fidl::WireResponse response = result.value();
  return response.status;
}

zx_status_t VPartitionAdapter::Reconnect() {
  fs_management::PartitionMatcher matcher{
      .type_guid = type_.data(),
      .instance_guid = guid_.data(),
  };
  zx::result fd = fs_management::OpenPartitionWithDevfs(devfs_root_.get(), matcher,
                                                        zx::duration::infinite().get(), &path_);
  if (fd.is_error()) {
    return fd.status_value();
  }
  fd_ = std::move(fd.value());
  return ZX_OK;
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

  {
    zx::result channel = GetChannel<fuchsia_hardware_block::Block>(device);
    if (channel.is_error()) {
      ADD_FAILURE("ConnectAt(%s): %s", device->path(), channel.status_string());
      return nullptr;
    }
    if (zx_status_t status =
            fs_management::FvmInitPreallocated(channel.value(), initial_block_count * block_size,
                                               maximum_block_count * block_size, slice_size);
        status != ZX_OK) {
      ADD_FAILURE("FvmInitPreallocated(%s): %s", device->path(), zx_status_get_string(status));
      return nullptr;
    }
  }

  {
    zx::result channel = GetChannel<fuchsia_device::Controller>(device);
    if (channel.is_error()) {
      ADD_FAILURE("ConnectAt(%s): %s", device->path(), channel.status_string());
      return nullptr;
    }
    const fidl::WireResult result =
        fidl::WireCall(channel.value())->Bind(fidl::StringView(kFvmDriverLib));
    if (!result.ok()) {
      ADD_FAILURE("Binding FVM driver failed: %s", result.FormatDescription().c_str());
      return nullptr;
    }
    const fit::result response = result.value();
    if (response.is_error()) {
      ADD_FAILURE("Binding FVM driver failed: %s", zx_status_get_string(response.error_value()));
      return nullptr;
    }
  }

  fbl::StringBuffer<kPathMax> fvm_path;
  fvm_path.AppendPrintf("%s/fvm", device->path());

  if (wait_for_device_at(devfs_root.get(), fvm_path.c_str(), kDeviceWaitTime.get()) != ZX_OK) {
    ADD_FAILURE("Loading FVM driver timeout.");
    return nullptr;
  }
  return std::make_unique<FvmAdapter>(devfs_root, fvm_path.c_str(), device);
}

FvmAdapter::~FvmAdapter() {
  fs_management::FvmDestroyWithDevfs(devfs_root_.get(), block_device_->path());
}

zx_status_t FvmAdapter::AddPartition(const fbl::unique_fd& devfs_root, const std::string& name,
                                     const Guid& guid, const Guid& type, uint64_t slice_count,
                                     std::unique_ptr<VPartitionAdapter>* out_vpartition) {
  FidlGuid fidl_guid, fidl_type;
  memcpy(fidl_guid.value.data(), guid.data(), guid.size());
  memcpy(fidl_type.value.data(), type.data(), type.size());

  zx::result channel = GetChannel<fuchsia_hardware_block_volume::VolumeManager>(this);
  if (channel.is_error()) {
    return channel.status_value();
  }
  const fidl::WireResult result = fidl::WireCall(channel.value())
                                      ->AllocatePartition(slice_count, fidl_type, fidl_guid,
                                                          fidl::StringView::FromExternal(name), 0u);
  if (!result.ok()) {
    return result.status();
  }
  const fidl::WireResponse response = result.value();
  if (zx_status_t status = response.status; status != ZX_OK) {
    return status;
  }

  auto vpartition = VPartitionAdapter::Create(devfs_root, name, guid, type);
  if (vpartition == nullptr) {
    return ZX_ERR_INVALID_ARGS;
  }
  if (zx_status_t status = vpartition->WaitUntilVisible(); status != ZX_OK) {
    return status;
  }

  if (out_vpartition != nullptr) {
    *out_vpartition = std::move(vpartition);
  }

  return ZX_OK;
}

zx_status_t FvmAdapter::Rebind(fbl::Vector<VPartitionAdapter*> vpartitions) {
  if (zx_status_t status = RebindBlockDevice(block_device_); status != ZX_OK) {
    ADD_FAILURE("FvmAdapter block device rebind failed.");
    return status;
  }

  // Bind the FVM to the block device.
  zx::result channel = GetChannel<fuchsia_device::Controller>(block_device_);
  if (channel.is_error()) {
    return channel.status_value();
  }
  const fidl::WireResult result =
      fidl::WireCall(channel.value())->Bind(fidl::StringView(kFvmDriverLib));
  if (!result.ok()) {
    ADD_FAILURE("Rebinding FVM driver failed: %s", result.FormatDescription().c_str());
    return result.status();
  }
  const fit::result response = result.value();
  if (response.is_error()) {
    ADD_FAILURE("Rebinding FVM driver failed: %s", zx_status_get_string(response.error_value()));
    return response.error_value();
  }

  // Wait for FVM driver to become visible.
  if (zx_status_t status = wait_for_device_at(devfs_root_.get(), path(), kDeviceWaitTime.get());
      status != ZX_OK) {
    ADD_FAILURE("Loading FVM driver timeout.");
    return status;
  }

  for (auto* vpartition : vpartitions) {
    if (zx_status_t status = vpartition->Reconnect(); status != ZX_OK) {
      return status;
    }
    if (zx_status_t status = vpartition->WaitUntilVisible(); status != ZX_OK) {
      return status;
    }
  }
  return ZX_OK;
}

zx_status_t FvmAdapter::Query(VolumeManagerInfo* out_info) const {
  fbl::unique_fd fd(openat(devfs_root_.get(), path(), O_RDWR));
  zx::result info = fs_management::FvmQuery(fd.get());
  if (info.is_error()) {
    return info.error_value();
  }
  *out_info = info.value();
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
