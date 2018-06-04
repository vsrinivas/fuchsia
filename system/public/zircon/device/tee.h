// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <zircon/device/ioctl-wrapper.h>
#include <zircon/device/ioctl.h>
#include <zircon/types.h>

__BEGIN_CDECLS

#define IOCTL_TEE_GET_DESCRIPTION \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_TEE, 0)

typedef struct tee_revision {
    uint32_t major;
    uint32_t minor;
} tee_revision_t;

#define TEE_OS_UUID_SIZE 4

typedef struct tee_ioctl_description {
    uint32_t os_uuid[TEE_OS_UUID_SIZE];
    tee_revision_t os_revision;
    bool is_global_platform_compliant;
} tee_ioctl_description_t;

// ssize_t ioctl_tee_get_description(int fd, tee_ioctl_description_t* out);
IOCTL_WRAPPER_OUT(ioctl_tee_get_description, IOCTL_TEE_GET_DESCRIPTION, tee_ioctl_description_t);

__END_CDECLS
