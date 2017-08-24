// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "acpi.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <launchpad/launchpad.h>

#include <acpisvc/simple.h>
#include <magenta/processargs.h>
#include <magenta/syscalls.h>
#include <mxio/io.h>
#include <mxio/util.h>

#include "devmgr.h"
#include "devhost.h"

static acpi_handle_t acpi_root;

void devhost_acpi_set_rpc(mx_handle_t handle) {
    acpi_handle_init(&acpi_root, handle);
}

void devhost_acpi_poweroff(void) {
    acpi_s_state_transition(&acpi_root, ACPI_S_STATE_S5);
    mx_debug_send_command(get_root_resource(), "poweroff", sizeof("poweroff"));
}

void devhost_acpi_reboot(void) {
    acpi_s_state_transition(&acpi_root, ACPI_S_STATE_REBOOT);
    mx_debug_send_command(get_root_resource(), "reboot", sizeof("reboot"));
}

