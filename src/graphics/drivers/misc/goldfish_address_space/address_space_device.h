// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DRIVERS_MISC_GOLDFISH_ADDRESS_SPACE_ADDRESS_SPACE_DEVICE_H_
#define SRC_GRAPHICS_DRIVERS_MISC_GOLDFISH_ADDRESS_SPACE_ADDRESS_SPACE_DEVICE_H_

#include <fidl/fuchsia.hardware.goldfish/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/loop.h>
#include <lib/ddk/device.h>
#include <lib/ddk/io-buffer.h>
#include <lib/device-protocol/pci.h>
#include <lib/mmio/mmio.h>
#include <lib/svc/outgoing.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <lib/zx/bti.h>
#include <lib/zx/pmt.h>
#include <zircon/types.h>

#include <map>

#include <ddktl/device.h>
#include <fbl/mutex.h>

namespace goldfish {

class AddressSpaceDevice;
using DeviceType = ddk::Device<AddressSpaceDevice>;

// This device is a child of a AddressSpaceDevice device and acts as a
// passthrough for FIDL requests for AddressSpaceDevice. The reason this device
// exists instead of making AddressSpaceDevice directly handle FIDL requests
// is because DFv2 does not allow for a device to be both bindable and have
// children.
class AddressSpacePassthroughDevice;
using PassthroughDeviceType =
    ddk::Device<AddressSpacePassthroughDevice,
                ddk::Messageable<fuchsia_hardware_goldfish::AddressSpaceDevice>::Mixin>;

class AddressSpaceChildDriver;
using ChildDriverType =
    ddk::Device<AddressSpaceChildDriver,
                ddk::Messageable<fuchsia_hardware_goldfish::AddressSpaceChildDriver>::Mixin>;

class AddressSpaceDevice : public DeviceType {
 public:
  static zx_status_t Create(void* ctx, zx_device_t* parent);

  explicit AddressSpaceDevice(zx_device_t* parent, async_dispatcher_t* dispatcher);
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
  void DdkRelease();

  zx_status_t OpenChildDriver(fuchsia_hardware_goldfish::wire::AddressSpaceChildDriverType type,
                              zx::channel request);

 private:
  uint32_t CommandMmioLocked(uint32_t cmd) TA_REQ(mmio_lock_);

  ddk::Pci pci_;
  zx::bti bti_;
  zx::vmo dma_region_;
  uint64_t dma_region_paddr_;

  fbl::Mutex mmio_lock_;
  std::optional<fdf::MmioBuffer> mmio_ TA_GUARDED(mmio_lock_);

  std::optional<svc::Outgoing> outgoing_;
  async_dispatcher_t* dispatcher_;
  async::Loop loop_{&kAsyncLoopConfigNeverAttachToThread};

  DISALLOW_COPY_ASSIGN_AND_MOVE(AddressSpaceDevice);
};

class AddressSpacePassthroughDevice : public PassthroughDeviceType {
  using fidl::WireServer<fuchsia_hardware_goldfish::AddressSpaceDevice>::OpenChildDriver;

 public:
  explicit AddressSpacePassthroughDevice(AddressSpaceDevice* device);

  // Device protocol implementation.
  void DdkRelease();

  // |fidl::WireServer<fuchsia_hardware_goldfish::AddressSpaceDevice>|
  void OpenChildDriver(OpenChildDriverRequestView request,
                       OpenChildDriverCompleter::Sync& completer) override;

 private:
  AddressSpaceDevice* const device_;
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
