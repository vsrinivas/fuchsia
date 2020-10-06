// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <fuchsia/device/c/fidl.h>
#include <fuchsia/device/llcpp/fidl.h>
#include <fuchsia/hardware/block/partition/c/fidl.h>
#include <fuchsia/io/llcpp/fidl.h>
#include <lib/devmgr-integration-test/fixture.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/fidl/cpp/message.h>
#include <lib/fidl/cpp/message_part.h>
#include <lib/fidl/llcpp/sync_call.h>
#include <lib/fidl/llcpp/vector_view.h>
#include <lib/zx/time.h>
#include <zircon/status.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <fs-management/fvm.h>
#include <fvm/test/device-ref.h>
#include <zxtest/zxtest.h>

namespace fvm {
namespace {
namespace fuchsia = ::llcpp::fuchsia;

constexpr char kRamdiskCtlPath[] = "misc/ramctl";
constexpr zx::duration kDeviceWaitTime = zx::sec(3);

zx_status_t RebindBlockDevice(DeviceRef* device) {
  // We need to create a DirWatcher to wait for the block device's child to disappear.
  std::unique_ptr<devmgr_integration_test::DirWatcher> watcher;
  fbl::unique_fd dir_fd(openat(device->devfs_root_fd(), device->path(), O_RDONLY | O_DIRECTORY));
  zx_status_t status = devmgr_integration_test::DirWatcher::Create(std::move(dir_fd), &watcher);
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
  return fidl::VectorView<uint8_t>(fidl::unowned_ptr(const_cast<uint8_t*>(data.data())),
                                   data.size());
}

using FidlGuid = fuchsia_hardware_block_partition_GUID;

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
  path_.Append(path.c_str());
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
  fuchsia::io::File::ResultOf::WriteAt result =
      fuchsia::io::File::Call::WriteAt(device()->channel(), ToFidlVector(data), offset);

  ASSERT_OK(result.status(), "Failed to communicate with block device.");
  ASSERT_OK(result->s);
  ASSERT_EQ(data.size(), result->actual);
}

void BlockDeviceAdapter::ReadAt(uint64_t offset, fbl::Array<uint8_t>* out_data) {
  fuchsia::io::File::ResultOf::ReadAt result =
      fuchsia::io::File::Call::ReadAt(device()->channel(), out_data->size(), offset);

  ASSERT_OK(result.status(), "Failed to communicate with block device.");
  ASSERT_OK(result->s);
  memcpy(out_data->data(), result->data.data(), result->data.count());
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

  char out_path[kPathMax] = {};
  fbl::unique_fd device_fd(open_partition_with_devfs(devfs_root.get(), guid.data(), type.data(),
                                                     kDeviceWaitTime.get(), out_path));
  if (!device_fd.is_valid()) {
    ADD_FAILURE("Unable to obtain handle for partition.");
    return nullptr;
  }
  return std::make_unique<VPartitionAdapter>(devfs_root, GetChannel(device_fd.get()), out_path,
                                             std::move(device_fd), name, guid, type);
}

VPartitionAdapter::~VPartitionAdapter() {
  destroy_partition_with_devfs(devfs_root_, guid_.data(), type_.data());
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
  char out_path[kPathMax] = {};
  fd_.reset(open_partition_with_devfs(devfs_root_, guid_.data(), type_.data(),
                                      zx::duration::infinite().get(), out_path));
  ASSERT_TRUE(fd_.get());
  path_.Clear();
  path_.Append(out_path);
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

  if (fvm_init_preallocated(device->fd(), initial_block_count * block_size,
                            maximum_block_count * block_size, slice_size) != ZX_OK) {
    return nullptr;
  }

  zx_status_t status = ZX_OK;
  auto resp = ::llcpp::fuchsia::device::Controller::Call::Bind(
      zx::unowned_channel(device->channel()->get()),
      ::fidl::StringView(kFvmDriverLib, fbl::constexpr_strlen(kFvmDriverLib)));
  zx_status_t fidl_status = resp.status();
  if (resp->result.is_err()) {
    status = resp->result.err();
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

FvmAdapter::~FvmAdapter() { fvm_destroy_with_devfs(devfs_root_, block_device_->path()); }

zx_status_t FvmAdapter::AddPartition(const fbl::unique_fd& devfs_root, const std::string& name,
                                     const Guid& guid, const Guid& type, uint64_t slice_count,
                                     std::unique_ptr<VPartitionAdapter>* out_partition) {
  zx_status_t status;
  FidlGuid fidl_guid, fidl_type;
  memcpy(fidl_guid.value, guid.data(), guid.size());
  memcpy(fidl_type.value, type.data(), type.size());

  zx_status_t fidl_status = fuchsia_hardware_block_volume_VolumeManagerAllocatePartition(
      channel_->get(), slice_count, &fidl_type, &fidl_guid, name.c_str(), name.size(), 0u, &status);
  if (fidl_status != ZX_OK) {
    return fidl_status;
  }

  if (status != ZX_OK) {
    return status;
  }

  auto vpartition = VPartitionAdapter::Create(devfs_root, name, guid, type);
  if (vpartition == nullptr) {
    return ZX_ERR_INVALID_ARGS;
  }
  status = vpartition->WaitUntilVisible();

  if (status != ZX_OK) {
    return status;
  }

  if (out_partition != nullptr) {
    *out_partition = std::move(vpartition);
  }

  return status;
}

zx_status_t FvmAdapter::Rebind(fbl::Vector<VPartitionAdapter*> vpartitions) {
  zx_status_t status = RebindBlockDevice(block_device_);

  if (status != ZX_OK) {
    ADD_FAILURE("FvmAdapter block device rebind failed.");
    return status;
  }

  auto resp = ::llcpp::fuchsia::device::Controller::Call::Bind(
      zx::unowned_channel(block_device_->channel()->get()),
      ::fidl::StringView(kFvmDriverLib, fbl::constexpr_strlen(kFvmDriverLib)));
  zx_status_t fidl_status = resp.status();
  status = ZX_OK;
  if (resp->result.is_err()) {
    status = resp->result.err();
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

zx_status_t FvmAdapter::Query(VolumeInfo* info) const { return fvm_query(fd(), info); }

fbl::Array<uint8_t> MakeRandomBuffer(size_t size, unsigned int* seed) {
  fbl::Array data(new uint8_t[size], size);

  for (size_t byte = 0; byte < size; ++byte) {
    data[byte] = static_cast<uint8_t>(rand_r(seed));
  }

  return data;
}

bool IsConsistentAfterGrowth(const VolumeInfo& before, const VolumeInfo& after) {
  // Frowing a FVM should not allocate any slices nor should it change the slice size.
  return before.slice_size == after.slice_size &&
         before.pslice_allocated_count == after.pslice_allocated_count;
}

}  // namespace fvm
