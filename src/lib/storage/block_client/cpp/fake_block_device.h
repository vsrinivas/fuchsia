// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_STORAGE_BLOCK_CLIENT_CPP_FAKE_BLOCK_DEVICE_H_
#define SRC_LIB_STORAGE_BLOCK_CLIENT_CPP_FAKE_BLOCK_DEVICE_H_

#include <lib/zx/vmo.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>

#include <functional>
#include <map>
#include <optional>

#include <fbl/condition_variable.h>
#include <fbl/mutex.h>
#include <range/range.h>
#include <storage-metrics/block-metrics.h>

#include "src/lib/storage/block_client/cpp/block_device.h"

namespace block_client {

// A fake device implementing (most of) the BlockDevice interface on top of an in-memory VMO
// representing the device. This allows clients of the BlockDevice interface to test against this
// fake in-process instead of relying on a real block device.
//
// This device also supports pausing processing FIFO transactions to allow tests to emulate slow
// devices or validate behavior in intermediate states.
//
// This class is thread-safe.
// This class is not movable or copyable.
class FakeBlockDevice : public BlockDevice {
 public:
  struct Config {
    uint64_t block_count = 0;
    uint32_t block_size = 0;
    bool supports_trim = false;
    uint32_t max_transfer_size = fuchsia_hardware_block::wire::kMaxTransferUnbounded;
  };
  explicit FakeBlockDevice(const Config&);
  FakeBlockDevice(uint64_t block_count, uint32_t block_size)
      : FakeBlockDevice({block_count, block_size, false}) {}
  FakeBlockDevice(const FakeBlockDevice&) = delete;
  FakeBlockDevice& operator=(const FakeBlockDevice&) = delete;
  FakeBlockDevice(FakeBlockDevice&& other) = delete;
  FakeBlockDevice& operator=(FakeBlockDevice&& other) = delete;

  ~FakeBlockDevice() override = default;

  // Sets a callback which will be invoked for each FIFO request that is received by the block
  // device. (If the FIFO request targets a VMO, |vmo| will be set as well.)
  // Note that if any request in a FIFO transaction fails, the transaction is immediately aborted.
  // In that case, the failing request will still be sent into the callback, but the other requests
  // in the transaction may or may not also be sent into the callback. (In practice, requests are
  // processed in order, so all requests after the first failing request wouldn't be processed.)
  // Not thread safe.  Should be called only when the device is not active.
  using Hook = std::function<zx_status_t(const block_fifo_request_t& request, const zx::vmo* vmo)>;
  void set_hook(Hook hook) { hook_ = std::move(hook); }

  // When paused, this device will make FIFO operations block until Resume() is called. The device
  // is in the Resume() state by default.
  void Pause();
  void Resume();

  // Sets the number of blocks which may be written to the block device. Once |limit| is reached,
  // all following operations will return ZX_ERR_IO.
  //
  // May be "std::nullopt" to allow an unlimited count of blocks.
  void SetWriteBlockLimit(uint64_t limit);

  // Turns off the "write block limit".
  void ResetWriteBlockLimit();
  uint64_t GetWriteBlockCount() const;
  void ResetBlockCounts();

  void SetInfoFlags(fuchsia_hardware_block::wire::Flag flags);
  void SetBlockCount(uint64_t block_count);
  void SetBlockSize(uint32_t block_size);
  bool IsRegistered(vmoid_t vmoid) const;

  void GetStats(bool clear, fuchsia_hardware_block::wire::BlockStats* out_stats);

  // Wipes the device to a zeroed state.
  void Wipe();

  // BlockDevice interface

  zx::result<std::string> GetDevicePath() const override { return zx::error(ZX_ERR_NOT_SUPPORTED); }

  zx_status_t VolumeGetInfo(
      fuchsia_hardware_block_volume::wire::VolumeManagerInfo* out_manager_info,
      fuchsia_hardware_block_volume::wire::VolumeInfo* out_volume_info) const override {
    return ZX_ERR_NOT_SUPPORTED;
  }

  zx_status_t VolumeQuerySlices(const uint64_t* slices, size_t slices_count,
                                fuchsia_hardware_block_volume::wire::VsliceRange* out_ranges,
                                size_t* out_ranges_count) const override {
    return ZX_ERR_NOT_SUPPORTED;
  }

