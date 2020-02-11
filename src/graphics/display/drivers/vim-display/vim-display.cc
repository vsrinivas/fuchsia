// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vim-display.h"

#include <assert.h>
#include <fuchsia/sysmem/c/fidl.h>
#include <lib/device-protocol/platform-device.h>
#include <lib/image-format/image_format.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zircon/assert.h>
#include <zircon/pixelformat.h>
#include <zircon/syscalls.h>

#include <memory>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/io-buffer.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/composite.h>
#include <ddk/protocol/display/controller.h>
#include <ddk/protocol/i2cimpl.h>
#include <ddk/protocol/platform/device.h>
#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>
#include <hw/arch_ops.h>
#include <hw/reg.h>

#include "hdmitx.h"

enum {
  COMPONENT_PDEV,
  COMPONENT_HPD_GPIO,
  COMPONENT_CANVAS,
  COMPONENT_SYSMEM,
  COMPONENT_COUNT,
};

/* Default formats */
static const uint8_t _ginput_color_format = HDMI_COLOR_FORMAT_444;
static const uint8_t _gcolor_depth = HDMI_COLOR_DEPTH_24B;

static zx_pixel_format_t _gsupported_pixel_formats[] = {ZX_PIXEL_FORMAT_RGB_x888,
                                                        ZX_PIXEL_FORMAT_NV12};

typedef struct image_info {
  zx_handle_t pmt;
  zx_pixel_format_t format;
  uint8_t canvas_idx[2];

  list_node_t node;
} image_info_t;

void populate_added_display_args(vim2_display_t* display, added_display_args_t* args) {
  args->display_id = display->display_id;
  args->edid_present = true;
  args->panel.i2c_bus_id = 0;
  args->pixel_format_list = _gsupported_pixel_formats;
  args->pixel_format_count = countof(_gsupported_pixel_formats);
  args->cursor_info_count = 0;
}

static void vim_set_display_controller_interface(
    void* ctx, const display_controller_interface_protocol_t* intf) {
  vim2_display_t* display = static_cast<vim2_display_t*>(ctx);
  mtx_lock(&display->display_lock);

  display->dc_intf = *intf;

  if (display->display_attached) {
    added_display_args_t args;
    added_display_info_t info;
    populate_added_display_args(display, &args);
    display_controller_interface_on_displays_changed(&display->dc_intf, &args, 1, NULL, 0, &info, 1,
                                                     NULL);

    if (info.is_standard_srgb_out) {
      display->output_color_format = HDMI_COLOR_FORMAT_RGB;
    } else {
      display->output_color_format = HDMI_COLOR_FORMAT_444;
    }
  }
  mtx_unlock(&display->display_lock);
}

