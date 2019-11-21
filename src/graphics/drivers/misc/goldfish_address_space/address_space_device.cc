// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "address_space_device.h"

#include <fuchsia/hardware/goldfish/c/fidl.h>
#include <lib/device-protocol/pci.h>
#include <lib/fidl-utils/bind.h>
#include <limits.h>

#include <map>
#include <memory>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/trace/event.h>
#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>

#define GOLDFISH_ADDRESS_SPACE_PCI_VID 0x607D
#define GOLDFISH_ADDRESS_SPACE_PCI_DID 0xF153

namespace goldfish {
namespace {

const char* kTag = "goldfish-address-space";

enum Registers {
  REGISTER_COMMAND = 0,
  REGISTER_STATUS = 4,
  REGISTER_GUEST_PAGE_SIZE = 8,
  REGISTER_BLOCK_SIZE_LOW = 12,
  REGISTER_BLOCK_SIZE_HIGH = 16,
  REGISTER_BLOCK_OFFSET_LOW = 20,
  REGISTER_BLOCK_OFFSET_HIGH = 24,
  REGISTER_PING = 28,
  REGISTER_PING_INFO_ADDR_LOW = 32,
  REGISTER_PING_INFO_ADDR_HIGH = 36,
  REGISTER_HANDLE = 40,
  REGISTER_PHYS_START_LOW = 44,
  REGISTER_PHYS_START_HIGH = 48,
};

enum Commands {
  COMMAND_ALLOCATE_BLOCK = 1,
  COMMAND_DEALLOCATE_BLOCK = 2,
  COMMAND_GEN_HANDLE = 3,
  COMMAND_DESTROY_HANDLE = 4,
  COMMAND_TELL_PING_INFO_ADDR = 5,
};

enum PciBarIds {
  PCI_CONTROL_BAR_ID = 0,
  PCI_AREA_BAR_ID = 1,
};

class Instance;
using InstanceType = ddk::Device<Instance, ddk::Messageable>;

// Deprecated
// This class implements an address space instance device.
class Instance : public InstanceType {
 public:
  Instance(AddressSpaceDevice* device, uint64_t dma_region_paddr)
      : Device(device->zxdev()), device_(device), dma_region_paddr_(dma_region_paddr) {}

  ~Instance() {
    for (auto& block : allocated_blocks_) {
      device_->DeallocateBlock(block.second.offset);
    }
  }

  zx_status_t Bind() {
    TRACE_DURATION("gfx", "Instance::Bind");
    return DdkAdd("address-space", DEVICE_ADD_INSTANCE);
  }

  zx_status_t FidlOpenChildDriver(fuchsia_hardware_goldfish_AddressSpaceChildDriverType type,
                                  zx_handle_t request_handle) {
    zx::channel request(request_handle);

    ddk::IoBuffer io_buffer;
    uint32_t handle;
    zx_status_t status = device_->CreateChildDriver(&io_buffer, &handle);
    if (status != ZX_OK) {
      return status;
    }

    fuchsia_hardware_goldfish_AddressSpaceChildDriverPingMessage* ping =
        reinterpret_cast<struct fuchsia_hardware_goldfish_AddressSpaceChildDriverPingMessage*>(
            io_buffer.virt());
    memset(ping, 0, sizeof(*ping));
    ping->offset = dma_region_paddr_;
    ping->metadata = static_cast<uint64_t>(type);
    device_->ChildDriverPing(handle);

    auto child_driver = std::make_unique<AddressSpaceChildDriver>(type, device_, dma_region_paddr_,
                                                                  std::move(io_buffer), handle);

    status = child_driver->DdkAdd("address-space-child",  // name
                                  DEVICE_ADD_INSTANCE,    // flags
                                  nullptr,                // props
                                  0,                      // prop_count
                                  0,                      // proto_id
                                  nullptr,                // proxy_args
                                  request.release()       // client_remote
    );

    if (status != ZX_OK) {
      zxlogf(ERROR, "%s: failed to DdkAdd child driver: %d\n", kTag, status);
      return status;
    }

    child_driver.release();

    return ZX_OK;
  }

