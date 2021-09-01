// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DRIVERS_MISC_GOLDFISH_ADDRESS_SPACE_ADDRESS_SPACE_DEVICE_H_
#define SRC_GRAPHICS_DRIVERS_MISC_GOLDFISH_ADDRESS_SPACE_ADDRESS_SPACE_DEVICE_H_

#include <fidl/fuchsia.hardware.goldfish/cpp/wire.h>
#include <fuchsia/hardware/goldfish/addressspace/cpp/banjo.h>
#include <fuchsia/hardware/pci/cpp/banjo.h>
#include <lib/ddk/device.h>
#include <lib/ddk/io-buffer.h>
#include <lib/mmio/mmio.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <lib/zx/bti.h>
#include <lib/zx/pmt.h>
#include <zircon/types.h>

#include <map>

#include <ddktl/device.h>
#include <fbl/mutex.h>

namespace goldfish {

class AddressSpaceDevice;
using DeviceType =
    ddk::Device<AddressSpaceDevice,
                ddk::Messageable<fuchsia_hardware_goldfish::AddressSpaceDevice>::Mixin>;

class AddressSpaceChildDriver;
using ChildDriverType =
    ddk::Device<AddressSpaceChildDriver,
                ddk::Messageable<fuchsia_hardware_goldfish::AddressSpaceChildDriver>::Mixin>;

class AddressSpaceDevice
    : public DeviceType,
      public ddk::GoldfishAddressSpaceProtocol<AddressSpaceDevice, ddk::base_protocol> {
  using fidl::WireServer<fuchsia_hardware_goldfish::AddressSpaceDevice>::OpenChildDriver;

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

  // |fuchsia.hardware.GoldfishAddressSpace|
  zx_status_t GoldfishAddressSpaceOpenChildDriver(address_space_child_driver_type_t type,
                                                  zx::channel request) {
    return OpenChildDriver(
        static_cast<fuchsia_hardware_goldfish::wire::AddressSpaceChildDriverType>(type),
        std::move(request));
  }

  // |fidl::WireServer<fuchsia_hardware_goldfish::AddressSpaceDevice>|
  void OpenChildDriver(OpenChildDriverRequestView request,
                       OpenChildDriverCompleter::Sync& completer) override {
    zx_status_t result = OpenChildDriver(request->type, request->req.TakeChannel());
    completer.Close(result);
  }

  // Device protocol implementation.
  void DdkRelease();

 private:
  zx_status_t OpenChildDriver(fuchsia_hardware_goldfish::wire::AddressSpaceChildDriverType type,
                              zx::channel request);

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
  explicit AddressSpaceChildDriver(
      fuchsia_hardware_goldfish::wire::AddressSpaceChildDriverType type, AddressSpaceDevice* device,
      uint64_t dma_region_paddr, ddk::IoBuffer&& io_buffer, uint32_t child_device_handle);

  // AddressSpaceChildDriver is destroyed when the fidl channel it binds to
  // is disconnected by the client (the goldfish Vulkan ICD) when the client
  // gets destroyed.
  // This destructor unpins all the pinned memory when it destroys the block
  // maps. Client (ICD) guarantees that all the blocks allocated / claimed
  // by this device (including host-visible memory, user-space ring buffers)
  // will not be accessed anymore after the ICD is destroyed, so it's safe
  // to unpin the memory.
  ~AddressSpaceChildDriver();

  zx_status_t Bind();

  // |fidl::WireServer<fuchsia_hardware_goldfish::AddressSpaceChildDriver>|
  void AllocateBlock(AllocateBlockRequestView request,
                     AllocateBlockCompleter::Sync& completer) override;
  void DeallocateBlock(DeallocateBlockRequestView request,
                       DeallocateBlockCompleter::Sync& completer) override;
  void ClaimSharedBlock(ClaimSharedBlockRequestView request,
                        ClaimSharedBlockCompleter::Sync& completer) override;
  void UnclaimSharedBlock(UnclaimSharedBlockRequestView request,
                          UnclaimSharedBlockCompleter::Sync& completer) override;
  void Ping(PingRequestView request, PingCompleter::Sync& completer) override;

  void DdkRelease();

  struct Block {
   public:
    Block(uint64_t offset, uint64_t size, zx::pmt pmt);
    ~Block();
    uint64_t offset;
    uint64_t size;
    zx::pmt pmt;
  };

 private:
  AddressSpaceDevice* const device_;
  const uint64_t dma_region_paddr_;

  ddk::IoBuffer io_buffer_;
  const uint32_t handle_;

  // TODO(fxbug.dev/3213): This should be std::unordered_map.
  using BlockMap = std::map<uint64_t, Block>;
  BlockMap allocated_blocks_;
  BlockMap claimed_blocks_;

  DISALLOW_COPY_ASSIGN_AND_MOVE(AddressSpaceChildDriver);
};

}  // namespace goldfish

#endif  // SRC_GRAPHICS_DRIVERS_MISC_GOLDFISH_ADDRESS_SPACE_ADDRESS_SPACE_DEVICE_H_
