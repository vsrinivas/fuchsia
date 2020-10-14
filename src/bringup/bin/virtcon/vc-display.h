// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_BRINGUP_BIN_VIRTCON_VC_DISPLAY_H_
#define SRC_BRINGUP_BIN_VIRTCON_VC_DISPLAY_H_

#include "fuchsia/hardware/display/llcpp/fidl.h"
#include "fuchsia/sysmem/llcpp/fidl.h"
#include "src/lib/listnode/listnode.h"
#include "vc.h"
#include "zircon/types.h"

typedef struct display_info {
  uint64_t id;
  uint32_t width;
  uint32_t height;
  uint32_t stride;
  zx_pixel_format_t format;

  uint64_t image_id;
  uint64_t layer_id;

  // 0 means no collection.
  uint64_t buffer_collection_id;

  bool bound;

  // Only valid when |bound| is true.
  zx_handle_t image_vmo;
  llcpp::fuchsia::hardware::display::ImageConfig image_config;

  vc_gfx_t* graphics;

  struct list_node node;
  // If the display is not a main display, then this is the log vc for the
  // display.
  vc_t* log_vc;
} display_info_t;

void handle_display_removed(uint64_t id);

zx_status_t rebind_display(bool use_all);

zx_status_t create_layer(uint64_t display_id, uint64_t* layer_id);
void destroy_layer(uint64_t layer_id);
void release_image(uint64_t image_id);
zx_status_t set_display_layer(uint64_t display_id, uint64_t layer_id);
zx_status_t configure_layer(display_info_t* display, uint64_t layer_id, uint64_t image_id,
                            llcpp::fuchsia::hardware::display::ImageConfig* config);
zx_status_t alloc_display_info_vmo(display_info_t* display);
zx_status_t apply_configuration();
zx_status_t import_vmo(zx_handle_t vmo, llcpp::fuchsia::hardware::display::ImageConfig* config,
                       uint64_t* id);
zx_status_t dc_callback_handler(zx_signals_t signals);
#if BUILD_FOR_DISPLAY_TEST

struct list_node* get_display_list();
void initialize_display_channel(zx::channel channel);
llcpp::fuchsia::sysmem::Allocator::SyncClient* get_sysmem_allocator();

#endif

#endif  // SRC_BRINGUP_BIN_VIRTCON_VC_DISPLAY_H_
