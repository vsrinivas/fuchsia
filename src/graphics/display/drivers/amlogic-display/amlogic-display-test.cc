// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "amlogic-display.h"

#include <fidl/fuchsia.sysmem/cpp/wire_test_base.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/inspect/cpp/inspect.h>

#include "osd.h"
#include "zxtest/zxtest.h"

namespace sysmem = fuchsia_sysmem;

class MockBufferCollection : public fuchsia_sysmem::testing::BufferCollection_TestBase {
 public:
  void SetConstraints(SetConstraintsRequestView request,
                      SetConstraintsCompleter::Sync& _completer) override {
    EXPECT_TRUE(request->constraints.buffer_memory_constraints.inaccessible_domain_supported);
    EXPECT_FALSE(request->constraints.buffer_memory_constraints.cpu_domain_supported);
    EXPECT_EQ(64u, request->constraints.image_format_constraints[0].bytes_per_row_divisor);
    EXPECT_EQ(4u, request->constraints.image_format_constraints_count);
    EXPECT_EQ(sysmem::wire::kFormatModifierArmLinearTe,
              request->constraints.image_format_constraints[1].pixel_format.format_modifier.value);
    set_constraints_called_ = true;
  }

  void SetName(SetNameRequestView request, SetNameCompleter::Sync& completer) override {
    EXPECT_EQ(10u, request->priority);
    EXPECT_EQ(std::string("Display"), std::string(request->name.data(), request->name.size()));
    set_name_called_ = true;
  }

  void WaitForBuffersAllocated(WaitForBuffersAllocatedRequestView request,
                               WaitForBuffersAllocatedCompleter::Sync& completer) override {
    sysmem::wire::BufferCollectionInfo2 collection;
    collection.buffer_count = 1;
    collection.settings.has_image_format_constraints = true;
    auto& image_constraints = collection.settings.image_format_constraints;
    image_constraints.min_bytes_per_row = 4;
    image_constraints.min_coded_height = 4;
    image_constraints.pixel_format.type = sysmem::wire::PixelFormatType::kBgr24;
    EXPECT_EQ(ZX_OK, zx::vmo::create(ZX_PAGE_SIZE, 0u, &collection.buffers[0].vmo));
    completer.Reply(ZX_OK, std::move(collection));
  }

  void NotImplemented_(const std::string& name, fidl::CompleterBase& completer) override {
    EXPECT_TRUE(false);
  }

  bool set_constraints_called() const { return set_constraints_called_; }
  bool set_name_called() const { return set_name_called_; }

 private:
  bool set_constraints_called_ = false;
  bool set_name_called_ = false;
};

class FakeCanvasProtocol : ddk::AmlogicCanvasProtocol<FakeCanvasProtocol> {
 public:
  zx_status_t AmlogicCanvasConfig(zx::vmo vmo, size_t offset, const canvas_info_t* info,
                                  uint8_t* canvas_idx) {
    for (uint32_t i = 0; i < std::size(in_use_); i++) {
      if (!in_use_[i]) {
        in_use_[i] = true;
        *canvas_idx = i;
        return ZX_OK;
      }
    }
    return ZX_ERR_NO_MEMORY;
  }
  zx_status_t AmlogicCanvasFree(uint8_t canvas_idx) {
    EXPECT_TRUE(in_use_[canvas_idx]);
    in_use_[canvas_idx] = false;
    return ZX_OK;
  }

  void CheckThatNoEntriesInUse() {
    for (uint32_t i = 0; i < std::size(in_use_); i++) {
      EXPECT_FALSE(in_use_[i]);
    }
  }

  const amlogic_canvas_protocol_t& get_protocol() { return protocol_; }

 private:
  static constexpr uint32_t kCanvasEntries = 256;
  bool in_use_[kCanvasEntries] = {};
  amlogic_canvas_protocol_t protocol_ = {.ops = &amlogic_canvas_protocol_ops_, .ctx = this};
};

