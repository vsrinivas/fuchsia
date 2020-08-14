// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fuchsia/hardware/block/volume/c/fidl.h>
#include <lib/zx/channel.h>
#include <limits.h>

#include <memory>
#include <string>
#include <utility>

#include <fbl/array.h>
#include <fbl/string_buffer.h>
#include <fbl/unique_fd.h>
#include <fbl/vector.h>
#include <fvm/fvm.h>
#include <ramdevice-client/ramdisk.h>

// This utility library introduces objects wrapping the devices we interact with, to make it clear
// what we are interacting with, and avoid references to global variables.
namespace fvm {

// Alias for simplicity in testing.
using VolumeInfo = fuchsia_hardware_block_volume_VolumeInfo;
using RamdiskClient = ramdisk_client_t;

constexpr uint64_t kPathMax = PATH_MAX;

constexpr char kFvmDriverLib[] = "/boot/driver/fvm.so";

// Convenient wrapper over uint8_t array.
class Guid {
 public:
  Guid() = default;
  explicit Guid(const uint8_t data[fvm::kGuidSize]) : size_(fvm::kGuidSize) {
    memcpy(data_, data, size_);
  }
  Guid(const uint8_t* data, size_t size) : size_(std::min(size, fvm::kGuidSize)) {
    memcpy(data_, data, size_);
  }
  Guid(const Guid&) = default;
  Guid(Guid&&) = default;
  Guid& operator=(const Guid&) = default;
  Guid& operator=(Guid&&) = default;
  ~Guid() = default;

  const uint8_t* data() const { return data_; }
  size_t size() const { return size_; }

  bool operator==(const Guid& rhs) const {
    return size() == rhs.size() && memcmp(data_, rhs.data(), size_) == 0;
  }

  bool operator!=(const Guid& rhs) const { return !(*this == rhs); }

  std::string ToString() const {
    // Hex string for each byte + space + null string.
    fbl::StringBuffer<fvm::kGuidSize * 5 + 1> hex_string;
    for (size_t i = 0; i < size_; ++i) {
      auto byte = data_[i];
      hex_string.AppendPrintf("0x%X%c", byte, (i == size_ - 1) ? '\0' : ' ');
    }
    return hex_string.c_str();
  }

 private:
  uint8_t data_[fvm::kGuidSize] = {};
  size_t size_ = 0;
};

// Represents a reference to a device, providing communication paths and topological path to it.
// The resources used for communicating with the respective device are released upon going out of
// scope.
class DeviceRef {
 public:
  // Creates a connection to a block device at a given |path|.
  // Returns nullptr on failure.
  static std::unique_ptr<DeviceRef> Create(const fbl::unique_fd& devfs_root,
                                           const std::string& path);

  DeviceRef() = default;
  DeviceRef(const fbl::unique_fd& devfs_root, const std::string& path, fbl::unique_fd fd);
  DeviceRef(const DeviceRef&) = delete;
  DeviceRef(DeviceRef&&) = delete;
  DeviceRef& operator=(const DeviceRef&) = delete;
  DeviceRef& operator=(DeviceRef&&) = delete;
  virtual ~DeviceRef() = default;

  // Channel to communicate with the device.
  zx::unowned_channel channel() const { return zx::unowned_channel(channel_); }

  // Topological path to the device.
  const char* path() const { return path_.c_str(); }

  // File descriptor used to communicate with the device.
  int fd() const { return fd_.get(); }

  int devfs_root_fd() const { return devfs_root_; }

  // Closes the current connection to |device_| and opens a new one based on the path.
  virtual void Reconnect();

 protected:
  // Borrowed FD to the root of devfs.
  int devfs_root_;
  fbl::StringBuffer<kPathMax> path_;
  fbl::unique_fd fd_;
  mutable zx::unowned_channel channel_;
};

// Provides a Base class for other classes that wish to expose helper methods to a block device.
class BlockDeviceAdapter : public DeviceRef {
 public:
  BlockDeviceAdapter(const fbl::unique_fd& devfs_root, const std::string& path, fbl::unique_fd fd)
      : DeviceRef(devfs_root, path, std::move(fd)) {}
  BlockDeviceAdapter(const BlockDeviceAdapter&) = delete;
  BlockDeviceAdapter(BlockDeviceAdapter&&) = delete;
  BlockDeviceAdapter& operator=(const BlockDeviceAdapter&) = delete;
  BlockDeviceAdapter& operator=(BlockDeviceAdapter&&) = delete;
  virtual ~BlockDeviceAdapter() = default;

  // Write |data| into the underlying block device at |offset|.
  void WriteAt(const fbl::Array<uint8_t>& data, uint64_t offset);

  // Reads |data::size()| bytes from the block device, starting at |offset|.
  void ReadAt(uint64_t offset, fbl::Array<uint8_t>* out_data);

  // Checks the contents of the block device at |offset| and verifies it matches |data|.
  void CheckContentsAt(const fbl::Array<uint8_t>& data, uint64_t offset);

  // Returns ZX_OK if the device became visible before the deadlines.
  zx_status_t WaitUntilVisible() const;

  // Returns ZX_OK if the driver Rebind completed within a deadline.
  zx_status_t Rebind();

  virtual const DeviceRef* device() const { return this; }
  virtual DeviceRef* device() { return this; }

