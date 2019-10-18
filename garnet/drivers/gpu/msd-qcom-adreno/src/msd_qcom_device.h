// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MSD_QCOM_DEVICE_H
#define MSD_QCOM_DEVICE_H

#include <platform_bus_mapper.h>

#include <magma_util/register_io.h>

#include "allocating_address_space.h"
#include "firmware.h"
#include "msd_qcom_platform_device.h"
#include "ringbuffer.h"

class MsdQcomDevice : public magma::AddressSpaceOwner {
 public:
  static std::unique_ptr<MsdQcomDevice> Create(void* device_handle);

  uint32_t GetChipId() { return qcom_platform_device_->GetChipId(); }

  uint32_t GetGmemSize() { return qcom_platform_device_->GetGmemSize(); }

  static void GetCpInitPacket(std::vector<uint32_t>& packet);

 private:
  std::shared_ptr<AddressSpace> address_space() { return address_space_; }
  magma::RegisterIo* register_io() { return register_io_.get(); }
  magma::Ringbuffer<GpuMapping>* ringbuffer() { return ringbuffer_.get(); }
  Firmware* firmware() { return firmware_.get(); }

  // magma::AddressSpaceOwner
  magma::PlatformBusMapper* GetBusMapper() override { return bus_mapper_.get(); }

  static constexpr uint64_t kGmemGpuAddrBase = 0x00100000;
  static constexpr uint64_t kClientGpuAddrBase = 0x01000000;

  bool Init(void* device_handle, std::unique_ptr<magma::RegisterIo::Hook> hook);
  bool HardwareInit();
  bool EnableClockGating(bool enable);
  bool InitRingbuffer();
  bool InitControlProcessor();
  void FlushRingbuffer(uint32_t tail);
  bool WaitForIdleRingbuffer(uint32_t tail);

  std::unique_ptr<MsdQcomPlatformDevice> qcom_platform_device_;
  std::unique_ptr<magma::RegisterIo> register_io_;
  std::unique_ptr<magma::PlatformBusMapper> bus_mapper_;
  std::shared_ptr<AllocatingAddressSpace> address_space_;
  std::unique_ptr<Ringbuffer> ringbuffer_;
  std::unique_ptr<Firmware> firmware_;

  friend class TestQcomDevice;
};

#endif  // MSD_QCOM_DEVICE_H
