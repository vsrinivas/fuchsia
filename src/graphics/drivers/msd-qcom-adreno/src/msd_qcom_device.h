// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MSD_QCOM_DEVICE_H
#define MSD_QCOM_DEVICE_H

#include <msd.h>
#include <platform_bus_mapper.h>

#include <magma_util/register_io.h>

#include "allocating_address_space.h"
#include "firmware.h"
#include "msd_qcom_connection.h"
#include "msd_qcom_platform_device.h"
#include "ringbuffer.h"

class MsdQcomDevice : public msd_device_t,
                      public magma::AddressSpaceOwner,
                      public MsdQcomConnection::Owner {
 public:
  static std::unique_ptr<MsdQcomDevice> Create(void* device_handle);

  MsdQcomDevice() { magic_ = kMagic; }

  std::unique_ptr<MsdQcomConnection> Open(msd_client_id_t client_id);

  uint32_t GetChipId() const { return qcom_platform_device_->GetChipId(); }

  uint32_t GetGmemSize() const { return qcom_platform_device_->GetGmemSize(); }

  magma_status_t Query(uint64_t id, uint64_t* value_out) const;

  static void GetCpInitPacket(std::vector<uint32_t>& packet);

  static MsdQcomDevice* cast(msd_device_t* dev) {
    DASSERT(dev);
    DASSERT(dev->magic_ == kMagic);
    return static_cast<MsdQcomDevice*>(dev);
  }

 private:
  std::shared_ptr<AddressSpace> address_space() { return address_space_; }
  magma::RegisterIo* register_io() { return register_io_.get(); }
  magma::Ringbuffer<GpuMapping>* ringbuffer() { return ringbuffer_.get(); }
  Firmware* firmware() { return firmware_.get(); }

  // magma::AddressSpaceOwner, MsdQcomConnection::Owner
  magma::PlatformBusMapper* GetBusMapper() override { return bus_mapper_.get(); }

  static const uint32_t kMagic = 0x64657669;  //"devi"

  // Maximum size for GMEM.
  static constexpr uint64_t kGmemGpuAddrSize = 0x01000000;
  // Maximum size for system allocations (firmware, ringbuffers).
  static constexpr uint64_t kSystemGpuAddrSize = 0x01000000;
  // Remainder of the address space allocated to client.
  // TODO(fxbug.dev/44002) - support for greater than 32 bits of address space.
  static constexpr uint64_t kClientGpuAddrSize =
      (1ull << 32) - kGmemGpuAddrSize - kSystemGpuAddrSize;

  static constexpr uint64_t kClientGpuAddrBase = 0;
  static constexpr uint64_t kSystemGpuAddrBase = kClientGpuAddrBase + kClientGpuAddrSize;
  static constexpr uint64_t kGmemGpuAddrBase = kSystemGpuAddrBase + kGmemGpuAddrSize;

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
  std::shared_ptr<magma::PlatformIommu> iommu_;
  std::shared_ptr<PartialAllocatingAddressSpace> address_space_;
  std::unique_ptr<Ringbuffer> ringbuffer_;
  std::unique_ptr<Firmware> firmware_;

  friend class TestQcomDevice;
};

#endif  // MSD_QCOM_DEVICE_H