static zx_status_t vim_import_vmo_image(void* ctx, image_t* image, zx_handle_t vmo_in,
                                        size_t offset) {
  zx::vmo vmo(vmo_in);
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t vim_import_image(void* ctx, image_t* image, zx_unowned_handle_t handle,
                             uint32_t index) {
  zx_status_t status = ZX_OK;

  if (image->type != IMAGE_TYPE_SIMPLE) {
    return ZX_ERR_INVALID_ARGS;
  }

  zx_status_t status2;
  fuchsia_sysmem_BufferCollectionInfo_2 collection_info;
  status =
      fuchsia_sysmem_BufferCollectionWaitForBuffersAllocated(handle, &status2, &collection_info);
  if (status != ZX_OK) {
    return status;
  }
  if (status2 != ZX_OK) {
    return status2;
  }

  fbl::Vector<zx::vmo> vmos;
  for (uint32_t i = 0; i < collection_info.buffer_count; ++i) {
    vmos.push_back(zx::vmo(collection_info.buffers[i].vmo));
  }

  if (!collection_info.settings.has_image_format_constraints || index >= vmos.size()) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  uint64_t offset = collection_info.buffers[index].vmo_usable_start;

  zx::vmo dup_vmo;
  status = vmos[index].duplicate(ZX_RIGHT_SAME_RIGHTS, &dup_vmo);
  if (status != ZX_OK) {
    DISP_ERROR("Failed to duplicate vmo: %d\n", status);
    return ZX_ERR_INVALID_ARGS;
  }

  fbl::AllocChecker ac;
  std::unique_ptr<image_info_t> import_info = fbl::make_unique_checked<image_info_t>(&ac);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  vim2_display_t* display = static_cast<vim2_display_t*>(ctx);
  fbl::AutoLock lock(&display->image_lock);

  if (image->type != IMAGE_TYPE_SIMPLE)
    return ZX_ERR_INVALID_ARGS;

  import_info->format = image->pixel_format;

  if (image->pixel_format == ZX_PIXEL_FORMAT_RGB_x888) {
    if (collection_info.settings.image_format_constraints.pixel_format.type !=
        fuchsia_sysmem_PixelFormatType_BGRA32) {
      return ZX_ERR_INVALID_ARGS;
    }
    ZX_DEBUG_ASSERT(
        collection_info.settings.image_format_constraints.pixel_format.has_format_modifier);
    ZX_DEBUG_ASSERT(
        collection_info.settings.image_format_constraints.pixel_format.format_modifier.value ==
        fuchsia_sysmem_FORMAT_MODIFIER_LINEAR);

    uint32_t minimum_row_bytes;
    if (!ImageFormatMinimumRowBytes(&collection_info.settings.image_format_constraints,
                                    image->width, &minimum_row_bytes)) {
      DISP_ERROR("Invalid image width %d for collection\n", image->width);
      return ZX_ERR_INVALID_ARGS;
    }
    zx_status_t status = ZX_OK;
    canvas_info_t info;
    info.height = image->height;
    info.stride_bytes = minimum_row_bytes;
    info.wrap = 0;
    info.blkmode = 0;
    info.endianness = 0;
    info.flags = CANVAS_FLAGS_READ;

    status = amlogic_canvas_config(&display->canvas, dup_vmo.release(), offset, &info,
                                   &import_info->canvas_idx[0]);
    if (status != ZX_OK) {
      return ZX_ERR_NO_RESOURCES;
    }
    image->handle = import_info->canvas_idx[0];
  } else if (image->pixel_format == ZX_PIXEL_FORMAT_NV12) {
    if (image->height % 2 != 0) {
      return ZX_ERR_INVALID_ARGS;
    }
    if (collection_info.settings.image_format_constraints.pixel_format.type !=
        fuchsia_sysmem_PixelFormatType_NV12) {
      return ZX_ERR_INVALID_ARGS;
    }
    ZX_DEBUG_ASSERT(
        collection_info.settings.image_format_constraints.pixel_format.has_format_modifier);
    ZX_DEBUG_ASSERT(
        collection_info.settings.image_format_constraints.pixel_format.format_modifier.value ==
        fuchsia_sysmem_FORMAT_MODIFIER_LINEAR);

    uint32_t minimum_row_bytes;
    if (!ImageFormatMinimumRowBytes(&collection_info.settings.image_format_constraints,
                                    image->width, &minimum_row_bytes)) {
      DISP_ERROR("Invalid image width %d for collection\n", image->width);
      return ZX_ERR_INVALID_ARGS;
    }
    zx_status_t status = ZX_OK;
    canvas_info_t info;
    info.height = image->height;
    info.stride_bytes = minimum_row_bytes;
    info.wrap = 0;
    info.blkmode = 0;
    // Do 64-bit endianness conversion.
    info.endianness = 7;
    info.flags = CANVAS_FLAGS_READ;

    zx::vmo dup_vmo2;
    status = dup_vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup_vmo2);
    if (status != ZX_OK) {
      return status;
    }

    status = amlogic_canvas_config(&display->canvas, dup_vmo.release(), offset, &info,
                                   &import_info->canvas_idx[0]);
    if (status != ZX_OK) {
      return ZX_ERR_NO_RESOURCES;
    }

    info.height /= 2;
    uint32_t plane_offset = minimum_row_bytes * image->height;

    status = amlogic_canvas_config(&display->canvas, dup_vmo2.release(), offset + plane_offset,
                                   &info, &import_info->canvas_idx[1]);
    if (status != ZX_OK) {
      amlogic_canvas_free(&display->canvas, import_info->canvas_idx[0]);
      return ZX_ERR_NO_RESOURCES;
    }
    // The handle used by hardware is VVUUYY, so the UV plane is included twice.
    image->handle = (((uint64_t)import_info->canvas_idx[1] << 16) |
                     (import_info->canvas_idx[1] << 8) | import_info->canvas_idx[0]);
  } else {
    return ZX_ERR_INVALID_ARGS;
  }

  list_add_head(&display->imported_images, &import_info.release()->node);

  return ZX_OK;
}

static void vim_release_image(void* ctx, image_t* image) {
  vim2_display_t* display = static_cast<vim2_display_t*>(ctx);
  mtx_lock(&display->image_lock);

  image_info_t* info;
  uint32_t canvas_idx0 = (uint64_t)image->handle & 0xff;
  uint32_t canvas_idx1 = ((uint64_t)image->handle >> 8) & 0xff;
  list_for_every_entry (&display->imported_images, info, image_info_t, node) {
    if (info->canvas_idx[0] == canvas_idx0 && info->canvas_idx[1] == canvas_idx1) {
      list_delete(&info->node);
      break;
    }
  }

  mtx_unlock(&display->image_lock);

  if (info) {
    amlogic_canvas_free(&display->canvas, info->canvas_idx[0]);
    if (info->format == ZX_PIXEL_FORMAT_NV12)
      amlogic_canvas_free(&display->canvas, info->canvas_idx[1]);
    free(info);
  }
}

static uint32_t vim_check_configuration(void* ctx, const display_config_t** display_configs,
                                        size_t display_count, uint32_t** layer_cfg_results,
                                        size_t* layer_cfg_result_count) {
  if (display_count != 1) {
    if (display_count > 1) {
      // The core display driver should never see a configuration with more
      // than 1 display, so this is a bug in the core driver.
      ZX_DEBUG_ASSERT(false);
      return CONFIG_DISPLAY_TOO_MANY;
    }
    return CONFIG_DISPLAY_OK;
  }
  vim2_display_t* display = static_cast<vim2_display_t*>(ctx);
  mtx_lock(&display->display_lock);

  // no-op, just wait for the client to try a new config
  if (!display->display_attached || display_configs[0]->display_id != display->display_id) {
    mtx_unlock(&display->display_lock);
    return CONFIG_DISPLAY_OK;
  }

  struct hdmi_param p;
  if ((memcmp(&display->cur_display_mode, &display_configs[0]->mode, sizeof(display_mode_t)) &&
       get_vic(&display_configs[0]->mode, &p) != ZX_OK) ||
      (display_configs[0]->mode.v_addressable % 8)) {
    mtx_unlock(&display->display_lock);
    return CONFIG_DISPLAY_UNSUPPORTED_MODES;
  }

  bool success;
  if (display_configs[0]->layer_count != 1) {
    success = display_configs[0]->layer_count == 0;
  } else {
    uint32_t width = display_configs[0]->mode.h_addressable;
    uint32_t height = display_configs[0]->mode.v_addressable;
    primary_layer_t* layer = &display_configs[0]->layer_list[0]->cfg.primary;
    frame_t frame = {
        .x_pos = 0,
        .y_pos = 0,
        .width = width,
        .height = height,
    };
    success = display_configs[0]->layer_list[0]->type == LAYER_TYPE_PRIMARY &&
              layer->transform_mode == FRAME_TRANSFORM_IDENTITY && layer->image.width == width &&
              layer->image.height == height &&
              memcmp(&layer->dest_frame, &frame, sizeof(frame_t)) == 0 &&
              memcmp(&layer->src_frame, &frame, sizeof(frame_t)) == 0 &&
              display_configs[0]->cc_flags == 0 && layer->alpha_mode == ALPHA_DISABLE;
  }
  if (!success) {
    layer_cfg_results[0][0] = CLIENT_MERGE_BASE;
    for (unsigned i = 1; i < display_configs[0]->layer_count; i++) {
      layer_cfg_results[0][i] = CLIENT_MERGE_SRC;
    }
    layer_cfg_result_count[0] = display_configs[0]->layer_count;
  }
  mtx_unlock(&display->display_lock);
  return CONFIG_DISPLAY_OK;
}

