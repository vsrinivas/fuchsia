// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "address-space-device.h"

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/protocol/pci-lib.h>
#include <ddk/trace/event.h>
#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>
#include <fuchsia/hardware/goldfish/address/space/c/fidl.h>
#include <lib/fidl-utils/bind.h>
#include <limits.h>

#include <map>
#include <memory>

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
};

enum Commands {
    COMMAND_ALLOCATE_BLOCK = 1,
    COMMAND_DEALLOCATE_BLOCK = 2,
};

enum PciBarIds {
    PCI_CONTROL_BAR_ID = 0,
    PCI_AREA_BAR_ID = 1,
};

class Instance;
using InstanceType = ddk::Device<Instance, ddk::Messageable>;

// This class implements an address space instance device.
class Instance : public InstanceType {
public:
    explicit Instance(AddressSpaceDevice* device)
        : Device(device->zxdev()), device_(device) {}
    ~Instance() {
        for (auto& block : blocks_) {
            device_->DeallocateBlock(block.second.offset);
        }
    }

    zx_status_t Bind() {
        TRACE_DURATION("gfx", "Instance::Bind");
        return DdkAdd("address-space", DEVICE_ADD_INSTANCE);
    }

    zx_status_t FidlAllocateBlock(uint64_t size, fidl_txn_t* txn) {
        TRACE_DURATION("gfx", "Instance::FidlAllocateBlock", "size", size);

        uint64_t offset;
        uint32_t result = device_->AllocateBlock(&size, &offset);
        if (result) {
            zxlogf(ERROR, "%s: failed to allocate block: %lu %d\n", kTag, size,
                   result);
            return fuchsia_hardware_goldfish_address_space_DeviceAllocateBlock_reply(
                txn, ZX_ERR_INTERNAL, 0, ZX_HANDLE_INVALID);
        }

        auto deallocate_block = fbl::MakeAutoCall(
            [this, offset]() { device_->DeallocateBlock(offset); });

        zx_paddr_t paddr;
        zx::pmt pmt;
        zx_status_t status = device_->PinBlock(offset, size, &paddr, &pmt);
        if (status != ZX_OK) {
            zxlogf(ERROR, "%s: failed to pin block: %d\n", kTag, status);
            return status;
        }

        // The VMO created here is a sub-region of device::dma_region_.
        // TODO(reveman): Stop using root resource when we have an alternative
        // solution (e.g. non-COW child VMOs) or a more limited resource for the
        // phys mapping.
        zx_handle_t vmo = ZX_HANDLE_INVALID;
        status = zx_vmo_create_physical(get_root_resource(), paddr, size, &vmo);
        if (status != ZX_OK) {
            zxlogf(ERROR, "%s: failed to create VMO: %d\n", kTag, status);
            return status;
        }

        deallocate_block.cancel();
        blocks_[paddr] = {offset, std::move(pmt)};
        return fuchsia_hardware_goldfish_address_space_DeviceAllocateBlock_reply(
            txn, ZX_OK, paddr, vmo);
    }

    zx_status_t FidlDeallocateBlock(uint64_t paddr, fidl_txn_t* txn) {
        TRACE_DURATION("gfx", "Instance::FidlDeallocateBlock", "paddr", paddr);

        auto it = blocks_.find(paddr);
        if (it == blocks_.end()) {
            zxlogf(ERROR, "%s: invalid block: %lu\n", kTag, paddr);
            return ZX_ERR_INVALID_ARGS;
        }

        uint32_t result = device_->DeallocateBlock(it->second.offset);
        if (result) {
            zxlogf(ERROR, "%s: failed to deallocate block: %lu %d\n", kTag,
                   paddr, result);
            return fuchsia_hardware_goldfish_address_space_DeviceDeallocateBlock_reply(
                txn, ZX_ERR_INTERNAL);
        }

        blocks_.erase(it);
        return fuchsia_hardware_goldfish_address_space_DeviceDeallocateBlock_reply(
            txn, ZX_OK);
    }

    // Device protocol implementation.
    zx_status_t DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn) {
        using Binder = fidl::Binder<Instance>;

        static const fuchsia_hardware_goldfish_address_space_Device_ops_t kOps =
            {
                .AllocateBlock =
                    Binder::BindMember<&Instance::FidlAllocateBlock>,
                .DeallocateBlock =
                    Binder::BindMember<&Instance::FidlDeallocateBlock>,
            };

        return fuchsia_hardware_goldfish_address_space_Device_dispatch(
            this, txn, msg, &kOps);
    }
    zx_status_t DdkClose(uint32_t flags) { return ZX_OK; }
    void DdkRelease() { delete this; }

