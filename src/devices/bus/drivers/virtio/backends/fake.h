// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_DEV_BUS_VIRTIO_BACKENDS_FAKE_H_
#define ZIRCON_SYSTEM_DEV_BUS_VIRTIO_BACKENDS_FAKE_H_

#include <assert.h>

#include <initializer_list>
#include <map>
#include <set>
#include <utility>

#include <zxtest/zxtest.h>

#include "backend.h"

namespace virtio {

// FakeBackend allows writing tests of virtio device drivers.
//
// Tests may subclass FakeBackend and override certain functions to check device/driver
// interactions. FakeBackend also provides a small amount of helper functionality itself - it
// checks the device initialization state machine, tracks valid queues/sizes, and valid config
// registers.
class FakeBackend : public Backend {
 public:
  ~FakeBackend() override = default;

  zx_status_t Bind() override { return ZX_OK; }
  void Unbind() override {}
  bool ReadFeature(uint32_t bit) override { return false; }
  void SetFeature(uint32_t bit) override { EXPECT_NE(state_, State::DRIVER_OK); }
  zx_status_t ConfirmFeatures() override { return ZX_OK; }
  void DriverStatusOk() override {
    EXPECT_EQ(state_, State::DEVICE_STATUS_ACK);
    state_ = State::DRIVER_OK;
  }
  void DriverStatusAck() override {
    EXPECT_EQ(state_, State::DEVICE_RESET);
    state_ = State::DEVICE_STATUS_ACK;
  }
  void DeviceReset() override {
    state_ = State::DEVICE_RESET;
    kicked_queues_.clear();
  }
  void ReadDeviceConfig(uint16_t offset, uint8_t* value) override {
    auto shifted_offset = static_cast<uint16_t>(offset + kISRStatus + 1);
    EXPECT_GT(registers8_.count(shifted_offset), 0, "offset-%xh/8", offset);
    *value = registers8_[shifted_offset];
  }
  void ReadDeviceConfig(uint16_t offset, uint16_t* value) override {
    auto shifted_offset = static_cast<uint16_t>(offset + kISRStatus + 1);
    EXPECT_GT(registers16_.count(shifted_offset), 0, "offset-%xh/16", offset);
    *value = registers16_[shifted_offset];
  }
  void ReadDeviceConfig(uint16_t offset, uint32_t* value) override {
    auto shifted_offset = static_cast<uint16_t>(offset + kISRStatus + 1);
    EXPECT_GT(registers32_.count(shifted_offset), 0, "offset-%xh/32", offset);
    *value = registers32_[shifted_offset];
  }
  void ReadDeviceConfig(uint16_t offset, uint64_t* value) override {
    EXPECT_TRUE(0);  // Not Implemented.
  }
  void WriteDeviceConfig(uint16_t offset, uint8_t value) override {
    auto shifted_offset = static_cast<uint16_t>(offset + kISRStatus + 1);
    registers8_[shifted_offset] = value;
  }
  void WriteDeviceConfig(uint16_t offset, uint16_t value) override {
    auto shifted_offset = static_cast<uint16_t>(offset + kISRStatus + 1);
    registers16_[shifted_offset] = value;
  }
  void WriteDeviceConfig(uint16_t offset, uint32_t value) override {
    auto shifted_offset = static_cast<uint16_t>(offset + kISRStatus + 1);
    registers32_[shifted_offset] = value;
  }
  void WriteDeviceConfig(uint16_t offset, uint64_t value) override {
    EXPECT_TRUE(0);  // Not Implemented.
  }
  uint16_t GetRingSize(uint16_t index) override {
    EXPECT_GT(queue_sizes_.count(index), 0);
    return queue_sizes_[index];
  }
  void SetRing(uint16_t index, uint16_t count, zx_paddr_t pa_desc, zx_paddr_t pa_avail,
               zx_paddr_t pa_used) override {}
  void RingKick(uint16_t ring_index) override {
    EXPECT_EQ(state_, State::DRIVER_OK);
    EXPECT_GT(queue_sizes_.count(ring_index), 0);
    kicked_queues_.insert(ring_index);
  }
  uint32_t IsrStatus() override { return registers8_.find(kISRStatus)->second; }
  zx_status_t InterruptValid() override {
    if (!irq_handle_) {
      return ZX_ERR_BAD_HANDLE;
    }
    return ZX_OK;
  }
  zx_status_t WaitForInterrupt() override { return ZX_OK; }
  void InterruptAck() override {}

