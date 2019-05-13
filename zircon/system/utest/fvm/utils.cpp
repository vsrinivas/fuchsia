// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <errno.h>
#include <fcntl.h>

#include <fs-management/fvm.h>
#include <fuchsia/device/c/fidl.h>
#include <fuchsia/hardware/block/partition/c/fidl.h>
#include <fuchsia/io/llcpp/fidl.h>
#include <lib/fidl/cpp/message.h>
#include <lib/fidl/cpp/message_part.h>
#include <lib/fidl/cpp/vector_view.h>
#include <lib/fzl/fdio.h>
#include <lib/zx/time.h>
#include <zircon/status.h>
#include <zxtest/zxtest.h>

#include "utils.h"

namespace fvm {
namespace {

zx_status_t RebindBlockDevice(DeviceRef* device) {
    zx_status_t status;
    zx_status_t fidl_status =
        fuchsia_hardware_block_BlockRebindDevice(device->channel()->get(), &status);

    if (fidl_status != ZX_OK || status != ZX_OK) {
        ADD_FAILURE("Block device rebind failed. Path: %s", device->path());
        if (status != ZX_OK) {
            return status;
        }
        return fidl_status;
    }
    device->Reconnect();
    return status;
}

fidl::BytePart ToBytePart(fbl::Array<uint8_t>* message) {
    return fidl::BytePart(message->get(), static_cast<uint32_t>(message->size()));
}

fidl::VectorView<uint8_t> ToFidlVector(const fbl::Array<uint8_t>& data) {
    return fidl::VectorView<uint8_t>(data.size(), const_cast<uint8_t*>(data.get()));
}

using FidlGuid = fuchsia_hardware_block_partition_GUID;

constexpr zx::duration kDeviceWaitTime = zx::sec(3);

zx::unowned_channel GetChannel(int fd) {
    if (fd < 0) {
        return zx::unowned_channel();
    }
    fzl::UnownedFdioCaller caller(fd);
    return zx::unowned_channel(caller.borrow_channel());
}

} // namespace

// namespace

DeviceRef::DeviceRef(const std::string& path, fbl::unique_fd fd)
    : fd_(std::move(fd)), channel_(GetChannel(fd_.get())) {
    path_.Append(path.c_str());
}

void DeviceRef::Reconnect() {
    ASSERT_FALSE(path_.empty(), "Attempt to reconnect device with unset path.");
    fd_.reset(open(path_.c_str(), O_RDWR));
    ASSERT_TRUE(fd_.is_valid(), "Failed to reconnect to device.");
    channel_ = GetChannel(fd_.get());
}

std::unique_ptr<DeviceRef> DeviceRef::Create(const std::string& device_path) {
    fbl::unique_fd device_fd(open(device_path.c_str(), O_RDWR));
    if (!device_fd.is_valid()) {
        ADD_FAILURE("Unable to obtain handle to block_device at %s. Reason: %s",
                    device_path.c_str(), strerror(errno));
        return nullptr;
    }

    return std::make_unique<DeviceRef>(device_path, std::move(device_fd));
}

std::unique_ptr<RamdiskRef> RamdiskRef::Create(uint64_t block_size, uint64_t block_count) {
    if (block_size == 0 || block_count == 0) {
        ADD_FAILURE("Attempting to create 0 sized ramdisk.");
        return nullptr;
    }

    RamdiskClient* client;
    zx_status_t status;
    if ((status = ramdisk_create(block_size, block_count, &client)) != ZX_OK) {
        ADD_FAILURE("Failed to create ramdisk. Reason: %s", zx_status_get_string(status));
        return nullptr;
    }
    const char* path = ramdisk_get_path(client);
    fbl::unique_fd device_fd(open(path, O_RDWR));
    if (!device_fd.is_valid()) {
        ADD_FAILURE("Error: Unable to obtain handle to block_device at %s. Reason: %s", path,
                    strerror(errno));
        return nullptr;
    }

    return std::make_unique<RamdiskRef>(path, std::move(device_fd), client);
}

RamdiskRef::~RamdiskRef() {
    ramdisk_destroy(ramdisk_client_);
}

zx_status_t RamdiskRef::Grow(uint64_t target_size) {
    return ramdisk_grow(ramdisk_client_, target_size);
}

void BlockDeviceAdapter::WriteAt(const fbl::Array<uint8_t>& data, uint64_t offset) {
    uint64_t written_bytes;
    uint64_t request_size = FIDL_ALIGN(sizeof(fuchsia::io::File::WriteAtRequest) + data.size());
    uint64_t response_size = FIDL_ALIGN(sizeof(fuchsia::io::File::WriteAtResponse) + data.size());
    fbl::Array<uint8_t> request(new uint8_t[request_size], request_size);
    fbl::Array<uint8_t> response(new uint8_t[response_size], response_size);

    zx_status_t status;
    fidl::DecodeResult<fuchsia::io::File::WriteAtResponse> result =
        fuchsia::io::File::Call::WriteAt(device()->channel(), ToBytePart(&request),
                                         ToFidlVector(data), offset, ToBytePart(&response), &status,
                                         &written_bytes);

    ASSERT_OK(result.status, "Failed to communicate with block device.");
    ASSERT_OK(status, "Failed to write to block device.");
    ASSERT_EQ(data.size(), written_bytes);
}

void BlockDeviceAdapter::ReadAt(uint64_t offset, fbl::Array<uint8_t>* out_data) {
    int64_t request_size = FIDL_ALIGN(sizeof(fuchsia::io::File::ReadAtRequest) + out_data->size());
    uint64_t response_size =
        FIDL_ALIGN(sizeof(fuchsia::io::File::ReadAtResponse) + out_data->size());
    fbl::Array<uint8_t> request(new uint8_t[request_size], request_size);
    fbl::Array<uint8_t> response(new uint8_t[response_size], response_size);
    fidl::VectorView<uint8_t> data;
    zx_status_t status;

    fidl::DecodeResult<fuchsia::io::File::ReadAtResponse> result =
        fuchsia::io::File::Call::ReadAt(device()->channel(), ToBytePart(&request), out_data->size(),
                                        offset, ToBytePart(&response), &status, &data);

    ASSERT_OK(result.status, "Failed to communicate with block device.");
    ASSERT_OK(status, "Failed to read from block device.");
    memcpy(out_data->get(), data.data(), data.count());
}

void BlockDeviceAdapter::CheckContentsAt(const fbl::Array<uint8_t>& data, uint64_t offset) {
    ASSERT_GT(data.size(), 0, "data::size must be greater than 0.");
    fbl::Array<uint8_t> device_data(new uint8_t[data.size()], data.size());
    ASSERT_NO_FAILURES(ReadAt(offset, &device_data));
    ASSERT_BYTES_EQ(device_data.get(), data.get(), data.size());
}

zx_status_t BlockDeviceAdapter::WaitUntilVisible() const {
    zx_status_t status = wait_for_device(device()->path(), kDeviceWaitTime.get());

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

std::unique_ptr<VPartitionAdapter> VPartitionAdapter::Create(const std::string& name,
                                                             const Guid& guid, const Guid& type) {
    if (name.empty() || type.size() == 0 || guid.size() == 0) {
        ADD_FAILURE("Partition name(size=%lu), type(size=%lu) and guid(size=%lu) must be non "
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
    fbl::unique_fd device_fd(
        open_partition(guid.data(), type.data(), kDeviceWaitTime.get(), out_path));
    if (!device_fd.is_valid()) {
        ADD_FAILURE("Unable to obtain handle for partition.");
        return nullptr;
    }
    return std::make_unique<VPartitionAdapter>(GetChannel(device_fd.get()), out_path,
                                               std::move(device_fd), name, guid, type);
}

VPartitionAdapter::~VPartitionAdapter() {
    destroy_partition(guid_.data(), type_.data());
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
    fd_.reset(open_partition(guid_.data(), type_.data(), zx::duration::infinite().get(), out_path));
    ASSERT_TRUE(fd_.get());
    path_.Clear();
    path_.Append(out_path);
    channel_ = GetChannel(fd_.get());
}

std::unique_ptr<FvmAdapter> FvmAdapter::Create(uint64_t block_size, uint64_t block_count,
                                               uint64_t slice_size, DeviceRef* device) {
    return CreateGrowable(block_size, block_count, block_count, slice_size, device);
}

std::unique_ptr<FvmAdapter> FvmAdapter::CreateGrowable(uint64_t block_size,
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

    zx_status_t status;
    zx_status_t fidl_status = fuchsia_device_ControllerBind(
        device->channel()->get(), kFvmDriverLib, fbl::constexpr_strlen(kFvmDriverLib), &status);
    if (fidl_status != ZX_OK || status != ZX_OK) {
        ADD_FAILURE("Binding FVM driver failed. Reason: %s",
                    zx_status_get_string((fidl_status != ZX_OK) ? fidl_status : status));
        return nullptr;
    }

    fbl::StringBuffer<kPathMax> fvm_path;
    fvm_path.AppendPrintf("%s/fvm", device->path());

    if (wait_for_device(fvm_path.c_str(), kDeviceWaitTime.get()) != ZX_OK) {
        ADD_FAILURE("Loading FVM driver timeout.");
        return nullptr;
    }

    fbl::unique_fd device_fd(open(fvm_path.c_str(), O_RDWR));
    if (!device_fd.is_valid()) {
        ADD_FAILURE("Failed to acquire handle for fvm.");
        return nullptr;
    }

    return std::make_unique<FvmAdapter>(fvm_path.c_str(), std::move(device_fd), device);
}

FvmAdapter::~FvmAdapter() {
    fvm_destroy(block_device_->path());
}

zx_status_t FvmAdapter::AddPartition(const std::string& name, const Guid& guid, const Guid& type,
                                     uint64_t slice_count,
                                     std::unique_ptr<VPartitionAdapter>* out_partition) {
    zx_status_t status;
    FidlGuid fidl_guid, fidl_type;
    memcpy(fidl_guid.value, guid.data(), guid.size());
    memcpy(fidl_type.value, type.data(), type.size());

    zx_status_t fidl_status = fuchsia_hardware_block_volume_VolumeManagerAllocatePartition(
        channel_->get(), slice_count, &fidl_type, &fidl_guid, name.c_str(), name.size(), 0u,
        &status);
    if (fidl_status != ZX_OK) {
        return fidl_status;
    }

    if (status != ZX_OK) {
        return status;
    }

    auto vpartition = VPartitionAdapter::Create(name, guid, type);
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

    zx_status_t fidl_status =
        fuchsia_device_ControllerBind(block_device_->channel()->get(), kFvmDriverLib,
                                      fbl::constexpr_strlen(kFvmDriverLib), &status);
    // Bind the FVM to the block device.
    if (fidl_status != ZX_OK || status != ZX_OK) {
        ADD_FAILURE("Rebinding FVM driver failed.");
        if (status != ZX_OK) {
            return status;
        }
        return fidl_status;
    }

    // Wait for FVM driver to become visible.
    if ((status = wait_for_device(path(), kDeviceWaitTime.get())) != ZX_OK) {
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

zx_status_t FvmAdapter::Query(VolumeInfo* info) const {
    return fvm_query(fd(), info);
}

fbl::Array<uint8_t> MakeRandomBuffer(size_t size, unsigned int* seed) {
    fbl::Array data(new uint8_t[size], size);

    for (size_t byte = 0; byte < size; ++byte) {
        data[byte] = static_cast<uint8_t>(rand_r(seed));
    }

    return data;
}

bool AreEqual(const fvm::FormatInfo& a, const fvm::FormatInfo& b) {
    return memcmp(&a, &b, sizeof(fvm::FormatInfo)) == 0;
}

bool IsConsistentAfterGrowth(const VolumeInfo& before, const VolumeInfo& after) {
    // Frowing a FVM should not allocate any slices nor should it change the slice size.
    return before.slice_size == after.slice_size &&
           before.pslice_allocated_count == after.pslice_allocated_count;
}

} // namespace fvm
