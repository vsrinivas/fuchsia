// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <limits.h>
#include <stdio.h>

#include <inet6/inet6.h>

#include <atomic>

#include <zircon/boot/netboot.h>
#include <zircon/types.h>

// netfile interface
struct netfile_state {
    int fd;
    off_t offset;
    // false: Filename is the open file and final destination
    // true : Filename is final destination; open file has a magic tmp suffix
    bool needs_rename;
    char filename[PATH_MAX];
};

extern netfile_state netfile;

struct netfilemsg {
    nbmsg hdr;
    uint8_t data[1024];
};

int netfile_open(const char* filename, uint32_t arg, size_t* file_size);

ssize_t netfile_offset_read(void* data_out, off_t offset, size_t max_len);

ssize_t netfile_read(void* data_out, size_t data_sz);

ssize_t netfile_offset_write(const char* data, off_t offset, size_t length);

ssize_t netfile_write(const char* data, size_t len);

int netfile_close();

void netfile_abort_write();

// netboot interface
extern const char* nodename;
extern bool netbootloader;

void netboot_advertise(const char* nodename);

void netboot_recv(void* data, size_t len, bool is_mcast, const ip6_addr_t* daddr, uint16_t dport,
                  const ip6_addr_t* saddr, uint16_t sport);

void netboot_run_cmd(const char* cmd);

// TFTP interface
extern zx_time_t tftp_next_timeout;
extern std::atomic<bool> paving_in_progress;
extern std::atomic<int> paver_exit_code;

void tftp_recv(void* data, size_t len, const ip6_addr_t* daddr, uint16_t dport,
               const ip6_addr_t* saddr, uint16_t sport);

void tftp_timeout_expired();

bool tftp_has_pending();
void tftp_send_next();

// debuglog interface
extern zx_time_t debuglog_next_timeout;

int debuglog_init();

void debuglog_recv(void* data, size_t len, bool is_mcast);

void debuglog_timeout_expired();

// netsvc interface
void update_timeouts();
