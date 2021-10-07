// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "intel-i915.h"

#include <fidl/fuchsia.sysmem/cpp/wire_test_base.h>
#include <fuchsia/hardware/sysmem/cpp/banjo.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/ddk/driver.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/mmio-ptr/fake.h>
#include <lib/zircon-internal/align.h>
#include <zircon/pixelformat.h>

#include <type_traits>
#include <vector>

#include <zxtest/zxtest.h>

#include "src/devices/pci/testing/pci_protocol_fake.h"
#include "src/devices/testing/mock-ddk/mock-device.h"

namespace sysmem = fuchsia_sysmem;

namespace {

// Module-scope global data structure that acts as the data source for the zx_framebuffer_get_info
// implementation below.
struct Framebuffer {
  zx_status_t status = ZX_OK;
  uint32_t format = 0u;
  uint32_t width = 0u;
  uint32_t height = 0u;
  uint32_t stride = 0u;
};
thread_local Framebuffer g_framebuffer;

void SetFramebuffer(const Framebuffer& buffer) { g_framebuffer = buffer; }

}  // namespace

zx_status_t zx_framebuffer_get_info(zx_handle_t resource, uint32_t* format, uint32_t* width,
                                    uint32_t* height, uint32_t* stride) {
  *format = g_framebuffer.format;
  *width = g_framebuffer.width;
  *height = g_framebuffer.height;
  *stride = g_framebuffer.stride;
  return g_framebuffer.status;
}

namespace {

class MockNoCpuBufferCollection : public fuchsia_sysmem::testing::BufferCollection_TestBase {
 public:
  bool set_constraints_called() const { return set_constraints_called_; }
  void SetConstraints(SetConstraintsRequestView request,
                      SetConstraintsCompleter::Sync& _completer) override {
    set_constraints_called_ = true;
    EXPECT_FALSE(request->constraints.buffer_memory_constraints.inaccessible_domain_supported);
    EXPECT_FALSE(request->constraints.buffer_memory_constraints.cpu_domain_supported);
  }

  void NotImplemented_(const std::string& name, fidl::CompleterBase& completer) override {
    EXPECT_TRUE(false);
  }

 private:
  bool set_constraints_called_ = false;
};

class FakeSysmem : public ddk::SysmemProtocol<FakeSysmem> {
 public:
  FakeSysmem() = default;

  const sysmem_protocol_ops_t* proto_ops() const { return &sysmem_protocol_ops_; }

  zx_status_t SysmemConnect(zx::channel allocator2_request) { return ZX_ERR_NOT_SUPPORTED; }
  zx_status_t SysmemRegisterHeap(uint64_t heap, zx::channel heap_connection) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  zx_status_t SysmemRegisterSecureMem(zx::channel tee_connection) { return ZX_ERR_NOT_SUPPORTED; }
  zx_status_t SysmemUnregisterSecureMem() { return ZX_ERR_NOT_SUPPORTED; }
};

class IntegrationTest : public zxtest::Test {
 protected:
  void SetUp() final {
    SetFramebuffer({});

    pci_.CreateBar(0u, std::numeric_limits<uint32_t>::max(), /*is_mmio=*/true);
    pci_.AddLegacyInterrupt();

    // This configures the "GMCH Graphics Control" register to report 2MB for the available GTT
    // Graphics Memory. All other bits of this register are set to zero and should get populated as
    // required for the tests below.
    pci_.PciConfigWrite16(registers::GmchGfxControl::kAddr, 0x40);

    parent_ = MockDevice::FakeRootParent();
    parent_->AddProtocol(ZX_PROTOCOL_SYSMEM, sysmem_.proto_ops(), &sysmem_, "sysmem");
    parent_->AddProtocol(ZX_PROTOCOL_PCI, pci_.get_protocol().ops, pci_.get_protocol().ctx, "pci");
  }

  void TearDown() final { device_async_remove(parent()); }

  MockDevice* parent() const { return parent_.get(); }

 private:
  // Emulated parent protocols.
  pci::FakePciProtocol pci_;
  FakeSysmem sysmem_;

