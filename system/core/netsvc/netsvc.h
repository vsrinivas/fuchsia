// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdio.h>
#include <limits.h>

#include <inet6/inet6.h>

#include <magenta/boot/netboot.h>

typedef struct netfile_state_t {
    int      fd;
    // false: Filename is the open file and final destination
    // true : Filename is final destination; open file has a magic tmp suffix
    bool     needs_rename;
    char     filename[PATH_MAX];
} netfile_state;

extern netfile_state netfile;
extern const char* nodename;
extern bool netbootloader;

typedef struct netfilemsg_t {
    nbmsg   hdr;
    uint8_t data[1024];
} netfilemsg;

int netfile_open(const char* filename, uint32_t arg);

int netfile_read(void* data_out, size_t data_sz);

int netfile_write(const char* data, size_t len);

int netfile_close(void);

void netboot_advertise(const char* nodename);

void netboot_recv(void* data, size_t len, bool is_mcast,
                  const ip6_addr_t* daddr, uint16_t dport,
                  const ip6_addr_t* saddr, uint16_t sport);

void netboot_run_cmd(const char* cmd);
