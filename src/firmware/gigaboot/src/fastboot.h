// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_FIRMWARE_GIGABOOT_SRC_FASTBOOT_H_
#define SRC_FIRMWARE_GIGABOOT_SRC_FASTBOOT_H_

#define FB_SERVER_PORT 5554

#include <inttypes.h>

typedef struct {
  void *kernel_start;
  uint32_t kernel_size;
} fb_bootimg_t;

// Return type from fb_poll().
typedef enum {
  // keep calling fb_poll
  POLL = 0,
  // boot image from provided fb_bootimg_t.
  BOOT_FROM_RAM,
  // continue booting from disk.
  CONTINUE_BOOT,
} fb_poll_next_action;

// |img| will be populated with the image to boot when return value is BOOT_FROM_RAM.
fb_poll_next_action fb_poll(fb_bootimg_t *img);
void fb_recv(void *data, size_t len, void *saddr, uint16_t sport, uint16_t dport);

#endif  // SRC_FIRMWARE_GIGABOOT_SRC_FASTBOOT_H_
