// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <zircon/device/ioctl.h>
#include <zircon/device/ioctl-wrapper.h>
#include <zircon/types.h>


// Return the root job handle
//   in: none
//   out: zx_handle_t
#define IOCTL_SYSINFO_GET_ROOT_JOB \
    IOCTL(IOCTL_KIND_GET_HANDLE, IOCTL_FAMILY_SYSINFO, 1)

// Return the root resource (with only ZX_RIGHT_ENUMERATE and ZX_RIGHT_TRANSFER)
//   in: none
//   out: zx_handle_t
#define IOCTL_SYSINFO_GET_ROOT_RESOURCE \
    IOCTL(IOCTL_KIND_GET_HANDLE, IOCTL_FAMILY_SYSINFO, 2)

// Return the hypervisor resource (with only ZX_RIGHT_TRANSFER)
//   in: none
//   out: zx_handle_t
#define IOCTL_SYSINFO_GET_HYPERVISOR_RESOURCE \
    IOCTL(IOCTL_KIND_GET_HANDLE, IOCTL_FAMILY_SYSINFO, 3)

// Return the board name for the platform we are running on
//   in: none
//   out: char[ZBI_BOARD_NAME_LEN]
#define IOCTL_SYSINFO_GET_BOARD_NAME \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_SYSINFO, 4)

// Return interrupt controller information
//   in: none
//   out: interrupt_controller_info_t
#define IOCTL_SYSINFO_GET_INTERRUPT_CONTROLLER_INFO \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_SYSINFO, 5)

#define INTERRUPT_CONTROLLER_TYPE_UNKNOWN 0
#define INTERRUPT_CONTROLLER_TYPE_APIC 1
#define INTERRUPT_CONTROLLER_TYPE_GIC_V2 2
#define INTERRUPT_CONTROLLER_TYPE_GIC_V3 3

typedef struct interrupt_controller_info_t {
    uint8_t type;
} interrupt_controller_info_t;

// ssize_t ioctl_sysinfo_get_root_job(int fd, zx_handle_t* out);
IOCTL_WRAPPER_OUT(ioctl_sysinfo_get_root_job, IOCTL_SYSINFO_GET_ROOT_JOB, zx_handle_t);

// ssize_t ioctl_sysinfo_get_root_resource(int fd, zx_handle_t* out);
IOCTL_WRAPPER_OUT(ioctl_sysinfo_get_root_resource, IOCTL_SYSINFO_GET_ROOT_RESOURCE, zx_handle_t);

// ssize_t ioctl_sysinfo_get_hypervisor_resource(int fd, zx_handle_t* out);
IOCTL_WRAPPER_OUT(ioctl_sysinfo_get_hypervisor_resource, IOCTL_SYSINFO_GET_HYPERVISOR_RESOURCE, zx_handle_t);

// ssize_t ioctl_sysinfo_get_board_name(int fd, char* out, size_t out_len);
IOCTL_WRAPPER_VAROUT(ioctl_sysinfo_get_board_name, IOCTL_SYSINFO_GET_BOARD_NAME, char);

// ssize_t ioctl_sysinfo_get_gic_version(int fd, uint8_t* out);
IOCTL_WRAPPER_OUT(ioctl_sysinfo_get_interrupt_controller_info,
                  IOCTL_SYSINFO_GET_INTERRUPT_CONTROLLER_INFO, interrupt_controller_info_t);