  // Deprecated (moved to AddressSpaceChildDriver)
  zx_status_t FidlAllocateBlock(uint64_t size, fidl_txn_t* txn) {
    TRACE_DURATION("gfx", "Instance::FidlAllocateBlock", "size", size);

    uint64_t offset;
    uint32_t result = device_->AllocateBlock(&size, &offset);
    if (result) {
      zxlogf(ERROR, "%s: failed to allocate block: %lu %d\n", kTag, size, result);
      return fuchsia_hardware_goldfish_AddressSpaceDeviceAllocateBlock_reply(txn, ZX_ERR_INTERNAL,
                                                                             0, ZX_HANDLE_INVALID);
    }

    auto deallocate_block =
        fbl::MakeAutoCall([this, offset]() { device_->DeallocateBlock(offset); });

    zx_paddr_t paddr;
    zx::pmt pmt;
    zx::vmo vmo;
    zx_status_t status = device_->PinBlock(offset, size, &paddr, &pmt, &vmo);
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s: failed to pin block: %d\n", kTag, status);
      return status;
    }

    deallocate_block.cancel();
    allocated_blocks_[paddr] = {offset, std::move(pmt)};
    return fuchsia_hardware_goldfish_AddressSpaceDeviceAllocateBlock_reply(txn, ZX_OK, paddr,
                                                                           vmo.release());
  }

  // Deprecated (moved to AddressSpaceChildDriver)
  zx_status_t FidlDeallocateBlock(uint64_t paddr, fidl_txn_t* txn) {
    TRACE_DURATION("gfx", "Instance::FidlDeallocateBlock", "paddr", paddr);

    auto it = allocated_blocks_.find(paddr);
    if (it == allocated_blocks_.end()) {
      zxlogf(ERROR, "%s: invalid block: %lu\n", kTag, paddr);
      return ZX_ERR_INVALID_ARGS;
    }

    uint32_t result = device_->DeallocateBlock(it->second.offset);
    if (result) {
      zxlogf(ERROR, "%s: failed to deallocate block: %lu %d\n", kTag, paddr, result);
      return fuchsia_hardware_goldfish_AddressSpaceDeviceDeallocateBlock_reply(txn,
                                                                               ZX_ERR_INTERNAL);
    }

    allocated_blocks_.erase(it);
    return fuchsia_hardware_goldfish_AddressSpaceDeviceDeallocateBlock_reply(txn, ZX_OK);
  }

  // Device protocol implementation.
  zx_status_t DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn) {
    using Binder = fidl::Binder<Instance>;

    static const fuchsia_hardware_goldfish_AddressSpaceDevice_ops_t kOps = {
        .AllocateBlock = Binder::BindMember<&Instance::FidlAllocateBlock>,
        .DeallocateBlock = Binder::BindMember<&Instance::FidlDeallocateBlock>,
        .OpenChildDriver = Binder::BindMember<&Instance::FidlOpenChildDriver>,
    };

    return fuchsia_hardware_goldfish_AddressSpaceDevice_dispatch(this, txn, msg, &kOps);
  }
  zx_status_t DdkClose(uint32_t flags) { return ZX_OK; }
  void DdkRelease() { delete this; }

 private:
  struct Block {
    uint64_t offset;
    zx::pmt pmt;
  };
  AddressSpaceDevice* const device_;
  const uint64_t dma_region_paddr_ = 0;

  // TODO(TC-383): This should be std::unordered_map.
  using BlockMap = std::map<uint64_t, Block>;
  BlockMap allocated_blocks_;

  DISALLOW_COPY_ASSIGN_AND_MOVE(Instance);
};

uint32_t upper_32_bits(uint64_t n) { return static_cast<uint32_t>(n >> 32); }

uint32_t lower_32_bits(uint64_t n) { return static_cast<uint32_t>(n); }

}  // namespace

