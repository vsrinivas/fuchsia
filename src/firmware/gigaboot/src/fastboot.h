// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_FIRMWARE_GIGABOOT_SRC_FASTBOOT_H_
#define SRC_FIRMWARE_GIGABOOT_SRC_FASTBOOT_H_

#define FB_SERVER_PORT 5554

#include <inttypes.h>

typedef struct {
  void * kernel_start;
  uint32_t kernel_size;
} fb_bootimg_t;

// fb_poll returns 1 when img has been populated with a ZBI to boot in memory.
int fb_poll(fb_bootimg_t *img);
void fb_recv(void *data, size_t len, void *saddr, uint16_t sport, uint16_t dport);

#endif  // SRC_FIRMWARE_GIGABOOT_SRC_FASTBOOT_H_
