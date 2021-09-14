// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERFORMANCE_COUNTERS_H
#define PERFORMANCE_COUNTERS_H

#include <lib/fit/thread_checker.h>

#include <mutex>
#include <thread>
#include <unordered_set>

#include "address_manager.h"
#include "gpu_features.h"
#include "magma_util/macros.h"
#include "msd_arm_buffer.h"
#include "performance_counters_manager.h"

class MsdArmConnection;

// This class should be accessed only on the device thread.
class PerformanceCounters {
 public:
  class Owner {
   public:
    virtual magma::RegisterIo* register_io() = 0;
    virtual AddressManager* address_manager() = 0;
    virtual MsdArmConnection::Owner* connection_owner() = 0;
  };

  // The client is what receives perf count dumps. If multiple clients are connected, each of them
  // will receive the same data.
  class Client {
   public:
    virtual void OnPerfCountDump(const std::vector<uint32_t>& dumped) = 0;
    // Called if the performance counters are cleared or become temporarily unavailable. Can happen
    // due to switching into protected mode.
    virtual void OnPerfCountersCanceled(size_t perf_counter_size) = 0;
  };

  explicit PerformanceCounters(Owner* owner) : owner_(owner) {
    // Until the device thread starts its safe to modify this data from the initial thread.
    SetDeviceThreadId(std::this_thread::get_id());
  }

  void SetGpuFeatures(const GpuFeatures& gpu_features);

  void SetDeviceThreadId(std::thread::id device_thread_id) {
    device_thread_checker_.emplace(device_thread_id);
  }

  void AddClient(Client* client);
  void RemoveClient(Client* client);

  bool AddManager(PerformanceCountersManager* manager);
  void RemoveManager(PerformanceCountersManager* manager);

  // Update the enabled status of the performance counters based on the current set of managers.
  void Update();
  bool TriggerRead();
  void ReadCompleted();

  void ForceDisable();
  void TriggerCanceledClients() MAGMA_REQUIRES(*device_thread_checker_);

  bool running() const {
    std::lock_guard<fit::thread_checker> lock(*device_thread_checker_);
    return counter_state_ == PerformanceCounterState::kEnabled ||
           counter_state_ == PerformanceCounterState::kTriggered;
  }

  void RemoveForceDisable() {
    std::lock_guard<fit::thread_checker> lock(*device_thread_checker_);
    force_disabled_ = false;
  }

  bool force_disabled() const {
    std::lock_guard<fit::thread_checker> lock(*device_thread_checker_);
    return force_disabled_;
  }

 private:
  friend class PerformanceCounterTest;
  enum class PerformanceCounterState {
    kDisabled,
    kEnabled,
    kTriggered,
    kTriggeredWillBeDisabled,
  };

  bool ShouldBeEnabled();
  bool Enable();
  bool Disable();

  Owner* owner_;
  // Size of the performance counter block in bytes.
  MAGMA_GUARDED(*device_thread_checker_) size_t perf_counter_size_ = 0;
  mutable std::optional<fit::thread_checker> device_thread_checker_;
  MAGMA_GUARDED(*device_thread_checker_)
  PerformanceCounterState counter_state_ = PerformanceCounterState::kDisabled;
  MAGMA_GUARDED(*device_thread_checker_) std::shared_ptr<MsdArmConnection> connection_;
  MAGMA_GUARDED(*device_thread_checker_) std::shared_ptr<MsdArmBuffer> buffer_;
  MAGMA_GUARDED(*device_thread_checker_) std::shared_ptr<AddressSlotMapping> address_mapping_;
  MAGMA_GUARDED(*device_thread_checker_) uint64_t last_perf_base_ = 0;
  MAGMA_GUARDED(*device_thread_checker_) std::chrono::steady_clock::time_point enable_time_;
  MAGMA_GUARDED(*device_thread_checker_) bool force_disabled_ = false;

  MAGMA_GUARDED(*device_thread_checker_) std::unordered_set<Client*> clients_;
  MAGMA_GUARDED(*device_thread_checker_) PerformanceCountersManager* manager_{};
};

#endif  // SRC_GRAPHICS_DRIVERS_MSD_ARM_MALI_SRC_PERFORMANCE_COUNTERS_H_
