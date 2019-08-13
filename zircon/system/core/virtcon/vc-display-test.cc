// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vc-display.h"

#include <fcntl.h>
#include <fuchsia/io/c/fidl.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/io.h>
#include <lib/fidl/coding.h>
#include <lib/fzl/fdio.h>
#include <lib/zx/channel.h>
#include <lib/zx/vmo.h>
#include <string.h>
#include <zircon/assert.h>
#include <zircon/device/display-controller.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

#include <list>

#include <fbl/unique_fd.h>
#include <port/port.h>
#include <zxtest/zxtest.h>

#include "fuchsia/hardware/display/c/fidl.h"
#include "vc.h"

zx_status_t log_create_vc(vc_gfx_t* graphics, vc_t** vc_out) { return ZX_OK; }

void log_delete_vc(vc_t* vc) {}

void set_log_listener_active(bool active) {}

void vc_attach_gfx(vc_t* vc) {}

// We want to make sure that we destroy every layer we create.
std::list<uint64_t> layers;
uint64_t next_layer = 1;
zx_status_t create_layer(uint64_t display_id, uint64_t* layer_id) {
  layers.push_back(next_layer);
  *layer_id = next_layer++;
  return ZX_OK;
}

void destroy_layer(uint64_t layer_id) {
  layers.remove_if([layer_id](uint64_t layer) { return (layer == layer_id); });
}

// We want to make sure we release every image we create.
std::list<uint64_t> images;
uint64_t next_image = 1;
zx_status_t import_vmo(zx_handle_t vmo, fuchsia_hardware_display_ImageConfig* config,
                       uint64_t* id) {
  images.push_back(next_image);
  *id = next_image++;
  return ZX_OK;
}

void release_image(uint64_t image_id) {
  images.remove_if([image_id](uint64_t image) { return (image == image_id); });
}

zx_status_t set_display_layer(uint64_t display_id, uint64_t layer_id) { return ZX_OK; }

zx_status_t configure_layer(display_info_t* display, uint64_t layer_id, uint64_t image_id,
                            fuchsia_hardware_display_ImageConfig* config) {
  return ZX_OK;
}

zx_status_t alloc_display_info_vmo(display_info_t* display) { return ZX_OK; }

zx_status_t apply_configuration() { return ZX_OK; }

zx_status_t vc_init_gfx(vc_gfx_t* gfx, zx_handle_t fb_vmo, int32_t width, int32_t height,
                        zx_pixel_format_t format, int32_t stride) {
  return ZX_OK;
}

void vc_change_graphics(vc_gfx_t* graphics) {}

class VcDisplayTest : public zxtest::Test {
  void SetUp() override {
    layers.clear();
    next_layer = 1;

    images.clear();
    next_image = 1;
  }

  void TearDown() override {
    ASSERT_EQ(layers.size(), 0);
    ASSERT_EQ(images.size(), 0);
  }
};

TEST_F(VcDisplayTest, EmptyRebind) { ASSERT_EQ(rebind_display(true), ZX_ERR_NO_RESOURCES); }

TEST_F(VcDisplayTest, OneDisplay) {
  fuchsia_hardware_display_Info info = {};
  info.id = 1;
  int32_t format = 0x0;
  info.pixel_format.data = &format;

  fuchsia_hardware_display_Mode mode = {};
  ASSERT_EQ(handle_display_added(&info, &mode, 0), ZX_OK);
  ASSERT_EQ(rebind_display(true), ZX_OK);
  ASSERT_TRUE(is_primary_bound());
  display_info_t* primary = list_peek_head_type(get_display_list(), display_info_t, node);
  ASSERT_TRUE(primary->bound);

  handle_display_removed(1);
  ASSERT_FALSE(is_primary_bound());
}

TEST_F(VcDisplayTest, TwoDisplays) {
  fuchsia_hardware_display_Info hardware_display = {};
  hardware_display.id = 1;
  int32_t format = 0x0;
  hardware_display.pixel_format.data = &format;

  fuchsia_hardware_display_Mode mode = {};

  // Add the first display.
  ASSERT_EQ(handle_display_added(&hardware_display, &mode, 0), ZX_OK);
  ASSERT_EQ(rebind_display(true), ZX_OK);
  ASSERT_TRUE(is_primary_bound());

  display_info_t* primary = list_peek_head_type(get_display_list(), display_info_t, node);
  ASSERT_TRUE(primary->bound);

  // Add the second display.
  hardware_display.id = 2;
  ASSERT_EQ(handle_display_added(&hardware_display, &mode, 0), ZX_OK);
  ASSERT_EQ(rebind_display(true), ZX_OK);
  ASSERT_TRUE(is_primary_bound());

  // Check that all of the displays were bound.
  display_info_t* info;
  int num_displays = 0;
  list_for_every_entry (get_display_list(), info, display_info_t, node) {
    ASSERT_TRUE(info->bound);
    num_displays++;
  }
  ASSERT_EQ(num_displays, 2);

  // Remove the second display.
  handle_display_removed(2);
  ASSERT_TRUE(is_primary_bound());

  // Remove the first display.
  handle_display_removed(1);
  ASSERT_FALSE(is_primary_bound());
}

// This test checks that the primary display switches over correctly.
// It allocates display 1 and then display 2, then removes display 1.
// Display 2 should switch over to the primary display.
TEST_F(VcDisplayTest, ChangePrimaryDisplay) {
  fuchsia_hardware_display_Info hardware_display = {};
  hardware_display.id = 1;
  int32_t format = 0x0;
  hardware_display.pixel_format.data = &format;

  fuchsia_hardware_display_Mode mode = {};

  // Add the first display.
  ASSERT_EQ(handle_display_added(&hardware_display, &mode, 0), ZX_OK);
  ASSERT_EQ(rebind_display(true), ZX_OK);
  ASSERT_TRUE(is_primary_bound());

  display_info_t* primary = list_peek_head_type(get_display_list(), display_info_t, node);
  ASSERT_TRUE(primary->bound);

  // Add the second display.
  hardware_display.id = 2;
  ASSERT_EQ(handle_display_added(&hardware_display, &mode, 0), ZX_OK);
  ASSERT_EQ(rebind_display(true), ZX_OK);
  ASSERT_TRUE(is_primary_bound());

  // Check that all of the displays were bound.
  display_info_t* info;
  int num_displays = 0;
  list_for_every_entry (get_display_list(), info, display_info_t, node) {
    ASSERT_TRUE(info->bound);
    num_displays++;
  }
  ASSERT_EQ(num_displays, 2);

  // Remove the first display.
  handle_display_removed(1);
  ASSERT_FALSE(is_primary_bound());
  rebind_display(true);
  ASSERT_TRUE(is_primary_bound());

  // Remove the second display.
  handle_display_removed(2);
  ASSERT_FALSE(is_primary_bound());
}
