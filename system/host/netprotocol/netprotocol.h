// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <arpa/inet.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdint.h>

#include <magenta/boot/netboot.h>

#define MAXSIZE 1024

typedef struct {
    struct nbmsg_t hdr;
    uint8_t data[MAXSIZE];
} msg;

typedef enum device_state {
  UNKNOWN, OFFLINE, DEVICE, BOOTLOADER,
} device_state_t;

typedef struct device_info {
  char nodename[MAX_NODENAME_LENGTH];
  char inet6_addr_s[INET6_ADDRSTRLEN];
  struct sockaddr_in6 inet6_addr;
  device_state_t state;
  uint32_t bootloader_version;
  uint16_t bootloader_port;
} device_info_t;

// Handle netboot command line options.
int netboot_handle_getopt(int argc, char * const *argv);
int netboot_handle_custom_getopt(int argc, char * const *argv,
                                 const struct option *custom_opts,
                                 size_t num_custom_opts,
                                 bool (*opt_callback)(int ch, int argc, char * const *argv));
void netboot_usage(void);

// Returns whether discovery should continue or not.
typedef bool (*on_device_cb)(device_info_t* device, void* cookie);
int netboot_discover(unsigned port, const char* ifname, on_device_cb callback, void* cookie);

int netboot_open(const char* hostname, const char* ifname,
                 struct sockaddr_in6* addr, bool make_connection);

int netboot_txn(int s, msg* in, msg* out, int outlen);
