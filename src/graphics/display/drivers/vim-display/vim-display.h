// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DISPLAY_DRIVERS_VIM_DISPLAY_VIM_DISPLAY_H_
#define SRC_GRAPHICS_DISPLAY_DRIVERS_VIM_DISPLAY_VIM_DISPLAY_H_

#include <assert.h>
#include <lib/mmio/mmio.h>
#include <lib/zx/channel.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zircon/listnode.h>
#include <zircon/pixelformat.h>

#include <optional>

#include <ddk/debug.h>
#include <ddk/protocol/amlogiccanvas.h>
#include <ddk/protocol/display/controller.h>
#include <ddk/protocol/gpio.h>
#include <ddk/protocol/sysmem.h>

#include "edid.h"
#include "vim-audio.h"
#include "vpu.h"

__BEGIN_CDECLS

#define DISP_ERROR(fmt, ...) zxlogf(ERROR, "[%s %d]" fmt, __func__, __LINE__, ##__VA_ARGS__)
#define DISP_INFO(fmt, ...) zxlogf(INFO, "[%s %d]" fmt, __func__, __LINE__, ##__VA_ARGS__)
#define DISP_SPEW(fmt, ...) zxlogf(SPEW, "[%s %d]" fmt, __func__, __LINE__, ##__VA_ARGS__)
#define DISP_TRACE zxlogf(INFO, "[%s %d]\n", __func__, __LINE__)

#define NUM_CANVAS_ENTRIES 256
#define CANVAS_BYTE_STRIDE 32

// From uBoot source
#define VFIFO2VD_TO_HDMI_LATENCY 2
#define EDID_BUF_SIZE 256

#define MAX_RDMA_CHANNELS 3

// Should match display_irqs table in board driver
enum {
  IRQ_VSYNC,
  IRQ_RDMA,
};

// MMIO indices (based on vim2_display_mmios)
enum {
  MMIO_PRESET = 0,
  MMIO_HDMITX,
  MMIO_HIU,
  MMIO_VPU,
  MMIO_HDMTX_SEC,
  MMIO_DMC,
  MMIO_CBUS,
  MMIO_AUD_OUT,
  MMIO_COUNT  // Must be the final entry
};

// BTI indices (based on vim2_display_btis)
enum {
  BTI_DISPLAY = 0,
  BTI_AUDIO,
  BTI_COUNT  // Must be the final entry
};

typedef struct vim2_display {
  zx_device_t* zxdev;
  pdev_protocol_t pdev;
  zx_device_t* parent;
  zx_device_t* mydevice;
  zx_handle_t bti;
  zx_handle_t inth;

  gpio_protocol_t gpio;
  amlogic_canvas_protocol_t canvas;
  sysmem_protocol_t sysmem;

  thrd_t main_thread;
  thrd_t vsync_thread;

  // RDMA IRQ thread
  thrd_t rdma_thread;

  // Lock for general display state, in particular display_id.
  mtx_t display_lock;
  // Lock for imported images.
  mtx_t image_lock;
  mtx_t i2c_lock;

  // TODO(stevensd): This can race if this is changed right after
  // vsync but before the interrupt is handled.
  bool current_image_valid;
  uint8_t current_image;
  bool vd1_image_valid;
  uint32_t vd1_image;

  std::optional<ddk::MmioBuffer> mmio_preset;
  std::optional<ddk::MmioBuffer> mmio_hdmitx;
  std::optional<ddk::MmioBuffer> mmio_hiu;
  std::optional<ddk::MmioBuffer> mmio_vpu;
  std::optional<ddk::MmioBuffer> mmio_hdmitx_sec;
  std::optional<ddk::MmioBuffer> mmio_dmc;
  std::optional<ddk::MmioBuffer> mmio_cbus;

  zx_handle_t vsync_interrupt;
  zx_handle_t rdma_interrupt;
  RdmaContainer rdma_container;

  bool display_attached;
  // The current display id (if display_attached), or the next display id
  uint64_t display_id;
  const char* manufacturer_name;
  char monitor_name[14];
  char monitor_serial[14];

  uint8_t input_color_format;
  uint8_t output_color_format;
  uint8_t color_depth;

  struct hdmi_param* p;
  display_mode_t cur_display_mode;

  display_controller_interface_protocol_t dc_intf;
  list_node_t imported_images;

  // A reference to the object which controls the VIM2 DAIs used to feed audio
  // into the HDMI stream.
  vim2_audio_t* audio;
  uint32_t audio_format_count;
} vim2_display_t;

void disable_vd(vim2_display_t* display, uint32_t vd_index);
void configure_vd(vim2_display_t* display, uint32_t vd_index);
void flip_vd(vim2_display_t* display, uint32_t vd_index, uint32_t index);
zx_status_t display_get_protocol(void* ctx, uint32_t proto_id, void* protocol);

void disable_osd(vim2_display_t* display, uint32_t osd_index);
zx_status_t configure_osd(vim2_display_t* display, uint32_t osd_index);
void flip_osd(vim2_display_t* display, uint32_t osd_index, uint8_t idx);
void osd_debug_dump_register_all(vim2_display_t* display);
void osd_dump(vim2_display_t* display);
void release_osd(vim2_display_t* display);
zx_status_t setup_rdma(vim2_display_t* display);
int rdma_thread(void* arg);
zx_status_t get_preferred_res(vim2_display_t* display, uint16_t edid_buf_size);
struct hdmi_param** get_supported_formats(void);

// TODO(johngro) : eliminate the need for these hooks if/when we start to
// support composite device drivers and can separate the DAI driver from the
// HDMI driver (which is currently playing the role of codec driver)
//
// TODO(johngro) : add any info needed to properly set up the audio info-frame.
zx_status_t vim2_display_configure_audio_mode(const vim2_display_t* display, uint32_t N,
                                              uint32_t CTS, uint32_t frame_rate,
                                              uint32_t bits_per_sample);
void vim2_display_disable_audio(const vim2_display_t* display);

__END_CDECLS

#endif  // SRC_GRAPHICS_DISPLAY_DRIVERS_VIM_DISPLAY_VIM_DISPLAY_H_