static void vim_apply_configuration(void* ctx, const display_config_t** display_configs,
                                    size_t display_count) {
  vim2_display_t* display = static_cast<vim2_display_t*>(ctx);
  mtx_lock(&display->display_lock);

  if (display_count == 1 && display_configs[0]->layer_count) {
    if (memcmp(&display->cur_display_mode, &display_configs[0]->mode, sizeof(display_mode_t))) {
      zx_status_t status = get_vic(&display_configs[0]->mode, display->p);
      if (status != ZX_OK) {
        mtx_unlock(&display->display_lock);
        zxlogf(ERROR, "Apply with bad mode\n");
        return;
      }

      memcpy(&display->cur_display_mode, &display_configs[0]->mode, sizeof(display_mode_t));

      init_hdmi_interface(display, display->p);
      configure_osd(display, 1);
      configure_vd(display, 0);
      disable_osd(display, 0);  // OSD1 is never used and if not disabled can cover up OSD2 and VD0.
    }

    // The only way a checked configuration could now be invalid is if display was
    // unplugged. If that's the case, then the upper layers will give a new configuration
    // once they finish handling the unplug event. So just return.
    if (!display->display_attached || display_configs[0]->display_id != display->display_id) {
      mtx_unlock(&display->display_lock);
      return;
    }
    if (display_configs[0]->layer_list[0]->cfg.primary.image.pixel_format == ZX_PIXEL_FORMAT_NV12) {
      uint32_t addr =
          (uint32_t)(uint64_t)display_configs[0]->layer_list[0]->cfg.primary.image.handle;
      flip_vd(display, 0, addr);
      disable_osd(display, 1);
    } else {
      uint8_t addr;
      addr = (uint8_t)(uint64_t)display_configs[0]->layer_list[0]->cfg.primary.image.handle;
      flip_osd(display, 1, addr);
      disable_vd(display, 0);
    }
  } else {
    disable_vd(display, 0);
    disable_osd(display, 1);
  }

  mtx_unlock(&display->display_lock);
}

static zx_status_t get_sysmem_connection(void* ctx, zx_handle_t handle) {
  vim2_display_t* display = static_cast<vim2_display_t*>(ctx);
  zx::channel request_handle(handle);

  zx_status_t status = sysmem_connect(&display->sysmem, request_handle.release());
  if (status != ZX_OK) {
    DISP_ERROR("Could not connect to sysmem - status: %d\n", status);
    return status;
  }

  return ZX_OK;
}

static zx_status_t set_buffer_collection_constraints(void* ctx, const image_t* config,
                                                     uint32_t client_endpoint) {
  fuchsia_sysmem_BufferCollectionConstraints constraints = {};
  constraints.usage.display = fuchsia_sysmem_displayUsageLayer;
  constraints.has_buffer_memory_constraints = true;
  fuchsia_sysmem_BufferMemoryConstraints& buffer_constraints =
      constraints.buffer_memory_constraints;
  buffer_constraints.min_size_bytes = 0;
  buffer_constraints.max_size_bytes = 0xffffffff;
  buffer_constraints.physically_contiguous_required = true;
  buffer_constraints.secure_required = false;
  buffer_constraints.ram_domain_supported = true;
  buffer_constraints.cpu_domain_supported = false;
  buffer_constraints.heap_permitted_count = 1;
  buffer_constraints.heap_permitted[0] = fuchsia_sysmem_HeapType_SYSTEM_RAM;
  constraints.image_format_constraints_count = 1;
  fuchsia_sysmem_ImageFormatConstraints& image_constraints =
      constraints.image_format_constraints[0];
  if (config->pixel_format == ZX_PIXEL_FORMAT_NV12) {
    image_constraints.pixel_format.type = fuchsia_sysmem_PixelFormatType_NV12;
    image_constraints.pixel_format.has_format_modifier = true;
    image_constraints.pixel_format.format_modifier.value = fuchsia_sysmem_FORMAT_MODIFIER_LINEAR;
    image_constraints.color_spaces_count = 1;
    image_constraints.color_space[0].type = fuchsia_sysmem_ColorSpaceType_REC709;
  } else {
    image_constraints.pixel_format.type = fuchsia_sysmem_PixelFormatType_BGRA32;
    image_constraints.pixel_format.has_format_modifier = true;
    image_constraints.pixel_format.format_modifier.value = fuchsia_sysmem_FORMAT_MODIFIER_LINEAR;
    image_constraints.color_spaces_count = 1;
    image_constraints.color_space[0].type = fuchsia_sysmem_ColorSpaceType_SRGB;
  }
  image_constraints.min_coded_width = 0;
  image_constraints.max_coded_width = 0xffffffff;
  image_constraints.min_coded_height = 0;
  image_constraints.max_coded_height = 0xffffffff;
  image_constraints.min_bytes_per_row = 0;
  image_constraints.max_bytes_per_row = 0xffffffff;
  image_constraints.max_coded_width_times_coded_height = 0xffffffff;
  image_constraints.layers = 1;
  image_constraints.coded_width_divisor = 1;
  image_constraints.coded_height_divisor = 1;
  image_constraints.bytes_per_row_divisor = 32;
  image_constraints.start_offset_divisor = 32;
  image_constraints.display_width_divisor = 1;
  image_constraints.display_height_divisor = 1;

  zx_status_t status =
      fuchsia_sysmem_BufferCollectionSetConstraints(client_endpoint, true, &constraints);

  if (status != ZX_OK) {
    DISP_ERROR("Failed to set constraints");
    return status;
  }

  return ZX_OK;
}

