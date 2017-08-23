// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

void initialize_status(const char* name, size_t size);
void update_status(size_t bytes_so_far);
int tftp_xfer(struct sockaddr_in6* addr, const char* fn, const char* name);
int netboot_xfer(struct sockaddr_in6* addr, const char* fn, const char* name);

#define DEFAULT_TFTP_BLOCK_SZ 1024
#define DEFAULT_TFTP_WIN_SZ 1024
#define DEFAULT_US_BETWEEN_PACKETS 20

extern char* appname;
extern int64_t us_between_packets;
extern bool use_filename_prefix;
extern uint16_t* tftp_block_size;
extern uint16_t* tftp_window_size;

