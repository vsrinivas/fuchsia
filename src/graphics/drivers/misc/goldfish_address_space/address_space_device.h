// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DRIVERS_MISC_GOLDFISH_ADDRESS_SPACE_ADDRESS_SPACE_DEVICE_H_
#define SRC_GRAPHICS_DRIVERS_MISC_GOLDFISH_ADDRESS_SPACE_ADDRESS_SPACE_DEVICE_H_

#include <fuchsia/hardware/goldfish/c/fidl.h>
#include <lib/mmio/mmio.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <lib/zx/bti.h>
#include <zircon/types.h>

#include <map>

#include <ddk/device.h>
#include <ddk/io-buffer.h>
#include <ddktl/device.h>
#include <ddktl/protocol/pci.h>
#include <fbl/mutex.h>

namespace goldfish {

class AddressSpaceDevice;
using DeviceType = ddk::Device<AddressSpaceDevice, ddk::UnbindableNew, ddk::Openable>;

class AddressSpaceChildDriver;
using ChildDriverType = ddk::Device<AddressSpaceChildDriver, ddk::Messageable, ddk::Closable>;

class AddressSpaceDevice : public DeviceType {
 public:
  static zx_status_t Create(void* ctx, zx_device_t* parent);

  explicit AddressSpaceDevice(zx_device_t* parent);
  ~AddressSpaceDevice();

  zx_status_t Bind();

  uint32_t AllocateBlock(uint64_t* size, uint64_t* offset);
  uint32_t DeallocateBlock(uint64_t offset);
  zx_status_t PinBlock(uint64_t offset, uint64_t size, zx_paddr_t* paddr, zx::pmt* pmt,
                       zx::vmo* vmo);

  zx_status_t CreateChildDriver(ddk::IoBuffer* io_buffer, uint32_t* handle);
  uint32_t DestroyChildDriver(uint32_t handle);
  uint32_t ChildDriverPing(uint32_t handle);

  // Device protocol implementation.
  zx_status_t DdkOpen(zx_device_t** dev_out, uint32_t flags);
  void DdkUnbindNew(ddk::UnbindTxn txn);
  void DdkRelease();

 private:
  uint32_t CommandMmioLocked(uint32_t cmd) TA_REQ(mmio_lock_);

  ddk::PciProtocolClient pci_;
  zx::bti bti_;
  zx::vmo dma_region_;
  uint64_t dma_region_paddr_;

  fbl::Mutex mmio_lock_;
  std::optional<ddk::MmioBuffer> mmio_ TA_GUARDED(mmio_lock_);

  DISALLOW_COPY_ASSIGN_AND_MOVE(AddressSpaceDevice);
};

class AddressSpaceChildDriver : public ChildDriverType {
 public:
  explicit AddressSpaceChildDriver(fuchsia_hardware_goldfish_AddressSpaceChildDriverType type,
                                   AddressSpaceDevice* device, uint64_t dma_region_paddr,
                                   ddk::IoBuffer&& io_buffer, uint32_t child_device_handle);
  ~AddressSpaceChildDriver();

  zx_status_t Bind();

  zx_status_t FidlAllocateBlock(uint64_t size, fidl_txn_t* txn);
  zx_status_t FidlDeallocateBlock(uint64_t paddr, fidl_txn_t* txn);
  zx_status_t FidlClaimSharedBlock(uint64_t offset, uint64_t size, fidl_txn_t* txn);
  zx_status_t FidlUnclaimSharedBlock(uint64_t offset, fidl_txn_t* txn);
  zx_status_t FidlPing(const fuchsia_hardware_goldfish_AddressSpaceChildDriverPingMessage* ping,
                       fidl_txn_t* txn);
  zx_status_t DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn);
  zx_status_t DdkClose(uint32_t flags);
  void DdkRelease();

 private:
  struct Block {
    uint64_t offset;
    uint64_t size;
    zx::pmt pmt;
  };
  AddressSpaceDevice* const device_;
  const uint64_t dma_region_paddr_;

  ddk::IoBuffer io_buffer_;
  const uint32_t handle_;

  // TODO(TC-383): This should be std::unordered_map.
  using BlockMap = std::map<uint64_t, Block>;
  BlockMap allocated_blocks_;
  BlockMap claimed_blocks_;

  DISALLOW_COPY_ASSIGN_AND_MOVE(AddressSpaceChildDriver);
};

}  // namespace goldfish

#endif  // SRC_GRAPHICS_DRIVERS_MISC_GOLDFISH_ADDRESS_SPACE_ADDRESS_SPACE_DEVICE_H_
