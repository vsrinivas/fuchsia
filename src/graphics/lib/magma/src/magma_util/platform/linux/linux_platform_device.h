// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LINUX_PLATFORM_DEVICE_H
#define LINUX_PLATFORM_DEVICE_H

#include <magma_util/macros.h>

#include "linux_platform_handle.h"
#include "linux_platform_mmio.h"
#include "platform_device.h"

namespace magma {

class LinuxPlatformDevice : public PlatformDevice {
 public:
  LinuxPlatformDevice(LinuxPlatformHandle handle) : handle_(handle.release()) {}

  // Don't close the handle because we don't own it.
  ~LinuxPlatformDevice() { handle_.release(); }

  int fd() { return handle_.get(); }

  void* GetDeviceHandle() override { return reinterpret_cast<void*>(fd()); }

  std::unique_ptr<PlatformHandle> GetSchedulerProfile(Priority priority,
                                                      const char* name) const override {
    return DRETP(nullptr, "GetSchedulerProfile not implemented");
  }

  std::unique_ptr<PlatformHandle> GetDeadlineSchedulerProfile(std::chrono::nanoseconds capacity_ns,
                                                              std::chrono::nanoseconds deadline_ns,
                                                              std::chrono::nanoseconds period_ns,
                                                              const char* name) const override {
    return DRETP(nullptr, "GetDeadlineSchedulerProfile not implemented");
  }

  std::unique_ptr<PlatformHandle> GetBusTransactionInitiator() const override;

  std::unique_ptr<PlatformHandle> GetIommuConnector() const override;

  Status LoadFirmware(const char* filename, std::unique_ptr<PlatformBuffer>* firmware_out,
                      uint64_t* size_out) const override;

  std::unique_ptr<PlatformMmio> CpuMapMmio(unsigned int index,
                                           PlatformMmio::CachePolicy cache_policy) override;

  std::unique_ptr<PlatformInterrupt> RegisterInterrupt(unsigned int index) override {
    return DRETP(nullptr, "RegisterInterrupt not implemented");
  }

  static bool UdmabufCreate(int udmabuf_fd, int mem_fd, uint64_t page_start_index,
                            uint64_t page_count, int* dma_buf_fd_out);

  enum class MagmaGetParamKey {
    kRegisterSize = 10,
    kChipId = 11,
    kGmemSize = 12,
  };

  static bool MagmaGetParam(int device_fd, MagmaGetParamKey key, uint64_t* value_out);

  static bool MagmaMapPageRangeBus(int device_fd, int dma_buf_fd, uint64_t start_page_index,
                                   uint64_t page_count, uint64_t* token_out,
                                   uint64_t* bus_addr_out);

  // Maps if |map| is true; unmaps otherwise
  static bool MagmaMapGpu(int device_fd, bool map, uint64_t gpu_addr, uint32_t token);

  static void MagmaResetGmu(int device_fd);

 private:
  LinuxPlatformHandle handle_;
};

}  // namespace magma

#endif  // ZIRCON_PLATFORM_DEVICE_H
