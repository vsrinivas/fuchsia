// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/graphics/display/drivers/intel-i915-tgl/intel-i915-tgl.h"

#include <fidl/fuchsia.sysmem/cpp/wire_test_base.h>
#include <fuchsia/hardware/sysmem/cpp/banjo.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async-loop/loop.h>
#include <lib/ddk/driver.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/mmio-ptr/fake.h>
#include <lib/zircon-internal/align.h>
#include <lib/zx/vmar.h>
#include <zircon/pixelformat.h>

#include <type_traits>
#include <vector>

#include <gtest/gtest.h>

#include "src/devices/pci/testing/pci_protocol_fake.h"
#include "src/devices/testing/mock-ddk/mock-device.h"
#include "src/graphics/display/drivers/intel-i915-tgl/pci-ids.h"

#define ASSERT_OK(x) ASSERT_EQ(ZX_OK, (x))
#define EXPECT_OK(x) EXPECT_EQ(ZX_OK, (x))

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

namespace i915_tgl {

namespace {

class MockNoCpuBufferCollection
    : public fidl::testing::WireTestBase<fuchsia_sysmem::BufferCollection> {
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

class TglIntegrationTest : public ::testing::Test {
 protected:
  void SetUp() final {
    SetFramebuffer({});

    pci_.CreateBar(0u, std::numeric_limits<uint32_t>::max(), /*is_mmio=*/true);
    pci_.AddLegacyInterrupt();

    // This configures the "GMCH Graphics Control" register to report 2MB for the available GTT
    // Graphics Memory. All other bits of this register are set to zero and should get populated as
    // required for the tests below.
    pci_.PciWriteConfig16(tgl_registers::GmchGfxControl::kAddr, 0x40);

    constexpr uint16_t kIntelVendorId = 0x8086;
    pci_.SetDeviceInfo({
        .vendor_id = kIntelVendorId,
        .device_id = kTestDeviceDid,
    });

    parent_ = MockDevice::FakeRootParent();
    parent_->AddProtocol(ZX_PROTOCOL_SYSMEM, sysmem_.proto_ops(), &sysmem_, "sysmem");

    parent_->AddFidlProtocol(
        fidl::DiscoverableProtocolName<fuchsia_hardware_pci::Device>,
        [this](zx::channel channel) {
          fidl::BindServer(loop_.dispatcher(),
                           fidl::ServerEnd<fuchsia_hardware_pci::Device>(std::move(channel)),
                           &pci_);
          return ZX_OK;
        },
        "pci");
    loop_.StartThread("pci-fidl-server-thread");
  }

  MockDevice* parent() const { return parent_.get(); }

 private:
  async::Loop loop_{&kAsyncLoopConfigNeverAttachToThread};
  // Emulated parent protocols.
  pci::FakePciProtocol pci_;
  FakeSysmem sysmem_;

  // mock-ddk parent device of the Controller under test.
  std::shared_ptr<MockDevice> parent_;
};

TEST(IntelI915TglDisplay, SysmemRequirements) {
  Controller display(nullptr);
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

TEST(IntelI915TglDisplay, SysmemNoneFormat) {
  Controller display(nullptr);
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

TEST(IntelI915TglDisplay, SysmemInvalidFormat) {
  Controller display(nullptr);
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

TEST(IntelI915TglDisplay, SysmemInvalidType) {
  Controller display(nullptr);
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

// Tests that DDK basic DDK lifecycle hooks function as expected.
TEST_F(TglIntegrationTest, BindAndInit) {
  ASSERT_OK(Controller::Create(parent()));

  // There should be two published devices: one "intel_i915" device rooted at |parent()|, and a
  // grandchild "intel-gpu-core" device.
  ASSERT_EQ(1u, parent()->child_count());
  auto dev = parent()->GetLatestChild();
  ASSERT_EQ(2u, dev->child_count());

  // Perform the async initialization and wait for a response.
  dev->InitOp();
  EXPECT_EQ(ZX_OK, dev->WaitUntilInitReplyCalled());

  // Unbind the device and ensure it completes synchronously.
  dev->UnbindOp();
  EXPECT_TRUE(dev->UnbindReplyCalled());

  mock_ddk::ReleaseFlaggedDevices(parent());
  EXPECT_EQ(0u, dev->child_count());
}

// Tests that the device can initialize even if bootloader framebuffer information is not available
// and global GTT allocations start at offset 0.
TEST_F(TglIntegrationTest, InitFailsIfBootloaderGetInfoFails) {
  SetFramebuffer({.status = ZX_ERR_INVALID_ARGS});

  ASSERT_EQ(ZX_OK, Controller::Create(parent()));
  auto dev = parent()->GetLatestChild();
  Controller* ctx = dev->GetDeviceContext<Controller>();

  uint64_t addr;
  EXPECT_EQ(ZX_OK, ctx->IntelGpuCoreGttAlloc(1, &addr));
  EXPECT_EQ(0u, addr);
}

// TODO(fxbug.dev/85836): Add tests for DisplayPort display enumeration by InitOp, covering the
// following cases:
//   - Display found during start up but not already powered.
//   - Display found during start up but already powered up.
//   - Display added and removed in a hotplug event.
// TODO(fxbug.dev/86314): Add test for HDMI display enumeration by InitOp.
// TODO(fxbug.dev/86315): Add test for DVI display enumeration by InitOp.

TEST_F(TglIntegrationTest, GttAllocationDoesNotOverlapBootloaderFramebuffer) {
  constexpr uint32_t kStride = 1920;
  constexpr uint32_t kHeight = 1080;
  SetFramebuffer({
      .format = ZX_PIXEL_FORMAT_RGB_888,
      .width = kStride,
      .height = kHeight,
      .stride = kStride,
  });
  ASSERT_OK(Controller::Create(parent()));

  // There should be two published devices: one "intel_i915" device rooted at |parent()|, and a
  // grandchild "intel-gpu-core" device.
  ASSERT_EQ(1u, parent()->child_count());
  auto dev = parent()->GetLatestChild();
  Controller* ctx = dev->GetDeviceContext<Controller>();

  uint64_t addr;
  EXPECT_EQ(ZX_OK, ctx->IntelGpuCoreGttAlloc(1, &addr));
  EXPECT_EQ(ZX_ROUNDUP(kHeight * kStride * 3, PAGE_SIZE), addr);
}

}  // namespace

}  // namespace i915_tgl
