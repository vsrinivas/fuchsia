// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/graphics/drivers/misc/goldfish_address_space/address_space_device.h"

#include <fidl/fuchsia.hardware.goldfish/cpp/common_types.h>
#include <fidl/fuchsia.hardware.goldfish/cpp/wire.h>
#include <fidl/fuchsia.hardware.pci/cpp/wire_test_base.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/loop.h>
#include <lib/fake-bti/bti.h>
#include <lib/mmio-ptr/fake.h>
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

#include <zxtest/zxtest.h>

#include "src/devices/testing/mock-ddk/mock-device.h"

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
      zx::vmar::root_self()->map(perm_, 0, vmo_, offset_, size_,
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

class FakePci : public fidl::testing::WireTestBase<fuchsia_hardware_pci::Device> {
 public:
  void GetBti(GetBtiRequestView request, GetBtiCompleter::Sync& completer) override {
    ASSERT_OK(fake_bti_create(out_bti_.reset_and_get_address()));
    completer.ReplySuccess(std::move(out_bti_));
  }

  void GetBar(GetBarRequestView request, GetBarCompleter::Sync& completer) override {
    constexpr size_t kCtrlSize = 4096u;
    constexpr size_t kAreaSize = 128 * 4096u;

    if (request->bar_id == PCI_CONTROL_BAR_ID) {
      zx::vmo vmo_control;
      ASSERT_OK(zx::vmo::create(kCtrlSize, 0u, &vmo_control));
      ASSERT_OK(vmo_control.duplicate(ZX_RIGHT_SAME_RIGHTS, &vmo_control_));

      completer.ReplySuccess(
          {.bar_id = 0,
           .size = kCtrlSize,
           .result = fuchsia_hardware_pci::wire::BarResult::WithVmo(std::move(vmo_control))});
    } else if (request->bar_id == PCI_AREA_BAR_ID) {
      zx::vmo vmo_area;
      ASSERT_OK(zx::vmo::create(kAreaSize, 0u, &vmo_area));

      completer.ReplySuccess(
          {.bar_id = 1,
           .size = kAreaSize,
           .result = fuchsia_hardware_pci::wire::BarResult::WithVmo(std::move(vmo_area))});
    } else {
      completer.ReplyError(ZX_ERR_NOT_FOUND);
    }
  }

  std::unique_ptr<VmoMapping> MapControlRegisters() const {
    return std::make_unique<VmoMapping>(vmo_control_, /*size=*/sizeof(Registers), /*offset=*/0);
  }

  void NotImplemented_(const std::string& name, fidl::CompleterBase& completer) final {
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

 private:
  zx::bti out_bti_;
  zx::vmo vmo_control_;
};

}  // namespace

class AddressSpaceDeviceTest : public zxtest::Test {
 public:
  AddressSpaceDeviceTest()
      : async_loop_(&kAsyncLoopConfigNeverAttachToThread),
        pci_loop_(&kAsyncLoopConfigNeverAttachToThread) {}

  // |zxtest::Test|
  void SetUp() override {
    fake_root_ = MockDevice::FakeRootParent();

    fake_root_->AddFidlProtocol(
        fidl::DiscoverableProtocolName<fuchsia_hardware_pci::Device>,
        [this](zx::channel channel) {
          fidl::BindServer(pci_loop_.dispatcher(),
                           fidl::ServerEnd<fuchsia_hardware_pci::Device>(std::move(channel)),
                           &fake_pci_);
          return ZX_OK;
        },
        "pci");

    pci_loop_.StartThread("pci-fidl-server-thread");

    std::unique_ptr<AddressSpaceDevice> dut(
        new AddressSpaceDevice(fake_root_.get(), async_loop_.dispatcher()));
    ASSERT_OK(dut->Bind());
    dut_ = dut.release();
  }

  // |zxtest::Test|
  void TearDown() override {
    device_async_remove(dut_->zxdev());
    ASSERT_OK(mock_ddk::ReleaseFlaggedDevices(fake_root_.get()));
  }
  std::unique_ptr<VmoMapping> MapControlRegisters() const {
    std::unique_ptr<VmoMapping> ret;

    // Have to run this code in the pci_loop's async dispatcher because fake_pci
    // is bound to it as a FIDL server.
    sync_completion_t completion;
    async::PostTask(pci_loop_.dispatcher(), [&] {
      ret = fake_pci_.MapControlRegisters();
      sync_completion_signal(&completion);
    });
    sync_completion_wait(&completion, ZX_TIME_INFINITE);

    return ret;
  }

  template <typename T>
  static void Flush(const T* t) {
    zx_cache_flush(t, sizeof(T), ZX_CACHE_FLUSH_DATA | ZX_CACHE_FLUSH_INVALIDATE);
  }

 protected:
  async::Loop async_loop_;
  async::Loop pci_loop_;
  FakePci fake_pci_;
  std::shared_ptr<MockDevice> fake_root_;
  AddressSpaceDevice* dut_;
};

TEST_F(AddressSpaceDeviceTest, Bind) {
  auto mapped = MapControlRegisters();
  Registers* ctrl_regs = reinterpret_cast<Registers*>(mapped->ptr());
  ASSERT_NE(ctrl_regs->guest_page_size, 0u);
}

TEST_F(AddressSpaceDeviceTest, BlockAllocation) {
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
}

TEST_F(AddressSpaceDeviceTest, OpenChildDriver) {
  auto mapped = MapControlRegisters();
  Registers* ctrl_regs = reinterpret_cast<Registers*>(mapped->ptr());

  zx::result endpoints =
      fidl::CreateEndpoints<fuchsia_hardware_goldfish::AddressSpaceChildDriver>();
  ASSERT_OK(endpoints.status_value());

  // Before opening child driver, we set up the mock PCI device
  // to accept GenHandle commands.
  constexpr uint32_t kChildDriverHandle = 1u;
  ctrl_regs->handle = kChildDriverHandle;
  Flush(ctrl_regs);

  // Create device.
  ASSERT_OK(dut_->OpenChildDriver(fuchsia_hardware_goldfish::AddressSpaceChildDriverType::kDefault,
                                  endpoints->server.TakeChannel()));
  Flush(ctrl_regs);
  EXPECT_EQ(ctrl_regs->handle, kChildDriverHandle);

  // Test availability of the FIDL channel communication.
  fidl::WireSyncClient client{std::move(endpoints->client)};

  // Bind server side:
  async::Loop loop(&kAsyncLoopConfigNeverAttachToThread);
  ASSERT_OK(loop.StartThread("child_driver_thread"));
  auto child = dut_->zxdev()->GetLatestChild();
  auto child_dev = child->GetDeviceContext<goldfish::AddressSpaceChildDriver>();
  ASSERT_NE(child, nullptr);
  auto binding =
      fidl::BindServer(loop.dispatcher(),
                       fidl::ServerEnd<fuchsia_hardware_goldfish::AddressSpaceChildDriver>(
                           std::move(child->TakeClientRemote())),
                       child_dev);

  // Set up return status and offset on the mock PCI device
  // to accept AllocateBlock() calls.
  ctrl_regs->status = 0u;
  ctrl_regs->block_offset_low = 0u;
  ctrl_regs->block_offset_high = 0u;
  Flush(ctrl_regs);

  // Test AddressSpaceChildDriver.AllocateBlock()
  auto result_alloc = client->AllocateBlock(4096u);
  EXPECT_TRUE(result_alloc.ok());
  EXPECT_EQ(result_alloc.value().res, ZX_OK);
  EXPECT_NE(result_alloc.value().paddr, 0u);
  EXPECT_TRUE(result_alloc.value().vmo.is_valid());

  // Test AddressSpaceChildDriver.DeallocateBlock()
  auto paddr = result_alloc.value().paddr;
  auto result_dealloc = client->DeallocateBlock(paddr);
  EXPECT_TRUE(result_dealloc.ok());
  EXPECT_EQ(result_dealloc.value().res, ZX_OK);
}

}  // namespace goldfish
