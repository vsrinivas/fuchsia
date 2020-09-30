// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vim-display.h"

#include <fuchsia/sysmem/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/mock-sysmem/mock-buffer-collection.h>

#include <ddk/protocol/display/controller.h>
#include <zxtest/zxtest.h>

namespace sysmem = llcpp::fuchsia::sysmem;

namespace {
// Use a stub buffer collection instead of the real sysmem since some tests may
// require things (like protected memory) that aren't available on the current
// system.
class MockBufferCollection : public mock_sysmem::MockBufferCollection {
 public:
  void SetConstraints(bool has_constraints, sysmem::BufferCollectionConstraints constraints,
                      SetConstraintsCompleter::Sync& _completer) override {
    EXPECT_FALSE(constraints.buffer_memory_constraints.inaccessible_domain_supported);
    EXPECT_FALSE(constraints.buffer_memory_constraints.cpu_domain_supported);
    set_constraints_called_ = true;
  }
  void WaitForBuffersAllocated(WaitForBuffersAllocatedCompleter::Sync& _completer) override {
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

  bool set_constraints_called() const { return set_constraints_called_; }

 private:
  bool set_constraints_called_ = false;
};

static zx_status_t stub_canvas_config(void* ctx, zx_handle_t vmo, size_t offset,
                                      const canvas_info_t* info, uint8_t* out_canvas_idx) {
  *out_canvas_idx = 1;
  return ZX_OK;
}

static zx_status_t stub_canvas_free(void* ctx, uint8_t canvas_idx) { return ZX_OK; }

static amlogic_canvas_protocol_ops_t canvas_proto_ops = {
    .config = stub_canvas_config,
    .free = stub_canvas_free,
};

TEST(VimDisplay, ImportVmo) {
  vim2_display display;
  display.canvas.ops = &canvas_proto_ops;
  list_initialize(&display.imported_images);
  mtx_init(&display.display_lock, mtx_plain);
  mtx_init(&display.image_lock, mtx_plain);
  mtx_init(&display.i2c_lock, mtx_plain);

  display_controller_impl_protocol_t protocol;
  ASSERT_OK(display_get_protocol(&display, ZX_PROTOCOL_DISPLAY_CONTROLLER_IMPL, &protocol));
  zx::channel server_channel, client_channel;
  ASSERT_OK(zx::channel::create(0u, &server_channel, &client_channel));

  MockBufferCollection collection;
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  image_t image = {};
  image.pixel_format = ZX_PIXEL_FORMAT_RGB_x888;
  image.width = 4;
  image.height = 4;
  ASSERT_OK(
      fidl::BindSingleInFlightOnly(loop.dispatcher(), std::move(server_channel), &collection));

  loop.StartThread();

  EXPECT_OK(
      protocol.ops->set_buffer_collection_constraints(protocol.ctx, &image, client_channel.get()));
  EXPECT_OK(protocol.ops->import_image(protocol.ctx, &image, client_channel.get(), 0));
  protocol.ops->release_image(protocol.ctx, &image);

  EXPECT_TRUE(collection.set_constraints_called());
}

}  // namespace
