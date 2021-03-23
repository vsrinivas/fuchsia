// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_FIRMWARE_GIGABOOT_SRC_BOOTIMG_H_
#define SRC_FIRMWARE_GIGABOOT_SRC_BOOTIMG_H_

#define BOOT_MAGIC "ANDROID!"
#define BOOT_MAGIC_SIZE 8
#define BOOT_NAME_SIZE 16
#define BOOT_ARGS_SIZE 512
#define BOOT_EXTRA_ARGS_SIZE 1024

#include <inttypes.h>

// See https://android.googlesource.com/platform/system/tools/mkbootimg/+/refs/heads/master/include/bootimg/bootimg.h
// for a full explanation of these structs and their fields.

typedef struct {
    uint8_t magic[BOOT_MAGIC_SIZE];
    uint32_t kernel_size;
    uint32_t kernel_addr;
    uint32_t ramdisk_size;
    uint32_t ramdisk_addr;
    uint32_t second_size;
    uint32_t second_addr;
    uint32_t tags_addr;
    uint32_t page_size;
    uint32_t header_version;
    uint32_t os_version;
    uint8_t name[BOOT_NAME_SIZE];
    uint8_t cmdline[BOOT_ARGS_SIZE];
    uint32_t id[8];
    uint8_t extra_cmdline[BOOT_EXTRA_ARGS_SIZE];
} __attribute((packed)) boot_img_hdr_v0;

typedef struct {
    uint8_t magic[BOOT_MAGIC_SIZE];
    uint32_t kernel_size;
    uint32_t kernel_addr;
    uint32_t ramdisk_size;
    uint32_t ramdisk_addr;
    uint32_t second_size;
    uint32_t second_addr;
    uint32_t tags_addr;
    uint32_t page_size;
    uint32_t header_version;
    uint32_t os_version;
    uint8_t name[BOOT_NAME_SIZE];
    uint8_t cmdline[BOOT_ARGS_SIZE];
    uint32_t id[8];
    uint8_t extra_cmdline[BOOT_EXTRA_ARGS_SIZE];

    uint32_t recovery_dtbo_size;
    uint64_t recovery_dtbo_offset;
    uint32_t header_size;
} __attribute((packed)) boot_img_hdr_v1;

typedef struct {
    uint8_t magic[BOOT_MAGIC_SIZE];
    uint32_t kernel_size;
    uint32_t kernel_addr;
    uint32_t ramdisk_size;
    uint32_t ramdisk_addr;
    uint32_t second_size;
    uint32_t second_addr;
    uint32_t tags_addr;
    uint32_t page_size;
    uint32_t header_version;
    uint32_t os_version;
    uint8_t name[BOOT_NAME_SIZE];
    uint8_t cmdline[BOOT_ARGS_SIZE];
    uint32_t id[8];
    uint8_t extra_cmdline[BOOT_EXTRA_ARGS_SIZE];

    uint32_t recovery_dtbo_size;
    uint64_t recovery_dtbo_offset;
    uint32_t header_size;

    uint32_t dtb_size;
    uint64_t dtb_addr;
} __attribute((packed)) boot_img_hdr_v2;

typedef struct {
    uint8_t magic[BOOT_MAGIC_SIZE];
    uint32_t kernel_size;
    uint32_t ramdisk_size;
    uint32_t os_version;
    uint32_t header_size;
    uint32_t reserved[4];
    uint32_t header_version;
    uint8_t cmdline[BOOT_ARGS_SIZE + BOOT_EXTRA_ARGS_SIZE];
} __attribute((packed)) boot_img_hdr_v3;

typedef struct {
    uint8_t magic[BOOT_MAGIC_SIZE];
    uint32_t kernel_size;
    uint32_t ramdisk_size;
    uint32_t os_version;
    uint32_t header_size;
    uint32_t reserved[4];
    uint32_t header_version;
    uint8_t cmdline[BOOT_ARGS_SIZE + BOOT_EXTRA_ARGS_SIZE];

    uint32_t signature_size;
} __attribute((packed)) boot_img_hdr_v4;

uint32_t validate_bootimg(void *bootimg);
uint32_t get_kernel_size(void *bootimg, uint32_t hdr_version);
uint32_t get_page_size(void *bootimg, uint32_t hdr_version);

#endif  // SRC_FIRMWARE_GIGABOOT_SRC_BOOTIMG_H_
