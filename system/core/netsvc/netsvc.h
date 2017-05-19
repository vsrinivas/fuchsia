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
    uint32_t blocknum;
    uint32_t cookie;
    uint8_t  data[1024];
    size_t   datasize;
} netfile_state;

extern netfile_state netfile;

typedef struct netfilemsg_t {
    nbmsg   hdr;
    uint8_t data[1024];
} netfilemsg;

void netfile_open(const char* filename, uint32_t cookie, uint32_t arg,
                const ip6_addr_t* saddr, uint16_t sport, uint16_t dport);

void netfile_read(uint32_t cookie, uint32_t arg,
                  const ip6_addr_t* saddr, uint16_t sport, uint16_t dport);

void netfile_write(const char* data, size_t len, uint32_t cookie, uint32_t arg,
                   const ip6_addr_t* saddr, uint16_t sport, uint16_t dport);

void netfile_close(uint32_t cookie,
                   const ip6_addr_t* saddr, uint16_t sport, uint16_t dport);

void netboot_advertise(const char* nodename);

void netboot_recv(void* data, size_t len,
                  const ip6_addr_t* daddr, uint16_t dport,
                  const ip6_addr_t* saddr, uint16_t sport);