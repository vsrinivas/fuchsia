// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "intel-i915.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/mock-sysmem/mock-buffer-collection.h>

#include <type_traits>

#include <ddk/driver.h>

#include "interrupts.h"
#include "zxtest/zxtest.h"

namespace sysmem = llcpp::fuchsia::sysmem;

static void empty_callback(void* ctx, uint32_t master_interrupt_control, uint64_t timestamp) {}

namespace {
class MockNoCpuBufferCollection : public mock_sysmem::MockBufferCollection {
 public:
  bool set_constraints_called() const { return set_constraints_called_; }
  void SetConstraints(bool has_constraints, sysmem::BufferCollectionConstraints constraints,
                      SetConstraintsCompleter::Sync _completer) override {
    set_constraints_called_ = true;
    EXPECT_FALSE(constraints.buffer_memory_constraints.inaccessible_domain_supported);
    EXPECT_FALSE(constraints.buffer_memory_constraints.cpu_domain_supported);
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
  ASSERT_OK(fidl::Bind(loop.dispatcher(), std::move(server_channel), &collection));

  EXPECT_OK(
      display.DisplayControllerImplSetBufferCollectionConstraints(&image, client_channel.get()));

  loop.RunUntilIdle();
  EXPECT_TRUE(collection.set_constraints_called());
}

TEST(IntelI915Display, SetInterruptCallback) {
  i915::Controller controller(nullptr);

  // Allocate Interrupts into non-zero memory
  std::aligned_storage_t<sizeof(i915::Interrupts), alignof(i915::Interrupts)> mem;
  memset(&mem, 0xff, sizeof(i915::Interrupts));
  auto interrupts = new (&mem) i915::Interrupts(&controller);

  zx_intel_gpu_core_interrupt_t callback = {.callback = empty_callback, .ctx = nullptr};
  EXPECT_EQ(ZX_OK, interrupts->SetInterruptCallback(&callback, 0 /* interrupt_mask */));
  interrupts->~Interrupts();
}

}  // namespace