// static
zx_status_t AddressSpaceDevice::Create(void* ctx, zx_device_t* device) {
  auto address_space_device = std::make_unique<goldfish::AddressSpaceDevice>(device);

  zx_status_t status = address_space_device->Bind();
  if (status == ZX_OK) {
    // devmgr now owns device.
    __UNUSED auto* dev = address_space_device.release();
  }
  return status;
}

AddressSpaceDevice::AddressSpaceDevice(zx_device_t* parent) : DeviceType(parent), pci_(parent) {}

AddressSpaceDevice::~AddressSpaceDevice() = default;

zx_status_t AddressSpaceDevice::Bind() {
  if (!pci_.is_valid()) {
    zxlogf(ERROR, "%s: no pci protocol\n", kTag);
    return ZX_ERR_NOT_SUPPORTED;
  }

  zx_status_t status = pci_.GetBti(0, &bti_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: failed to get BTI: %d\n", kTag, status);
    return status;
  }

  zx_pci_bar_t control_bar;
  status = pci_.GetBar(PCI_CONTROL_BAR_ID, &control_bar);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: get_bar: could not get control BAR\n", kTag);
    return status;
  }
  ZX_DEBUG_ASSERT(control_bar.type == ZX_PCI_BAR_TYPE_MMIO);
  ZX_DEBUG_ASSERT(control_bar.handle != ZX_HANDLE_INVALID);

  fbl::AutoLock lock(&mmio_lock_);
  status = ddk::MmioBuffer::Create(0, control_bar.size, zx::vmo(control_bar.handle),
                                   ZX_CACHE_POLICY_UNCACHED_DEVICE, &mmio_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: failed to create MMIO buffer: %d\n", kTag, status);
    return status;
  }

  zx_pci_bar_t area_bar;
  status = pci_.GetBar(PCI_AREA_BAR_ID, &area_bar);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: get_bar: could not get area BAR: %d\n", kTag, status);
    return status;
  }
  ZX_DEBUG_ASSERT(area_bar.type == ZX_PCI_BAR_TYPE_MMIO);
  ZX_DEBUG_ASSERT(area_bar.handle != ZX_HANDLE_INVALID);
  dma_region_ = zx::vmo(area_bar.handle);

  mmio_->Write32(PAGE_SIZE, REGISTER_GUEST_PAGE_SIZE);

  zx::pmt pmt;
  // Pin offset 0 just to get the starting physical address
  bti_.pin(ZX_BTI_PERM_READ | ZX_BTI_PERM_WRITE | ZX_BTI_CONTIGUOUS, dma_region_, 0, PAGE_SIZE,
           &dma_region_paddr_, 1, &pmt);

  mmio_->Write32(static_cast<uint32_t>(dma_region_paddr_), REGISTER_PHYS_START_LOW);
  mmio_->Write32(static_cast<uint32_t>(dma_region_paddr_ >> 32), REGISTER_PHYS_START_HIGH);

  return DdkAdd("goldfish-address-space", 0, nullptr, 0, ZX_PROTOCOL_GOLDFISH_ADDRESS_SPACE);
}

uint32_t AddressSpaceDevice::AllocateBlock(uint64_t* size, uint64_t* offset) {
  fbl::AutoLock lock(&mmio_lock_);

  mmio_->Write32(lower_32_bits(*size), REGISTER_BLOCK_SIZE_LOW);
  mmio_->Write32(upper_32_bits(*size), REGISTER_BLOCK_SIZE_HIGH);

  uint32_t result = CommandMmioLocked(COMMAND_ALLOCATE_BLOCK);
  if (!result) {
    uint64_t low = mmio_->Read32(REGISTER_BLOCK_OFFSET_LOW);
    uint64_t high = mmio_->Read32(REGISTER_BLOCK_OFFSET_HIGH);
    *offset = low | (high << 32);

    low = mmio_->Read32(REGISTER_BLOCK_SIZE_LOW);
    high = mmio_->Read32(REGISTER_BLOCK_SIZE_HIGH);
    *size = low | (high << 32);
  }
  return result;
}

