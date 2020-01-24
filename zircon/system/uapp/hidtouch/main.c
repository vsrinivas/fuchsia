// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fuchsia/hardware/input/c/fidl.h>
#include <lib/fdio/unsafe.h>
#include <lib/framebuffer/framebuffer.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <ddk/device.h>
#include <hid/acer12.h>
#include <hid/egalax.h>
#include <hid/eyoyo.h>
#include <hid/ft3x27.h>
#include <hid/ft5726.h>
#include <hid/paradise.h>
#include <hid/usages.h>

#define DEV_INPUT "/dev/class/input"
#define CLEAR_BTN_SIZE 50
#define I2C_HID_DEBUG 0

enum touch_panel_type {
  TOUCH_PANEL_UNKNOWN,
  TOUCH_PANEL_ACER12,
  TOUCH_PANEL_PARADISE,
  TOUCH_PANEL_PARADISEv2,
  TOUCH_PANEL_PARADISEv3,
  TOUCH_PANEL_EGALAX,
  TOUCH_PANEL_EYOYO,
  TOUCH_PANEL_FT3X27,
  TOUCH_PANEL_FT5726,
};

typedef struct display_info {
  uint32_t width;
  uint32_t height;
  uint32_t stride;
  zx_pixel_format_t format;
} display_info_t;

// Array of colors for each finger
static uint32_t colors[] = {
    0x00ff0000, 0x0000ff00, 0x000000ff, 0x00ffff00, 0x00ff00ff,
    0x0000ffff, 0x00000000, 0x00f0f0f0, 0x00f00f00, 0x000ff000,
};

static uint16_t colors16[] = {
    0x003f, 0x03c0, 0xfc00, 0xe00f, 0xeff3, 0x003f, 0x03c0, 0x1c00, 0xe000, 0xe003,
};

static bool run = false;

static void acer12_touch_dump(acer12_touch_t* rpt) {
  printf("report id: %u\n", rpt->rpt_id);
  for (int i = 0; i < 5; i++) {
    printf("finger %d\n", i);
    printf("  finger_id: %u\n", rpt->fingers[i].finger_id);
    printf("    tswitch: %u\n", acer12_finger_id_tswitch(rpt->fingers[i].finger_id));
    printf("    contact: %u\n", acer12_finger_id_contact(rpt->fingers[i].finger_id));
    printf("  width:  %u\n", rpt->fingers[i].width);
    printf("  height: %u\n", rpt->fingers[i].height);
    printf("  x:      %u\n", rpt->fingers[i].x);
    printf("  y:      %u\n", rpt->fingers[i].y);
  }
  printf("scan_time: %u\n", rpt->scan_time);
  printf("contact count: %u\n", rpt->contact_count);
}

static void ft3x27_touch_dump(ft3x27_touch_t* rpt) {
  printf("report id: %u\n", rpt->rpt_id);
  for (int i = 0; i < 5; i++) {
    printf("finger %d\n", i);
    printf("  finger_id: %u\n", rpt->fingers[i].finger_id);
    printf("    tswitch: %u\n", ft3x27_finger_id_tswitch(rpt->fingers[i].finger_id));
    printf("    contact: %u\n", ft3x27_finger_id_contact(rpt->fingers[i].finger_id));
    printf("  x:      %u\n", rpt->fingers[i].x);
    printf("  y:      %u\n", rpt->fingers[i].y);
  }
  printf("contact count: %u\n", rpt->contact_count);
}

static void paradise_touch_dump(paradise_touch_t* rpt) {
  printf("report id: %u\n", rpt->rpt_id);
  printf("pad: %#02x\n", rpt->pad);
  printf("contact count: %u\n", rpt->contact_count);
  for (int i = 0; i < 5; i++) {
    printf("finger %d\n", i);
    printf("  flags: %#02x\n", rpt->fingers[i].flags);
    printf("    tswitch: %u\n", paradise_finger_flags_tswitch(rpt->fingers[i].flags));
    printf("    confidence: %u\n", paradise_finger_flags_confidence(rpt->fingers[i].flags));
    printf("  finger_id: %u\n", rpt->fingers[i].finger_id);
    printf("  x:      %u\n", rpt->fingers[i].x);
    printf("  y:      %u\n", rpt->fingers[i].y);
  }
  printf("scan_time: %u\n", rpt->scan_time);
}