private:
    struct Block {
        uint64_t offset;
        zx::pmt pmt;
    };
    AddressSpaceDevice* const device_;
    // TODO(TC-383): This should be std::unordered_map.
    using BlockMap = std::map<uint64_t, Block>;
    BlockMap blocks_;

    DISALLOW_COPY_ASSIGN_AND_MOVE(Instance);
};

uint32_t upper_32_bits(uint64_t n) {
    return static_cast<uint32_t>(n >> 32);
}

uint32_t lower_32_bits(uint64_t n) {
    return static_cast<uint32_t>(n);
}

} // namespace

// static
zx_status_t AddressSpaceDevice::Create(void* ctx, zx_device_t* device) {
    auto address_space_device =
        std::make_unique<goldfish::AddressSpaceDevice>(device);

    zx_status_t status = address_space_device->Bind();
    if (status == ZX_OK) {
        // devmgr now owns device.
        __UNUSED auto* dev = address_space_device.release();
    }
    return status;
}

AddressSpaceDevice::AddressSpaceDevice(zx_device_t* parent)
    : DeviceType(parent), pci_(parent) {}

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
    status = ddk::MmioBuffer::Create(0, control_bar.size,
                                     zx::vmo(control_bar.handle),
                                     ZX_CACHE_POLICY_UNCACHED_DEVICE, &mmio_);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: failed to create MMIO buffer: %d\n", kTag, status);
        return status;
    }

    zx_pci_bar_t area_bar;
    status = pci_.GetBar(PCI_AREA_BAR_ID, &area_bar);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: get_bar: could not get area BAR: %d\n", kTag,
               status);
        return status;
    }
    ZX_DEBUG_ASSERT(area_bar.type == ZX_PCI_BAR_TYPE_MMIO);
    ZX_DEBUG_ASSERT(area_bar.handle != ZX_HANDLE_INVALID);
    dma_region_ = zx::vmo(area_bar.handle);

    mmio_->Write32(PAGE_SIZE, REGISTER_GUEST_PAGE_SIZE);

    return DdkAdd("goldfish-address-space", 0, nullptr, 0,
                  ZX_PROTOCOL_GOLDFISH_ADDRESS_SPACE);
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

zx_status_t AddressSpaceDevice::PinBlock(uint64_t offset, uint64_t size,
                                         zx_paddr_t* paddr, zx::pmt* pmt) {
    return bti_.pin(ZX_BTI_PERM_READ | ZX_BTI_PERM_WRITE | ZX_BTI_CONTIGUOUS,
                    dma_region_, offset, size, paddr, 1, pmt);
}

zx_status_t AddressSpaceDevice::DdkOpen(zx_device_t** dev_out, uint32_t flags) {
    auto instance = std::make_unique<Instance>(this);

    zx_status_t status = instance->Bind();
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: failed to init instance: %d\n", kTag, status);
        return status;
    }

    Instance* instance_ptr = instance.release();
    *dev_out = instance_ptr->zxdev();
    return ZX_OK;
}

void AddressSpaceDevice::DdkUnbind() {
    DdkRemove();
}

void AddressSpaceDevice::DdkRelease() {
    delete this;
}

uint32_t AddressSpaceDevice::CommandMmioLocked(uint32_t cmd) {
    mmio_->Write32(cmd, REGISTER_COMMAND);
    return mmio_->Read32(REGISTER_STATUS);
}

} // namespace goldfish

static constexpr zx_driver_ops_t goldfish_address_space_driver_ops =
    []() -> zx_driver_ops_t {
    zx_driver_ops_t ops = {};
    ops.version = DRIVER_OPS_VERSION;
    ops.bind = goldfish::AddressSpaceDevice::Create;
    return ops;
}();

ZIRCON_DRIVER_BEGIN(goldfish_address_space, goldfish_address_space_driver_ops,
                    "zircon", "0.1", 3)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PCI),
    BI_ABORT_IF(NE, BIND_PCI_VID, GOLDFISH_ADDRESS_SPACE_PCI_VID),
    BI_MATCH_IF(EQ, BIND_PCI_DID, GOLDFISH_ADDRESS_SPACE_PCI_DID),
ZIRCON_DRIVER_END(goldfish_address_space)