uint32_t AddressSpaceDevice::DeallocateBlock(uint64_t offset) {
  fbl::AutoLock lock(&mmio_lock_);

  mmio_->Write32(lower_32_bits(offset), REGISTER_BLOCK_OFFSET_LOW);
  mmio_->Write32(upper_32_bits(offset), REGISTER_BLOCK_OFFSET_HIGH);

  return CommandMmioLocked(COMMAND_DEALLOCATE_BLOCK);
}

uint32_t AddressSpaceDevice::DestroyChildDriver(uint32_t handle) {
  fbl::AutoLock lock(&mmio_lock_);
  mmio_->Write32(handle, REGISTER_HANDLE);
  CommandMmioLocked(COMMAND_DESTROY_HANDLE);
  return ZX_OK;
}

zx_status_t AddressSpaceDevice::PinBlock(uint64_t offset, uint64_t size, zx_paddr_t* paddr,
                                         zx::pmt* pmt, zx::vmo* vmo) {
  auto status = bti_.pin(ZX_BTI_PERM_READ | ZX_BTI_PERM_WRITE | ZX_BTI_CONTIGUOUS, dma_region_,
                         offset, size, paddr, 1, pmt);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: zx_bti_pin failed:  %d\n", kTag, status);
    return status;
  }

  status = dma_region_.create_child(ZX_VMO_CHILD_SLICE, offset, size, vmo);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: x_vmo_create_child failed: %d\n", kTag, status);
    return status;
  }

  return ZX_OK;
}

zx_status_t AddressSpaceDevice::CreateChildDriver(ddk::IoBuffer* io_buffer, uint32_t* handle) {
  fbl::AutoLock lock(&mmio_lock_);
  CommandMmioLocked(COMMAND_GEN_HANDLE);
  *handle = mmio_->Read32(REGISTER_HANDLE);

  zx_status_t status = io_buffer->Init(bti_.get(), PAGE_SIZE, IO_BUFFER_RW | IO_BUFFER_CONTIG);

  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: failed to io_buffer.Init. status: %d\n", kTag, status);
    return status;
  }

  mmio_->Write32(*handle, REGISTER_HANDLE);
  mmio_->Write32(lower_32_bits(io_buffer->phys()), REGISTER_PING_INFO_ADDR_LOW);
  mmio_->Write32(upper_32_bits(io_buffer->phys()), REGISTER_PING_INFO_ADDR_HIGH);
  CommandMmioLocked(COMMAND_TELL_PING_INFO_ADDR);

  return ZX_OK;
}

uint32_t AddressSpaceDevice::ChildDriverPing(uint32_t handle) {
  fbl::AutoLock lock(&mmio_lock_);
  mmio_->Write32(handle, REGISTER_PING);
  return ZX_OK;
}

zx_status_t AddressSpaceDevice::DdkOpen(zx_device_t** dev_out, uint32_t flags) {
  auto instance = std::make_unique<Instance>(this, dma_region_paddr_);

  zx_status_t status = instance->Bind();
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: failed to init instance: %d\n", kTag, status);
    return status;
  }

  Instance* instance_ptr = instance.release();
  *dev_out = instance_ptr->zxdev();
  return ZX_OK;
}

void AddressSpaceDevice::DdkUnbindNew(ddk::UnbindTxn txn) { txn.Reply(); }

void AddressSpaceDevice::DdkRelease() { delete this; }

uint32_t AddressSpaceDevice::CommandMmioLocked(uint32_t cmd) {
  mmio_->Write32(cmd, REGISTER_COMMAND);
  return mmio_->Read32(REGISTER_STATUS);
}

AddressSpaceChildDriver::AddressSpaceChildDriver(
    fuchsia_hardware_goldfish_AddressSpaceChildDriverType type, AddressSpaceDevice* device,
    uint64_t dma_region_paddr, ddk::IoBuffer&& io_buffer, uint32_t child_device_handle)
    : Device(device->zxdev()),
      device_(device),
      dma_region_paddr_(dma_region_paddr),
      io_buffer_(std::move(io_buffer)),
      handle_(child_device_handle) {}