static void paradise_touch_v2_dump(paradise_touch_v2_t* rpt) {
  printf("report id: %u\n", rpt->rpt_id);
  printf("pad: %#02x\n", rpt->pad);
  printf("contact count: %u\n", rpt->contact_count);
  for (int i = 0; i < 5; i++) {
    printf("finger %d\n", i);
    printf("  flags: %#02x\n", rpt->fingers[i].flags);
    printf("    tswitch: %u\n", paradise_finger_flags_tswitch(rpt->fingers[i].flags));
    printf("    confidence: %u\n", paradise_finger_flags_confidence(rpt->fingers[i].flags));
    printf("  finger_id: %u\n", rpt->fingers[i].finger_id);
    printf("  width:  %u\n", rpt->fingers[i].width);
    printf("  height: %u\n", rpt->fingers[i].height);
    printf("  x:      %u\n", rpt->fingers[i].x);
    printf("  y:      %u\n", rpt->fingers[i].y);
  }
  printf("scan_time: %u\n", rpt->scan_time);
}

static void egalax_touch_dump(egalax_touch_t* rpt) {
  printf("report id: %u\n", rpt->report_id);
  printf("pad: %02x\n", egalax_pad(rpt->button_pad));
  printf("device supports one contact\n");
  printf("  finger down: %u\n", egalax_pressed_flags(rpt->button_pad));
  printf("    x: %u\n", rpt->x);
  printf("    y: %u\n", rpt->y);
}

static void eyoyo_touch_dump(acer12_touch_t* rpt) {
  printf("report id: %u\n", rpt->rpt_id);
  for (int i = 0; i < 5; i++) {
    printf("finger %d\n", i);
    printf("  finger_id: %u\n", rpt->fingers[i].finger_id);
    printf("    tswitch: %u\n", acer12_finger_id_tswitch(rpt->fingers[i].finger_id));
    printf("    contact: %u\n", acer12_finger_id_contact(rpt->fingers[i].finger_id));
    printf("  width:  %u\n", rpt->fingers[i].width);
    printf("  height: %u\n", rpt->fingers[i].height);
    printf("  x:      %u\n", rpt->fingers[i].x);
    printf("  y:      %u\n", rpt->fingers[i].y);
  }
  printf("scan_time: %u\n", rpt->scan_time);
  printf("contact count: %u\n", rpt->contact_count);
}

static uint32_t scale32(uint32_t z, uint32_t screen_dim, uint32_t rpt_dim) {
  return (z * screen_dim) / rpt_dim;
}

static void draw_points(uint32_t* pixels, uint32_t color, uint32_t x, uint32_t y, uint8_t width,
                        uint8_t height, uint32_t fbwidth, uint32_t fbheight) {
  uint32_t xrad = (width + 1) / 2;
  uint32_t yrad = (height + 1) / 2;

  uint32_t xmin = (xrad > x) ? 0 : x - xrad;
  uint32_t xmax = (xrad > fbwidth - x) ? fbwidth : x + xrad;
  uint32_t ymin = (yrad > y) ? 0 : y - yrad;
  uint32_t ymax = (yrad > fbheight - y) ? fbheight : y + yrad;

  for (uint32_t px = xmin; px < xmax; px++) {
    for (uint32_t py = ymin; py < ymax; py++) {
      *(pixels + py * fbwidth + px) = color;
    }
  }
}
static uint8_t is_exit(uint32_t x, uint32_t y, display_info_t* info) {
  return (((y + CLEAR_BTN_SIZE) > info->height) && (x < CLEAR_BTN_SIZE));
}

