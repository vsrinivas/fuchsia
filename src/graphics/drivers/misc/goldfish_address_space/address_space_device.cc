// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "address_space_device.h"

#include <fuchsia/hardware/goldfish/llcpp/fidl.h>
#include <lib/device-protocol/pci.h>
#include <lib/fidl-utils/bind.h>
#include <limits.h>

#include <map>
#include <memory>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/trace/event.h>
#include <ddktl/fidl.h>
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
    zxlogf(ERROR, "%s: no pci protocol", kTag);
    return ZX_ERR_NOT_SUPPORTED;
  }

  zx_status_t status = pci_.GetBti(0, &bti_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: failed to get BTI: %d", kTag, status);
    return status;
  }

  zx_pci_bar_t control_bar;
  status = pci_.GetBar(PCI_CONTROL_BAR_ID, &control_bar);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: get_bar: could not get control BAR", kTag);
    return status;
  }
  ZX_DEBUG_ASSERT(control_bar.type == ZX_PCI_BAR_TYPE_MMIO);
  ZX_DEBUG_ASSERT(control_bar.handle != ZX_HANDLE_INVALID);

  fbl::AutoLock lock(&mmio_lock_);
  status = ddk::MmioBuffer::Create(0, control_bar.size, zx::vmo(control_bar.handle),
                                   ZX_CACHE_POLICY_UNCACHED_DEVICE, &mmio_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: failed to create MMIO buffer: %d", kTag, status);
    return status;
  }

  zx_pci_bar_t area_bar;
  status = pci_.GetBar(PCI_AREA_BAR_ID, &area_bar);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: get_bar: could not get area BAR: %d", kTag, status);
    return status;
  }
  ZX_DEBUG_ASSERT(area_bar.type == ZX_PCI_BAR_TYPE_MMIO);
  ZX_DEBUG_ASSERT(area_bar.handle != ZX_HANDLE_INVALID);
  dma_region_ = zx::vmo(area_bar.handle);

  mmio_->Write32(PAGE_SIZE, REGISTER_GUEST_PAGE_SIZE);

  zx::pmt pmt;
  // Pin offset 0 just to get the starting physical address
  status = bti_.pin(ZX_BTI_PERM_READ | ZX_BTI_PERM_WRITE | ZX_BTI_CONTIGUOUS, dma_region_, 0,
                    PAGE_SIZE, &dma_region_paddr_, 1, &pmt);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: bti_pin: could not pin pages: %d", kTag, status);
    return status;
  }

  // The pinned memory will not be accessed but only used to get the starting
  // physical address, so we unpin the PMT.
  status = pmt.unpin();
  ZX_DEBUG_ASSERT(status == ZX_OK);

  mmio_->Write32(static_cast<uint32_t>(dma_region_paddr_), REGISTER_PHYS_START_LOW);
  mmio_->Write32(static_cast<uint32_t>(dma_region_paddr_ >> 32), REGISTER_PHYS_START_HIGH);

  return DdkAdd(ddk::DeviceAddArgs("goldfish-address-space")
                    .set_proto_id(ZX_PROTOCOL_GOLDFISH_ADDRESS_SPACE));
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
    zxlogf(ERROR, "%s: zx_bti_pin failed:  %d", kTag, status);
    return status;
  }

  status = dma_region_.create_child(ZX_VMO_CHILD_SLICE, offset, size, vmo);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: x_vmo_create_child failed: %d", kTag, status);
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
    zxlogf(ERROR, "%s: failed to io_buffer.Init. status: %d", kTag, status);
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

zx_status_t AddressSpaceDevice::OpenChildDriver(
    llcpp::fuchsia::hardware::goldfish::AddressSpaceChildDriverType type, zx::channel request) {
  using llcpp::fuchsia::hardware::goldfish::AddressSpaceChildDriverPingMessage;

  ddk::IoBuffer io_buffer;
  uint32_t handle;
  zx_status_t status = CreateChildDriver(&io_buffer, &handle);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: failed to create child driver: %d", kTag, status);
    return status;
  }

  AddressSpaceChildDriverPingMessage* ping =
      reinterpret_cast<struct AddressSpaceChildDriverPingMessage*>(io_buffer.virt());
  memset(ping, 0, sizeof(*ping));
  ping->offset = dma_region_paddr_;
  ping->metadata = static_cast<uint64_t>(type);
  ChildDriverPing(handle);

  auto child_driver = std::make_unique<AddressSpaceChildDriver>(type, this, dma_region_paddr_,
                                                                std::move(io_buffer), handle);

  status = child_driver->DdkAdd(ddk::DeviceAddArgs("address-space-child")
                                    .set_flags(DEVICE_ADD_INSTANCE)
                                    .set_client_remote(std::move(request)));

  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: failed to DdkAdd child driver: %d", kTag, status);
    return status;
  }

  child_driver.release();
  return ZX_OK;
}

void AddressSpaceDevice::DdkRelease() { delete this; }

zx_status_t AddressSpaceDevice::DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn) {
  DdkTransaction transaction(txn);
  llcpp::fuchsia::hardware::goldfish::AddressSpaceDevice::Dispatch(this, msg, &transaction);
  return transaction.Status();
}

