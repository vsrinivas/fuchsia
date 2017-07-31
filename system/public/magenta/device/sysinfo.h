// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <magenta/device/ioctl.h>
#include <magenta/device/ioctl-wrapper.h>
#include <magenta/types.h>


// Return the root job handle
//   in: none
//   out: mx_handle_t
#define IOCTL_SYSINFO_GET_ROOT_JOB \
    IOCTL(IOCTL_KIND_GET_HANDLE, IOCTL_FAMILY_SYSINFO, 1)

// Return the root resource (with only MX_RIGHT_ENUMERATE and MX_RIGHT_TRANSFER)
//   in: none
//   out: mx_handle_t
#define IOCTL_SYSINFO_GET_ROOT_RESOURCE \
    IOCTL(IOCTL_KIND_GET_HANDLE, IOCTL_FAMILY_SYSINFO, 2)

// Return the hypervisor resource (with only MX_RIGHT_TRANSFER)
//   in: none
//   out: mx_handle_t
#define IOCTL_SYSINFO_GET_HYPERVISOR_RESOURCE \
    IOCTL(IOCTL_KIND_GET_HANDLE, IOCTL_FAMILY_SYSINFO, 3)

// ssize_t ioctl_sysinfo_get_root_job(int fd, mx_handle_t* out);
IOCTL_WRAPPER_OUT(ioctl_sysinfo_get_root_job, IOCTL_SYSINFO_GET_ROOT_JOB, mx_handle_t);

// ssize_t ioctl_sysinfo_get_root_resource(int fd, mx_handle_t* out);
IOCTL_WRAPPER_OUT(ioctl_sysinfo_get_root_resource, IOCTL_SYSINFO_GET_ROOT_RESOURCE, mx_handle_t);

// ssize_t ioctl_sysinfo_get_hypervisor_resource(int fd, mx_handle_t* out);
IOCTL_WRAPPER_OUT(ioctl_sysinfo_get_hypervisor_resource, IOCTL_SYSINFO_GET_HYPERVISOR_RESOURCE, mx_handle_t);