 protected:
  // virtio header register offsets.
  static constexpr uint16_t kDeviceFeatures = 0;
  static constexpr uint16_t kGuestFeatures = 4;
  static constexpr uint16_t kQueueAddress = 8;
  static constexpr uint16_t kQueueSize = 12;
  static constexpr uint16_t kQueueSelect = 14;
  static constexpr uint16_t kQueueNotify = 16;
  static constexpr uint16_t kDeviceStatus = 18;
  static constexpr uint16_t kISRStatus = 19;

  explicit FakeBackend(std::initializer_list<std::pair<const uint16_t, uint16_t>> queue_sizes)
      : queue_sizes_(queue_sizes) {
    // Bind standard virtio header registers into register maps.
    registers32_.insert({kDeviceFeatures, 0});
    registers32_.insert({kGuestFeatures, 0});
    registers32_.insert({kQueueAddress, 0});
    registers16_.insert({kQueueSize, 0});
    registers16_.insert({kQueueSelect, 0});
    registers16_.insert({kQueueNotify, 0});
    registers8_.insert({kDeviceStatus, 0});
    registers8_.insert({kISRStatus, 0});
  }

  // Returns true if a queue has been kicked (notified) and clears the notified bit.
  bool QueueKicked(uint16_t queue_index) {
    bool is_queue_kicked = (kicked_queues_.count(queue_index));
    if (is_queue_kicked) {
      kicked_queues_.erase(queue_index);
    }
    return is_queue_kicked;
  }

  template <typename T>
  void AddClassRegister(uint16_t offset, T value) {
    if constexpr (sizeof(T) == 1) {
      registers8_.insert({kISRStatus + 1 + offset, value});
    } else if constexpr (sizeof(T) == 2) {
      registers16_.insert({kISRStatus + 1 + offset, value});
    } else if constexpr (sizeof(T) == 4) {
      registers32_.insert({kISRStatus + 1 + offset, value});
    }
  }

  template <typename T>
  void SetRegister(uint16_t offset, T value) {
    if constexpr (sizeof(T) == 1) {
      registers8_[offset] = value;
    } else if constexpr (sizeof(T) == 2) {
      registers16_[offset] = value;
    } else if constexpr (sizeof(T) == 4) {
      registers32_[offset] = value;
    }
  }

  template <typename T>
  void ReadRegister(uint16_t offset, T* output) {
    if constexpr (sizeof(T) == 1) {
      *output = registers8_.find(offset)->second;
    } else if constexpr (sizeof(T) == 2) {
      *output = registers16_.find(offset)->second;
    } else if constexpr (sizeof(T) == 4) {
      *output = registers32_.find(offset)->second;
    }
  }

 private:
  enum class State {
    DEVICE_VOID,
    DEVICE_RESET,
    DEVICE_STATUS_ACK,
    DRIVER_OK,
  };

  State state_ = State::DEVICE_VOID;
  std::map<uint16_t, uint8_t> registers8_;
  std::map<uint16_t, uint16_t> registers16_;
  std::map<uint16_t, uint32_t> registers32_;
  std::map<uint16_t, uint16_t> queue_sizes_;
  std::set<uint16_t> kicked_queues_;
};

}  // namespace virtio

#endif  // ZIRCON_SYSTEM_DEV_BUS_VIRTIO_BACKENDS_FAKE_H_
