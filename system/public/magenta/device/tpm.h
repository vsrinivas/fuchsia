// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/device/ioctl.h>
#include <magenta/device/ioctl-wrapper.h>

__BEGIN_CDECLS;

/* TPM IOCTL commands */
#define IOCTL_TPM_SAVE_STATE \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_TPM, 0)

// ssize_t ioctl_tpm_save_state(int fd);
IOCTL_WRAPPER(ioctl_tpm_save_state, IOCTL_TPM_SAVE_STATE);

__END_CDECLS;
