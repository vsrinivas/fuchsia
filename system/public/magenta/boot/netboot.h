// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stddef.h>
#include <stdint.h>

// clang-format off

#define BOOTLOADER_VERSION "0.6.0"

#define NB_MAGIC              0xAA774217
#define NB_DEBUGLOG_MAGIC     0xAEAE1123

#define NB_SERVER_PORT        33330
#define NB_ADVERT_PORT        33331
#define NB_CMD_PORT_START     33332
#define NB_CMD_PORT_END       33339
#define NB_TFTP_OUTGOING_PORT 33340
#define NB_TFTP_INCOMING_PORT 33341


#define NB_COMMAND           1   // arg=0, data=command
#define NB_SEND_FILE         2   // arg=size, data=filename
#define NB_DATA              3   // arg=offset, data=data
#define NB_BOOT              4   // arg=0
#define NB_QUERY             5   // arg=0, data=hostname (or "*")
#define NB_SHELL_CMD         6   // arg=0, data=command string
#define NB_OPEN              7   // arg=O_RDONLY|O_WRONLY, data=filename
#define NB_READ              8   // arg=blocknum
#define NB_WRITE             9   // arg=blocknum, data=data
#define NB_CLOSE             10  // arg=0
#define NB_LAST_DATA         11  // arg=offset, data=data

#define NB_ACK                0 // arg=0 or -err, NB_READ: data=data
#define NB_FILE_RECEIVED      0x70000001 // arg=size

#define NB_ADVERTISE          0x77777777

#define NB_ERROR              0x80000000
#define NB_ERROR_BAD_CMD      0x80000001
#define NB_ERROR_BAD_PARAM    0x80000002
#define NB_ERROR_TOO_LARGE    0x80000003
#define NB_ERROR_BAD_FILE     0x80000004

#define NB_VERSION_1_0  0x0001000
#define NB_VERSION_1_1  0x0001010
#define NB_VERSION_1_2  0x0001020
#define NB_VERSION_CURRENT NB_VERSION_1_2

#define NB_FILENAME_PREFIX "<<netboot>>"
#define NB_KERNEL_FILENAME NB_FILENAME_PREFIX "kernel.bin"
#define NB_RAMDISK_FILENAME NB_FILENAME_PREFIX "ramdisk.bin"
#define NB_CMDLINE_FILENAME NB_FILENAME_PREFIX "cmdline"

typedef struct nbmsg_t {
    uint32_t magic;
    uint32_t cookie;
    uint32_t cmd;
    uint32_t arg;
    uint8_t  data[0];
} nbmsg;

typedef struct nbfile_t {
    uint8_t* data;
    size_t size; // max size of buffer
    size_t offset; // write pointer
} nbfile;

int netboot_init(const char* nodename);
const char* netboot_nodename(void);
int netboot_poll(void);
void netboot_close(void);

// Ask for a buffer suitable to put the file /name/ in
// Return NULL to indicate /name/ is not wanted.
nbfile* netboot_get_buffer(const char* name, size_t size);

#define DEBUGLOG_PORT         33337
#define DEBUGLOG_ACK_PORT     33338

#define MAX_LOG_DATA 1216
#define MAX_NODENAME_LENGTH 64

typedef struct logpacket {
    uint32_t magic;
    uint32_t seqno;
    char nodename[MAX_NODENAME_LENGTH];
    char data[MAX_LOG_DATA];
} logpacket_t;