static zx_status_t get_single_buffer_framebuffer(void* ctx, zx_handle_t* out_vmo,
                                                 uint32_t* out_stride) {
  return ZX_ERR_NOT_SUPPORTED;
}

static display_controller_impl_protocol_ops_t display_controller_ops = {
    .set_display_controller_interface = vim_set_display_controller_interface,
    .import_vmo_image = vim_import_vmo_image,
    .import_image = vim_import_image,
    .release_image = vim_release_image,
    .check_configuration = vim_check_configuration,
    .apply_configuration = vim_apply_configuration,
    .get_sysmem_connection = get_sysmem_connection,
    .set_buffer_collection_constraints = set_buffer_collection_constraints,
    .get_single_buffer_framebuffer = get_single_buffer_framebuffer,
};

static uint32_t get_bus_count(void* ctx) { return 1; }

static zx_status_t get_max_transfer_size(void* ctx, uint32_t bus_id, size_t* out_size) {
  *out_size = UINT32_MAX;
  return ZX_OK;
}

static zx_status_t set_bitrate(void* ctx, uint32_t bus_id, uint32_t bitrate) {
  // no-op
  return ZX_OK;
}

static zx_status_t transact(void* ctx, uint32_t bus_id, const i2c_impl_op_t* ops, size_t count) {
  vim2_display_t* display = static_cast<vim2_display_t*>(ctx);
  mtx_lock(&display->i2c_lock);

  uint8_t segment_num = 0;
  uint8_t offset = 0;
  for (unsigned i = 0; i < count; i++) {
    auto op = ops[i];

    // The HDMITX_DWC_I2CM registers are a limited interface to the i2c bus for the E-DDC
    // protocol, which is good enough for the bus this device provides.
    if (op.address == 0x30 && !op.is_read && op.data_size == 1) {
      segment_num = *((const uint8_t*)op.data_buffer);
    } else if (op.address == 0x50 && !op.is_read && op.data_size == 1) {
      offset = *((const uint8_t*)op.data_buffer);
    } else if (op.address == 0x50 && op.is_read) {
      if (op.data_size % 8 != 0) {
        mtx_unlock(&display->i2c_lock);
        return ZX_ERR_NOT_SUPPORTED;
      }

      hdmitx_writereg(display, HDMITX_DWC_I2CM_SLAVE, 0x50);
      hdmitx_writereg(display, HDMITX_DWC_I2CM_SEGADDR, 0x30);
      hdmitx_writereg(display, HDMITX_DWC_I2CM_SEGPTR, segment_num);

      for (uint32_t i = 0; i < op.data_size; i += 8) {
        hdmitx_writereg(display, HDMITX_DWC_I2CM_ADDRESS, offset);
        hdmitx_writereg(display, HDMITX_DWC_I2CM_OPERATION, 1 << 2);
        offset = static_cast<uint8_t>(offset + 8);

        uint32_t timeout = 0;
        while ((!(hdmitx_readreg(display, HDMITX_DWC_IH_I2CM_STAT0) & (1 << 1))) && (timeout < 5)) {
          usleep(1000);
          timeout++;
        }
        if (timeout == 5) {
          DISP_ERROR("HDMI DDC TimeOut\n");
          mtx_unlock(&display->i2c_lock);
          return ZX_ERR_TIMED_OUT;
        }
        usleep(1000);
        hdmitx_writereg(display, HDMITX_DWC_IH_I2CM_STAT0, 1 << 1);  // clear INT

        for (int j = 0; j < 8; j++) {
          uint32_t address = static_cast<uint32_t>(HDMITX_DWC_I2CM_READ_BUFF0 + j);
          ((uint8_t*)op.data_buffer)[i + j] =
              static_cast<uint8_t>(hdmitx_readreg(display, address));
        }
      }
    } else {
      mtx_unlock(&display->i2c_lock);
      return ZX_ERR_NOT_SUPPORTED;
    }

    if (op.stop) {
      segment_num = 0;
      offset = 0;
    }
  }

  mtx_unlock(&display->i2c_lock);
  return ZX_OK;
}

static i2c_impl_protocol_ops_t i2c_impl_ops = {
    .get_bus_count = get_bus_count,
    .get_max_transfer_size = get_max_transfer_size,
    .set_bitrate = set_bitrate,
    .transact = transact,
};