static void draw_points16(uint32_t* pixels, uint16_t color, uint32_t x, uint32_t y, uint8_t width,
                          uint8_t height, uint32_t fbwidth, uint32_t fbheight) {
  uint16_t* pixels16 = (uint16_t*)pixels;
  uint32_t xrad = (width + 1) / 2;
  uint32_t yrad = (height + 1) / 2;

  uint32_t xmin = (xrad > x) ? 0 : x - xrad;
  uint32_t xmax = (xrad > fbwidth - x) ? fbwidth : x + xrad;
  uint32_t ymin = (yrad > y) ? 0 : y - yrad;
  uint32_t ymax = (yrad > fbheight - y) ? fbheight : y + yrad;

  for (uint32_t px = xmin; px < xmax; px++) {
    for (uint32_t py = ymin; py < ymax; py++) {
      pixels16[py * fbwidth + px] = color;
    }
  }
}

static uint32_t get_color(uint8_t c) { return colors[c]; }

static uint32_t get_color16(uint8_t c) { return colors16[c]; }

static void clear_screen(void* buf, display_info_t* info) {
  memset(buf, 0xff, ZX_PIXEL_FORMAT_BYTES(info->format) * info->stride * info->height);
  if (ZX_PIXEL_FORMAT_BYTES(info->format) == 4) {
    draw_points((uint32_t*)buf, 0xff00ff, info->stride - (CLEAR_BTN_SIZE / 2), (CLEAR_BTN_SIZE / 2),
                CLEAR_BTN_SIZE, CLEAR_BTN_SIZE, info->stride, info->height);
    draw_points((uint32_t*)buf, 0x0000ff, (CLEAR_BTN_SIZE / 2), info->height - (CLEAR_BTN_SIZE / 2),
                CLEAR_BTN_SIZE, CLEAR_BTN_SIZE, info->stride, info->height);
  } else if (ZX_PIXEL_FORMAT_BYTES(info->format) == 2) {
    draw_points16((uint32_t*)buf, 0xf00f, info->stride - (CLEAR_BTN_SIZE / 2), (CLEAR_BTN_SIZE / 2),
                  CLEAR_BTN_SIZE, CLEAR_BTN_SIZE, info->stride, info->height);
    draw_points16((uint32_t*)buf, 0x001f, (CLEAR_BTN_SIZE / 2), info->height - (CLEAR_BTN_SIZE / 2),
                  CLEAR_BTN_SIZE, CLEAR_BTN_SIZE, info->stride, info->height);
  }
}

static void process_acer12_touchscreen_input(void* buf, size_t len, uint32_t* pixels,
                                             display_info_t* info) {
  acer12_touch_t* rpt = buf;
  if (len < sizeof(*rpt)) {
    printf("bad report size: %zd < %zd\n", len, sizeof(*rpt));
    return;
  }
#if I2C_HID_DEBUG
  acer12_touch_dump(rpt);
#endif
  for (uint8_t c = 0; c < 5; c++) {
    if (!acer12_finger_id_tswitch(rpt->fingers[c].finger_id % 10))
      continue;
    uint32_t x = scale32(rpt->fingers[c].x, info->width, ACER12_X_MAX);
    uint32_t y = scale32(rpt->fingers[c].y, info->height, ACER12_Y_MAX);
    uint32_t width = 2 * rpt->fingers[c].width;
    uint32_t height = 2 * rpt->fingers[c].height;
    uint32_t color = get_color(acer12_finger_id_contact(rpt->fingers[c].finger_id));
    draw_points(pixels, color, x, y, width, height, info->stride, info->height);
  }

  if (acer12_finger_id_tswitch(rpt->fingers[0].finger_id)) {
    uint32_t x = scale32(rpt->fingers[0].x, info->width, ACER12_X_MAX);
    uint32_t y = scale32(rpt->fingers[0].y, info->height, ACER12_Y_MAX);
    if (x + CLEAR_BTN_SIZE > info->width && y < CLEAR_BTN_SIZE) {
      clear_screen(pixels, info);
    }
    run = !is_exit(x, y, info);
  }
}

