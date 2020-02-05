// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gpu.h"

#include <fuchsia/sysmem/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fake-bti/bti.h>
#include <lib/fidl-async/cpp/bind.h>

#include <ddk/protocol/display/controller.h>
#include <zxtest/zxtest.h>

#include "backends/fake.h"

namespace sysmem = llcpp::fuchsia::sysmem;

namespace {
// Use a stub buffer collection instead of the real sysmem since some tests may
// require things that aren't available on the current system.
class StubBufferCollection : public sysmem::BufferCollection::Interface {
 public:
  void SetEventSink(::zx::channel events, SetEventSinkCompleter::Sync _completer) override {
    EXPECT_TRUE(false);
  }
  void Sync(SyncCompleter::Sync _completer) override { EXPECT_TRUE(false); }
  void SetConstraints(bool has_constraints, sysmem::BufferCollectionConstraints constraints,
                      SetConstraintsCompleter::Sync _completer) override {
    auto& image_constraints = constraints.image_format_constraints[0];
    EXPECT_EQ(sysmem::PixelFormatType::BGRA32, image_constraints.pixel_format.type);
    EXPECT_EQ(4u, image_constraints.bytes_per_row_divisor);
  }
  void WaitForBuffersAllocated(WaitForBuffersAllocatedCompleter::Sync _completer) override {
    sysmem::BufferCollectionInfo_2 info;
    info.settings.has_image_format_constraints = true;
    info.buffer_count = 1;
    ASSERT_OK(zx::vmo::create(4096, 0, &info.buffers[0].vmo));
    sysmem::ImageFormatConstraints& constraints = info.settings.image_format_constraints;
    constraints.pixel_format.type = sysmem::PixelFormatType::BGRA32;
    constraints.pixel_format.has_format_modifier = true;
    constraints.pixel_format.format_modifier.value = sysmem::FORMAT_MODIFIER_LINEAR;
    constraints.max_coded_width = 1000;
    constraints.max_bytes_per_row = 4000;
    constraints.bytes_per_row_divisor = 1;
    _completer.Reply(ZX_OK, std::move(info));
  }
  void CheckBuffersAllocated(CheckBuffersAllocatedCompleter::Sync _completer) override {
    EXPECT_TRUE(false);
  }

  void CloseSingleBuffer(uint64_t buffer_index,
                         CloseSingleBufferCompleter::Sync _completer) override {
    EXPECT_TRUE(false);
  }
  void AllocateSingleBuffer(uint64_t buffer_index,
                            AllocateSingleBufferCompleter::Sync _completer) override {
    EXPECT_TRUE(false);
  }
  void WaitForSingleBufferAllocated(
      uint64_t buffer_index, WaitForSingleBufferAllocatedCompleter::Sync _completer) override {
    EXPECT_TRUE(false);
  }
  void CheckSingleBufferAllocated(uint64_t buffer_index,
                                  CheckSingleBufferAllocatedCompleter::Sync _completer) override {
    EXPECT_TRUE(false);
  }
  void Close(CloseCompleter::Sync _completer) override { EXPECT_TRUE(false); }
};

class FakeGpuBackend : public virtio::FakeBackend {
 public:
  FakeGpuBackend() : FakeBackend({{0, 1024}}) {}
};

class VirtioGpuTest : public zxtest::Test {
 public:
  VirtioGpuTest() : loop_(&kAsyncLoopConfigAttachToCurrentThread) {}
  void SetUp() override {
    zx::bti bti;
    fake_bti_create(bti.reset_and_get_address());
    device_ = std::make_unique<virtio::GpuDevice>(nullptr, std::move(bti),
                                                  std::make_unique<FakeGpuBackend>());

    zx::channel server_channel;
    ASSERT_OK(zx::channel::create(0u, &server_channel, &client_channel_));

    ASSERT_OK(fidl::Bind(loop_.dispatcher(), std::move(server_channel), &collection_));

    loop_.StartThread();
  }
  void TearDown() override {
    // Ensure the loop processes all queued FIDL messages.
    loop_.Quit();
    loop_.JoinThreads();
    loop_.ResetQuit();
    loop_.RunUntilIdle();
  }

 protected:
  std::unique_ptr<virtio::GpuDevice> device_;
  StubBufferCollection collection_;
  async::Loop loop_;
  zx::channel client_channel_;
};

TEST_F(VirtioGpuTest, ImportVmo) {
  image_t image = {};
  image.pixel_format = ZX_PIXEL_FORMAT_RGB_x888;
  image.width = 4;
  image.height = 4;

  zx::vmo vmo;
  size_t offset;
  uint32_t pixel_size;
  uint32_t row_bytes;
  EXPECT_OK(device_->GetVmoAndStride(&image, client_channel_.get(), 0, &vmo, &offset, &pixel_size,
                                     &row_bytes));
  EXPECT_EQ(4, pixel_size);
  EXPECT_EQ(16, row_bytes);
}

TEST_F(VirtioGpuTest, SetConstraints) {
  image_t image = {};
  image.pixel_format = ZX_PIXEL_FORMAT_RGB_x888;
  image.width = 4;
  image.height = 4;
  display_controller_impl_protocol_t proto;
  EXPECT_OK(device_->DdkGetProtocol(ZX_PROTOCOL_DISPLAY_CONTROLLER_IMPL,
                                    reinterpret_cast<void*>(&proto)));
  EXPECT_OK(
      proto.ops->set_buffer_collection_constraints(device_.get(), &image, client_channel_.get()));
}

}  // namespace
