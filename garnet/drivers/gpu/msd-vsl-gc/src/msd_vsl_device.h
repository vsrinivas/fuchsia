// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MSD_VSL_DEVICE_H
#define MSD_VSL_DEVICE_H

#include <memory>

#include "gpu_features.h"
#include "magma_util/macros.h"
#include "magma_util/register_io.h"
#include "magma_vsl_gc_types.h"
#include "msd.h"
#include "msd_vsl_connection.h"
#include "page_table_arrays.h"
#include "page_table_slot_allocator.h"
#include "platform_bus_mapper.h"
#include "platform_device.h"

class MsdVslDevice : public msd_device_t, public MsdVslConnection::Owner {
 public:
  // Creates a device for the given |device_handle| and returns ownership.
  static std::unique_ptr<MsdVslDevice> Create(void* device_handle, bool enable_mmu);

  MsdVslDevice() { magic_ = kMagic; }

  virtual ~MsdVslDevice() = default;

  uint32_t device_id() { return device_id_; }

  bool IsIdle();

  std::unique_ptr<MsdVslConnection> Open(msd_client_id_t client_id);

  magma_status_t ChipIdentity(magma_vsl_gc_chip_identity* out_identity);
  magma_status_t ChipOption(magma_vsl_gc_chip_option* out_option);

  static MsdVslDevice* cast(msd_device_t* dev) {
    DASSERT(dev);
    DASSERT(dev->magic_ == kMagic);
    return static_cast<MsdVslDevice*>(dev);
  }

 private:
  bool Init(void* device_handle, bool enable_mmu);
  void HardwareInit(bool enable_mmu);
  void Reset();

  bool SubmitCommandBufferNoMmu(uint64_t bus_addr, uint32_t length, uint16_t* prefetch_out);
  bool SubmitCommandBuffer(uint32_t gpu_addr, uint32_t length, uint16_t* prefetch_out);

  magma::RegisterIo* register_io() { return register_io_.get(); }

  // MsdVslConnection::Owner
  magma::PlatformBusMapper* GetBusMapper() override { return bus_mapper_.get(); }

  void ConnectionReleased(MsdVslConnection* connection) override {
    page_table_slot_allocator_->Free(connection->page_table_array_slot());
  }

  PageTableArrays* page_table_arrays() { return page_table_arrays_.get(); }

  static const uint32_t kMagic = 0x64657669;  //"devi"

  std::unique_ptr<magma::PlatformDevice> platform_device_;
  std::unique_ptr<magma::RegisterIo> register_io_;
  std::unique_ptr<GpuFeatures> gpu_features_;
  uint32_t device_id_ = 0;
  std::unique_ptr<magma::PlatformBusMapper> bus_mapper_;
  std::unique_ptr<PageTableArrays> page_table_arrays_;
  std::unique_ptr<PageTableSlotAllocator> page_table_slot_allocator_;

  friend class TestMsdVslDevice;
  friend class MsdVslDeviceTest_FetchEngineDma_Test;
  friend class MsdVslDeviceTest_LoadAddressSpace_Test;
};

#endif  // MSD_VSL_DEVICE_H