TEST(AmlogicDisplay, SysmemRequirements) {
  amlogic_display::AmlogicDisplay display(nullptr);
  zx::channel server_channel, client_channel;
  ASSERT_OK(zx::channel::create(0u, &server_channel, &client_channel));

  MockBufferCollection collection;
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  image_t image = {};
  ASSERT_OK(
      fidl::BindSingleInFlightOnly(loop.dispatcher(), std::move(server_channel), &collection));

  EXPECT_OK(
      display.DisplayControllerImplSetBufferCollectionConstraints(&image, client_channel.get()));

  loop.RunUntilIdle();
  EXPECT_TRUE(collection.set_constraints_called());
  EXPECT_TRUE(collection.set_name_called());
}

TEST(AmlogicDisplay, FloatToFix3_10) {
  inspect::Inspector inspector;
  amlogic_display::Osd osd = amlogic_display::Osd(true, 100, 100, 100, 100, &inspector.GetRoot());
  EXPECT_EQ(0x0000, osd.FloatToFixed3_10(0.0f));
  EXPECT_EQ(0x0066, osd.FloatToFixed3_10(0.1f));
  EXPECT_EQ(0x1f9a, osd.FloatToFixed3_10(-0.1f));
  // Test for maximum positive (<4)
  EXPECT_EQ(0x0FFF, osd.FloatToFixed3_10(4.0f));
  EXPECT_EQ(0x0FFF, osd.FloatToFixed3_10(40.0f));
  EXPECT_EQ(0x0FFF, osd.FloatToFixed3_10(3.9999f));
  // Test for minimum negative (>= -4)
  EXPECT_EQ(0x1000, osd.FloatToFixed3_10(-4.0f));
  EXPECT_EQ(0x1000, osd.FloatToFixed3_10(-14.0f));
}

TEST(AmlogicDisplay, FloatToFixed2_10) {
  inspect::Inspector inspector;
  amlogic_display::Osd osd = amlogic_display::Osd(true, 100, 100, 100, 100, &inspector.GetRoot());
  EXPECT_EQ(0x0000, osd.FloatToFixed2_10(0.0f));
  EXPECT_EQ(0x0066, osd.FloatToFixed2_10(0.1f));
  EXPECT_EQ(0x0f9a, osd.FloatToFixed2_10(-0.1f));
  // Test for maximum positive (<2)
  EXPECT_EQ(0x07FF, osd.FloatToFixed2_10(2.0f));
  EXPECT_EQ(0x07FF, osd.FloatToFixed2_10(20.0f));
  EXPECT_EQ(0x07FF, osd.FloatToFixed2_10(1.9999f));
  // Test for minimum negative (>= -2)
  EXPECT_EQ(0x0800, osd.FloatToFixed2_10(-2.0f));
  EXPECT_EQ(0x0800, osd.FloatToFixed2_10(-14.0f));
}

TEST(AmlogicDisplay, NoLeakCaptureCanvas) {
  amlogic_display::AmlogicDisplay display(nullptr);
  zx::channel server_channel, client_channel;
  ASSERT_OK(zx::channel::create(0u, &server_channel, &client_channel));

  MockBufferCollection collection;
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  ASSERT_OK(
      fidl::BindSingleInFlightOnly(loop.dispatcher(), std::move(server_channel), &collection));
  loop.StartThread("sysmem-thread");
  FakeCanvasProtocol canvas;
  display.SetCanvasForTesting(ddk::AmlogicCanvasProtocolClient(&canvas.get_protocol()));

  uint64_t capture_handle;
  EXPECT_OK(
      display.DisplayCaptureImplImportImageForCapture(client_channel.get(), 0, &capture_handle));
  EXPECT_OK(display.DisplayCaptureImplReleaseCapture(capture_handle));

  canvas.CheckThatNoEntriesInUse();
}
