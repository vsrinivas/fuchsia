// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/device/ioctl.h>
#include <magenta/device/ioctl-wrapper.h>

#define IOCTL_BLOCK_GET_SIZE \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_BLOCK, 1)
#define IOCTL_BLOCK_GET_BLOCKSIZE \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_BLOCK, 2)
#define IOCTL_BLOCK_GET_GUID \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_BLOCK, 3)
#define IOCTL_BLOCK_GET_NAME \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_BLOCK, 4)
#define IOCTL_BLOCK_RR_PART \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_BLOCK, 5)

// ssize_t ioctl_block_get_size(int fd, uint64_t* out);
IOCTL_WRAPPER_OUT(ioctl_block_get_size, IOCTL_BLOCK_GET_SIZE, uint64_t);

// ssize_t ioctl_block_get_blocksize(int fd, uint64_t* out);
IOCTL_WRAPPER_OUT(ioctl_block_get_blocksize, IOCTL_BLOCK_GET_BLOCKSIZE, uint64_t);

// ssize_t ioctl_block_get_guid(int fd, char* out, size_t out_len);
IOCTL_WRAPPER_VAROUT(ioctl_block_get_guid, IOCTL_BLOCK_GET_GUID, char);

// ssize_t ioctl_block_get_name(int fd, char* out, size_t out_len);
IOCTL_WRAPPER_VAROUT(ioctl_block_get_name, IOCTL_BLOCK_GET_NAME, char);

// ssize_t ioctl_block_rr_part(int fd);
IOCTL_WRAPPER(ioctl_block_rr_part, IOCTL_BLOCK_RR_PART);