static void display_release(void* ctx) {
  vim2_display_t* display = static_cast<vim2_display_t*>(ctx);

  if (display) {
    disable_osd(display, 1);
    disable_vd(display, 0);
    release_osd(display);
    bool wait_for_vsync_shutdown = false;
    if (display->vsync_interrupt != ZX_HANDLE_INVALID) {
      zx_interrupt_trigger(display->vsync_interrupt, 0, 0);
      wait_for_vsync_shutdown = true;
    }

    bool wait_for_main_shutdown = false;
    if (display->inth != ZX_HANDLE_INVALID) {
      zx_interrupt_trigger(display->inth, 0, 0);
      wait_for_main_shutdown = true;
    }

    int res;
    if (wait_for_vsync_shutdown) {
      thrd_join(display->vsync_thread, &res);
    }

    if (wait_for_main_shutdown) {
      thrd_join(display->main_thread, &res);
    }

    gpio_release_interrupt(&display->gpio);
    display->mmio_preset.reset();
    display->mmio_hdmitx.reset();
    display->mmio_hiu.reset();
    display->mmio_vpu.reset();
    display->mmio_hdmitx_sec.reset();
    display->mmio_cbus.reset();
    zx_handle_close(display->bti);
    zx_handle_close(display->vsync_interrupt);
    zx_handle_close(display->inth);
    free(display->p);
  }
  free(display);
}

static void display_unbind(void* ctx) {
  vim2_display_t* display = static_cast<vim2_display_t*>(ctx);
  vim2_audio_shutdown(&display->audio);
  device_unbind_reply(display->mydevice);
}

zx_status_t display_get_protocol(void* ctx, uint32_t proto_id, void* protocol) {
  if (proto_id == ZX_PROTOCOL_DISPLAY_CONTROLLER_IMPL) {
    auto ops = static_cast<display_controller_impl_protocol_t*>(protocol);
    ops->ctx = ctx;
    ops->ops = &display_controller_ops;
  } else if (proto_id == ZX_PROTOCOL_I2C_IMPL) {
    auto ops = static_cast<i2c_impl_protocol_t*>(protocol);
    ops->ctx = ctx;
    ops->ops = &i2c_impl_ops;
  } else {
    return ZX_ERR_NOT_SUPPORTED;
  }
  return ZX_OK;
}

static zx_protocol_device_t main_device_proto = []() {
  zx_protocol_device_t ops = {};
  ops.version = DEVICE_OPS_VERSION;
  ops.get_protocol = display_get_protocol;
  ops.unbind = display_unbind;
  ops.release = display_release;
  return ops;
}();

static int hdmi_irq_handler(void* arg) {
  vim2_display_t* display = static_cast<vim2_display_t*>(arg);
  zx_status_t status;
  while (1) {
    status = zx_interrupt_wait(display->inth, NULL);
    if (status != ZX_OK) {
      DISP_ERROR("Waiting in Interrupt failed %d\n", status);
      return -1;
    }
    usleep(500000);
    uint8_t hpd;
    status = gpio_read(&display->gpio, &hpd);
    if (status != ZX_OK) {
      DISP_ERROR("gpio_read failed HDMI HPD\n");
      continue;
    }

    mtx_lock(&display->display_lock);

    bool display_added = false;
    added_display_args_t args;
    added_display_info_t info;
    uint64_t display_removed = INVALID_DISPLAY_ID;
    if (hpd && !display->display_attached) {
      DISP_ERROR("Display is connected\n");

      display->display_attached = true;
      memset(&display->cur_display_mode, 0, sizeof(display_mode_t));
      populate_added_display_args(display, &args);
      display_added = true;
      gpio_set_polarity(&display->gpio, GPIO_POLARITY_LOW);
    } else if (!hpd && display->display_attached) {
      DISP_ERROR("Display Disconnected!\n");
      hdmi_shutdown(display);

      display_removed = display->display_id;
      display->display_id++;
      display->display_attached = false;

      gpio_set_polarity(&display->gpio, GPIO_POLARITY_HIGH);
    }

    if (display->dc_intf.ops && (display_removed != INVALID_DISPLAY_ID || display_added)) {
      display_controller_interface_on_displays_changed(
          &display->dc_intf, &args, display_added ? 1 : 0, &display_removed,
          display_removed != INVALID_DISPLAY_ID, &info, display_added ? 1 : 0, NULL);
      if (display_added) {
        // See if we need to change output color to RGB
        if (info.is_standard_srgb_out) {
          display->output_color_format = HDMI_COLOR_FORMAT_RGB;
        } else {
          display->output_color_format = HDMI_COLOR_FORMAT_444;
        }
        display->audio_format_count = info.audio_format_count;

        display->manufacturer_name = info.manufacturer_name;
        memcpy(display->monitor_name, info.monitor_name, sizeof(info.monitor_name));
        memcpy(display->monitor_serial, info.monitor_serial, sizeof(info.monitor_serial));
        static_assert(sizeof(display->monitor_name) == sizeof(info.monitor_name), "");
        static_assert(sizeof(display->monitor_serial) == sizeof(info.monitor_serial), "");
      }
    }

    mtx_unlock(&display->display_lock);

    if (display_removed != INVALID_DISPLAY_ID) {
      vim2_audio_on_display_removed(display, display_removed);
    }

    if (display_added && info.audio_format_count) {
      vim2_audio_on_display_added(display, display->display_id);
    }
  }
}

static int vsync_thread(void* arg) {
  vim2_display_t* display = static_cast<vim2_display_t*>(arg);

  for (;;) {
    zx_status_t status;
    zx_time_t timestamp;
    status = zx_interrupt_wait(display->vsync_interrupt, &timestamp);
    if (status != ZX_OK) {
      DISP_INFO("Vsync wait failed");
      break;
    }

    mtx_lock(&display->display_lock);

    uint64_t display_id = display->display_id;
    bool attached = display->display_attached;
    uint64_t live[2] = {};
    uint32_t current_image_count = 0;
    if (display->current_image_valid) {
      live[current_image_count++] = display->current_image;
    }
    if (display->vd1_image_valid) {
      live[current_image_count++] = display->vd1_image;
    }

    if (display->dc_intf.ops && attached) {
      display_controller_interface_on_display_vsync(&display->dc_intf, display_id, timestamp, live,
                                                    current_image_count);
    }
    mtx_unlock(&display->display_lock);
  }

  return 0;
}