  zx_status_t VolumeExtend(uint64_t offset, uint64_t length) override {
    return ZX_ERR_NOT_SUPPORTED;
  }

  zx_status_t VolumeShrink(uint64_t offset, uint64_t length) override {
    return ZX_ERR_NOT_SUPPORTED;
  }

  zx_status_t FifoTransaction(block_fifo_request_t* requests, size_t count) override;
  zx_status_t BlockGetInfo(fuchsia_hardware_block::wire::BlockInfo* out_info) const override;
  zx_status_t BlockAttachVmo(const zx::vmo& vmo, storage::Vmoid* out_vmoid) final;

 protected:
  // Resizes the block device to be at least |new_size| bytes.
  void ResizeDeviceToAtLeast(uint64_t new_size);

 private:
  void AdjustBlockDeviceSizeLocked(uint64_t new_size) __TA_REQUIRES(lock_);
  void UpdateStats(bool success, zx::ticks start_tick, const block_fifo_request_t& op)
      __TA_REQUIRES(lock_);

  // Waits, blocking the current thread, until execution is not paused.
  void WaitOnPaused() const __TA_REQUIRES(lock_);

  mutable fbl::Mutex lock_ = {};

  // For handling paused_ waiters. Use BlockOnPaused() to wait on this.
  mutable fbl::ConditionVariable pause_condition_;

  bool paused_ __TA_GUARDED(lock_) = false;

  // The number of transactions which may occur before I/O errors are returned
  // to callers. If "nullopt", no limit is set.
  std::optional<uint64_t> write_block_limit_ __TA_GUARDED(lock_) = std::nullopt;
  uint64_t write_block_count_ __TA_GUARDED(lock_) = 0;

  uint64_t block_count_ __TA_GUARDED(lock_) = 0;
  uint32_t block_size_ __TA_GUARDED(lock_) = 0;
  fuchsia_hardware_block::wire::Flag block_info_flags_ __TA_GUARDED(lock_) = {};
  uint32_t max_transfer_size_ __TA_GUARDED(lock_) = 0;
  std::map<vmoid_t, zx::vmo> vmos_ __TA_GUARDED(lock_);
  zx::vmo block_device_ __TA_GUARDED(lock_);
  mutable storage_metrics::BlockDeviceMetrics stats_ __TA_GUARDED(lock_) = {};
  Hook hook_;
};

// An extension of FakeBlockDevice that allows for testing on FVM devices.
//
// This class is thread-safe.
// This class is not movable or copyable.
class FakeFVMBlockDevice : public FakeBlockDevice {
 public:
  FakeFVMBlockDevice(uint64_t block_count, uint32_t block_size, uint64_t slice_size,
                     uint64_t slice_capacity);

  zx_status_t FifoTransaction(block_fifo_request_t* requests, size_t count) final;
  zx_status_t VolumeGetInfo(
      fuchsia_hardware_block_volume::wire::VolumeManagerInfo* out_manager_info,
      fuchsia_hardware_block_volume::wire::VolumeInfo* out_volume_info) const final;
  zx_status_t VolumeQuerySlices(const uint64_t* slices, size_t slices_count,
                                fuchsia_hardware_block_volume::wire::VsliceRange* out_ranges,
                                size_t* out_ranges_count) const final;
  zx_status_t VolumeExtend(uint64_t offset, uint64_t length) final;
  zx_status_t VolumeShrink(uint64_t offset, uint64_t length) final;

 private:
  mutable fbl::Mutex fvm_lock_ = {};

  fuchsia_hardware_block_volume::wire::VolumeManagerInfo manager_info_ __TA_GUARDED(fvm_lock_) = {};
  fuchsia_hardware_block_volume::wire::VolumeInfo volume_info_ __TA_GUARDED(fvm_lock_) = {};

  // Start Slice -> Range.
  std::map<uint64_t, range::Range<uint64_t>> extents_ __TA_GUARDED(fvm_lock_);
};

}  // namespace block_client

#endif  // SRC_LIB_STORAGE_BLOCK_CLIENT_CPP_FAKE_BLOCK_DEVICE_H_