static void process_ft3x27_ft5726_touchscreen_input(void* buf, size_t len, uint32_t* pixels,
                                                    display_info_t* info) {
  ft3x27_touch_t* rpt = buf;
  if (len < sizeof(*rpt)) {
    printf("bad report size: %zd < %zd\n", len, sizeof(*rpt));
    return;
  }
#if I2C_HID_DEBUG
  ft3x27_touch_dump(rpt);
#endif
  for (uint8_t c = 0; c < 5; c++) {
    if (!ft3x27_finger_id_tswitch(rpt->fingers[c].finger_id))
      continue;
    uint32_t x = scale32(rpt->fingers[c].x, info->width, FT3X27_X_MAX);
    uint32_t y = scale32(rpt->fingers[c].y, info->height, FT3X27_Y_MAX);
    uint32_t width = 10;   // 2 * rpt->fingers[c].width;
    uint32_t height = 10;  // 2 * rpt->fingers[c].height;
    uint16_t color = get_color16(ft3x27_finger_id_contact(rpt->fingers[c].finger_id));
    draw_points16(pixels, color, x, y, width, height, info->stride, info->height);
  }

  if (ft3x27_finger_id_tswitch(rpt->fingers[0].finger_id)) {
    uint32_t x = scale32(rpt->fingers[0].x, info->width, FT3X27_X_MAX);
    uint32_t y = scale32(rpt->fingers[0].y, info->height, FT3X27_Y_MAX);
    if (x + CLEAR_BTN_SIZE > info->width && y < CLEAR_BTN_SIZE) {
      clear_screen(pixels, info);
    }
    run = !is_exit(x, y, info);
  }
}

static void process_egalax_touchscreen_input(void* buf, size_t len, uint32_t* pixels,
                                             display_info_t* info) {
  egalax_touch_t* rpt = buf;
  if (len < sizeof(*rpt)) {
    printf("bad report size: %zd < %zd\n", len, sizeof(*rpt));
    return;
  }
#if I2C_HID_DEBUG
  egalax_touch_dump(rpt);
#endif
  if (!egalax_pressed_flags(rpt->button_pad)) {
    uint32_t x = scale32(rpt->x, info->width, EGALAX_X_MAX);
    uint32_t y = scale32(rpt->y, info->height, EGALAX_Y_MAX);
    uint32_t width = 5;
    uint32_t height = 5;
    uint32_t color = get_color(1);
    draw_points(pixels, color, x, y, width, height, info->stride, info->height);
  } else {
    uint32_t x = scale32(rpt->x, info->width, EGALAX_X_MAX);
    uint32_t y = scale32(rpt->y, info->height, EGALAX_Y_MAX);
    if (x + CLEAR_BTN_SIZE > info->width && y < CLEAR_BTN_SIZE) {
      clear_screen(pixels, info);
    }
    run = !is_exit(x, y, info);
  }
}

static void process_eyoyo_touchscreen_input(void* buf, size_t len, uint32_t* pixels,
                                            display_info_t* info) {
  eyoyo_touch_t* rpt = buf;
  if (len < sizeof(*rpt)) {
    printf("bad report size: %zd < %zd\n", len, sizeof(*rpt));
    return;
  }
#if I2C_HID_DEBUG
  eyoyo_touch_dump(rpt);
#endif

  for (uint8_t c = 0; c < 10; c++) {
    if (!eyoyo_finger_id_tswitch(rpt->fingers[c].finger_id))
      continue;
    uint32_t x = scale32(rpt->fingers[c].x, info->width, EYOYO_X_MAX);
    uint32_t y = scale32(rpt->fingers[c].y, info->height, EYOYO_Y_MAX);
    uint32_t width = 10;
    uint32_t height = 10;
    uint32_t color = get_color(eyoyo_finger_id_contact(rpt->fingers[c].finger_id));
    draw_points(pixels, color, x, y, width, height, info->stride, info->height);
  }

  if (eyoyo_finger_id_tswitch(rpt->fingers[0].finger_id)) {
    uint32_t x = scale32(rpt->fingers[0].x, info->width, FT3X27_X_MAX);
    uint32_t y = scale32(rpt->fingers[0].y, info->height, FT3X27_Y_MAX);
    if (x + CLEAR_BTN_SIZE > info->width && y < CLEAR_BTN_SIZE) {
      clear_screen(pixels, info);
    }
    run = !is_exit(x, y, info);
  }
}