zx_status_t vim2_display_bind(void* ctx, zx_device_t* parent) {
  vim2_display_t* display = static_cast<vim2_display_t*>(calloc(1, sizeof(vim2_display_t)));
  if (!display) {
    DISP_ERROR("Could not allocated display structure\n");
    return ZX_ERR_NO_MEMORY;
  }

  zx_status_t status = ZX_ERR_INTERNAL;
  display->parent = parent;

  // If anything goes wrong from here on out, make sure to log the status code
  // of the error, and destroy our display object.
  auto cleanup = fbl::MakeAutoCall([&]() {
    DISP_ERROR("bind failed! %d\n", status);
    display_release(display);
  });

  composite_protocol_t composite;
  status = device_get_protocol(parent, ZX_PROTOCOL_COMPOSITE, &composite);
  if (status != ZX_OK) {
    DISP_ERROR("Could not get composite protocol\n");
    return status;
  }

  zx_device_t* components[COMPONENT_COUNT];
  size_t actual;
  composite_get_components(&composite, components, countof(components), &actual);
  if (actual != countof(components)) {
    DISP_ERROR("could not get components\n");
    return ZX_ERR_NOT_SUPPORTED;
  }

  status = device_get_protocol(components[COMPONENT_PDEV], ZX_PROTOCOL_PDEV, &display->pdev);
  if (status != ZX_OK) {
    DISP_ERROR("Could not get PDEV protocol\n");
    return status;
  }

  // Test for platform device get_board_info support.
  pdev_board_info_t board_info;
  pdev_get_board_info(&display->pdev, &board_info);
  printf("BOARD INFO: %d %d %s %d\n", board_info.vid, board_info.pid, board_info.board_name,
         board_info.board_revision);
  assert(board_info.vid == PDEV_VID_KHADAS);
  assert(board_info.pid == PDEV_PID_VIM2);
  assert(!strcmp(board_info.board_name, "vim2"));
  assert(board_info.board_revision == 1234);

  // Fetch the device info and sanity check our resource counts.
  pdev_device_info_t dev_info;
  status = pdev_get_device_info(&display->pdev, &dev_info);
  if (status != ZX_OK) {
    DISP_ERROR("Failed to fetch device info (status %d)\n", status);
    return status;
  }

  if (dev_info.mmio_count != MMIO_COUNT) {
    DISP_ERROR(
        "MMIO region count mismatch!  Expected %u regions to be supplied by board "
        "driver, but only %u were passed\n",
        MMIO_COUNT, dev_info.mmio_count);
    return status;
  }

  if (dev_info.bti_count != BTI_COUNT) {
    DISP_ERROR(
        "BTI count mismatch!  Expected %u BTIs to be supplied by board "
        "driver, but only %u were passed\n",
        BTI_COUNT, dev_info.bti_count);
    return status;
  }

  status = pdev_get_bti(&display->pdev, 0, &display->bti);
  if (status != ZX_OK) {
    DISP_ERROR("Could not get BTI handle\n");
    return status;
  }

  status = device_get_protocol(components[COMPONENT_HPD_GPIO], ZX_PROTOCOL_GPIO, &display->gpio);
  if (status != ZX_OK) {
    DISP_ERROR("Could not get Display GPIO protocol\n");
    return status;
  }

  status = device_get_protocol(components[COMPONENT_CANVAS], ZX_PROTOCOL_AMLOGIC_CANVAS,
                               &display->canvas);
  if (status != ZX_OK) {
    DISP_ERROR("Could not get Display CANVAS protocol\n");
    return status;
  }

  status = device_get_protocol(components[COMPONENT_SYSMEM], ZX_PROTOCOL_SYSMEM, &display->sysmem);
  if (status != ZX_OK) {
    DISP_ERROR("Could not get Display SYSMEM protocol\n");
    return status;
  }

  // Map all the various MMIOs
  mmio_buffer_t mmio;
  status =
      pdev_map_mmio_buffer(&display->pdev, MMIO_PRESET, ZX_CACHE_POLICY_UNCACHED_DEVICE, &mmio);
  if (status != ZX_OK) {
    DISP_ERROR("Could not map display MMIO PRESET\n");
    return status;
  }
  display->mmio_preset = ddk::MmioBuffer(mmio);

  status =
      pdev_map_mmio_buffer(&display->pdev, MMIO_HDMITX, ZX_CACHE_POLICY_UNCACHED_DEVICE, &mmio);
  if (status != ZX_OK) {
    DISP_ERROR("Could not map display MMIO HDMITX\n");
    return status;
  }
  display->mmio_hdmitx = ddk::MmioBuffer(mmio);

  status = pdev_map_mmio_buffer(&display->pdev, MMIO_HIU, ZX_CACHE_POLICY_UNCACHED_DEVICE, &mmio);
  if (status != ZX_OK) {
    DISP_ERROR("Could not map display MMIO HIU\n");
    return status;
  }
  display->mmio_hiu = ddk::MmioBuffer(mmio);

  status = pdev_map_mmio_buffer(&display->pdev, MMIO_VPU, ZX_CACHE_POLICY_UNCACHED_DEVICE, &mmio);
  if (status != ZX_OK) {
    DISP_ERROR("Could not map display MMIO VPU\n");
    return status;
  }
  display->mmio_vpu = ddk::MmioBuffer(mmio);

  status =
      pdev_map_mmio_buffer(&display->pdev, MMIO_HDMTX_SEC, ZX_CACHE_POLICY_UNCACHED_DEVICE, &mmio);
  if (status != ZX_OK) {
    DISP_ERROR("Could not map display MMIO HDMITX SEC\n");
    return status;
  }
  display->mmio_hdmitx_sec = ddk::MmioBuffer(mmio);

  status = pdev_map_mmio_buffer(&display->pdev, MMIO_CBUS, ZX_CACHE_POLICY_UNCACHED_DEVICE, &mmio);
  if (status != ZX_OK) {
    DISP_ERROR("Could not map display MMIO CBUS\n");
    return status;
  }
  display->mmio_cbus = ddk::MmioBuffer(mmio);

  status = gpio_config_in(&display->gpio, GPIO_PULL_DOWN);
  if (status != ZX_OK) {
    DISP_ERROR("gpio_config_in failed for gpio\n");
    return status;
  }

  status = gpio_get_interrupt(&display->gpio, ZX_INTERRUPT_MODE_LEVEL_HIGH, &display->inth);
  if (status != ZX_OK) {
    DISP_ERROR("gpio_get_interrupt failed for gpio\n");
    return status;
  }

  status = pdev_get_interrupt(&display->pdev, IRQ_VSYNC, 0, &display->vsync_interrupt);
  if (status != ZX_OK) {
    DISP_ERROR("Could not map vsync interrupt\n");
    return status;
  }

  status = pdev_get_interrupt(&display->pdev, IRQ_RDMA, 0, &display->rdma_interrupt);
  if (status != ZX_OK) {
    DISP_ERROR("Could not map RDMA interrupt\n");
    return status;
  }

  status = vim2_audio_create(&display->pdev, &display->audio);
  if (status != ZX_OK) {
    DISP_ERROR("Failed to create DAI controller (status %d)\n", status);
    return status;
  }

  // For some reason the vsync interrupt enable bit needs to be cleared for
  // vsync interrupts to occur at the correct rate.
  display->mmio_vpu->ClearBits32(1 << 8, VPU_VIU_MISC_CTRL0);

  // Setup RDMA
  status = setup_rdma(display);
  if (status != ZX_OK) {
    DISP_ERROR("Could not setup RDMA (status %d)\n", status);
    return status;
  }

  fbl::AllocChecker ac;
  display->p = new (&ac) struct hdmi_param();
  if (!ac.check()) {
    DISP_ERROR("Could not allocated hdmi param structure\n");
    DISP_ERROR("bind failed! %d\n", status);
    display_release(display);
    return status;
  }

  // initialize HDMI
  display->input_color_format = _ginput_color_format;
  display->color_depth = _gcolor_depth;
  init_hdmi_hardware(display);

  device_add_args_t add_args = {};
  add_args.version = DEVICE_ADD_ARGS_VERSION;
  add_args.name = "vim2-display";
  add_args.ctx = display;
  add_args.ops = &main_device_proto;
  add_args.props = nullptr;
  add_args.prop_count = 0;
  add_args.proto_id = ZX_PROTOCOL_DISPLAY_CONTROLLER_IMPL;
  add_args.proto_ops = &display_controller_ops;
  add_args.proxy_args = nullptr;
  add_args.flags = 0;
  add_args.client_remote = ZX_HANDLE_INVALID;

  status = device_add(display->parent, &add_args, &display->mydevice);
  if (status != ZX_OK) {
    DISP_ERROR("Could not add device\n");
    return status;
  }

  display->display_id = 1;
  display->display_attached = false;
  list_initialize(&display->imported_images);
  mtx_init(&display->display_lock, mtx_plain);
  mtx_init(&display->image_lock, mtx_plain);
  mtx_init(&display->i2c_lock, mtx_plain);

  thrd_create_with_name(&display->main_thread, hdmi_irq_handler, display, "hdmi_irq_handler");
  thrd_create_with_name(&display->vsync_thread, vsync_thread, display, "vsync_thread");
  thrd_create_with_name(&display->rdma_thread, rdma_thread, display, "rdma_thread");

  // Things went well!  Cancel our cleanup auto call
  cleanup.cancel();
  return ZX_OK;
}

