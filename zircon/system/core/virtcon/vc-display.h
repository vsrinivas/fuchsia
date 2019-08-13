// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_CORE_VIRTCON_VC_DISPLAY_H_
#define ZIRCON_SYSTEM_CORE_VIRTCON_VC_DISPLAY_H_

#include <zircon/listnode.h>

#include "fuchsia/hardware/display/c/fidl.h"
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

  bool bound;

  // Only valid when |bound| is true.
  zx_handle_t image_vmo;
  fuchsia_hardware_display_ImageConfig image_config;

  vc_gfx_t* graphics;

  struct list_node node;
  // If the display is not a main display, then this is the log vc for the
  // display.
  vc_t* log_vc;
} display_info_t;

void handle_display_removed(uint64_t id);

zx_status_t rebind_display(bool use_all);

zx_status_t handle_display_added(fuchsia_hardware_display_Info* info,
                                 fuchsia_hardware_display_Mode* mode, int32_t pixel_format);

zx_status_t create_layer(uint64_t display_id, uint64_t* layer_id);
void destroy_layer(uint64_t layer_id);
void release_image(uint64_t image_id);
zx_status_t set_display_layer(uint64_t display_id, uint64_t layer_id);
zx_status_t configure_layer(display_info_t* display, uint64_t layer_id, uint64_t image_id,
                            fuchsia_hardware_display_ImageConfig* config);
zx_status_t alloc_display_info_vmo(display_info_t* display);
zx_status_t apply_configuration();
zx_status_t import_vmo(zx_handle_t vmo, fuchsia_hardware_display_ImageConfig* config, uint64_t* id);

#if BUILD_FOR_DISPLAY_TEST

bool is_primary_bound();
struct list_node* get_display_list();

#endif

#endif  // ZIRCON_SYSTEM_CORE_VIRTCON_VC_DISPLAY_H_
