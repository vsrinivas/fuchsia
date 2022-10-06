// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/graphics/display/drivers/amlogic-display/amlogic-display.h"

#include <fidl/fuchsia.sysmem/cpp/wire_test_base.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/inspect/cpp/inspect.h>

#include "src/graphics/display/drivers/amlogic-display/osd.h"
#include "zxtest/zxtest.h"

namespace sysmem = fuchsia_sysmem;

class MockBufferCollection : public fidl::testing::WireTestBase<fuchsia_sysmem::BufferCollection> {
 public:
  MockBufferCollection(const std::vector<sysmem::wire::PixelFormatType>& pixel_format_types =
                           {sysmem::wire::PixelFormatType::kBgra32,
                            sysmem::wire::PixelFormatType::kR8G8B8A8})
      : supported_pixel_format_types_(pixel_format_types) {}

  void SetConstraints(SetConstraintsRequestView request,
                      SetConstraintsCompleter::Sync& _completer) override {
    EXPECT_TRUE(request->constraints.buffer_memory_constraints.inaccessible_domain_supported);
    EXPECT_FALSE(request->constraints.buffer_memory_constraints.cpu_domain_supported);
    EXPECT_EQ(64u, request->constraints.image_format_constraints[0].bytes_per_row_divisor);

    bool has_rgba =
        std::find(supported_pixel_format_types_.begin(), supported_pixel_format_types_.end(),
                  fuchsia_sysmem::wire::PixelFormatType::kR8G8B8A8) !=
        supported_pixel_format_types_.end();
    bool has_bgra =
        std::find(supported_pixel_format_types_.begin(), supported_pixel_format_types_.end(),
                  fuchsia_sysmem::wire::PixelFormatType::kBgra32) !=
        supported_pixel_format_types_.end();

    size_t expected_format_constraints_count = 0u;
    const auto& image_format_constraints = request->constraints.image_format_constraints;
    if (has_bgra) {
      expected_format_constraints_count += 2;
      EXPECT_TRUE(std::find_if(image_format_constraints.begin(),
                               image_format_constraints.begin() +
                                   request->constraints.image_format_constraints_count,
                               [](const auto& format) {
                                 return format.pixel_format.format_modifier.value ==
                                        sysmem::wire::kFormatModifierArmLinearTe;
                               }) != image_format_constraints.end());
    }
    if (has_rgba) {
      expected_format_constraints_count += 2;
    }

    EXPECT_EQ(expected_format_constraints_count,
              request->constraints.image_format_constraints_count);
    set_constraints_called_ = true;
  }

  void SetName(SetNameRequestView request, SetNameCompleter::Sync& completer) override {
    EXPECT_EQ(10u, request->priority);
    EXPECT_EQ(std::string("Display"), std::string(request->name.data(), request->name.size()));
    set_name_called_ = true;
  }

  void WaitForBuffersAllocated(WaitForBuffersAllocatedCompleter::Sync& completer) override {
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
  std::vector<sysmem::wire::PixelFormatType> supported_pixel_format_types_;
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
  display.SetFormatSupportCheck([](auto) { return true; });
  zx::channel server_channel, client_channel;
  ASSERT_OK(zx::channel::create(0u, &server_channel, &client_channel));

  MockBufferCollection collection(
      {sysmem::wire::PixelFormatType::kBgra32, sysmem::wire::PixelFormatType::kR8G8B8A8});
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

TEST(AmlogicDisplay, SysmemRequirements_BgraOnly) {
  amlogic_display::AmlogicDisplay display(nullptr);
  display.SetFormatSupportCheck([](zx_pixel_format_t format) {
    return format == ZX_PIXEL_FORMAT_RGB_x888 || format == ZX_PIXEL_FORMAT_ARGB_8888;
  });
  zx::channel server_channel, client_channel;
  ASSERT_OK(zx::channel::create(0u, &server_channel, &client_channel));

  MockBufferCollection collection({sysmem::wire::PixelFormatType::kBgra32});
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
  display.SetFormatSupportCheck([](auto) { return true; });
  zx::channel server_channel, client_channel;
  ASSERT_OK(zx::channel::create(0u, &server_channel, &client_channel));

  MockBufferCollection collection(
      {sysmem::wire::PixelFormatType::kBgra32, sysmem::wire::PixelFormatType::kR8G8B8A8});
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