AddressSpaceChildDriver::~AddressSpaceChildDriver() {
  for (auto& block : allocated_blocks_) {
    device_->DeallocateBlock(block.second.offset);
  }

  device_->DestroyChildDriver(handle_);
}

zx_status_t AddressSpaceChildDriver::Bind() {
  TRACE_DURATION("gfx", "Instance::Bind");
  return DdkAdd("address-space", DEVICE_ADD_INSTANCE);
}

zx_status_t AddressSpaceChildDriver::FidlAllocateBlock(uint64_t size, fidl_txn_t* txn) {
  TRACE_DURATION("gfx", "Instance::FidlAllocateBlock", "size", size);

  uint64_t offset;
  uint32_t result = device_->AllocateBlock(&size, &offset);
  if (result) {
    zxlogf(ERROR, "%s: failed to allocate block: %lu %d\n", kTag, size, result);
    return fuchsia_hardware_goldfish_AddressSpaceChildDriverAllocateBlock_reply(
        txn, ZX_ERR_INTERNAL, 0, ZX_HANDLE_INVALID);
  }

  auto deallocate_block = fbl::MakeAutoCall([this, offset]() { device_->DeallocateBlock(offset); });

  zx_paddr_t paddr;
  zx::pmt pmt;
  zx::vmo vmo;
  zx_status_t status = device_->PinBlock(offset, size, &paddr, &pmt, &vmo);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: failed to pin block: %d\n", kTag, status);
    return status;
  }

  deallocate_block.cancel();
  allocated_blocks_[paddr] = {offset, size, std::move(pmt)};
  return fuchsia_hardware_goldfish_AddressSpaceChildDriverAllocateBlock_reply(txn, ZX_OK, paddr,
                                                                              vmo.release());
}

zx_status_t AddressSpaceChildDriver::FidlDeallocateBlock(uint64_t paddr, fidl_txn_t* txn) {
  TRACE_DURATION("gfx", "Instance::FidlDeallocateBlock", "paddr", paddr);

  auto it = allocated_blocks_.find(paddr);
  if (it == allocated_blocks_.end()) {
    zxlogf(ERROR, "%s: invalid block: %lu\n", kTag, paddr);
    return ZX_ERR_INVALID_ARGS;
  }

  uint32_t result = device_->DeallocateBlock(it->second.offset);
  if (result) {
    zxlogf(ERROR, "%s: failed to deallocate block: %lu %d\n", kTag, paddr, result);
    return fuchsia_hardware_goldfish_AddressSpaceChildDriverDeallocateBlock_reply(txn,
                                                                                  ZX_ERR_INTERNAL);
  }

  allocated_blocks_.erase(it);
  return fuchsia_hardware_goldfish_AddressSpaceChildDriverDeallocateBlock_reply(txn, ZX_OK);
}

zx_status_t AddressSpaceChildDriver::FidlClaimSharedBlock(uint64_t offset, uint64_t size,
                                                          fidl_txn_t* txn) {
  auto end = offset + size;
  for (const auto& entry : claimed_blocks_) {
    auto entry_start = entry.second.offset;
    auto entry_end = entry.second.offset + entry.second.size;
    if ((offset >= entry_start && offset < entry_end) || (end > entry_start && end <= entry_end)) {
      zxlogf(ERROR,
             "%s: tried to claim region [0x%llx 0x%llx) which overlaps existing region [0x%llx "
             "0x%llx). %d\n",
             kTag, (unsigned long long)offset, (unsigned long long)size,
             (unsigned long long)entry_start, (unsigned long long)entry_end, ZX_ERR_INVALID_ARGS);
      return fuchsia_hardware_goldfish_AddressSpaceChildDriverClaimSharedBlock_reply(
          txn, ZX_ERR_INVALID_ARGS, ZX_HANDLE_INVALID);
    }
  }

  zx_paddr_t paddr;
  zx::pmt pmt;
  zx::vmo vmo;
  zx_status_t status = device_->PinBlock(offset, size, &paddr, &pmt, &vmo);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: failed to pin block: %d\n", kTag, status);
    return status;
  }

  claimed_blocks_[offset] = {offset, size, std::move(pmt)};
  return fuchsia_hardware_goldfish_AddressSpaceChildDriverClaimSharedBlock_reply(txn, ZX_OK,
                                                                                 vmo.release());
};

