// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <bootimg.h>
#include <stdio.h>
#include <string.h>

uint32_t validate_bootimg(void *bootimg) {
  boot_img_hdr_v0 *hdr = (boot_img_hdr_v0 *) bootimg;
  if (!strncmp((char *) (hdr->magic), BOOT_MAGIC, BOOT_MAGIC_SIZE)) {
    return hdr->header_version;
  }
  return (uint32_t)(-1);
 }

uint32_t get_kernel_size(void *bootimg, uint32_t hdr_version) {
  switch (hdr_version) {
    case 0:
        return ((boot_img_hdr_v0 *) bootimg)->kernel_size;
    case 1:
        return ((boot_img_hdr_v1 *) bootimg)->kernel_size;
    case 2:
        return ((boot_img_hdr_v2 *) bootimg)->kernel_size;
    case 3:
       return ((boot_img_hdr_v3 *) bootimg)->kernel_size;
    case 4:
       return ((boot_img_hdr_v4 *) bootimg)->kernel_size;
    default:
      return (uint32_t)(-1);
  }
}

uint32_t get_page_size(void *bootimg, uint32_t hdr_version) {
  switch (hdr_version) {
    case 0:
        return ((boot_img_hdr_v0 *) bootimg)->page_size;
    case 1:
        return ((boot_img_hdr_v1 *) bootimg)->page_size;
    case 2:
        return ((boot_img_hdr_v2 *) bootimg)->page_size;
    case 3:
    case 4:
        // Versions 3 and 4 fix the page size at 4096, see:
        // https://android.googlesource.com/platform/system/tools/mkbootimg/+/refs/heads/master/include/bootimg/bootimg.h#219
       return 4096;
    default:
      return (uint32_t)(-1);
  }}
