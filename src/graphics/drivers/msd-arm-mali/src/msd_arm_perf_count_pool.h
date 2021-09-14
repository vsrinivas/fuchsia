// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DRIVERS_MSD_ARM_MALI_SRC_MSD_ARM_PERF_COUNT_POOL_H_
#define SRC_GRAPHICS_DRIVERS_MSD_ARM_MALI_SRC_MSD_ARM_PERF_COUNT_POOL_H_

#include <lib/fit/thread_checker.h>

#include <list>
#include <vector>

#include "magma_util/macros.h"
#include "msd.h"
#include "msd_arm_buffer.h"
#include "performance_counters.h"

// All interaction with this class must happen on the device thread.
class MsdArmPerfCountPool : public PerformanceCounters::Client {
 public:
  MsdArmPerfCountPool(std::shared_ptr<MsdArmConnection> connection, uint64_t pool_id)
      : device_thread_checker_(connection->GetDeviceThreadId()),
        connection_(connection),
        pool_id_(pool_id) {}

  virtual ~MsdArmPerfCountPool() = default;

  void set_valid(bool valid) {
    std::lock_guard lock(device_thread_checker_);
    valid_ = false;
  }
  uint64_t pool_id() { return pool_id_; }

  // PerformanceCounters::Client implementation.
  void OnPerfCountDump(const std::vector<uint32_t>& dumped) override;
  void OnPerfCountersCanceled(size_t perf_count_size) override;

  void AddBuffer(std::shared_ptr<MsdArmBuffer> buffer, uint64_t buffer_id, uint64_t offset,
                 uint64_t size);
  void RemoveBuffer(std::shared_ptr<MsdArmBuffer> buffer);
  void AddTriggerId(uint32_t trigger_id);

 private:
  void OnPerfCountDumpLocked(const std::vector<uint32_t>& dumped)
      MAGMA_REQUIRES(device_thread_checker_);

  fit::thread_checker device_thread_checker_;

  struct BufferOffset {
    std::shared_ptr<MsdArmBuffer> buffer;
    uint64_t buffer_id;
    uint64_t offset;
    uint64_t size;
  };

  MAGMA_GUARDED(device_thread_checker_) std::weak_ptr<MsdArmConnection> connection_;
  // If valid_ is false, this pool is in the process of being torn down.
  MAGMA_GUARDED(device_thread_checker_) bool valid_ = true;
  const uint64_t pool_id_;

  MAGMA_GUARDED(device_thread_checker_) std::list<BufferOffset> buffers_;
  MAGMA_GUARDED(device_thread_checker_) std::vector<uint32_t> triggers_;
  MAGMA_GUARDED(device_thread_checker_) bool discontinuous_ = true;

  static constexpr uint32_t kMagic = 'MPCP';
};

class MsdArmAbiPerfCountPool : public msd_perf_count_pool {
 public:
  MsdArmAbiPerfCountPool(std::shared_ptr<MsdArmPerfCountPool> ptr) : ptr_(std::move(ptr)) {
    magic_ = kMagic;
  }

  static MsdArmAbiPerfCountPool* cast(msd_perf_count_pool* pool) {
    DASSERT(pool);
    DASSERT(pool->magic_ == kMagic);
    return static_cast<MsdArmAbiPerfCountPool*>(pool);
  }

  std::shared_ptr<MsdArmPerfCountPool> ptr() { return ptr_; }

 private:
  std::shared_ptr<MsdArmPerfCountPool> ptr_;
  static const uint32_t kMagic = 'MPCP';
};

#endif  // SRC_GRAPHICS_DRIVERS_MSD_ARM_MALI_SRC_MSD_ARM_PERF_COUNT_POOL_H_