static void process_paradise_touchscreen_input(void* buf, size_t len, uint32_t* pixels,
                                               display_info_t* info) {
  paradise_touch_t* rpt = buf;
  if (len < sizeof(*rpt)) {
    printf("bad report size: %zd < %zd\n", len, sizeof(*rpt));
    return;
  }
#if I2C_HID_DEBUG
  paradise_touch_dump(rpt);
#endif
  for (uint8_t c = 0; c < 5; c++) {
    if (!paradise_finger_flags_tswitch(rpt->fingers[c].flags))
      continue;
    uint32_t x = scale32(rpt->fingers[c].x, info->width, PARADISE_X_MAX);
    uint32_t y = scale32(rpt->fingers[c].y, info->height, PARADISE_Y_MAX);
    uint32_t width = 10;
    uint32_t height = 10;
    uint32_t color = get_color(c);
    draw_points(pixels, color, x, y, width, height, info->stride, info->height);
  }

  if (paradise_finger_flags_tswitch(rpt->fingers[0].flags)) {
    uint32_t x = scale32(rpt->fingers[0].x, info->width, PARADISE_X_MAX);
    uint32_t y = scale32(rpt->fingers[0].y, info->height, PARADISE_Y_MAX);
    if (x + CLEAR_BTN_SIZE > info->width && y < CLEAR_BTN_SIZE) {
      clear_screen(pixels, info);
    }
    run = !is_exit(x, y, info);
  }
}

static void process_paradise_touchscreen_v2_input(void* buf, size_t len, uint32_t* pixels,
                                                  display_info_t* info) {
  paradise_touch_v2_t* rpt = buf;
  if (len < sizeof(*rpt)) {
    printf("bad report size: %zd < %zd\n", len, sizeof(*rpt));
    return;
  }
#if I2C_HID_DEBUG
  paradise_touch_v2_dump(rpt);
#endif
  for (uint8_t c = 0; c < 5; c++) {
    if (!paradise_finger_flags_tswitch(rpt->fingers[c].flags))
      continue;
    uint32_t x = scale32(rpt->fingers[c].x, info->width, PARADISE_X_MAX);
    uint32_t y = scale32(rpt->fingers[c].y, info->height, PARADISE_Y_MAX);
    uint32_t width = 2 * rpt->fingers[c].width;
    uint32_t height = 2 * rpt->fingers[c].height;
    uint32_t color = get_color(c);
    draw_points(pixels, color, x, y, width, height, info->stride, info->height);
  }

  if (paradise_finger_flags_tswitch(rpt->fingers[0].flags)) {
    uint32_t x = scale32(rpt->fingers[0].x, info->width, PARADISE_X_MAX);
    uint32_t y = scale32(rpt->fingers[0].y, info->height, PARADISE_Y_MAX);
    if (x + CLEAR_BTN_SIZE > info->width && y < CLEAR_BTN_SIZE) {
      clear_screen(pixels, info);
    }
    run = !is_exit(x, y, info);
  }
}