zx_status_t vim2_display_configure_audio_mode(const vim2_display_t* display, uint32_t N,
                                              uint32_t CTS, uint32_t frame_rate,
                                              uint32_t bits_per_sample) {
  ZX_DEBUG_ASSERT(display != NULL);

  if ((N > 0xFFFFF) || (CTS > 0xFFFFF) || (bits_per_sample < 16) || (bits_per_sample > 24)) {
    vim2_display_disable_audio(display);
    return ZX_ERR_INVALID_ARGS;
  }

  ZX_DEBUG_ASSERT(display != NULL);
  hdmitx_writereg(display, HDMITX_DWC_AUD_CONF0, 0u);   // Make sure that I2S is deselected
  hdmitx_writereg(display, HDMITX_DWC_AUD_SPDIF2, 0u);  // Deselect SPDIF

  // Select non-HBR linear PCM, as well as the proper number of bits per sample.
  hdmitx_writereg(display, HDMITX_DWC_AUD_SPDIF1, bits_per_sample);

  // Set the N/CTS parameters using DesignWare's atomic update sequence
  //
  // For details, refer to...
  // DesignWare Cores HDMI Transmitter Controler Databook v2.12a Sections 6.8.3 Table 6-282
  hdmitx_writereg(display, HDMITX_DWC_AUD_N3,
                  ((N >> AUD_N3_N_START_BIT) & AUD_N3_N_MASK) | AUD_N3_ATOMIC_WRITE);
  hw_wmb();
  hdmitx_writereg(display, HDMITX_DWC_AUD_CTS3,
                  ((CTS >> AUD_CTS3_CTS_START_BIT) & AUD_CTS3_CTS_MASK));
  hdmitx_writereg(display, HDMITX_DWC_AUD_CTS2,
                  ((CTS >> AUD_CTS2_CTS_START_BIT) & AUD_CTS2_CTS_MASK));
  hdmitx_writereg(display, HDMITX_DWC_AUD_CTS1,
                  ((CTS >> AUD_CTS1_CTS_START_BIT) & AUD_CTS1_CTS_MASK));
  hdmitx_writereg(display, HDMITX_DWC_AUD_N3,
                  ((N >> AUD_N3_N_START_BIT) & AUD_N3_N_MASK) | AUD_N3_ATOMIC_WRITE);
  hdmitx_writereg(display, HDMITX_DWC_AUD_N2, ((N >> AUD_N2_N_START_BIT) & AUD_N2_N_MASK));
  hw_wmb();
  hdmitx_writereg(display, HDMITX_DWC_AUD_N1, ((N >> AUD_N1_N_START_BIT) & AUD_N1_N_MASK));

  // Select SPDIF data stream 0 (coming from the AmLogic section of the S912)
  hdmitx_writereg(display, HDMITX_DWC_AUD_SPDIF2, AUD_SPDIF2_ENB_ISPDIFDATA0);

  // Reset the SPDIF FIFO
  hdmitx_writereg(display, HDMITX_DWC_AUD_SPDIF0, AUD_SPDIF0_SW_FIFO_RESET);
  hw_wmb();

  // Now, as required, reset the SPDIF sampler.
  // See Section 6.9.1 of the DW HDMT TX controller databook
  hdmitx_writereg(display, HDMITX_DWC_MC_SWRSTZREQ, 0xEF);
  hw_wmb();

  // Set up the audio infoframe.  Refer to the follow specifications for
  // details about how to do this.
  //
  // DesignWare Cores HDMI Transmitter Controler Databook v2.12a Sections 6.5.35 - 6.5.37
  // CTA-861-G Section 6.6

  uint32_t CT = 0x01;  // Coding type == LPCM
  uint32_t CC = 0x01;  // Channel count = 2
  uint32_t CA = 0x00;  // Channel allocation; currently we hardcode FL/FR

  // Sample size
  uint32_t SS;
  switch (bits_per_sample) {
    case 16:
      SS = 0x01;
      break;
    case 20:
      SS = 0x02;
      break;
    case 24:
      SS = 0x03;
      break;
    default:
      SS = 0x00;
      break;  // "refer to stream"
  }

  // Sample frequency
  uint32_t SF;
  switch (frame_rate) {
    case 32000:
      SF = 0x01;
      break;
    case 44100:
      SF = 0x02;
      break;
    case 48000:
      SF = 0x03;
      break;
    case 88200:
      SF = 0x04;
      break;
    case 96000:
      SF = 0x05;
      break;
    case 176400:
      SF = 0x06;
      break;
    case 192000:
      SF = 0x07;
      break;
    default:
      SF = 0x00;
      break;  // "refer to stream"
  }

  hdmitx_writereg(display, HDMITX_DWC_FC_AUDICONF0, (CT & 0xF) | ((CC & 0x7) << 4));
  hdmitx_writereg(display, HDMITX_DWC_FC_AUDICONF1, (SF & 0x7) | ((SS & 0x3) << 4));
  hdmitx_writereg(display, HDMITX_DWC_FC_AUDICONF2, CA);
  // Right now, we just hardcode the following...
  // LSV    : Level shift value == 0dB
  // DM_INH : Downmix inhibit == down-mixing permitted.
  // LFEPBL : LFE playback level unknown.
  hdmitx_writereg(display, HDMITX_DWC_FC_AUDICONF3, 0u);

  return ZX_OK;
}

