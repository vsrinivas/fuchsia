// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "intel-i915.h"

#include <fuchsia/sysmem/llcpp/fidl_test_base.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/ddk/driver.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/mmio-ptr/fake.h>
#include <zircon/pixelformat.h>

#include <type_traits>
#include <vector>

#include "interrupts.h"
#include "zxtest/zxtest.h"

namespace sysmem = fuchsia_sysmem;

static void empty_callback(void* ctx, uint32_t master_interrupt_control, uint64_t timestamp) {}

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

TEST(IntelI915Display, SetInterruptCallback) {
  i915::Controller controller(nullptr);

  // Allocate Interrupts into non-zero memory
  std::aligned_storage_t<sizeof(i915::Interrupts), alignof(i915::Interrupts)> mem;
  memset(&mem, 0xff, sizeof(i915::Interrupts));
  auto interrupts = new (&mem) i915::Interrupts(&controller);

  intel_gpu_core_interrupt_t callback = {.callback = empty_callback, .ctx = nullptr};
  EXPECT_EQ(ZX_OK, interrupts->SetInterruptCallback(&callback, 0 /* interrupt_mask */));
  interrupts->~Interrupts();
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

}  // namespace
