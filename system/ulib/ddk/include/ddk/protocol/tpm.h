// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/driver.h>
#include <ddk/ioctl.h>
#include <stddef.h>
#include <sys/types.h>

/* TPM IOCTL commands */
#define IOCTL_TPM_SAVE_STATE \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_TPM, 0)

typedef struct mx_protocol_tpm {
    ssize_t (*get_random)(mx_device_t *dev, void *buf, size_t count);
    mx_status_t (*save_state)(mx_device_t *dev);
} mx_protocol_tpm_t;