zx_status_t AddressSpaceChildDriver::FidlUnclaimSharedBlock(uint64_t offset, fidl_txn_t* txn) {
  if (claimed_blocks_.end() == claimed_blocks_.find(offset)) {
    zxlogf(ERROR,
           "%s: tried to erase region at 0x%llx but there is no such region with that offset: %d\n",
           kTag, (unsigned long long)offset, ZX_ERR_INVALID_ARGS);
    return fuchsia_hardware_goldfish_AddressSpaceChildDriverUnclaimSharedBlock_reply(
        txn, ZX_ERR_INVALID_ARGS);
  }
  claimed_blocks_.erase(offset);
  return fuchsia_hardware_goldfish_AddressSpaceChildDriverUnclaimSharedBlock_reply(txn, ZX_OK);
};

zx_status_t AddressSpaceChildDriver::FidlPing(
    const fuchsia_hardware_goldfish_AddressSpaceChildDriverPingMessage* ping, fidl_txn_t* txn) {
  fuchsia_hardware_goldfish_AddressSpaceChildDriverPingMessage* output =
      reinterpret_cast<struct fuchsia_hardware_goldfish_AddressSpaceChildDriverPingMessage*>(
          io_buffer_.virt());
  *output = *ping;
  output->offset += dma_region_paddr_;
  device_->ChildDriverPing(handle_);

  return fuchsia_hardware_goldfish_AddressSpaceChildDriverPing_reply(txn, ZX_OK, output);
}

// Device protocol implementation.
zx_status_t AddressSpaceChildDriver::DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn) {
  using Binder = fidl::Binder<AddressSpaceChildDriver>;

  static const fuchsia_hardware_goldfish_AddressSpaceChildDriver_ops_t kOps = {
      .AllocateBlock = Binder::BindMember<&AddressSpaceChildDriver::FidlAllocateBlock>,
      .DeallocateBlock = Binder::BindMember<&AddressSpaceChildDriver::FidlDeallocateBlock>,
      .ClaimSharedBlock = Binder::BindMember<&AddressSpaceChildDriver::FidlClaimSharedBlock>,
      .UnclaimSharedBlock = Binder::BindMember<&AddressSpaceChildDriver::FidlUnclaimSharedBlock>,
      .Ping = Binder::BindMember<&AddressSpaceChildDriver::FidlPing>,
  };

  return fuchsia_hardware_goldfish_AddressSpaceChildDriver_dispatch(this, txn, msg, &kOps);
}

zx_status_t AddressSpaceChildDriver::DdkClose(uint32_t flags) { return ZX_OK; }

void AddressSpaceChildDriver::DdkRelease() { delete this; }

}  // namespace goldfish

static constexpr zx_driver_ops_t goldfish_address_space_driver_ops = []() -> zx_driver_ops_t {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = goldfish::AddressSpaceDevice::Create;
  return ops;
}();

ZIRCON_DRIVER_BEGIN(goldfish_address_space, goldfish_address_space_driver_ops, "zircon", "0.1", 3)
BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PCI),
    BI_ABORT_IF(NE, BIND_PCI_VID, GOLDFISH_ADDRESS_SPACE_PCI_VID),
    BI_MATCH_IF(EQ, BIND_PCI_DID, GOLDFISH_ADDRESS_SPACE_PCI_DID),
    ZIRCON_DRIVER_END(goldfish_address_space)