static void process_acer12_stylus_input(void* buf, size_t len, uint32_t* pixels,
                                        display_info_t* info) {
  acer12_stylus_t* rpt = buf;
  if (len < sizeof(*rpt)) {
    printf("bad report size: %zd < %zd\n", len, sizeof(*rpt));
    return;
  }
  // Don't draw for out of range or hover with no switches.
  if (!rpt->status || rpt->status == ACER12_STYLUS_STATUS_INRANGE)
    return;

  uint32_t x = scale32(rpt->x, info->width, ACER12_STYLUS_X_MAX);
  uint32_t y = scale32(rpt->y, info->height, ACER12_STYLUS_Y_MAX);
  // Pressing the clear button requires contact (not just hover).
  if (acer12_stylus_status_tswitch(rpt->status)) {
    if (x + CLEAR_BTN_SIZE > info->width && y < CLEAR_BTN_SIZE) {
      clear_screen(pixels, info);
      return;
    }
    run = !is_exit(x, y, info);
  }
  uint32_t size, color;
  size = acer12_stylus_status_tswitch(rpt->status) ? rpt->pressure >> 4 : 4;
  switch (rpt->status) {
    case 3:  // in_range | tip_switch
      color = get_color(0);
      break;
    case 5:  // in_range | barrel_switch
      color = get_color(1);
      break;
    case 7:  // in_range | tip_switch | barrel_switch
      color = get_color(4);
      break;
    case 9:  // in_range | invert
      color = get_color(5);
      break;
    case 17:  // in_range | erase (== tip_switch | invert)
      color = 0x00ffffff;
      size = 32;  // fixed size eraser
      break;
    default:
      printf("unknown rpt->status=%u\n", rpt->status);
      color = get_color(6);
      break;
  }

  draw_points(pixels, color, x, y, size, size, info->stride, info->height);
}

