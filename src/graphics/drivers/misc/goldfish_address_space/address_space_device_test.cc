// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/graphics/drivers/misc/goldfish_address_space/address_space_device.h"

#include <fuchsia/hardware/goldfish/llcpp/fidl.h>
#include <lib/fake-bti/bti.h>
#include <lib/fake_ddk/fake_ddk.h>
#include <lib/fake_ddk/fidl-helper.h>
#include <lib/zx/channel.h>
#include <lib/zx/vmar.h>
#include <zircon/rights.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/object.h>
#include <zircon/syscalls/pci.h>
#include <zircon/types.h>

#include <cstddef>
#include <cstring>
#include <memory>
#include <vector>

#include <ddk/protocol/goldfish/addressspace.h>
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

struct ProtocolDeviceOps {
  const zx_protocol_device_t* ops = nullptr;
  void* ctx = nullptr;
};

// Create our own fake_ddk Bind class. The Binder will have multiple devices
// added (AddressSpaceDevice and AddressSpaceChildDevice). Each device will
// have its own FIDL messenger bound to the remote channel.
class Binder : public fake_ddk::Bind {
 public:
  zx_status_t DeviceAdd(zx_driver_t* drv, zx_device_t* parent, device_add_args_t* args,
                        zx_device_t** out) override {
    zx_status_t status;

    if (args && args->ops) {
      if (args->ops->message) {
        // We use parent device as a key to find device
        // FIDL messengers device tree.
        fidl_messengers_[parent] = std::make_unique<fake_ddk::FidlMessenger>();
        auto* fidl = fidl_messengers_[parent].get();

        std::optional<zx::channel> remote_channel = std::nullopt;
        if (args->client_remote) {
          remote_channel = zx::channel(args->client_remote);
        }

        if ((status = fidl->SetMessageOp(args->ctx, args->ops->message,
                                         std::move(remote_channel))) < 0) {
          return status;
        }
      }
    }

    *out = fake_ddk::kFakeDevice;
    add_called_ = true;

    last_ops_.ctx = args->ctx;
    last_ops_.ops = args->ops;
    return ZX_OK;
  }

  ProtocolDeviceOps GetLastDeviceOps() { return last_ops_; }

  const zx::channel& GetFidlChannel(zx_device_t* parent) const {
    return fidl_messengers_.at(parent)->local();
  }

 private:
  std::map</*parent*/ zx_device_t*, std::unique_ptr<fake_ddk::FidlMessenger>> fidl_messengers_;
  ProtocolDeviceOps last_ops_;
};

}  // namespace

class AddressSpaceDeviceTest : public zxtest::Test {
 public:
  // |zxtest::Test|
  void SetUp() override {
    zx::bti out_bti;
    ASSERT_OK(fake_bti_create(out_bti.reset_and_get_address()));

    constexpr size_t kCtrlSize = 4096u;
    constexpr size_t kAreaSize = 128 * 4096u;
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

  template <typename T>
  static void Flush(const T* t) {
    zx_cache_flush(t, sizeof(T), ZX_CACHE_FLUSH_DATA | ZX_CACHE_FLUSH_INVALIDATE);
  }

 protected:
  ddk::MockPci mock_pci_;
  Binder ddk_;
  std::unique_ptr<AddressSpaceDevice> dut_;
  ProtocolDeviceOps child_device_ops_;

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

TEST_F(AddressSpaceDeviceTest, OpenChildDriver) {
  ASSERT_OK(dut_->Bind());

  auto mapped = MapControlRegisters();
  Registers* ctrl_regs = reinterpret_cast<Registers*>(mapped->ptr());

  zx::channel child_client, child_server;
  ASSERT_OK(zx::channel::create(0u, &child_client, &child_server));

  // Before opening child driver, we set up the mock PCI device
  // to accept GenHandle commands.
  constexpr uint32_t kChildDriverHandle = 1u;
  ctrl_regs->handle = kChildDriverHandle;
  Flush(ctrl_regs);

  // Create device.
  ASSERT_OK(dut_->GoldfishAddressSpaceOpenChildDriver(ADDRESS_SPACE_CHILD_DRIVER_TYPE_DEFAULT,
                                                      std::move(child_server)));
  child_device_ops_ = ddk_.GetLastDeviceOps();
  ASSERT_TRUE(child_device_ops_.ops->release);
  Flush(ctrl_regs);
  EXPECT_EQ(ctrl_regs->handle, kChildDriverHandle);

  // Test availability of the FIDL channel communication.
  llcpp::fuchsia::hardware::goldfish::AddressSpaceChildDriver::SyncClient client(
      std::move(child_client));

  // Set up return status and offset on the mock PCI device
  // to accept AllocateBlock() calls.
  ctrl_regs->status = 0u;
  ctrl_regs->block_offset_low = 0u;
  ctrl_regs->block_offset_high = 0u;
  Flush(ctrl_regs);

  // Test AddressSpaceChildDriver.AllocateBlock()
  auto result_alloc = client.AllocateBlock(4096u);
  EXPECT_TRUE(result_alloc.ok());
  EXPECT_EQ(result_alloc.value().res, ZX_OK);
  EXPECT_NE(result_alloc.value().paddr, 0u);
  EXPECT_TRUE(result_alloc.value().vmo.is_valid());

  // Test AddressSpaceChildDriver.DeallocateBlock()
  auto paddr = result_alloc.value().paddr;
  auto result_dealloc = client.DeallocateBlock(paddr);
  EXPECT_TRUE(result_dealloc.ok());
  EXPECT_EQ(result_dealloc.value().res, ZX_OK);

  dut_->DdkAsyncRemove();
  EXPECT_TRUE(ddk_.Ok());

  child_device_ops_.ops->release(child_device_ops_.ctx);
}

}  // namespace goldfish