  // mock-ddk parent device of the i915::Controller under test.
  std::shared_ptr<MockDevice> parent_;
};

TEST(IntelI915Display, SysmemRequirements) {
  i915::Controller display(nullptr);
  zx::channel server_channel, client_channel;
  ASSERT_OK(zx::channel::create(0u, &server_channel, &client_channel));

  MockNoCpuBufferCollection collection;
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  image_t image = {};
  image.pixel_format = ZX_PIXEL_FORMAT_ARGB_8888;
  ASSERT_OK(
      fidl::BindSingleInFlightOnly(loop.dispatcher(), std::move(server_channel), &collection));

  EXPECT_OK(
      display.DisplayControllerImplSetBufferCollectionConstraints(&image, client_channel.get()));

  loop.RunUntilIdle();
  EXPECT_TRUE(collection.set_constraints_called());
}

TEST(IntelI915Display, SysmemNoneFormat) {
  i915::Controller display(nullptr);
  zx::channel server_channel, client_channel;
  ASSERT_OK(zx::channel::create(0u, &server_channel, &client_channel));

  MockNoCpuBufferCollection collection;
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  image_t image = {};
  image.pixel_format = ZX_PIXEL_FORMAT_NONE;
  ASSERT_OK(
      fidl::BindSingleInFlightOnly(loop.dispatcher(), std::move(server_channel), &collection));

  EXPECT_OK(
      display.DisplayControllerImplSetBufferCollectionConstraints(&image, client_channel.get()));

  loop.RunUntilIdle();
  EXPECT_TRUE(collection.set_constraints_called());
}

TEST(IntelI915Display, SysmemInvalidFormat) {
  i915::Controller display(nullptr);
  zx::channel server_channel, client_channel;
  ASSERT_OK(zx::channel::create(0u, &server_channel, &client_channel));

  MockNoCpuBufferCollection collection;
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  image_t image = {};
  image.pixel_format = UINT32_MAX;
  ASSERT_OK(
      fidl::BindSingleInFlightOnly(loop.dispatcher(), std::move(server_channel), &collection));

  EXPECT_EQ(ZX_ERR_INVALID_ARGS, display.DisplayControllerImplSetBufferCollectionConstraints(
                                     &image, client_channel.get()));

  loop.RunUntilIdle();
  EXPECT_FALSE(collection.set_constraints_called());
}

TEST(IntelI915Display, SysmemInvalidType) {
  i915::Controller display(nullptr);
  zx::channel server_channel, client_channel;
  ASSERT_OK(zx::channel::create(0u, &server_channel, &client_channel));

  MockNoCpuBufferCollection collection;
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  image_t image = {};
  image.pixel_format = ZX_PIXEL_FORMAT_ARGB_8888;
  image.type = 1000000;
  ASSERT_OK(
      fidl::BindSingleInFlightOnly(loop.dispatcher(), std::move(server_channel), &collection));

  EXPECT_EQ(ZX_ERR_INVALID_ARGS, display.DisplayControllerImplSetBufferCollectionConstraints(
                                     &image, client_channel.get()));

  loop.RunUntilIdle();
  EXPECT_FALSE(collection.set_constraints_called());
}

TEST(IntelI915Display, BacklightValue) {
  i915::Controller controller(nullptr);
  i915::DpDisplay display(&controller, 0, registers::kDdis[0]);

  constexpr uint32_t kMinimumRegCount = 0xd0000 / sizeof(uint32_t);
  std::vector<uint32_t> regs(kMinimumRegCount);
  mmio_buffer_t buffer{.vaddr = FakeMmioPtr(regs.data()),
                       .offset = 0,
                       .size = regs.size() * sizeof(uint32_t),
                       .vmo = ZX_HANDLE_INVALID};
  controller.SetMmioForTesting(ddk::MmioBuffer(buffer));
  registers::SouthBacklightCtl2::Get()
      .FromValue(0)
      .set_modulation_freq(1024)
      .set_duty_cycle(512)
      .WriteTo(controller.mmio_space());

  const_cast<i915::IgdOpRegion&>(controller.igd_opregion())
      .SetIsEdpForTesting(registers::kDdis[0], true);
  EXPECT_EQ(0.5, display.GetBacklightBrightness());

  // Unset so controller teardown doesn't crash.
  controller.ResetMmioSpaceForTesting();
}

// Tests that DDK basic DDK lifecycle hooks function as expected.
TEST_F(IntegrationTest, BindAndInit) {
  ASSERT_OK(i915::Controller::Create(parent()));

  // There should be two published devices: one "intel_i915" device rooted at |parent()|, and a
  // grandchild "intel-gpu-core" device.
  ASSERT_EQ(1, parent()->child_count());
  auto dev = parent()->GetLatestChild();
  ASSERT_EQ(1, dev->child_count());

  // Perform the async initialization and wait for a response.
  dev->InitOp();
  EXPECT_EQ(ZX_OK, dev->WaitUntilInitReplyCalled());

  // Unbind the device and ensure it completes synchronously.
  dev->UnbindOp();
  EXPECT_TRUE(dev->UnbindReplyCalled());

  mock_ddk::ReleaseFlaggedDevices(parent());
  EXPECT_EQ(0, dev->child_count());
}

// Tests that the device fails to initialize if bootloader framebuffer information cannot be
// obtained.
TEST_F(IntegrationTest, InitFailsIfBootloaderGetInfoFails) {
  g_framebuffer.status = ZX_ERR_NOT_SUPPORTED;

  EXPECT_EQ(ZX_ERR_NOT_SUPPORTED, i915::Controller::Create(parent()));
  EXPECT_EQ(0u, parent()->children().size());
}

// TODO(fxbug.dev/85836): Add test for display initialization by InitOp.

TEST_F(IntegrationTest, GttAllocationDoesNotOverlapBootloaderFramebuffer) {
  constexpr uint32_t kStride = 1920;
  constexpr uint32_t kHeight = 1080;
  SetFramebuffer({
      .format = ZX_PIXEL_FORMAT_RGB_888,
      .width = kStride,
      .height = kHeight,
      .stride = kStride,
  });
  ASSERT_OK(i915::Controller::Create(parent()));

  // There should be two published devices: one "intel_i915" device rooted at |parent()|, and a
  // grandchild "intel-gpu-core" device.
  ASSERT_EQ(1, parent()->child_count());
  auto dev = parent()->GetLatestChild();
  i915::Controller* ctx = dev->GetDeviceContext<i915::Controller>();

  uint64_t addr;
  EXPECT_EQ(ZX_OK, ctx->IntelGpuCoreGttAlloc(1, &addr));
  EXPECT_EQ(ZX_ROUNDUP(kHeight * kStride * 3, PAGE_SIZE), addr);
}

}  // namespace
