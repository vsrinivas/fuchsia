// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERFORMANCE_COUNTERS_H
#define PERFORMANCE_COUNTERS_H

#include <mutex>
#include <unordered_set>

#include "address_manager.h"
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
    virtual void OnForceDisabled() = 0;
  };

  explicit PerformanceCounters(Owner* owner) : owner_(owner) {}

  void AddClient(Client* client);
  void RemoveClient(Client* client);

  bool AddManager(PerformanceCountersManager* manager);
  void RemoveManager(PerformanceCountersManager* manager);

  // Update the enabled status of the performance counters based on the current set of managers.
  void Update();
  bool TriggerRead();
  void ReadCompleted();

  void ForceDisable() {
    force_disabled_ = true;
    for (Client* client : clients_)
      client->OnForceDisabled();
    counter_state_ = PerformanceCounterState::kDisabled;
    address_mapping_.reset();
  }

  bool running() const {
    return counter_state_ == PerformanceCounterState::kEnabled ||
           counter_state_ == PerformanceCounterState::kTriggered;
  }

  void RemoveForceDisable() { force_disabled_ = false; }

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
  PerformanceCounterState counter_state_ = PerformanceCounterState::kDisabled;
  std::shared_ptr<MsdArmConnection> connection_;
  std::shared_ptr<MsdArmBuffer> buffer_;
  std::shared_ptr<AddressSlotMapping> address_mapping_;
  uint64_t last_perf_base_ = 0;
  std::chrono::steady_clock::time_point enable_time_;
  bool force_disabled_ = false;

  std::unordered_set<Client*> clients_;
  PerformanceCountersManager* manager_{};
};

#endif  // SRC_GRAPHICS_DRIVERS_MSD_ARM_MALI_SRC_PERFORMANCE_COUNTERS_H_
