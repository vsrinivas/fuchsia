// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <assert.h>
#include <initializer_list>
#include <map>
#include <set>
#include <utility>
#include <unittest/unittest.h>
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
        EXPECT_EQ(state_, State::DEVICE_VOID);
        state_ = State::DEVICE_RESET;
    }
    void DeviceConfigRead(uint16_t offset, uint8_t* value) override {
        EXPECT_GT(registers8_.count(offset), 0);
        *value = registers8_[offset];
    }
    void DeviceConfigRead(uint16_t offset, uint16_t* value) override {
        EXPECT_GT(registers16_.count(offset), 0);
        *value = registers16_[offset];
    }
    void DeviceConfigRead(uint16_t offset, uint32_t* value) override {
        EXPECT_GT(registers32_.count(offset), 0);
        *value = registers32_[offset];
    }
    void DeviceConfigRead(uint16_t offset, uint64_t* value) override {
        EXPECT_TRUE(0);  // Not Implemented.
    }
    void DeviceConfigWrite(uint16_t offset, uint8_t value) override {
        registers8_[offset] = value;
    }
    void DeviceConfigWrite(uint16_t offset, uint16_t value) override {
        registers16_[offset] = value;
    }
    void DeviceConfigWrite(uint16_t offset, uint32_t value) override {
        registers32_[offset] = value;
    }
    void DeviceConfigWrite(uint16_t offset, uint64_t value) override {
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
    uint32_t IsrStatus() override { return 0; }
    zx_status_t InterruptValid() override { return ZX_OK; }
    zx_status_t WaitForInterrupt() override { return ZX_OK; }

  protected:
    FakeBackend(std::initializer_list<std::pair<const uint16_t, uint8_t>> registers8,
                std::initializer_list<std::pair<const uint16_t, uint16_t>> registers16,
                std::initializer_list<std::pair<const uint16_t, uint32_t>> registers32,
                std::initializer_list<std::pair<const uint16_t, uint16_t>> queue_sizes):
        registers8_(registers8), registers16_(registers16), registers32_(registers32),
        queue_sizes_(queue_sizes) {}

   // Returns true if a queue has been kicked (notified) and clears the notified bit.
   bool QueueKicked(uint16_t queue_index) {
     bool is_queue_kicked = (kicked_queues_.count(queue_index));
     if (is_queue_kicked) {
       kicked_queues_.erase(queue_index);
     }
     return is_queue_kicked;
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

} // namespace virtio
