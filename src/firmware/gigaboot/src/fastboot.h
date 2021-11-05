// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_FIRMWARE_GIGABOOT_SRC_FASTBOOT_H_
#define SRC_FIRMWARE_GIGABOOT_SRC_FASTBOOT_H_

#define FB_SERVER_PORT 5554

#include <inttypes.h>
#include <stdbool.h>
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
  POLL = 0,       // Continue calling fb_poll().
  BOOT_FROM_RAM,  // Boot the given RAM kernel image.
  CONTINUE_BOOT,  // Continue booting normally from disk.
  REBOOT,         // Reboot the board.
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
//   REBOOT if the caller should reboot the board.
fb_poll_next_action fb_poll(fb_bootimg_t *img);

// Processes an incoming fastboot UDP packet.
//
// Args:
//   data: UDP packet data.
//   len: UDP packet data len.
//   saddr: UDP sender IP address.
//   sport: UDP sender port.
void fb_recv(void *data, size_t len, const void *saddr, uint16_t sport);

// Informs fastboot that a TCP packet has been seen.
//
// We can't run our low-level networking at the same time as the TCP driver
// since they will steal each other's packets. Instead, we run the low-level
// networking by default, and if we see an incoming TCP packet call this to
// switch fastboot into TCP mode until the session completes.
//
// This initial packet will be dropped, but a retry packet should be sent
// shortly that the TCP driver will be able to pick up. This adds about ~1s
// latency to each fastboot TCP connection, so very fast operations like
// "getvar" will probably be slower over TCP, but it's worth it because things
// like flashing will be much faster.
void fb_tcp_recv(void);

// Returns true if fastboot-over-TCP is available.
bool fb_tcp_is_available(void);

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

// Resets the fastboot TCP state.
//
// Testing code that uses globals is always a bit dicey, if this turns out to
// be a pain we may want to bundle the state all up in a passed struct instead
// to make it more explicit.
void fb_reset_tcp_state_for_testing(void);

__END_CDECLS

#endif  // SRC_FIRMWARE_GIGABOOT_SRC_FASTBOOT_H_
