// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SYSROOT_ZIRCON_BOOT_NETBOOT_H_
#define SYSROOT_ZIRCON_BOOT_NETBOOT_H_

#include <stddef.h>
#include <stdint.h>
#include <zircon/types.h>

// clang-format off

#define BOOTLOADER_VERSION "0.7.22"

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
#define NB_REBOOT            12  // arg=0
#define NB_GET_ADVERT        13  // arg=0

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
#define NB_VERSION_1_3  0x0001030
#define NB_VERSION_CURRENT NB_VERSION_1_3

#define NB_FILENAME_PREFIX "<<netboot>>"
#define NB_KERNEL_FILENAME NB_FILENAME_PREFIX "kernel.bin"
#define NB_RAMDISK_FILENAME NB_FILENAME_PREFIX "ramdisk.bin"
#define NB_CMDLINE_FILENAME NB_FILENAME_PREFIX "cmdline"

#define NB_IMAGE_PREFIX "<<image>>"
#define NB_FVM_HOST_FILENAME "sparse.fvm"
#define NB_FVM_FILENAME NB_IMAGE_PREFIX NB_FVM_HOST_FILENAME
#define NB_BOOTLOADER_HOST_FILENAME "bootloader.img"
#define NB_BOOTLOADER_FILENAME NB_IMAGE_PREFIX NB_BOOTLOADER_HOST_FILENAME
// Firmware images are slightly different, as they have an optional type suffix:
//   firmware_     <- type = "" (the default)
//   firmware_foo  <- type = "foo"
#define NB_FIRMWARE_HOST_FILENAME_PREFIX "firmware_"
#define NB_FIRMWARE_FILENAME_PREFIX NB_IMAGE_PREFIX NB_FIRMWARE_HOST_FILENAME_PREFIX
#define NB_FIRMWAREA_HOST_FILENAME_PREFIX "firmwarea_"
#define NB_FIRMWAREA_FILENAME_PREFIX NB_IMAGE_PREFIX NB_FIRMWAREA_HOST_FILENAME_PREFIX
#define NB_FIRMWAREB_HOST_FILENAME_PREFIX "firmwareb_"
#define NB_FIRMWAREB_FILENAME_PREFIX NB_IMAGE_PREFIX NB_FIRMWAREB_HOST_FILENAME_PREFIX
#define NB_FIRMWARER_HOST_FILENAME_PREFIX "firmwarer_"
#define NB_FIRMWARER_FILENAME_PREFIX NB_IMAGE_PREFIX NB_FIRMWARER_HOST_FILENAME_PREFIX
#define NB_ZIRCONA_HOST_FILENAME "zircona.img"
#define NB_ZIRCONA_FILENAME NB_IMAGE_PREFIX NB_ZIRCONA_HOST_FILENAME
#define NB_ZIRCONB_HOST_FILENAME "zirconb.img"
#define NB_ZIRCONB_FILENAME NB_IMAGE_PREFIX NB_ZIRCONB_HOST_FILENAME
#define NB_ZIRCONR_HOST_FILENAME "zirconr.img"
#define NB_ZIRCONR_FILENAME NB_IMAGE_PREFIX NB_ZIRCONR_HOST_FILENAME
#define NB_VBMETAA_HOST_FILENAME "vbmetaa.img"
#define NB_VBMETAA_FILENAME NB_IMAGE_PREFIX NB_VBMETAA_HOST_FILENAME
#define NB_VBMETAB_HOST_FILENAME "vbmetab.img"
#define NB_VBMETAB_FILENAME NB_IMAGE_PREFIX NB_VBMETAB_HOST_FILENAME
#define NB_VBMETAR_HOST_FILENAME "vbmetar.img"
#define NB_VBMETAR_FILENAME NB_IMAGE_PREFIX NB_VBMETAR_HOST_FILENAME
#define NB_SSHAUTH_HOST_FILENAME "authorized_keys"
#define NB_SSHAUTH_FILENAME NB_IMAGE_PREFIX NB_SSHAUTH_HOST_FILENAME
#define NB_BOARD_NAME_HOST_FILENAME "board_name"
#define NB_BOARD_NAME_FILENAME NB_IMAGE_PREFIX NB_BOARD_NAME_HOST_FILENAME
#define NB_BOARD_REVISION_HOST_FILENAME "board_revision"
#define NB_BOARD_REVISION_FILENAME NB_IMAGE_PREFIX NB_BOARD_REVISION_HOST_FILENAME
#define NB_BOARD_INFO_HOST_FILENAME "board_info"
#define NB_BOARD_INFO_FILENAME NB_IMAGE_PREFIX NB_BOARD_INFO_HOST_FILENAME
#define NB_INIT_PARTITION_TABLES_HOST_FILENAME "init_partition_tables"
#define NB_INIT_PARTITION_TABLES_FILENAME NB_IMAGE_PREFIX NB_INIT_PARTITION_TABLES_HOST_FILENAME
#define NB_WIPE_PARTITION_TABLES_HOST_FILENAME "wipe_partition_tables"
#define NB_WIPE_PARTITION_TABLES_FILENAME NB_IMAGE_PREFIX NB_WIPE_PARTITION_TABLES_HOST_FILENAME

// Should match paver FIDL definition.
// Length does not include the '\0' terminator, so when allocating a character
// buffer to hold the type use (NB_FIRMWARE_TYPE_MAX_LENGTH  + 1).
#define NB_FIRMWARE_TYPE_MAX_LENGTH 256

typedef struct board_info {
  char board_name[ZX_MAX_NAME_LEN];
  uint32_t board_revision;
  uint8_t mac_address[8];
} board_info_t;

typedef struct modify_partition_table_info {
  // Path of block device to initialize or wipe.
  char block_device_path[ZX_MAX_NAME_LEN + 1];
} modify_partition_table_info_t;

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

#endif  // SYSROOT_ZIRCON_BOOT_NETBOOT_H_
