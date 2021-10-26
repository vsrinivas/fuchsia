// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_FIRMWARE_GIGABOOT_SRC_FASTBOOT_H_
#define SRC_FIRMWARE_GIGABOOT_SRC_FASTBOOT_H_

#define FB_SERVER_PORT 5554

#include <inttypes.h>
#include <stdlib.h>
#include <zircon/compiler.h>

#include "inet6.h"

__BEGIN_CDECLS

typedef struct {
  void *kernel_start;
  uint32_t kernel_size;
} fb_bootimg_t;

// Return type from fb_poll().
typedef enum {
  POLL = 0,
  BOOT_FROM_RAM,
  CONTINUE_BOOT,
} fb_poll_next_action;

// Polls the fastboot main loop.
//
// Calls the network poll function and fills |img| if we are booting from RAM.
// This should be called as often as possible while in fastboot mode to avoid
// losing any packets.
//
// Args:
//   img: populated with the image to boot when return value is BOOT_FROM_RAM.
//
// Returns:
//   POLL if the caller should call this function again in the next loop.
//   BOOT_FROM_RAM if the caller should boot the kernel in |img|.
//   CONTINUE_BOOT if the caller should boot from disk.
fb_poll_next_action fb_poll(fb_bootimg_t *img);

// Processes an incoming fastboot UDP packet.
//
// Args:
//   data: UDP packet data.
//   len: UDP packet data len.
//   saddr: UDP sender IP address.
//   sport: UDP sender port.
void fb_recv(void *data, size_t len, const void *saddr, uint16_t sport);

// Sets replacements for UDP functions used by fastboot.
//
// This allows us to test the fastboot UDP logic without having to mock out
// all the corresponding EFI network functionality.
//
// Note that this permanently replaces the functions; tests should call this
// again with NULL pointers to restore defaults when finished.
typedef void (*fb_udp_poll_func_t)(void);
typedef int (*fb_udp6_send_func_t)(const void *data, size_t len, const ip6_addr *daddr,
                                   uint16_t dport, uint16_t sport);
void fb_set_udp_functions_for_testing(fb_udp_poll_func_t poll_func, fb_udp6_send_func_t send_func);

__END_CDECLS

#endif  // SRC_FIRMWARE_GIGABOOT_SRC_FASTBOOT_H_