int main(int argc, char* argv[]) {
  const char* err;
  zx_status_t status = fb_bind(true, &err);
  if (status != ZX_OK) {
    printf("failed to open framebuffer: %d (%s)\n", status, err);
    return -1;
  }

  int ret;
  uint8_t* rpt_desc = NULL;
  int touchfd = -1;
  zx_handle_t touchsvc = ZX_HANDLE_INVALID;
  fdio_t* touchio = NULL;

  display_info_t info;
  fb_get_config(&info.width, &info.height, &info.stride, &info.format);

  zx_handle_t vmo = fb_get_single_buffer();

  printf("format = %d\n", info.format);
  printf("width = %d\n", info.width);
  printf("height = %d\n", info.height);
  printf("stride = %d\n", info.stride);

  size_t size = info.stride * ZX_PIXEL_FORMAT_BYTES(info.format) * info.height;
  uintptr_t fbo;
  status =
      zx_vmar_map(zx_vmar_root_self(), ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, 0, vmo, 0, size, &fbo);
  if (status != ZX_OK) {
    printf("couldn't map fb: %d\n", status);
    ret = -1;
    goto cleanup;
  }

  uint32_t* pixels32 = (uint32_t*)fbo;

  clear_screen((void*)fbo, &info);
  zx_cache_flush(pixels32, size, ZX_CACHE_FLUSH_DATA);

  // Scan /dev/class/input to find the touchscreen
  struct dirent* de;
  DIR* dir = opendir(DEV_INPUT);
  if (!dir) {
    printf("failed to open %s: %d\n", DEV_INPUT, errno);
    ret = -1;
    goto cleanup;
  }

  uint16_t rpt_desc_len = 0;
  enum touch_panel_type panel = TOUCH_PANEL_UNKNOWN;
  while ((de = readdir(dir)) != NULL) {
    char devname[128];

    if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) {
      continue;
    }

    snprintf(devname, sizeof(devname), "%s/%s", DEV_INPUT, de->d_name);
    touchfd = open(devname, O_RDONLY);
    if (touchfd < 0) {
      printf("failed to open %s: %d\n", devname, errno);
      continue;
    }

    touchio = fdio_unsafe_fd_to_io(touchfd);
    if (touchio == NULL) {
      printf("failed to convert to io\n");
      close(touchfd);
      touchfd = -1;
      continue;
    }
    touchsvc = fdio_unsafe_borrow_channel(touchio);

    status = fuchsia_hardware_input_DeviceGetReportDescSize(touchsvc, &rpt_desc_len);
    if (status != ZX_OK) {
      printf("failed to get report descriptor length for %s: %d\n", devname, status);
      goto next_node;
    }

    rpt_desc = malloc(rpt_desc_len);
    if (rpt_desc == NULL) {
      printf("no memory!\n");
      exit(-1);
    }

    size_t actual;
    status = fuchsia_hardware_input_DeviceGetReportDesc(touchsvc, rpt_desc, rpt_desc_len, &actual);
    if (status != ZX_OK) {
      printf("failed to get report descriptor for %s: %d\n", devname, status);
      goto next_node;
    }
    if (actual != rpt_desc_len) {
      printf("mismatch in desc len: %u versus %zu\n", rpt_desc_len, actual);
      goto next_node;
    }

    if (is_acer12_touch_report_desc(rpt_desc, rpt_desc_len)) {
      panel = TOUCH_PANEL_ACER12;
      // Found the touchscreen
      printf("touchscreen: %s\n", devname);
      break;
    }

    if (is_paradise_touch_report_desc(rpt_desc, rpt_desc_len)) {
      panel = TOUCH_PANEL_PARADISE;
      // Found the touchscreen
      printf("touchscreen: %s\n", devname);
      break;
    }

    if (is_paradise_touch_v2_report_desc(rpt_desc, rpt_desc_len)) {
      panel = TOUCH_PANEL_PARADISEv2;
      // Found the touchscreen
      printf("touchscreen: %s\n", devname);
      break;
    }

    if (is_paradise_touch_v3_report_desc(rpt_desc, rpt_desc_len)) {
      panel = TOUCH_PANEL_PARADISEv3;
      // Found the touchscreen
      printf("touchscreen: %s\n", devname);
      break;
    }

    if (is_egalax_touchscreen_report_desc(rpt_desc, rpt_desc_len)) {
      panel = TOUCH_PANEL_EGALAX;
      printf("touchscreen: %s is egalax\n", devname);
      break;
    }

    if (is_eyoyo_touch_report_desc(rpt_desc, rpt_desc_len)) {
      panel = TOUCH_PANEL_EYOYO;
      printf("touchscreen: %s is eyoyo\n", devname);
      break;
    }

    if (is_ft3x27_touch_report_desc(rpt_desc, rpt_desc_len)) {
      panel = TOUCH_PANEL_FT3X27;
      printf("touchscreen: %s is ft3x27\n", devname);
      break;
    }

    if (is_ft5726_touch_report_desc(rpt_desc, rpt_desc_len)) {
      panel = TOUCH_PANEL_FT5726;
      printf("touchscreen: %s is ft5726\n", devname);
      break;
    }

  next_node:
    rpt_desc_len = 0;

    if (rpt_desc != NULL) {
      free(rpt_desc);
      rpt_desc = NULL;
    }

    if (touchfd >= 0) {
      touchsvc = ZX_HANDLE_INVALID;
      fdio_unsafe_release(touchio);
      close(touchfd);
      touchfd = -1;
    }
  }
  closedir(dir);

  if (touchfd < 0) {
    printf("could not find a touchscreen!\n");
    ret = -1;
    goto cleanup;
  }
  assert(rpt_desc_len > 0);
  assert(rpt_desc);

  uint16_t max_rpt_sz = 0;
  status = fuchsia_hardware_input_DeviceGetMaxInputReportSize(touchsvc, &max_rpt_sz);
  if (status != ZX_OK) {
    printf("failed to get max report size: %d\n", status);
    ret = -1;
    goto cleanup;
  }
  printf("Max report size is %u\n", max_rpt_sz);

  zx_handle_t report_event = ZX_OK;
  zx_status_t out_status = ZX_OK;
  status = fuchsia_hardware_input_DeviceGetReportsEvent(touchsvc, &out_status, &report_event);
  if (status != ZX_OK || out_status != ZX_OK) {
    printf("failed to get reports event: %d %d\n", status, out_status);
    ret = -1;
    goto cleanup;
  }

  run = true;
  uint8_t report_data[fuchsia_hardware_input_MAX_REPORT_LEN] = {};
  size_t report_data_size = 0;
  size_t report_data_used = 0;
  while (run) {
    status = fuchsia_hardware_input_DeviceReadReports(touchsvc, &out_status, report_data,
                                                      sizeof(report_data), &report_data_size);
    if (status != ZX_OK || out_status != ZX_OK) {
      if (out_status == ZX_ERR_SHOULD_WAIT) {
        status = zx_object_wait_one(report_event, DEV_STATE_READABLE, ZX_TIME_INFINITE, NULL);
        continue;
      }
      printf("touchscreen read error: %d %d\n", status, out_status);
      break;
    }
    report_data_used = 0;
    while (report_data_used < report_data_size) {
      uint8_t* buf = report_data + report_data_used;
      ssize_t r = report_data_size - report_data_used;
      if (panel == TOUCH_PANEL_ACER12) {
        if (*(uint8_t*)buf == ACER12_RPT_ID_TOUCH) {
          process_acer12_touchscreen_input(buf, r, pixels32, &info);
          report_data_used += sizeof(acer12_touch_t);
        } else if (*(uint8_t*)buf == ACER12_RPT_ID_STYLUS) {
          process_acer12_stylus_input(buf, r, pixels32, &info);
          report_data_used += sizeof(acer12_stylus_t);
        }
      } else if (panel == TOUCH_PANEL_PARADISE) {
        if (*(uint8_t*)buf == PARADISE_RPT_ID_TOUCH) {
          process_paradise_touchscreen_input(buf, r, pixels32, &info);
          report_data_used += sizeof(paradise_touch_t);
        }
      } else if (panel == TOUCH_PANEL_PARADISEv2) {
        if (*(uint8_t*)buf == PARADISE_RPT_ID_TOUCH) {
          process_paradise_touchscreen_v2_input(buf, r, pixels32, &info);
          report_data_used += sizeof(paradise_touch_v2_t);
        }
      } else if (panel == TOUCH_PANEL_PARADISEv3) {
        if (*(uint8_t*)buf == PARADISE_RPT_ID_TOUCH) {
          process_paradise_touchscreen_input(buf, r, pixels32, &info);
          report_data_used += sizeof(paradise_touch_t);
        }
      } else if (panel == TOUCH_PANEL_EGALAX) {
        if (*(uint8_t*)buf == EGALAX_RPT_ID_TOUCH) {
          process_egalax_touchscreen_input(buf, r, pixels32, &info);
          report_data_used += sizeof(egalax_touch_t);
        }
      } else if (panel == TOUCH_PANEL_EYOYO) {
        if (*(uint8_t*)buf == EYOYO_RPT_ID_TOUCH) {
          process_eyoyo_touchscreen_input(buf, r, pixels32, &info);
          report_data_used += sizeof(eyoyo_touch_t);
        }
      } else if (panel == TOUCH_PANEL_FT3X27) {
        if (*(uint8_t*)buf == FT3X27_RPT_ID_TOUCH) {
          process_ft3x27_ft5726_touchscreen_input(buf, r, pixels32, &info);
          report_data_used += sizeof(ft3x27_touch_t);
        }
      } else if (panel == TOUCH_PANEL_FT5726) {
        if (*(uint8_t*)buf == FT5726_RPT_ID_TOUCH) {
          process_ft3x27_ft5726_touchscreen_input(buf, r, pixels32, &info);
          report_data_used += sizeof(ft3x27_touch_t);
        }
      }
    }
    zx_cache_flush(pixels32, size, ZX_CACHE_FLUSH_DATA);
  }
  memset(pixels32, 0x00, ZX_PIXEL_FORMAT_BYTES(info.format) * info.stride * info.height);
  zx_cache_flush(pixels32, size, ZX_CACHE_FLUSH_DATA);

  ret = 0;
cleanup:
  free(rpt_desc);
  if (touchio != NULL) {
    fdio_unsafe_release(touchio);
  }
  close(touchfd);
  _zx_vmar_unmap(zx_vmar_root_self(), fbo, size);
  fb_release();
  return ret;
}