 protected:
  BlockDeviceAdapter() = default;
};

// Provides a Ramdisk device that is destroyed upon leaving the scope.
class RamdiskRef final : public BlockDeviceAdapter {
 public:
  // Creates a block device with the respective block count and size.
  // Returns nullptr on failure.
  static std::unique_ptr<RamdiskRef> Create(const fbl::unique_fd& devfs_root, uint64_t block_count,
                                            uint64_t block_size);

  RamdiskRef(const fbl::unique_fd& devfs_root, const std::string& path, fbl::unique_fd fd,
             RamdiskClient* client)
      : BlockDeviceAdapter(devfs_root, path, std::move(fd)), ramdisk_client_(client) {}
  RamdiskRef(const RamdiskRef&) = delete;
  RamdiskRef(RamdiskRef&&) = delete;
  RamdiskRef& operator=(const RamdiskRef&) = delete;
  RamdiskRef& operator=(RamdiskRef&&) = delete;
  ~RamdiskRef() final;

  // Attempts to grow the underlying ramdisk to |target_size|.
  zx_status_t Grow(uint64_t target_size);

 private:
  RamdiskRef() = default;

  // Only set when a ramdisk is created.
  RamdiskClient* ramdisk_client_ = nullptr;
};

// Wrapper over a VPartitionAdapter, that provides common methods using in fvm-tests.
class VPartitionAdapter final : public BlockDeviceAdapter {
 public:
  // Attaches itself to an existing VPartitionAdapter.
  static std::unique_ptr<VPartitionAdapter> Create(const fbl::unique_fd& devfs_root,
                                                   const std::string& name, const Guid& guid,
                                                   const Guid& type);

  VPartitionAdapter(const fbl::unique_fd& devfs_root, zx::unowned_channel channel,
                    const std::string& path, fbl::unique_fd fd, const std::string name,
                    const Guid& guid, const Guid& type)
      : BlockDeviceAdapter(devfs_root, path, std::move(fd)), guid_(guid), type_(type) {
    name_.Append(name.c_str());
  }
  VPartitionAdapter(const VPartitionAdapter&) = delete;
  VPartitionAdapter(VPartitionAdapter&&) = delete;
  VPartitionAdapter& operator=(const VPartitionAdapter&) = delete;
  VPartitionAdapter& operator=(VPartitionAdapter&&) = delete;
  ~VPartitionAdapter() final;

  // Adds |length| slices  at |offset| to the partition.
  zx_status_t Extend(uint64_t offset, uint64_t length);

  void Reconnect() final;

 private:
  VPartitionAdapter() = default;

  fbl::StringBuffer<fvm::kMaxVPartitionNameLength> name_;
  Guid guid_;
  Guid type_;
};

// Wrapper over FVM and common operations, to reduce the boilerplate and complexity of tests.
class FvmAdapter : public DeviceRef {
 public:
  static std::unique_ptr<FvmAdapter> Create(const fbl::unique_fd& devfs_root, uint64_t block_size,
                                            uint64_t block_count, uint64_t slice_size,
                                            DeviceRef* device);

  static std::unique_ptr<FvmAdapter> CreateGrowable(const fbl::unique_fd& devfs_root,
                                                    uint64_t block_size,
                                                    uint64_t initial_block_count,
                                                    uint64_t total_block_count, uint64_t slice_size,
                                                    DeviceRef* device);

  FvmAdapter(const fbl::unique_fd& devfs_root, const std::string& path, fbl::unique_fd fd,
             DeviceRef* block_device)
      : DeviceRef(devfs_root, path, std::move(fd)), block_device_(block_device) {}
  FvmAdapter(const FvmAdapter&) = delete;
  FvmAdapter(FvmAdapter&&) = delete;
  FvmAdapter& operator=(const FvmAdapter&) = delete;
  FvmAdapter& operator=(FvmAdapter&&) = delete;
  ~FvmAdapter();

  zx_status_t AddPartition(const fbl::unique_fd& devfs_root, const std::string& name,
                           const Guid& guid, const Guid& type, uint64_t slice_count,
                           std::unique_ptr<VPartitionAdapter>* out_vpartition);

  // Rebinds the fvm, and waits for each vpartition to become visible.
  zx_status_t Rebind(fbl::Vector<VPartitionAdapter*> vpartitions);

  // Queries the FVM device and sets |out_info|.
  zx_status_t Query(VolumeInfo* out_info) const;

  // Returns a reference to the underlying device.
  const DeviceRef* device() const { return this; }
  DeviceRef* device() { return this; }

 private:
  FvmAdapter() = default;

  // Used for rebinding.
  fbl::StringBuffer<kPathMax> driver_path_;

  // Underlying block device.
  DeviceRef* block_device_;
};

// Returns an array with random contents.
fbl::Array<uint8_t> MakeRandomBuffer(size_t size, unsigned int* seed);

// Returns true if |a| and |b| describe the same format.
bool AreEqual(const fvm::FormatInfo& a, const fvm::FormatInfo& b);

// Returns true if the invariants of the fvm volumes are the same (same slize_size, same allocated
// count).
bool IsConsistentAfterGrowth(const VolumeInfo& before, const VolumeInfo& after);

}  // namespace fs_test_utils
