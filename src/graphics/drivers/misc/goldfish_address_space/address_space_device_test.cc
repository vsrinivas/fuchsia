// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/graphics/drivers/misc/goldfish_address_space/address_space_device.h"

#include <lib/fake-bti/bti.h>
#include <lib/fake_ddk/fake_ddk.h>
#include <lib/zx/vmar.h>
#include <zircon/rights.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/pci.h>
#include <zircon/types.h>

#include <cstddef>
#include <cstring>
#include <memory>
#include <vector>

#include <mmio-ptr/fake.h>
#include <mock/ddktl/protocol/pci.h>
#include <zxtest/zxtest.h>

namespace goldfish {

namespace {

enum PciBarIds {
  PCI_CONTROL_BAR_ID = 0,
  PCI_AREA_BAR_ID = 1,
};

// MMIO Registers of PCI control bar.
// The layout should match the register offsets defined in address_space_device.cc.
struct Registers {
  uint32_t command;
  uint32_t status;
  uint32_t guest_page_size;
  uint32_t block_size_low;
  uint32_t block_size_high;
  uint32_t block_offset_low;
  uint32_t block_offset_high;
  uint32_t ping;
  uint32_t ping_info_addr_low;
  uint32_t ping_info_addr_high;
  uint32_t handle;
  uint32_t phys_start_low;
  uint32_t phys_start_high;

  void DebugPrint() const {
    printf(
        "Registers [ command %08x status %08x guest_page_size %08x block_size %08x %08x "
        "block_offset %08x %08x ping %08x ping_info_addr %08x %08x "
        "handle %08x phys_start %08x %08x ]\n",
        command, status, guest_page_size, block_size_low, block_size_high, block_offset_low,
        block_offset_high, ping, ping_info_addr_low, ping_info_addr_high, handle, phys_start_low,
        phys_start_high);
  }
};

// A RAII memory mapping wrapper of VMO to memory.
class VmoMapping {
 public:
  VmoMapping(const zx::vmo& vmo, size_t size, size_t offset = 0,
             zx_vm_option_t perm = ZX_VM_PERM_READ | ZX_VM_PERM_WRITE)
      : vmo_(vmo), size_(size), offset_(offset), perm_(perm) {
    map();
  }

  ~VmoMapping() { unmap(); }

  void map() {
    if (!ptr_) {
      zx::vmar::root_self()->map(0, vmo_, offset_, size_, perm_,
                                 reinterpret_cast<uintptr_t*>(&ptr_));
    }
  }

  void unmap() {
    if (ptr_) {
      zx::vmar::root_self()->unmap(reinterpret_cast<uintptr_t>(ptr_), size_);
      ptr_ = nullptr;
    }
  }

  void* ptr() const { return ptr_; }

 private:
  const zx::vmo& vmo_;
  size_t size_ = 0u;
  size_t offset_ = 0u;
  zx_vm_option_t perm_ = 0;
  void* ptr_ = nullptr;
};

}  // namespace

class AddressSpaceDeviceTest : public zxtest::Test {
 public:
  // |zxtest::Test|
  void SetUp() override {
    zx::bti out_bti;
    ASSERT_OK(fake_bti_create(out_bti.reset_and_get_address()));

    constexpr size_t kCtrlSize = 1024u;
    constexpr size_t kAreaSize = 128 * 1024u;
    zx::vmo vmo_control, vmo_area;
    ASSERT_OK(zx::vmo::create(kCtrlSize, 0u, &vmo_control));
    ASSERT_OK(zx::vmo::create(kAreaSize, 0u, &vmo_area));
    ASSERT_OK(vmo_control.duplicate(ZX_RIGHT_SAME_RIGHTS, &vmo_control_));

    // Simulate expected PCI banjo methods.
    mock_pci_.ExpectGetBti(ZX_OK, 0 /*index*/, std::move(out_bti))
        .ExpectGetBar(ZX_OK, PCI_CONTROL_BAR_ID,
                      zx_pci_bar_t{.id = 0,
                                   .type = ZX_PCI_BAR_TYPE_MMIO,
                                   .size = kCtrlSize,
                                   .handle = vmo_control.release()})
        .ExpectGetBar(ZX_OK, PCI_AREA_BAR_ID,
                      zx_pci_bar_t{.id = 1,
                                   .type = ZX_PCI_BAR_TYPE_MMIO,
                                   .size = kAreaSize,
                                   .handle = vmo_area.release()});

    fbl::Array<fake_ddk::ProtocolEntry> protocols(new fake_ddk::ProtocolEntry[1], 1);
    protocols[0] = {ZX_PROTOCOL_PCI,
                    *reinterpret_cast<const fake_ddk::Protocol*>(mock_pci_.GetProto())};

    ddk_.SetProtocols(std::move(protocols));
    dut_ = std::make_unique<AddressSpaceDevice>(fake_ddk::FakeParent());
  }

  // |zxtest::Test|
  void TearDown() override {}

  std::unique_ptr<VmoMapping> MapControlRegisters() const {
    return std::make_unique<VmoMapping>(vmo_control_, /*size=*/sizeof(Registers), /*offset=*/0);
  }

 protected:
  ddk::MockPci mock_pci_;
  fake_ddk::Bind ddk_;
  std::unique_ptr<AddressSpaceDevice> dut_;

  zx::vmo vmo_control_;
};

TEST_F(AddressSpaceDeviceTest, Bind) {
  ASSERT_OK(dut_->Bind());

  {
    auto mapped = MapControlRegisters();
    Registers* ctrl_regs = reinterpret_cast<Registers*>(mapped->ptr());
    ASSERT_NE(ctrl_regs->guest_page_size, 0u);
  }

  dut_->DdkAsyncRemove();
  EXPECT_TRUE(ddk_.Ok());
}

TEST_F(AddressSpaceDeviceTest, BlockAllocation) {
  ASSERT_OK(dut_->Bind());

  size_t current_offset = 0u;
  const std::vector<uint64_t> kAllocSizes = {1024u, 2048u, 3072u, 4096u};

  for (auto it = kAllocSizes.begin(); it != kAllocSizes.end(); ++it) {
    // Since we use a simulated vmo-based MMIO, we have to set the registered
    // offset before calling AllocateBlock().
    auto mapped = MapControlRegisters();
    Registers* ctrl_regs = reinterpret_cast<Registers*>(mapped->ptr());
    ctrl_regs->status = 0u;
    ctrl_regs->block_offset_low = current_offset & 0xfffffffful;
    ctrl_regs->block_offset_high = current_offset & (0xfffffffful << 32);
    zx_cache_flush(ctrl_regs, sizeof(Registers), ZX_CACHE_FLUSH_DATA | ZX_CACHE_FLUSH_INVALIDATE);

    uint64_t alloc_size = *it;
    uint64_t offset = 0u;
    dut_->AllocateBlock(&alloc_size, &offset);

    EXPECT_EQ(alloc_size, *it);
    EXPECT_EQ(offset, current_offset);

    current_offset += alloc_size;
  }

  for (auto it = kAllocSizes.rbegin(); it != kAllocSizes.rend(); ++it) {
    current_offset -= *it;

    dut_->DeallocateBlock(current_offset);

    auto mapped = MapControlRegisters();
    Registers* ctrl_regs = reinterpret_cast<Registers*>(mapped->ptr());
    EXPECT_EQ(current_offset, static_cast<uint64_t>(ctrl_regs->block_offset_low) |
                                  (static_cast<uint64_t>(ctrl_regs->block_offset_high) << 32));
  }

  dut_->DdkAsyncRemove();
  EXPECT_TRUE(ddk_.Ok());
}

}  // namespace goldfish