void vim2_display_disable_audio(const vim2_display_t* display) {
  ZX_DEBUG_ASSERT(display != NULL);
  hdmitx_writereg(display, HDMITX_DWC_AUD_CONF0, 0u);   // Deselect I2S
  hdmitx_writereg(display, HDMITX_DWC_AUD_SPDIF2, 0u);  // Deselect SPDIF

  // Set the N/CTS parameters to 0 using DesignWare's atomic update sequence
  hdmitx_writereg(display, HDMITX_DWC_AUD_N3, 0x80);
  hdmitx_writereg(display, HDMITX_DWC_AUD_CTS3, 0u);
  hdmitx_writereg(display, HDMITX_DWC_AUD_CTS2, 0u);
  hdmitx_writereg(display, HDMITX_DWC_AUD_CTS1, 0u);
  hdmitx_writereg(display, HDMITX_DWC_AUD_N3, 0x80);
  hdmitx_writereg(display, HDMITX_DWC_AUD_N2, 0u);
  hw_wmb();
  hdmitx_writereg(display, HDMITX_DWC_AUD_N1, 0u);

  // Reset the audio info frame to defaults.
  hdmitx_writereg(display, HDMITX_DWC_FC_AUDICONF0, 0u);
  hdmitx_writereg(display, HDMITX_DWC_FC_AUDICONF1, 0u);
  hdmitx_writereg(display, HDMITX_DWC_FC_AUDICONF2, 0u);
  hdmitx_writereg(display, HDMITX_DWC_FC_AUDICONF3, 0u);
}

static constexpr zx_driver_ops_t vim2_display_driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = vim2_display_bind;
  return ops;
}();

// clang-format off
ZIRCON_DRIVER_BEGIN(vim2_display, vim2_display_driver_ops, "zircon", "0.1", 4)
  BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_COMPOSITE),
  BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_KHADAS),
  BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_VIM2),
  BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_VIM_DISPLAY),
ZIRCON_DRIVER_END(vim_2display)
