// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PLATFORM_DEVICE_H
#define PLATFORM_DEVICE_H

#include <chrono>
#include <memory>

#include "magma_util/dlog.h"
#include "magma_util/status.h"
#include "platform_buffer.h"
#include "platform_handle.h"
#include "platform_interrupt.h"
#include "platform_mmio.h"

namespace magma {

class PlatformDevice {
 public:
  // See zircon/syscalls/profile.h
  enum Priority {
    kPriorityLowest = 0,
    kPriorityLow = 8,
    kPriorityDefault = 16,
    kPriorityHigher = 20,
    kPriorityHigh = 24,
    kPriorityHighest = 31
  };

  virtual ~PlatformDevice() { DLOG("PlatformDevice dtor"); }

  virtual void* GetDeviceHandle() = 0;

  // Get a driver-specific protocol implementation. |proto_id| identifies which
  // protocol to retrieve.
  virtual bool GetProtocol(uint32_t proto_id, void* proto_out) = 0;

  virtual uint32_t GetMmioCount() const = 0;

  virtual std::unique_ptr<PlatformHandle> GetBusTransactionInitiator() const = 0;

  virtual std::unique_ptr<PlatformHandle> GetIommuConnector() const {
    return DRETP(nullptr, "GetIommuConnector not implemented");
  }

  virtual Status LoadFirmware(const char* filename, std::unique_ptr<PlatformBuffer>* firmware_out,
                              uint64_t* size_out) const = 0;

  // Map an MMIO listed at |index| in the MDI for this device.
  virtual std::unique_ptr<PlatformMmio> CpuMapMmio(unsigned int index,
                                                   PlatformMmio::CachePolicy cache_policy) {
    DLOG("CpuMapMmio unimplemented");
    return nullptr;
  }

  virtual std::unique_ptr<PlatformBuffer> GetMmioBuffer(unsigned int index) {
    DLOG("GetMmioBuffer unimplemented");
    return nullptr;
  }

  // Register an interrupt listed at |index| in the MDI for this device.
  virtual std::unique_ptr<PlatformInterrupt> RegisterInterrupt(unsigned int index) {
    DLOG("RegisterInterrupt unimplemented");
    return nullptr;
  }

  // Ownership of |device_handle| is *not* transferred to the PlatformDevice.
  static std::unique_ptr<PlatformDevice> Create(void* device_handle);
};

}  // namespace magma

#endif  // PLATFORM_DEVICE_H