uint32_t AddressSpaceDevice::CommandMmioLocked(uint32_t cmd) {
  mmio_->Write32(cmd, REGISTER_COMMAND);
  return mmio_->Read32(REGISTER_STATUS);
}

AddressSpaceChildDriver::AddressSpaceChildDriver(
    llcpp::fuchsia::hardware::goldfish::AddressSpaceChildDriverType type,
    AddressSpaceDevice* device, uint64_t dma_region_paddr, ddk::IoBuffer&& io_buffer,
    uint32_t child_device_handle)
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

void AddressSpaceChildDriver::AllocateBlock(uint64_t size, AllocateBlockCompleter::Sync completer) {
  TRACE_DURATION("gfx", "Instance::FidlAllocateBlock", "size", size);

  uint64_t offset;
  uint32_t result = device_->AllocateBlock(&size, &offset);
  if (result) {
    zxlogf(ERROR, "%s: failed to allocate block: %lu %d", kTag, size, result);
    completer.Reply(ZX_ERR_INTERNAL, 0, zx::vmo());
    return;
  }

  auto deallocate_block = fbl::MakeAutoCall([this, offset]() { device_->DeallocateBlock(offset); });

  zx_paddr_t paddr;
  zx::pmt pmt;
  zx::vmo vmo;
  zx_status_t status = device_->PinBlock(offset, size, &paddr, &pmt, &vmo);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: failed to pin block: %d", kTag, status);
    completer.Close(status);
    return;
  }

  deallocate_block.cancel();
  allocated_blocks_.try_emplace(paddr, offset, size, std::move(pmt));
  completer.Reply(ZX_OK, paddr, std::move(vmo));
}

void AddressSpaceChildDriver::DeallocateBlock(uint64_t paddr,
                                              DeallocateBlockCompleter::Sync completer) {
  TRACE_DURATION("gfx", "Instance::FidlDeallocateBlock", "paddr", paddr);

  auto it = allocated_blocks_.find(paddr);
  if (it == allocated_blocks_.end()) {
    zxlogf(ERROR, "%s: invalid block: %lu", kTag, paddr);
    completer.Close(ZX_ERR_INVALID_ARGS);
    return;
  }

  uint32_t result = device_->DeallocateBlock(it->second.offset);
  if (result) {
    zxlogf(ERROR, "%s: failed to deallocate block: %lu %d", kTag, paddr, result);
    completer.Reply(ZX_ERR_INTERNAL);
    return;
  }

  allocated_blocks_.erase(it);
  completer.Reply(ZX_OK);
}

void AddressSpaceChildDriver::ClaimSharedBlock(uint64_t offset, uint64_t size,
                                               ClaimSharedBlockCompleter::Sync completer) {
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
      completer.Reply(ZX_ERR_INVALID_ARGS, zx::vmo());
      return;
    }
  }

  zx_paddr_t paddr;
  zx::pmt pmt;
  zx::vmo vmo;
  zx_status_t status = device_->PinBlock(offset, size, &paddr, &pmt, &vmo);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: failed to pin block: %d", kTag, status);
    completer.Close(status);
    return;
  }

  claimed_blocks_.try_emplace(offset, offset, size, std::move(pmt));
  completer.Reply(ZX_OK, std::move(vmo));
};

void AddressSpaceChildDriver::UnclaimSharedBlock(uint64_t offset,
                                                 UnclaimSharedBlockCompleter::Sync completer) {
  auto it = claimed_blocks_.find(offset);
  if (it == claimed_blocks_.end()) {
    zxlogf(ERROR,
           "%s: tried to erase region at 0x%llx but there is no such region with that offset: %d\n",
           kTag, (unsigned long long)offset, ZX_ERR_INVALID_ARGS);
    completer.Reply(ZX_ERR_INVALID_ARGS);
    return;
  }

  claimed_blocks_.erase(offset);
  completer.Reply(ZX_OK);
};

void AddressSpaceChildDriver::Ping(
    llcpp::fuchsia::hardware::goldfish::AddressSpaceChildDriverPingMessage ping,
    PingCompleter::Sync completer) {
  using llcpp::fuchsia::hardware::goldfish::AddressSpaceChildDriverPingMessage;
  AddressSpaceChildDriverPingMessage* output =
      reinterpret_cast<struct AddressSpaceChildDriverPingMessage*>(io_buffer_.virt());
  *output = ping;
  output->offset += dma_region_paddr_;
  device_->ChildDriverPing(handle_);

  completer.Reply(ZX_OK, *output);
}

// Device protocol implementation.
zx_status_t AddressSpaceChildDriver::DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn) {
  DdkTransaction transaction(txn);
  llcpp::fuchsia::hardware::goldfish::AddressSpaceChildDriver::Dispatch(this, msg, &transaction);
  return transaction.Status();
}

void AddressSpaceChildDriver::DdkRelease() { delete this; }

AddressSpaceChildDriver::Block::Block(uint64_t offset, uint64_t size, zx::pmt pmt)
    : offset(offset), size(size), pmt(std::move(pmt)) {}

AddressSpaceChildDriver::Block::~Block() {
  ZX_DEBUG_ASSERT(pmt.is_valid());
  pmt.unpin();
}

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
